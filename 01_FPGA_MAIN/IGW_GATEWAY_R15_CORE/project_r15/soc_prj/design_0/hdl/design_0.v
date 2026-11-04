// R15 fixed HDL integration. Vendor IP leaf files are preserved byte-for-byte.
// No HDMI, camera, ISP, video DMA, or video timing generator is instantiated.
// Maintain this HDL integration; do not regenerate it from an older DI diagram.
module design_0 (
    input wire RS485_RX0_3V3,
    output wire RS485_TX0_3V3,
    output wire RS485_DE0_3V3,
    inout wire mipi_iic_i2c0_scl_io,
    inout wire mipi_iic_i2c0_sda_io,
    input wire can0_rx_in,
    output wire can0_tx_out,
    output wire p2f_clk0_50m,
    output wire gateway_ready,
    output wire npu_clock_locked
);
    wire [32:0] w_net__igw_rs485_branch_0__m_axi_araddr;
    wire [2:0] w_net__igw_rs485_branch_0__m_axi_arprot;
    wire w_net__igw_rs485_branch_0__m_axi_arvalid;
    wire [32:0] w_net__igw_rs485_branch_0__m_axi_awaddr;
    wire [2:0] w_net__igw_rs485_branch_0__m_axi_awprot;
    wire w_net__igw_rs485_branch_0__m_axi_awvalid;
    wire w_net__igw_rs485_branch_0__m_axi_bready;
    wire w_net__igw_rs485_branch_0__m_axi_rready;
    wire [31:0] w_net__igw_rs485_branch_0__m_axi_wdata;
    wire [3:0] w_net__igw_rs485_branch_0__m_axi_wstrb;
    wire w_net__igw_rs485_branch_0__m_axi_wvalid;
    wire w_net__igw_rs485_branch_0__s_axi_arready;
    wire w_net__igw_rs485_branch_0__s_axi_awready;
    wire [1:0] w_net__igw_rs485_branch_0__s_axi_bresp;
    wire w_net__igw_rs485_branch_0__s_axi_bvalid;
    wire [31:0] w_net__igw_rs485_branch_0__s_axi_rdata;
    wire [1:0] w_net__igw_rs485_branch_0__s_axi_rresp;
    wire w_net__igw_rs485_branch_0__s_axi_rvalid;
    wire w_net__igw_rs485_branch_0__s_axi_wready;
    wire w_port__ARM_Processor_System_0__can0_tx_out;
    wire w_port__ARM_Processor_System_0__i2c0_scl_in;
    wire w_port__ARM_Processor_System_0__i2c0_scl_oe;
    wire w_port__ARM_Processor_System_0__i2c0_sda_in;
    wire w_port__ARM_Processor_System_0__i2c0_sda_oe;
    wire [32:0] w_port__ARM_Processor_System_0__master_gp0_axi_araddr;
    wire [1:0] w_port__ARM_Processor_System_0__master_gp0_axi_arburst;
    wire [3:0] w_port__ARM_Processor_System_0__master_gp0_axi_arcache;
    wire [11:0] w_port__ARM_Processor_System_0__master_gp0_axi_arid;
    wire [3:0] w_port__ARM_Processor_System_0__master_gp0_axi_arlen;
    wire w_port__ARM_Processor_System_0__master_gp0_axi_arlock;
    wire [2:0] w_port__ARM_Processor_System_0__master_gp0_axi_arprot;
    wire [1:0] w_port__ARM_Processor_System_0__master_gp0_axi_arsize;
    wire w_port__ARM_Processor_System_0__master_gp0_axi_arvalid;
    wire [32:0] w_port__ARM_Processor_System_0__master_gp0_axi_awaddr;
    wire [1:0] w_port__ARM_Processor_System_0__master_gp0_axi_awburst;
    wire [3:0] w_port__ARM_Processor_System_0__master_gp0_axi_awcache;
    wire [11:0] w_port__ARM_Processor_System_0__master_gp0_axi_awid;
    wire [3:0] w_port__ARM_Processor_System_0__master_gp0_axi_awlen;
    wire w_port__ARM_Processor_System_0__master_gp0_axi_awlock;
    wire [2:0] w_port__ARM_Processor_System_0__master_gp0_axi_awprot;
    wire [1:0] w_port__ARM_Processor_System_0__master_gp0_axi_awsize;
    wire w_port__ARM_Processor_System_0__master_gp0_axi_awvalid;
    wire w_port__ARM_Processor_System_0__master_gp0_axi_bready;
    wire w_port__ARM_Processor_System_0__master_gp0_axi_rready;
    wire [31:0] w_port__ARM_Processor_System_0__master_gp0_axi_wdata;
    wire w_port__ARM_Processor_System_0__master_gp0_axi_wlast;
    wire [3:0] w_port__ARM_Processor_System_0__master_gp0_axi_wstrb;
    wire w_port__ARM_Processor_System_0__master_gp0_axi_wvalid;
    wire [32:0] w_port__ARM_Processor_System_0__master_gp1_axi_araddr;
    wire [1:0] w_port__ARM_Processor_System_0__master_gp1_axi_arburst;
    wire [3:0] w_port__ARM_Processor_System_0__master_gp1_axi_arcache;
    wire [11:0] w_port__ARM_Processor_System_0__master_gp1_axi_arid;
    wire [3:0] w_port__ARM_Processor_System_0__master_gp1_axi_arlen;
    wire w_port__ARM_Processor_System_0__master_gp1_axi_arlock;
    wire [2:0] w_port__ARM_Processor_System_0__master_gp1_axi_arprot;
    wire [1:0] w_port__ARM_Processor_System_0__master_gp1_axi_arsize;
    wire w_port__ARM_Processor_System_0__master_gp1_axi_arvalid;
    wire [32:0] w_port__ARM_Processor_System_0__master_gp1_axi_awaddr;
    wire [1:0] w_port__ARM_Processor_System_0__master_gp1_axi_awburst;
    wire [3:0] w_port__ARM_Processor_System_0__master_gp1_axi_awcache;
    wire [11:0] w_port__ARM_Processor_System_0__master_gp1_axi_awid;
    wire [3:0] w_port__ARM_Processor_System_0__master_gp1_axi_awlen;
    wire w_port__ARM_Processor_System_0__master_gp1_axi_awlock;
    wire [2:0] w_port__ARM_Processor_System_0__master_gp1_axi_awprot;
    wire [1:0] w_port__ARM_Processor_System_0__master_gp1_axi_awsize;
    wire w_port__ARM_Processor_System_0__master_gp1_axi_awvalid;
    wire w_port__ARM_Processor_System_0__master_gp1_axi_bready;
    wire w_port__ARM_Processor_System_0__master_gp1_axi_rready;
    wire [31:0] w_port__ARM_Processor_System_0__master_gp1_axi_wdata;
    wire w_port__ARM_Processor_System_0__master_gp1_axi_wlast;
    wire [3:0] w_port__ARM_Processor_System_0__master_gp1_axi_wstrb;
    wire w_port__ARM_Processor_System_0__master_gp1_axi_wvalid;
    wire w_port__ARM_Processor_System_0__p2f_clk0;
    wire w_port__ARM_Processor_System_0__p2f_rst0_n;
    wire w_port__ARM_Processor_System_0__slave_hp0_axi_arready;
    wire w_port__ARM_Processor_System_0__slave_hp0_axi_awready;
    wire [5:0] w_port__ARM_Processor_System_0__slave_hp0_axi_bid;
    wire [1:0] w_port__ARM_Processor_System_0__slave_hp0_axi_bresp;
    wire w_port__ARM_Processor_System_0__slave_hp0_axi_bvalid;
    wire [63:0] w_port__ARM_Processor_System_0__slave_hp0_axi_rdata;
    wire [5:0] w_port__ARM_Processor_System_0__slave_hp0_axi_rid;
    wire w_port__ARM_Processor_System_0__slave_hp0_axi_rlast;
    wire [1:0] w_port__ARM_Processor_System_0__slave_hp0_axi_rresp;
    wire w_port__ARM_Processor_System_0__slave_hp0_axi_rvalid;
    wire w_port__ARM_Processor_System_0__slave_hp0_axi_wready;
    wire w_port__ARM_Processor_System_0__slave_hp1_axi_arready;
    wire w_port__ARM_Processor_System_0__slave_hp1_axi_awready;
    wire [5:0] w_port__ARM_Processor_System_0__slave_hp1_axi_bid;
    wire [1:0] w_port__ARM_Processor_System_0__slave_hp1_axi_bresp;
    wire w_port__ARM_Processor_System_0__slave_hp1_axi_bvalid;
    wire [63:0] w_port__ARM_Processor_System_0__slave_hp1_axi_rdata;
    wire [5:0] w_port__ARM_Processor_System_0__slave_hp1_axi_rid;
    wire w_port__ARM_Processor_System_0__slave_hp1_axi_rlast;
    wire [1:0] w_port__ARM_Processor_System_0__slave_hp1_axi_rresp;
    wire w_port__ARM_Processor_System_0__slave_hp1_axi_rvalid;
    wire w_port__ARM_Processor_System_0__slave_hp1_axi_wready;
    wire w_port__AXI_Matrix_0__us0_arready;
    wire w_port__AXI_Matrix_0__us0_awready;
    wire [1:0] w_port__AXI_Matrix_0__us0_bresp;
    wire w_port__AXI_Matrix_0__us0_bvalid;
    wire [31:0] w_port__AXI_Matrix_0__us0_rdata;
    wire [1:0] w_port__AXI_Matrix_0__us0_rresp;
    wire w_port__AXI_Matrix_0__us0_rvalid;
    wire w_port__AXI_Matrix_0__us0_wready;
    wire [32:0] w_port__AXI_Matrix_1__ds0_araddr;
    wire [1:0] w_port__AXI_Matrix_1__ds0_arburst;
    wire [3:0] w_port__AXI_Matrix_1__ds0_arcache;
    wire [12:0] w_port__AXI_Matrix_1__ds0_arid;
    wire [7:0] w_port__AXI_Matrix_1__ds0_arlen;
    wire [0:0] w_port__AXI_Matrix_1__ds0_arlock;
    wire [2:0] w_port__AXI_Matrix_1__ds0_arprot;
    wire [3:0] w_port__AXI_Matrix_1__ds0_arqos;
    wire [2:0] w_port__AXI_Matrix_1__ds0_arsize;
    wire w_port__AXI_Matrix_1__ds0_arvalid;
    wire [32:0] w_port__AXI_Matrix_1__ds0_awaddr;
    wire [1:0] w_port__AXI_Matrix_1__ds0_awburst;
    wire [3:0] w_port__AXI_Matrix_1__ds0_awcache;
    wire [12:0] w_port__AXI_Matrix_1__ds0_awid;
    wire [7:0] w_port__AXI_Matrix_1__ds0_awlen;
    wire [0:0] w_port__AXI_Matrix_1__ds0_awlock;
    wire [2:0] w_port__AXI_Matrix_1__ds0_awprot;
    wire [3:0] w_port__AXI_Matrix_1__ds0_awqos;
    wire [2:0] w_port__AXI_Matrix_1__ds0_awsize;
    wire w_port__AXI_Matrix_1__ds0_awvalid;
    wire w_port__AXI_Matrix_1__ds0_bready;
    wire w_port__AXI_Matrix_1__ds0_rready;
    wire [31:0] w_port__AXI_Matrix_1__ds0_wdata;
    wire w_port__AXI_Matrix_1__ds0_wlast;
    wire [3:0] w_port__AXI_Matrix_1__ds0_wstrb;
    wire w_port__AXI_Matrix_1__ds0_wvalid;
    wire w_port__AXI_Matrix_1__us0_arready;
    wire w_port__AXI_Matrix_1__us0_awready;
    wire [11:0] w_port__AXI_Matrix_1__us0_bid;
    wire [1:0] w_port__AXI_Matrix_1__us0_bresp;
    wire w_port__AXI_Matrix_1__us0_bvalid;
    wire [31:0] w_port__AXI_Matrix_1__us0_rdata;
    wire [11:0] w_port__AXI_Matrix_1__us0_rid;
    wire w_port__AXI_Matrix_1__us0_rlast;
    wire [1:0] w_port__AXI_Matrix_1__us0_rresp;
    wire w_port__AXI_Matrix_1__us0_rvalid;
    wire w_port__AXI_Matrix_1__us0_wready;
    wire [32:0] w_port__AXI_Protocol_Converter_0__ds_axi_araddr;
    wire [2:0] w_port__AXI_Protocol_Converter_0__ds_axi_arprot;
    wire w_port__AXI_Protocol_Converter_0__ds_axi_arvalid;
    wire [32:0] w_port__AXI_Protocol_Converter_0__ds_axi_awaddr;
    wire [2:0] w_port__AXI_Protocol_Converter_0__ds_axi_awprot;
    wire w_port__AXI_Protocol_Converter_0__ds_axi_awvalid;
    wire w_port__AXI_Protocol_Converter_0__ds_axi_bready;
    wire w_port__AXI_Protocol_Converter_0__ds_axi_rready;
    wire [31:0] w_port__AXI_Protocol_Converter_0__ds_axi_wdata;
    wire [3:0] w_port__AXI_Protocol_Converter_0__ds_axi_wstrb;
    wire w_port__AXI_Protocol_Converter_0__ds_axi_wvalid;
    wire w_port__AXI_Protocol_Converter_0__us_axi_arready;
    wire w_port__AXI_Protocol_Converter_0__us_axi_awready;
    wire [11:0] w_port__AXI_Protocol_Converter_0__us_axi_bid;
    wire [1:0] w_port__AXI_Protocol_Converter_0__us_axi_bresp;
    wire w_port__AXI_Protocol_Converter_0__us_axi_bvalid;
    wire [31:0] w_port__AXI_Protocol_Converter_0__us_axi_rdata;
    wire [11:0] w_port__AXI_Protocol_Converter_0__us_axi_rid;
    wire w_port__AXI_Protocol_Converter_0__us_axi_rlast;
    wire [1:0] w_port__AXI_Protocol_Converter_0__us_axi_rresp;
    wire w_port__AXI_Protocol_Converter_0__us_axi_rvalid;
    wire w_port__AXI_Protocol_Converter_0__us_axi_wready;
    wire [32:0] w_port__AXI_Protocol_Converter_1__ds_axi_araddr;
    wire [2:0] w_port__AXI_Protocol_Converter_1__ds_axi_arprot;
    wire w_port__AXI_Protocol_Converter_1__ds_axi_arvalid;
    wire [32:0] w_port__AXI_Protocol_Converter_1__ds_axi_awaddr;
    wire [2:0] w_port__AXI_Protocol_Converter_1__ds_axi_awprot;
    wire w_port__AXI_Protocol_Converter_1__ds_axi_awvalid;
    wire w_port__AXI_Protocol_Converter_1__ds_axi_bready;
    wire w_port__AXI_Protocol_Converter_1__ds_axi_rready;
    wire [31:0] w_port__AXI_Protocol_Converter_1__ds_axi_wdata;
    wire [3:0] w_port__AXI_Protocol_Converter_1__ds_axi_wstrb;
    wire w_port__AXI_Protocol_Converter_1__ds_axi_wvalid;
    wire w_port__AXI_Protocol_Converter_1__us_axi_arready;
    wire w_port__AXI_Protocol_Converter_1__us_axi_awready;
    wire [12:0] w_port__AXI_Protocol_Converter_1__us_axi_bid;
    wire [1:0] w_port__AXI_Protocol_Converter_1__us_axi_bresp;
    wire w_port__AXI_Protocol_Converter_1__us_axi_bvalid;
    wire [31:0] w_port__AXI_Protocol_Converter_1__us_axi_rdata;
    wire [12:0] w_port__AXI_Protocol_Converter_1__us_axi_rid;
    wire w_port__AXI_Protocol_Converter_1__us_axi_rlast;
    wire [1:0] w_port__AXI_Protocol_Converter_1__us_axi_rresp;
    wire w_port__AXI_Protocol_Converter_1__us_axi_rvalid;
    wire w_port__AXI_Protocol_Converter_1__us_axi_wready;
    wire [15:0] w_port__Concat_0__Dout;
    wire w_port__PLL_npu__clk0_out;
    wire w_port__PLL_npu__lock;
    wire [32:0] w_port__Video_NPU_0__m_axi_araddr_soft_o;
    wire [1:0] w_port__Video_NPU_0__m_axi_arburst_soft_o;
    wire [3:0] w_port__Video_NPU_0__m_axi_arcache_soft_o;
    wire [5:0] w_port__Video_NPU_0__m_axi_arid_soft_o;
    wire [3:0] w_port__Video_NPU_0__m_axi_arlen_soft_o;
    wire w_port__Video_NPU_0__m_axi_arlock_soft_o;
    wire [2:0] w_port__Video_NPU_0__m_axi_arprot_soft_o;
    wire [1:0] w_port__Video_NPU_0__m_axi_arsize_soft_o;
    wire w_port__Video_NPU_0__m_axi_arvalid_soft_o;
    wire [32:0] w_port__Video_NPU_0__m_axi_awaddr_soft_o;
    wire [1:0] w_port__Video_NPU_0__m_axi_awburst_soft_o;
    wire [3:0] w_port__Video_NPU_0__m_axi_awcache_soft_o;
    wire [5:0] w_port__Video_NPU_0__m_axi_awid_soft_o;
    wire [3:0] w_port__Video_NPU_0__m_axi_awlen_soft_o;
    wire w_port__Video_NPU_0__m_axi_awlock_soft_o;
    wire [2:0] w_port__Video_NPU_0__m_axi_awprot_soft_o;
    wire [1:0] w_port__Video_NPU_0__m_axi_awsize_soft_o;
    wire w_port__Video_NPU_0__m_axi_awvalid_soft_o;
    wire w_port__Video_NPU_0__m_axi_bready_soft_o;
    wire w_port__Video_NPU_0__m_axi_rready_soft_o;
    wire [63:0] w_port__Video_NPU_0__m_axi_wdata_soft_o;
    wire w_port__Video_NPU_0__m_axi_wlast_soft_o;
    wire [7:0] w_port__Video_NPU_0__m_axi_wstrb_soft_o;
    wire w_port__Video_NPU_0__m_axi_wvalid_soft_o;
    wire w_port__Video_NPU_0__npu_soft_intr;
    wire w_port__Video_NPU_0__s_axilite_arready_o;
    wire w_port__Video_NPU_0__s_axilite_awready_o;
    wire [1:0] w_port__Video_NPU_0__s_axilite_bresp_o;
    wire w_port__Video_NPU_0__s_axilite_bvalid_o;
    wire [31:0] w_port__Video_NPU_0__s_axilite_rdata_o;
    wire [1:0] w_port__Video_NPU_0__s_axilite_rresp_o;
    wire w_port__Video_NPU_0__s_axilite_rvalid_o;
    wire w_port__Video_NPU_0__s_axilite_wready_o;
    wire w_port__pll_150m__clk0_out;
    wire w_port__pll_150m__lock;
    wire w_port__pll_75m__clk0_out;
    wire w_port__pll_75m__lock;
    wire [0:0] w_port__rst_n_75m__interconnect_aresetn;
    wire [0:0] w_port__rstn_150m__peripheral_aresetn;
    wire [0:0] w_port__rstn_npu__interconnect_aresetn;
    wire [0:0] w_port__rstn_npu__peripheral_aresetn;
    assign can0_tx_out = w_port__ARM_Processor_System_0__can0_tx_out;
    assign p2f_clk0_50m = w_port__ARM_Processor_System_0__p2f_clk0;
    assign gateway_ready = w_port__rst_n_75m__interconnect_aresetn;
    assign npu_clock_locked = w_port__PLL_npu__lock;
    DR1_LOGIC_IOTRIBUF mipi_iic_i2c0_scl_DR1_LOGIC_IOTRIBUF_0 (
        .o(w_port__ARM_Processor_System_0__i2c0_scl_in),
        .i(1'b0),
        .t(~w_port__ARM_Processor_System_0__i2c0_scl_oe),
        .io(mipi_iic_i2c0_scl_io)
    );
    DR1_LOGIC_IOTRIBUF mipi_iic_i2c0_sda_DR1_LOGIC_IOTRIBUF_0 (
        .o(w_port__ARM_Processor_System_0__i2c0_sda_in),
        .i(1'b0),
        .t(~w_port__ARM_Processor_System_0__i2c0_sda_oe),
        .io(mipi_iic_i2c0_sda_io)
    );
    AXI_Protocol_Converter_0 design_0_AXI_Protocol_Converter_0_Inst (
        .us_axi_awid(w_port__ARM_Processor_System_0__master_gp0_axi_awid),
        .us_axi_awaddr(w_port__ARM_Processor_System_0__master_gp0_axi_awaddr),
        .us_axi_awlen(({4'b0,w_port__ARM_Processor_System_0__master_gp0_axi_awlen})),
        .us_axi_awsize(({1'b0,w_port__ARM_Processor_System_0__master_gp0_axi_awsize})),
        .us_axi_awburst(w_port__ARM_Processor_System_0__master_gp0_axi_awburst),
        .us_axi_awlock(w_port__ARM_Processor_System_0__master_gp0_axi_awlock),
        .us_axi_awcache(w_port__ARM_Processor_System_0__master_gp0_axi_awcache),
        .us_axi_awprot(w_port__ARM_Processor_System_0__master_gp0_axi_awprot),
        .us_axi_awvalid(w_port__ARM_Processor_System_0__master_gp0_axi_awvalid),
        .us_axi_awready(w_port__AXI_Protocol_Converter_0__us_axi_awready),
        .us_axi_wdata(w_port__ARM_Processor_System_0__master_gp0_axi_wdata),
        .us_axi_wstrb(w_port__ARM_Processor_System_0__master_gp0_axi_wstrb),
        .us_axi_wlast(w_port__ARM_Processor_System_0__master_gp0_axi_wlast),
        .us_axi_wvalid(w_port__ARM_Processor_System_0__master_gp0_axi_wvalid),
        .us_axi_wready(w_port__AXI_Protocol_Converter_0__us_axi_wready),
        .us_axi_bid(w_port__AXI_Protocol_Converter_0__us_axi_bid),
        .us_axi_bresp(w_port__AXI_Protocol_Converter_0__us_axi_bresp),
        .us_axi_bvalid(w_port__AXI_Protocol_Converter_0__us_axi_bvalid),
        .us_axi_bready(w_port__ARM_Processor_System_0__master_gp0_axi_bready),
        .us_axi_arid(w_port__ARM_Processor_System_0__master_gp0_axi_arid),
        .us_axi_araddr(w_port__ARM_Processor_System_0__master_gp0_axi_araddr),
        .us_axi_arlen(({4'b0,w_port__ARM_Processor_System_0__master_gp0_axi_arlen})),
        .us_axi_arsize(({1'b0,w_port__ARM_Processor_System_0__master_gp0_axi_arsize})),
        .us_axi_arburst(w_port__ARM_Processor_System_0__master_gp0_axi_arburst),
        .us_axi_arlock(w_port__ARM_Processor_System_0__master_gp0_axi_arlock),
        .us_axi_arcache(w_port__ARM_Processor_System_0__master_gp0_axi_arcache),
        .us_axi_arprot(w_port__ARM_Processor_System_0__master_gp0_axi_arprot),
        .us_axi_arvalid(w_port__ARM_Processor_System_0__master_gp0_axi_arvalid),
        .us_axi_arready(w_port__AXI_Protocol_Converter_0__us_axi_arready),
        .us_axi_rid(w_port__AXI_Protocol_Converter_0__us_axi_rid),
        .us_axi_rdata(w_port__AXI_Protocol_Converter_0__us_axi_rdata),
        .us_axi_rresp(w_port__AXI_Protocol_Converter_0__us_axi_rresp),
        .us_axi_rlast(w_port__AXI_Protocol_Converter_0__us_axi_rlast),
        .us_axi_rvalid(w_port__AXI_Protocol_Converter_0__us_axi_rvalid),
        .us_axi_rready(w_port__ARM_Processor_System_0__master_gp0_axi_rready),
        .us_axi_awuser(1'b0),
        .us_axi_awqos(4'b0),
        .us_axi_wuser(1'b0),
        .us_axi_buser(),
        .us_axi_aruser(1'b0),
        .us_axi_arqos(4'b0),
        .us_axi_ruser(),
        .aclk(w_port__ARM_Processor_System_0__p2f_clk0),
        .aresetn(w_port__rst_n_75m__interconnect_aresetn),
        .ds_axi_awaddr(w_port__AXI_Protocol_Converter_0__ds_axi_awaddr),
        .ds_axi_awprot(w_port__AXI_Protocol_Converter_0__ds_axi_awprot),
        .ds_axi_awvalid(w_port__AXI_Protocol_Converter_0__ds_axi_awvalid),
        .ds_axi_awready(w_net__igw_rs485_branch_0__s_axi_awready),
        .ds_axi_wdata(w_port__AXI_Protocol_Converter_0__ds_axi_wdata),
        .ds_axi_wstrb(w_port__AXI_Protocol_Converter_0__ds_axi_wstrb),
        .ds_axi_wvalid(w_port__AXI_Protocol_Converter_0__ds_axi_wvalid),
        .ds_axi_wready(w_net__igw_rs485_branch_0__s_axi_wready),
        .ds_axi_bresp(w_net__igw_rs485_branch_0__s_axi_bresp),
        .ds_axi_bvalid(w_net__igw_rs485_branch_0__s_axi_bvalid),
        .ds_axi_bready(w_port__AXI_Protocol_Converter_0__ds_axi_bready),
        .ds_axi_araddr(w_port__AXI_Protocol_Converter_0__ds_axi_araddr),
        .ds_axi_arprot(w_port__AXI_Protocol_Converter_0__ds_axi_arprot),
        .ds_axi_arvalid(w_port__AXI_Protocol_Converter_0__ds_axi_arvalid),
        .ds_axi_arready(w_net__igw_rs485_branch_0__s_axi_arready),
        .ds_axi_rdata(w_net__igw_rs485_branch_0__s_axi_rdata),
        .ds_axi_rresp(w_net__igw_rs485_branch_0__s_axi_rresp),
        .ds_axi_rvalid(w_net__igw_rs485_branch_0__s_axi_rvalid),
        .ds_axi_rready(w_port__AXI_Protocol_Converter_0__ds_axi_rready)
    );
    design_0_ARM_Processor_System_0 design_0_ARM_Processor_System_0_Inst (
        .slave_hp0_axi_araddr(w_port__Video_NPU_0__m_axi_araddr_soft_o),
        .slave_hp0_axi_arburst(w_port__Video_NPU_0__m_axi_arburst_soft_o),
        .slave_hp0_axi_arcache(w_port__Video_NPU_0__m_axi_arcache_soft_o),
        .slave_hp0_axi_arid(w_port__Video_NPU_0__m_axi_arid_soft_o),
        .slave_hp0_axi_arlen(w_port__Video_NPU_0__m_axi_arlen_soft_o),
        .slave_hp0_axi_arlock(w_port__Video_NPU_0__m_axi_arlock_soft_o),
        .slave_hp0_axi_arprot(w_port__Video_NPU_0__m_axi_arprot_soft_o),
        .slave_hp0_axi_arqos(4'b0),
        .slave_hp0_axi_arready(w_port__ARM_Processor_System_0__slave_hp0_axi_arready),
        .slave_hp0_axi_arsize(w_port__Video_NPU_0__m_axi_arsize_soft_o),
        .slave_hp0_axi_arvalid(w_port__Video_NPU_0__m_axi_arvalid_soft_o),
        .slave_hp0_axi_awaddr(w_port__Video_NPU_0__m_axi_awaddr_soft_o),
        .slave_hp0_axi_awburst(w_port__Video_NPU_0__m_axi_awburst_soft_o),
        .slave_hp0_axi_awcache(w_port__Video_NPU_0__m_axi_awcache_soft_o),
        .slave_hp0_axi_awid(w_port__Video_NPU_0__m_axi_awid_soft_o),
        .slave_hp0_axi_awlen(w_port__Video_NPU_0__m_axi_awlen_soft_o),
        .slave_hp0_axi_awlock(w_port__Video_NPU_0__m_axi_awlock_soft_o),
        .slave_hp0_axi_awprot(w_port__Video_NPU_0__m_axi_awprot_soft_o),
        .slave_hp0_axi_awqos(4'b0),
        .slave_hp0_axi_awready(w_port__ARM_Processor_System_0__slave_hp0_axi_awready),
        .slave_hp0_axi_awsize(w_port__Video_NPU_0__m_axi_awsize_soft_o),
        .slave_hp0_axi_awvalid(w_port__Video_NPU_0__m_axi_awvalid_soft_o),
        .slave_hp0_axi_bid(w_port__ARM_Processor_System_0__slave_hp0_axi_bid),
        .slave_hp0_axi_bready(w_port__Video_NPU_0__m_axi_bready_soft_o),
        .slave_hp0_axi_bresp(w_port__ARM_Processor_System_0__slave_hp0_axi_bresp),
        .slave_hp0_axi_bvalid(w_port__ARM_Processor_System_0__slave_hp0_axi_bvalid),
        .slave_hp0_axi_rdata(w_port__ARM_Processor_System_0__slave_hp0_axi_rdata),
        .slave_hp0_axi_rid(w_port__ARM_Processor_System_0__slave_hp0_axi_rid),
        .slave_hp0_axi_rlast(w_port__ARM_Processor_System_0__slave_hp0_axi_rlast),
        .slave_hp0_axi_rready(w_port__Video_NPU_0__m_axi_rready_soft_o),
        .slave_hp0_axi_rresp(w_port__ARM_Processor_System_0__slave_hp0_axi_rresp),
        .slave_hp0_axi_rvalid(w_port__ARM_Processor_System_0__slave_hp0_axi_rvalid),
        .slave_hp0_axi_wdata(w_port__Video_NPU_0__m_axi_wdata_soft_o),
        .slave_hp0_axi_wlast(w_port__Video_NPU_0__m_axi_wlast_soft_o),
        .slave_hp0_axi_wready(w_port__ARM_Processor_System_0__slave_hp0_axi_wready),
        .slave_hp0_axi_wstrb(w_port__Video_NPU_0__m_axi_wstrb_soft_o),
        .slave_hp0_axi_wvalid(w_port__Video_NPU_0__m_axi_wvalid_soft_o),
        .slave_hp1_axi_araddr(1'b0),
        .slave_hp1_axi_arburst(1'b0),
        .slave_hp1_axi_arcache(1'b0),
        .slave_hp1_axi_arid(1'b0),
        .slave_hp1_axi_arlen(1'b0),
        .slave_hp1_axi_arlock(1'b0),
        .slave_hp1_axi_arprot(1'b0),
        .slave_hp1_axi_arqos(1'b0),
        .slave_hp1_axi_arready(w_port__ARM_Processor_System_0__slave_hp1_axi_arready),
        .slave_hp1_axi_arsize(1'b0),
        .slave_hp1_axi_arvalid(1'b0),
        .slave_hp1_axi_awaddr(1'b0),
        .slave_hp1_axi_awburst(1'b0),
        .slave_hp1_axi_awcache(1'b0),
        .slave_hp1_axi_awid(1'b0),
        .slave_hp1_axi_awlen(1'b0),
        .slave_hp1_axi_awlock(1'b0),
        .slave_hp1_axi_awprot(1'b0),
        .slave_hp1_axi_awqos(1'b0),
        .slave_hp1_axi_awready(w_port__ARM_Processor_System_0__slave_hp1_axi_awready),
        .slave_hp1_axi_awsize(1'b0),
        .slave_hp1_axi_awvalid(1'b0),
        .slave_hp1_axi_bid(w_port__ARM_Processor_System_0__slave_hp1_axi_bid),
        .slave_hp1_axi_bready(1'b0),
        .slave_hp1_axi_bresp(w_port__ARM_Processor_System_0__slave_hp1_axi_bresp),
        .slave_hp1_axi_bvalid(w_port__ARM_Processor_System_0__slave_hp1_axi_bvalid),
        .slave_hp1_axi_rdata(w_port__ARM_Processor_System_0__slave_hp1_axi_rdata),
        .slave_hp1_axi_rid(w_port__ARM_Processor_System_0__slave_hp1_axi_rid),
        .slave_hp1_axi_rlast(w_port__ARM_Processor_System_0__slave_hp1_axi_rlast),
        .slave_hp1_axi_rready(1'b0),
        .slave_hp1_axi_rresp(w_port__ARM_Processor_System_0__slave_hp1_axi_rresp),
        .slave_hp1_axi_rvalid(w_port__ARM_Processor_System_0__slave_hp1_axi_rvalid),
        .slave_hp1_axi_wdata(1'b0),
        .slave_hp1_axi_wlast(1'b0),
        .slave_hp1_axi_wready(w_port__ARM_Processor_System_0__slave_hp1_axi_wready),
        .slave_hp1_axi_wstrb(1'b0),
        .slave_hp1_axi_wvalid(1'b0),
        .slave_hp0_axi_aclk(w_port__ARM_Processor_System_0__p2f_clk0),
        .slave_hp1_axi_aclk(w_port__pll_150m__clk0_out),
        .master_gp0_axi_aclk(w_port__ARM_Processor_System_0__p2f_clk0),
        .master_gp1_axi_aclk(w_port__ARM_Processor_System_0__p2f_clk0),
        .f2p_irq(w_port__Concat_0__Dout),
        .master_gp0_axi_araddr(w_port__ARM_Processor_System_0__master_gp0_axi_araddr),
        .master_gp0_axi_arburst(w_port__ARM_Processor_System_0__master_gp0_axi_arburst),
        .master_gp0_axi_arcache(w_port__ARM_Processor_System_0__master_gp0_axi_arcache),
        .master_gp0_axi_arid(w_port__ARM_Processor_System_0__master_gp0_axi_arid),
        .master_gp0_axi_arlen(w_port__ARM_Processor_System_0__master_gp0_axi_arlen),
        .master_gp0_axi_arlock(w_port__ARM_Processor_System_0__master_gp0_axi_arlock),
        .master_gp0_axi_arprot(w_port__ARM_Processor_System_0__master_gp0_axi_arprot),
        .master_gp0_axi_arready(w_port__AXI_Protocol_Converter_0__us_axi_arready),
        .master_gp0_axi_arsize(w_port__ARM_Processor_System_0__master_gp0_axi_arsize),
        .master_gp0_axi_arvalid(w_port__ARM_Processor_System_0__master_gp0_axi_arvalid),
        .master_gp0_axi_awaddr(w_port__ARM_Processor_System_0__master_gp0_axi_awaddr),
        .master_gp0_axi_awburst(w_port__ARM_Processor_System_0__master_gp0_axi_awburst),
        .master_gp0_axi_awcache(w_port__ARM_Processor_System_0__master_gp0_axi_awcache),
        .master_gp0_axi_awid(w_port__ARM_Processor_System_0__master_gp0_axi_awid),
        .master_gp0_axi_awlen(w_port__ARM_Processor_System_0__master_gp0_axi_awlen),
        .master_gp0_axi_awlock(w_port__ARM_Processor_System_0__master_gp0_axi_awlock),
        .master_gp0_axi_awprot(w_port__ARM_Processor_System_0__master_gp0_axi_awprot),
        .master_gp0_axi_awready(w_port__AXI_Protocol_Converter_0__us_axi_awready),
        .master_gp0_axi_awsize(w_port__ARM_Processor_System_0__master_gp0_axi_awsize),
        .master_gp0_axi_awvalid(w_port__ARM_Processor_System_0__master_gp0_axi_awvalid),
        .master_gp0_axi_bid(w_port__AXI_Protocol_Converter_0__us_axi_bid),
        .master_gp0_axi_bready(w_port__ARM_Processor_System_0__master_gp0_axi_bready),
        .master_gp0_axi_bresp(w_port__AXI_Protocol_Converter_0__us_axi_bresp),
        .master_gp0_axi_bvalid(w_port__AXI_Protocol_Converter_0__us_axi_bvalid),
        .master_gp0_axi_rdata(w_port__AXI_Protocol_Converter_0__us_axi_rdata),
        .master_gp0_axi_rid(w_port__AXI_Protocol_Converter_0__us_axi_rid),
        .master_gp0_axi_rlast(w_port__AXI_Protocol_Converter_0__us_axi_rlast),
        .master_gp0_axi_rready(w_port__ARM_Processor_System_0__master_gp0_axi_rready),
        .master_gp0_axi_rresp(w_port__AXI_Protocol_Converter_0__us_axi_rresp),
        .master_gp0_axi_rvalid(w_port__AXI_Protocol_Converter_0__us_axi_rvalid),
        .master_gp0_axi_wdata(w_port__ARM_Processor_System_0__master_gp0_axi_wdata),
        .master_gp0_axi_wlast(w_port__ARM_Processor_System_0__master_gp0_axi_wlast),
        .master_gp0_axi_wready(w_port__AXI_Protocol_Converter_0__us_axi_wready),
        .master_gp0_axi_wstrb(w_port__ARM_Processor_System_0__master_gp0_axi_wstrb),
        .master_gp0_axi_wvalid(w_port__ARM_Processor_System_0__master_gp0_axi_wvalid),
        .master_gp1_axi_araddr(w_port__ARM_Processor_System_0__master_gp1_axi_araddr),
        .master_gp1_axi_arburst(w_port__ARM_Processor_System_0__master_gp1_axi_arburst),
        .master_gp1_axi_arcache(w_port__ARM_Processor_System_0__master_gp1_axi_arcache),
        .master_gp1_axi_arid(w_port__ARM_Processor_System_0__master_gp1_axi_arid),
        .master_gp1_axi_arlen(w_port__ARM_Processor_System_0__master_gp1_axi_arlen),
        .master_gp1_axi_arlock(w_port__ARM_Processor_System_0__master_gp1_axi_arlock),
        .master_gp1_axi_arprot(w_port__ARM_Processor_System_0__master_gp1_axi_arprot),
        .master_gp1_axi_arready(w_port__AXI_Matrix_1__us0_arready),
        .master_gp1_axi_arsize(w_port__ARM_Processor_System_0__master_gp1_axi_arsize),
        .master_gp1_axi_arvalid(w_port__ARM_Processor_System_0__master_gp1_axi_arvalid),
        .master_gp1_axi_awaddr(w_port__ARM_Processor_System_0__master_gp1_axi_awaddr),
        .master_gp1_axi_awburst(w_port__ARM_Processor_System_0__master_gp1_axi_awburst),
        .master_gp1_axi_awcache(w_port__ARM_Processor_System_0__master_gp1_axi_awcache),
        .master_gp1_axi_awid(w_port__ARM_Processor_System_0__master_gp1_axi_awid),
        .master_gp1_axi_awlen(w_port__ARM_Processor_System_0__master_gp1_axi_awlen),
        .master_gp1_axi_awlock(w_port__ARM_Processor_System_0__master_gp1_axi_awlock),
        .master_gp1_axi_awprot(w_port__ARM_Processor_System_0__master_gp1_axi_awprot),
        .master_gp1_axi_awready(w_port__AXI_Matrix_1__us0_awready),
        .master_gp1_axi_awsize(w_port__ARM_Processor_System_0__master_gp1_axi_awsize),
        .master_gp1_axi_awvalid(w_port__ARM_Processor_System_0__master_gp1_axi_awvalid),
        .master_gp1_axi_bid(w_port__AXI_Matrix_1__us0_bid),
        .master_gp1_axi_bready(w_port__ARM_Processor_System_0__master_gp1_axi_bready),
        .master_gp1_axi_bresp(w_port__AXI_Matrix_1__us0_bresp),
        .master_gp1_axi_bvalid(w_port__AXI_Matrix_1__us0_bvalid),
        .master_gp1_axi_rdata(w_port__AXI_Matrix_1__us0_rdata),
        .master_gp1_axi_rid(w_port__AXI_Matrix_1__us0_rid),
        .master_gp1_axi_rlast(w_port__AXI_Matrix_1__us0_rlast),
        .master_gp1_axi_rready(w_port__ARM_Processor_System_0__master_gp1_axi_rready),
        .master_gp1_axi_rresp(w_port__AXI_Matrix_1__us0_rresp),
        .master_gp1_axi_rvalid(w_port__AXI_Matrix_1__us0_rvalid),
        .master_gp1_axi_wdata(w_port__ARM_Processor_System_0__master_gp1_axi_wdata),
        .master_gp1_axi_wlast(w_port__ARM_Processor_System_0__master_gp1_axi_wlast),
        .master_gp1_axi_wready(w_port__AXI_Matrix_1__us0_wready),
        .master_gp1_axi_wstrb(w_port__ARM_Processor_System_0__master_gp1_axi_wstrb),
        .master_gp1_axi_wvalid(w_port__ARM_Processor_System_0__master_gp1_axi_wvalid),
        .can0_rx_in(can0_rx_in),
        .can0_stby(),
        .can0_tx_out(w_port__ARM_Processor_System_0__can0_tx_out),
        .i2c0_scl_in(w_port__ARM_Processor_System_0__i2c0_scl_in),
        .i2c0_scl_oe(w_port__ARM_Processor_System_0__i2c0_scl_oe),
        .i2c0_sda_in(w_port__ARM_Processor_System_0__i2c0_sda_in),
        .i2c0_sda_oe(w_port__ARM_Processor_System_0__i2c0_sda_oe),
        .i2c1_scl_in(1'b0),
        .i2c1_scl_oe(),
        .i2c1_sda_in(1'b0),
        .i2c1_sda_oe(),
        .tc0_clk0_in(1'b0),
        .tc0_clk1_in(1'b0),
        .tc0_clk2_in(1'b0),
        .tc0_wave0_out(),
        .tc0_wave1_out(),
        .tc0_wave2_out(),
        .p2f_rst0_n(w_port__ARM_Processor_System_0__p2f_rst0_n),
        .p2f_clk0(w_port__ARM_Processor_System_0__p2f_clk0)
    );
    design_0_Concat_0 design_0_Concat_0_Inst (
        .In0(1'b0),
        .In1(1'b0),
        .In2(1'b0),
        .In3(1'b0),
        .In4(1'b0),
        .In5(1'b0),
        .In6(1'b0),
        .In7(1'b0),
        .In8(1'b0),
        .In9(1'b0),
        .In10(1'b0),
        .In11(1'b0),
        .In12(1'b0),
        .In13(1'b0),
        .In14(1'b0),
        .In15(w_port__Video_NPU_0__npu_soft_intr),
        .Dout(w_port__Concat_0__Dout)
    );
    design_0_PLL_0 design_0_pll_75m_Inst (
        .refclk(w_port__ARM_Processor_System_0__p2f_clk0),
        .clk0_out(w_port__pll_75m__clk0_out),
        .clk1_out(),
        .lock(w_port__pll_75m__lock)
    );
    design_0_Processor_System_Reset_0 design_0_rst_n_75m_Inst (
        .clk(w_port__ARM_Processor_System_0__p2f_clk0),
        .external_reset(1'b0),
        .aux_rst(w_port__ARM_Processor_System_0__p2f_rst0_n),
        .system_rst(1'b0),
        .pll_locked(w_port__pll_75m__lock),
        .soc_reset(),
        .bus_reset(),
        .peripheral_reset(),
        .interconnect_aresetn(w_port__rst_n_75m__interconnect_aresetn),
        .peripheral_aresetn()
    );
    design_0_PLL_1 design_0_PLL_npu_Inst (
        .refclk(w_port__ARM_Processor_System_0__p2f_clk0),
        .clk0_out(w_port__PLL_npu__clk0_out),
        .lock(w_port__PLL_npu__lock)
    );
    design_0_Processor_System_Reset_1 design_0_rstn_150m_Inst (
        .clk(w_port__pll_150m__clk0_out),
        .external_reset(1'b0),
        .aux_rst(w_port__ARM_Processor_System_0__p2f_rst0_n),
        .system_rst(1'b0),
        .pll_locked(w_port__pll_150m__lock),
        .soc_reset(),
        .bus_reset(),
        .peripheral_reset(),
        .interconnect_aresetn(),
        .peripheral_aresetn(w_port__rstn_150m__peripheral_aresetn)
    );
    design_0_PLL_2 design_0_pll_150m_Inst (
        .refclk(w_port__ARM_Processor_System_0__p2f_clk0),
        .clk0_out(w_port__pll_150m__clk0_out),
        .lock(w_port__pll_150m__lock)
    );
    design_0_Processor_System_Reset_2 design_0_rstn_npu_Inst (
        .clk(w_port__ARM_Processor_System_0__p2f_clk0),
        .external_reset(1'b0),
        .aux_rst(w_port__ARM_Processor_System_0__p2f_rst0_n),
        .system_rst(1'b0),
        .pll_locked(w_port__PLL_npu__lock),
        .soc_reset(),
        .bus_reset(),
        .peripheral_reset(),
        .interconnect_aresetn(w_port__rstn_npu__interconnect_aresetn),
        .peripheral_aresetn(w_port__rstn_npu__peripheral_aresetn)
    );
    design_0_AXI_Matrix_1 design_0_AXI_Matrix_1_Inst (
        .us0_awid(w_port__ARM_Processor_System_0__master_gp1_axi_awid),
        .us0_awaddr(w_port__ARM_Processor_System_0__master_gp1_axi_awaddr),
        .us0_awlen(({4'b0,w_port__ARM_Processor_System_0__master_gp1_axi_awlen})),
        .us0_awsize(({1'b0,w_port__ARM_Processor_System_0__master_gp1_axi_awsize})),
        .us0_awburst(w_port__ARM_Processor_System_0__master_gp1_axi_awburst),
        .us0_awlock(w_port__ARM_Processor_System_0__master_gp1_axi_awlock),
        .us0_awcache(w_port__ARM_Processor_System_0__master_gp1_axi_awcache),
        .us0_awprot(w_port__ARM_Processor_System_0__master_gp1_axi_awprot),
        .us0_awvalid(w_port__ARM_Processor_System_0__master_gp1_axi_awvalid),
        .us0_awready(w_port__AXI_Matrix_1__us0_awready),
        .us0_wdata(w_port__ARM_Processor_System_0__master_gp1_axi_wdata),
        .us0_wstrb(w_port__ARM_Processor_System_0__master_gp1_axi_wstrb),
        .us0_wlast(w_port__ARM_Processor_System_0__master_gp1_axi_wlast),
        .us0_wvalid(w_port__ARM_Processor_System_0__master_gp1_axi_wvalid),
        .us0_wready(w_port__AXI_Matrix_1__us0_wready),
        .us0_bid(w_port__AXI_Matrix_1__us0_bid),
        .us0_bresp(w_port__AXI_Matrix_1__us0_bresp),
        .us0_bvalid(w_port__AXI_Matrix_1__us0_bvalid),
        .us0_bready(w_port__ARM_Processor_System_0__master_gp1_axi_bready),
        .us0_arid(w_port__ARM_Processor_System_0__master_gp1_axi_arid),
        .us0_araddr(w_port__ARM_Processor_System_0__master_gp1_axi_araddr),
        .us0_arlen(({4'b0,w_port__ARM_Processor_System_0__master_gp1_axi_arlen})),
        .us0_arsize(({1'b0,w_port__ARM_Processor_System_0__master_gp1_axi_arsize})),
        .us0_arburst(w_port__ARM_Processor_System_0__master_gp1_axi_arburst),
        .us0_arlock(w_port__ARM_Processor_System_0__master_gp1_axi_arlock),
        .us0_arcache(w_port__ARM_Processor_System_0__master_gp1_axi_arcache),
        .us0_arprot(w_port__ARM_Processor_System_0__master_gp1_axi_arprot),
        .us0_arvalid(w_port__ARM_Processor_System_0__master_gp1_axi_arvalid),
        .us0_arready(w_port__AXI_Matrix_1__us0_arready),
        .us0_rid(w_port__AXI_Matrix_1__us0_rid),
        .us0_rdata(w_port__AXI_Matrix_1__us0_rdata),
        .us0_rresp(w_port__AXI_Matrix_1__us0_rresp),
        .us0_rlast(w_port__AXI_Matrix_1__us0_rlast),
        .us0_rvalid(w_port__AXI_Matrix_1__us0_rvalid),
        .us0_rready(w_port__ARM_Processor_System_0__master_gp1_axi_rready),
        .us0_awqos(4'b0),
        .us0_arqos(4'b0),
        .aclk(w_port__ARM_Processor_System_0__p2f_clk0),
        .aresetn(w_port__rstn_npu__peripheral_aresetn),
        .ds0_awid(w_port__AXI_Matrix_1__ds0_awid),
        .ds0_awaddr(w_port__AXI_Matrix_1__ds0_awaddr),
        .ds0_awlen(w_port__AXI_Matrix_1__ds0_awlen),
        .ds0_awsize(w_port__AXI_Matrix_1__ds0_awsize),
        .ds0_awburst(w_port__AXI_Matrix_1__ds0_awburst),
        .ds0_awlock(w_port__AXI_Matrix_1__ds0_awlock),
        .ds0_awcache(w_port__AXI_Matrix_1__ds0_awcache),
        .ds0_awprot(w_port__AXI_Matrix_1__ds0_awprot),
        .ds0_awvalid(w_port__AXI_Matrix_1__ds0_awvalid),
        .ds0_awready(w_port__AXI_Protocol_Converter_1__us_axi_awready),
        .ds0_wdata(w_port__AXI_Matrix_1__ds0_wdata),
        .ds0_wstrb(w_port__AXI_Matrix_1__ds0_wstrb),
        .ds0_wlast(w_port__AXI_Matrix_1__ds0_wlast),
        .ds0_wvalid(w_port__AXI_Matrix_1__ds0_wvalid),
        .ds0_wready(w_port__AXI_Protocol_Converter_1__us_axi_wready),
        .ds0_bid(w_port__AXI_Protocol_Converter_1__us_axi_bid),
        .ds0_bresp(w_port__AXI_Protocol_Converter_1__us_axi_bresp),
        .ds0_bvalid(w_port__AXI_Protocol_Converter_1__us_axi_bvalid),
        .ds0_bready(w_port__AXI_Matrix_1__ds0_bready),
        .ds0_arid(w_port__AXI_Matrix_1__ds0_arid),
        .ds0_araddr(w_port__AXI_Matrix_1__ds0_araddr),
        .ds0_arlen(w_port__AXI_Matrix_1__ds0_arlen),
        .ds0_arsize(w_port__AXI_Matrix_1__ds0_arsize),
        .ds0_arburst(w_port__AXI_Matrix_1__ds0_arburst),
        .ds0_arlock(w_port__AXI_Matrix_1__ds0_arlock),
        .ds0_arcache(w_port__AXI_Matrix_1__ds0_arcache),
        .ds0_arprot(w_port__AXI_Matrix_1__ds0_arprot),
        .ds0_arvalid(w_port__AXI_Matrix_1__ds0_arvalid),
        .ds0_arready(w_port__AXI_Protocol_Converter_1__us_axi_arready),
        .ds0_rid(w_port__AXI_Protocol_Converter_1__us_axi_rid),
        .ds0_rdata(w_port__AXI_Protocol_Converter_1__us_axi_rdata),
        .ds0_rresp(w_port__AXI_Protocol_Converter_1__us_axi_rresp),
        .ds0_rlast(w_port__AXI_Protocol_Converter_1__us_axi_rlast),
        .ds0_rvalid(w_port__AXI_Protocol_Converter_1__us_axi_rvalid),
        .ds0_rready(w_port__AXI_Matrix_1__ds0_rready),
        .ds0_awqos(w_port__AXI_Matrix_1__ds0_awqos),
        .ds0_awregion(),
        .ds0_arqos(w_port__AXI_Matrix_1__ds0_arqos),
        .ds0_arregion(),
        .ds1_awid(),
        .ds1_awaddr(),
        .ds1_awlen(),
        .ds1_awsize(),
        .ds1_awburst(),
        .ds1_awlock(),
        .ds1_awcache(),
        .ds1_awprot(),
        .ds1_awvalid(),
        .ds1_awready(1'b0),
        .ds1_wdata(),
        .ds1_wstrb(),
        .ds1_wlast(),
        .ds1_wvalid(),
        .ds1_wready(1'b0),
        .ds1_bid(13'b0),
        .ds1_bresp(2'b0),
        .ds1_bvalid(1'b0),
        .ds1_bready(),
        .ds1_arid(),
        .ds1_araddr(),
        .ds1_arlen(),
        .ds1_arsize(),
        .ds1_arburst(),
        .ds1_arlock(),
        .ds1_arcache(),
        .ds1_arprot(),
        .ds1_arvalid(),
        .ds1_arready(1'b0),
        .ds1_rid(13'b0),
        .ds1_rdata(32'b0),
        .ds1_rresp(2'b0),
        .ds1_rlast(1'b0),
        .ds1_rvalid(1'b0),
        .ds1_rready(),
        .ds1_awqos(),
        .ds1_awregion(),
        .ds1_arqos(),
        .ds1_arregion()
    );
    design_0_AXI_Protocol_Converter_1 design_0_AXI_Protocol_Converter_1_Inst (
        .us_axi_awid(w_port__AXI_Matrix_1__ds0_awid),
        .us_axi_awaddr(w_port__AXI_Matrix_1__ds0_awaddr),
        .us_axi_awlen(w_port__AXI_Matrix_1__ds0_awlen),
        .us_axi_awsize(w_port__AXI_Matrix_1__ds0_awsize),
        .us_axi_awburst(w_port__AXI_Matrix_1__ds0_awburst),
        .us_axi_awlock(w_port__AXI_Matrix_1__ds0_awlock),
        .us_axi_awcache(w_port__AXI_Matrix_1__ds0_awcache),
        .us_axi_awprot(w_port__AXI_Matrix_1__ds0_awprot),
        .us_axi_awvalid(w_port__AXI_Matrix_1__ds0_awvalid),
        .us_axi_awready(w_port__AXI_Protocol_Converter_1__us_axi_awready),
        .us_axi_wdata(w_port__AXI_Matrix_1__ds0_wdata),
        .us_axi_wstrb(w_port__AXI_Matrix_1__ds0_wstrb),
        .us_axi_wlast(w_port__AXI_Matrix_1__ds0_wlast),
        .us_axi_wvalid(w_port__AXI_Matrix_1__ds0_wvalid),
        .us_axi_wready(w_port__AXI_Protocol_Converter_1__us_axi_wready),
        .us_axi_bid(w_port__AXI_Protocol_Converter_1__us_axi_bid),
        .us_axi_bresp(w_port__AXI_Protocol_Converter_1__us_axi_bresp),
        .us_axi_bvalid(w_port__AXI_Protocol_Converter_1__us_axi_bvalid),
        .us_axi_bready(w_port__AXI_Matrix_1__ds0_bready),
        .us_axi_arid(w_port__AXI_Matrix_1__ds0_arid),
        .us_axi_araddr(w_port__AXI_Matrix_1__ds0_araddr),
        .us_axi_arlen(w_port__AXI_Matrix_1__ds0_arlen),
        .us_axi_arsize(w_port__AXI_Matrix_1__ds0_arsize),
        .us_axi_arburst(w_port__AXI_Matrix_1__ds0_arburst),
        .us_axi_arlock(w_port__AXI_Matrix_1__ds0_arlock),
        .us_axi_arcache(w_port__AXI_Matrix_1__ds0_arcache),
        .us_axi_arprot(w_port__AXI_Matrix_1__ds0_arprot),
        .us_axi_arvalid(w_port__AXI_Matrix_1__ds0_arvalid),
        .us_axi_arready(w_port__AXI_Protocol_Converter_1__us_axi_arready),
        .us_axi_rid(w_port__AXI_Protocol_Converter_1__us_axi_rid),
        .us_axi_rdata(w_port__AXI_Protocol_Converter_1__us_axi_rdata),
        .us_axi_rresp(w_port__AXI_Protocol_Converter_1__us_axi_rresp),
        .us_axi_rlast(w_port__AXI_Protocol_Converter_1__us_axi_rlast),
        .us_axi_rvalid(w_port__AXI_Protocol_Converter_1__us_axi_rvalid),
        .us_axi_rready(w_port__AXI_Matrix_1__ds0_rready),
        .us_axi_awuser(1'b0),
        .us_axi_awqos(w_port__AXI_Matrix_1__ds0_awqos),
        .us_axi_wuser(1'b0),
        .us_axi_buser(),
        .us_axi_aruser(1'b0),
        .us_axi_arqos(w_port__AXI_Matrix_1__ds0_arqos),
        .us_axi_ruser(),
        .aclk(w_port__ARM_Processor_System_0__p2f_clk0),
        .aresetn(w_port__rstn_npu__peripheral_aresetn),
        .ds_axi_awaddr(w_port__AXI_Protocol_Converter_1__ds_axi_awaddr),
        .ds_axi_awprot(w_port__AXI_Protocol_Converter_1__ds_axi_awprot),
        .ds_axi_awvalid(w_port__AXI_Protocol_Converter_1__ds_axi_awvalid),
        .ds_axi_awready(w_port__Video_NPU_0__s_axilite_awready_o),
        .ds_axi_wdata(w_port__AXI_Protocol_Converter_1__ds_axi_wdata),
        .ds_axi_wstrb(w_port__AXI_Protocol_Converter_1__ds_axi_wstrb),
        .ds_axi_wvalid(w_port__AXI_Protocol_Converter_1__ds_axi_wvalid),
        .ds_axi_wready(w_port__Video_NPU_0__s_axilite_wready_o),
        .ds_axi_bresp(w_port__Video_NPU_0__s_axilite_bresp_o),
        .ds_axi_bvalid(w_port__Video_NPU_0__s_axilite_bvalid_o),
        .ds_axi_bready(w_port__AXI_Protocol_Converter_1__ds_axi_bready),
        .ds_axi_araddr(w_port__AXI_Protocol_Converter_1__ds_axi_araddr),
        .ds_axi_arprot(w_port__AXI_Protocol_Converter_1__ds_axi_arprot),
        .ds_axi_arvalid(w_port__AXI_Protocol_Converter_1__ds_axi_arvalid),
        .ds_axi_arready(w_port__Video_NPU_0__s_axilite_arready_o),
        .ds_axi_rdata(w_port__Video_NPU_0__s_axilite_rdata_o),
        .ds_axi_rresp(w_port__Video_NPU_0__s_axilite_rresp_o),
        .ds_axi_rvalid(w_port__Video_NPU_0__s_axilite_rvalid_o),
        .ds_axi_rready(w_port__AXI_Protocol_Converter_1__ds_axi_rready)
    );
    design_0_Video_NPU_0 design_0_Video_NPU_0_Inst (
        .s_axilite_awaddr_i(w_port__AXI_Protocol_Converter_1__ds_axi_awaddr),
        .s_axilite_awprot_i(w_port__AXI_Protocol_Converter_1__ds_axi_awprot),
        .s_axilite_awvalid_i(w_port__AXI_Protocol_Converter_1__ds_axi_awvalid),
        .s_axilite_awready_o(w_port__Video_NPU_0__s_axilite_awready_o),
        .s_axilite_wdata_i(w_port__AXI_Protocol_Converter_1__ds_axi_wdata),
        .s_axilite_wstrb_i(w_port__AXI_Protocol_Converter_1__ds_axi_wstrb),
        .s_axilite_wvalid_i(w_port__AXI_Protocol_Converter_1__ds_axi_wvalid),
        .s_axilite_wready_o(w_port__Video_NPU_0__s_axilite_wready_o),
        .s_axilite_bresp_o(w_port__Video_NPU_0__s_axilite_bresp_o),
        .s_axilite_bvalid_o(w_port__Video_NPU_0__s_axilite_bvalid_o),
        .s_axilite_bready_i(w_port__AXI_Protocol_Converter_1__ds_axi_bready),
        .s_axilite_araddr_i(w_port__AXI_Protocol_Converter_1__ds_axi_araddr),
        .s_axilite_arprot_i(w_port__AXI_Protocol_Converter_1__ds_axi_arprot),
        .s_axilite_arvalid_i(w_port__AXI_Protocol_Converter_1__ds_axi_arvalid),
        .s_axilite_arready_o(w_port__Video_NPU_0__s_axilite_arready_o),
        .s_axilite_rdata_o(w_port__Video_NPU_0__s_axilite_rdata_o),
        .s_axilite_rresp_o(w_port__Video_NPU_0__s_axilite_rresp_o),
        .s_axilite_rvalid_o(w_port__Video_NPU_0__s_axilite_rvalid_o),
        .s_axilite_rready_i(w_port__AXI_Protocol_Converter_1__ds_axi_rready),
        .S_AXILite_ACLK(w_port__ARM_Processor_System_0__p2f_clk0),
        .S_AXILite_AResetn(w_port__rstn_npu__peripheral_aresetn),
        .M_AXI_ACLK(w_port__ARM_Processor_System_0__p2f_clk0),
        .M_AXI_AResetn(w_port__rstn_npu__peripheral_aresetn),
        .m_axi_awid_soft_o(w_port__Video_NPU_0__m_axi_awid_soft_o),
        .m_axi_awaddr_soft_o(w_port__Video_NPU_0__m_axi_awaddr_soft_o),
        .m_axi_awlen_soft_o(w_port__Video_NPU_0__m_axi_awlen_soft_o),
        .m_axi_awsize_soft_o(w_port__Video_NPU_0__m_axi_awsize_soft_o),
        .m_axi_awburst_soft_o(w_port__Video_NPU_0__m_axi_awburst_soft_o),
        .m_axi_awlock_soft_o(w_port__Video_NPU_0__m_axi_awlock_soft_o),
        .m_axi_awcache_soft_o(w_port__Video_NPU_0__m_axi_awcache_soft_o),
        .m_axi_awprot_soft_o(w_port__Video_NPU_0__m_axi_awprot_soft_o),
        .m_axi_awvalid_soft_o(w_port__Video_NPU_0__m_axi_awvalid_soft_o),
        .m_axi_awready_soft_i(w_port__ARM_Processor_System_0__slave_hp0_axi_awready),
        .m_axi_wdata_soft_o(w_port__Video_NPU_0__m_axi_wdata_soft_o),
        .m_axi_wstrb_soft_o(w_port__Video_NPU_0__m_axi_wstrb_soft_o),
        .m_axi_wlast_soft_o(w_port__Video_NPU_0__m_axi_wlast_soft_o),
        .m_axi_wvalid_soft_o(w_port__Video_NPU_0__m_axi_wvalid_soft_o),
        .m_axi_wready_soft_i(w_port__ARM_Processor_System_0__slave_hp0_axi_wready),
        .m_axi_bid_soft_i(w_port__ARM_Processor_System_0__slave_hp0_axi_bid),
        .m_axi_bresp_soft_i(w_port__ARM_Processor_System_0__slave_hp0_axi_bresp),
        .m_axi_bvalid_soft_i(w_port__ARM_Processor_System_0__slave_hp0_axi_bvalid),
        .m_axi_bready_soft_o(w_port__Video_NPU_0__m_axi_bready_soft_o),
        .m_axi_arid_soft_o(w_port__Video_NPU_0__m_axi_arid_soft_o),
        .m_axi_araddr_soft_o(w_port__Video_NPU_0__m_axi_araddr_soft_o),
        .m_axi_arlen_soft_o(w_port__Video_NPU_0__m_axi_arlen_soft_o),
        .m_axi_arsize_soft_o(w_port__Video_NPU_0__m_axi_arsize_soft_o),
        .m_axi_arburst_soft_o(w_port__Video_NPU_0__m_axi_arburst_soft_o),
        .m_axi_arlock_soft_o(w_port__Video_NPU_0__m_axi_arlock_soft_o),
        .m_axi_arcache_soft_o(w_port__Video_NPU_0__m_axi_arcache_soft_o),
        .m_axi_arprot_soft_o(w_port__Video_NPU_0__m_axi_arprot_soft_o),
        .m_axi_arvalid_soft_o(w_port__Video_NPU_0__m_axi_arvalid_soft_o),
        .m_axi_arready_soft_i(w_port__ARM_Processor_System_0__slave_hp0_axi_arready),
        .m_axi_rid_soft_i(w_port__ARM_Processor_System_0__slave_hp0_axi_rid),
        .m_axi_rdata_soft_i(w_port__ARM_Processor_System_0__slave_hp0_axi_rdata),
        .m_axi_rresp_soft_i(w_port__ARM_Processor_System_0__slave_hp0_axi_rresp),
        .m_axi_rlast_soft_i(w_port__ARM_Processor_System_0__slave_hp0_axi_rlast),
        .m_axi_rvalid_soft_i(w_port__ARM_Processor_System_0__slave_hp0_axi_rvalid),
        .m_axi_rready_soft_o(w_port__Video_NPU_0__m_axi_rready_soft_o),
        .npu_soft_intr(w_port__Video_NPU_0__npu_soft_intr)
    );
    design_0_igw_rs485_branch_0 design_0_igw_rs485_branch_0_Inst (
        .CAN_RX_OBSERVE(can0_rx_in),
        .aclk(w_port__ARM_Processor_System_0__p2f_clk0),
        .aresetn(w_port__rst_n_75m__interconnect_aresetn),
        .RS485_RX0_3V3(RS485_RX0_3V3),
        .RS485_TX0_3V3(RS485_TX0_3V3),
        .RS485_DE0_3V3(RS485_DE0_3V3),
        .s_axi_awaddr(w_port__AXI_Protocol_Converter_0__ds_axi_awaddr),
        .s_axi_awprot(w_port__AXI_Protocol_Converter_0__ds_axi_awprot),
        .s_axi_awvalid(w_port__AXI_Protocol_Converter_0__ds_axi_awvalid),
        .s_axi_awready(w_net__igw_rs485_branch_0__s_axi_awready),
        .s_axi_wdata(w_port__AXI_Protocol_Converter_0__ds_axi_wdata),
        .s_axi_wstrb(w_port__AXI_Protocol_Converter_0__ds_axi_wstrb),
        .s_axi_wvalid(w_port__AXI_Protocol_Converter_0__ds_axi_wvalid),
        .s_axi_wready(w_net__igw_rs485_branch_0__s_axi_wready),
        .s_axi_bresp(w_net__igw_rs485_branch_0__s_axi_bresp),
        .s_axi_bvalid(w_net__igw_rs485_branch_0__s_axi_bvalid),
        .s_axi_bready(w_port__AXI_Protocol_Converter_0__ds_axi_bready),
        .s_axi_araddr(w_port__AXI_Protocol_Converter_0__ds_axi_araddr),
        .s_axi_arprot(w_port__AXI_Protocol_Converter_0__ds_axi_arprot),
        .s_axi_arvalid(w_port__AXI_Protocol_Converter_0__ds_axi_arvalid),
        .s_axi_arready(w_net__igw_rs485_branch_0__s_axi_arready),
        .s_axi_rdata(w_net__igw_rs485_branch_0__s_axi_rdata),
        .s_axi_rresp(w_net__igw_rs485_branch_0__s_axi_rresp),
        .s_axi_rvalid(w_net__igw_rs485_branch_0__s_axi_rvalid),
        .s_axi_rready(w_port__AXI_Protocol_Converter_0__ds_axi_rready),
        .m_axi_awaddr(w_net__igw_rs485_branch_0__m_axi_awaddr),
        .m_axi_awprot(w_net__igw_rs485_branch_0__m_axi_awprot),
        .m_axi_awvalid(w_net__igw_rs485_branch_0__m_axi_awvalid),
        .m_axi_awready(w_port__AXI_Matrix_0__us0_awready),
        .m_axi_wdata(w_net__igw_rs485_branch_0__m_axi_wdata),
        .m_axi_wstrb(w_net__igw_rs485_branch_0__m_axi_wstrb),
        .m_axi_wvalid(w_net__igw_rs485_branch_0__m_axi_wvalid),
        .m_axi_wready(w_port__AXI_Matrix_0__us0_wready),
        .m_axi_bresp(w_port__AXI_Matrix_0__us0_bresp),
        .m_axi_bvalid(w_port__AXI_Matrix_0__us0_bvalid),
        .m_axi_bready(w_net__igw_rs485_branch_0__m_axi_bready),
        .m_axi_araddr(w_net__igw_rs485_branch_0__m_axi_araddr),
        .m_axi_arprot(w_net__igw_rs485_branch_0__m_axi_arprot),
        .m_axi_arvalid(w_net__igw_rs485_branch_0__m_axi_arvalid),
        .m_axi_arready(w_port__AXI_Matrix_0__us0_arready),
        .m_axi_rdata(w_port__AXI_Matrix_0__us0_rdata),
        .m_axi_rresp(w_port__AXI_Matrix_0__us0_rresp),
        .m_axi_rvalid(w_port__AXI_Matrix_0__us0_rvalid),
        .m_axi_rready(w_net__igw_rs485_branch_0__m_axi_rready)
    );
    igw_axi_reject igw_removed_peripherals (
        .aclk(w_port__ARM_Processor_System_0__p2f_clk0),
        .aresetn(w_port__rst_n_75m__interconnect_aresetn),
        .s_axi_awaddr(w_net__igw_rs485_branch_0__m_axi_awaddr),
        .s_axi_awprot(w_net__igw_rs485_branch_0__m_axi_awprot),
        .s_axi_awvalid(w_net__igw_rs485_branch_0__m_axi_awvalid),
        .s_axi_awready(w_port__AXI_Matrix_0__us0_awready),
        .s_axi_wdata(w_net__igw_rs485_branch_0__m_axi_wdata),
        .s_axi_wstrb(w_net__igw_rs485_branch_0__m_axi_wstrb),
        .s_axi_wvalid(w_net__igw_rs485_branch_0__m_axi_wvalid),
        .s_axi_wready(w_port__AXI_Matrix_0__us0_wready),
        .s_axi_bresp(w_port__AXI_Matrix_0__us0_bresp),
        .s_axi_bvalid(w_port__AXI_Matrix_0__us0_bvalid),
        .s_axi_bready(w_net__igw_rs485_branch_0__m_axi_bready),
        .s_axi_araddr(w_net__igw_rs485_branch_0__m_axi_araddr),
        .s_axi_arprot(w_net__igw_rs485_branch_0__m_axi_arprot),
        .s_axi_arvalid(w_net__igw_rs485_branch_0__m_axi_arvalid),
        .s_axi_arready(w_port__AXI_Matrix_0__us0_arready),
        .s_axi_rdata(w_port__AXI_Matrix_0__us0_rdata),
        .s_axi_rresp(w_port__AXI_Matrix_0__us0_rresp),
        .s_axi_rvalid(w_port__AXI_Matrix_0__us0_rvalid),
        .s_axi_rready(w_net__igw_rs485_branch_0__m_axi_rready)
    );
endmodule
