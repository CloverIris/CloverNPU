`ifndef CLOVER_FP_PKG_SV
`define CLOVER_FP_PKG_SV

// Synthesizable FP32 add/mul helpers.  v0.1 treats subnormal inputs as zero
// and flushes subnormal results to zero; FP16/BF16 kernel operands are normal
// after conversion.  Random-normal tests cover the rounding path.

function automatic [31:0] clover_fp32_pack(input logic sign,
                                           input integer exp_i,
                                           input logic [23:0] mant);
    begin
        if (exp_i >= 255)
            clover_fp32_pack = {sign, 8'hFF, 23'd0};
        else if (exp_i <= 0)
            clover_fp32_pack = {sign, 31'd0};
        else
            clover_fp32_pack = {sign, exp_i[7:0], mant[22:0]};
    end
endfunction

function automatic [31:0] clover_fp32_add(input logic [31:0] a,
                                          input logic [31:0] b);
    logic sa, sb, sr;
    logic [7:0] ea, eb;
    logic [22:0] fa, fb;
    logic [23:0] ma, mb, mant24, mant_tmp;
    logic [26:0] ma_ext, mb_ext, mb_shift, diffm;
    logic [27:0] sum;
    logic zero_a, zero_b, inf_a, inf_b, nan_a, nan_b;
    logic guard, rnd, sticky, lost;
    integer diff, exp_i, i;
    logic [7:0] exp_res;
    begin
        sa = a[31]; ea = a[30:23]; fa = a[22:0];
        sb = b[31]; eb = b[30:23]; fb = b[22:0];
        zero_a = (ea == 8'd0) && (fa == 23'd0);
        zero_b = (eb == 8'd0) && (fb == 23'd0);
        inf_a  = (ea == 8'hFF) && (fa == 23'd0);
        inf_b  = (eb == 8'hFF) && (fb == 23'd0);
        nan_a  = (ea == 8'hFF) && (fa != 23'd0);
        nan_b  = (eb == 8'hFF) && (fb != 23'd0);

        if (nan_a || nan_b) begin
            clover_fp32_add = 32'h7FC0_0000;
        end else if (inf_a && inf_b) begin
            if (sa == sb) clover_fp32_add = {sa, 8'hFF, 23'd0};
            else          clover_fp32_add = 32'h7FC0_0000;
        end else if (inf_a) begin
            clover_fp32_add = a;
        end else if (inf_b) begin
            clover_fp32_add = b;
        end else if (zero_a && zero_b) begin
            clover_fp32_add = {sa & sb, 31'd0};
        end else if (zero_a) begin
            clover_fp32_add = b;
        end else if (zero_b) begin
            clover_fp32_add = a;
        end else if ((ea == 8'd0) || (eb == 8'd0)) begin
            // Subnormal flush (documented v0.1 limitation).
            clover_fp32_add = (ea == 8'd0) ? b : a;
        end else begin
            ma = {1'b1, fa};
            mb = {1'b1, fb};
            exp_i = ea;
            if ((eb > ea) || ((eb == ea) && (mb > ma))) begin
                // Swap so a has larger magnitude.
                {sa, sb} = {sb, sa};
                {ea, eb} = {eb, ea};
                {ma, mb} = {mb, ma};
                exp_i = ea;
            end
            sr = sa;
            diff = ea - eb;
            ma_ext = {ma, 3'b000};
            mb_ext = {mb, 3'b000};
            if (diff >= 27) begin
                mb_shift = 27'd0;
                sticky = 1'b1;
            end else begin
                mb_shift = mb_ext >> diff;
                sticky = |(mb_ext & ((27'd1 << diff) - 27'd1));
            end
            mb_shift[0] = mb_shift[0] | sticky;

            if (sa == sb) begin
                sum = {1'b0, ma_ext} + {1'b0, mb_shift};
                exp_res = ea;
                if (sum[27]) begin
                    lost = sum[0];
                    sum = sum >> 1;
                    sum[0] = sum[0] | lost;
                    exp_res = exp_res + 8'd1;
                end
            end else begin
                diffm = ma_ext - mb_shift;
                exp_res = ea;
                if (diffm == 27'd0) begin
                    sum = 28'd0;
                end else begin
                    for (i = 0; i < 27; i = i + 1) begin
                        if (!diffm[26]) begin
                            diffm = diffm << 1;
                            exp_res = exp_res - 8'd1;
                        end
                    end
                    sum = {1'b0, diffm};
                end
            end

            if (sum == 28'd0) begin
                clover_fp32_add = 32'd0;
            end else begin
                mant24 = sum[26:3];
                guard  = sum[2];
                rnd    = sum[1];
                sticky = sum[0];
                if (guard && (rnd || sticky || mant24[0])) begin
                    mant_tmp = mant24 + 24'd1;
                    if (mant_tmp == 24'd0) begin
                        mant24 = 24'h80_0000;
                        exp_res = exp_res + 8'd1;
                    end else begin
                        mant24 = mant_tmp;
                    end
                end
                clover_fp32_add = clover_fp32_pack(sr, exp_res, mant24);
            end
        end
    end
endfunction

function automatic [31:0] clover_fp32_mul(input logic [31:0] a,
                                          input logic [31:0] b);
    logic sa, sb, sr;
    logic [7:0] ea, eb;
    logic [22:0] fa, fb;
    logic [23:0] ma, mb, mant24, mant_tmp;
    logic zero_a, zero_b, inf_a, inf_b, nan_a, nan_b;
    logic [47:0] prod;
    logic guard, rnd, sticky;
    integer exp_i;
    begin
        sa = a[31]; ea = a[30:23]; fa = a[22:0];
        sb = b[31]; eb = b[30:23]; fb = b[22:0];
        zero_a = (ea == 8'd0) && (fa == 23'd0);
        zero_b = (eb == 8'd0) && (fb == 23'd0);
        inf_a  = (ea == 8'hFF) && (fa == 23'd0);
        inf_b  = (eb == 8'hFF) && (fb == 23'd0);
        nan_a  = (ea == 8'hFF) && (fa != 23'd0);
        nan_b  = (eb == 8'hFF) && (fb != 23'd0);
        sr = sa ^ sb;

        if (nan_a || nan_b) begin
            clover_fp32_mul = 32'h7FC0_0000;
        end else if ((inf_a && zero_b) || (inf_b && zero_a)) begin
            clover_fp32_mul = 32'h7FC0_0000;
        end else if (inf_a || inf_b) begin
            clover_fp32_mul = {sr, 8'hFF, 23'd0};
        end else if (zero_a || zero_b) begin
            clover_fp32_mul = {sr, 31'd0};
        end else if ((ea == 8'd0) || (eb == 8'd0)) begin
            clover_fp32_mul = {sr, 31'd0};
        end else begin
            ma = {1'b1, fa};
            mb = {1'b1, fb};
            prod = ma * mb;
            exp_i = ea + eb - 127;
            if (prod[47]) begin
                prod   = prod >> 1;
                exp_i  = exp_i + 1;
            end
            mant24 = prod[46:23];
            guard  = prod[22];
            rnd    = prod[21];
            sticky = |prod[20:0];
            if (guard && (rnd || sticky || mant24[0])) begin
                mant_tmp = mant24 + 24'd1;
                if (mant_tmp == 24'd0) begin
                    mant24 = 24'h80_0000;
                    exp_i  = exp_i + 1;
                end else begin
                    mant24 = mant_tmp;
                end
            end
            clover_fp32_mul = clover_fp32_pack(sr, exp_i, mant24);
        end
    end
endfunction

function automatic [31:0] clover_fp16_to_fp32(input logic [15:0] h);
    logic sign;
    logic [4:0] exp5;
    logic [9:0] frac;
    integer exp32;
    begin
        sign = h[15];
        exp5 = h[14:10];
        frac = h[9:0];
        if (exp5 == 5'd0) begin
            clover_fp16_to_fp32 = {sign, 31'd0}; // subnormal flush
        end else if (exp5 == 5'h1F) begin
            if (frac == 10'd0) clover_fp16_to_fp32 = {sign, 8'hFF, 23'd0};
            else               clover_fp16_to_fp32 = 32'h7FC0_0000;
        end else begin
            exp32 = exp5 + 112;
            clover_fp16_to_fp32 = {sign, exp32[7:0], frac, 13'd0};
        end
    end
endfunction

function automatic [31:0] clover_bf16_to_fp32(input logic [15:0] h);
    begin
        clover_bf16_to_fp32 = {h, 16'd0};
    end
endfunction

`endif
