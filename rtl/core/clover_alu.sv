`include "clover_pkg.sv"

module clover_alu
(
    input  logic [31:0] a,
    input  logic [31:0] b,
    input  alu_op_t     op,
    output logic [31:0] y
);

    logic signed [63:0] sa64;
    logic signed [63:0] sb64;
    logic signed [63:0] sp;
    logic        [63:0] up;
    logic signed [63:0] spsu;
    logic signed [31:0] div_s;
    logic signed [31:0] rem_s;
    logic        [31:0] div_u;
    logic        [31:0] rem_u;

    always_comb begin
        sa64 = {{32{a[31]}}, a};
        sb64 = {{32{b[31]}}, b};
        sp   = sa64 * sb64;
        up   = {32'b0, a} * {32'b0, b};
        spsu = sa64 * {32'b0, b};

        if (b == 32'd0) begin
            div_s = -32'sd1;
            rem_s = $signed(a);
            div_u = 32'hFFFF_FFFF;
            rem_u = a;
        end else if ((a == 32'h8000_0000) && (b == 32'hFFFF_FFFF)) begin
            div_s = $signed(a);
            rem_s = 32'sd0;
            div_u = a / b;
            rem_u = a % b;
        end else begin
            div_s = $signed(a) / $signed(b);
            rem_s = $signed(a) % $signed(b);
            div_u = a / b;
            rem_u = a % b;
        end

        unique case (op)
            ALU_ADD:   y = a + b;
            ALU_SUB:   y = a - b;
            ALU_SLL:   y = a << b[4:0];
            ALU_SLT:   y = ($signed(a) < $signed(b)) ? 32'd1 : 32'd0;
            ALU_SLTU:  y = (a < b) ? 32'd1 : 32'd0;
            ALU_XOR:   y = a ^ b;
            ALU_SRL:   y = a >> b[4:0];
            ALU_SRA:   y = $signed(a) >>> b[4:0];
            ALU_OR:    y = a | b;
            ALU_AND:   y = a & b;
            ALU_MUL:   y = sp[31:0];
            ALU_MULH:  y = sp[63:32];
            ALU_MULHSU:y = spsu[63:32];
            ALU_MULHU: y = up[63:32];
            ALU_DIV:   y = div_s;
            ALU_DIVU:  y = div_u;
            ALU_REM:   y = rem_s;
            ALU_REMU:  y = rem_u;
            ALU_PASS_B:y = b;
            default:   y = 32'd0;
        endcase
    end

endmodule
