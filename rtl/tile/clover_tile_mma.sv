`include "clover_pkg.sv"
`include "clover_fp_pkg.sv"

// Serialized 16x16x16 MMA engine.  One FP32 MAC per cycle; reads A/B from the
// scratchpad read ports and writes one FP32 C element per cycle.
module clover_tile_mma (
    input  logic         clk,
    input  logic         rst_n,

    input  logic         start,
    input  logic [31:0]  desc_addr,
    output logic         busy,
    output logic         done,
    output logic         error,
    output logic [3:0]   error_code,

    // Scratchpad read port A (descriptor and A elements).
    output logic         a_req,
    output logic [17:0]  a_addr,
    input  logic [511:0] a_rdata,

    // Scratchpad read port B (B elements / C accumulate).
    output logic         b_req,
    output logic [17:0]  b_addr,
    input  logic [511:0] b_rdata,

    // Scratchpad write port (C).
    output logic         w_req,
    output logic [17:0]  w_addr,
    output logic [511:0] w_data,
    output logic [63:0]  w_be,
    input  logic         w_ready
);

    localparam logic [2:0] ST_IDLE   = 3'd0;
    localparam logic [2:0] ST_FETCH  = 3'd1;
    localparam logic [2:0] ST_CHECK  = 3'd2;
    localparam logic [2:0] ST_SCALE  = 3'd3;
    localparam logic [2:0] ST_LOAD_C = 3'd4;
    localparam logic [2:0] ST_MMA    = 3'd5;
    localparam logic [2:0] ST_WRITE  = 3'd6;
    localparam logic [2:0] ST_CH_SCALE = 3'd7;

    logic [2:0]  state;
    logic [31:0] d[0:15];
    logic [3:0]  fetch_index;
    logic [31:0] desc_addr_q;
    logic [31:0] desc_line;
    logic [31:0] desc_word;

    logic [4:0]  m, n, k;
    logic [4:0]  mi, ni, ki;
    logic [31:0] a_base, b_base, c_base;
    logic [31:0] a_stride, b_stride, c_stride;
    logic        accumulate, transpose_a, transpose_b, relu;
    logic        a_scale_en, b_scale_en, a_per_channel, b_per_channel;
    logic        post_scale_en, post_bias_en;
    logic [2:0]  a_type, b_type, c_type;
    logic [31:0] flags;
    logic [31:0] a_scale_q, b_scale_q, post_value;
    logic [31:0] a_ch_addr, b_ch_addr;

    logic [31:0] a_element_addr, b_element_addr, c_element_addr;
    logic [31:0] a_word, b_word, c_word;
    logic [3:0]  a_lane, b_lane, c_lane;
    logic [1:0]  a_byte_off, b_byte_off, c_offset;
    logic [2:0]  a_size, b_size;
    logic [31:0] a_raw, b_raw;
    logic [31:0] a_f32, b_f32, prod, acc, acc_next;
    logic [31:0] c_write;

    logic        error_q;
    logic [3:0]  error_code_q;
    logic        done_q;
    logic        busy_q;

    assign busy  = busy_q;
    assign done  = done_q;
    assign error = error_q;
    assign error_code = error_code_q;

    // ---------------------------------------------------------------- helpers
    function automatic [31:0] read_word_a(input logic [17:0] address);
        begin
            read_word_a = a_rdata[32 * address[5:2] +: 32];
        end
    endfunction

    function automatic [31:0] read_word_b(input logic [17:0] address);
        begin
            read_word_b = b_rdata[32 * address[5:2] +: 32];
        end
    endfunction

    always_comb begin
        a_lane = a_element_addr[5:2];
        b_lane = b_element_addr[5:2];
        c_lane = c_element_addr[5:2];
        a_byte_off = a_element_addr[1:0];
        b_byte_off = b_element_addr[1:0];
        c_offset = c_element_addr[1:0];

        a_word = a_rdata[32 * a_lane +: 32];
        b_word = b_rdata[32 * b_lane +: 32];
        c_word = b_rdata[32 * c_lane +: 32];
        a_raw = (a_size == 3'd4) ? a_word :
                (a_size == 3'd2) ? (a_byte_off[1] ? a_word[31:16] : a_word[15:0]) :
                                   {24'd0, a_word[8 * a_byte_off +: 8]};
        b_raw = (b_size == 3'd4) ? b_word :
                (b_size == 3'd2) ? (b_byte_off[1] ? b_word[31:16] : b_word[15:0]) :
                                   {24'd0, b_word[8 * b_byte_off +: 8]};

        case (a_type)
        3'd0: a_f32 = a_raw;
        3'd1: a_f32 = clover_fp16_to_fp32(a_raw[15:0]);
        3'd2: a_f32 = clover_bf16_to_fp32(a_raw[15:0]);
        3'd3: a_f32 = clover_fp8_e4m3_to_fp32(a_raw[7:0]);
        3'd4: a_f32 = clover_fp8_e5m2_to_fp32(a_raw[7:0]);
        3'd5: a_f32 = clover_fp4_to_fp32(a_raw[3:0]);
        default: a_f32 = 32'd0;
        endcase
        case (b_type)
        3'd0: b_f32 = b_raw;
        3'd1: b_f32 = clover_fp16_to_fp32(b_raw[15:0]);
        3'd2: b_f32 = clover_bf16_to_fp32(b_raw[15:0]);
        3'd3: b_f32 = clover_fp8_e4m3_to_fp32(b_raw[7:0]);
        3'd4: b_f32 = clover_fp8_e5m2_to_fp32(b_raw[7:0]);
        3'd5: b_f32 = clover_fp4_to_fp32(b_raw[3:0]);
        default: b_f32 = 32'd0;
        endcase
        prod = clover_fp32_mul(clover_fp32_mul(a_f32, a_scale_q),
                               clover_fp32_mul(b_f32, b_scale_q));
        acc_next = clover_fp32_add(acc, prod);

        post_value = acc;
        if (post_scale_en)
            post_value = clover_fp32_mul(post_value, d[13]);
        if (post_bias_en)
            post_value = clover_fp32_add(post_value, d[14]);
        if (relu && post_value[31])
            c_write = 32'd0;
        else
            c_write = post_value;
    end

    always_comb begin
        // A element address.
        a_element_addr = a_base;
        if (transpose_a)
            a_element_addr = a_base + ki * a_stride + mi * a_size;
        else
            a_element_addr = a_base + mi * a_stride + ki * a_size;

        // B element address.
        b_element_addr = b_base;
        if (transpose_b)
            b_element_addr = b_base + ni * b_stride + ki * b_size;
        else
            b_element_addr = b_base + ki * b_stride + ni * b_size;

        // C element address (FP32).
        c_element_addr = c_base + mi * c_stride + ni * 4;
        a_ch_addr = d[11] + mi * 4;
        b_ch_addr = d[12] + ni * 4;
    end

    always_comb begin
        a_req  = 1'b0;
        b_req  = 1'b0;
        w_req  = 1'b0;
        a_addr = 18'd0;
        b_addr = 18'd0;
        w_addr = 18'd0;
        w_data = 512'd0;
        w_be   = 64'd0;

        case (state)
        ST_FETCH: begin
            a_req  = 1'b1;
            a_addr = {desc_addr_q[17:6], 6'b0};
        end
        ST_SCALE: begin
            if (a_scale_en) begin
                a_req  = 1'b1;
                a_addr = {d[11][17:6], 6'b0};
            end
            if (b_scale_en) begin
                b_req  = 1'b1;
                b_addr = {d[12][17:6], 6'b0};
            end
        end
        ST_LOAD_C: begin
            if (accumulate) begin
                b_req  = 1'b1;
                b_addr = {c_element_addr[17:6], 6'b0};
            end
        end
        ST_CH_SCALE: begin
            if (a_per_channel) begin
                a_req  = 1'b1;
                a_addr = {a_ch_addr[17:6], 6'b0};
            end
            if (b_per_channel) begin
                b_req  = 1'b1;
                b_addr = {b_ch_addr[17:6], 6'b0};
            end
        end
        ST_MMA: begin
            a_req  = 1'b1;
            a_addr = {a_element_addr[17:6], 6'b0};
            b_req  = 1'b1;
            b_addr = {b_element_addr[17:6], 6'b0};
        end
        ST_WRITE: begin
            w_req  = 1'b1;
            w_addr = {c_element_addr[17:6], 6'b0};
            w_data = {480'd0, c_write} << (32 * c_lane);
            w_be   = (64'h0000_0000_0000_000F << (4 * c_lane)) << (4 * c_offset);
        end
        default: begin
        end
        endcase
    end

    // ------------------------------------------------------------- sequential
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state        <= ST_IDLE;
            busy_q       <= 1'b0;
            done_q       <= 1'b0;
            error_q      <= 1'b0;
            error_code_q <= 4'd0;
            fetch_index  <= 4'd0;
            desc_addr_q  <= 32'd0;
            a_scale_q    <= 32'h3F80_0000;
            b_scale_q    <= 32'h3F80_0000;
            mi <= 5'd0;
            ni <= 5'd0;
            ki <= 5'd0;
            acc <= 32'd0;
            for (int i = 0; i < 16; i++) d[i] <= 32'd0;
        end else begin
            done_q <= 1'b0;

            case (state)
            ST_IDLE: begin
                if (start) begin
                    busy_q       <= 1'b1;
                    error_q      <= 1'b0;
                    error_code_q <= 4'd0;
                    fetch_index  <= 4'd0;
                    desc_addr_q  <= desc_addr;
                    state        <= ST_FETCH;
                end
            end

            ST_FETCH: begin
                d[fetch_index] <= a_rdata[32 * fetch_index[3:0] +: 32];
                if (fetch_index == 4'd15)
                    state <= ST_CHECK;
                else
                    fetch_index <= fetch_index + 4'd1;
            end

            ST_CHECK: begin
                flags      = d[1];
                m          = d[2][4:0];
                n          = d[3][4:0];
                k          = d[4][4:0];
                a_base     = d[5];
                b_base     = d[6];
                c_base     = d[7];
                a_stride   = d[8];
                b_stride   = d[9];
                c_stride   = d[10];
                accumulate = flags[0];
                transpose_a= flags[1];
                transpose_b= flags[2];
                relu       = flags[3];
                a_scale_en = flags[14];
                b_scale_en = flags[15];
                a_per_channel = flags[16];
                b_per_channel = flags[17];
                post_scale_en = flags[18];
                post_bias_en  = flags[19];
                a_type     = flags[7:5];
                b_type     = flags[10:8];
                c_type     = flags[13:11];
                a_size     = clover_dtype_size(a_type);
                b_size     = clover_dtype_size(b_type);

                if (d[0] != 32'h3141_4D4D) begin
                    error_q      <= 1'b1;
                    error_code_q <= 4'd1;
                    busy_q       <= 1'b0;
                    done_q       <= 1'b1;
                    state        <= ST_IDLE;
                end else if (m == 0 || m > 16 || n == 0 || n > 16 ||
                             k == 0 || k > 16) begin
                    error_q      <= 1'b1;
                    error_code_q <= 4'd2;
                    busy_q       <= 1'b0;
                    done_q       <= 1'b1;
                    state        <= ST_IDLE;
                end else if ((a_type > 3'd5) || (b_type > 3'd5) ||
                             (c_type != 3'd0) || (a_size == 0) || (b_size == 0) ||
                             (a_per_channel && !a_scale_en) ||
                             (b_per_channel && !b_scale_en)) begin
                    error_q      <= 1'b1;
                    error_code_q <= 4'd3;
                    busy_q       <= 1'b0;
                    done_q       <= 1'b1;
                    state        <= ST_IDLE;
                end else begin
                    mi <= 5'd0;
                    ni <= 5'd0;
                    ki <= 5'd0;
                    state <= (a_per_channel || b_per_channel) ? ST_LOAD_C :
                             ((a_scale_en || b_scale_en) ? ST_SCALE : ST_LOAD_C);
                end
            end

            ST_SCALE: begin
                a_scale_q <= a_scale_en ?
                    a_rdata[32 * d[11][5:2] +: 32] : 32'h3F80_0000;
                b_scale_q <= b_scale_en ?
                    b_rdata[32 * d[12][5:2] +: 32] : 32'h3F80_0000;
                state <= ST_LOAD_C;
            end

            ST_LOAD_C: begin
                if (accumulate)
                    acc <= c_word;
                else
                    acc <= 32'd0;
                state <= (a_per_channel || b_per_channel) ? ST_CH_SCALE : ST_MMA;
            end

            ST_CH_SCALE: begin
                if (a_per_channel)
                    a_scale_q <= a_rdata[32 * a_ch_addr[5:2] +: 32];
                if (b_per_channel)
                    b_scale_q <= b_rdata[32 * b_ch_addr[5:2] +: 32];
                state <= ST_MMA;
            end

            ST_MMA: begin
                acc <= acc_next;
                if (ki + 5'd1 == k) begin
                    ki    <= 5'd0;
                    state <= ST_WRITE;
                end else begin
                    ki <= ki + 5'd1;
                end
            end

            ST_WRITE: begin
                if (w_ready) begin
                    if (ni + 5'd1 == n) begin
                        ni <= 5'd0;
                        if (mi + 5'd1 == m) begin
                            busy_q <= 1'b0;
                            done_q <= 1'b1;
                            state  <= ST_IDLE;
                        end else begin
                            mi <= mi + 5'd1;
                            state <= ST_LOAD_C;
                        end
                    end else begin
                        ni <= ni + 5'd1;
                        state <= ST_LOAD_C;
                    end
                end
            end

            default: state <= ST_IDLE;
            endcase
        end
    end

endmodule
