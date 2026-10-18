/*
 * Clover C kernel-side ABI: descriptor helpers and tile intrinsics.
 *
 * This header is compiled by cloverc.  The __clover_* names are compiler
 * builtins that lower to Clover-S TILE instructions.
 */
#ifndef CLOVER_TILE_H
#define CLOVER_TILE_H

#include "clover_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

void __clover_mma(const struct clover_mma_desc *desc);
void __clover_vec(const struct clover_vec_desc *desc);
void __clover_dma(const struct clover_dma_desc *desc);

uint32_t __clover_wait(uint32_t mask);
uint32_t __clover_wait_timeout(uint32_t mask, uint32_t timeout);
void __clover_fence(void);
void __clover_cache(uint32_t op, uint32_t addr);

uint32_t __clover_csr_read(uint32_t csr);
void __clover_csr_write(uint32_t csr, uint32_t value);
uint32_t __clover_cycle(void);
void __clover_break(void);

/* Common one-block helper.  Returns CLOVER_TILE_* status. */
static inline uint32_t clover_mma_block(const struct clover_mma_desc *desc)
{
    __clover_mma(desc);
    return __clover_wait(CLOVER_WAIT_MMA);
}

static inline uint32_t clover_vec_block(const struct clover_vec_desc *desc)
{
    __clover_vec(desc);
    return __clover_wait(CLOVER_WAIT_VEC);
}

static inline uint32_t clover_dma_block(const struct clover_dma_desc *desc)
{
    __clover_dma(desc);
    return __clover_wait(CLOVER_WAIT_DMA);
}

#ifdef __cplusplus
}
#endif

#endif /* CLOVER_TILE_H */
