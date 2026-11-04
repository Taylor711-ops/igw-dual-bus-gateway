`timescale 1ns/1ps
module tb_rs485_axi;
    reg clk=0; always #5 clk=~clk;
    reg rst_n=0,rx=1;
    wire tx,de,irq;
    reg [11:0] awaddr=0,araddr=0;
    reg awvalid=0,wvalid=0,bready=0,arvalid=0,rready=0;
    reg [31:0] wdata=0;
    reg [3:0] wstrb=0;
    wire awready,wready,bvalid,arready,rvalid;
    wire [1:0] bresp,rresp;
    wire [31:0] rdata;
    localparam DIV=16,T15=264,T35=616;
    integer assertions=0;
    reg [31:0] value, saved;
    reg [1:0] response;
    reg [7:0] adu[0:255];
    integer format_mode=1, tx_bytes=0;
    reg [7:0] observed[0:255];
    wire [7:0] observed_byte;wire observed_valid,observed_error,observed_start,observed_busy;
    igw_uart_rx monitor(.clk(clk),.rst_n(rst_n),.rx(tx),.divisor(32'd16),
        .parity_mode(2'd1),.two_stop(1'b0),.data(observed_byte),.valid(observed_valid),
        .error(observed_error),.start_pulse(observed_start),.busy(observed_busy));
    always @(posedge clk) if(observed_valid && de) begin observed[tx_bytes]=observed_byte;tx_bytes=tx_bytes+1; end
    igw_rs485_axi #(.CLOCK_HZ(100000000),.DEFAULT_BAUD(6250000),.WATCHDOG_CYCLES(4000)) dut(
        .aclk(clk),.aresetn(rst_n),.RS485_RX0_3V3(rx),.RS485_TX0_3V3(tx),.RS485_DE0_3V3(de),
        .s_axi_awaddr(awaddr),.s_axi_awprot(3'b0),.s_axi_awvalid(awvalid),.s_axi_awready(awready),
        .s_axi_wdata(wdata),.s_axi_wstrb(wstrb),.s_axi_wvalid(wvalid),.s_axi_wready(wready),
        .s_axi_bresp(bresp),.s_axi_bvalid(bvalid),.s_axi_bready(bready),
        .s_axi_araddr(araddr),.s_axi_arprot(3'b0),.s_axi_arvalid(arvalid),.s_axi_arready(arready),
        .s_axi_rdata(rdata),.s_axi_rresp(rresp),.s_axi_rvalid(rvalid),.s_axi_rready(rready),.irq(irq));
    task check(input bit condition,input string reason);
        begin assertions=assertions+1;if(!condition)$fatal(1,"FAIL: %s",reason);end
    endtask
    task clocks(input integer n);begin repeat(n)@(negedge clk);end endtask
    task reset;
        begin rst_n=0;rx=1;awvalid=0;wvalid=0;arvalid=0;bready=0;rready=0;format_mode=1;tx_bytes=0;clocks(8);rst_n=1;clocks(8);end
    endtask
    // AW and W are deliberately issued independently, with either order.
    task write_reg(input [11:0] address,input [31:0] data,input [3:0] strobes,input integer order,input [1:0] expected);
        reg [1:0] held;integer tries;
        begin
            if(order==0) begin
                awaddr=address;awvalid=1;@(posedge clk);while(!awready)@(posedge clk);@(negedge clk);awvalid=0;
                clocks(3);wdata=data;wstrb=strobes;wvalid=1;@(posedge clk);while(!wready)@(posedge clk);@(negedge clk);wvalid=0;
            end else begin
                wdata=data;wstrb=strobes;wvalid=1;@(posedge clk);while(!wready)@(posedge clk);@(negedge clk);wvalid=0;
                clocks(3);awaddr=address;awvalid=1;@(posedge clk);while(!awready)@(posedge clk);@(negedge clk);awvalid=0;
            end
            tries=0;while(!bvalid && tries<30)begin clocks(1);tries=tries+1;end
            check(bvalid,"write must finish within a bounded number of cycles");held=bresp;
            clocks(3);check(bvalid && bresp==held,"B response stable under backpressure");
            check(bresp==expected,"write response");bready=1;clocks(1);bready=0;clocks(1);
        end
    endtask
    task read_reg(input [11:0] address,output [31:0] data,output [1:0] resp);
        reg [31:0] held;reg [1:0] held_resp;integer tries;
        begin
            araddr=address;arvalid=1;clocks(1);arvalid=0;
            tries=0;while(!rvalid && tries<30)begin clocks(1);tries=tries+1;end
            check(rvalid,"read must finish");held=rdata;held_resp=rresp;
            clocks(3);check(rvalid && rdata===held && rresp==held_resp,"R response stable under backpressure");
            data=rdata;resp=rresp;rready=1;clocks(1);rready=0;clocks(1);
        end
    endtask
    task byte_rx(input [7:0] d,input bit corrupt_parity);
        integer k;reg parity;
        begin
            rx=0;clocks(DIV);
            for(k=0;k<8;k=k+1)begin rx=d[k];clocks(DIV);end
            parity=(^d) ^ (format_mode==2) ^ corrupt_parity;
            if(format_mode!=4)begin rx=parity;clocks(DIV);end
            rx=1;clocks(format_mode==4 ? 2*DIV : DIV);
        end
    endtask
    task golden;
        begin
            // Published Modbus request: unit 1, FC03, start 0, ten registers.
            adu[0]=1;adu[1]=3;adu[2]=0;adu[3]=0;adu[4]=0;adu[5]=10;adu[6]=8'hC5;adu[7]=8'hCD;
        end
    endtask
    task frame_rx(input integer n);
        integer k;begin for(k=0;k<n;k=k+1)byte_rx(adu[k],0);clocks(T35+20);end
    endtask
    initial begin
        // Global timeout also catches an AXI handshake testbench mistake.
        #10000000;$fatal(1,"Simulation deadline exceeded");
    end
    initial begin
        reset();golden();check(!de && tx,"reset defaults to receive with TX idle high");
        read_reg(12'h000,value,response);check(value==32'h32574749 && response==0,"PL identity");
        write_reg(12'h024,1,4'hF,0,0);write_reg(12'h020,0,0,1,0);
        read_reg(12'h020,value,response);check(value==DIV,"WSTRB zero preserves configuration");
        write_reg(12'h050,7,4'hF,0,2);read_reg(12'h001,value,response);check(response==2,"unaligned read rejected");
        frame_rx(8);read_reg(12'h040,value,response);check(irq && value==8,"valid RTU reaches mailbox");
        read_reg(12'h100,value,response);check(value==32'h00000301,"little-endian payload window");
        read_reg(12'h04C,value,response);check(value==32'h00000301,"PL extracts unit and function fields");
        read_reg(12'h044,saved,response);frame_rx(8);
        read_reg(12'h05C,value,response);check(value==1,"occupied mailbox drops visibly");
        read_reg(12'h044,value,response);check(value==saved,"mailbox timestamp remains immutable");
        write_reg(12'h038,1,4'hF,1,0);clocks(3);check(!irq,"mailbox release clears IRQ");
        adu[7]=8'hCC;frame_rx(8);read_reg(12'h054,value,response);check(value==1 && !irq,"bad CRC is discarded");golden();
        frame_rx(3);read_reg(12'h054,value,response);check(value==2,"partial frame terminates and reports error");
        byte_rx(1,1);clocks(T35+20);read_reg(12'h058,value,response);check(value==1,"bad parity counted");
        reset();golden();byte_rx(adu[0],0);clocks(T15+40);
        for(integer q=1;q<8;q=q+1)byte_rx(adu[q],0);
        clocks(T35+20);read_reg(12'h068,value,response);check(value==1 && !irq,"t1.5 gap invalidates frame");
        reset();write_reg(12'h024,4,4'hF,0,0);format_mode=4;frame_rx(8);
        read_reg(12'h040,value,response);check(value==8 && irq,"8N2 decode");
        reset();write_reg(12'h024,2,4'hF,1,0);format_mode=2;frame_rx(8);
        read_reg(12'h040,value,response);check(value==8 && irq,"8O1 decode");
        reset();rx=0;clocks(1000);read_reg(12'h058,value,response);check(value==1,"continuous break produces one error");rx=1;clocks(T35+50);
        reset();golden();clocks(T35+20);
        read_reg(12'h01C,value,response);check(value[6] && !value[5] && !value[2],"status quiet bit matches Linux driver mask");
        write_reg(12'h200,32'h00000301,4'hF,0,0);
        write_reg(12'h204,32'hCDC50A00,4'hF,1,0);
        write_reg(12'h030,8,4'hF,0,0);
        write_reg(12'h034,1,4'hF,0,2);check(!de,"TX denied without a lease");
        write_reg(12'h010,32'h4B49434B,4'hF,1,0);write_reg(12'h00C,1,4'hF,0,0);
        read_reg(12'h01C,value,response);check(value[2],"status permit bit matches Linux driver mask");
        write_reg(12'h034,1,4'hF,1,0);clocks(2000);
        read_reg(12'h060,value,response);check(value==1 && !de && tx_bytes==8,"whole ADU and DE hold complete");
        for(integer q=0;q<8;q=q+1)check(observed[q]==adu[q],"TX byte content and parity");
        clocks(2500);read_reg(12'h064,value,response);check(value==1 && !de,"watchdog expiration revokes send permission");
        write_reg(12'h010,32'h4B49434B,4'hF,0,0);write_reg(12'h034,1,4'hF,1,0);
        clocks(100);check(de,"active transmission has DE asserted");write_reg(12'h00C,0,4'hF,0,0);
        clocks(3);check(!de && tx,"explicit revoke aborts transmission");
        // Expire an active transfer, not just an idle permission.
        reset();golden();clocks(T35+20);
        write_reg(12'h200,32'h00000301,4'hF,0,0);write_reg(12'h204,32'hCDC50A00,4'hF,1,0);
        write_reg(12'h030,8,4'hF,0,0);write_reg(12'h010,32'h4B49434B,4'hF,1,0);
        write_reg(12'h00C,1,4'hF,0,0);clocks(3300);write_reg(12'h034,1,4'hF,1,0);
        clocks(200);check(de,"transfer active before expiry");clocks(700);
        check(!de && tx,"watchdog aborts an active transfer");read_reg(12'h060,value,response);check(value==0,"aborted ADU is not counted complete");
        // Maximum ADU length, then one-byte overrun. Generate a valid CRC body.
        reset();
        begin : max_frame
            reg [15:0] c;reg [7:0] d;
            c=16'hFFFF;
            for(integer q=0;q<254;q=q+1)begin
                d=q==0 ? 1 : q==1 ? 3 : q;
                adu[q]=d;c=c ^ {8'b0,d};
                for(integer b=0;b<8;b=b+1)c=c[0] ? (c>>1)^16'hA001 : c>>1;
            end
            adu[254]=c[7:0];adu[255]=c[15:8];frame_rx(256);
            read_reg(12'h040,value,response);check(value==256 && irq,"maximum 256-byte ADU");
            write_reg(12'h038,1,4'hF,0,0);
            for(integer q=0;q<256;q=q+1)byte_rx(adu[q],0);byte_rx(0,0);clocks(T35+20);
            read_reg(12'h068,value,response);check(value==1 && !irq,"oversized ADU discarded");
        end
        $display("PASS tb_rs485_axi: %0d assertions",assertions);$finish;
    end
endmodule
