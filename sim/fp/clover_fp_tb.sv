`timescale 1ns/1ps
`include "clover_fp_pkg.sv"

module clover_fp_tb (
    input  logic [31:0] a,
    input  logic [31:0] b,
    input  logic        is_mul,
    output logic [31:0] y
);
    always_comb begin
        y = is_mul ? clover_fp32_mul(a, b) : clover_fp32_add(a, b);
    end
endmodule
