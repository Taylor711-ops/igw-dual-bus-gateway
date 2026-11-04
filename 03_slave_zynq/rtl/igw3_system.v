/* Board-specific top.  Original PL numeric/uplink design is unchanged. */
module igw3_system(
    inout [14:0] DDR_addr,inout [2:0] DDR_ba,
    inout DDR_cas_n,DDR_ck_n,DDR_ck_p,DDR_cke,DDR_cs_n,
    inout [3:0] DDR_dm,inout [31:0] DDR_dq,inout [3:0] DDR_dqs_n,DDR_dqs_p,
    inout DDR_odt,DDR_ras_n,DDR_reset_n,DDR_we_n,
    inout FIXED_IO_ddr_vrn,FIXED_IO_ddr_vrp,inout [53:0] FIXED_IO_mio,
    inout FIXED_IO_ps_clk,FIXED_IO_ps_porb,FIXED_IO_ps_srstb,
    inout [0:0] GPIO_EMIO_KEY_tri_io,
    input sys_clk,sys_rst_n,key1,output [1:0] led,
    input rs485_uart_rxd,output rs485_uart_txd
);
    wire ps_tx,pl_tx;
    system_wrapper ps(
        .DDR_addr(DDR_addr),.DDR_ba(DDR_ba),.DDR_cas_n(DDR_cas_n),
        .DDR_ck_n(DDR_ck_n),.DDR_ck_p(DDR_ck_p),.DDR_cke(DDR_cke),.DDR_cs_n(DDR_cs_n),
        .DDR_dm(DDR_dm),.DDR_dq(DDR_dq),.DDR_dqs_n(DDR_dqs_n),.DDR_dqs_p(DDR_dqs_p),
        .DDR_odt(DDR_odt),.DDR_ras_n(DDR_ras_n),.DDR_reset_n(DDR_reset_n),.DDR_we_n(DDR_we_n),
        .FIXED_IO_ddr_vrn(FIXED_IO_ddr_vrn),.FIXED_IO_ddr_vrp(FIXED_IO_ddr_vrp),
        .FIXED_IO_mio(FIXED_IO_mio),.FIXED_IO_ps_clk(FIXED_IO_ps_clk),
        .FIXED_IO_ps_porb(FIXED_IO_ps_porb),.FIXED_IO_ps_srstb(FIXED_IO_ps_srstb),
        .GPIO_EMIO_KEY_tri_io(GPIO_EMIO_KEY_tri_io),
        .UART1_rxd(rs485_uart_rxd),.UART1_txd(ps_tx));
    rs485_key_led established_pl(
        .sys_clk(sys_clk),.sys_rst_n(sys_rst_n),.key({key1,GPIO_EMIO_KEY_tri_io[0]}),
        .led(led),.rs485_uart_rxd(rs485_uart_rxd),.rs485_uart_txd(pl_tx));
    file_tx_guard guard(.clk(sys_clk),.rst_n(sys_rst_n),.rxd(rs485_uart_rxd),
                        .ps_txd(ps_tx),.pl_txd(pl_tx),.txd(rs485_uart_txd));
endmodule
