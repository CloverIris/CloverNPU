`timescale 1ns/1ps

`include "clover_pkg.sv"
`include "clover_cmd_proc.sv"
`include "clover_dma.sv"
`include "clover_scratchpad.sv"
`include "clover_axi_ram.sv"

module clover_cp_tb (
    input  logic         clk,
    input  logic         rst_n,

    input  logic         enable,
    input  logic         cp_abort,
    input  logic [31:0]  queue_base,
    input  logic [31:0]  queue_size,
    input  logic [31:0]  queue_tail,
    input  logic [31:0]  completion_base,
    input  logic [31:0]  completion_size,
    input  logic [31:0]  completion_tail,
    input  logic [31:0]  cycle_count,

    output logic [31:0]  queue_head,
    output logic [31:0]  completion_head,
    output logic         busy,
    output logic         irq,
    output logic         error,
    output logic [3:0]   error_code,

    input  logic         host_we,
    input  logic [15:0]  host_addr,
    input  logic [127:0] host_wdata,
    input  logic [15:0]  host_wstrb,
    output logic [127:0] host_rdata
);

    // CP AXI.
    logic [31:0]  cp_araddr;
    logic [7:0]   cp_arlen;
    logic [2:0]   cp_arsize;
    logic [1:0]   cp_arburst;
    logic [3:0]   cp_arid;
    logic         cp_arvalid;
    logic         cp_arready;
    logic [127:0] cp_rdata;
    logic         cp_rvalid;
    logic         cp_rlast;
    logic [3:0]   cp_rid;
    logic         cp_rready;
    logic [31:0]  cp_awaddr;
    logic [7:0]   cp_awlen;
    logic [2:0]   cp_awsize;
    logic [1:0]   cp_awburst;
    logic [3:0]   cp_awid;
    logic         cp_awvalid;
    logic         cp_awready;
    logic [127:0] cp_wdata;
    logic [15:0]  cp_wstrb;
    logic         cp_wlast;
    logic         cp_wvalid;
    logic         cp_wready;
    logic [1:0]   cp_bresp;
    logic         cp_bvalid;
    logic         cp_bready;
    logic         cp_axi_active;

    // DMA AXI.
    logic [31:0]  dma_araddr;
    logic [7:0]   dma_arlen;
    logic [2:0]   dma_arsize;
    logic [1:0]   dma_arburst;
    logic [3:0]   dma_arid;
    logic         dma_arvalid;
    logic         dma_arready;
    logic [127:0] dma_rdata;
    logic         dma_rvalid;
    logic         dma_rlast;
    logic [3:0]   dma_rid;
    logic         dma_rready;
    logic [31:0]  dma_awaddr;
    logic [7:0]   dma_awlen;
    logic [2:0]   dma_awsize;
    logic [1:0]   dma_awburst;
    logic [3:0]   dma_awid;
    logic         dma_awvalid;
    logic         dma_awready;
    logic [127:0] dma_wdata;
    logic [15:0]  dma_wstrb;
    logic         dma_wlast;
    logic         dma_wvalid;
    logic         dma_wready;
    logic [1:0]   dma_bresp;
    logic         dma_bvalid;
    logic         dma_bready;

    // DMA <-> CP.
    logic         dma_start;
    logic [1:0]   dma_dir;
    logic [31:0]  dma_src;
    logic [31:0]  dma_dst;
    logic [31:0]  dma_bytes;
    logic [31:0]  dma_rows;
    logic [31:0]  dma_src_stride;
    logic [31:0]  dma_dst_stride;
    logic         dma_busy;
    logic         dma_done;
    logic         dma_error;

    // AXI mux.
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

    assign m_araddr  = cp_axi_active ? cp_araddr : dma_araddr;
    assign m_arlen   = cp_axi_active ? cp_arlen : dma_arlen;
    assign m_arsize  = cp_axi_active ? cp_arsize : dma_arsize;
    assign m_arburst = cp_axi_active ? cp_arburst : dma_arburst;
    assign m_arid    = cp_axi_active ? cp_arid : dma_arid;
    assign m_arvalid = cp_axi_active ? cp_arvalid : dma_arvalid;
    assign cp_arready = cp_axi_active ? m_arready : 1'b0;
    assign dma_arready = cp_axi_active ? 1'b0 : m_arready;
    assign m_rready  = cp_axi_active ? cp_rready : dma_rready;
    assign cp_rdata  = m_rdata;
    assign cp_rvalid = m_rvalid && cp_axi_active;
    assign cp_rlast  = m_rlast;
    assign cp_rid    = m_rid;
    assign dma_rdata = m_rdata;
    assign dma_rvalid = m_rvalid && !cp_axi_active;
    assign dma_rlast = m_rlast;
    assign dma_rid   = m_rid;

    assign m_awaddr  = cp_axi_active ? cp_awaddr : dma_awaddr;
    assign m_awlen   = cp_axi_active ? cp_awlen : dma_awlen;
    assign m_awsize  = cp_axi_active ? cp_awsize : dma_awsize;
    assign m_awburst = cp_axi_active ? cp_awburst : dma_awburst;
    assign m_awid    = cp_axi_active ? cp_awid : dma_awid;
    assign m_awvalid = cp_axi_active ? cp_awvalid : dma_awvalid;
    assign cp_awready = cp_axi_active ? m_awready : 1'b0;
    assign dma_awready = cp_axi_active ? 1'b0 : m_awready;
    assign m_wdata   = cp_axi_active ? cp_wdata : dma_wdata;
    assign m_wstrb   = cp_axi_active ? cp_wstrb : dma_wstrb;
    assign m_wlast   = cp_axi_active ? cp_wlast : dma_wlast;
    assign m_wvalid  = cp_axi_active ? cp_wvalid : dma_wvalid;
    assign cp_wready = cp_axi_active ? m_wready : 1'b0;
    assign dma_wready = cp_axi_active ? 1'b0 : m_wready;
    assign m_bready  = cp_axi_active ? cp_bready : dma_bready;
    assign cp_bresp  = m_bresp;
    assign cp_bvalid = m_bvalid && cp_axi_active;
    assign dma_bresp = m_bresp;
    assign dma_bvalid = m_bvalid && !cp_axi_active;

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

    clover_cmd_proc u_cp (
        .clk              (clk),
        .rst_n            (rst_n),
        .enable           (enable),
        .abort            (cp_abort),
        .queue_base       (queue_base),
        .queue_size       (queue_size),
        .queue_tail       (queue_tail),
        .completion_base  (completion_base),
        .completion_size  (completion_size),
        .completion_tail  (completion_tail),
        .cycle_count      (cycle_count),
        .queue_head       (queue_head),
        .completion_head  (completion_head),
        .busy             (busy),
        .irq              (irq),
        .error            (error),
        .error_code       (error_code),
        .m_araddr         (cp_araddr),
        .m_arlen          (cp_arlen),
        .m_arsize         (cp_arsize),
        .m_arburst        (cp_arburst),
        .m_arid           (cp_arid),
        .m_arvalid        (cp_arvalid),
        .m_arready        (cp_arready),
        .m_rdata          (cp_rdata),
        .m_rvalid         (cp_rvalid),
        .m_rlast          (cp_rlast),
        .m_rid            (cp_rid),
        .m_rready         (cp_rready),
        .m_awaddr         (cp_awaddr),
        .m_awlen          (cp_awlen),
        .m_awsize         (cp_awsize),
        .m_awburst        (cp_awburst),
        .m_awid           (cp_awid),
        .m_awvalid        (cp_awvalid),
        .m_awready        (cp_awready),
        .m_wdata          (cp_wdata),
        .m_wstrb          (cp_wstrb),
        .m_wlast          (cp_wlast),
        .m_wvalid         (cp_wvalid),
        .m_wready         (cp_wready),
        .m_bresp          (cp_bresp),
        .m_bvalid         (cp_bvalid),
        .m_bready         (cp_bready),
        .cp_axi_active    (cp_axi_active),
        .dma_start        (dma_start),
        .dma_dir          (dma_dir),
        .dma_src          (dma_src),
        .dma_dst          (dma_dst),
        .dma_bytes        (dma_bytes),
        .dma_rows         (dma_rows),
        .dma_src_stride   (dma_src_stride),
        .dma_dst_stride   (dma_dst_stride),
        .dma_busy         (dma_busy),
        .dma_done         (dma_done),
        .dma_error        (dma_error)
    );

    clover_dma u_dma (
        .clk          (clk),
        .rst_n        (rst_n),
        .start        (dma_start),
        .dir          (dma_dir),
        .src_addr     (dma_src),
        .dst_addr     (dma_dst),
        .bytes_per_row(dma_bytes),
        .num_rows     (dma_rows),
        .src_stride   (dma_src_stride),
        .dst_stride   (dma_dst_stride),
        .busy         (dma_busy),
        .done         (dma_done),
        .error        (dma_error),
        .m_araddr     (dma_araddr),
        .m_arlen      (dma_arlen),
        .m_arsize     (dma_arsize),
        .m_arburst    (dma_arburst),
        .m_arid       (dma_arid),
        .m_arvalid    (dma_arvalid),
        .m_arready    (dma_arready),
        .m_rdata      (dma_rdata),
        .m_rvalid     (dma_rvalid),
        .m_rlast      (dma_rlast),
        .m_rid        (dma_rid),
        .m_rready     (dma_rready),
        .m_awaddr     (dma_awaddr),
        .m_awlen      (dma_awlen),
        .m_awsize     (dma_awsize),
        .m_awburst    (dma_awburst),
        .m_awid       (dma_awid),
        .m_awvalid    (dma_awvalid),
        .m_awready    (dma_awready),
        .m_wdata      (dma_wdata),
        .m_wstrb      (dma_wstrb),
        .m_wlast      (dma_wlast),
        .m_wvalid     (dma_wvalid),
        .m_wready     (dma_wready),
        .m_bresp      (dma_bresp),
        .m_bvalid     (dma_bvalid),
        .m_bready     (dma_bready),
        .sp_rd_req    (dma_sp_rd_req),
        .sp_rd_addr   (dma_sp_rd_addr),
        .sp_rd_data   (dma_sp_rd_data),
        .sp_wr_req    (dma_sp_wr_req),
        .sp_wr_addr   (dma_sp_wr_addr),
        .sp_wr_data   (dma_sp_wr_data),
        .sp_wr_be     (dma_sp_wr_be),
        .sp_wr_ready  (dma_sp_wr_ready)
    );

    assign sp_a_req  = 1'b0;
    assign sp_a_addr = 18'd0;
    assign sp_b_req  = dma_sp_rd_req;
    assign sp_b_addr = dma_sp_rd_addr;
    assign dma_sp_rd_data = sp_b_rdata;
    assign sp_w_req  = dma_sp_wr_req;
    assign sp_w_addr = dma_sp_wr_addr;
    assign sp_w_data = dma_sp_wr_data;
    assign sp_w_be   = dma_sp_wr_be;
    assign dma_sp_wr_ready = sp_w_ready;

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

endmodule
