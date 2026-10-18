`include "clover_pkg.sv"

// Global CSR block behind the AXI4-Lite slave.
module clover_csr (
    input  logic        clk,
    input  logic        rst_n,

    input  logic        csr_we,
    input  logic [11:0] csr_waddr,
    input  logic [31:0] csr_wdata,
    input  logic [3:0]  csr_wstrb,
    input  logic [11:0] csr_raddr,
    output logic [31:0] csr_rdata,

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

    localparam logic [31:0] ID_VALUE      = 32'h564F_4C43; // "CLOV"
    localparam logic [31:0] VERSION_VALUE = 32'h0000_0001;

    logic [31:0] control_q;
    logic [31:0] irq_status_q;
    logic [31:0] irq_enable_q;
    logic [31:0] queue_base_q;
    logic [31:0] queue_size_q;
    logic [31:0] queue_tail_q;
    logic [31:0] complete_base_q;
    logic [31:0] complete_size_q;
    logic [31:0] complete_tail_q;
    logic [31:0] error_code_q;
    logic [31:0] error_addr_q;

    logic doorbell_q;
    logic abort_q;
    logic soft_reset_q;

    assign queue_base = queue_base_q;
    assign queue_size = queue_size_q;
    assign queue_tail = queue_tail_q;
    assign completion_base = complete_base_q;
    assign completion_size = complete_size_q;
    assign completion_tail = complete_tail_q;
    assign enable = control_q[0];
    assign irq = |(irq_status_q & irq_enable_q);
    assign doorbell = doorbell_q;
    assign abort_pulse = abort_q;
    assign soft_reset_pulse = soft_reset_q;

    function automatic logic [31:0] merge_strb(input logic [31:0] old_value,
                                               input logic [31:0] new_value,
                                               input logic [3:0]  strb);
        logic [31:0] merged;
        merged = old_value;
        for (int i = 0; i < 4; i++) begin
            if (strb[i]) begin
                merged[8*i +: 8] = new_value[8*i +: 8];
            end
        end
        merge_strb = merged;
    endfunction

    function automatic logic [31:0] strb_mask(input logic [3:0] strb);
        logic [31:0] mask;
        mask = 32'd0;
        for (int i = 0; i < 4; i++) begin
            if (strb[i]) begin
                mask[8*i +: 8] = 8'hFF;
            end
        end
        strb_mask = mask;
    endfunction

    logic [31:0] status_value;
    assign status_value = {29'd0, 1'b0, status_busy, ~status_busy};

    always_comb begin
        unique case (csr_raddr)
            CSR_ID:            csr_rdata = ID_VALUE;
            CSR_VERSION:       csr_rdata = VERSION_VALUE;
            CSR_STATUS:        csr_rdata = status_value;
            CSR_CONTROL:       csr_rdata = control_q;
            CSR_IRQ_STATUS:    csr_rdata = irq_status_q;
            CSR_IRQ_ENABLE:    csr_rdata = irq_enable_q;
            CSR_QUEUE_BASE:    csr_rdata = queue_base_q;
            CSR_QUEUE_SIZE:    csr_rdata = queue_size_q;
            CSR_QUEUE_HEAD:    csr_rdata = queue_head_in;
            CSR_QUEUE_TAIL:    csr_rdata = queue_tail_q;
            CSR_COMPLETE_BASE: csr_rdata = complete_base_q;
            CSR_COMPLETE_SIZE: csr_rdata = complete_size_q;
            CSR_COMPLETE_HEAD: csr_rdata = completion_head_in;
            CSR_COMPLETE_TAIL: csr_rdata = complete_tail_q;
            CSR_CYCLE_LO:      csr_rdata = cycle_count[31:0];
            CSR_CYCLE_HI:      csr_rdata = cycle_count[63:32];
            CSR_ERROR_CODE:    csr_rdata = error_code_q;
            CSR_ERROR_ADDR:    csr_rdata = error_addr_q;
            CSR_SCRATCH_BASE:  csr_rdata = 32'h0000_0000;
            CSR_SCRATCH_SIZE:  csr_rdata = 32'h0004_0000;
            CSR_CAP0:          csr_rdata = 32'h0000_000F;
            CSR_CAP1:          csr_rdata = 32'd0;
            default:           csr_rdata = 32'd0;
        endcase
    end

    logic [31:0] irq_clear_mask;
    always_comb begin
        irq_clear_mask = 32'd0;
        if (csr_we && (csr_waddr == CSR_IRQ_STATUS)) begin
            irq_clear_mask = csr_wdata & strb_mask(csr_wstrb);
        end
    end

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            control_q       <= 32'd0;
            irq_status_q    <= 32'd0;
            irq_enable_q    <= 32'd0;
            queue_base_q    <= 32'd0;
            queue_size_q    <= 32'd0;
            queue_tail_q    <= 32'd0;
            complete_base_q <= 32'd0;
            complete_size_q <= 32'd0;
            complete_tail_q <= 32'd0;
            error_code_q    <= 32'd0;
            error_addr_q    <= 32'd0;
            doorbell_q      <= 1'b0;
            abort_q         <= 1'b0;
            soft_reset_q    <= 1'b0;
        end else begin
            doorbell_q   <= 1'b0;
            abort_q      <= 1'b0;
            soft_reset_q <= 1'b0;

            irq_status_q <= (irq_status_q & ~irq_clear_mask) |
                            {31'd0, irq_set};

            if (csr_we) begin
                unique case (csr_waddr)
                    CSR_CONTROL:
                        control_q <= merge_strb(control_q, csr_wdata, csr_wstrb);
                    CSR_IRQ_STATUS:
                        ; // handled through irq_clear_mask above
                    CSR_IRQ_ENABLE:
                        irq_enable_q <= merge_strb(irq_enable_q, csr_wdata, csr_wstrb);
                    CSR_QUEUE_BASE:
                        queue_base_q <= merge_strb(queue_base_q, csr_wdata, csr_wstrb);
                    CSR_QUEUE_SIZE:
                        queue_size_q <= merge_strb(queue_size_q, csr_wdata, csr_wstrb);
                    CSR_QUEUE_TAIL: begin
                        queue_tail_q <= merge_strb(queue_tail_q, csr_wdata, csr_wstrb);
                        doorbell_q <= 1'b1;
                    end
                    CSR_COMPLETE_BASE:
                        complete_base_q <= merge_strb(complete_base_q, csr_wdata, csr_wstrb);
                    CSR_COMPLETE_SIZE:
                        complete_size_q <= merge_strb(complete_size_q, csr_wdata, csr_wstrb);
                    CSR_COMPLETE_TAIL:
                        complete_tail_q <= merge_strb(complete_tail_q, csr_wdata, csr_wstrb);
                    CSR_ABORT:
                        if (csr_wdata[0]) abort_q <= 1'b1;
                    CSR_SOFT_RESET:
                        if (csr_wdata[0]) soft_reset_q <= 1'b1;
                    default: ;
                endcase
            end
        end
    end

endmodule
