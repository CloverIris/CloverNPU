`include "clover_pkg.sv"
`include "clover_axi_csr.sv"
`include "clover_cmd_proc.sv"
`include "clover_dma.sv"
`include "clover_scratchpad.sv"
`include "clover_core.sv"

// M4a NPU top: AXI4-Lite CSR + command processor + 2D DMA + 5-stage core +
// scratchpad.  Kernel code/args are DMA'd into scratchpad before RUN.
module clover_npu_mem (
    input  logic         clk,
    input  logic         rst_n,

    // AXI4-Lite slave (CSR).
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

    // AXI4 master (DMA + command/completion).
    output logic [31:0]  m_araddr,
    output logic [7:0]   m_arlen,
    output logic [2:0]   m_arsize,
    output logic [1:0]   m_arburst,
    output logic [3:0]   m_arid,
    output logic         m_arvalid,
    input  logic         m_arready,
    input  logic [127:0] m_rdata,
    input  logic         m_rvalid,
    input  logic         m_rlast,
    input  logic [3:0]   m_rid,
    output logic         m_rready,
    output logic [31:0]  m_awaddr,
    output logic [7:0]   m_awlen,
    output logic [2:0]   m_awsize,
    output logic [1:0]   m_awburst,
    output logic [3:0]   m_awid,
    output logic         m_awvalid,
    input  logic         m_awready,
    output logic [127:0] m_wdata,
    output logic [15:0]  m_wstrb,
    output logic         m_wlast,
    output logic         m_wvalid,
    input  logic         m_wready,
    input  logic [1:0]   m_bresp,
    input  logic         m_bvalid,
    output logic         m_bready,

    input  logic [31:0]  cycle_count,
    output logic         busy,
    output logic         irq,
    output logic [31:0]  queue_head,
    output logic [31:0]  completion_head,
    output logic         core_active,
    output logic [31:0]  last_exit_code
);

    logic [31:0] queue_base;
    logic [31:0] queue_size;
    logic [31:0] queue_tail;
    logic [31:0] completion_base;
    logic [31:0] completion_size;
    logic [31:0] completion_tail;
    logic        csr_enable;
    logic        doorbell;
    logic        csr_abort;
    logic        csr_soft_reset;

    logic        cp_busy;
    logic        cp_irq;
    logic        cp_error;
    logic [3:0]  cp_error_code;

    logic [31:0] cp_araddr;
    logic [7:0]  cp_arlen;
    logic [2:0]  cp_arsize;
    logic [1:0]  cp_arburst;
    logic [3:0]  cp_arid;
    logic        cp_arvalid;
    logic        cp_arready;
    logic [127:0] cp_rdata;
    logic        cp_rvalid;
    logic        cp_rlast;
    logic [3:0]  cp_rid;
    logic        cp_rready;
    logic [31:0] cp_awaddr;
    logic [7:0]  cp_awlen;
    logic [2:0]  cp_awsize;
    logic [1:0]  cp_awburst;
    logic [3:0]  cp_awid;
    logic        cp_awvalid;
    logic        cp_awready;
    logic [127:0] cp_wdata;
    logic [15:0] cp_wstrb;
    logic        cp_wlast;
    logic        cp_wvalid;
    logic        cp_wready;
    logic [1:0]  cp_bresp;
    logic        cp_bvalid;
    logic        cp_bready;
    logic        cp_axi_active;

    logic [31:0] dma_araddr;
    logic [7:0]  dma_arlen;
    logic [2:0]  dma_arsize;
    logic [1:0]  dma_arburst;
    logic [3:0]  dma_arid;
    logic        dma_arvalid;
    logic        dma_arready;
    logic [127:0] dma_rdata;
    logic        dma_rvalid;
    logic        dma_rlast;
    logic [3:0]  dma_rid;
    logic        dma_rready;
    logic [31:0] dma_awaddr;
    logic [7:0]  dma_awlen;
    logic [2:0]  dma_awsize;
    logic [1:0]  dma_awburst;
    logic [3:0]  dma_awid;
    logic        dma_awvalid;
    logic        dma_awready;
    logic [127:0] dma_wdata;
    logic [15:0] dma_wstrb;
    logic        dma_wlast;
    logic        dma_wvalid;
    logic        dma_wready;
    logic [1:0]  dma_bresp;
    logic        dma_bvalid;
    logic        dma_bready;

    logic        dma_start;
    logic [1:0]  dma_dir;
    logic [31:0] dma_src;
    logic [31:0] dma_dst;
    logic [31:0] dma_bytes;
    logic [31:0] dma_rows;
    logic [31:0] dma_src_stride;
    logic [31:0] dma_dst_stride;
    logic        dma_busy;
    logic        dma_done;
    logic        dma_error;

    logic        dma_sp_rd_req;
    logic [17:0] dma_sp_rd_addr;
    logic [511:0] dma_sp_rd_data;
    logic        dma_sp_wr_req;
    logic [17:0] dma_sp_wr_addr;
    logic [511:0] dma_sp_wr_data;
    logic [63:0] dma_sp_wr_be;
    logic        dma_sp_wr_ready;

    logic        sp_rd_req;
    logic [17:0] sp_rd_addr;
    logic        sp_wr_req;
    logic [17:0] sp_wr_addr;
    logic [511:0] sp_wr_data;
    logic [63:0] sp_wr_be;
    logic        sp_wr_ready;

    // Core interfaces.
    logic        core_start;
    logic [31:0] core_entry;
    logic [31:0] core_sp;
    logic [31:0] core_arg;
    logic        core_halted;
    logic [31:0] core_exit_code;
    logic        c_core_active;

    logic        c_imem_req;
    logic [31:0] c_imem_addr;
    logic        c_imem_ready;
    logic [31:0] c_imem_rdata;
    logic        c_dmem_req;
    logic        c_dmem_we;
    logic [31:0] c_dmem_addr;
    logic [1:0]  c_dmem_size;
    logic [31:0] c_dmem_wdata;
    logic        c_dmem_ready;
    logic [31:0] c_dmem_rdata;

    logic [511:0] core_a_rdata;
    logic [511:0] core_b_rdata;
    logic [511:0] core_w_data;
    logic [63:0]  core_w_be;
    logic [17:0]  core_a_addr;
    logic [17:0]  core_b_addr;
    logic [17:0]  core_w_addr;
    logic [3:0]   c_imem_lane;
    logic [3:0]   c_dmem_lane;
    logic [1:0]   c_dmem_offset;
    logic [3:0]   c_lane_be;

    assign busy = cp_busy;
    assign core_active = core_halted ? 1'b0 : c_core_active;

    clover_axi_csr u_csr (
        .clk              (clk),
        .rst_n            (rst_n),
        .s_awaddr         (s_awaddr),
        .s_awvalid        (s_awvalid),
        .s_awready        (s_awready),
        .s_wdata          (s_wdata),
        .s_wstrb          (s_wstrb),
        .s_wvalid         (s_wvalid),
        .s_wready         (s_wready),
        .s_bresp          (s_bresp),
        .s_bvalid         (s_bvalid),
        .s_bready         (s_bready),
        .s_araddr         (s_araddr),
        .s_arvalid        (s_arvalid),
        .s_arready        (s_arready),
        .s_rdata          (s_rdata),
        .s_rresp          (s_rresp),
        .s_rvalid         (s_rvalid),
        .s_rready         (s_rready),
        .cycle_count      ({32'd0, cycle_count}),
        .status_busy      (cp_busy || dma_busy),
        .irq_set          (cp_irq),
        .queue_head_in    (queue_head),
        .completion_head_in(completion_head),
        .queue_base       (queue_base),
        .queue_size       (queue_size),
        .queue_tail       (queue_tail),
        .completion_base  (completion_base),
        .completion_size  (completion_size),
        .completion_tail  (completion_tail),
        .enable           (csr_enable),
        .irq              (irq),
        .doorbell         (doorbell),
        .abort_pulse      (csr_abort),
        .soft_reset_pulse (csr_soft_reset)
    );

    clover_cmd_proc u_cp (
        .clk              (clk),
        .rst_n            (rst_n),
        .enable           (csr_enable && !csr_soft_reset),
        .abort            (csr_abort || csr_soft_reset),
        .queue_base       (queue_base),
        .queue_size       (queue_size),
        .queue_tail       (queue_tail),
        .completion_base  (completion_base),
        .completion_size  (completion_size),
        .completion_tail  (completion_tail),
        .cycle_count      (cycle_count),
        .queue_head       (queue_head),
        .completion_head  (completion_head),
        .busy             (cp_busy),
        .irq              (cp_irq),
        .error            (cp_error),
        .error_code       (cp_error_code),
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
        .core_start       (core_start),
        .core_entry       (core_entry),
        .core_sp          (core_sp),
        .core_arg         (core_arg),
        .core_active      (c_core_active),
        .core_halted      (core_halted),
        .core_exit_code   (core_exit_code),
        .last_exit_code   (last_exit_code),
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
        .sp_wr_ready  (sp_wr_ready)
    );

    // AXI master mux: command processor has priority while it is active.
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

    // Core.
    assign c_imem_lane = c_imem_addr[5:2];
    assign core_a_addr = {c_imem_addr[17:6], 6'b0};
    assign c_imem_rdata = core_a_rdata[32*c_imem_lane +: 32];
    assign c_imem_ready = 1'b1;

    assign c_dmem_lane   = c_dmem_addr[5:2];
    assign c_dmem_offset = c_dmem_addr[1:0];
    assign c_dmem_rdata  = core_b_rdata[32*c_dmem_lane +: 32];
    assign c_dmem_ready  = 1'b1;
    assign core_b_addr   = {c_dmem_addr[17:6], 6'b0};
    assign core_w_addr   = {c_dmem_addr[17:6], 6'b0};

    always_comb begin
        unique case (c_dmem_size)
            2'd0: c_lane_be = 4'b0001 << c_dmem_offset;
            2'd1: c_lane_be = 4'b0011 << c_dmem_offset;
            default: c_lane_be = 4'b1111;
        endcase
    end

    always_comb begin
        core_w_data = 512'd0;
        core_w_data[32*c_dmem_lane +: 32] = c_dmem_wdata << (8*c_dmem_offset);
    end
    assign core_w_be = {60'd0, c_lane_be} << (4*c_dmem_lane);

    clover_core #(
        .RESET_PC(32'd0)
    ) u_core (
        .clk        (clk),
        .rst_n      (rst_n),
        .core_start (core_start),
        .core_entry (core_entry),
        .core_sp    (core_sp),
        .core_arg   (core_arg),
        .core_active(c_core_active),
        .imem_req   (c_imem_req),
        .imem_addr  (c_imem_addr),
        .imem_ready (c_imem_ready),
        .imem_rdata (c_imem_rdata),
        .dmem_req   (c_dmem_req),
        .dmem_we    (c_dmem_we),
        .dmem_addr  (c_dmem_addr),
        .dmem_size  (c_dmem_size),
        .dmem_wdata (c_dmem_wdata),
        .dmem_ready (c_dmem_ready),
        .dmem_rdata (c_dmem_rdata),
        .halted     (core_halted),
        .exit_code  (core_exit_code),
        .uart_data  (),
        .uart_valid (),
        .pc_out     (),
        .cycle_count(),
        .halt_reason()
    );

    // Scratchpad port mux: core owns the ports while active; DMA otherwise.
    assign sp_wr_req  = c_core_active ? (c_dmem_req && c_dmem_we) : dma_sp_wr_req;
    assign sp_wr_addr = c_core_active ? core_w_addr : dma_sp_wr_addr;
    assign sp_wr_data = c_core_active ? core_w_data : dma_sp_wr_data;
    assign sp_wr_be   = c_core_active ? core_w_be : dma_sp_wr_be;
    assign dma_sp_wr_ready = c_core_active ? 1'b0 : sp_wr_ready;

    assign sp_rd_req  = c_core_active ? (c_dmem_req && !c_dmem_we) : dma_sp_rd_req;
    assign sp_rd_addr = c_core_active ? core_b_addr : dma_sp_rd_addr;
    assign dma_sp_rd_data = core_b_rdata;

    clover_scratchpad u_sp (
        .clk     (clk),
        .a_req   (c_imem_req),
        .a_addr  (core_a_addr),
        .a_rdata (core_a_rdata),
        .b_req   (sp_rd_req),
        .b_addr  (sp_rd_addr),
        .b_rdata (core_b_rdata),
        .w_req   (sp_wr_req),
        .w_addr  (sp_wr_addr),
        .w_data  (sp_wr_data),
        .w_be    (sp_wr_be),
        .w_ready (sp_wr_ready)
    );

endmodule
