`timescale 1ns/1ps

// Behavioral AXI4 slave RAM for DMA simulation.
// One 64 KiB window; AXI addresses 0x1000_0000 + offset.
module clover_axi_ram (
    input  logic         clk,
    input  logic         rst_n,

    input  logic [31:0]  s_awaddr,
    input  logic [7:0]   s_awlen,
    input  logic [2:0]   s_awsize,
    input  logic [1:0]   s_awburst,
    input  logic [3:0]   s_awid,
    input  logic         s_awvalid,
    output logic         s_awready,
    input  logic [127:0] s_wdata,
    input  logic [15:0]  s_wstrb,
    input  logic         s_wlast,
    input  logic         s_wvalid,
    output logic         s_wready,
    output logic [1:0]   s_bresp,
    output logic         s_bvalid,
    input  logic         s_bready,
    input  logic [31:0]  s_araddr,
    input  logic [7:0]   s_arlen,
    input  logic [2:0]   s_arsize,
    input  logic [1:0]   s_arburst,
    input  logic [3:0]   s_arid,
    input  logic         s_arvalid,
    output logic         s_arready,
    output logic [127:0] s_rdata,
    output logic [3:0]   s_rid,
    output logic         s_rlast,
    output logic         s_rvalid,
    input  logic         s_rready,

    // Host/debug port.
    input  logic         host_we,
    input  logic [15:0]  host_addr,
    input  logic [127:0] host_wdata,
    input  logic [15:0]  host_wstrb,
    output logic [127:0] host_rdata
);

    logic [127:0] mem [0:4095];

    logic        aw_pending;
    logic        w_pending;
    logic        bvalid_q;
    logic [31:0] awaddr_q;
    logic [127:0] wdata_q;
    logic [15:0] wstrb_q;

    logic        rvalid_q;
    logic [31:0] araddr_q;
    logic [7:0]  arlen_q;
    logic [7:0]  rcount_q;

    assign s_awready = !aw_pending && !bvalid_q;
    assign s_wready  = !w_pending && !bvalid_q;
    assign s_bresp   = 2'b00;
    assign s_bvalid  = bvalid_q;

    assign s_arready = !rvalid_q;
    assign s_rid     = 4'd0;
    assign s_rlast   = (rcount_q == arlen_q);
    assign s_rvalid  = rvalid_q;
    assign s_rdata   = mem[(araddr_q[15:4] + {4'd0, rcount_q}) & 12'hFFF];

    assign host_rdata = mem[host_addr[15:4]];

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            aw_pending <= 1'b0;
            w_pending  <= 1'b0;
            bvalid_q   <= 1'b0;
            awaddr_q   <= 32'd0;
            wdata_q    <= 128'd0;
            wstrb_q    <= 16'd0;
            rvalid_q   <= 1'b0;
            araddr_q    <= 32'd0;
            arlen_q     <= 8'd0;
            rcount_q    <= 8'd0;
        end else begin
            if (s_awvalid && s_awready) begin
                aw_pending <= 1'b1;
                awaddr_q   <= s_awaddr;
            end
            if (s_wvalid && s_wready) begin
                w_pending <= 1'b1;
                wdata_q   <= s_wdata;
                wstrb_q   <= s_wstrb;
            end
            if (aw_pending && w_pending && !bvalid_q) begin
                for (int i = 0; i < 16; i++) begin
                    if (wstrb_q[i]) begin
                        mem[awaddr_q[15:4]][8*i +: 8] <= wdata_q[8*i +: 8];
                    end
                end
                aw_pending <= 1'b0;
                w_pending  <= 1'b0;
                bvalid_q   <= 1'b1;
            end
            if (bvalid_q && s_bready) begin
                bvalid_q <= 1'b0;
            end

            if (s_arvalid && s_arready) begin
                araddr_q <= s_araddr;
                arlen_q  <= s_arlen;
                rcount_q <= 8'd0;
                rvalid_q <= 1'b1;
            end
            if (rvalid_q && s_rready) begin
                if (rcount_q == arlen_q) begin
                    rvalid_q <= 1'b0;
                end else begin
                    rcount_q <= rcount_q + 8'd1;
                end
            end

            if (host_we) begin
                for (int i = 0; i < 16; i++) begin
                    if (host_wstrb[i]) begin
                        mem[host_addr[15:4]][8*i +: 8] <= host_wdata[8*i +: 8];
                    end
                end
            end
        end
    end

endmodule
