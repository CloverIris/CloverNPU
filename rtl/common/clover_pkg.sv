`ifndef CLOVER_PKG_SV
`define CLOVER_PKG_SV

    localparam int XLEN = 32;
    localparam logic [31:0] RESET_VECTOR = 32'h1000_0000;

    // Device memory map (see docs/03-abi.md).
    localparam logic [31:0] SCRATCHPAD_BASE = 32'h0000_0000;
    localparam logic [31:0] SCRATCHPAD_SIZE = 32'h0004_0000;
    localparam logic [31:0] DRAM_BASE       = 32'h1000_0000;
    localparam logic [31:0] DRAM_SIZE       = 32'h1000_0000;

    typedef enum logic [6:0] {
        OPC_LOAD    = 7'h03,
        OPC_MISC    = 7'h0F,
        OPC_OPIMM   = 7'h13,
        OPC_AUIPC   = 7'h17,
        OPC_STORE   = 7'h23,
        OPC_TILE    = 7'h2B,
        OPC_OP      = 7'h33,
        OPC_LUI     = 7'h37,
        OPC_BRANCH  = 7'h63,
        OPC_JALR    = 7'h67,
        OPC_JAL     = 7'h6F,
        OPC_SYSTEM  = 7'h73
    } opcode_t;

    typedef enum logic [4:0] {
        ALU_ADD,
        ALU_SUB,
        ALU_SLL,
        ALU_SLT,
        ALU_SLTU,
        ALU_XOR,
        ALU_SRL,
        ALU_SRA,
        ALU_OR,
        ALU_AND,
        ALU_MUL,
        ALU_MULH,
        ALU_MULHSU,
        ALU_MULHU,
        ALU_DIV,
        ALU_DIVU,
        ALU_REM,
        ALU_REMU,
        ALU_PASS_B
    } alu_op_t;

    typedef enum logic [2:0] {
        BR_BEQ  = 3'd0,
        BR_BNE  = 3'd1,
        BR_BLT  = 3'd4,
        BR_BGE  = 3'd5,
        BR_BLTU = 3'd6,
        BR_BGEU = 3'd7
    } branch_op_t;

    typedef enum logic [1:0] {
        MEM_BYTE = 2'd0,
        MEM_HALF = 2'd1,
        MEM_WORD = 2'd2
    } mem_size_t;

    typedef enum logic [2:0] {
        CSR_NONE = 3'd0,
        CSR_RW   = 3'd1,
        CSR_RS   = 3'd2,
        CSR_RC   = 3'd3,
        CSR_RWI  = 3'd5,
        CSR_RSI  = 3'd6,
        CSR_RCI  = 3'd7
    } csr_op_t;

    typedef struct packed {
        logic                 reg_write;
        logic                 mem_read;
        logic                 mem_write;
        mem_size_t            mem_size;
        logic                 mem_unsigned;
        alu_op_t              alu_op;
        logic                 alu_a_pc;
        logic                 alu_b_imm;
        logic                 branch;
        branch_op_t           branch_op;
        logic                 jump;
        logic                 jump_reg;
        csr_op_t              csr_op;
        logic [11:0]          csr_addr;
        logic                 csr_imm;
        logic                 illegal;
        logic                 ecall;
        logic                 ebreak;
        logic                 mret;
        logic                 wfi;
        logic                 tile;
        logic [2:0]           tile_funct3;
        logic [31:0]          imm;
        logic [4:0]           rd;
        logic [4:0]           rs1;
        logic [4:0]           rs2;
    } ctrl_t;

    // CSR addresses.
    localparam logic [11:0] CSR_MSTATUS  = 12'h300;
    localparam logic [11:0] CSR_MTVEC    = 12'h305;
    localparam logic [11:0] CSR_MSCRATCH = 12'h340;
    localparam logic [11:0] CSR_MEPC     = 12'h341;
    localparam logic [11:0] CSR_MCAUSE   = 12'h342;
    localparam logic [11:0] CSR_MTVAL    = 12'h343;
    localparam logic [11:0] CSR_CYCLE    = 12'hC00;
    localparam logic [11:0] CSR_INSTRET  = 12'hC02;
    localparam logic [11:0] CSR_TILE_STATUS = 12'h7C0;

    // Global CSR offsets (AXI4-Lite window in docs/03-abi.md).
    localparam logic [11:0] CSR_ID            = 12'h000;
    localparam logic [11:0] CSR_VERSION       = 12'h004;
    localparam logic [11:0] CSR_STATUS        = 12'h008;
    localparam logic [11:0] CSR_CONTROL       = 12'h00C;
    localparam logic [11:0] CSR_IRQ_STATUS    = 12'h010;
    localparam logic [11:0] CSR_IRQ_ENABLE    = 12'h014;
    localparam logic [11:0] CSR_QUEUE_BASE    = 12'h018;
    localparam logic [11:0] CSR_QUEUE_SIZE    = 12'h01C;
    localparam logic [11:0] CSR_QUEUE_HEAD    = 12'h020;
    localparam logic [11:0] CSR_QUEUE_TAIL    = 12'h024;
    localparam logic [11:0] CSR_COMPLETE_BASE = 12'h028;
    localparam logic [11:0] CSR_COMPLETE_SIZE = 12'h02C;
    localparam logic [11:0] CSR_COMPLETE_HEAD = 12'h030;
    localparam logic [11:0] CSR_COMPLETE_TAIL = 12'h034;
    localparam logic [11:0] CSR_CYCLE_LO      = 12'h038;
    localparam logic [11:0] CSR_CYCLE_HI      = 12'h03C;
    localparam logic [11:0] CSR_ERROR_CODE    = 12'h040;
    localparam logic [11:0] CSR_ERROR_ADDR    = 12'h044;
    localparam logic [11:0] CSR_ABORT         = 12'h048;
    localparam logic [11:0] CSR_SCRATCH_BASE  = 12'h04C;
    localparam logic [11:0] CSR_SCRATCH_SIZE  = 12'h050;
    localparam logic [11:0] CSR_CAP0          = 12'h054;
    localparam logic [11:0] CSR_CAP1          = 12'h058;
    localparam logic [11:0] CSR_SOFT_RESET    = 12'h05C;

    // Trap causes.
    localparam logic [31:0] CAUSE_ILLEGAL       = 32'd2;
    localparam logic [31:0] CAUSE_BREAKPOINT    = 32'd3;
    localparam logic [31:0] CAUSE_LOAD_MISALIGN = 32'd4;
    localparam logic [31:0] CAUSE_LOAD_FAULT    = 32'd5;
    localparam logic [31:0] CAUSE_STORE_MISALIGN= 32'd6;
    localparam logic [31:0] CAUSE_STORE_FAULT   = 32'd7;
    localparam logic [31:0] CAUSE_ECALL_M       = 32'd11;


`endif
