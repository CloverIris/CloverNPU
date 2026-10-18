/*
 * CloverNPU host runtime API.
 *
 * This header is compiled by the host toolchain, not by cloverc.
 */
#ifndef CLOVER_H
#define CLOVER_H

#include <stddef.h>
#include <stdint.h>

#include "clover_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CLOVER_VERSION_MAJOR 0
#define CLOVER_VERSION_MINOR 1
#define CLOVER_VERSION_PATCH 0
#define CLOVER_VERSION_STRING "0.1.0"

typedef struct clover_device clover_device;
typedef struct clover_buffer clover_buffer;
typedef struct clover_kernel clover_kernel;
typedef struct clover_graph clover_graph;

typedef enum clover_status {
    CLOVER_OK = 0,
    CLOVER_ERR_INVALID = 1,
    CLOVER_ERR_NOMEM = 2,
    CLOVER_ERR_IO = 3,
    CLOVER_ERR_TIMEOUT = 4,
    CLOVER_ERR_DEVICE = 5,
    CLOVER_ERR_UNSUPPORTED = 6
} clover_status;

/* Buffer flags */
#define CLOVER_BUF_SCRATCHPAD 0x00000001u
#define CLOVER_BUF_UNCACHED 0x00000002u

const char *clover_strerror(clover_status status);

/* Device lifecycle.  "sim" selects the functional/Verilator backend. */
clover_status clover_open_sim(const char *config, clover_device **out);
void clover_close(clover_device *dev);

/* Device memory.  Allocations are 64-byte aligned unless align says otherwise. */
clover_status clover_alloc(clover_device *dev, size_t bytes, size_t align,
                           uint32_t flags, clover_buffer **out);
void clover_free(clover_buffer *buf);
void *clover_buffer_ptr(clover_buffer *buf);
uint32_t clover_buffer_addr(clover_buffer *buf);
size_t clover_buffer_size(clover_buffer *buf);

clover_status clover_copy_h2d(clover_device *dev, clover_buffer *dst,
                              const void *src, size_t bytes);
clover_status clover_copy_d2h(clover_device *dev, void *dst,
                              clover_buffer *src, size_t bytes);

/* Kernel image loading.  The simulator backend accepts a flat image plus an
 * explicit load base and entry address.  CLOV parsing is a later milestone. */
clover_status clover_load(clover_device *dev, const void *clov, size_t bytes,
                          clover_kernel **out);
clover_status clover_load_image(clover_device *dev, const void *image,
                                size_t bytes, uint32_t load_base,
                                uint32_t entry, clover_kernel **out);
void clover_kernel_free(clover_kernel *kernel);

/* Low-level command submission.  cmds points at an array of clover_cmd. */
clover_status clover_submit(clover_device *dev, const void *cmds, size_t n_cmds);
clover_status clover_wait(clover_device *dev, uint32_t seq, uint32_t timeout_ms);

/* Graph API.  inputs/outputs are matched by name and order. */
clover_status clover_graph_create(clover_device *dev, clover_graph **out);
clover_status clover_graph_add(clover_graph *graph, clover_kernel *kernel,
                               const char *const *inputs, uint32_t n_inputs,
                               const char *const *outputs, uint32_t n_outputs,
                               const char *attrs_json);
clover_status clover_graph_execute(clover_graph *graph,
                                   const struct clover_tensor *inputs,
                                   uint32_t n_inputs,
                                   struct clover_tensor *outputs,
                                   uint32_t n_outputs);
/* Direct-tensor graph API used by the simulator backend.
 * `op` is "gemm" or "add"; tensors carry device addresses and shapes.
 * The runtime executes nodes in insertion order. */
clover_status clover_graph_add_op(clover_graph *graph, clover_kernel *kernel,
                                  const char *op,
                                  const struct clover_tensor *inputs,
                                  uint32_t n_inputs,
                                  const struct clover_tensor *outputs,
                                  uint32_t n_outputs);
clover_status clover_graph_run(clover_graph *graph);

void clover_graph_destroy(clover_graph *graph);

#ifdef __cplusplus
}
#endif

#endif /* CLOVER_H */
