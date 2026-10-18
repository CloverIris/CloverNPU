`include "clover_pkg.sv"

module clover_regfile
(
    input  logic        clk,
    input  logic        rst_n,
    input  logic [4:0]  rs1,
    input  logic [4:0]  rs2,
    input  logic [4:0]  rd,
    input  logic        we,
    input  logic [31:0] wdata,
    output logic [31:0] rdata1,
    output logic [31:0] rdata2
);

    logic [31:0] regs [0:31];

    assign rdata1 = (rs1 == 5'd0) ? 32'd0 : regs[rs1];
    assign rdata2 = (rs2 == 5'd0) ? 32'd0 : regs[rs2];

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            for (int i = 0; i < 32; i++) begin
                regs[i] <= 32'd0;
            end
        end else if (we && (rd != 5'd0)) begin
            regs[rd] <= wdata;
        end
    end

endmodule
