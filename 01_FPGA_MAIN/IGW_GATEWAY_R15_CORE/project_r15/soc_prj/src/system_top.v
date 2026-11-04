// R15 gateway-only top. Local display is the Ethernet web page.
module system_top (
    input wire RS485_RX0_3V3,
    output wire RS485_TX0_3V3, RS485_DE0_3V3,
    input wire can0_rx_in,
    output wire can0_tx_out,
    inout wire SCL0, SDA0,
    output wire [2:0] led
);
    wire clk50, ready50, npu_pll_locked;
    reg [24:0] heartbeat;
    always @(posedge clk50 or negedge ready50)
        if (!ready50) heartbeat <= 25'b0;
        else heartbeat <= heartbeat + 1'b1;
    assign led = {npu_pll_locked, ready50, heartbeat[24]};
    design_0 design_0_inst (
        .RS485_RX0_3V3(RS485_RX0_3V3), .RS485_TX0_3V3(RS485_TX0_3V3),
        .RS485_DE0_3V3(RS485_DE0_3V3), .can0_rx_in(can0_rx_in),
        .can0_tx_out(can0_tx_out), .mipi_iic_i2c0_scl_io(SCL0),
        .mipi_iic_i2c0_sda_io(SDA0), .p2f_clk0_50m(clk50),
        .gateway_ready(ready50), .npu_clock_locked(npu_pll_locked)
    );
endmodule
