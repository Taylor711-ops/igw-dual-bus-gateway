// Digital default-9600 regression: independent main 50MHz and peer 100MHz clocks.
// Physical transceiver, cabling and FPGA STA are outside this simulation.
`timescale 1ns/1ps
// Digital model of two physical transceivers on one idle-biased RS485 bus.
// The register-driving tasks represent PS transactions, not an actual Linux BSP.
module tb_rs485_default_9600;
    reg clk_a=0;always #10 clk_a=~clk_a;
    reg clk_b=0;initial begin #3;forever #5 clk_b=~clk_b;end
    wire [1:0] clk={clk_b,clk_a};
    reg main_bit_checked=0,peer_bit_checked=0;
    realtime main_start,peer_start;
    reg rst_n=0;
    reg [11:0] awaddr[0:1],araddr[0:1];
    reg [31:0] wdata[0:1];reg [3:0] wstrb[0:1];
    reg awvalid[0:1],wvalid[0:1],bready[0:1],arvalid[0:1],rready[0:1];
    wire awready[0:1],wready[0:1],bvalid[0:1],arready[0:1],rvalid[0:1];
    wire [1:0] bresp[0:1],rresp[0:1];wire [31:0] rdata[0:1];
    wire tx[0:1],de[0:1],irq[0:1];
    wire bus_line=de[0] ? tx[0] : de[1] ? tx[1] : 1'b1;
    integer assertions=0;reg [31:0] value;
    genvar g;
    generate for(g=0;g<2;g=g+1)begin : board
        igw_rs485_axi #(.CLOCK_HZ(g==0 ? 50000000 : 100000000),.DEFAULT_BAUD(9600)) dut(
          .aclk(clk[g]),.aresetn(rst_n),.RS485_RX0_3V3(bus_line),.RS485_TX0_3V3(tx[g]),.RS485_DE0_3V3(de[g]),
          .s_axi_awaddr(awaddr[g]),.s_axi_awprot(3'b0),.s_axi_awvalid(awvalid[g]),.s_axi_awready(awready[g]),
          .s_axi_wdata(wdata[g]),.s_axi_wstrb(wstrb[g]),.s_axi_wvalid(wvalid[g]),.s_axi_wready(wready[g]),
          .s_axi_bresp(bresp[g]),.s_axi_bvalid(bvalid[g]),.s_axi_bready(bready[g]),
          .s_axi_araddr(araddr[g]),.s_axi_arprot(3'b0),.s_axi_arvalid(arvalid[g]),.s_axi_arready(arready[g]),
          .s_axi_rdata(rdata[g]),.s_axi_rresp(rresp[g]),.s_axi_rvalid(rvalid[g]),.s_axi_rready(rready[g]),.irq(irq[g]));
    end endgenerate
    always @(posedge clk_a) if(rst_n && de[0] && de[1])$fatal(1,"Two boards drove RS485 simultaneously");
    task check(input bit ok,input string message);
        begin assertions=assertions+1;if(!ok)$fatal(1,"%s",message);end
    endtask
    task clocks(input integer id,input integer n);begin repeat(n)@(negedge clk[id]);end endtask
    task wr(input integer id,input [11:0] address,input [31:0] data);
        begin
            @(negedge clk[id]);awaddr[id]=address;awvalid[id]=1;@(posedge clk[id]);while(!awready[id])@(posedge clk[id]);@(negedge clk[id]);awvalid[id]=0;
            wdata[id]=data;wstrb[id]=15;wvalid[id]=1;@(posedge clk[id]);while(!wready[id])@(posedge clk[id]);@(negedge clk[id]);wvalid[id]=0;
            while(!bvalid[id])clocks(id,1);check(bresp[id]==0,"AXI write accepted");bready[id]=1;clocks(id,1);bready[id]=0;clocks(id,1);
        end
    endtask
    task rd(input integer id,input [11:0] address,output [31:0] data);
        begin
            @(negedge clk[id]);araddr[id]=address;arvalid[id]=1;clocks(id,1);arvalid[id]=0;
            while(!rvalid[id])clocks(id,1);check(rresp[id]==0,"AXI read accepted");data=rdata[id];rready[id]=1;clocks(id,1);rready[id]=0;clocks(id,1);
        end
    endtask
    // First request/reply byte is 0x01: first rising TX edge ends its start bit.
    initial begin
        wait(rst_n);@(negedge tx[0]);main_start=$realtime;@(posedge tx[0]);
        check(($realtime-main_start)>104159 && ($realtime-main_start)<104161,"50MHz default start bit is 104160ns");
        main_bit_checked=1;
    end
    initial begin
        wait(rst_n);@(negedge tx[1]);peer_start=$realtime;@(posedge tx[1]);
        check(($realtime-peer_start)>104169 && ($realtime-peer_start)<104171,"100MHz default start bit is 104170ns");
        peer_bit_checked=1;
    end
    initial begin #80000000;$fatal(1,"two-board transaction deadline exceeded");end
    initial begin
        for(integer k=0;k<2;k=k+1)begin
            awaddr[k]=0;araddr[k]=0;wdata[k]=0;wstrb[k]=0;
            awvalid[k]=0;wvalid[k]=0;bready[k]=0;arvalid[k]=0;rready[k]=0;
        end
        clocks(0,10);rst_n=1;clocks(0,220000);
        for(integer k=0;k<2;k=k+1)begin wr(k,12'h010,32'h4B49434B);wr(k,12'h00C,1);end
        // A: poll unit 1, four holding registers (CRC 44 09).
        wr(0,12'h200,32'h00000301);wr(0,12'h204,32'h09440400);wr(0,12'h030,8);wr(0,12'h034,1);
        while(!irq[1])clocks(0,1);rd(1,12'h040,value);check(value==8,"B received the complete request");
        rd(1,12'h104,value);check(value==32'h09440400,"B request count and CRC");
        check(!irq[0],"A does not capture its own TX echo");wr(1,12'h038,1);
        // B: [2300, 1000, 1500, 3500], CRC 0D 81.
        wr(1,12'h200,32'h08080301);wr(1,12'h204,32'h05E803FC);
        wr(1,12'h208,32'h0DAC0DDC);wr(1,12'h20C,32'h00000081);
        wr(1,12'h030,13);wr(1,12'h034,1);
        while(!irq[0])clocks(0,1);rd(0,12'h040,value);check(value==13,"A received B's response");
        rd(0,12'h100,value);check(value==32'h08080301,"A response address/function/byte count");
        rd(0,12'h104,value);check(value==32'h05E803FC,"A response register values");
        rd(0,12'h054,value);check(value==0,"A CRC errors zero");rd(1,12'h054,value);check(value==0,"B CRC errors zero");
        check(!irq[1],"B does not capture its own TX echo");
        check(!de[0] && !de[1],"both transceivers return to receive");
        check(main_bit_checked && peer_bit_checked,"both default-baud bit widths observed");
        rd(0,12'h018,value);check(value==50000000,"main reports 50MHz");
        rd(1,12'h018,value);check(value==100000000,"peer reports 100MHz");
        rd(0,12'h020,value);check(value==5208,"main default divisor 5208");
        rd(1,12'h020,value);check(value==10417,"peer default divisor 10417");
        $display("PASS tb_rs485_default_9600: %0d assertions; main 50MHz/5208 peer 100MHz/10417",assertions);$finish;
    end
endmodule
