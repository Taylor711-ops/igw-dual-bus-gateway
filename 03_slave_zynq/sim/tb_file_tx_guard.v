`timescale 1ns/1ps
module tb_file_tx_guard;
    reg clk=0,rst_n=0,rxd=1,ps_txd=0,pl_txd=1;
    wire txd;
    always #5 clk=~clk;
    file_tx_guard #(.CLK_FREQ(320000),.UART_BPS(10000),.WINDOW_CLKS(3000)) dut(
        .clk(clk),.rst_n(rst_n),.rxd(rxd),.ps_txd(ps_txd),.pl_txd(pl_txd),.txd(txd));
    task bit_value(input value);
        begin @(negedge clk);rxd=value;repeat(32) @(negedge clk);end
    endtask
    task send_byte(input [7:0] value);
        integer i;begin
          bit_value(0);for(i=0;i<8;i=i+1)bit_value(value[i]);
          bit_value(^value);bit_value(1);
        end
    endtask
    initial begin
        repeat(4) @(negedge clk);rst_n=1;repeat(20) @(negedge clk);
        if(txd!==1) $fatal(1,"uninitialized PS UART must not drive the bus");
        ps_txd=1;send_byte(3);send_byte(3);repeat(1500) @(negedge clk);
        ps_txd=0;repeat(5) @(negedge clk);
        if(txd!==1) $fatal(1,"numeric request must not enable PS output");
        ps_txd=1;send_byte(3);send_byte(8'h42);repeat(30) @(negedge clk);
        ps_txd=0;repeat(5) @(negedge clk);
        if(txd!==0) $fatal(1,"file request must enable PS output");
        ps_txd=1;repeat(3200) @(negedge clk);ps_txd=0;repeat(5) @(negedge clk);
        if(txd!==1) $fatal(1,"expired file window must close");
        pl_txd=0;repeat(5) @(negedge clk);
        if(txd!==0) $fatal(1,"PL numeric transmitter must remain connected");
        $display("TB_GUARD=PASS: startup isolation, function routing, expiry, PL TX");
        $finish;
    end
endmodule
