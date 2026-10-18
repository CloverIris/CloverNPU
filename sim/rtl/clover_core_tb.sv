`timescale 1ns/1ps

`include "clover_pkg.sv"
`include "clover_core.sv"

// Simulation-only memory model for the scalar core.
//   DRAM      0x1000_0000 .. 0x1003_FFFF -> mem[0x00000 .. 0x0FFFF]
//   scratch   0x0000_0000 .. 0x0003_FFFF -> mem[0x20000 .. 0x2FFFF]
module clover_core_tb (
    input  logic        clk,
    input  logic        rst_n,
    output logic        halted,
    output logic [31:0] exit_code,
    output logic [7:0]  uart_data,
    output logic        uart_valid,
    output logic [31:0] pc_out,
    output logic [31:0] cycle_count,
    output logic [3:0]  halt_reason
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


    logic [31:0] mem [0:196607];

    function automatic int idx(input logic [31:0] addr);
        if (addr >= 32'h1000_0000) begin
            return int'(addr[17:2]);
        end
        return 32'h0002_0000 + int'(addr[15:2]);
    endfunction

    assign imem_ready = 1'b1;
    assign dmem_ready = 1'b1;
    assign imem_rdata = mem[idx(imem_addr)];
    assign dmem_rdata = mem[idx(dmem_addr)];

    always_ff @(posedge clk) begin
        if (dmem_req && dmem_we) begin
            unique case (dmem_size)
                2'd0: begin
                    unique case (dmem_addr[1:0])
                        2'd0: mem[idx(dmem_addr)][7:0]   <= dmem_wdata[7:0];
                        2'd1: mem[idx(dmem_addr)][15:8]  <= dmem_wdata[7:0];
                        2'd2: mem[idx(dmem_addr)][23:16] <= dmem_wdata[7:0];
                        default: mem[idx(dmem_addr)][31:24] <= dmem_wdata[7:0];
                    endcase
                end
                2'd1: begin
                    if (dmem_addr[1]) begin
                        mem[idx(dmem_addr)][31:16] <= dmem_wdata[15:0];
                    end else begin
                        mem[idx(dmem_addr)][15:0] <= dmem_wdata[15:0];
                    end
                end
                default: mem[idx(dmem_addr)] <= dmem_wdata;
            endcase
        end
    end

    logic       core_start;
    logic [3:0] boot_count;
    logic       core_active;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            boot_count <= 4'd0;
        end else if (boot_count < 4'd4) begin
            boot_count <= boot_count + 4'd1;
        end
    end

    assign core_start = rst_n && (boot_count == 4'd0);

    clover_core u_core (
        .clk        (clk),
        .rst_n      (rst_n),
        .core_start (core_start),
        .core_entry (32'h1000_0000),
        .core_sp    (32'h0003_FFF0),
        .core_arg   (32'd0),
        .core_active(core_active),
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

    initial begin
        string prog_path;
        for (int i = 0; i < 196608; i++) begin
            mem[i] = 32'd0;
        end
        if (!$value$plusargs("PROG=%s", prog_path)) begin
            prog_path = "prog.hex";
        end
        $display("CLOVER_TB loading %s", prog_path);
        $readmemh(prog_path, mem);
    end

endmodule
