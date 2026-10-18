/*
 * CloverNPU shared ABI definitions.
 *
 * This header is the single source of truth for descriptor, command,
 * kernel-argument and image layouts.  It must stay in sync with
 * docs/03-abi.md.  All multi-byte fields are little-endian.
 */
#ifndef CLOVER_ABI_H
#define CLOVER_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CLOVER_ABI_VERSION_MAJOR 0
#define CLOVER_ABI_VERSION_MINOR 1

#define CLOVER_ALIGN(n) __attribute__((aligned(n)))

/* Magic values are chosen so that a little-endian dump spells the tag. */
#define CLOVER_MMA_MAGIC 0x31414D4Du /* "MMA1" */
#define CLOVER_VEC_MAGIC 0x31434556u /* "VEC1" */
#define CLOVER_DMA_MAGIC 0x31414D44u /* "DMA1" */
#define CLOVER_KARG_MAGIC 0x4752414Bu /* "KARG" */
#define CLOVER_CLOV_MAGIC 0x564F4C43u /* "CLOV" */
#define CLOVER_ID_MAGIC 0x564F4C43u   /* "CLOV" */

enum clover_dtype {
    CLOVER_DTYPE_FP32 = 0,
    CLOVER_DTYPE_FP16 = 1,
    CLOVER_DTYPE_BF16 = 2,
    CLOVER_DTYPE_FP8_E4M3 = 3,
    CLOVER_DTYPE_FP8_E5M2 = 4,
    CLOVER_DTYPE_FP4_E2M1 = 5,
    CLOVER_DTYPE_INT32 = 6,
    CLOVER_DTYPE_RESERVED = 7
};

/* tile.wait mask */
#define CLOVER_WAIT_MMA 0x1u
#define CLOVER_WAIT_VEC 0x2u
#define CLOVER_WAIT_DMA 0x4u
#define CLOVER_WAIT_ALL 0x8u

/* clover_wait() status */
#define CLOVER_TILE_OK 0u
#define CLOVER_TILE_TIMEOUT 1u
#define CLOVER_TILE_ERROR 2u

/* MMA flags */
#define CLOVER_MMA_ACCUMULATE 0x00000001u
#define CLOVER_MMA_TRANSPOSE_A 0x00000002u
#define CLOVER_MMA_TRANSPOSE_B 0x00000004u
#define CLOVER_MMA_RELU 0x00000008u
#define CLOVER_MMA_SATURATE 0x00000010u
#define CLOVER_MMA_A_SCALE_EN 0x00004000u
#define CLOVER_MMA_B_SCALE_EN 0x00008000u
#define CLOVER_MMA_A_SCALE_PER_CHANNEL 0x00010000u
#define CLOVER_MMA_B_SCALE_PER_CHANNEL 0x00020000u
#define CLOVER_MMA_POST_SCALE_EN 0x00040000u
#define CLOVER_MMA_POST_BIAS_EN 0x00080000u

#define CLOVER_MMA_A_TYPE_SHIFT 5
#define CLOVER_MMA_B_TYPE_SHIFT 8
#define CLOVER_MMA_C_TYPE_SHIFT 11
#define CLOVER_MMA_A_TYPE_MASK (0x7u << CLOVER_MMA_A_TYPE_SHIFT)
#define CLOVER_MMA_B_TYPE_MASK (0x7u << CLOVER_MMA_B_TYPE_SHIFT)
#define CLOVER_MMA_C_TYPE_MASK (0x7u << CLOVER_MMA_C_TYPE_SHIFT)

/* VEC flags */
#define CLOVER_VEC_OP_MASK 0x0000000Fu
#define CLOVER_VEC_SATURATE 0x00000010u
#define CLOVER_VEC_RELU 0x00000020u
#define CLOVER_VEC_BROADCAST_A 0x00000040u
#define CLOVER_VEC_BROADCAST_B 0x00000080u

enum clover_vec_op {
    CLOVER_VEC_OP_ADD = 0,
    CLOVER_VEC_OP_SUB = 1,
    CLOVER_VEC_OP_MUL = 2,
    CLOVER_VEC_OP_MAX = 3,
    CLOVER_VEC_OP_MIN = 4,
    CLOVER_VEC_OP_RELU = 5,
    CLOVER_VEC_OP_ABS = 6,
    CLOVER_VEC_OP_NEG = 7,
    CLOVER_VEC_OP_FMA = 8,
    CLOVER_VEC_OP_CVT = 9,
    CLOVER_VEC_OP_REDUCE_SUM = 10,
    CLOVER_VEC_OP_REDUCE_MAX = 11
};

/* DMA direction */
#define CLOVER_DMA_DRAM_TO_SP 0u
#define CLOVER_DMA_SP_TO_DRAM 1u
#define CLOVER_DMA_SP_TO_SP 2u
#define CLOVER_DMA_DRAM_TO_DRAM 3u

struct clover_mma_desc {
    uint32_t magic;
    uint32_t flags;
    uint32_t m;
    uint32_t n;
    uint32_t k;
    uint32_t a_addr;
    uint32_t b_addr;
    uint32_t c_addr;
    uint32_t a_stride;
    uint32_t b_stride;
    uint32_t c_stride;
    uint32_t scale_a_addr;
    uint32_t scale_b_addr;
    uint32_t post_scale;
    uint32_t post_bias;
    uint32_t reserved;
} CLOVER_ALIGN(64);

struct clover_vec_desc {
    uint32_t magic;
    uint32_t flags;
    uint32_t n;
    uint32_t a_addr;
    uint32_t b_addr;
    uint32_t c_addr;
    uint32_t a_stride;
    uint32_t b_stride;
    uint32_t c_stride;
    uint32_t src_type;
    uint32_t dst_type;
    uint32_t scale_a;
    uint32_t scale_b;
    uint32_t post_scale;
    uint32_t post_bias;
    uint32_t reserved;
} CLOVER_ALIGN(64);

struct clover_dma_desc {
    uint32_t magic;
    uint32_t flags;
    uint32_t src_addr;
    uint32_t dst_addr;
    uint32_t bytes_per_row;
    uint32_t num_rows;
    uint32_t src_stride;
    uint32_t dst_stride;
    uint32_t reserved0;
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3;
    uint32_t reserved4;
    uint32_t reserved5;
    uint32_t reserved6;
    uint32_t reserved7;
} CLOVER_ALIGN(64);

/* 64-byte command. */
struct clover_cmd {
    uint32_t opcode;
    uint32_t flags;
    uint32_t src;
    uint32_t dst;
    uint32_t dim0;
    uint32_t dim1;
    uint32_t dim2;
    uint32_t stride0;
    uint32_t stride1;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t seq;
    uint32_t reserved0;
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t crc;
} CLOVER_ALIGN(64);

enum clover_cmd_opcode {
    CLOVER_CMD_NOP = 0x01,
    CLOVER_CMD_DMA_2D = 0x02,
    CLOVER_CMD_RUN = 0x03,
    CLOVER_CMD_WAIT = 0x04,
    CLOVER_CMD_SIGNAL = 0x05,
    CLOVER_CMD_WRITE_REG = 0x06,
    CLOVER_CMD_PREFETCH = 0x07,
    CLOVER_CMD_END = 0x08,
    CLOVER_CMD_BARRIER = 0x10,
    CLOVER_CMD_DEBUG = 0x11
};

struct clover_completion {
    uint32_t seq;
    uint32_t status;
    uint32_t cycles;
    uint32_t error_code;
};

struct clover_tensor {
    uint32_t addr;
    uint32_t dtype;
    uint32_t ndim;
    uint32_t flags;
    uint32_t dim[4];
    uint32_t stride[4];
    uint32_t scale_addr;
    uint32_t reserved;
};

struct clover_kernel_args {
    uint32_t magic;
    uint32_t kernel_id;
    uint32_t n_tensors;
    uint32_t n_scalars;
    uint32_t scalar[16];
    struct clover_tensor tensors[16];
    uint32_t reserved[12];
} CLOVER_ALIGN(64);

struct clov_header {
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t flags;
    uint32_t entry;
    uint32_t load_base;
    uint32_t text_off;
    uint32_t text_size;
    uint32_t rodata_off;
    uint32_t rodata_size;
    uint32_t data_off;
    uint32_t data_size;
    uint32_t bss_size;
    uint32_t symtab_off;
    uint32_t symtab_size;
    uint32_t checksum;
};

#ifdef __cplusplus
}
#endif

#endif /* CLOVER_ABI_H */
