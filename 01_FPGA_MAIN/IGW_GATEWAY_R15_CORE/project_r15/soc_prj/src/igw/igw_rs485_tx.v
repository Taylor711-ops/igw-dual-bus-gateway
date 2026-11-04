// Whole RTU ADU transmission. DE covers pre-enable, all stop bits and hold.
// Board U21 has DE and /RE tied together: DE=0 enables reception.
module igw_rs485_tx (
    input wire clk, rst_n, permit, kick,
    input wire [8:0] length, input wire [7:0] buffer_byte,
    input wire [31:0] divisor, input wire [1:0] parity_mode,
    input wire two_stop,
    output reg [7:0] buffer_index,
    output wire tx, de, busy,
    output reg completed
);
    localparam IDLE=0, PRE=1, LOAD=2, WAIT_BYTE=3, HOLD=4;
    reg [2:0] state;
    reg [31:0] timer;
    wire uart_tx, uart_busy, uart_done;
    assign busy=state!=IDLE;
    assign de=permit && state!=IDLE;
    assign tx=permit ? uart_tx : 1'b1;
    igw_uart_tx uart(.clk(clk),.rst_n(rst_n && permit),.divisor(divisor),
        .parity_mode(parity_mode),.two_stop(two_stop),.data(buffer_byte),
        .start(state==LOAD),.tx(uart_tx),.busy(uart_busy),.done(uart_done));
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin state<=IDLE; timer<=0; buffer_index<=0; completed<=0; end
        else begin
            completed<=0;
            if (!permit) begin state<=IDLE; timer<=0; buffer_index<=0; end
            else case(state)
                IDLE: if (kick && length>=4 && length<=256) begin
                    state<=PRE; timer<=divisor-1; buffer_index<=0;
                end
                PRE: if (timer!=0) timer<=timer-1; else state<=LOAD;
                LOAD: state<=WAIT_BYTE;
                WAIT_BYTE: if (uart_done) begin
                    if ({1'b0,buffer_index}+1>=length) begin state<=HOLD; timer<=divisor-1; end
                    else begin buffer_index<=buffer_index+1; state<=LOAD; end
                end
                HOLD: if (timer!=0) timer<=timer-1;
                    else begin state<=IDLE; completed<=1; end
                default: state<=IDLE;
            endcase
        end
    end
endmodule
