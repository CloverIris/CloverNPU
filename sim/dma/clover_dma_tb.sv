`timescale 1ns/1ps

`include "clover_pkg.sv"
`include "clover_dma.sv"
`include "clover_scratchpad.sv"
`include "clover_axi_ram.sv"

module clover_dma_tb (
    input  logic         clk,
    input  logic         rst_n,

    input  logic         start,
    input  logic [1:0]   dir,
    input  logic [31:0]  src_addr,
    input  logic [31:0]  dst_addr,
    input  logic [31:0]  bytes_per_row,
    input  logic [31:0]  num_rows,
    input  logic [31:0]  src_stride,
    input  logic [31:0]  dst_stride,
    output logic         busy,
    output logic         done,
    output logic         error,

    input  logic         host_we,
    input  logic [15:0]  host_addr,
    input  logic [127:0] host_wdata,
    input  logic [15:0]  host_wstrb,
    output logic [127:0] host_rdata,

    input  logic         loader_we,
    input  logic [17:0]  loader_addr,
    input  logic [511:0] loader_data,
    input  logic [63:0]  loader_be
);

    logic [31:0]  m_araddr;
    logic [7:0]   m_arlen;
    logic [2:0]   m_arsize;
    logic [1:0]   m_arburst;
    logic [3:0]   m_arid;
    logic         m_arvalid;
    logic         m_arready;
    logic [127:0] m_rdata;
    logic         m_rvalid;
    logic         m_rlast;
    logic [3:0]   m_rid;
    logic         m_rready;
    logic [31:0]  m_awaddr;
    logic [7:0]   m_awlen;
    logic [2:0]   m_awsize;
    logic [1:0]   m_awburst;
    logic [3:0]   m_awid;
    logic         m_awvalid;
    logic         m_awready;
    logic [127:0] m_wdata;
    logic [15:0]  m_wstrb;
    logic         m_wlast;
    logic         m_wvalid;
    logic         m_wready;
    logic [1:0]   m_bresp;
    logic         m_bvalid;
    logic         m_bready;

    logic         dma_sp_rd_req;
    logic [17:0]  dma_sp_rd_addr;
    logic [511:0] dma_sp_rd_data;
    logic         dma_sp_wr_req;
    logic [17:0]  dma_sp_wr_addr;
    logic [511:0] dma_sp_wr_data;
    logic [63:0]  dma_sp_wr_be;
    logic         dma_sp_wr_ready;

    logic         sp_a_req;
    logic [17:0]  sp_a_addr;
    logic [511:0] sp_a_rdata;
    logic         sp_b_req;
    logic [17:0]  sp_b_addr;
    logic [511:0] sp_b_rdata;
    logic         sp_w_req;
    logic [17:0]  sp_w_addr;
    logic [511:0] sp_w_data;
    logic [63:0]  sp_w_be;
    logic         sp_w_ready;

    clover_dma u_dma (
        .clk          (clk),
        .rst_n        (rst_n),
        .start        (start),
        .dir          (dir),
        .src_addr     (src_addr),
        .dst_addr     (dst_addr),
        .bytes_per_row(bytes_per_row),
        .num_rows     (num_rows),
        .src_stride   (src_stride),
        .dst_stride   (dst_stride),
        .busy         (busy),
        .done         (done),
        .error        (error),
        .m_araddr     (m_araddr),
        .m_arlen      (m_arlen),
        .m_arsize     (m_arsize),
        .m_arburst    (m_arburst),
        .m_arid       (m_arid),
        .m_arvalid    (m_arvalid),
        .m_arready    (m_arready),
        .m_rdata      (m_rdata),
        .m_rvalid     (m_rvalid),
        .m_rlast      (m_rlast),
        .m_rid        (m_rid),
        .m_rready     (m_rready),
        .m_awaddr     (m_awaddr),
        .m_awlen      (m_awlen),
        .m_awsize     (m_awsize),
        .m_awburst    (m_awburst),
        .m_awid       (m_awid),
        .m_awvalid    (m_awvalid),
        .m_awready    (m_awready),
        .m_wdata      (m_wdata),
        .m_wstrb      (m_wstrb),
        .m_wlast      (m_wlast),
        .m_wvalid     (m_wvalid),
        .m_wready     (m_wready),
        .m_bresp      (m_bresp),
        .m_bvalid     (m_bvalid),
        .m_bready     (m_bready),
        .sp_rd_req    (dma_sp_rd_req),
        .sp_rd_addr   (dma_sp_rd_addr),
        .sp_rd_data   (dma_sp_rd_data),
        .sp_wr_req    (dma_sp_wr_req),
        .sp_wr_addr   (dma_sp_wr_addr),
        .sp_wr_data   (dma_sp_wr_data),
        .sp_wr_be     (dma_sp_wr_be),
        .sp_wr_ready  (dma_sp_wr_ready)
    );

    clover_axi_ram u_ram (
        .clk        (clk),
        .rst_n      (rst_n),
        .s_awaddr   (m_awaddr),
        .s_awlen    (m_awlen),
        .s_awsize   (m_awsize),
        .s_awburst  (m_awburst),
        .s_awid     (m_awid),
        .s_awvalid  (m_awvalid),
        .s_awready  (m_awready),
        .s_wdata    (m_wdata),
        .s_wstrb    (m_wstrb),
        .s_wlast    (m_wlast),
        .s_wvalid   (m_wvalid),
        .s_wready   (m_wready),
        .s_bresp    (m_bresp),
        .s_bvalid   (m_bvalid),
        .s_bready   (m_bready),
        .s_araddr   (m_araddr),
        .s_arlen    (m_arlen),
        .s_arsize   (m_arsize),
        .s_arburst  (m_arburst),
        .s_arid     (m_arid),
        .s_arvalid  (m_arvalid),
        .s_arready  (m_arready),
        .s_rdata    (m_rdata),
        .s_rid      (m_rid),
        .s_rlast    (m_rlast),
        .s_rvalid   (m_rvalid),
        .s_rready   (m_rready),
        .host_we    (host_we),
        .host_addr  (host_addr),
        .host_wdata (host_wdata),
        .host_wstrb (host_wstrb),
        .host_rdata (host_rdata)
    );

    assign sp_a_req  = 1'b0;
    assign sp_a_addr = 18'd0;

    assign sp_b_req  = dma_sp_rd_req;
    assign sp_b_addr = dma_sp_rd_addr;
    assign dma_sp_rd_data = sp_b_rdata;

    assign sp_w_req  = loader_we ? 1'b1 : dma_sp_wr_req;
    assign sp_w_addr = loader_we ? loader_addr : dma_sp_wr_addr;
    assign sp_w_data = loader_we ? loader_data : dma_sp_wr_data;
    assign sp_w_be   = loader_we ? loader_be : dma_sp_wr_be;
    assign dma_sp_wr_ready = loader_we ? 1'b0 : sp_w_ready;

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
        .w_ready (sp_w_ready)
    );

endmodule
