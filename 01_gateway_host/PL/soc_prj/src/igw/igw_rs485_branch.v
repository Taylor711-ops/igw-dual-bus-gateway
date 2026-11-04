// AXI4-Lite GP0 branch: 0x80400000..0x80400fff -> RS485.
// All other full 33-bit addresses go to the existing video AXI Matrix.
// One outstanding read and one outstanding write, independently.
// AW and W may arrive in either order; route remains pinned until response.
module igw_rs485_branch (
    input wire aclk, aresetn,
    input wire RS485_RX0_3V3,
    input wire CAN_RX_OBSERVE,
    output wire RS485_TX0_3V3, RS485_DE0_3V3,
    input wire [32:0] s_axi_awaddr,
    input wire [2:0] s_axi_awprot,
    input wire s_axi_awvalid,
    output wire s_axi_awready,
    input wire [31:0] s_axi_wdata,
    input wire [3:0] s_axi_wstrb,
    input wire s_axi_wvalid,
    output wire s_axi_wready,
    output wire [1:0] s_axi_bresp,
    output wire s_axi_bvalid,
    input wire s_axi_bready,
    input wire [32:0] s_axi_araddr,
    input wire [2:0] s_axi_arprot,
    input wire s_axi_arvalid,
    output wire s_axi_arready,
    output wire [31:0] s_axi_rdata,
    output wire [1:0] s_axi_rresp,
    output wire s_axi_rvalid,
    input wire s_axi_rready,
    output wire [32:0] m_axi_awaddr,
    output wire [2:0] m_axi_awprot,
    output wire m_axi_awvalid,
    input wire m_axi_awready,
    output wire [31:0] m_axi_wdata,
    output wire [3:0] m_axi_wstrb,
    output wire m_axi_wvalid,
    input wire m_axi_wready,
    input wire [1:0] m_axi_bresp,
    input wire m_axi_bvalid,
    output wire m_axi_bready,
    output wire [32:0] m_axi_araddr,
    output wire [2:0] m_axi_arprot,
    output wire m_axi_arvalid,
    input wire m_axi_arready,
    input wire [31:0] m_axi_rdata,
    input wire [1:0] m_axi_rresp,
    input wire m_axi_rvalid,
    output wire m_axi_rready
);
    // GP0 control and RS485 share PS 50 MHz; no AXI clock-domain crossing.
    reg [1:0] reset_sync;
    always @(posedge aclk or negedge aresetn)
        if (!aresetn) reset_sync <= 0;
        else reset_sync <= {reset_sync[0],1'b1};
    wire core_resetn = reset_sync[1];
    reg aw_hold, w_hold, wr_active, aw_sent, w_sent, wr_rs;
    reg [32:0] aw_addr;
    reg [2:0] aw_prot;
    reg [31:0] w_data;
    reg [3:0] w_strb;
    reg rd_active, ar_sent, rd_rs;
    reg [32:0] ar_addr;
    reg [2:0] ar_prot;
    wire p_awready, p_wready, p_bvalid, p_arready, p_rvalid;
    wire [1:0] p_bresp, p_rresp;
    wire [31:0] p_rdata;
    wire wr_can=wr_rs && aw_addr[11:8]==4'h8;
    wire rd_can=rd_rs && ar_addr[11:8]==4'h8;
    wire c_awready,c_wready,c_bvalid,c_arready,c_rvalid;
    wire [1:0] c_bresp,c_rresp;
    wire [31:0] c_rdata;
    wire chosen_awready = wr_rs ? (wr_can ? c_awready : p_awready) : m_axi_awready;
    wire chosen_wready = wr_rs ? (wr_can ? c_wready : p_wready) : m_axi_wready;
    wire chosen_arready = rd_rs ? (rd_can ? c_arready : p_arready) : m_axi_arready;
    wire aw_go = wr_active && !aw_sent;
    wire w_go = wr_active && !w_sent;
    wire b_go = wr_active && aw_sent && w_sent;
    wire ar_go = rd_active && !ar_sent;
    wire r_go = rd_active && ar_sent;
    assign s_axi_awready = core_resetn && !aw_hold;
    assign s_axi_wready = core_resetn && !w_hold;
    assign s_axi_arready = core_resetn && !rd_active;
    assign s_axi_bvalid = b_go && (wr_rs ? (wr_can ? c_bvalid : p_bvalid) : m_axi_bvalid);
    assign s_axi_bresp = wr_rs ? (wr_can ? c_bresp : p_bresp) : m_axi_bresp;
    assign s_axi_rvalid = r_go && (rd_rs ? (rd_can ? c_rvalid : p_rvalid) : m_axi_rvalid);
    assign s_axi_rresp = rd_rs ? (rd_can ? c_rresp : p_rresp) : m_axi_rresp;
    assign s_axi_rdata = rd_rs ? (rd_can ? c_rdata : p_rdata) : m_axi_rdata;
    assign m_axi_awaddr = aw_addr;
    assign m_axi_awprot = aw_prot;
    assign m_axi_awvalid = aw_go && !wr_rs;
    assign m_axi_wdata = w_data;
    assign m_axi_wstrb = w_strb;
    assign m_axi_wvalid = w_go && !wr_rs;
    assign m_axi_bready = b_go && !wr_rs && s_axi_bready;
    assign m_axi_araddr = ar_addr;
    assign m_axi_arprot = ar_prot;
    assign m_axi_arvalid = ar_go && !rd_rs;
    assign m_axi_rready = r_go && !rd_rs && s_axi_rready;
    always @(posedge aclk or negedge core_resetn) begin
        if (!core_resetn) begin
            aw_hold<=0; w_hold<=0; wr_active<=0; aw_sent<=0; w_sent<=0; wr_rs<=0;
            aw_addr<=0; aw_prot<=0; w_data<=0; w_strb<=0;
            rd_active<=0; ar_sent<=0; rd_rs<=0; ar_addr<=0; ar_prot<=0;
        end else begin
            if (s_axi_awvalid && s_axi_awready) begin
                aw_hold<=1; aw_addr<=s_axi_awaddr; aw_prot<=s_axi_awprot;
                wr_rs <= s_axi_awaddr[32:12]==21'h80400;
            end
            if (s_axi_wvalid && s_axi_wready) begin
                w_hold<=1; w_data<=s_axi_wdata; w_strb<=s_axi_wstrb;
            end
            if (!wr_active && aw_hold && w_hold) begin
                wr_active<=1; aw_sent<=0; w_sent<=0;
            end
            if (aw_go && chosen_awready) aw_sent<=1;
            if (w_go && chosen_wready) w_sent<=1;
            if (s_axi_bvalid && s_axi_bready) begin
                aw_hold<=0; w_hold<=0; wr_active<=0; aw_sent<=0; w_sent<=0;
            end
            if (s_axi_arvalid && s_axi_arready) begin
                rd_active<=1; ar_sent<=0; ar_addr<=s_axi_araddr; ar_prot<=s_axi_arprot;
                rd_rs <= s_axi_araddr[32:12]==21'h80400;
            end
            if (ar_go && chosen_arready) ar_sent<=1;
            if (s_axi_rvalid && s_axi_rready) begin rd_active<=0; ar_sent<=0; end
        end
    end
    // IRQ is intentionally not routed into the PS interrupt concat yet.
    // Initial Linux bring-up uses the existing polling ABI.
    wire unused_irq;
    igw_rs485_axi #(.CLOCK_HZ(50000000), .DEFAULT_BAUD(9600)) peripheral (
        .aclk(aclk), .aresetn(core_resetn),
        .RS485_RX0_3V3(RS485_RX0_3V3), .RS485_TX0_3V3(RS485_TX0_3V3),
        .RS485_DE0_3V3(RS485_DE0_3V3), .irq(unused_irq),
        .s_axi_awaddr(aw_addr[11:0]), .s_axi_awprot(aw_prot),
        .s_axi_awvalid(aw_go && wr_rs && !wr_can), .s_axi_awready(p_awready),
        .s_axi_wdata(w_data), .s_axi_wstrb(w_strb),
        .s_axi_wvalid(w_go && wr_rs && !wr_can), .s_axi_wready(p_wready),
        .s_axi_bresp(p_bresp), .s_axi_bvalid(p_bvalid),
        .s_axi_bready(b_go && wr_rs && !wr_can && s_axi_bready),
        .s_axi_araddr(ar_addr[11:0]), .s_axi_arprot(ar_prot),
        .s_axi_arvalid(ar_go && rd_rs && !rd_can && !rd_can), .s_axi_arready(p_arready),
        .s_axi_rdata(p_rdata), .s_axi_rresp(p_rresp), .s_axi_rvalid(p_rvalid),
        .s_axi_rready(r_go && rd_rs && !rd_can && s_axi_rready)
    );
    // Passive CAN tap shares the 50 MHz domain. PS retains TX/ACK ownership.
    igw_can_observer_axi observer (
        .aclk(aclk),.aresetn(core_resetn),.can_rx(CAN_RX_OBSERVE),
        .s_axi_awaddr(aw_addr[7:0]),.s_axi_awvalid(aw_go && wr_can),.s_axi_awready(c_awready),
        .s_axi_wdata(w_data),.s_axi_wstrb(w_strb),.s_axi_wvalid(w_go && wr_can),.s_axi_wready(c_wready),
        .s_axi_bresp(c_bresp),.s_axi_bvalid(c_bvalid),.s_axi_bready(b_go && wr_can && s_axi_bready),
        .s_axi_araddr(ar_addr[7:0]),.s_axi_arvalid(ar_go && rd_can),.s_axi_arready(c_arready),
        .s_axi_rdata(c_rdata),.s_axi_rresp(c_rresp),.s_axi_rvalid(c_rvalid),.s_axi_rready(r_go && rd_can && s_axi_rready)
    );
endmodule
