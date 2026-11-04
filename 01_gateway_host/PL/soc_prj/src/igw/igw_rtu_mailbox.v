// CRC16 RTU receive assembly with a bounded, immutable PS mailbox.
// Timestamp is captured at the first start bit in this frame, in clk cycles.
module igw_rtu_mailbox #(
    parameter integer MAX_BYTES=256
) (
    input wire clk, input wire rst_n, input wire [63:0] ticks,
    input wire [7:0] byte_data, input wire byte_valid, byte_error,
    input wire char_start, uart_busy,
    input wire [31:0] t15_cycles, t35_cycles, char_tail_cycles,
    input wire acknowledge,
    input wire [7:0] read_index,
    output wire [31:0] read_word,
    output reg frame_valid, output reg [8:0] frame_length,
    output reg [31:0] frame_info,
    output reg [63:0] frame_ticks,
    output reg [31:0] good_count, crc_errors, gap_errors, overruns,
    output reg active, output wire quiet_ready
);
    reg [7:0] assembling [0:MAX_BYTES-1];
    reg [7:0] mailbox [0:MAX_BYTES-1];
    reg [8:0] length;
    reg [15:0] crc;
    reg malformed;
    reg [31:0] quiet;
    reg [63:0] first_ticks;
    integer i;
    function automatic [15:0] crc_byte(input [15:0] c, input [7:0] d);
        reg [15:0] v; integer b;
        begin
            v=c ^ {8'b0,d};
            for (b=0;b<8;b=b+1) v=v[0] ? (v>>1)^16'hA001 : v>>1;
            crc_byte=v;
        end
    endfunction
    // Word-aligned read_index is selected by AXI. Four bytes, little endian.
    assign read_word={mailbox[read_index+3],mailbox[read_index+2],mailbox[read_index+1],mailbox[read_index]};
    // UART valid is at the last stop-bit centre; include its remaining half bit.
    assign quiet_ready=!active && !uart_busy && quiet>=t35_cycles+char_tail_cycles;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            frame_valid<=0; frame_length<=0; frame_ticks<=0; frame_info<=0;
            good_count<=0; crc_errors<=0; gap_errors<=0; overruns<=0;
            active<=0; length<=0; crc<=16'hFFFF; malformed<=0; quiet<=0; first_ticks<=0;
        end else begin
            if (acknowledge) frame_valid<=0;
            if (char_start || byte_valid || byte_error) quiet<=0;
            else if (!uart_busy && quiet!=32'hFFFFFFFF) quiet<=quiet+1;
            if (char_start) begin
                if (!active) begin active<=1; first_ticks<=ticks; end
                else if (length!=0 && quiet>t15_cycles+char_tail_cycles) malformed<=1;
            end
            if (byte_error) malformed<=1;
            if (byte_valid) begin
                if (length<MAX_BYTES) begin
                    assembling[length]<=byte_data; length<=length+1;
                    crc<=crc_byte(crc,byte_data);
                end else malformed<=1;
            end
            if (active && !uart_busy && !char_start && !byte_valid && !byte_error && quiet>=t35_cycles+char_tail_cycles) begin
                if (malformed) gap_errors<=gap_errors+1;
                else if (length<4 || crc!=0) crc_errors<=crc_errors+1;
                else begin
                    good_count<=good_count+1;
                    if (frame_valid && !acknowledge) overruns<=overruns+1;
                    else begin
                        for (i=0;i<MAX_BYTES;i=i+1) mailbox[i]<=assembling[i];
                        frame_valid<=1; frame_length<=length; frame_ticks<=first_ticks;
                        frame_info<={8'b0,assembling[2],assembling[1],assembling[0]};
                    end
                end
                active<=0; length<=0; crc<=16'hFFFF; malformed<=0;
            end
        end
    end
endmodule
