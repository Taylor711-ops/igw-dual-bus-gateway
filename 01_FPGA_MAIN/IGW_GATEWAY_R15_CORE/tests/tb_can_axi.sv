`timescale 1ns/1ps
module tb_can_axi;
reg clk=0;always #10 clk=~clk;
reg rst=0,rx=1;reg[7:0]aw=0,ar=0;reg av=0,wv=0,br=0,arv=0,rr=0;reg[31:0]wd=0;reg[3:0]ws=15;
wire awr,wr,bv,arr,rv;wire[1:0]bre,rre;wire[31:0]rd;
igw_can_observer_axi dut(clk,rst,rx,aw,av,awr,wd,ws,wv,wr,bre,bv,br,ar,arv,arr,rd,rre,rv,rr);
integer rc,f,n,v,i;reg[2047:0]path;reg[31:0]old;
task read_reg(input[7:0]addr,input[31:0]expected,input[1:0]resp);
 begin
 @(negedge clk);ar=addr;arv=1;rr=0;
 do @(posedge clk);while(!arr);
 @(negedge clk);arv=0;while(!rv)@(negedge clk);
 if(rd!==expected || rre!==resp)$fatal(1,"read %h got %h/%h expected %h/%h",addr,rd,rre,expected,resp);
 repeat(4)begin @(negedge clk);if(!rv||rd!==expected||rre!==resp)$fatal(1,"R backpressure instability");end
 rr=1;@(negedge clk);rr=0;
 end
endtask
task write_reg(input[7:0]addr,input[31:0]value,input[3:0]strobe,input integer order,input[1:0]resp);
 begin
 @(negedge clk);aw=addr;wd=value;ws=strobe;br=0;
 if(order==0)begin av=1;do @(posedge clk);while(!awr);@(negedge clk);av=0;repeat(3)@(negedge clk);wv=1;do @(posedge clk);while(!wr);@(negedge clk);wv=0;end
 else begin wv=1;do @(posedge clk);while(!wr);@(negedge clk);wv=0;repeat(3)@(negedge clk);av=1;do @(posedge clk);while(!awr);@(negedge clk);av=0;end
 while(!bv)@(negedge clk);
 if(bre!==resp)$fatal(1,"write response %h expected %h",bre,resp);
 repeat(4)begin @(negedge clk);if(!bv||bre!==resp)$fatal(1,"B backpressure instability");end
 br=1;@(negedge clk);br=0;
 end
endtask
// FIFO boundary unit tests inject the VALID pulse only. Serial decoder is
// separately exercised below, and by test_receiver.py; no RX claim uses force.
task inject;
 begin @(negedge clk);force dut.valid=1;@(negedge clk);release dut.valid;@(negedge clk);end
endtask
initial begin
 repeat(5)@(negedge clk);rst=1;
 read_reg(0,32'h4e414349,0);read_reg(4,32'h10000,0);read_reg(8,31,0);
 read_reg('h18,50000000,0);read_reg('h1c,500000,0);read_reg('h44,16,0);
 read_reg('h80,0,2);read_reg(1,0,2);read_reg('h48,0,2);
 write_reg('h0c,1,15,0,2);write_reg('h0c,1,7,1,2);write_reg('h00,1,15,0,2);
 if(!$value$plusargs("STIM=%s",path))$fatal;
 f=$fopen(path,"r");while(!$feof(f))begin rc=$fscanf(f,"%d %d\n",n,v);if(rc==2)begin rx=v;repeat(n)@(negedge clk);end end
 $fclose(f);repeat(5)@(negedge clk);
 read_reg('h14,1,0);read_reg('h20,1,0);read_reg('h80,0,0);read_reg('h84,32'h211,0);
 // The genuine serial frame is ID211, DLC8, bytes 01 23 45 67 89 ab cd ef.
 read_reg('h94,32'h67452301,0);read_reg('h98,32'hefcdab89,0);
 for(i=0;i<20;i=i+1)inject;
 read_reg('h14,16,0);read_reg('h20,21,0);read_reg('h24,5,0);read_reg('h80,0,0);
 for(i=0;i<16;i=i+1)begin read_reg('h80,i,0);write_reg('h0c,1,15,i%2,0);end
 read_reg('h14,0,0);read_reg('h40,16,0);
 // Refill after wrapping both pointers; dropped sequence IDs must be visible.
 for(i=0;i<16;i=i+1)inject;
 read_reg('h80,21,0);read_reg('h14,16,0);
 // Simultaneous POP and PUSH at full: no drop and old unread head advances once.
 @(negedge clk);aw='h0c;wd=1;ws=15;av=1;wv=1;br=0;
 @(negedge clk);av=0;wv=0;force dut.valid=1;
 @(negedge clk);release dut.valid;
 while(!bv)@(negedge clk);if(bre!==0)$fatal;br=1;@(negedge clk);br=0;
 read_reg('h14,16,0);read_reg('h24,5,0);read_reg('h80,22,0);
 for(i=22;i<=37;i=i+1)begin read_reg('h80,i,0);write_reg('h0c,1,15,i%2,0);end
 read_reg('h14,0,0);read_reg('h40,33,0);read_reg('h20,38,0);
 // Invalid empty POP coincident with a new frame: retain arriving record.
 @(negedge clk);aw='h0c;wd=1;ws=15;av=1;wv=1;br=0;
 @(negedge clk);av=0;wv=0;force dut.valid=1;
 @(negedge clk);release dut.valid;
 while(!bv)@(negedge clk);if(bre!==2)$fatal;br=1;@(negedge clk);br=0;
 read_reg('h14,1,0);read_reg('h80,38,0);
 // Reset flushes FIFO, pending handshakes and counters.
 rst=0;repeat(3)@(negedge clk);rst=1;repeat(3)@(negedge clk);
 read_reg('h14,0,0);read_reg('h20,0,0);read_reg('h24,0,0);read_reg('h40,0,0);
 $display("PASS AXI: serial record, R/B stalls, independent AW/W, invalid accesses, overflow, wrap, simultaneous push/pop, reset");$finish;
end
initial begin #10000000;$fatal(1,"watchdog");end
endmodule
