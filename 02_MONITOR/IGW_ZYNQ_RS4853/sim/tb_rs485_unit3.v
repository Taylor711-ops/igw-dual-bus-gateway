`timescale 1ns/1ps
module tb_rs485_unit3;
 localparam BIT_CLKS=32;
 localparam BIT_NS=BIT_CLKS*20;
 reg clk=0, rst_n=0, rxd=1;
 wire txd;
 wire [1:0] led;
 reg [7:0] request[0:7];
 reg [7:0] expected[0:20];
 reg [7:0] actual;
 integer tx_edges=0;
 integer i;
 always #10 clk=~clk;
 always @(negedge txd) tx_edges=tx_edges+1;
 rs485_key_led #(.CLK_FREQ(320000),.UART_BPS(10000)) dut(
  .sys_clk(clk),.sys_rst_n(rst_n),.key(2'b11),.led(led),
  .rs485_uart_rxd(rxd),.rs485_uart_txd(txd));
 function [15:0] crc_byte;
  input [15:0] c; input [7:0] d;
  reg [15:0] t; integer b;
  begin t=c^{8'b0,d};for(b=0;b<8;b=b+1)t=t[0]?((t>>1)^16'ha001):(t>>1);crc_byte=t;end
 endfunction
 task build_request;
  input [7:0] unit,func;input [15:0] address,argument;
  reg [15:0] c;integer k;
  begin
   request[0]=unit;request[1]=func;request[2]=address[15:8];request[3]=address[7:0];
   request[4]=argument[15:8];request[5]=argument[7:0];c=16'hffff;
   for(k=0;k<6;k=k+1)c=crc_byte(c,request[k]);request[6]=c[7:0];request[7]=c[15:8];
  end
 endtask
 task send_byte;
  input [7:0] value;input bad_parity; integer b;
  begin
   rxd=0;#(BIT_NS);
   for(b=0;b<8;b=b+1)begin rxd=value[b];#(BIT_NS);end
   rxd=(^value)^bad_parity;#(BIT_NS);rxd=1;#(BIT_NS);
  end
 endtask
 task send_request;
  input integer corrupt_parity_index;integer k;
  begin for(k=0;k<8;k=k+1)send_byte(request[k],k==corrupt_parity_index);end
 endtask
 task get_byte;
  output [7:0] value;reg par;integer b;
  begin
   @(negedge txd);#(BIT_NS+BIT_NS/2);
   for(b=0;b<8;b=b+1)begin value[b]=txd;#(BIT_NS);end
   par=txd;#(BIT_NS);
   if(par !== ^value || txd !== 1'b1) begin $display("FAIL: reply parity or stop");$fatal(1);end
  end
 endtask
 task expect_response;
  input integer payload_bytes;reg [15:0] c;integer k;
  begin
   c=16'hffff;for(k=0;k<payload_bytes;k=k+1)c=crc_byte(c,expected[k]);
   expected[payload_bytes]=c[7:0];expected[payload_bytes+1]=c[15:8];
   for(k=0;k<payload_bytes+2;k=k+1)begin
    get_byte(actual);
    if(actual !== expected[k])begin
     $display("FAIL response byte %0d got %02x expected %02x",k,actual,expected[k]);$fatal(1);
    end
   end
   #(BIT_NS*40); // end of stop bit plus inter-frame silence
  end
 endtask
 task expect_silent;
  input integer edge_before;
  begin #(BIT_NS*100); if(tx_edges!=edge_before)begin $display("FAIL: unexpected response");$fatal(1);end end
 endtask
 task expect_word16;
  begin
   build_request(3,3,16,1);send_request(-1);
   expected[0]=3;expected[1]=3;expected[2]=2;expected[3]=8'h30;expected[4]=8'h39;
   expect_response(5);
  end
 endtask
 integer before_edges;
 initial begin
  // A failed test cannot hang indefinitely if no response arrives.
  #20000000; $display("FAIL: timeout");$fatal(1);
 end
 initial begin
  #200;rst_n=1;#(BIT_NS*5);
  before_edges=tx_edges;expect_silent(before_edges);
  build_request(1,3,0,1);before_edges=tx_edges;send_request(-1);expect_silent(before_edges);
  build_request(3,3,0,2);send_request(-1);
  expected[0]=3;expected[1]=3;expected[2]=4;expected[3]=8'h12;expected[4]=8'h34;expected[5]=0;expected[6]=3;
  expect_response(7);
  build_request(3,6,16,16'h3039);send_request(-1);
  for(i=0;i<6;i=i+1)expected[i]=request[i];expect_response(6);
  expect_word16();
  build_request(3,3,4,4);send_request(-1);
  expected[0]=3;expected[1]=3;expected[2]=8;expected[3]=8'h08;expected[4]=1;
  expected[5]=0;expected[6]=1;expected[7]=0;expected[8]=16;expected[9]=8'h30;expected[10]=8'h39;
  expect_response(11);
  build_request(3,6,16,16'h1111);request[6]=request[6]^1;before_edges=tx_edges;send_request(-1);expect_silent(before_edges);expect_word16();
  build_request(3,6,16,16'h2222);before_edges=tx_edges;send_request(4);expect_silent(before_edges);expect_word16();
  build_request(0,6,16,16'h3333);before_edges=tx_edges;send_request(-1);expect_silent(before_edges);expect_word16();
  build_request(3,6,27,16'h4444);send_request(-1);expected[0]=3;expected[1]=8'h86;expected[2]=2;expect_response(3);
  build_request(3,3,0,9);send_request(-1);expected[0]=3;expected[1]=8'h83;expected[2]=3;expect_response(3);
  $display("TB_RESULT=PASS: unit filtering, 8E1, read/write/readback, CRC, parity, broadcast, exceptions");
  $finish;
 end
endmodule
