// Receive-only Classical CAN base-frame observer. No TX/ACK/error output.
// Fixed 500 kbit/s at 50 MHz by default; 80% sampling, bounded falling-edge
// resynchronization, dynamic destuffing through the final CRC sequence bit.
// Supported: standard 11-bit data/remote frames, DLC 0..8, dominant ACK.
// Extended/FD/DLC>8 are counted as unsupported, never published as good frames.
module igw_can_rx_observer #(
    parameter integer CLOCK_HZ=50000000,
    parameter integer BITRATE=500000,
    parameter integer SJW_CYCLES=20
) (
    input wire clk, resetn, can_rx,
    input wire [63:0] ticks,
    output reg frame_valid,
    output reg [10:0] frame_id,
    output reg [3:0] frame_dlc,
    output reg frame_rtr, frame_ack,
    output reg [14:0] frame_crc,
    output reg [63:0] frame_data, frame_ticks,
    output reg [31:0] frame_cycles,
    output reg [2:0] error_code,
    output reg active,
    output wire idle_ready
);
    localparam integer BIT_CYCLES=CLOCK_HZ/BITRATE;
    localparam integer SAMPLE=BIT_CYCLES*4/5-1;
    localparam integer IDLE_CYCLES=11*BIT_CYCLES;
    localparam [2:0] BITS=0, LAST_STUFF=1, CRC_DELIM=2,
        ACK_SLOT=3, ACK_DELIM=4, EOF_BITS=5, GAP_BITS=6;
    // Two-flop synchronizer; the third flop supplies edge history.
    (* ASYNC_REG="TRUE" *) reg rx_meta, rx_sync;
    reg rx_previous, last_sample, resync_used, idle_armed;
    wire falling=rx_previous && !rx_sync;
    reg [15:0] phase, idle_cycles;
    reg [2:0] state, run_length, eof_count;
    reg run_last, need_stuff;
    reg [7:0] plain_index, crc_start;
    reg [5:0] data_index;
    reg [7:0] byte_shift;
    reg [14:0] crc_calc;
    wire [3:0] next_dlc={frame_dlc[2:0],rx_sync};
    wire ends_run=(run_last==rx_sync && run_length==4);
    assign idle_ready=!active && (idle_armed || idle_cycles>=IDLE_CYCLES-1);
    function [14:0] crc_step;
        input [14:0] old_crc;
        input bit_value;
        begin
            crc_step={old_crc[13:0],1'b0};
            if(old_crc[14]^bit_value)crc_step=crc_step^15'h4599;
        end
    endfunction
    task reject_frame;
        input [2:0] code;
        begin active<=0;error_code<=code;need_stuff<=0;idle_armed<=0;end
    endtask
    always @(posedge clk or negedge resetn) begin
        if(!resetn) begin rx_meta<=1;rx_sync<=1;rx_previous<=1;end
        else begin rx_meta<=can_rx;rx_sync<=rx_meta;rx_previous<=rx_sync;end
    end
    always @(posedge clk or negedge resetn) begin
        if(!resetn) begin
            frame_valid<=0;frame_id<=0;frame_dlc<=0;frame_rtr<=0;frame_ack<=0;
            frame_crc<=0;frame_data<=0;frame_ticks<=0;frame_cycles<=0;
            error_code<=0;active<=0;phase<=0;idle_cycles<=0;idle_armed<=0;
            state<=BITS;run_length<=0;run_last<=0;need_stuff<=0;eof_count<=0;
            plain_index<=0;crc_start<=19;data_index<=0;byte_shift<=0;
            crc_calc<=0;last_sample<=1;resync_used<=0;
        end else begin
            frame_valid<=0;error_code<=0;
            if(!rx_sync)idle_cycles<=0;
            else if(idle_cycles<IDLE_CYCLES)idle_cycles<=idle_cycles+1'b1;
            if(!active && idle_cycles>=IDLE_CYCLES-1)idle_armed<=1;
            if(!active) begin
                if(idle_ready && falling) begin
                    active<=1;idle_armed<=0;phase<=0;resync_used<=1;last_sample<=1;
                    state<=BITS;plain_index<=0;crc_start<=19;data_index<=0;
                    run_length<=0;run_last<=0;need_stuff<=0;crc_calc<=0;
                    frame_id<=0;frame_dlc<=0;frame_rtr<=0;frame_ack<=0;
                    frame_crc<=0;frame_data<=0;byte_shift<=0;eof_count<=0;
                    frame_ticks<=ticks;
                end
            end else if(ticks-frame_ticks>=200*BIT_CYCLES)begin
                reject_frame(6); // bounded incomplete-frame timeout
            end else begin
                // At most one correction between consecutive sample points.
                if(falling && last_sample && !resync_used) begin
                    resync_used<=1;
                    if(phase<=SAMPLE) begin
                        if(phase>SJW_CYCLES)phase<=phase-SJW_CYCLES;
                        else phase<=0;
                    end else if(BIT_CYCLES-phase<=SJW_CYCLES)phase<=0;
                    else phase<=phase+SJW_CYCLES;
                end else begin
                    if(phase==BIT_CYCLES-1)phase<=0;
                    else phase<=phase+1'b1;
                    if(phase==SAMPLE) begin
                        resync_used<=0;last_sample<=rx_sync;
                        if((state==BITS || state==LAST_STUFF) && need_stuff) begin
                            if(rx_sync==run_last)reject_frame(1);
                            else begin
                                run_last<=rx_sync;run_length<=1;need_stuff<=0;
                                if(state==LAST_STUFF)state<=CRC_DELIM;
                            end
                        end else case(state)
                            BITS:begin
                                if(run_length==0 || rx_sync!=run_last)run_length<=1;
                                else run_length<=run_length+1'b1;
                                run_last<=rx_sync;if(ends_run)need_stuff<=1;
                                plain_index<=plain_index+1'b1;
                                if(plain_index<crc_start)crc_calc<=crc_step(crc_calc,rx_sync);
                                if(plain_index==0)begin
                                    if(rx_sync)reject_frame(3);
                                end else if(plain_index<=11)frame_id<={frame_id[9:0],rx_sync};
                                else if(plain_index==12)frame_rtr<=rx_sync;
                                else if(plain_index==13 || plain_index==14)begin
                                    if(rx_sync)reject_frame(4); // extended or FD/reserved
                                end else if(plain_index<=18)begin
                                    frame_dlc<=next_dlc;
                                    if(plain_index==18)begin
                                        if(next_dlc>8)reject_frame(4);
                                        else crc_start<=19+(frame_rtr?0:{next_dlc,3'b000});
                                    end
                                end else if(plain_index<crc_start)begin
                                    byte_shift<={byte_shift[6:0],rx_sync};
                                    data_index<=data_index+1'b1;
                                    if(data_index[2:0]==7)
                                        frame_data[data_index[5:3]*8+:8]<={byte_shift[6:0],rx_sync};
                                end else begin
                                    frame_crc<={frame_crc[13:0],rx_sync};
                                    if(plain_index==crc_start+14)begin
                                        if({frame_crc[13:0],rx_sync}!=crc_calc)reject_frame(2);
                                        else if(ends_run)state<=LAST_STUFF;
                                        else state<=CRC_DELIM;
                                    end
                                end
                            end
                            CRC_DELIM:if(!rx_sync)reject_frame(3);else state<=ACK_SLOT;
                            ACK_SLOT:begin
                                frame_ack<=!rx_sync;
                                if(rx_sync)reject_frame(5);else state<=ACK_DELIM;
                            end
                            ACK_DELIM:if(!rx_sync)reject_frame(3);else begin state<=EOF_BITS;eof_count<=0;end
                            EOF_BITS:begin
                                if(!rx_sync)reject_frame(3);
                                else if(eof_count==6)begin
                                    frame_valid<=1;frame_cycles<=ticks-frame_ticks;
                                    state<=GAP_BITS;eof_count<=0;
                                end else eof_count<=eof_count+1'b1;
                            end
                            GAP_BITS:begin
                                if(!rx_sync)reject_frame(3);
                                else if(eof_count==2)begin active<=0;idle_armed<=1;end
                                else eof_count<=eof_count+1'b1;
                            end
                            default:reject_frame(3);
                        endcase
                    end
                end
            end
        end
    end
endmodule
