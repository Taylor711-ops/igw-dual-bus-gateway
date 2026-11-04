// Zynq-7020 PL-only RS485 slave, node 3. 9600 baud 8E1, Modbus RTU FC03/FC06.
// Pin names and 50 MHz clock match the user's 20_rs485_key_led example.
// This design never transmits autonomously. No CAN, file service or nonvolatile storage.
module rs485_key_led #(
    parameter CLK_FREQ = 50000000,
    parameter UART_BPS = 9600
)(
    input wire sys_clk, input wire sys_rst_n,
    input wire [1:0] key, output wire [1:0] led,
    input wire rs485_uart_rxd, output wire rs485_uart_txd
);
    localparam integer BIT_CLKS = (CLK_FREQ + UART_BPS/2) / UART_BPS;
    // > 3.5 characters of 8E1: 39 bit-times after last received stop sample.
    localparam integer FRAME_GAP = BIT_CLKS * 39;
    localparam integer INTER_GAP = BIT_CLKS * 22;
    localparam [2:0] RX_FRAME=0, PREP_CRC=1, BUILD_CRC=2, SEND_FRAME=3;
    reg [2:0] state;
    reg [7:0] req[0:7], resp[0:20];
    reg [3:0] req_count;
    reg req_bad;
    reg [31:0] idle_count;
    reg [4:0] payload_length, response_length, send_index;
    reg tx_start;
    reg [7:0] tx_byte;
    wire tx_busy, rx_valid, rx_error, rx_start, rx_busy;
    wire [7:0] rx_byte;
    reg [15:0] words[16:31];
    reg [15:0] request_count, seconds, write_count, last_address, last_value;
    reg [31:0] second_divider;
    reg [15:0] req_crc, response_crc, req_addr, req_arg, value_temp;
    reg [4:0] crc_index;
    integer i;

    // LEDs show current register 16 low bits; keys intentionally have no effect.
    assign led = ~words[16][1:0];
    wire unused_key = &key;
    igw_uart_rx_8e1 #(.BIT_CLKS(BIT_CLKS)) u_rx(
        .clk(sys_clk), .rst_n(sys_rst_n), .enable(state == RX_FRAME),
        .rxd(rs485_uart_rxd), .data(rx_byte), .valid(rx_valid),
        .error(rx_error), .started(rx_start), .busy(rx_busy));
    igw_uart_tx_8e1 #(.BIT_CLKS(BIT_CLKS)) u_tx(
        .clk(sys_clk), .rst_n(sys_rst_n), .start(tx_start),
        .data(tx_byte), .txd(rs485_uart_txd), .busy(tx_busy));

    function [15:0] crc_byte;
        input [15:0] c; input [7:0] d;
        reg [15:0] a; integer bit_index;
        begin
            a = c ^ {8'b0,d};
            for (bit_index=0; bit_index<8; bit_index=bit_index+1)
                a = a[0] ? ((a >> 1) ^ 16'ha001) : (a >> 1);
            crc_byte = a;
        end
    endfunction
    function [15:0] register_value;
        input [15:0] address;
        begin
            case (address)
                0: register_value=16'h1234;
                1: register_value=16'd3;
                2: register_value=request_count;
                3: register_value=seconds;
                4: register_value=16'h0801;
                5: register_value=write_count;
                6: register_value=last_address;
                7: register_value=last_value;
                8: register_value=16'h0301; // RS485-only node3 protocol identity
                9: register_value=16'h0d01;
                10: register_value=16'd1; // one physical, one logical device
                default: if (address>=16 && address<=31) register_value=words[address];
                         else register_value=16'd0;
            endcase
        end
    endfunction

    always @(posedge sys_clk or negedge sys_rst_n) begin
        if (!sys_rst_n) begin
            state<=RX_FRAME; req_count<=0; req_bad<=0; idle_count<=0;
            payload_length<=0; response_length<=0; send_index<=0;
            req_crc<=16'hffff; response_crc<=16'hffff; crc_index<=0;
            tx_start<=0; tx_byte<=0;
            request_count<=0; seconds<=0; write_count<=0;
            last_address<=16'hffff; last_value<=0; second_divider<=0;
            for (i=16;i<32;i=i+1) words[i]<=0;
        end else begin
            tx_start<=0;
            if (second_divider==CLK_FREQ-1) begin second_divider<=0; seconds<=seconds+1'b1; end
            else second_divider<=second_divider+1'b1;
            case (state)
              RX_FRAME: begin
                // Only complete parity/stop-checked bytes enter the frame buffer.
                if (rx_busy || rx_start || rx_valid || rx_error) idle_count<=0;
                else if (idle_count<FRAME_GAP) idle_count<=idle_count+1'b1;
                if (rx_start && req_count!=0 && idle_count>INTER_GAP) req_bad<=1;
                if (rx_error) req_bad<=1;
                if (rx_valid) begin
                    req_crc<=crc_byte(req_crc,rx_byte);
                    if (req_count<8) begin req[req_count]<=rx_byte; req_count<=req_count+1'b1; end
                    else req_bad<=1;
                end
                if (idle_count==FRAME_GAP-1 && !rx_busy && !rx_valid && !rx_error) begin
                    req_count<=0; req_bad<=0; req_crc<=16'hffff;
                    if (!req_bad && req_count==8 && req[0]==8'd3 &&
                        req_crc==16'h0000) begin
                        request_count<=request_count+1'b1;
                        req_addr={req[2],req[3]}; req_arg={req[4],req[5]};
                        resp[0]<=8'd3;
                        if (req[1]==8'h03 && req_arg>=1 && req_arg<=8 &&
                            req_addr<=31 && req_addr+req_arg<=32) begin
                            resp[1]<=8'h03; resp[2]<=req_arg[7:0]*2;
                            for(i=0;i<8;i=i+1) begin
                                value_temp=register_value(req_addr+i);
                                if(i<req_arg) begin
                                    resp[3+2*i]<=value_temp[15:8];
                                    resp[4+2*i]<=value_temp[7:0];
                                end
                            end
                            payload_length<=3+2*req_arg; state<=PREP_CRC;
                        end else if(req[1]==8'h06 &&
                                    ((req_addr>=16 && req_addr<=26) || req_addr==31)) begin
                            words[req_addr]<=req_arg;
                            write_count<=write_count+1'b1; last_address<=req_addr; last_value<=req_arg;
                            for(i=1;i<6;i=i+1) resp[i]<=req[i];
                            payload_length<=6; state<=PREP_CRC;
                        end else begin
                            // Modbus exception: illegal function, address or value.
                            resp[1]<=req[1] | 8'h80;
                            if(req[1]!=8'h03 && req[1]!=8'h06) resp[2]<=8'h01;
                            else if(req[1]==8'h03 && (req_arg<1 || req_arg>8)) resp[2]<=8'h03;
                            else resp[2]<=8'h02;
                            payload_length<=3; state<=PREP_CRC;
                        end
                    end
                end
              end
              PREP_CRC: begin
                // One payload byte per clock avoids a long combinational CRC chain.
                response_crc<=16'hffff; crc_index<=0; state<=BUILD_CRC;
              end
              BUILD_CRC: begin
                if(crc_index<payload_length) begin
                    response_crc<=crc_byte(response_crc,resp[crc_index]); crc_index<=crc_index+1'b1;
                end else begin
                    resp[payload_length]<=response_crc[7:0]; resp[payload_length+1]<=response_crc[15:8];
                    response_length<=payload_length+2; send_index<=0; state<=SEND_FRAME;
                end
              end
              SEND_FRAME: begin
                if(!tx_busy && !tx_start) begin
                    if(send_index<response_length) begin
                        tx_byte<=resp[send_index]; tx_start<=1; send_index<=send_index+1'b1;
                    end else begin
                        state<=RX_FRAME; idle_count<=0; req_count<=0; req_bad<=0; req_crc<=16'hffff;
                    end
                end
              end
              default: state<=RX_FRAME;
            endcase
        end
    end
endmodule
