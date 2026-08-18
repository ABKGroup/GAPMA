// Greatest Common Divisor — 16-bit iterative Euclidean subtraction
module gcd #(parameter WIDTH = 16) (
    input  wire             clk,
    input  wire             reset,
    input  wire             operands_val,
    input  wire [WIDTH-1:0] operands_A,
    input  wire [WIDTH-1:0] operands_B,
    output reg              operands_rdy,
    output reg              result_val,
    output reg  [WIDTH-1:0] result
);

    localparam IDLE    = 2'b00;
    localparam COMPUTE = 2'b01;
    localparam DONE    = 2'b10;

    reg [1:0]       state;
    reg [WIDTH-1:0] A, B;

    always @(posedge clk) begin
        if (reset) begin
            state        <= IDLE;
            operands_rdy <= 1'b1;
            result_val   <= 1'b0;
            result       <= {WIDTH{1'b0}};
            A            <= {WIDTH{1'b0}};
            B            <= {WIDTH{1'b0}};
        end else begin
            case (state)
                IDLE: begin
                    result_val <= 1'b0;
                    if (operands_val) begin
                        A            <= operands_A;
                        B            <= operands_B;
                        operands_rdy <= 1'b0;
                        state        <= COMPUTE;
                    end
                end
                COMPUTE: begin
                    if (A == B) begin
                        result     <= A;
                        result_val <= 1'b1;
                        state      <= DONE;
                    end else if (A > B) begin
                        A <= A - B;
                    end else begin
                        B <= B - A;
                    end
                end
                DONE: begin
                    result_val   <= 1'b0;
                    operands_rdy <= 1'b1;
                    state        <= IDLE;
                end
                default: state <= IDLE;
            endcase
        end
    end

endmodule
