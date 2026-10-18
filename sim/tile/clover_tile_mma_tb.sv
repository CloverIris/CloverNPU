`timescale 1ns/1ps
`include "clover_pkg.sv"
`include "clover_tile_mma.sv"
`include "clover_scratchpad.sv"

module clover_tile_mma_tb (
    input  logic         clk,
    input  logic         rst_n,

    input  logic         host_we,
    input  logic [17:0]  host_addr,
    input  logic [511:0] host_wdata,
    input  logic [63:0]  host_be,
    output logic [511:0] host_rdata,

    input  logic         start,
    input  logic [31:0]  desc_addr,
    output logic         busy,
    output logic         done,
    output logic         error,
    output logic [3:0]   error_code
);

    logic        t_a_req;
    logic [17:0] t_a_addr;
    logic [511:0] t_a_rdata;
    logic        t_b_req;
    logic [17:0] t_b_addr;
    logic [511:0] t_b_rdata;
    logic        t_w_req;
    logic [17:0] t_w_addr;
    logic [511:0] t_w_data;
    logic [63:0] t_w_be;
    logic        t_w_ready;

    logic        sp_a_req;
    logic [17:0] sp_a_addr;
    logic [511:0] sp_a_rdata;
    logic        sp_b_req;
    logic [17:0] sp_b_addr;
    logic [511:0] sp_b_rdata;
    logic        sp_w_req;
    logic [17:0] sp_w_addr;
    logic [511:0] sp_w_data;
    logic [63:0] sp_w_be;

    // Host owns the ports while the tile engine is idle.
    assign sp_a_req  = busy ? t_a_req : host_we;
    assign sp_a_addr = busy ? t_a_addr : host_addr;
    assign sp_b_req  = busy ? t_b_req : 1'b0;
    assign sp_b_addr = t_b_addr;
    assign sp_w_req  = busy ? t_w_req : host_we;
    assign sp_w_addr = busy ? t_w_addr : host_addr;
    assign sp_w_data = busy ? t_w_data : host_wdata;
    assign sp_w_be   = busy ? t_w_be : host_be;
    assign host_rdata = sp_a_rdata;

    clover_tile_mma u_mma (
        .clk        (clk),
        .rst_n      (rst_n),
        .start      (start),
        .desc_addr  (desc_addr),
        .busy       (busy),
        .done       (done),
        .error      (error),
        .error_code (error_code),
        .a_req      (t_a_req),
        .a_addr     (t_a_addr),
        .a_rdata    (sp_a_rdata),
        .b_req      (t_b_req),
        .b_addr     (t_b_addr),
        .b_rdata    (sp_b_rdata),
        .w_req      (t_w_req),
        .w_addr     (t_w_addr),
        .w_data     (t_w_data),
        .w_be       (t_w_be),
        .w_ready    (t_w_ready)
    );

    clover_scratchpad u_sp (
        .clk     (clk),
        .a_req   (sp_a_req),
        .a_addr  (sp_a_addr),
        .a_rdata (sp_a_rdata),
        .b_req   (sp_b_req),
        .b_addr  (sp_b_addr),
        .b_rdata (sp_b_rdata),
        .w_req   (sp_w_req),
        .w_addr  (sp_w_addr),
        .w_data  (sp_w_data),
        .w_be    (sp_w_be),
        .w_ready (t_w_ready)
    );

endmodule
