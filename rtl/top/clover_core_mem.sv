`include "clover_pkg.sv"
`include "clover_core.sv"
`include "clover_scratchpad.sv"

// Core + scratchpad integration with a test/host loader port.
// Code and data live in scratchpad (base 0), stack at 0x0003FFF0.
module clover_core_mem (
    input  logic         clk,
    input  logic         rst_n,

    // Test/host loader: write one 64-byte line while the core is in reset.
    input  logic         loader_we,
    input  logic [17:0]  loader_addr,
    input  logic [511:0] loader_data,
    input  logic [63:0]  loader_be,

    output logic         halted,
    output logic [31:0]  exit_code,
    output logic [7:0]   uart_data,
    output logic         uart_valid,
    output logic [31:0]  pc_out,
    output logic [31:0]  cycle_count,
    output logic [3:0]   halt_reason
);

    logic        imem_req;
    logic [31:0] imem_addr;
    logic        imem_ready;
    logic [31:0] imem_rdata;

    logic        dmem_req;
    logic        dmem_we;
    logic [31:0] dmem_addr;
    logic [1:0]  dmem_size;
    logic [31:0] dmem_wdata;
    logic        dmem_ready;
    logic [31:0] dmem_rdata;

    logic        a_req;
    logic [17:0] a_addr;
    logic [511:0] a_rdata;
    logic        b_req;
    logic [17:0] b_addr;
    logic [511:0] b_rdata;
    logic        w_req;
    logic [17:0] w_addr;
    logic [511:0] w_data;
    logic [63:0] w_be;
    logic        w_ready;

    clover_core #(
        .RESET_PC(32'd0)
    ) u_core (
        .clk        (clk),
        .rst_n      (rst_n),
        .imem_req   (imem_req),
        .imem_addr  (imem_addr),
        .imem_ready (imem_ready),
        .imem_rdata (imem_rdata),
        .dmem_req   (dmem_req),
        .dmem_we    (dmem_we),
        .dmem_addr  (dmem_addr),
        .dmem_size  (dmem_size),
        .dmem_wdata (dmem_wdata),
        .dmem_ready (dmem_ready),
        .dmem_rdata (dmem_rdata),
        .halted     (halted),
        .exit_code  (exit_code),
        .uart_data  (uart_data),
        .uart_valid (uart_valid),
        .pc_out     (pc_out),
        .cycle_count(cycle_count),
        .halt_reason(halt_reason)
    );

    clover_scratchpad u_sp (
        .clk     (clk),
        .a_req   (a_req),
        .a_addr  (a_addr),
        .a_rdata (a_rdata),
        .b_req   (b_req),
        .b_addr  (b_addr),
        .b_rdata (b_rdata),
        .w_req   (w_req),
        .w_addr  (w_addr),
        .w_data  (w_data),
        .w_be    (w_be),
        .w_ready (w_ready)
    );

    // ---------------------------------------------------------- fetch path
    logic [3:0] imem_lane;
    assign imem_lane = imem_addr[5:2];
    assign a_req  = imem_req;
    assign a_addr = {imem_addr[17:6], 6'b0};
    assign imem_rdata = a_rdata[32*imem_lane +: 32];
    assign imem_ready = 1'b1;

    // ----------------------------------------------------------- data path
    logic [3:0]  dmem_lane;
    logic [1:0]  dmem_offset;
    logic [3:0]  lane_be;
    logic [511:0] core_wdata;
    logic [63:0] core_be;

    assign dmem_lane   = dmem_addr[5:2];
    assign dmem_offset = dmem_addr[1:0];
    assign b_req       = dmem_req && !dmem_we;
    assign b_addr      = {dmem_addr[17:6], 6'b0};
    assign dmem_rdata  = b_rdata[32*dmem_lane +: 32];
    assign dmem_ready  = 1'b1;

    always_comb begin
        unique case (dmem_size)
            2'd0: lane_be = 4'b0001 << dmem_offset;
            2'd1: lane_be = 4'b0011 << dmem_offset;
            default: lane_be = 4'b1111;
        endcase
    end

    always_comb begin
        core_wdata = 512'd0;
        core_wdata[32*dmem_lane +: 32] = dmem_wdata << (8*dmem_offset);
    end

    assign core_be = {60'd0, lane_be} << (4*dmem_lane);

    assign w_req  = loader_we || (dmem_req && dmem_we);
    assign w_addr = loader_we ? loader_addr : {dmem_addr[17:6], 6'b0};
    assign w_data = loader_we ? loader_data : core_wdata;
    assign w_be   = loader_we ? loader_be : core_be;

endmodule
