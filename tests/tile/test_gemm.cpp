#include "clover/assembler.h"
#include "clover/compiler.h"
#include "clover/iss.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kBase = 0x1000'0000u;
constexpr uint32_t kStackTop = 0x0003'FFF0u;
constexpr uint32_t kArgsAddr = 0x0003'8000u;
constexpr uint32_t kABase = 0x0001'0000u;
constexpr uint32_t kBBase = 0x0002'0000u;
constexpr uint32_t kCBase = 0x0003'0000u;
constexpr uint32_t kDFixed = 0x0002'8000u;

std::string read_file(const std::string &path)
{
    std::ifstream in(path);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

float bits_to_f32(uint32_t bits)
{
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

uint16_t f32_to_fp16(float value)
{
    _Float16 half = static_cast<_Float16>(value);
    uint16_t bits = 0;
    std::memcpy(&bits, &half, sizeof(bits));
    return bits;
}

void put16(clover::Iss &iss, uint32_t address, uint16_t value)
{
    if (!iss.write_memory(address, 2, value)) {
        std::cerr << "write16 failed at 0x" << std::hex << address << "\n";
        std::exit(2);
    }
}

void put32(clover::Iss &iss, uint32_t address, uint32_t value)
{
    if (!iss.write_memory(address, 4, value)) {
        std::cerr << "write32 failed at 0x" << std::hex << address << "\n";
        std::exit(2);
    }
}

uint32_t get32(clover::Iss &iss, uint32_t address)
{
    uint32_t value = 0;
    if (!iss.read_memory(address, 4, value)) {
        std::cerr << "read32 failed at 0x" << std::hex << address << "\n";
        std::exit(2);
    }
    return value;
}

bool run_shape(clover::Iss &iss, uint32_t entry, uint32_t m, uint32_t n,
               uint32_t k, clover::IssResult &result)
{
    const uint32_t lda = k * 2;
    const uint32_t ldb = n * 2;
    const uint32_t ldc = n * 4;

    for (uint32_t i = 0; i < m * k; i++) {
        put16(iss, kABase + i * 2, f32_to_fp16(static_cast<float>((i * 7 + 3) % 16)));
    }
    for (uint32_t i = 0; i < k * n; i++) {
        put16(iss, kBBase + i * 2, f32_to_fp16(static_cast<float>((i * 5 + 1) % 16)));
    }
    for (uint32_t i = 0; i < m * n; i++) {
        put32(iss, kCBase + i * 4, 0);
    }

    put32(iss, kArgsAddr + 0, m);
    put32(iss, kArgsAddr + 4, n);
    put32(iss, kArgsAddr + 8, k);
    put32(iss, kArgsAddr + 12, kABase);
    put32(iss, kArgsAddr + 16, kBBase);
    put32(iss, kArgsAddr + 20, kCBase);
    put32(iss, kArgsAddr + 24, lda);
    put32(iss, kArgsAddr + 28, ldb);
    put32(iss, kArgsAddr + 32, ldc);

    (void)kDFixed;
    iss.boot(entry, kStackTop, kArgsAddr);
    result = iss.run();
    if (!result.halted || result.faulted || !result.error.empty()) {
        std::cerr << "ISS failed: " << result.error << "\n";
        return false;
    }

    for (uint32_t mi = 0; mi < m; mi++) {
        for (uint32_t ni = 0; ni < n; ni++) {
            float expected = 0.0f;
            for (uint32_t ki = 0; ki < k; ki++) {
                const uint32_t av = mi * k + ki;
                const uint32_t bv = ki * n + ni;
                const float a = static_cast<float>((av * 7 + 3) % 16);
                const float b = static_cast<float>((bv * 5 + 1) % 16);
                expected += a * b;
            }
            const float got = bits_to_f32(get32(iss, kCBase + (mi * n + ni) * 4));
            if (got != expected) {
                std::cerr << "Mismatch at (" << mi << "," << ni << "): "
                          << got << " != " << expected << "\n";
                return false;
            }
        }
    }
    return true;
}

}  // namespace

int main()
{
    const std::string source_path = std::string(CLOVER_SOURCE_DIR) + "/kernels/gemm.c";
    const std::string source = read_file(source_path);
    if (source.empty()) {
        std::cerr << "cannot read " << source_path << "\n";
        return 1;
    }

    clover::CompileOptions options;
    options.emit_start = false;
    clover::CompileResult compiled =
        clover::compile_to_assembly(source, source_path, options);
    if (!compiled.ok) {
        std::cerr << compiled.error << "\n";
        return 1;
    }

    clover::AsmOptions asm_options;
    asm_options.base = kBase;
    asm_options.entry_symbol = "gemm";
    clover::AsmResult image;
    std::string error;
    if (!clover::assemble(compiled.assembly, source_path + ".s", asm_options,
                          image, error)) {
        std::cerr << error << "\n";
        return 1;
    }

    clover::IssOptions iss_options;
    iss_options.load_base = kBase;
    iss_options.entry = kBase;
    iss_options.stack_top = kStackTop;
    iss_options.max_steps = 5'000'000;
    clover::Iss iss(iss_options);
    if (!iss.load_image(image.image, error)) {
        std::cerr << error << "\n";
        return 1;
    }

    clover::IssResult result;
    if (!run_shape(iss, image.entry, 16, 16, 16, result)) {
        return 1;
    }
    if (!run_shape(iss, image.entry, 17, 19, 23, result)) {
        return 1;
    }
    std::cout << "GEMM ISS tests passed (16x16x16, 17x19x23)\n";
    return 0;
}
