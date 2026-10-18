#include "clover/clover.h"
#include "clover/iss.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

struct clover_kernel {
    uint32_t base = 0;
    uint32_t entry = 0;
    std::vector<uint8_t> image;
};

struct clover_buffer {
    uint32_t addr = 0;
    size_t size = 0;
    uint32_t flags = 0;
    std::vector<uint8_t> host;
};

struct clover_graph;

struct clover_node {
    clover_kernel *kernel = nullptr;
    std::string op;
    std::vector<clover_tensor> inputs;
    std::vector<clover_tensor> outputs;
};

struct clover_graph {
    clover_device *device = nullptr;
    std::vector<clover_node> nodes;
};

struct clover_device {
    clover::Iss iss;
    uint32_t alloc_next = 0x0001'0000u;
    static constexpr uint32_t kAllocLimit = 0x0002'8000u;
};

namespace {

uint32_t align_up(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

bool put32(clover::Iss &iss, uint32_t address, uint32_t value)
{
    return iss.write_memory(address, 4, value);
}

void set_error(char *dst, size_t size, const std::string &text)
{
    if (!dst || size == 0) {
        return;
    }
    std::strncpy(dst, text.c_str(), size - 1);
    dst[size - 1] = '\0';
}

}  // namespace

const char *clover_strerror(clover_status status)
{
    switch (status) {
    case CLOVER_OK: return "ok";
    case CLOVER_ERR_INVALID: return "invalid argument";
    case CLOVER_ERR_NOMEM: return "out of memory";
    case CLOVER_ERR_IO: return "io error";
    case CLOVER_ERR_TIMEOUT: return "timeout";
    case CLOVER_ERR_DEVICE: return "device error";
    case CLOVER_ERR_UNSUPPORTED: return "unsupported";
    }
    return "unknown";
}

clover_status clover_open_sim(const char *, clover_device **out)
{
    if (!out) {
        return CLOVER_ERR_INVALID;
    }
    *out = new clover_device();
    return CLOVER_OK;
}

void clover_close(clover_device *dev)
{
    delete dev;
}

clover_status clover_alloc(clover_device *dev, size_t bytes, size_t align,
                           uint32_t flags, clover_buffer **out)
{
    if (!dev || !out || bytes == 0) {
        return CLOVER_ERR_INVALID;
    }
    if (align == 0) {
        align = 64;
    }
    const uint32_t address = align_up(dev->alloc_next, static_cast<uint32_t>(align));
    if (address + bytes > clover_device::kAllocLimit) {
        return CLOVER_ERR_NOMEM;
    }
    auto *buffer = new clover_buffer();
    buffer->addr = address;
    buffer->size = bytes;
    buffer->flags = flags;
    buffer->host.assign(bytes, 0);
    dev->alloc_next = address + static_cast<uint32_t>(bytes);
    *out = buffer;
    return CLOVER_OK;
}

void clover_free(clover_buffer *buf)
{
    delete buf;
}

void *clover_buffer_ptr(clover_buffer *buf)
{
    return buf ? buf->host.data() : nullptr;
}

uint32_t clover_buffer_addr(clover_buffer *buf)
{
    return buf ? buf->addr : 0;
}

size_t clover_buffer_size(clover_buffer *buf)
{
    return buf ? buf->size : 0;
}

clover_status clover_copy_h2d(clover_device *dev, clover_buffer *dst,
                              const void *src, size_t bytes)
{
    if (!dev || !dst || !src || bytes > dst->size) {
        return CLOVER_ERR_INVALID;
    }
    std::memcpy(dst->host.data(), src, bytes);
    for (size_t i = 0; i < bytes; i++) {
        if (!dev->iss.write_memory(dst->addr + static_cast<uint32_t>(i), 1,
                                   dst->host[i])) {
            return CLOVER_ERR_IO;
        }
    }
    return CLOVER_OK;
}

clover_status clover_copy_d2h(clover_device *dev, void *dst,
                              clover_buffer *src, size_t bytes)
{
    if (!dev || !dst || !src || bytes > src->size) {
        return CLOVER_ERR_INVALID;
    }
    for (size_t i = 0; i < bytes; i++) {
        uint32_t value = 0;
        if (!dev->iss.read_memory(src->addr + static_cast<uint32_t>(i), 1, value)) {
            return CLOVER_ERR_IO;
        }
        static_cast<uint8_t *>(dst)[i] = static_cast<uint8_t>(value);
    }
    return CLOVER_OK;
}

clover_status clover_load(clover_device *, const void *, size_t,
                          clover_kernel **)
{
    return CLOVER_ERR_UNSUPPORTED;
}

clover_status clover_load_image(clover_device *dev, const void *image,
                                size_t bytes, uint32_t load_base,
                                uint32_t entry, clover_kernel **out)
{
    if (!dev || !image || bytes == 0 || !out) {
        return CLOVER_ERR_INVALID;
    }
    auto *kernel = new clover_kernel();
    kernel->base = load_base;
    kernel->entry = entry;
    kernel->image.assign(static_cast<const uint8_t *>(image),
                         static_cast<const uint8_t *>(image) + bytes);
    std::string error;
    if (!dev->iss.load_image_at(load_base, kernel->image, error)) {
        delete kernel;
        return CLOVER_ERR_IO;
    }
    *out = kernel;
    return CLOVER_OK;
}

void clover_kernel_free(clover_kernel *kernel)
{
    delete kernel;
}

clover_status clover_submit(clover_device *, const void *, size_t)
{
    return CLOVER_ERR_UNSUPPORTED;
}

clover_status clover_wait(clover_device *, uint32_t, uint32_t)
{
    return CLOVER_ERR_UNSUPPORTED;
}

clover_status clover_graph_create(clover_device *dev, clover_graph **out)
{
    if (!dev || !out) {
        return CLOVER_ERR_INVALID;
    }
    auto *graph = new clover_graph();
    graph->device = dev;
    *out = graph;
    return CLOVER_OK;
}

clover_status clover_graph_add(clover_graph *, clover_kernel *,
                               const char *const *, uint32_t,
                               const char *const *, uint32_t, const char *)
{
    return CLOVER_ERR_UNSUPPORTED;
}

clover_status clover_graph_add_op(clover_graph *graph, clover_kernel *kernel,
                                  const char *op,
                                  const clover_tensor *inputs, uint32_t n_inputs,
                                  const clover_tensor *outputs, uint32_t n_outputs)
{
    if (!graph || !kernel || !op || (n_inputs && !inputs) ||
        (n_outputs && !outputs)) {
        return CLOVER_ERR_INVALID;
    }
    const std::string op_name(op);
    if (op_name != "gemm" && op_name != "add") {
        return CLOVER_ERR_UNSUPPORTED;
    }
    clover_node node;
    node.kernel = kernel;
    node.op = op_name;
    node.inputs.assign(inputs, inputs + n_inputs);
    node.outputs.assign(outputs, outputs + n_outputs);
    graph->nodes.push_back(node);
    return CLOVER_OK;
}

clover_status clover_graph_run(clover_graph *graph)
{
    if (!graph || !graph->device) {
        return CLOVER_ERR_INVALID;
    }
    clover::Iss &iss = graph->device->iss;

    for (const clover_node &node : graph->nodes) {
        if (!node.kernel) {
            return CLOVER_ERR_INVALID;
        }
        if (node.op == "gemm") {
            if (node.inputs.size() < 2 || node.outputs.empty()) {
                return CLOVER_ERR_INVALID;
            }
            const clover_tensor &a = node.inputs[0];
            const clover_tensor &b = node.inputs[1];
            const clover_tensor &c = node.outputs[0];
            const uint32_t m = a.dim[0];
            const uint32_t k = a.dim[1];
            const uint32_t n = b.dim[1];
            const uint32_t args[9] = {m, n, k, a.addr, b.addr, c.addr,
                                      a.stride[0], b.stride[0], c.stride[0]};
            for (uint32_t i = 0; i < 9; i++) {
                if (!put32(iss, 0x0003'8000u + i * 4u, args[i])) {
                    return CLOVER_ERR_IO;
                }
            }
        } else if (node.op == "add") {
            if (node.inputs.size() < 2 || node.outputs.empty()) {
                return CLOVER_ERR_INVALID;
            }
            const clover_tensor &a = node.inputs[0];
            const clover_tensor &b = node.inputs[1];
            const clover_tensor &c = node.outputs[0];
            uint32_t count = 1;
            for (uint32_t d = 0; d < a.ndim; d++) {
                count *= a.dim[d];
            }
            const uint32_t args[4] = {count, a.addr, b.addr, c.addr};
            for (uint32_t i = 0; i < 4; i++) {
                if (!put32(iss, 0x0003'8000u + i * 4u, args[i])) {
                    return CLOVER_ERR_IO;
                }
            }
        } else {
            return CLOVER_ERR_UNSUPPORTED;
        }

        iss.boot(node.kernel->entry, 0x0003'FFF0u, 0x0003'8000u);
        const clover::IssResult result = iss.run();
        if (!result.halted || result.faulted || !result.error.empty() ||
            result.exit_code != 0) {
            std::cerr << "node " << node.op << " halted=" << result.halted
                      << " faulted=" << result.faulted
                      << " exit=" << result.exit_code
                      << " error=" << result.error << "\n";
            return CLOVER_ERR_DEVICE;
        }
    }
    return CLOVER_OK;
}

clover_status clover_graph_execute(clover_graph *, const clover_tensor *,
                                   uint32_t, clover_tensor *, uint32_t)
{
    return CLOVER_ERR_UNSUPPORTED;
}

void clover_graph_destroy(clover_graph *graph)
{
    delete graph;
}
