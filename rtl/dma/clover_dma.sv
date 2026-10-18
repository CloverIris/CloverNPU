`include "clover_pkg.sv"

// 2D DMA between external AXI memory and scratchpad.
// M3b uses single-beat 16-byte AXI transactions.  Source/destination and
// strides must be 16-byte aligned; bytes_per_row must be a multiple of 16.
module clover_dma (
    input  logic         clk,
    input  logic         rst_n,

    input  logic         start,
    input  logic [1:0]   dir,          // 0: DRAM->SP, 1: SP->DRAM
    input  logic [31:0]  src_addr,
    input  logic [31:0]  dst_addr,
    input  logic [31:0]  bytes_per_row,
    input  logic [31:0]  num_rows,
    input  logic [31:0]  src_stride,
    input  logic [31:0]  dst_stride,

    output logic         busy,
    output logic         done,
    output logic         error,

    // AXI4 master, 128-bit data.
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

    // Scratchpad.
    output logic         sp_rd_req,
    output logic [17:0]  sp_rd_addr,
    input  logic [511:0] sp_rd_data,
    output logic         sp_wr_req,
    output logic [17:0]  sp_wr_addr,
    output logic [511:0] sp_wr_data,
    output logic [63:0]  sp_wr_be,
    input  logic         sp_wr_ready
);

    typedef enum logic [3:0] {
        ST_IDLE,
        ST_AR,
        ST_R,
        ST_SPW,
        ST_SPR,
        ST_AW,
        ST_W,
        ST_B,
        ST_NEXT,
        ST_DONE
    } state_t;

    state_t state;

    logic [31:0] src_row;
    logic [31:0] dst_row;
    logic [31:0] off;
    logic [31:0] row;
    logic [31:0] n_rows;
    logic [31:0] row_bytes;
    logic [31:0] src_step;
    logic [31:0] dst_step;
    logic        dir_q;

    logic [127:0] rdata_q;
    logic [511:0] line_q;
    logic         error_q;

    logic [31:0] cur_src;
    logic [31:0] cur_dst;
    logic [5:0]  byte_off;
    logic [3:0]  word_lane;

    assign cur_src  = src_row + off;
    assign cur_dst  = dst_row + off;
    assign byte_off = cur_dst[5:0];
    assign word_lane = cur_dst[5:2];

    assign busy  = (state != ST_IDLE) && (state != ST_DONE);
    assign done  = (state == ST_DONE);
    assign error = error_q;

    assign m_araddr  = cur_src;
    assign m_arlen   = 8'd0;
    assign m_arsize  = 3'd4; // 16 bytes
    assign m_arburst = 2'b01;
    assign m_arid    = 4'd0;
    assign m_arvalid = (state == ST_AR);

    assign m_rready = (state == ST_R);

    assign m_awaddr  = cur_dst;
    assign m_awlen   = 8'd0;
    assign m_awsize  = 3'd4;
    assign m_awburst = 2'b01;
    assign m_awid    = 4'd0;
    assign m_awvalid = (state == ST_AW);

    assign m_wdata  = line_q[8*byte_off +: 128];
    assign m_wstrb  = 16'hFFFF;
    assign m_wlast  = 1'b1;
    assign m_wvalid = (state == ST_W);

    assign m_bready = (state == ST_B);

    assign sp_rd_req  = (state == ST_SPR);
    assign sp_rd_addr = {cur_src[17:6], 6'b0};

    assign sp_wr_req  = (state == ST_SPW);
    assign sp_wr_addr = {cur_dst[17:6], 6'b0};
    assign sp_wr_data = {384'd0, rdata_q} << (8*byte_off);
    assign sp_wr_be   = 64'h0000_0000_0000_FFFF << (4*word_lane);

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state    <= ST_IDLE;
            src_row  <= 32'd0;
            dst_row  <= 32'd0;
            off      <= 32'd0;
            row      <= 32'd0;
            n_rows   <= 32'd0;
            row_bytes<= 32'd0;
            src_step <= 32'd0;
            dst_step <= 32'd0;
            dir_q    <= 1'b0;
            rdata_q  <= 128'd0;
            line_q   <= 512'd0;
            error_q  <= 1'b0;
        end else begin
            unique case (state)
                ST_IDLE: begin
                    if (start) begin
                        n_rows    <= num_rows;
                        row_bytes <= bytes_per_row;
                        src_step  <= src_stride;
                        dst_step  <= dst_stride;
                        dir_q     <= (dir == 2'd1);
                        src_row   <= src_addr;
                        dst_row   <= dst_addr;
                        off       <= 32'd0;
                        row       <= 32'd0;
                        error_q   <= 1'b0;
                        if ((num_rows == 0) || (bytes_per_row == 0) ||
                            (bytes_per_row[3:0] != 4'd0) ||
                            (src_addr[3:0] != 4'd0) ||
                            (dst_addr[3:0] != 4'd0) ||
                            (src_stride[3:0] != 4'd0) ||
                            (dst_stride[3:0] != 4'd0) ||
                            (dir > 2'd1)) begin
                            error_q <= 1'b1;
                            state   <= ST_DONE;
                        end else begin
                            state <= (dir == 2'd1) ? ST_SPR : ST_AR;
                        end
                    end
                end
                ST_AR: begin
                    if (m_arready) state <= ST_R;
                end
                ST_R: begin
                    if (m_rvalid) begin
                        rdata_q <= m_rdata;
                        state   <= ST_SPW;
                    end
                end
                ST_SPW: begin
                    if (sp_wr_ready) state <= ST_NEXT;
                end
                ST_SPR: begin
                    line_q <= sp_rd_data;
                    state  <= ST_AW;
                end
                ST_AW: begin
                    if (m_awready) state <= ST_W;
                end
                ST_W: begin
                    if (m_wready) state <= ST_B;
                end
                ST_B: begin
                    if (m_bvalid) state <= ST_NEXT;
                end
                ST_NEXT: begin
                    if (off + 32'd16 >= row_bytes) begin
                        off     <= 32'd0;
                        src_row <= src_row + src_step;
                        dst_row <= dst_row + dst_step;
                        if (row + 32'd1 >= n_rows) begin
                            state <= ST_DONE;
                        end else begin
                            row   <= row + 32'd1;
                            state <= dir_q ? ST_SPR : ST_AR;
                        end
                    end else begin
                        off   <= off + 32'd16;
                        state <= dir_q ? ST_SPR : ST_AR;
                    end
                end
                ST_DONE: begin
                    // `done` stays high until the next start.  Start is held
                    // by the caller until `busy` rises, so DONE->IDLE->latch
                    // works across the one-cycle gap.
                    if (start) state <= ST_IDLE;
                end
                default: state <= ST_IDLE;
            endcase
        end
    end

endmodule
