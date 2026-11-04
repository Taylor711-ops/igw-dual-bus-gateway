/* The PS UART is allowed to drive the common RS485 TX only after an
 * addressed file header.  FC03/06/45 remain owned by the existing PL core.
 * A window is long enough for the host's 30-second SD operation timeout. */
module file_tx_guard #(
    parameter integer CLK_FREQ=50000000,
    parameter integer UART_BPS=9600,
    parameter integer WINDOW_CLKS=2000000000
)(input wire clk,rst_n,rxd,ps_txd,pl_txd,output wire txd);
    localparam integer BIT_CLKS=(CLK_FREQ+UART_BPS/2)/UART_BPS;
    localparam integer GAP=BIT_CLKS*39;
    wire [7:0] byte_data;
    wire valid,error,started,busy;
    reg [31:0] idle,window;
    reg [1:0] prefix;
    reg armed;
    igw_uart_rx_8e1 #(.BIT_CLKS(BIT_CLKS)) snoop(
        .clk(clk),.rst_n(rst_n),.enable(1'b1),.rxd(rxd),
        .data(byte_data),.valid(valid),.error(error),.started(started),.busy(busy));
    assign txd=pl_txd & ((window!=0 && armed)?ps_txd:1'b1);
    always @(posedge clk or negedge rst_n) begin
        if(!rst_n) begin idle<=0;window<=0;prefix<=0;armed<=0;end
        else begin
            if(window!=0) begin window<=window-1'b1;if(ps_txd)armed<=1;end
            else armed<=0;
            if(started||busy||valid||error) idle<=0;
            else if(idle<GAP) idle<=idle+1'b1;
            if(idle==GAP-1) prefix<=0;
            if(error) prefix<=2;
            if(valid) begin
                if(prefix==0) prefix <= (byte_data==8'd3)?1:2;
                else if(prefix==1) begin
                    if(byte_data==8'h42) begin
                        window<=WINDOW_CLKS;
                        armed<=ps_txd; // do not forward an uninitialized stuck-low UART
                    end
                    prefix<=2;
                end
            end
        end
    end
endmodule
