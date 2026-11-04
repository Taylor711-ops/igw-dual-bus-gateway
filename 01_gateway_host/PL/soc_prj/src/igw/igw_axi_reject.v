// R15: explicit AXI-Lite termination for removed/unmapped GP0 peripherals.
// No registers, no DMA and no successful emulation of removed video devices.
module igw_axi_reject (
    input wire aclk, aresetn,
    input wire [32:0] s_axi_awaddr,
    input wire [2:0] s_axi_awprot,
    input wire s_axi_awvalid,
    output wire s_axi_awready,
    input wire [31:0] s_axi_wdata,
    input wire [3:0] s_axi_wstrb,
    input wire s_axi_wvalid,
    output wire s_axi_wready,
    output wire [1:0] s_axi_bresp,
    output reg s_axi_bvalid,
    input wire s_axi_bready,
    input wire [32:0] s_axi_araddr,
    input wire [2:0] s_axi_arprot,
    input wire s_axi_arvalid,
    output wire s_axi_arready,
    output wire [31:0] s_axi_rdata,
    output wire [1:0] s_axi_rresp,
    output reg s_axi_rvalid,
    input wire s_axi_rready
);
    reg aw_seen, w_seen;
    assign s_axi_awready = aresetn && !aw_seen && !s_axi_bvalid;
    assign s_axi_wready  = aresetn && !w_seen && !s_axi_bvalid;
    assign s_axi_arready = aresetn && !s_axi_rvalid;
    assign s_axi_bresp = 2'b11; // DECERR
    assign s_axi_rresp = 2'b11;
    assign s_axi_rdata = 32'b0;
    wire aw_take = s_axi_awvalid && s_axi_awready;
    wire w_take = s_axi_wvalid && s_axi_wready;
    always @(posedge aclk or negedge aresetn) begin
        if (!aresetn) begin
            aw_seen <= 1'b0;
            w_seen <= 1'b0;
            s_axi_bvalid <= 1'b0;
            s_axi_rvalid <= 1'b0;
        end else begin
            if (s_axi_bvalid && s_axi_bready) s_axi_bvalid <= 1'b0;
            if (s_axi_rvalid && s_axi_rready) s_axi_rvalid <= 1'b0;
            if (aw_take) aw_seen <= 1'b1;
            if (w_take) w_seen <= 1'b1;
            if (!s_axi_bvalid && (aw_seen || aw_take) && (w_seen || w_take)) begin
                s_axi_bvalid <= 1'b1;
                aw_seen <= 1'b0;
                w_seen <= 1'b0;
            end
            if (s_axi_arvalid && s_axi_arready) s_axi_rvalid <= 1'b1;
        end
    end
endmodule
