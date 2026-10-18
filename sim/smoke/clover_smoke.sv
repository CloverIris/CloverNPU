`timescale 1ns/1ps

// 工具链 smoke DUT：一个带参数、同步复位的加法器。
// 目的不是验证 NPU 功能，而是证明 Verilator / Icarus / cocotb / Yosys
// 四套流程都能读同一份 SystemVerilog。
module clover_smoke #(
    parameter int WIDTH = 32
) (
    input  logic             clk,
    input  logic             rst_n,
    input  logic [WIDTH-1:0] a,
    input  logic [WIDTH-1:0] b,
    output logic [WIDTH-1:0] y
);

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            y <= '0;
        end else begin
            y <= a + b;
        end
    end

endmodule
