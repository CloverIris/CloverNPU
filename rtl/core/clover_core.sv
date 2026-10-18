`include "clover_pkg.sv"
`include "clover_alu.sv"
`include "clover_regfile.sv"
`include "clover_decoder.sv"

module clover_core
#(
    parameter logic [31:0] RESET_PC = RESET_VECTOR
) (
    input  logic        clk,
    input  logic        rst_n,

    // Kernel boot interface.
    input  logic        core_start,
    input  logic [31:0] core_entry,
    input  logic [31:0] core_sp,
    input  logic [31:0] core_arg,
    output logic        core_active,

    // Instruction memory (valid/ready, combinational read).
    output logic        imem_req,
    output logic [31:0] imem_addr,
    input  logic        imem_ready,
    input  logic [31:0] imem_rdata,

    // Data memory (valid/ready, combinational read).
    output logic        dmem_req,
    output logic        dmem_we,
    output logic [31:0] dmem_addr,
    output logic [1:0]  dmem_size,
    output logic [31:0] dmem_wdata,
    input  logic        dmem_ready,
    input  logic [31:0] dmem_rdata,

    // Status.
    output logic        halted,
    output logic [31:0] exit_code,
    output logic [7:0]  uart_data,
    output logic        uart_valid,
    output logic [31:0] pc_out,
    output logic [31:0] cycle_count,
    output logic [3:0]  halt_reason
);

    // ---------------------------------------------------------------- state
    logic [31:0] pc;
    logic        halted_q;
    logic        core_active_q;
    logic [31:0] exit_code_q;
    logic [7:0]  uart_data_q;
    logic        uart_valid_q;
    logic [31:0] cycle_count_q;
    logic [31:0] instret_q;
    logic [3:0]  halt_reason_q;

    // CSRs.
    logic [31:0] csr_mstatus_q;
    logic [31:0] csr_mtvec_q;
    logic [31:0] csr_mscratch_q;
    logic [31:0] csr_mepc_q;
    logic [31:0] csr_mcause_q;
    logic [31:0] csr_mtval_q;

    // ------------------------------------------------------------- pipeline
    logic        if_id_valid;
    logic [31:0] if_id_pc;
    logic [31:0] if_id_instr;

    logic        id_ex_valid;
    ctrl_t       id_ex_ctrl;
    logic [31:0] id_ex_pc;
    logic [31:0] id_ex_rs1_data;
    logic [31:0] id_ex_rs2_data;
    logic [4:0]  id_ex_rs1_addr;
    logic [4:0]  id_ex_rs2_addr;

    logic        ex_mem_valid;
    ctrl_t       ex_mem_ctrl;
    logic [31:0] ex_mem_result;
    logic [31:0] ex_mem_store_data;
    logic [31:0] ex_mem_pc;
    logic [4:0]  ex_mem_rd;

    logic        mem_wb_valid;
    ctrl_t       mem_wb_ctrl;
    logic [31:0] mem_wb_result;
    logic [31:0] mem_wb_pc;
    logic [4:0]  mem_wb_rd;

    // ------------------------------------------------------------ IF / ID
    assign imem_req  = !halted_q && core_active_q;
    assign imem_addr = pc;

    ctrl_t id_ctrl;
    clover_decoder u_decoder (
        .instr (if_id_instr),
        .ctrl  (id_ctrl)
    );

    logic [4:0] id_rs1_addr;
    logic [4:0] id_rs2_addr;
    always_comb begin
        id_rs1_addr = id_ctrl.csr_imm ? 5'd0 : id_ctrl.rs1;
        id_rs2_addr = id_ctrl.rs2;
        if (id_ctrl.ecall) begin
            id_rs1_addr = 5'd17; // a7
            id_rs2_addr = 5'd10; // a0
        end
    end

    logic [31:0] id_rs1_data;
    logic [31:0] id_rs2_data;
    logic        wb_we;
    logic [4:0]  wb_rd;
    logic [31:0] wb_data;

    logic [31:0] dbg_a0;
    clover_regfile u_regfile (
        .clk      (clk),
        .rst_n    (rst_n),
        .rs1      (id_rs1_addr),
        .rs2      (id_rs2_addr),
        .rd       (wb_rd),
        .we       (wb_we),
        .wdata    (wb_data),
        .boot     (core_start),
        .boot_sp  (core_sp),
        .boot_arg (core_arg),
        .rdata1   (id_rs1_data),
        .rdata2   (id_rs2_data),
        .dbg_a0   (dbg_a0)
    );

    // ------------------------------------------------------------- hazards
    logic load_use_id;
    logic load_use_ex;
    logic wb_id_hazard;
    logic mem_stall;
    logic imem_stall;
    logic stall_all;
    logic redirect;
    logic halt_event;
    logic ecall_exit;
    logic ecall_uart;
    logic ecall_error;
    logic mem_misaligned;

    assign load_use_id =
        id_ex_valid && id_ex_ctrl.mem_read && (id_ex_ctrl.rd != 5'd0) &&
        if_id_valid &&
        ((id_ex_ctrl.rd == id_rs1_addr) || (id_ex_ctrl.rd == id_rs2_addr));

    assign load_use_ex =
        ex_mem_valid && ex_mem_ctrl.mem_read && (ex_mem_ctrl.rd != 5'd0) &&
        id_ex_valid &&
        ((ex_mem_ctrl.rd == id_ex_rs1_addr) ||
         (ex_mem_ctrl.rd == id_ex_rs2_addr));

    // WB writes at the same edge at which ID reads the register file.  If ID
    // needs the value being written, stall one cycle so the write is visible.
    assign wb_id_hazard =
        if_id_valid && mem_wb_valid && mem_wb_ctrl.reg_write &&
        (mem_wb_rd != 5'd0) &&
        ((mem_wb_rd == id_rs1_addr) || (mem_wb_rd == id_rs2_addr));

    assign mem_stall  = (dmem_req && !dmem_ready);
    assign imem_stall = (imem_req && !imem_ready);
    assign stall_all  = mem_stall || imem_stall;

    // ------------------------------------------------------------ EX stage
    logic [31:0] fwd_rs1;
    logic [31:0] fwd_rs2;

    always_comb begin
        fwd_rs1 = id_ex_rs1_data;
        fwd_rs2 = id_ex_rs2_data;
        if (ex_mem_valid && ex_mem_ctrl.reg_write && (ex_mem_rd != 5'd0) &&
            !ex_mem_ctrl.mem_read && (ex_mem_rd == id_ex_rs1_addr)) begin
            fwd_rs1 = ex_mem_result;
        end else if (mem_wb_valid && mem_wb_ctrl.reg_write &&
                     (mem_wb_rd != 5'd0) &&
                     (mem_wb_rd == id_ex_rs1_addr)) begin
            fwd_rs1 = mem_wb_result;
        end
        if (ex_mem_valid && ex_mem_ctrl.reg_write && (ex_mem_rd != 5'd0) &&
            !ex_mem_ctrl.mem_read && (ex_mem_rd == id_ex_rs2_addr)) begin
            fwd_rs2 = ex_mem_result;
        end else if (mem_wb_valid && mem_wb_ctrl.reg_write &&
                     (mem_wb_rd != 5'd0) &&
                     (mem_wb_rd == id_ex_rs2_addr)) begin
            fwd_rs2 = mem_wb_result;
        end
    end

    logic [31:0] ex_a;
    logic [31:0] ex_b;
    assign ex_a = id_ex_ctrl.alu_a_pc ? id_ex_pc : fwd_rs1;
    assign ex_b = id_ex_ctrl.alu_b_imm ? id_ex_ctrl.imm : fwd_rs2;

    logic [31:0] alu_y;
    clover_alu u_alu (
        .a  (ex_a),
        .b  (ex_b),
        .op (id_ex_ctrl.alu_op),
        .y  (alu_y)
    );

    logic branch_taken;
    always_comb begin
        branch_taken = 1'b0;
        if (id_ex_ctrl.branch) begin
            unique case (id_ex_ctrl.branch_op)
                BR_BEQ:  branch_taken = (fwd_rs1 == fwd_rs2);
                BR_BNE:  branch_taken = (fwd_rs1 != fwd_rs2);
                BR_BLT:  branch_taken = ($signed(fwd_rs1) < $signed(fwd_rs2));
                BR_BGE:  branch_taken = ($signed(fwd_rs1) >= $signed(fwd_rs2));
                BR_BLTU: branch_taken = (fwd_rs1 < fwd_rs2);
                BR_BGEU: branch_taken = (fwd_rs1 >= fwd_rs2);
                default: branch_taken = 1'b0;
            endcase
        end
    end

    logic [31:0] csr_rdata;
    always_comb begin
        unique case (id_ex_ctrl.csr_addr)
            CSR_MSTATUS:  csr_rdata = csr_mstatus_q;
            CSR_MTVEC:    csr_rdata = csr_mtvec_q;
            CSR_MSCRATCH: csr_rdata = csr_mscratch_q;
            CSR_MEPC:     csr_rdata = csr_mepc_q;
            CSR_MCAUSE:   csr_rdata = csr_mcause_q;
            CSR_MTVAL:    csr_rdata = csr_mtval_q;
            CSR_CYCLE:    csr_rdata = cycle_count_q;
            CSR_INSTRET:  csr_rdata = instret_q;
            CSR_TILE_STATUS: csr_rdata = 32'd0;
            default:      csr_rdata = 32'd0;
        endcase
    end

    logic [31:0] csr_source;
    logic [31:0] csr_wdata;
    assign csr_source = id_ex_ctrl.csr_imm
                        ? {27'd0, id_ex_ctrl.rs1}
                        : fwd_rs1;
    always_comb begin
        unique case (id_ex_ctrl.csr_op)
            CSR_RW, CSR_RWI: csr_wdata = csr_source;
            CSR_RS, CSR_RSI: csr_wdata = csr_rdata | csr_source;
            CSR_RC, CSR_RCI: csr_wdata = csr_rdata & ~csr_source;
            default:         csr_wdata = csr_rdata;
        endcase
    end

    logic [31:0] ex_target;
    always_comb begin
        ex_target = id_ex_pc + id_ex_ctrl.imm;
        if (id_ex_ctrl.mret) begin
            ex_target = csr_mepc_q;
        end else if (id_ex_ctrl.jump_reg) begin
            ex_target = alu_y & ~32'd1;
        end
    end

    assign redirect = id_ex_valid &&
                      (branch_taken || id_ex_ctrl.jump || id_ex_ctrl.mret);

    logic [31:0] ex_result;
    always_comb begin
        if (id_ex_ctrl.mem_read || id_ex_ctrl.mem_write) begin
            ex_result = alu_y;
        end else if (id_ex_ctrl.jump) begin
            ex_result = id_ex_pc + 32'd4;
        end else if (id_ex_ctrl.csr_op != CSR_NONE) begin
            ex_result = csr_rdata;
        end else if (id_ex_ctrl.tile) begin
            ex_result = 32'd0;
        end else begin
            ex_result = alu_y;
        end
    end

    // ----------------------------------------------------------- MEM stage
    assign dmem_req   = ex_mem_valid && (ex_mem_ctrl.mem_read ||
                                         ex_mem_ctrl.mem_write) && !halted_q &&
                        core_active_q;
    assign dmem_we    = ex_mem_ctrl.mem_write;
    assign dmem_addr  = ex_mem_result;
    assign dmem_size  = ex_mem_ctrl.mem_size;
    assign dmem_wdata = ex_mem_store_data;

    logic [7:0]  mem_byte;
    logic [15:0] mem_half;
    always_comb begin
        unique case (dmem_addr[1:0])
            2'd0: mem_byte = dmem_rdata[7:0];
            2'd1: mem_byte = dmem_rdata[15:8];
            2'd2: mem_byte = dmem_rdata[23:16];
            default: mem_byte = dmem_rdata[31:24];
        endcase
        mem_half = dmem_addr[1] ? dmem_rdata[31:16] : dmem_rdata[15:0];
    end

    logic [31:0] mem_rdata_ext;
    always_comb begin
        unique case (ex_mem_ctrl.mem_size)
            MEM_BYTE: mem_rdata_ext = ex_mem_ctrl.mem_unsigned
                                      ? {24'd0, mem_byte}
                                      : {{24{mem_byte[7]}}, mem_byte};
            MEM_HALF: mem_rdata_ext = ex_mem_ctrl.mem_unsigned
                                      ? {16'd0, mem_half}
                                      : {{16{mem_half[15]}}, mem_half};
            default:  mem_rdata_ext = dmem_rdata;
        endcase
    end

    logic [31:0] mem_result;
    assign mem_result = ex_mem_ctrl.mem_read ? mem_rdata_ext : ex_mem_result;

    always_comb begin
        mem_misaligned = 1'b0;
        if (ex_mem_valid && (ex_mem_ctrl.mem_read || ex_mem_ctrl.mem_write)) begin
            if ((ex_mem_ctrl.mem_size == MEM_HALF) && dmem_addr[0]) begin
                mem_misaligned = 1'b1;
            end
            if ((ex_mem_ctrl.mem_size == MEM_WORD) && (dmem_addr[1:0] != 2'd0)) begin
                mem_misaligned = 1'b1;
            end
        end
    end

    // ------------------------------------------------------- syscall / halt
    assign ecall_exit  = id_ex_valid && id_ex_ctrl.ecall && (fwd_rs1 == 32'd0);
    assign ecall_uart  = id_ex_valid && id_ex_ctrl.ecall && (fwd_rs1 == 32'd1);
    assign ecall_error = id_ex_valid && id_ex_ctrl.ecall &&
                         (fwd_rs1 != 32'd0) && (fwd_rs1 != 32'd1);

    // Kernel return convention: CP boots with ra=0; the final `ret` is
    // jalr zero, ra, 0 and targets address 0.
    logic kernel_return;
    assign kernel_return = id_ex_valid && id_ex_ctrl.jump_reg &&
                           ((alu_y & ~32'd1) == 32'd0);

    assign halt_event = !halted_q &&
                        (mem_misaligned || kernel_return ||
                         (id_ex_valid && (id_ex_ctrl.illegal || id_ex_ctrl.ebreak)) ||
                         ecall_exit || ecall_error);

    assign wb_we = mem_wb_valid && mem_wb_ctrl.reg_write &&
                   (mem_wb_rd != 5'd0) && !halted_q && !stall_all;
    assign wb_rd   = mem_wb_rd;
    assign wb_data = mem_wb_result;

    logic csr_we;
    assign csr_we = id_ex_valid && (id_ex_ctrl.csr_op != CSR_NONE) &&
                    !halted_q && !halt_event && !stall_all &&
                    !load_use_id && !load_use_ex && !wb_id_hazard;

    // ------------------------------------------------------------ pipeline
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            pc            <= RESET_PC;
            if_id_valid   <= 1'b0;
            if_id_pc      <= 32'd0;
            if_id_instr   <= 32'd0;
            id_ex_valid   <= 1'b0;
            id_ex_ctrl    <= '0;
            id_ex_pc      <= 32'd0;
            id_ex_rs1_data<= 32'd0;
            id_ex_rs2_data<= 32'd0;
            id_ex_rs1_addr<= 5'd0;
            id_ex_rs2_addr<= 5'd0;
            ex_mem_valid  <= 1'b0;
            ex_mem_ctrl   <= '0;
            ex_mem_result <= 32'd0;
            ex_mem_store_data <= 32'd0;
            ex_mem_pc     <= 32'd0;
            ex_mem_rd     <= 5'd0;
            mem_wb_valid  <= 1'b0;
            mem_wb_ctrl   <= '0;
            mem_wb_result <= 32'd0;
            mem_wb_pc     <= 32'd0;
            mem_wb_rd     <= 5'd0;
            halted_q      <= 1'b0;
            core_active_q <= 1'b0;
            exit_code_q   <= 32'd0;
            uart_data_q   <= 8'd0;
            uart_valid_q  <= 1'b0;
            cycle_count_q <= 32'd0;
            instret_q     <= 32'd0;
            halt_reason_q <= 4'd0;
            csr_mstatus_q <= 32'd0;
            csr_mtvec_q   <= 32'd0;
            csr_mscratch_q<= 32'd0;
            csr_mepc_q    <= 32'd0;
            csr_mcause_q  <= 32'd0;
            csr_mtval_q   <= 32'd0;
        end else begin
            uart_valid_q <= 1'b0;
            if (!halted_q) begin
                cycle_count_q <= cycle_count_q + 32'd1;
            end

            if (core_start) begin
                pc            <= core_entry;
                core_active_q <= 1'b1;
                halted_q      <= 1'b0;
                exit_code_q   <= 32'd0;
                halt_reason_q <= 4'd0;
                if_id_valid   <= 1'b0;
                id_ex_valid   <= 1'b0;
                ex_mem_valid  <= 1'b0;
                mem_wb_valid  <= 1'b0;
            end else if (halted_q) begin
                // Frozen.
            end else if (halt_event) begin
                halted_q      <= 1'b1;
                core_active_q <= 1'b0;
                exit_code_q   <= kernel_return ? dbg_a0 :
                                (ecall_exit ? {24'd0, fwd_rs2[7:0]}
                                            : (ecall_error ? 32'd1 : 32'd0));
                halt_reason_q <= mem_misaligned ? 4'd5 :
                                 (kernel_return ? 4'd6 :
                                 (id_ex_ctrl.illegal ? 4'd1 :
                                 (id_ex_ctrl.ebreak ? 4'd2 :
                                 (ecall_exit ? 4'd3 :
                                 (ecall_error ? 4'd4 : 4'd0)))));
                if_id_valid <= 1'b0;
                id_ex_valid <= 1'b0;
                ex_mem_valid<= 1'b0;
                mem_wb_valid<= 1'b0;
            end else if (stall_all) begin
                // Hold the whole pipeline.
            end else begin
                if (mem_wb_valid) begin
                    instret_q <= instret_q + 32'd1;
                end

                // WB (already driven by mem_wb through wb_we).

                // MEM/WB <- EX/MEM; EX/MEM <- EX.
                mem_wb_valid  <= ex_mem_valid;
                mem_wb_ctrl   <= ex_mem_ctrl;
                mem_wb_result <= mem_result;
                mem_wb_pc     <= ex_mem_pc;
                mem_wb_rd     <= ex_mem_rd;

                if (load_use_ex) begin
                    // The load in MEM/WB will supply the held instruction.
                    // This must win over a redirect: the branch in ID/EX may
                    // be reading the load result.
                    mem_wb_valid  <= ex_mem_valid;
                    mem_wb_ctrl   <= ex_mem_ctrl;
                    mem_wb_result <= mem_result;
                    mem_wb_pc     <= ex_mem_pc;
                    mem_wb_rd     <= ex_mem_rd;
                    ex_mem_valid  <= 1'b0;
                    ex_mem_ctrl   <= '0;
                    // Hold ID/EX, IF/ID and PC.
                end else if (redirect) begin
                    // The branch/jump in EX still proceeds to EX/MEM (for JAL
                    // link), but younger instructions are squashed.
                    ex_mem_valid  <= id_ex_valid;
                    ex_mem_ctrl   <= id_ex_ctrl;
                    ex_mem_result <= ex_result;
                    ex_mem_store_data <= fwd_rs2;
                    ex_mem_pc     <= id_ex_pc;
                    ex_mem_rd     <= id_ex_ctrl.rd;
                    id_ex_valid   <= 1'b0;
                    if_id_valid   <= 1'b0;
                    pc            <= ex_target;
                    if (id_ex_ctrl.mret) begin
                        csr_mstatus_q[3]     <= csr_mstatus_q[7];
                        csr_mstatus_q[7]     <= 1'b1;
                        csr_mstatus_q[12:11] <= 2'b00;
                    end
                end else if (wb_id_hazard) begin
                    ex_mem_valid  <= id_ex_valid;
                    ex_mem_ctrl   <= id_ex_ctrl;
                    ex_mem_result <= ex_result;
                    ex_mem_store_data <= fwd_rs2;
                    ex_mem_pc     <= id_ex_pc;
                    ex_mem_rd     <= id_ex_ctrl.rd;
                    id_ex_valid   <= 1'b0; // bubble; IF/ID and PC hold
                end else if (load_use_id) begin
                    ex_mem_valid  <= id_ex_valid;
                    ex_mem_ctrl   <= id_ex_ctrl;
                    ex_mem_result <= ex_result;
                    ex_mem_store_data <= fwd_rs2;
                    ex_mem_pc     <= id_ex_pc;
                    ex_mem_rd     <= id_ex_ctrl.rd;
                    id_ex_valid   <= 1'b0; // bubble
                    // Hold IF/ID and PC.
                end else begin
                    // Normal advance.
                    ex_mem_valid  <= id_ex_valid;
                    ex_mem_ctrl   <= id_ex_ctrl;
                    ex_mem_result <= ex_result;
                    ex_mem_store_data <= fwd_rs2;
                    ex_mem_pc     <= id_ex_pc;
                    ex_mem_rd     <= id_ex_ctrl.rd;

                    id_ex_valid    <= if_id_valid;
                    id_ex_ctrl     <= id_ctrl;
                    id_ex_pc       <= if_id_pc;
                    id_ex_rs1_data <= id_rs1_data;
                    id_ex_rs2_data <= id_rs2_data;
                    id_ex_rs1_addr <= id_rs1_addr;
                    id_ex_rs2_addr <= id_rs2_addr;

                    if_id_valid <= 1'b1;
                    if_id_pc    <= pc;
                    if_id_instr <= imem_rdata;
                    pc          <= pc + 32'd4;
                end

                if (ecall_uart) begin
                    uart_data_q  <= fwd_rs2[7:0];
                    uart_valid_q <= 1'b1;
                end

                if (csr_we) begin
                    unique case (id_ex_ctrl.csr_addr)
                        CSR_MSTATUS:  csr_mstatus_q  <= csr_wdata;
                        CSR_MTVEC:    csr_mtvec_q    <= csr_wdata;
                        CSR_MSCRATCH: csr_mscratch_q <= csr_wdata;
                        CSR_MEPC:     csr_mepc_q     <= csr_wdata;
                        CSR_MCAUSE:   csr_mcause_q   <= csr_wdata;
                        CSR_MTVAL:    csr_mtval_q    <= csr_wdata;
                        default: ;
                    endcase
                end
            end
        end
    end

    assign halted      = halted_q;
    assign exit_code   = exit_code_q;
    assign uart_data   = uart_data_q;
    assign uart_valid  = uart_valid_q;
    assign pc_out      = pc;
    assign cycle_count = cycle_count_q;
    assign halt_reason = halt_reason_q;
    assign core_active = core_active_q;

endmodule
