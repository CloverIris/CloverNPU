`timescale 1ns/1ps

`include "clover_pkg.sv"
`include "clover_npu_mem.sv"
`include "clover_axi_ram.sv"

module clover_npu_mem_tb (
    input  logic         clk,
    input  logic         rst_n,

    input  logic [31:0]  s_awaddr,
    input  logic         s_awvalid,
    output logic         s_awready,
    input  logic [31:0]  s_wdata,
    input  logic [3:0]   s_wstrb,
    input  logic         s_wvalid,
    output logic         s_wready,
    output logic [1:0]   s_bresp,
    output logic         s_bvalid,
    input  logic         s_bready,
    input  logic [31:0]  s_araddr,
    input  logic         s_arvalid,
    output logic         s_arready,
    output logic [31:0]  s_rdata,
    output logic [1:0]   s_rresp,
    output logic         s_rvalid,
    input  logic         s_rready,

    input  logic [31:0]  cycle_count,
    output logic         busy,
    output logic         irq,
    output logic [31:0]  queue_head,
    output logic [31:0]  completion_head,

    input  logic         host_we,
    input  logic [15:0]  host_addr,
    input  logic [127:0] host_wdata,
    input  logic [15:0]  host_wstrb,
    output logic [127:0] host_rdata
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

    clover_npu_mem u_npu (
        .clk             (clk),
        .rst_n           (rst_n),
        .s_awaddr        (s_awaddr),
        .s_awvalid       (s_awvalid),
        .s_awready       (s_awready),
        .s_wdata         (s_wdata),
        .s_wstrb         (s_wstrb),
        .s_wvalid        (s_wvalid),
        .s_wready        (s_wready),
        .s_bresp         (s_bresp),
        .s_bvalid        (s_bvalid),
        .s_bready        (s_bready),
        .s_araddr        (s_araddr),
        .s_arvalid       (s_arvalid),
        .s_arready       (s_arready),
        .s_rdata         (s_rdata),
        .s_rresp         (s_rresp),
        .s_rvalid        (s_rvalid),
        .s_rready        (s_rready),
        .m_araddr        (m_araddr),
        .m_arlen         (m_arlen),
        .m_arsize        (m_arsize),
        .m_arburst       (m_arburst),
        .m_arid          (m_arid),
        .m_arvalid       (m_arvalid),
        .m_arready       (m_arready),
        .m_rdata         (m_rdata),
        .m_rvalid        (m_rvalid),
        .m_rlast         (m_rlast),
        .m_rid           (m_rid),
        .m_rready        (m_rready),
        .m_awaddr        (m_awaddr),
        .m_awlen         (m_awlen),
        .m_awsize        (m_awsize),
        .m_awburst       (m_awburst),
        .m_awid          (m_awid),
        .m_awvalid       (m_awvalid),
        .m_awready       (m_awready),
        .m_wdata         (m_wdata),
        .m_wstrb         (m_wstrb),
        .m_wlast         (m_wlast),
        .m_wvalid        (m_wvalid),
        .m_wready        (m_wready),
        .m_bresp         (m_bresp),
        .m_bvalid        (m_bvalid),
        .m_bready        (m_bready),
        .cycle_count     (cycle_count),
        .busy            (busy),
        .irq             (irq),
        .queue_head      (queue_head),
        .completion_head (completion_head)
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
