// Fixed 8 data bits, even parity, one stop bit; 2-flop receive synchronizer.
module igw_uart_rx_8e1 #(parameter integer BIT_CLKS=5208)(
 input wire clk,rst_n,enable,rxd,
 output reg [7:0] data, output reg valid,error,started,busy
);
 reg rxd_meta,rxd_sync;
 reg [31:0] timer;
 reg [3:0] phase;
 reg [7:0] sample;
 reg parity_ok;
 always @(posedge clk or negedge rst_n) begin
  if(!rst_n) begin rxd_meta<=1; rxd_sync<=1; end
  else begin rxd_meta<=rxd; rxd_sync<=rxd_meta; end
 end
 always @(posedge clk or negedge rst_n) begin
  if(!rst_n) begin data<=0;valid<=0;error<=0;started<=0;busy<=0;timer<=0;phase<=0;sample<=0;parity_ok<=0;end
  else begin
   valid<=0;error<=0;started<=0;
   if(!enable) begin busy<=0;timer<=0;phase<=0;end
   else if(!busy) begin
    if(!rxd_sync) begin busy<=1;timer<=(BIT_CLKS/2)-1;phase<=0;started<=1;end
   end else if(timer!=0) timer<=timer-1'b1;
   else begin
    timer<=BIT_CLKS-1;
    if(phase==0) begin
      if(rxd_sync) begin busy<=0;error<=1;end else phase<=1;
    end else if(phase<=8) begin
      sample[phase-1]<=rxd_sync;phase<=phase+1'b1;
    end else if(phase==9) begin
      parity_ok<=(rxd_sync == ^sample);phase<=10;
    end else begin
      busy<=0;
      if(rxd_sync && parity_ok) begin data<=sample;valid<=1;end else error<=1;
    end
   end
  end
 end
endmodule

module igw_uart_tx_8e1 #(parameter integer BIT_CLKS=5208)(
 input wire clk,rst_n,start,input wire [7:0] data,
 output wire txd, output reg busy
);
 reg [10:0] shift;
 reg [3:0] remaining;
 reg [31:0] timer;
 assign txd=busy?shift[0]:1'b1;
 always @(posedge clk or negedge rst_n) begin
  if(!rst_n) begin busy<=0;shift<=11'h7ff;remaining<=0;timer<=0;end
  else if(!busy) begin
   if(start) begin shift<={1'b1,^data,data,1'b0};remaining<=11;timer<=BIT_CLKS-1;busy<=1;end
  end else if(timer!=0) timer<=timer-1'b1;
  else begin
   timer<=BIT_CLKS-1;
   if(remaining==1) begin busy<=0;remaining<=0;end
   else begin shift<={1'b1,shift[10:1]};remaining<=remaining-1'b1;end
  end
 end
endmodule
