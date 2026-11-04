`timescale 1ns/1ps
module tb_rs485_branch;
    reg clk=0; always #10 clk=~clk;
    reg rst=0;
    reg [32:0] awaddr=0,araddr=0;
    reg [2:0] awprot=3,arprot=5;
    reg [31:0] wdata=0;
    reg [3:0] wstrb=15;
    reg awvalid=0,wvalid=0,bready=0,arvalid=0,rready=0;
    wire awready,wready,bvalid,arready,rvalid,tx,de;
    wire [1:0] bresp,rresp;
    wire [31:0] rdata;
    wire [32:0] ma,ra;
    wire [2:0] mp,rp;
    wire [31:0] md;
    wire [3:0] ms;
    wire mav,mwv,mbr,rav,rr;
    reg mbv=0,rv=0,ah=0,wh=0;
    reg [31:0] rd=0,video_reg=0,held_data=0;
    reg [3:0] held_strb=0;
    reg [32:0] last_aw=0,last_ar=0;
    reg [2:0] last_wp=0,last_rp=0;
    reg [1:0] mbresp=0,rrsp=0;
    integer cycles=0,video_reads=0,video_writes=0,checks=0;
    wire mar=!ah&&!mbv&&(cycles%3!=0);
    wire mwr=!wh&&!mbv&&(cycles%4==0);
    wire rar=!rv&&(cycles%3==0);
    igw_rs485_branch dut(
        .aclk(clk),.aresetn(rst),.CAN_RX_OBSERVE(1'b1),.RS485_RX0_3V3(1'b1),.RS485_TX0_3V3(tx),.RS485_DE0_3V3(de),
        .s_axi_awaddr(awaddr),.s_axi_awprot(awprot),.s_axi_awvalid(awvalid),.s_axi_awready(awready),
        .s_axi_wdata(wdata),.s_axi_wstrb(wstrb),.s_axi_wvalid(wvalid),.s_axi_wready(wready),
        .s_axi_bresp(bresp),.s_axi_bvalid(bvalid),.s_axi_bready(bready),
        .s_axi_araddr(araddr),.s_axi_arprot(arprot),.s_axi_arvalid(arvalid),.s_axi_arready(arready),
        .s_axi_rdata(rdata),.s_axi_rresp(rresp),.s_axi_rvalid(rvalid),.s_axi_rready(rready),
        .m_axi_awaddr(ma),.m_axi_awprot(mp),.m_axi_awvalid(mav),.m_axi_awready(mar),
        .m_axi_wdata(md),.m_axi_wstrb(ms),.m_axi_wvalid(mwv),.m_axi_wready(mwr),
        .m_axi_bresp(mbresp),.m_axi_bvalid(mbv),.m_axi_bready(mbr),
        .m_axi_araddr(ra),.m_axi_arprot(rp),.m_axi_arvalid(rav),.m_axi_arready(rar),
        .m_axi_rdata(rd),.m_axi_rresp(rrsp),.m_axi_rvalid(rv),.m_axi_rready(rr)
    );
    // A deliberately backpressured AXI-Lite video target, not a video IP model.
    always @(posedge clk) begin
        if (!rst) begin ah<=0;wh<=0;mbv<=0;rv<=0;cycles<=0;end
        else begin
            cycles<=cycles+1;
            if(mav&&mar) begin ah<=1;last_aw<=ma;last_wp<=mp;end
            if(mwv&&mwr) begin wh<=1;held_data<=md;held_strb<=ms;end
            if(ah&&wh&&!mbv) begin
                mbv<=1;video_writes<=video_writes+1;
                mbresp<=last_aw[32] ? 2'b11 : 2'b00;
                for(integer i=0;i<4;i++) if(held_strb[i])video_reg[8*i+:8]<=held_data[8*i+:8];
            end
            if(mbv&&mbr)begin mbv<=0;ah<=0;wh<=0;end
            if(rav&&rar)begin
                rv<=1;last_ar<=ra;last_rp<=rp;video_reads<=video_reads+1;
                rd<=ra[31:0]^32'h12345678;rrsp<=ra[32]?2'b11:2'b00;
            end
            if(rv&&rr)rv<=0;
        end
    end
    task check(input bit ok,input string message);
        checks++; if(!ok)$fatal(1,"%s",message);
    endtask
    task rd_reg(input [32:0] address,output [31:0] value,output [1:0] resp);
        reg [31:0] saved;
        reg [1:0] saved_resp;
        @(negedge clk);araddr=address;arvalid=1;
        @(posedge clk);while(!arready)@(posedge clk);
        @(negedge clk);arvalid=0;araddr=33'h180400000;
        wait(rvalid);saved=rdata;saved_resp=rresp;
        repeat(5)begin @(negedge clk);check(rvalid&&rdata===saved&&rresp===saved_resp,"read response stable under backpressure");check(!arready,"no second outstanding read");end
        value=saved;resp=saved_resp;rready=1;
        @(negedge clk);rready=0;
    endtask
    task wr_reg(input [32:0] address,input [31:0] value,input [3:0] strobes,input bit data_first,output [1:0] resp);
        reg [1:0] saved;
        if(data_first)begin
            @(negedge clk);wdata=value;wstrb=strobes;wvalid=1;
            @(posedge clk);while(!wready)@(posedge clk);
            @(negedge clk);wvalid=0;wdata=32'hdeadbeef;
            repeat(4)begin @(negedge clk);check(!bvalid,"W alone cannot create B");end
        end
        @(negedge clk);awaddr=address;awvalid=1;
        @(posedge clk);while(!awready)@(posedge clk);
        @(negedge clk);awvalid=0;awaddr=33'h180400000;
        if(!data_first)begin
            repeat(4)begin @(negedge clk);check(!bvalid,"AW alone cannot create B");end
            wdata=value;wstrb=strobes;wvalid=1;
            @(posedge clk);while(!wready)@(posedge clk);
            @(negedge clk);wvalid=0;wdata=32'hdeadbeef;
        end
        wait(bvalid);saved=bresp;
        repeat(5)begin @(negedge clk);check(bvalid&&bresp===saved,"B response stable under backpressure");check(!awready&&!wready,"no second outstanding write");end
        resp=saved;bready=1;
        @(negedge clk);bready=0;
    endtask
    reg [31:0] value;reg [1:0] resp,wr_resp;
    integer saved_reads,saved_writes;
    initial begin
        #1;rst=1;#1;rst=0;repeat(4)@(negedge clk);
        check(!awready&&!wready&&!arready,"no handshake in reset");
        rst=1;repeat(5)@(negedge clk);
        rd_reg(33'h080400000,value,resp);check(resp==0&&value==32'h32574749,"RS485 magic at new base");
        rd_reg(33'h080400004,value,resp);check(resp==0&&value==32'h00020000,"existing ABI version");
        rd_reg(33'h080400018,value,resp);check(resp==0&&value==50000000,"50MHz clock register");
        rd_reg(33'h080400020,value,resp);check(resp==0&&value==5208,"9600 default divisor recomputed");
        check(video_reads==0&&video_writes==0,"RS accesses never reach video");
        wr_reg(33'h080400020,32'd12500,15,1,resp);check(resp==0,"RS W-before-AW");
        rd_reg(33'h080400020,value,resp);check(value==12500,"RS stored value");
        wr_reg(33'h080400020,32'hab,1,0,resp);check(resp==0,"RS byte strobes");
        rd_reg(33'h080400020,value,resp);check(value==32'h30ab,"RS high bytes preserved");
        wr_reg(33'h080400024,0,15,0,resp);check(resp==2,"RS invalid format error retained");
        rd_reg(33'h080400002,value,resp);check(resp==2,"RS unaligned read rejected");
        for(integer i=1;i<=3;i++)begin
            rd_reg(33'h080000000+i*33'h100000,value,resp);
            check(resp==0&&value==((32'h80000000+i*32'h100000)^32'h12345678),"existing video address passed unchanged");
        end
        wr_reg(33'h080100000,32'h12345678,15,0,resp);check(resp==0&&video_reg==32'h12345678,"video AW-before-W");
        wr_reg(33'h080300000,32'habcd0000,12,1,resp);check(resp==0&&video_reg==32'habcd5678,"video W-before-AW with strobes");
        check(last_aw==33'h080300000&&last_wp==3,"full video AW address and protection retained");
        rd_reg(33'h180400000,value,resp);check(resp==3&&last_ar==33'h180400000,"bit32 alias must bypass RS and retain response");
        check(last_rp==5,"read protection retained");
        wr_reg(33'h180400020,32'hbb,15,1,resp);check(resp==3&&last_aw==33'h180400020,"upper address write alias stays video");
        rd_reg(33'h080401000,value,resp);check(last_ar==33'h080401000,"next 4K page not intercepted");
        fork
            wr_reg(33'h080200010,32'h76543210,15,0,wr_resp);
            rd_reg(33'h080400000,value,resp);
        join
        check(wr_resp==0&&resp==0&&value==32'h32574749,"independent concurrent read/write routes");
        // Cancel a W-only pending transaction; no orphan write after reset.
        @(negedge clk);wdata=32'hffffffff;wvalid=1;
        @(posedge clk);while(!wready)@(posedge clk);
        @(negedge clk);wvalid=0;rst=0;repeat(4)@(negedge clk);
        check(!bvalid&&!rvalid&&!mav&&!mwv&&!rav&&!de&&tx,"reset cancels pending transaction and releases transceiver");
        rst=1;repeat(5)@(negedge clk);
        rd_reg(33'h080400020,value,resp);check(resp==0&&value==5208,"peripheral reset restores clock-correct divisor");
        $display("PASS tb_rs485_branch: %0d assertions",checks);$finish;
    end
    initial begin #1000000;$fatal(1,"branch test timeout");end
endmodule
