`include "clover_pkg.sv"
`include "clover_fp_pkg.sv"

// Serialized vector engine for FP32 VEC1 descriptors.  One element/cycle.
module clover_tile_vec (
    input  logic         clk,
    input  logic         rst_n,

    input  logic         start,
    input  logic [31:0]  desc_addr,
    output logic         busy,
    output logic         done,
    output logic         error,
    output logic [3:0]   error_code,

    output logic         a_req,
    output logic [17:0]  a_addr,
    input  logic [511:0] a_rdata,

    output logic         b_req,
    output logic [17:0]  b_addr,
    input  logic [511:0] b_rdata,

    output logic         w_req,
    output logic [17:0]  w_addr,
    output logic [511:0] w_data,
    output logic [63:0]  w_be,
    input  logic         w_ready
);

    localparam logic [2:0] ST_IDLE  = 3'd0;
    localparam logic [2:0] ST_FETCH = 3'd1;
    localparam logic [2:0] ST_CHECK = 3'd2;
    localparam logic [2:0] ST_LOOP  = 3'd3;

    logic [2:0]  state;
    logic [31:0] d[0:15];
    logic [3:0]  fetch_index;
    logic [31:0] desc_addr_q;
    logic [31:0] i;
    logic [31:0] count;
    logic [31:0] a_base, b_base, c_base;
    logic [31:0] a_stride, b_stride, c_stride;
    logic [3:0]  op;
    logic        relu;
    logic [2:0]  src_type, dst_type;
    logic        busy_q, done_q, error_q;
    logic [3:0]  error_code_q;
    logic [31:0] a_element_addr, b_element_addr, c_element_addr;
    logic [3:0]  a_lane, b_lane, c_lane;
    logic [31:0] a_f32, b_f32, result;

    assign busy = busy_q;
    assign done = done_q;
    assign error = error_q;
    assign error_code = error_code_q;

    always_comb begin
        a_element_addr = a_base + i * a_stride;
        b_element_addr = b_base + i * b_stride;
        c_element_addr = c_base + i * c_stride;
        a_lane = a_element_addr[5:2];
        b_lane = b_element_addr[5:2];
        c_lane = c_element_addr[5:2];
        a_f32 = a_rdata[32 * a_lane +: 32];
        b_f32 = b_rdata[32 * b_lane +: 32];

        result = a_f32;
        case (op)
        4'd0: result = clover_fp32_add(a_f32, b_f32);
        4'd1: result = clover_fp32_add(a_f32, {~b_f32[31], b_f32[30:0]});
        4'd2: result = clover_fp32_mul(a_f32, b_f32);
        4'd3: result = (a_f32[31] == 0 && b_f32[31] == 1) ? a_f32 :
                       (a_f32[31] == 1 && b_f32[31] == 0) ? b_f32 :
                       (a_f32 > b_f32) ? a_f32 : b_f32;
        4'd4: result = (a_f32[31] == 0 && b_f32[31] == 1) ? b_f32 :
                       (a_f32[31] == 1 && b_f32[31] == 0) ? a_f32 :
                       (a_f32 < b_f32) ? a_f32 : b_f32;
        4'd5: result = a_f32[31] ? 32'd0 : a_f32;
        4'd6: result = {1'b0, a_f32[30:0]};
        4'd7: result = {~a_f32[31], a_f32[30:0]};
        4'd8: result = clover_fp32_add(clover_fp32_mul(a_f32, b_f32),
                                       clover_fp32_add(a_f32, 32'd0));
        4'd9: result = a_f32;
        default: result = a_f32;
        endcase
        if (relu && result[31]) result = 32'd0;
    end

    always_comb begin
        a_req = 1'b0;
        b_req = 1'b0;
        w_req = 1'b0;
        a_addr = 18'd0;
        b_addr = 18'd0;
        w_addr = 18'd0;
        w_data = 512'd0;
        w_be = 64'd0;
        case (state)
        ST_FETCH: begin
            a_req = 1'b1;
            a_addr = {desc_addr_q[17:6], 6'b0};
        end
        ST_LOOP: begin
            a_req = 1'b1;
            a_addr = {a_element_addr[17:6], 6'b0};
            if (op != 4'd5 && op != 4'd6 && op != 4'd7 && op != 4'd9) begin
                b_req = 1'b1;
                b_addr = {b_element_addr[17:6], 6'b0};
            end
            w_req = 1'b1;
            w_addr = {c_element_addr[17:6], 6'b0};
            w_data = {480'd0, result} << (32 * c_lane);
            w_be = {60'd0, 4'hF} << (4 * c_lane);
        end
        default: begin
        end
        endcase
    end

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state        <= ST_IDLE;
            busy_q       <= 1'b0;
            done_q       <= 1'b0;
            error_q      <= 1'b0;
            error_code_q <= 4'd0;
            fetch_index  <= 4'd0;
            desc_addr_q  <= 32'd0;
            i            <= 32'd0;
            count        <= 32'd0;
            for (int n = 0; n < 16; n++) d[n] <= 32'd0;
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
                d[fetch_index] <= a_rdata[32 * fetch_index +: 32];
                if (fetch_index == 4'd15)
                    state <= ST_CHECK;
                else
                    fetch_index <= fetch_index + 4'd1;
            end
            ST_CHECK: begin
                count      = d[2];
                a_base     = d[3];
                b_base     = d[4];
                c_base     = d[5];
                a_stride   = d[6];
                b_stride   = d[7];
                c_stride   = d[8];
                src_type   = d[9][2:0];
                dst_type   = d[10][2:0];
                op         = d[1][3:0];
                relu       = d[1][5];
                if (d[0] != 32'h3143_4556) begin
                    error_q      <= 1'b1;
                    error_code_q <= 4'd1;
                    busy_q       <= 1'b0;
                    done_q       <= 1'b1;
                    state        <= ST_IDLE;
                end else if (count == 0) begin
                    error_q      <= 1'b1;
                    error_code_q <= 4'd2;
                    busy_q       <= 1'b0;
                    done_q       <= 1'b1;
                    state        <= ST_IDLE;
                end else if (src_type != 3'd0 || dst_type != 3'd0) begin
                    error_q      <= 1'b1;
                    error_code_q <= 4'd3;
                    busy_q       <= 1'b0;
                    done_q       <= 1'b1;
                    state        <= ST_IDLE;
                end else begin
                    i     <= 32'd0;
                    state <= ST_LOOP;
                end
            end
            ST_LOOP: begin
                if (w_ready) begin
                    if (i + 32'd1 == count) begin
                        busy_q <= 1'b0;
                        done_q <= 1'b1;
                        state  <= ST_IDLE;
                    end else begin
                        i <= i + 32'd1;
                    end
                end
            end
            default: state <= ST_IDLE;
            endcase
        end
    end

endmodule
