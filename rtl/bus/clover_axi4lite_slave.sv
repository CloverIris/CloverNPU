`include "clover_pkg.sv"

// Minimal AXI4-Lite slave for a 4 KiB CSR window.
// The CSR block applies byte strobes; this module only handles the channels.
module clover_axi4lite_slave (
    input  logic        clk,
    input  logic        rst_n,

    input  logic [31:0] s_awaddr,
    input  logic        s_awvalid,
    output logic        s_awready,
    input  logic [31:0] s_wdata,
    input  logic [3:0]  s_wstrb,
    input  logic        s_wvalid,
    output logic        s_wready,
    output logic [1:0]  s_bresp,
    output logic        s_bvalid,
    input  logic        s_bready,

    input  logic [31:0] s_araddr,
    input  logic        s_arvalid,
    output logic        s_arready,
    output logic [31:0] s_rdata,
    output logic [1:0]  s_rresp,
    output logic        s_rvalid,
    input  logic        s_rready,

    output logic        csr_we,
    output logic [11:0] csr_waddr,
    output logic [31:0] csr_wdata,
    output logic [3:0]  csr_wstrb,
    output logic [11:0] csr_raddr,
    input  logic [31:0] csr_rdata
);

    logic        aw_pending;
    logic        w_pending;
    logic        bvalid_q;
    logic [31:0] awaddr_q;
    logic [31:0] wdata_q;
    logic [3:0]  wstrb_q;

    logic        rvalid_q;
    logic [31:0] araddr_q;

    assign s_awready = !aw_pending && !bvalid_q;
    assign s_wready  = !w_pending && !bvalid_q;
    assign s_bresp   = 2'b00;
    assign s_bvalid  = bvalid_q;
    assign s_arready = !rvalid_q;
    assign s_rresp   = 2'b00;
    assign s_rvalid  = rvalid_q;
    assign s_rdata   = csr_rdata;

    assign csr_we     = aw_pending && w_pending && !bvalid_q;
    assign csr_waddr  = awaddr_q[11:0];
    assign csr_wdata  = wdata_q;
    assign csr_wstrb  = wstrb_q;
    assign csr_raddr  = araddr_q[11:0];

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            aw_pending <= 1'b0;
            w_pending  <= 1'b0;
            bvalid_q   <= 1'b0;
            awaddr_q   <= 32'd0;
            wdata_q    <= 32'd0;
            wstrb_q    <= 4'd0;
            rvalid_q   <= 1'b0;
            araddr_q   <= 32'd0;
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
            if (csr_we) begin
                aw_pending <= 1'b0;
                w_pending  <= 1'b0;
                bvalid_q   <= 1'b1;
            end
            if (bvalid_q && s_bready) begin
                bvalid_q <= 1'b0;
            end

            if (s_arvalid && s_arready) begin
                araddr_q <= s_araddr;
                rvalid_q <= 1'b1;
            end
            if (rvalid_q && s_rready) begin
                rvalid_q <= 1'b0;
            end
        end
    end

endmodule
