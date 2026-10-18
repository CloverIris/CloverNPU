`include "clover_pkg.sv"

module clover_decoder
(
    input  logic [31:0] instr,
    output ctrl_t       ctrl
);

    function automatic logic [31:0] imm_i(input logic [31:0] x);
        imm_i = {{20{x[31]}}, x[31:20]};
    endfunction

    function automatic logic [31:0] imm_s(input logic [31:0] x);
        imm_s = {{20{x[31]}}, x[31:25], x[11:7]};
    endfunction

    function automatic logic [31:0] imm_b(input logic [31:0] x);
        imm_b = {{19{x[31]}}, x[31], x[7], x[30:25], x[11:8], 1'b0};
    endfunction

    function automatic logic [31:0] imm_u(input logic [31:0] x);
        imm_u = {x[31:12], 12'b0};
    endfunction

    function automatic logic [31:0] imm_j(input logic [31:0] x);
        imm_j = {{11{x[31]}}, x[31], x[19:12], x[20], x[30:21], 1'b0};
    endfunction

    always_comb begin
        ctrl = '0;
        ctrl.rd  = instr[11:7];
        ctrl.rs1 = instr[19:15];
        ctrl.rs2 = instr[24:20];

        unique case (instr[6:0])
            OPC_LUI: begin
                ctrl.reg_write = 1'b1;
                ctrl.alu_op    = ALU_PASS_B;
                ctrl.alu_b_imm = 1'b1;
                ctrl.imm       = imm_u(instr);
            end
            OPC_AUIPC: begin
                ctrl.reg_write = 1'b1;
                ctrl.alu_op    = ALU_ADD;
                ctrl.alu_a_pc  = 1'b1;
                ctrl.alu_b_imm = 1'b1;
                ctrl.imm       = imm_u(instr);
            end
            OPC_JAL: begin
                ctrl.reg_write = 1'b1;
                ctrl.jump      = 1'b1;
                ctrl.imm       = imm_j(instr);
            end
            OPC_JALR: begin
                if (instr[14:12] == 3'd0) begin
                    ctrl.reg_write = 1'b1;
                    ctrl.jump      = 1'b1;
                    ctrl.jump_reg  = 1'b1;
                    ctrl.alu_op    = ALU_ADD;
                    ctrl.alu_b_imm = 1'b1;
                    ctrl.imm       = imm_i(instr);
                end else begin
                    ctrl.illegal = 1'b1;
                end
            end
            OPC_BRANCH: begin
                if ((instr[14:12] == 3'd0) || (instr[14:12] == 3'd1) ||
                    (instr[14:12] == 3'd4) || (instr[14:12] == 3'd5) ||
                    (instr[14:12] == 3'd6) || (instr[14:12] == 3'd7)) begin
                    ctrl.branch    = 1'b1;
                    ctrl.branch_op = branch_op_t'(instr[14:12]);
                    ctrl.imm       = imm_b(instr);
                end else begin
                    ctrl.illegal = 1'b1;
                end
            end
            OPC_LOAD: begin
                ctrl.reg_write = 1'b1;
                ctrl.mem_read  = 1'b1;
                ctrl.alu_op    = ALU_ADD;
                ctrl.alu_b_imm = 1'b1;
                ctrl.imm       = imm_i(instr);
                unique case (instr[14:12])
                    3'd0: begin ctrl.mem_size = MEM_BYTE; ctrl.mem_unsigned = 1'b0; end
                    3'd1: begin ctrl.mem_size = MEM_HALF; ctrl.mem_unsigned = 1'b0; end
                    3'd2: begin ctrl.mem_size = MEM_WORD; ctrl.mem_unsigned = 1'b0; end
                    3'd4: begin ctrl.mem_size = MEM_BYTE; ctrl.mem_unsigned = 1'b1; end
                    3'd5: begin ctrl.mem_size = MEM_HALF; ctrl.mem_unsigned = 1'b1; end
                    default: ctrl.illegal = 1'b1;
                endcase
            end
            OPC_STORE: begin
                ctrl.mem_write = 1'b1;
                ctrl.alu_op    = ALU_ADD;
                ctrl.alu_b_imm = 1'b1;
                ctrl.imm       = imm_s(instr);
                unique case (instr[14:12])
                    3'd0: ctrl.mem_size = MEM_BYTE;
                    3'd1: ctrl.mem_size = MEM_HALF;
                    3'd2: ctrl.mem_size = MEM_WORD;
                    default: ctrl.illegal = 1'b1;
                endcase
            end
            OPC_OPIMM: begin
                ctrl.reg_write = 1'b1;
                ctrl.alu_b_imm = 1'b1;
                ctrl.imm       = imm_i(instr);
                unique case (instr[14:12])
                    3'd0: ctrl.alu_op = ALU_ADD;
                    3'd1: begin
                        ctrl.alu_op = ALU_SLL;
                        ctrl.imm    = {27'd0, instr[24:20]};
                    end
                    3'd2: ctrl.alu_op = ALU_SLT;
                    3'd3: ctrl.alu_op = ALU_SLTU;
                    3'd4: ctrl.alu_op = ALU_XOR;
                    3'd5: begin
                        ctrl.imm = {27'd0, instr[24:20]};
                        if (instr[31:25] == 7'h00) begin
                            ctrl.alu_op = ALU_SRL;
                        end else if (instr[31:25] == 7'h20) begin
                            ctrl.alu_op = ALU_SRA;
                        end else begin
                            ctrl.illegal = 1'b1;
                        end
                    end
                    3'd6: ctrl.alu_op = ALU_OR;
                    3'd7: ctrl.alu_op = ALU_AND;
                    default: ctrl.illegal = 1'b1;
                endcase
            end
            OPC_OP: begin
                ctrl.reg_write = 1'b1;
                if (instr[31:25] == 7'h00) begin
                    unique case (instr[14:12])
                        3'd0: ctrl.alu_op = ALU_ADD;
                        3'd1: ctrl.alu_op = ALU_SLL;
                        3'd2: ctrl.alu_op = ALU_SLT;
                        3'd3: ctrl.alu_op = ALU_SLTU;
                        3'd4: ctrl.alu_op = ALU_XOR;
                        3'd5: ctrl.alu_op = ALU_SRL;
                        3'd6: ctrl.alu_op = ALU_OR;
                        3'd7: ctrl.alu_op = ALU_AND;
                        default: ctrl.illegal = 1'b1;
                    endcase
                end else if (instr[31:25] == 7'h20) begin
                    unique case (instr[14:12])
                        3'd0: ctrl.alu_op = ALU_SUB;
                        3'd5: ctrl.alu_op = ALU_SRA;
                        default: ctrl.illegal = 1'b1;
                    endcase
                end else if (instr[31:25] == 7'h01) begin
                    unique case (instr[14:12])
                        3'd0: ctrl.alu_op = ALU_MUL;
                        3'd1: ctrl.alu_op = ALU_MULH;
                        3'd2: ctrl.alu_op = ALU_MULHSU;
                        3'd3: ctrl.alu_op = ALU_MULHU;
                        3'd4: ctrl.alu_op = ALU_DIV;
                        3'd5: ctrl.alu_op = ALU_DIVU;
                        3'd6: ctrl.alu_op = ALU_REM;
                        3'd7: ctrl.alu_op = ALU_REMU;
                        default: ctrl.illegal = 1'b1;
                    endcase
                end else begin
                    ctrl.illegal = 1'b1;
                end
            end
            OPC_MISC: begin
                // FENCE and CACHE are accepted as no-ops by the scalar core.
            end
            OPC_SYSTEM: begin
                if (instr[14:12] == 3'd0) begin
                    unique case (instr[31:20])
                        12'h000: begin
                            ctrl.ecall = 1'b1;
                            ctrl.rs1   = 5'd17; // a7
                            ctrl.rs2   = 5'd10; // a0
                        end
                        12'h001: ctrl.ebreak = 1'b1;
                        12'h302: ctrl.mret   = 1'b1;
                        12'h105: ctrl.wfi    = 1'b1;
                        default: ctrl.illegal = 1'b1;
                    endcase
                end else if ((instr[14:12] == 3'd1) || (instr[14:12] == 3'd2) ||
                             (instr[14:12] == 3'd3) || (instr[14:12] == 3'd5) ||
                             (instr[14:12] == 3'd6) || (instr[14:12] == 3'd7)) begin
                    ctrl.reg_write = 1'b1;
                    ctrl.csr_op    = csr_op_t'(instr[14:12]);
                    ctrl.csr_addr  = instr[31:20];
                    ctrl.csr_imm   = (instr[14:12] >= 3'd5);
                    if (ctrl.csr_imm) begin
                        ctrl.rs1 = instr[19:15]; // zimm
                    end
                end else begin
                    ctrl.illegal = 1'b1;
                end
            end
            OPC_TILE: begin
                ctrl.tile        = 1'b1;
                ctrl.tile_funct3 = instr[14:12];
                if (instr[14:12] == 3'd3) begin
                    ctrl.reg_write = 1'b1; // TILE.WAIT returns status 0
                end
            end
            default: ctrl.illegal = 1'b1;
        endcase
    end

endmodule
