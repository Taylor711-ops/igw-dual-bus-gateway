// Single-clock AXI4-Lite peripheral. Connect through the vendor AXI protocol
// converter when the PS master is full AXI4. No clock-domain crossings here.
module igw_rs485_axi #(
    parameter integer CLOCK_HZ=100000000,
    parameter integer DEFAULT_BAUD=9600,
    parameter integer WATCHDOG_CYCLES=CLOCK_HZ/2
) (
    input wire aclk, aresetn,
    input wire RS485_RX0_3V3,
    output wire RS485_TX0_3V3, RS485_DE0_3V3,
    input wire [11:0] s_axi_awaddr, input wire [2:0] s_axi_awprot,
    input wire s_axi_awvalid, output wire s_axi_awready,
    input wire [31:0] s_axi_wdata, input wire [3:0] s_axi_wstrb,
    input wire s_axi_wvalid, output wire s_axi_wready,
    output reg [1:0] s_axi_bresp, output reg s_axi_bvalid,
    input wire s_axi_bready,
    input wire [11:0] s_axi_araddr, input wire [2:0] s_axi_arprot,
    input wire s_axi_arvalid, output wire s_axi_arready,
    output reg [31:0] s_axi_rdata, output reg [1:0] s_axi_rresp,
    output reg s_axi_rvalid, input wire s_axi_rready,
    output wire irq
);
    localparam [31:0] MAGIC=32'h32574749, VERSION=32'h00020000;
    localparam integer DEFAULT_DIV=(CLOCK_HZ+DEFAULT_BAUD/2)/DEFAULT_BAUD;
    reg aw_pending, w_pending;
    reg [11:0] awaddr;
    reg [31:0] wdata;
    reg [3:0] wstrb;
    reg tx_authorized;
    reg [31:0] watchdog, divisor, format, t15, t35;
    reg [8:0] tx_length;
    reg [7:0] tx_buffer[0:255];
    reg tx_kick, rx_ack;
    reg [63:0] ticks;
    reg [31:0] uart_errors, tx_count, watchdog_expiries;
    wire [7:0] rx_byte, tx_index;
    wire rx_byte_valid, rx_byte_error, rx_start, rx_busy, rx_active;
    wire frame_valid, quiet_ready, tx_busy, tx_completed;
    wire [8:0] frame_length;
    wire [63:0] frame_ticks;
    wire [31:0] frame_word, frame_info, good_count, crc_errors, gap_errors, overruns;
    wire permit=tx_authorized && watchdog!=0;
    wire config_idle=!tx_busy && !rx_active && !rx_busy;
    wire [31:0] status={25'b0,quiet_ready,1'b0,rx_active,rx_busy,permit,tx_busy,frame_valid};
    integer lane;
    assign s_axi_awready=!aw_pending && !s_axi_bvalid;
    assign s_axi_wready=!w_pending && !s_axi_bvalid;
    assign s_axi_arready=!s_axi_rvalid;
    assign irq=frame_valid;
    igw_uart_rx rx_uart(.clk(aclk),.rst_n(aresetn),
        .rx(RS485_DE0_3V3 ? 1'b1 : RS485_RX0_3V3),.divisor(divisor),
        .parity_mode(format[1:0]),.two_stop(format[2]),
        .data(rx_byte),.valid(rx_byte_valid),.error(rx_byte_error),
        .start_pulse(rx_start),.busy(rx_busy));
    igw_rtu_mailbox mailbox(.clk(aclk),.rst_n(aresetn),.ticks(ticks),
        .byte_data(rx_byte),.byte_valid(rx_byte_valid),.byte_error(rx_byte_error),
        .char_start(rx_start),.uart_busy(rx_busy),.t15_cycles(t15),.t35_cycles(t35),
        .char_tail_cycles(divisor>>1),
        .acknowledge(rx_ack),.read_index({s_axi_araddr[7:2],2'b00}),
        .read_word(frame_word),.frame_valid(frame_valid),.frame_length(frame_length),
        .frame_info(frame_info),
        .frame_ticks(frame_ticks),.good_count(good_count),.crc_errors(crc_errors),
        .gap_errors(gap_errors),.overruns(overruns),.active(rx_active),.quiet_ready(quiet_ready));
    igw_rs485_tx tx_engine(.clk(aclk),.rst_n(aresetn),.permit(permit),.kick(tx_kick),
        .length(tx_length),.buffer_byte(tx_buffer[tx_index]),.divisor(divisor),
        .parity_mode(format[1:0]),.two_stop(format[2]),.buffer_index(tx_index),
        .tx(RS485_TX0_3V3),.de(RS485_DE0_3V3),.busy(tx_busy),.completed(tx_completed));

    function automatic [31:0] merge_bytes(input [31:0] old_value, new_value, input [3:0] strobes);
        integer j;
        begin
            merge_bytes=old_value;
            for(j=0;j<4;j=j+1) if(strobes[j]) merge_bytes[j*8+:8]=new_value[j*8+:8];
        end
    endfunction
    wire [31:0] merged_div=merge_bytes(divisor,wdata,wstrb);
    wire [31:0] merged_format=merge_bytes(format,wdata,wstrb);
    wire [31:0] merged_t15=merge_bytes(t15,wdata,wstrb);
    wire [31:0] merged_t35=merge_bytes(t35,wdata,wstrb);
    wire [31:0] merged_length=merge_bytes({23'b0,tx_length},wdata,wstrb);

    always @(posedge aclk or negedge aresetn) begin
        if (!aresetn) begin
            aw_pending<=0; w_pending<=0; awaddr<=0; wdata<=0; wstrb<=0;
            s_axi_bresp<=0; s_axi_bvalid<=0; s_axi_rdata<=0; s_axi_rresp<=0; s_axi_rvalid<=0;
            tx_authorized<=0; watchdog<=0; divisor<=DEFAULT_DIV; format<=1; // 8E1
            t15<=(DEFAULT_DIV*33+1)/2; t35<=(DEFAULT_DIV*77+1)/2;
            tx_length<=0; tx_kick<=0; rx_ack<=0; ticks<=0;
            uart_errors<=0; tx_count<=0; watchdog_expiries<=0;
        end else begin
            ticks<=ticks+1; tx_kick<=0; rx_ack<=0;
            if (watchdog!=0) watchdog<=watchdog-1;
            if (watchdog==1 && tx_authorized) watchdog_expiries<=watchdog_expiries+1;
            if (rx_byte_error) uart_errors<=uart_errors+1;
            if (tx_completed) tx_count<=tx_count+1;
            if (s_axi_bvalid && s_axi_bready) s_axi_bvalid<=0;
            if (s_axi_rvalid && s_axi_rready) s_axi_rvalid<=0;
            if (s_axi_awvalid && s_axi_awready) begin aw_pending<=1; awaddr<=s_axi_awaddr; end
            if (s_axi_wvalid && s_axi_wready) begin w_pending<=1; wdata<=s_axi_wdata; wstrb<=s_axi_wstrb; end
            if (aw_pending && w_pending && !s_axi_bvalid) begin
                aw_pending<=0; w_pending<=0; s_axi_bvalid<=1; s_axi_bresp<=0;
                if (awaddr[1:0]!=0) s_axi_bresp<=2;
                else if (awaddr>=12'h200 && awaddr<=12'h2FC) begin
                    if (tx_busy) s_axi_bresp<=2;
                    else for(lane=0;lane<4;lane=lane+1)
                        if(wstrb[lane]) tx_buffer[{awaddr[7:2],2'b00}+lane]<=wdata[lane*8+:8];
                end else case(awaddr)
                    12'h00C: if (wstrb[0]) begin tx_authorized<=wdata[0]; if(!wdata[0]) watchdog<=0; end
                    12'h010: if (wstrb==4'hF && wdata==32'h4B49434B) watchdog<=WATCHDOG_CYCLES;
                        else s_axi_bresp<=2;
                    12'h020: if (config_idle && merged_div>=16 && merged_div<=CLOCK_HZ) divisor<=merged_div;
                        else s_axi_bresp<=2;
                    12'h024: if (config_idle && (merged_format==1 || merged_format==2 || merged_format==4)) format<=merged_format;
                        else s_axi_bresp<=2; // Standard 8E1, 8O1 or 8N2 RTU modes only.
                    12'h028: if (config_idle && merged_t15>=16 && merged_t15<=CLOCK_HZ) t15<=merged_t15; else s_axi_bresp<=2;
                    12'h02C: if (config_idle && merged_t35>=16 && merged_t35<=CLOCK_HZ) t35<=merged_t35; else s_axi_bresp<=2;
                    12'h030: if (!tx_busy && merged_length>=4 && merged_length<=256) tx_length<=merged_length;
                        else s_axi_bresp<=2;
                    12'h034: if (wstrb[0] && wdata==1 && permit && !tx_busy && quiet_ready && tx_length>=4) tx_kick<=1;
                        else s_axi_bresp<=2;
                    12'h038: if (wstrb[0] && wdata==1) rx_ack<=1; else s_axi_bresp<=2;
                    default: s_axi_bresp<=2;
                endcase
            end
            if(s_axi_arvalid && s_axi_arready) begin
                s_axi_rvalid<=1; s_axi_rresp<=0; s_axi_rdata<=0;
                if(s_axi_araddr[1:0]!=0) s_axi_rresp<=2;
                else if(s_axi_araddr>=12'h100 && s_axi_araddr<=12'h1FC) s_axi_rdata<=frame_word;
                else case(s_axi_araddr)
                    12'h000: s_axi_rdata<=MAGIC;
                    12'h004: s_axi_rdata<=VERSION;
                    12'h008: s_axi_rdata<=32'h1F;
                    12'h00C: s_axi_rdata<={31'b0,tx_authorized};
                    12'h014: s_axi_rdata<=WATCHDOG_CYCLES;
                    12'h018: s_axi_rdata<=CLOCK_HZ;
                    12'h01C: s_axi_rdata<=status;
                    12'h020: s_axi_rdata<=divisor;
                    12'h024: s_axi_rdata<=format;
                    12'h028: s_axi_rdata<=t15;
                    12'h02C: s_axi_rdata<=t35;
                    12'h030: s_axi_rdata<={23'b0,tx_length};
                    12'h040: s_axi_rdata<={23'b0,frame_length};
                    12'h044: s_axi_rdata<=frame_ticks[31:0];
                    12'h048: s_axi_rdata<=frame_ticks[63:32];
                    12'h04C: s_axi_rdata<=frame_info;
                    12'h050: s_axi_rdata<=good_count;
                    12'h054: s_axi_rdata<=crc_errors;
                    12'h058: s_axi_rdata<=uart_errors;
                    12'h05C: s_axi_rdata<=overruns;
                    12'h060: s_axi_rdata<=tx_count;
                    12'h064: s_axi_rdata<=watchdog_expiries;
                    12'h068: s_axi_rdata<=gap_errors;
                    default: s_axi_rresp<=2;
                endcase
            end
        end
    end
endmodule
