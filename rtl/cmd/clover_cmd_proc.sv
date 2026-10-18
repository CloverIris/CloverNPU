`include "clover_pkg.sv"

// Command processor: fetch 64 B commands from a DRAM ring, issue 2D DMA,
// write 16 B completion entries, raise IRQ.
module clover_cmd_proc (
    input  logic         clk,
    input  logic         rst_n,

    // CSR configuration.
    input  logic         enable,
    input  logic         abort,
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

    // AXI4 master (command fetch + completion write).
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

    output logic         cp_axi_active,

    // DMA control.
    output logic         dma_start,
    output logic [1:0]   dma_dir,
    output logic [31:0]  dma_src,
    output logic [31:0]  dma_dst,
    output logic [31:0]  dma_bytes,
    output logic [31:0]  dma_rows,
    output logic [31:0]  dma_src_stride,
    output logic [31:0]  dma_dst_stride,
    input  logic         dma_busy,
    input  logic         dma_done,
    input  logic         dma_error
);

    typedef enum logic [3:0] {
        ST_IDLE,
        ST_AR,
        ST_R,
        ST_DECODE,
        ST_DMA_START,
        ST_DMA_WAIT,
        ST_CMP_AW,
        ST_CMP_W,
        ST_CMP_B,
        ST_NEXT
    } state_t;

    state_t state;

    logic [31:0] queue_head_q;
    logic [31:0] completion_head_q;
    logic [31:0] cmd_addr_q;
    logic [1:0]  beat_q;
    logic [511:0] cmd_data_q;

    logic [31:0] seq_q;
    logic [31:0] status_q;
    logic [3:0]  error_code_q;
    logic [31:0]  cmp_addr_comb;
    logic [127:0] cmp_data_comb;

    logic [31:0] dma_src_q;
    logic [31:0] dma_dst_q;
    logic [31:0] dma_bytes_q;
    logic [31:0] dma_rows_q;
    logic [31:0] dma_src_stride_q;
    logic [31:0] dma_dst_stride_q;
    logic [1:0]  dma_dir_q;

    logic irq_q;

    assign queue_head = queue_head_q;
    assign completion_head = completion_head_q;
    assign busy = (state != ST_IDLE);
    assign irq = irq_q;
    assign error = (status_q == 32'd1);
    assign error_code = error_code_q;

    assign cp_axi_active = (state == ST_AR) || (state == ST_R) ||
                           (state == ST_CMP_AW) || (state == ST_CMP_W) ||
                           (state == ST_CMP_B);

    assign dma_start = (state == ST_DMA_START);
    assign dma_dir = dma_dir_q;
    assign dma_src = dma_src_q;
    assign dma_dst = dma_dst_q;
    assign dma_bytes = dma_bytes_q;
    assign dma_rows = dma_rows_q;
    assign dma_src_stride = dma_src_stride_q;
    assign dma_dst_stride = dma_dst_stride_q;

    assign m_araddr  = cmd_addr_q + {26'd0, beat_q, 4'b0};
    assign m_arlen   = 8'd0;
    assign m_arsize  = 3'd4;
    assign m_arburst = 2'b01;
    assign m_arid    = 4'd0;
    assign m_arvalid = (state == ST_AR);
    assign m_rready  = (state == ST_R);

    assign cmp_addr_comb = completion_base + (completion_head_q << 4);
    assign cmp_data_comb = {{28'd0, error_code_q}, cycle_count, status_q, seq_q};

    assign m_awaddr  = cmp_addr_comb;
    assign m_awlen   = 8'd0;
    assign m_awsize  = 3'd4;
    assign m_awburst = 2'b01;
    assign m_awid    = 4'd0;
    assign m_awvalid = (state == ST_CMP_AW);
    assign m_wdata   = cmp_data_comb;
    assign m_wstrb   = 16'hFFFF;
    assign m_wlast   = 1'b1;
    assign m_wvalid  = (state == ST_CMP_W);
    assign m_bready  = (state == ST_CMP_B);

    // Command fields (little-endian 64-byte command).
    logic [31:0] cmd_opcode;
    logic [31:0] cmd_flags;
    logic [31:0] cmd_src;
    logic [31:0] cmd_dst;
    logic [31:0] cmd_dim0;
    logic [31:0] cmd_dim1;
    logic [31:0] cmd_stride0;
    logic [31:0] cmd_stride1;
    logic [31:0] cmd_seq;

    assign cmd_opcode  = cmd_data_q[31:0];
    assign cmd_flags   = cmd_data_q[63:32];
    assign cmd_src     = cmd_data_q[95:64];
    assign cmd_dst     = cmd_data_q[127:96];
    assign cmd_dim0    = cmd_data_q[159:128];
    assign cmd_dim1    = cmd_data_q[191:160];
    assign cmd_stride0 = cmd_data_q[255:224];
    assign cmd_stride1 = cmd_data_q[287:256];
    assign cmd_seq     = cmd_data_q[383:352];

    function automatic logic [31:0] ring_next(input logic [31:0] value,
                                              input logic [31:0] size);
        if (size == 0) begin
            ring_next = 0;
        end else if (value + 32'd1 >= size) begin
            ring_next = 0;
        end else begin
            ring_next = value + 32'd1;
        end
    endfunction

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state             <= ST_IDLE;
            queue_head_q      <= 32'd0;
            completion_head_q <= 32'd0;
            cmd_addr_q        <= 32'd0;
            beat_q            <= 2'd0;
            cmd_data_q        <= 512'd0;
            seq_q             <= 32'd0;
            status_q          <= 32'd0;
            error_code_q      <= 4'd0;
            dma_src_q         <= 32'd0;
            dma_dst_q         <= 32'd0;
            dma_bytes_q       <= 32'd0;
            dma_rows_q        <= 32'd0;
            dma_src_stride_q  <= 32'd0;
            dma_dst_stride_q  <= 32'd0;
            dma_dir_q         <= 2'd0;
            irq_q             <= 1'b0;
        end else begin
            irq_q <= 1'b0;

            if (abort) begin
                state <= ST_IDLE;
            end else begin
                unique case (state)
                    ST_IDLE: begin
                        if (enable && (queue_size != 0) &&
                            (queue_head_q != queue_tail)) begin
                            cmd_addr_q <= queue_base + (queue_head_q << 6);
                            beat_q     <= 2'd0;
                            state      <= ST_AR;
                        end
                    end
                    ST_AR: begin
                        if (m_arready) state <= ST_R;
                    end
                    ST_R: begin
                        if (m_rvalid) begin
                            cmd_data_q[128*beat_q +: 128] <= m_rdata;
                            if (beat_q == 2'd3) begin
                                state <= ST_DECODE;
                            end else begin
                                beat_q <= beat_q + 2'd1;
                                state  <= ST_AR;
                            end
                        end
                    end
                    ST_DECODE: begin
                        seq_q        <= cmd_seq;
                        status_q     <= 32'd0;
                        error_code_q <= 4'd0;
                        unique case (cmd_opcode)
                            32'h01: state <= ST_NEXT; // NOP
                            32'h02: begin             // DMA_2D
                                dma_src_q        <= cmd_src;
                                dma_dst_q        <= cmd_dst;
                                dma_bytes_q      <= cmd_dim0;
                                dma_rows_q       <= cmd_dim1;
                                dma_src_stride_q <= cmd_stride0;
                                dma_dst_stride_q <= cmd_stride1;
                                dma_dir_q        <= cmd_flags[1:0];
                                state            <= ST_DMA_START;
                            end
                            32'h05, 32'h08: state <= ST_CMP_AW; // SIGNAL / END
                            default: begin
                                status_q     <= 32'd1;
                                error_code_q <= 4'd1;
                                state        <= ST_CMP_AW;
                            end
                        endcase
                    end
                    ST_DMA_START: begin
                        if (dma_busy) state <= ST_DMA_WAIT;
                    end
                    ST_DMA_WAIT: begin
                        if (dma_done) begin
                            if (dma_error) begin
                                status_q     <= 32'd1;
                                error_code_q <= 4'd3;
                                state        <= ST_CMP_AW;
                            end else begin
                                // Successful DMA/NOP produce no completion entry;
                                // SIGNAL/END (or an error) do.
                                state <= ST_NEXT;
                            end
                        end
                    end
                    ST_CMP_AW: begin
                        if (m_awready) state <= ST_CMP_W;
                    end
                    ST_CMP_W: begin
                        if (m_wready) state <= ST_CMP_B;
                    end
                    ST_CMP_B: begin
                        if (m_bvalid) begin
                            completion_head_q <= ring_next(completion_head_q, completion_size);
                            irq_q <= 1'b1;
                            state <= ST_NEXT;
                        end
                    end
                    ST_NEXT: begin
                        queue_head_q <= ring_next(queue_head_q, queue_size);
                        state <= ST_IDLE;
                    end
                    default: state <= ST_IDLE;
                endcase
            end
        end
    end

endmodule
