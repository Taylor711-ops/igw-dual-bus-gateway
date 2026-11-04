`timescale 1ns/1ps
module tb_gateway_decode;
reg clk=0;always #10 clk=~clk;
reg rst=0;
reg [32:0] awaddr=0,araddr=0;
reg [31:0] wdata=0;
reg [3:0] wstrb=15;
reg awvalid=0,wvalid=0,bready=0,arvalid=0,rready=0;
wire awready,wready,bvalid,arready,rvalid,tx,de;
wire [1:0] bresp,rresp;
wire [31:0] rdata;
wire [32:0] ma,ra;
wire [2:0] mp,rp;
wire [31:0] md,rd;
wire [3:0] ms;
wire mav,mar,mwv,mwr,mbv,mbr,rav,rar,rv,rr;
wire [1:0] mbresp,rrsp;
integer checks=0,reads=0,writes=0;
igw_rs485_branch dut(
 .aclk(clk),.aresetn(rst),.CAN_RX_OBSERVE(1'b1),.RS485_RX0_3V3(1'b1),.RS485_TX0_3V3(tx),.RS485_DE0_3V3(de),
 .s_axi_awaddr(awaddr),.s_axi_awprot(3'b0),.s_axi_awvalid(awvalid),.s_axi_awready(awready),
 .s_axi_wdata(wdata),.s_axi_wstrb(wstrb),.s_axi_wvalid(wvalid),.s_axi_wready(wready),
 .s_axi_bresp(bresp),.s_axi_bvalid(bvalid),.s_axi_bready(bready),
 .s_axi_araddr(araddr),.s_axi_arprot(3'b0),.s_axi_arvalid(arvalid),.s_axi_arready(arready),
 .s_axi_rdata(rdata),.s_axi_rresp(rresp),.s_axi_rvalid(rvalid),.s_axi_rready(rready),
 .m_axi_awaddr(ma),.m_axi_awprot(mp),.m_axi_awvalid(mav),.m_axi_awready(mar),
 .m_axi_wdata(md),.m_axi_wstrb(ms),.m_axi_wvalid(mwv),.m_axi_wready(mwr),
 .m_axi_bresp(mbresp),.m_axi_bvalid(mbv),.m_axi_bready(mbr),
 .m_axi_araddr(ra),.m_axi_arprot(rp),.m_axi_arvalid(rav),.m_axi_arready(rar),
 .m_axi_rdata(rd),.m_axi_rresp(rrsp),.m_axi_rvalid(rv),.m_axi_rready(rr));
igw_axi_reject reject(
 .aclk(clk),.aresetn(rst),
 .s_axi_awaddr(ma),.s_axi_awprot(mp),.s_axi_awvalid(mav),.s_axi_awready(mar),
 .s_axi_wdata(md),.s_axi_wstrb(ms),.s_axi_wvalid(mwv),.s_axi_wready(mwr),
 .s_axi_bresp(mbresp),.s_axi_bvalid(mbv),.s_axi_bready(mbr),
 .s_axi_araddr(ra),.s_axi_arprot(rp),.s_axi_arvalid(rav),.s_axi_arready(rar),
 .s_axi_rdata(rd),.s_axi_rresp(rrsp),.s_axi_rvalid(rv),.s_axi_rready(rr));
task ck(input bit ok,input string msg); checks++;if(!ok)$fatal(1,"%s",msg);endtask
task rdreg(input [32:0] addr,input [1:0] expected_resp,input [31:0] expected_data);
 reg [31:0] saved;reg [1:0] resp;
 @(negedge clk);araddr=addr;arvalid=1;
 @(posedge clk);while(!arready)@(posedge clk);
 @(negedge clk);arvalid=0;araddr=33'h1ffffffff;
 wait(rvalid);saved=rdata;resp=rresp;
 ck(resp==expected_resp&&saved==expected_data,"read value/response");
 repeat(7)begin @(negedge clk);ck(rvalid&&rdata===saved&&rresp===resp,"read backpressure stability");end
 rready=1;@(negedge clk);rready=0;reads++;
endtask
task wrreg(input [32:0] addr,input [31:0] data,input integer order,input [1:0] expected_resp);
 reg [1:0] saved;
 if(order==0)begin
  @(negedge clk);awaddr=addr;awvalid=1;
  @(posedge clk);while(!awready)@(posedge clk);
  @(negedge clk);awvalid=0;
  repeat(3)begin @(negedge clk);ck(!bvalid,"AW alone cannot acknowledge");end
  wdata=data;wvalid=1;@(posedge clk);while(!wready)@(posedge clk);
  @(negedge clk);wvalid=0;
 end else if(order==1)begin
  @(negedge clk);wdata=data;wvalid=1;
  @(posedge clk);while(!wready)@(posedge clk);
  @(negedge clk);wvalid=0;
  repeat(3)begin @(negedge clk);ck(!bvalid,"W alone cannot acknowledge");end
  awaddr=addr;awvalid=1;@(posedge clk);while(!awready)@(posedge clk);
  @(negedge clk);awvalid=0;
 end else begin
  @(negedge clk);awaddr=addr;awvalid=1;wdata=data;wvalid=1;
  fork
   begin @(posedge clk);while(!awready)@(posedge clk);@(negedge clk);awvalid=0;end
   begin @(posedge clk);while(!wready)@(posedge clk);@(negedge clk);wvalid=0;end
  join
 end
 awaddr=33'h1ffffffff;wdata=0;
 wait(bvalid);saved=bresp;ck(saved==expected_resp,"write response");
 repeat(7)begin @(negedge clk);ck(bvalid&&bresp===saved,"write backpressure stability");end
 bready=1;@(negedge clk);bready=0;writes++;
endtask
initial begin
 #1;rst=1;#1;rst=0;repeat(5)@(negedge clk);
 ck(!awready&&!wready&&!arready,"reset handshake gating");rst=1;
 repeat(5)@(negedge clk);
 rdreg(33'h080400000,0,32'h32574749);
 rdreg(33'h080400018,0,50000000);
 rdreg(33'h080400800,0,32'h4e414349);
 wrreg(33'h080400020,12500,1,0);
 rdreg(33'h080400020,0,12500);
 // All three removed windows, unmapped addresses and 33-bit non-aliasing.
 for(integer i=0;i<72;i++)begin
  case(i%6)
   0:araddr=33'h080100000+(i*4);
   1:araddr=33'h080200000+(i*4);
   2:araddr=33'h080300000+(i*4);
   3:araddr=33'h180400000;
   4:araddr=33'h180400800;
   5:araddr=33'h07ffff000+(i*4);
  endcase
  begin reg[32:0] address;address=araddr;
   fork
    rdreg(address,3,0);
    wrreg(address,32'h11223344^i,i%3,3);
   join
  end
  ck(de==0,"no RS485 transmission from removed MMIO");
 end
 rdreg(33'h080400020,0,12500); // removed/unmapped writes cannot alter local bank
 rdreg(33'h08040000c,0,0); // TX authorization stays disabled
 // Reset discards a half-collected write, without a late B acknowledgement.
 @(negedge clk);awaddr=33'h080100000;awvalid=1;
 @(posedge clk);while(!awready)@(posedge clk);
 @(negedge clk);awvalid=0;rst=0;
 repeat(4)@(negedge clk);rst=1;
 repeat(12)begin @(negedge clk);ck(!bvalid&&!rvalid,"reset flushes unfinished transaction");end
 rdreg(33'h080400000,0,32'h32574749);
 $display("R15_DECODE_PASS checks=%0d reads=%0d writes=%0d",checks,reads,writes);$finish;
end
initial begin #2000000;$fatal(1,"watchdog");end
endmodule
