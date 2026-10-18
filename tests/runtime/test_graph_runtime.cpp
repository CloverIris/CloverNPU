#include "clover/assembler.h"
#include "clover/clover.h"
#include "clover/compiler.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kGemmBase = 0x1000'0000u;
constexpr uint32_t kAddBase = 0x1002'0000u;

std::string read_file(const std::string &path)
{
    std::ifstream in(path);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool compile_image(const std::string &path, const std::string &entry,
                   uint32_t base, std::vector<uint8_t> &image,
                   uint32_t &entry_address)
{
    const std::string source = read_file(path);
    if (source.empty()) {
        std::cerr << "cannot read " << path << "\n";
        return false;
    }
    clover::CompileOptions compile_options;
    compile_options.emit_start = false;
    const clover::CompileResult compiled =
        clover::compile_to_assembly(source, path, compile_options);
    if (!compiled.ok) {
        std::cerr << compiled.error << "\n";
        return false;
    }
    clover::AsmOptions asm_options;
    asm_options.base = base;
    asm_options.entry_symbol = entry;
    clover::AsmResult assembled;
    std::string error;
    if (!clover::assemble(compiled.assembly, path + ".s", asm_options,
                          assembled, error)) {
        std::cerr << error << "\n";
        return false;
    }
    image = assembled.image;
    entry_address = assembled.entry;
    return true;
}

bool ok(clover_status status, const char *what)
{
    if (status != CLOVER_OK) {
        std::cerr << what << ": " << clover_strerror(status) << "\n";
        return false;
    }
    return true;
}

uint16_t f32_to_fp16(float value)
{
    _Float16 half = static_cast<_Float16>(value);
    uint16_t bits = 0;
    std::memcpy(&bits, &half, sizeof(bits));
    return bits;
}

clover_tensor make_tensor(clover_buffer *buffer, uint32_t dtype,
                          std::initializer_list<uint32_t> dims,
                          uint32_t element_bytes)
{
    clover_tensor tensor{};
    tensor.addr = clover_buffer_addr(buffer);
    tensor.dtype = dtype;
    tensor.ndim = static_cast<uint32_t>(dims.size());
    uint32_t i = 0;
    for (uint32_t dim : dims) {
        tensor.dim[i++] = dim;
    }
    uint32_t stride = element_bytes;
    for (int index = static_cast<int>(dims.size()) - 1; index >= 0; --index) {
        tensor.stride[index] = stride;
        stride *= tensor.dim[index];
    }
    return tensor;
}

bool run_shape(clover_device *dev, clover_kernel *gemm_kernel,
               clover_kernel *add_kernel, uint32_t m, uint32_t n, uint32_t k)
{
    const size_t a_bytes = static_cast<size_t>(m) * k * 2;
    const size_t b_bytes = static_cast<size_t>(k) * n * 2;
    const size_t c_bytes = static_cast<size_t>(m) * n * 4;
    const size_t bias_bytes = static_cast<size_t>(m) * n * 4;
    const size_t out_bytes = static_cast<size_t>(m) * n * 4;

    clover_buffer *a = nullptr;
    clover_buffer *b = nullptr;
    clover_buffer *c = nullptr;
    clover_buffer *bias = nullptr;
    clover_buffer *out = nullptr;
    if (!ok(clover_alloc(dev, a_bytes, 64, 0, &a), "alloc A") ||
        !ok(clover_alloc(dev, b_bytes, 64, 0, &b), "alloc B") ||
        !ok(clover_alloc(dev, c_bytes, 64, 0, &c), "alloc C") ||
        !ok(clover_alloc(dev, bias_bytes, 64, 0, &bias), "alloc bias") ||
        !ok(clover_alloc(dev, out_bytes, 64, 0, &out), "alloc out")) {
        return false;
    }

    auto *a_host = static_cast<uint16_t *>(clover_buffer_ptr(a));
    auto *b_host = static_cast<uint16_t *>(clover_buffer_ptr(b));
    for (uint32_t i = 0; i < m * k; i++) {
        a_host[i] = f32_to_fp16(static_cast<float>((i * 7 + 3) % 16));
    }
    for (uint32_t i = 0; i < k * n; i++) {
        b_host[i] = f32_to_fp16(static_cast<float>((i * 5 + 1) % 16));
    }
    auto *bias_host = static_cast<float *>(clover_buffer_ptr(bias));
    for (uint32_t i = 0; i < m * n; i++) {
        bias_host[i] = 1.0f;
    }

    if (!ok(clover_copy_h2d(dev, a, a_host, a_bytes), "copy A") ||
        !ok(clover_copy_h2d(dev, b, b_host, b_bytes), "copy B") ||
        !ok(clover_copy_h2d(dev, bias, bias_host, bias_bytes), "copy bias")) {
        return false;
    }

    const clover_tensor a_tensor = make_tensor(a, 1, {m, k}, 2);
    const clover_tensor b_tensor = make_tensor(b, 1, {k, n}, 2);
    const clover_tensor c_tensor = make_tensor(c, 0, {m, n}, 4);
    const clover_tensor bias_tensor = make_tensor(bias, 0, {m * n}, 4);
    const clover_tensor out_tensor = make_tensor(out, 0, {m, n}, 4);

    const clover_tensor gemm_inputs[2] = {a_tensor, b_tensor};
    clover_graph *graph = nullptr;
    if (!ok(clover_graph_create(dev, &graph), "graph create") ||
        !ok(clover_graph_add_op(graph, gemm_kernel, "gemm", gemm_inputs, 2,
                                &c_tensor, 1), "add gemm")) {
        return false;
    }
    const clover_tensor add_inputs[2] = {c_tensor, bias_tensor};
    if (!ok(clover_graph_add_op(graph, add_kernel, "add", add_inputs, 2,
                                &out_tensor, 1), "add vec")) {
        return false;
    }
    if (!ok(clover_graph_run(graph), "graph run")) {
        return false;
    }
    clover_graph_destroy(graph);

    std::vector<float> result(m * n, 0.0f);
    if (!ok(clover_copy_d2h(dev, result.data(), out, out_bytes), "copy out")) {
        return false;
    }

    for (uint32_t mi = 0; mi < m; mi++) {
        for (uint32_t ni = 0; ni < n; ni++) {
            float expected = 1.0f;
            for (uint32_t ki = 0; ki < k; ki++) {
                const float av = static_cast<float>(((mi * k + ki) * 7 + 3) % 16);
                const float bv = static_cast<float>(((ki * n + ni) * 5 + 1) % 16);
                expected += av * bv;
            }
            const float got = result[mi * n + ni];
            if (got != expected) {
                std::cerr << "mismatch at (" << mi << "," << ni << "): "
                          << got << " != " << expected << "\n";
                return false;
            }
        }
    }

    clover_free(a);
    clover_free(b);
    clover_free(c);
    clover_free(bias);
    clover_free(out);
    std::cout << "graph " << m << "x" << n << "x" << k << " passed\n";
    return true;
}

}  // namespace

int main()
{
    const std::string root = CLOVER_SOURCE_DIR;
    std::vector<uint8_t> gemm_image;
    std::vector<uint8_t> add_image;
    uint32_t gemm_entry = 0;
    uint32_t add_entry = 0;
    if (!compile_image(root + "/kernels/gemm.c", "gemm", kGemmBase,
                       gemm_image, gemm_entry) ||
        !compile_image(root + "/kernels/add.c", "add", kAddBase,
                       add_image, add_entry)) {
        return 1;
    }

    clover_device *dev = nullptr;
    if (!ok(clover_open_sim(nullptr, &dev), "open sim")) {
        return 1;
    }
    clover_kernel *gemm_kernel = nullptr;
    clover_kernel *add_kernel = nullptr;
    if (!ok(clover_load_image(dev, gemm_image.data(), gemm_image.size(),
                              kGemmBase, gemm_entry, &gemm_kernel),
            "load gemm") ||
        !ok(clover_load_image(dev, add_image.data(), add_image.size(),
                              kAddBase, add_entry, &add_kernel),
            "load add")) {
        clover_close(dev);
        return 1;
    }

    const bool passed = run_shape(dev, gemm_kernel, add_kernel, 16, 16, 16) &&
                        run_shape(dev, gemm_kernel, add_kernel, 17, 19, 23);

    clover_kernel_free(gemm_kernel);
    clover_kernel_free(add_kernel);
    clover_close(dev);
    if (!passed) {
        return 1;
    }
    std::cout << "graph runtime tests passed\n";
    return 0;
}
