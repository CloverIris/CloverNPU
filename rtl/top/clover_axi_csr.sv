`include "clover_pkg.sv"
`include "clover_axi4lite_slave.sv"
`include "clover_csr.sv"

module clover_axi_csr (
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

    input  logic [63:0] cycle_count,
    input  logic        status_busy,
    input  logic        irq_set,
    input  logic [31:0] queue_head_in,
    input  logic [31:0] completion_head_in,

    output logic [31:0] queue_base,
    output logic [31:0] queue_size,
    output logic [31:0] queue_tail,
    output logic [31:0] completion_base,
    output logic [31:0] completion_size,
    output logic [31:0] completion_tail,
    output logic        enable,
    output logic        irq,
    output logic        doorbell,
    output logic        abort_pulse,
    output logic        soft_reset_pulse
);

    logic        csr_we;
    logic [11:0] csr_waddr;
    logic [31:0] csr_wdata;
    logic [3:0]  csr_wstrb;
    logic [11:0] csr_raddr;
    logic [31:0] csr_rdata;

    clover_axi4lite_slave u_slave (
        .clk        (clk),
        .rst_n      (rst_n),
        .s_awaddr   (s_awaddr),
        .s_awvalid  (s_awvalid),
        .s_awready  (s_awready),
        .s_wdata    (s_wdata),
        .s_wstrb    (s_wstrb),
        .s_wvalid   (s_wvalid),
        .s_wready   (s_wready),
        .s_bresp    (s_bresp),
        .s_bvalid   (s_bvalid),
        .s_bready   (s_bready),
        .s_araddr   (s_araddr),
        .s_arvalid  (s_arvalid),
        .s_arready  (s_arready),
        .s_rdata    (s_rdata),
        .s_rresp    (s_rresp),
        .s_rvalid   (s_rvalid),
        .s_rready   (s_rready),
        .csr_we     (csr_we),
        .csr_waddr  (csr_waddr),
        .csr_wdata  (csr_wdata),
        .csr_wstrb  (csr_wstrb),
        .csr_raddr  (csr_raddr),
        .csr_rdata  (csr_rdata)
    );

    clover_csr u_csr (
        .clk              (clk),
        .rst_n            (rst_n),
        .csr_we           (csr_we),
        .csr_waddr        (csr_waddr),
        .csr_wdata        (csr_wdata),
        .csr_wstrb        (csr_wstrb),
        .csr_raddr        (csr_raddr),
        .csr_rdata        (csr_rdata),
        .cycle_count      (cycle_count),
        .status_busy      (status_busy),
        .irq_set          (irq_set),
        .queue_head_in    (queue_head_in),
        .completion_head_in(completion_head_in),
        .queue_base       (queue_base),
        .queue_size       (queue_size),
        .queue_tail       (queue_tail),
        .completion_base  (completion_base),
        .completion_size  (completion_size),
        .completion_tail  (completion_tail),
        .enable           (enable),
        .irq              (irq),
        .doorbell         (doorbell),
        .abort_pulse      (abort_pulse),
        .soft_reset_pulse (soft_reset_pulse)
    );

endmodule
