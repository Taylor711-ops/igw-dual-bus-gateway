// Portable UART: 8 data bits, parity 0=none, 1=even, 2=odd; 1 or 2 stop bits.
// Configuration must remain constant for the whole character.
module igw_uart_rx (
    input wire clk, input wire rst_n, input wire rx,
    input wire [31:0] divisor, input wire [1:0] parity_mode,
    input wire two_stop,
    output reg [7:0] data, output reg valid, output reg error,
    output reg start_pulse, output wire busy
);
    (* ASYNC_REG = "TRUE" *) reg rx_meta, rx_sync;
    reg [2:0] state;
    reg [31:0] timer;
    reg [2:0] bit_index;
    reg [7:0] shift;
    reg bad;
    localparam IDLE=0, START=1, DATA=2, PARITY=3, STOP1=4, STOP2=5, WAIT_HIGH=7;
    assign busy = state != IDLE;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin rx_meta<=1; rx_sync<=1; end
        else begin rx_meta<=rx; rx_sync<=rx_meta; end
    end
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state<=IDLE; timer<=0; bit_index<=0; shift<=0; bad<=0;
            data<=0; valid<=0; error<=0; start_pulse<=0;
        end else begin
            valid<=0; error<=0; start_pulse<=0;
            case (state)
            IDLE: if (!rx_sync && divisor>=16) begin
                state<=START; timer<=(divisor>>1)-1; bad<=0; start_pulse<=1;
            end
            START: if (timer!=0) timer<=timer-1;
                else if (rx_sync) state<=IDLE; // Short glitch, no character.
                else begin state<=DATA; timer<=divisor-1; bit_index<=0; end
            DATA: if (timer!=0) timer<=timer-1;
                else begin
                    shift[bit_index]<=rx_sync; timer<=divisor-1;
                    if (bit_index==7) state<=parity_mode==0 ? STOP1 : PARITY;
                    else bit_index<=bit_index+1;
                end
            PARITY: if (timer!=0) timer<=timer-1;
                else begin
                    if (rx_sync != ((^shift) ^ (parity_mode==2))) bad<=1;
                    state<=STOP1; timer<=divisor-1;
                end
            STOP1: if (timer!=0) timer<=timer-1;
                else begin
                    if (two_stop) begin
                        if (!rx_sync) bad<=1;
                        state<=STOP2; timer<=divisor-1;
                    end else begin
                        data<=shift; valid<=!bad && rx_sync; error<=bad || !rx_sync;
                        state<=rx_sync ? IDLE : WAIT_HIGH;
                    end
                end
            STOP2: if (timer!=0) timer<=timer-1;
                else begin
                    data<=shift; valid<=!bad && rx_sync; error<=bad || !rx_sync;
                    state<=rx_sync ? IDLE : WAIT_HIGH;
                end
            WAIT_HIGH: if (rx_sync) state<=IDLE; // A break cannot flood the FIFO.
            default: state<=IDLE;
            endcase
        end
    end
endmodule

module igw_uart_tx (
    input wire clk, input wire rst_n,
    input wire [31:0] divisor, input wire [1:0] parity_mode,
    input wire two_stop, input wire [7:0] data, input wire start,
    output reg tx, output reg busy, output reg done
);
    reg [11:0] bits;
    reg [3:0] bits_left;
    reg [31:0] timer;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin tx<=1; busy<=0; done<=0; bits<=0; bits_left<=0; timer<=0; end
        else begin
            done<=0;
            if (!busy) begin
                tx<=1;
                if (start && divisor>=16) begin
                    // The start bit is emitted now; subsequent bits are shifted LSB first.
                    if (parity_mode==0) begin bits<={2'b11,1'b1,data,1'b0}; bits_left<=two_stop ? 11 : 10; end
                    else begin bits<={1'b1,1'b1,((^data)^(parity_mode==2)),data,1'b0}; bits_left<=two_stop ? 12 : 11; end
                    tx<=0; busy<=1; timer<=divisor-1;
                end
            end else if (timer!=0) timer<=timer-1;
            else if (bits_left==1) begin tx<=1; busy<=0; done<=1; end
            else begin
                bits<= {1'b1,bits[11:1]}; tx<=bits[1];
                bits_left<=bits_left-1; timer<=divisor-1;
            end
        end
    end
endmodule
