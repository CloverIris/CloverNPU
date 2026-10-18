`include "clover_pkg.sv"

// 256 KiB scratchpad: 16 banks x 4096 words (16 KiB per bank).
// Two 64-byte read ports and one 64-byte write port with byte enables.
// Address is a byte address in [0, 0x3FFFF]; the low 6 bits are ignored for
// the 64-byte ports (adapters align accesses to a line).
module clover_scratchpad (
    input  logic         clk,

    // Read port A (64 B)
    input  logic         a_req,
    input  logic [17:0]  a_addr,
    output logic [511:0] a_rdata,

    // Read port B (64 B)
    input  logic         b_req,
    input  logic [17:0]  b_addr,
    output logic [511:0] b_rdata,

    // Write port (64 B + byte enables)
    input  logic         w_req,
    input  logic [17:0]  w_addr,
    input  logic [511:0] w_data,
    input  logic [63:0]  w_be,
    output logic         w_ready
);

    localparam int unsigned LINES = 4096;

    logic [31:0] bank [0:15][0:LINES-1];

    logic [11:0] a_line;
    logic [11:0] b_line;
    logic [11:0] w_line;

    assign a_line = a_addr[17:6];
    assign b_line = b_addr[17:6];
    assign w_line = w_addr[17:6];

    genvar g;
    generate
        for (g = 0; g < 16; g++) begin : g_read
            assign a_rdata[32*g +: 32] = bank[g][a_line];
            assign b_rdata[32*g +: 32] = bank[g][b_line];
        end
    endgenerate

    always_ff @(posedge clk) begin
        if (w_req) begin
            for (int i = 0; i < 16; i++) begin
                if (w_be[4*i + 0]) bank[i][w_line][7:0]   <= w_data[32*i +: 8];
                if (w_be[4*i + 1]) bank[i][w_line][15:8]  <= w_data[32*i+8 +: 8];
                if (w_be[4*i + 2]) bank[i][w_line][23:16] <= w_data[32*i+16 +: 8];
                if (w_be[4*i + 3]) bank[i][w_line][31:24] <= w_data[32*i+24 +: 8];
            end
        end
    end

    assign w_ready = 1'b1;

endmodule
