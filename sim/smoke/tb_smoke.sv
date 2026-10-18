`timescale 1ns/1ps

module tb_smoke;

    logic        clk;
    logic        rst_n;
    logic [31:0] a;
    logic [31:0] b;
    logic [31:0] y;

    clover_smoke #(
        .WIDTH(32)
    ) dut (
        .clk   (clk),
        .rst_n (rst_n),
        .a     (a),
        .b     (b),
        .y     (y)
    );

    always #5 clk = ~clk;

    initial begin
        clk   = 1'b0;
        rst_n = 1'b0;
        a     = 32'd0;
        b     = 32'd0;

        #20;
        rst_n = 1'b1;
        a     = 32'd3;
        b     = 32'd4;

        @(posedge clk);
        #1;

        if (y !== 32'd7) begin
            $display("SMOKE FAIL: expected 7, got %0d", y);
            $fatal(1);
        end

        $display("SMOKE PASS: 3 + 4 = %0d", y);
        $finish;
    end

endmodule
