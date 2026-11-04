// Register sub-window 0x80400800..0x804008ff, 50 MHz GP0 domain.
// No transmit signals. The only accepted write is POP=1 at offset 0x0c.
// Head record is immutable until POP; full FIFO drops newest, never unread head.
module igw_can_observer_axi #(
    parameter integer CLOCK_HZ=50000000,
    parameter integer BITRATE=500000,
    parameter integer FIFO_DEPTH=16
) (
    input wire aclk, aresetn, can_rx,
    input wire [7:0] s_axi_awaddr,
    input wire s_axi_awvalid, output wire s_axi_awready,
    input wire [31:0] s_axi_wdata, input wire [3:0] s_axi_wstrb,
    input wire s_axi_wvalid, output wire s_axi_wready,
    output reg [1:0] s_axi_bresp, output reg s_axi_bvalid,
    input wire s_axi_bready,
    input wire [7:0] s_axi_araddr,
    input wire s_axi_arvalid, output wire s_axi_arready,
    output reg [31:0] s_axi_rdata, output reg [1:0] s_axi_rresp,
    output reg s_axi_rvalid, input wire s_axi_rready
);
    function integer clog2;
        input integer value;integer v;
        begin v=value-1;clog2=0;while(v>0)begin clog2=clog2+1;v=v>>1;end end
    endfunction
    localparam integer PW=clog2(FIFO_DEPTH);
    reg [63:0] ticks;
    wire valid, active, idle_ready, rtr, ack;
    wire [10:0] id;
    wire [3:0] dlc;
    wire [14:0] crc;
    wire [63:0] payload, sof_ticks;
    wire [31:0] frame_cycles;
    wire [2:0] error;
    reg [255:0] fifo[0:FIFO_DEPTH-1];
    reg [PW-1:0] head,tail;
    reg [PW:0] count;
    reg [31:0] sequence_number, good, dropped, popped;
    reg [31:0] stuff_errors, crc_errors, form_errors, unsupported, ack_errors, timeouts;
    reg aw_pending,w_pending;
    reg [7:0] awaddr;
    reg [31:0] wdata;
    reg [3:0] wstrb;
    wire write_fire=aw_pending && w_pending && !s_axi_bvalid;
    wire pop=write_fire && awaddr==8'h0c && wstrb==4'hf && wdata==1 && count!=0;
    wire push=valid && (count<FIFO_DEPTH || pop);
    wire [31:0] flags={1'b0,crc,8'b0,1'b1,ack,1'b0,rtr,dlc};
    wire [255:0] record={frame_cycles,payload[63:32],payload[31:0],sof_ticks[63:32],sof_ticks[31:0],flags,{21'b0,id},sequence_number};
    wire [255:0] first=fifo[head];
    assign s_axi_awready=!aw_pending && !s_axi_bvalid;
    assign s_axi_wready=!w_pending && !s_axi_bvalid;
    assign s_axi_arready=!s_axi_rvalid;
    igw_can_rx_observer #(.CLOCK_HZ(CLOCK_HZ),.BITRATE(BITRATE)) receiver(
        .clk(aclk),.resetn(aresetn),.can_rx(can_rx),.ticks(ticks),
        .frame_valid(valid),.frame_id(id),.frame_dlc(dlc),.frame_rtr(rtr),.frame_ack(ack),
        .frame_crc(crc),.frame_data(payload),.frame_ticks(sof_ticks),.frame_cycles(frame_cycles),
        .error_code(error),.active(active),.idle_ready(idle_ready));
    always @(posedge aclk or negedge aresetn) begin
        if(!aresetn) begin
            ticks<=0;head<=0;tail<=0;count<=0;sequence_number<=0;good<=0;dropped<=0;popped<=0;
            stuff_errors<=0;crc_errors<=0;form_errors<=0;unsupported<=0;ack_errors<=0;timeouts<=0;
            aw_pending<=0;w_pending<=0;awaddr<=0;wdata<=0;wstrb<=0;
            s_axi_bresp<=0;s_axi_bvalid<=0;s_axi_rdata<=0;s_axi_rresp<=0;s_axi_rvalid<=0;
        end else begin
            ticks<=ticks+1'b1;
            if(valid)begin
                good<=good+1'b1;sequence_number<=sequence_number+1'b1;
                if(!push)dropped<=dropped+1'b1;
            end
            if(push)begin fifo[tail]<=record;tail<=tail+1'b1;end
            if(pop)begin head<=head+1'b1;popped<=popped+1'b1;end
            case({push,pop})
                2'b10:count<=count+1'b1;
                2'b01:count<=count-1'b1;
                default:count<=count;
            endcase
            case(error)
                1:stuff_errors<=stuff_errors+1'b1;
                2:crc_errors<=crc_errors+1'b1;
                3:form_errors<=form_errors+1'b1;
                4:unsupported<=unsupported+1'b1;
                5:ack_errors<=ack_errors+1'b1;
                6:timeouts<=timeouts+1'b1;
                default:begin end
            endcase
            if(s_axi_bvalid && s_axi_bready)s_axi_bvalid<=0;
            if(s_axi_rvalid && s_axi_rready)s_axi_rvalid<=0;
            if(s_axi_awvalid && s_axi_awready)begin aw_pending<=1;awaddr<=s_axi_awaddr;end
            if(s_axi_wvalid && s_axi_wready)begin w_pending<=1;wdata<=s_axi_wdata;wstrb<=s_axi_wstrb;end
            if(write_fire)begin
                aw_pending<=0;w_pending<=0;s_axi_bvalid<=1;
                s_axi_bresp<=pop?2'b00:2'b10;
            end
            if(s_axi_arvalid && s_axi_arready)begin
                s_axi_rvalid<=1;s_axi_rresp<=0;s_axi_rdata<=0;
                if(s_axi_araddr[1:0]!=0)s_axi_rresp<=2;
                else if(s_axi_araddr>=8'h80 && s_axi_araddr<=8'h9c)begin
                    if(count==0)s_axi_rresp<=2;
                    else s_axi_rdata<=first[s_axi_araddr[4:2]*32+:32];
                end else case(s_axi_araddr)
                    8'h00:s_axi_rdata<=32'h4e414349; // ICAN
                    8'h04:s_axi_rdata<=32'h00010000;
                    8'h08:s_axi_rdata<=32'h0000001f; // passive, std, RTR, CRC15, FIFO
                    8'h10:s_axi_rdata<={27'b0,idle_ready,active,(count==FIFO_DEPTH),(count!=0),1'b0};
                    8'h14:s_axi_rdata<=count;
                    8'h18:s_axi_rdata<=CLOCK_HZ;
                    8'h1c:s_axi_rdata<=BITRATE;
                    8'h20:s_axi_rdata<=good;
                    8'h24:s_axi_rdata<=dropped;
                    8'h28:s_axi_rdata<=stuff_errors;
                    8'h2c:s_axi_rdata<=crc_errors;
                    8'h30:s_axi_rdata<=form_errors;
                    8'h34:s_axi_rdata<=unsupported;
                    8'h38:s_axi_rdata<=ack_errors;
                    8'h3c:s_axi_rdata<=timeouts;
                    8'h40:s_axi_rdata<=popped;
                    8'h44:s_axi_rdata<=FIFO_DEPTH;
                    default:s_axi_rresp<=2;
                endcase
            end
        end
    end
endmodule
