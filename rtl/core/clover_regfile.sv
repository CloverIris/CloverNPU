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
    input  logic        boot,
    input  logic [31:0] boot_sp,
    input  logic [31:0] boot_arg,
    output logic [31:0] rdata1,
    output logic [31:0] rdata2,
    output logic [31:0] dbg_a0
);

    logic [31:0] regs [0:31];

    assign rdata1 = (rs1 == 5'd0) ? 32'd0 : regs[rs1];
    assign rdata2 = (rs2 == 5'd0) ? 32'd0 : regs[rs2];
    assign dbg_a0 = regs[10];

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            for (int i = 0; i < 32; i++) begin
                regs[i] <= 32'd0;
            end
        end else if (boot) begin
            regs[1]  <= 32'd0;       // ra = 0: kernel return sentinel
            regs[2]  <= boot_sp;
            regs[10] <= boot_arg;
        end else if (we && (rd != 5'd0)) begin
            regs[rd] <= wdata;
        end
    end

endmodule
