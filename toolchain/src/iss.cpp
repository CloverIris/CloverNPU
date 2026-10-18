#include "clover/iss.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>

#include "clover/isa.h"

namespace clover {
namespace {

constexpr uint32_t kMstatus = 0x300;
constexpr uint32_t kMtvec = 0x305;
constexpr uint32_t kMscratch = 0x340;
constexpr uint32_t kMepc = 0x341;
constexpr uint32_t kMcause = 0x342;
constexpr uint32_t kMtval = 0x343;
constexpr uint32_t kCycle = 0xC00;
constexpr uint32_t kInstret = 0xC02;

int32_t as_i32(uint32_t value)
{
    return static_cast<int32_t>(value);
}

std::string hex(uint32_t value)
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%08x", value);
    return buffer;
}


float f32_from_bits(uint32_t bits)
{
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

uint32_t f32_to_bits(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

float fp16_to_f32(uint16_t bits)
{
    _Float16 value;
    std::memcpy(&value, &bits, sizeof(value));
    return static_cast<float>(value);
}

uint16_t f32_to_fp16(float value)
{
    if (std::isfinite(value)) {
        if (value > 65504.0f) value = 65504.0f;
        if (value < -65504.0f) value = -65504.0f;
    }
    _Float16 half = static_cast<_Float16>(value);
    uint16_t bits = 0;
    std::memcpy(&bits, &half, sizeof(bits));
    return bits;
}

float bf16_to_f32(uint16_t bits)
{
    return f32_from_bits(static_cast<uint32_t>(bits) << 16);
}

uint16_t f32_to_bf16(float value)
{
    uint32_t bits = f32_to_bits(value);
    const uint32_t lsb = (bits >> 16) & 1u;
    bits += 0x7FFFu + lsb;
    return static_cast<uint16_t>(bits >> 16);
}

float fp8_e4m3_to_f32(uint8_t bits)
{
    const bool sign = (bits >> 7) != 0;
    const uint32_t exp = (bits >> 3) & 0xFu;
    const uint32_t mant = bits & 0x7u;
    if (exp == 0xFu && mant == 0x7u) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    float value = 0.0f;
    if (exp == 0) {
        value = std::ldexp(static_cast<float>(mant) / 8.0f, -6);
    } else {
        value = std::ldexp(1.0f + static_cast<float>(mant) / 8.0f,
                           static_cast<int>(exp) - 7);
    }
    return sign ? -value : value;
}

float fp8_e5m2_to_f32(uint8_t bits)
{
    const bool sign = (bits >> 7) != 0;
    const uint32_t exp = (bits >> 2) & 0x1Fu;
    const uint32_t mant = bits & 0x3u;
    if (exp == 0x1Fu) {
        if (mant) return std::numeric_limits<float>::quiet_NaN();
        return sign ? -std::numeric_limits<float>::infinity()
                    : std::numeric_limits<float>::infinity();
    }
    float value = 0.0f;
    if (exp == 0) {
        value = std::ldexp(static_cast<float>(mant) / 4.0f, -14);
    } else {
        value = std::ldexp(1.0f + static_cast<float>(mant) / 4.0f,
                           static_cast<int>(exp) - 15);
    }
    return sign ? -value : value;
}

float fp4_to_f32(uint8_t bits)
{
    const bool sign = (bits >> 3) != 0;
    const uint32_t exp = (bits >> 1) & 0x3u;
    const uint32_t mant = bits & 0x1u;
    float value = 0.0f;
    if (exp == 0) {
        value = mant ? 0.5f : 0.0f;
    } else {
        value = std::ldexp(1.0f + 0.5f * static_cast<float>(mant),
                           static_cast<int>(exp) - 1);
    }
    return sign ? -value : value;
}

int dtype_size(uint32_t dtype)
{
    switch (dtype) {
    case 0: return 4;
    case 1:
    case 2: return 2;
    case 3:
    case 4:
    case 5: return 1;
    case 6: return 4;
    default: return 0;
    }
}

}  // namespace

Iss::Iss(IssOptions options)
    : options_(options),
      scratch_(kScratchSize, 0),
      dram_(kDramSize, 0)
{
    pc_ = options_.entry;
    regs_[sp] = options_.stack_top;
    csrs_[kMstatus] = 0;
}

bool Iss::load_image_at(uint32_t base, const std::vector<uint8_t> &image,
                        std::string &error)
{
    if (image.size() > kDramSize) {
        error = "image too large";
        return false;
    }
    if (base < kDramBase || base + image.size() > kDramBase + kDramSize) {
        error = "image does not fit in the DRAM window";
        return false;
    }
    std::copy(image.begin(), image.end(),
              dram_.begin() + (base - kDramBase));
    return true;
}

bool Iss::load_image(const std::vector<uint8_t> &image, std::string &error)
{
    if (!load_image_at(options_.load_base, image, error)) {
        return false;
    }

    if (options_.bss_size > 0) {
        const uint32_t bss_addr =
            (options_.load_base + static_cast<uint32_t>(image.size()) + 15u) &
            ~15u;
        if (bss_addr + options_.bss_size > kDramBase + kDramSize) {
            error = "bss does not fit in the DRAM window";
            return false;
        }
        uint32_t offset = bss_addr - kDramBase;
        std::fill(dram_.begin() + offset,
                  dram_.begin() + offset + options_.bss_size, 0);
    }

    pc_ = options_.entry;
    regs_[sp] = options_.stack_top;
    regs_[zero] = 0;
    return true;
}

bool Iss::access(uint32_t address, uint32_t size, bool write, uint32_t &value)
{
    const uint64_t end = static_cast<uint64_t>(address) + size;
    if (address < kScratchSize && end <= kScratchSize) {
        if (write) {
            for (uint32_t i = 0; i < size; ++i) {
                scratch_[address + i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
            }
        } else {
            value = 0;
            for (uint32_t i = 0; i < size; ++i) {
                value |= static_cast<uint32_t>(scratch_[address + i]) << (8 * i);
            }
        }
        return true;
    }
    if (address >= kDramBase && end <= kDramBase + kDramSize) {
        const uint32_t offset = address - kDramBase;
        if (write) {
            for (uint32_t i = 0; i < size; ++i) {
                dram_[offset + i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
            }
        } else {
            value = 0;
            for (uint32_t i = 0; i < size; ++i) {
                value |= static_cast<uint32_t>(dram_[offset + i]) << (8 * i);
            }
        }
        return true;
    }
    if (address >= kMmioBase && end <= kMmioBase + kMmioSize) {
        if (write) {
            return true;
        }
        value = 0;
        return true;
    }
    return false;
}

bool Iss::read_memory(uint32_t address, uint32_t size, uint32_t &value) const
{
    return const_cast<Iss *>(this)->access(address, size, false, value);
}

bool Iss::write_memory(uint32_t address, uint32_t size, uint32_t value)
{
    return access(address, size, true, value);
}

uint32_t Iss::reg(uint32_t index) const
{
    return index == 0 ? 0 : regs_[index & 31u];
}

uint32_t Iss::csr(uint32_t address) const
{
    if (address == kCycle) {
        return static_cast<uint32_t>(cycles_);
    }
    if (address == kInstret) {
        return static_cast<uint32_t>(instructions_);
    }
    return csrs_[address & 0xFFFu];
}

void Iss::set_reg(uint32_t index, uint32_t value)
{
    if (index != 0) {
        regs_[index & 31u] = value;
    }
}

void Iss::next(uint32_t target)
{
    next_pc_ = target;
}

void Iss::raise(uint32_t cause, uint32_t tval)
{
    faulted_ = true;
    csrs_[kMepc] = pc_;
    csrs_[kMcause] = cause;
    csrs_[kMtval] = tval;
    uint32_t status = csrs_[kMstatus];
    const uint32_t mie = (status >> 3) & 1u;
    status = (status & ~(1u << 7)) | (mie << 7);
    status &= ~(1u << 3);
    status = (status & ~(3u << 11)) | (3u << 11);
    csrs_[kMstatus] = status;
    const uint32_t vector = csrs_[kMtvec] & ~3u;
    if (vector == 0) {
        halted_ = true;
        exit_code_ = 1;
        std::ostringstream os;
        os << "unhandled trap: cause=" << cause << " tval=" << hex(tval)
           << " pc=" << hex(pc_);
        error_ = os.str();
    } else {
        trapped_ = true;
        next_pc_ = vector;
    }
}

void Iss::syscall()
{
    const uint32_t number = regs_[a7];
    switch (number) {
    case 0:
        halted_ = true;
        exit_code_ = static_cast<int>(regs_[a0] & 0xFFu);
        break;
    case 1: {
        const char c = static_cast<char>(regs_[a0] & 0xFFu);
        console_.push_back(c);
        if (options_.echo_console) {
            std::cout.put(c);
            std::cout.flush();
        }
        break;
    }
    case 2:
        set_reg(a0, static_cast<uint32_t>(cycles_));
        break;
    case 3:
        halted_ = true;
        exit_code_ = 0;
        error_ = "debug break";
        break;
    default:
        raise(2, number);
        break;
    }
}


void Iss::boot(uint32_t entry, uint32_t sp, uint32_t arg)
{
    pc_ = entry;
    regs_[0] = 0;
    regs_[1] = 0;
    regs_[2] = sp;
    regs_[10] = arg;
    halted_ = false;
    faulted_ = false;
    trapped_ = false;
    exit_code_ = 0;
    error_.clear();
    console_.clear();
}

bool Iss::read_elem(uint32_t address, uint32_t dtype, float &value,
                    std::string &error)
{
    uint32_t raw = 0;
    const int size = dtype_size(dtype);
    if (size == 0) {
        error = "unsupported dtype " + std::to_string(dtype);
        return false;
    }
    if (!read_memory(address, static_cast<uint32_t>(size), raw)) {
        error = "element read out of range";
        return false;
    }
    switch (dtype) {
    case 0: value = f32_from_bits(raw); return true;
    case 1: value = fp16_to_f32(static_cast<uint16_t>(raw)); return true;
    case 2: value = bf16_to_f32(static_cast<uint16_t>(raw)); return true;
    case 3: value = fp8_e4m3_to_f32(static_cast<uint8_t>(raw)); return true;
    case 4: value = fp8_e5m2_to_f32(static_cast<uint8_t>(raw)); return true;
    case 5: value = fp4_to_f32(static_cast<uint8_t>(raw & 0xFu)); return true;
    case 6: value = static_cast<float>(static_cast<int32_t>(raw)); return true;
    default:
        error = "unsupported MMA dtype";
        return false;
    }
}

bool Iss::write_elem(uint32_t address, uint32_t dtype, float value,
                     std::string &error)
{
    uint32_t raw = 0;
    int size = dtype_size(dtype);
    if (size == 0) {
        error = "unsupported dtype " + std::to_string(dtype);
        return false;
    }
    switch (dtype) {
    case 0:
        raw = f32_to_bits(value);
        break;
    case 1:
        raw = f32_to_fp16(value);
        break;
    case 2:
        raw = f32_to_bf16(value);
        break;
    case 3: {
        if (std::isnan(value)) { raw = 0x7Fu; break; }
        uint32_t best = 0;
        float best_diff = std::numeric_limits<float>::infinity();
        for (uint32_t code = 0; code < 256; code++) {
            const uint32_t exp = (code >> 3) & 0xFu;
            const uint32_t mant = code & 0x7u;
            if (exp == 0xFu && mant == 0x7u) continue;
            const float candidate = fp8_e4m3_to_f32(static_cast<uint8_t>(code));
            const float diff = std::fabs(candidate - value);
            if (diff < best_diff) { best_diff = diff; best = code; }
        }
        raw = best;
        break;
    }
    case 4: {
        if (std::isnan(value)) { raw = 0x7Fu; break; }
        uint32_t best = 0;
        float best_diff = std::numeric_limits<float>::infinity();
        for (uint32_t code = 0; code < 256; code++) {
            const uint32_t exp = (code >> 2) & 0x1Fu;
            if (exp == 0x1Fu) continue;
            const float candidate = fp8_e5m2_to_f32(static_cast<uint8_t>(code));
            const float diff = std::fabs(candidate - value);
            if (diff < best_diff) { best_diff = diff; best = code; }
        }
        raw = best;
        break;
    }
    case 5: {
        uint32_t best = 0;
        float best_diff = std::numeric_limits<float>::infinity();
        for (uint32_t code = 0; code < 16; code++) {
            const float candidate = fp4_to_f32(static_cast<uint8_t>(code));
            const float diff = std::fabs(candidate - value);
            if (diff < best_diff) { best_diff = diff; best = code; }
        }
        raw = best;
        break;
    }
    case 6:
        raw = static_cast<uint32_t>(static_cast<int32_t>(value));
        break;
    default:
        error = "unsupported output dtype";
        return false;
    }
    if (!write_memory(address, static_cast<uint32_t>(size), raw)) {
        error = "element write out of range";
        return false;
    }
    return true;
}

bool Iss::tile_mma(uint32_t descriptor, std::string &error)
{
    uint32_t d[16] = {};
    for (int i = 0; i < 16; i++) {
        if (!read_memory(descriptor + 4u * static_cast<uint32_t>(i), 4, d[i])) {
            error = "MMA descriptor out of range";
            return false;
        }
    }
    if (d[0] != 0x31414D4Du) {
        error = "bad MMA descriptor magic";
        return false;
    }
    const uint32_t flags = d[1];
    const uint32_t m = d[2];
    const uint32_t n = d[3];
    const uint32_t k = d[4];
    if (m == 0 || m > 16 || n == 0 || n > 16 || k == 0 || k > 16) {
        error = "MMA M/N/K out of range";
        return false;
    }
    const uint32_t a_type = (flags >> 5) & 0x7u;
    const uint32_t b_type = (flags >> 8) & 0x7u;
    const uint32_t c_type = (flags >> 11) & 0x7u;
    const int a_size = dtype_size(a_type);
    const int b_size = dtype_size(b_type);
    const int c_size = dtype_size(c_type);
    if (a_size == 0 || b_size == 0 || c_size == 0 || a_type == 6 || b_type == 6) {
        error = "unsupported MMA dtype";
        return false;
    }

    const bool accumulate = (flags & 1u) != 0;
    const bool transpose_a = (flags & 2u) != 0;
    const bool transpose_b = (flags & 4u) != 0;
    const bool relu = (flags & 8u) != 0;
    const bool a_scale_en = (flags & (1u << 14)) != 0;
    const bool b_scale_en = (flags & (1u << 15)) != 0;
    const bool a_per_channel = (flags & (1u << 16)) != 0;
    const bool b_per_channel = (flags & (1u << 17)) != 0;
    const bool post_scale_en = (flags & (1u << 18)) != 0;
    const bool post_bias_en = (flags & (1u << 19)) != 0;

    float a_scale = 1.0f;
    float b_scale = 1.0f;
    if (a_scale_en && !a_per_channel) {
        uint32_t raw = 0;
        if (!read_memory(d[11], 4, raw)) { error = "A scale address"; return false; }
        a_scale = f32_from_bits(raw);
    }
    if (b_scale_en && !b_per_channel) {
        uint32_t raw = 0;
        if (!read_memory(d[12], 4, raw)) { error = "B scale address"; return false; }
        b_scale = f32_from_bits(raw);
    }
    const float post_scale = post_scale_en ? f32_from_bits(d[13]) : 1.0f;
    const float post_bias = post_bias_en ? f32_from_bits(d[14]) : 0.0f;

    for (uint32_t mi = 0; mi < m; mi++) {
        float row_a_scale = a_scale;
        if (a_scale_en && a_per_channel) {
            uint32_t raw = 0;
            if (!read_memory(d[11] + 4u * mi, 4, raw)) { error = "A channel scale"; return false; }
            row_a_scale = f32_from_bits(raw);
        }
        for (uint32_t ni = 0; ni < n; ni++) {
            float col_b_scale = b_scale;
            if (b_scale_en && b_per_channel) {
                uint32_t raw = 0;
                if (!read_memory(d[12] + 4u * ni, 4, raw)) { error = "B channel scale"; return false; }
                col_b_scale = f32_from_bits(raw);
            }
            const uint32_t c_addr = d[7] + mi * d[10] + ni * static_cast<uint32_t>(c_size);
            float acc = 0.0f;
            if (accumulate) {
                if (!read_elem(c_addr, c_type, acc, error)) return false;
            }
            for (uint32_t ki = 0; ki < k; ki++) {
                const uint32_t a_addr = transpose_a
                    ? d[5] + ki * d[8] + mi * static_cast<uint32_t>(a_size)
                    : d[5] + mi * d[8] + ki * static_cast<uint32_t>(a_size);
                const uint32_t b_addr = transpose_b
                    ? d[6] + ni * d[9] + ki * static_cast<uint32_t>(b_size)
                    : d[6] + ki * d[9] + ni * static_cast<uint32_t>(b_size);
                float av = 0.0f;
                float bv = 0.0f;
                if (!read_elem(a_addr, a_type, av, error)) return false;
                if (!read_elem(b_addr, b_type, bv, error)) return false;
                av = av * row_a_scale;
                bv = bv * col_b_scale;
                const float product = av * bv;
                acc = acc + product;
            }
            if (post_scale_en) acc = acc * post_scale;
            if (post_bias_en) acc = acc + post_bias;
            if (relu && acc < 0.0f) acc = 0.0f;
            if (!write_elem(c_addr, c_type, acc, error)) return false;
        }
    }
    return true;
}

bool Iss::tile_vec(uint32_t descriptor, std::string &error)
{
    uint32_t d[16] = {};
    for (int i = 0; i < 16; i++) {
        if (!read_memory(descriptor + 4u * static_cast<uint32_t>(i), 4, d[i])) {
            error = "VEC descriptor out of range";
            return false;
        }
    }
    if (d[0] != 0x31434556u) {
        error = "bad VEC descriptor magic";
        return false;
    }
    const uint32_t flags = d[1];
    const uint32_t count = d[2];
    const uint32_t a_addr = d[3];
    const uint32_t b_addr = d[4];
    const uint32_t c_addr = d[5];
    const uint32_t a_stride = d[6];
    const uint32_t b_stride = d[7];
    const uint32_t c_stride = d[8];
    const uint32_t src_type = d[9];
    const uint32_t dst_type = d[10];
    const uint32_t op = flags & 0xFu;
    if (op > 9) {
        error = "unsupported VEC op";
        return false;
    }
    for (uint32_t i = 0; i < count; i++) {
        float av = 0.0f;
        float bv = 0.0f;
        if (!read_elem(a_addr + i * a_stride, src_type, av, error)) return false;
        if (op != 5 && op != 6 && op != 7 && op != 9) {
            if (!read_elem(b_addr + i * b_stride, src_type, bv, error)) return false;
        }
        float result = av;
        switch (op) {
        case 0: result = av + bv; break;
        case 1: result = av - bv; break;
        case 2: result = av * bv; break;
        case 3: result = av > bv ? av : bv; break;
        case 4: result = av < bv ? av : bv; break;
        case 5: result = av < 0.0f ? 0.0f : av; break;
        case 6: result = av < 0.0f ? -av : av; break;
        case 7: result = -av; break;
        case 8: result = av * bv + f32_from_bits(d[14]); break;
        case 9: result = av; break;
        default: break;
        }
        result = result * f32_from_bits(d[13]);
        if (flags & (1u << 5)) {
            if (result < 0.0f) result = 0.0f;
        }
        if (!write_elem(c_addr + i * c_stride, dst_type, result, error)) {
            return false;
        }
    }
    return true;
}

bool Iss::tile_dma(uint32_t descriptor, std::string &error)
{
    uint32_t d[16] = {};
    for (int i = 0; i < 16; i++) {
        if (!read_memory(descriptor + 4u * static_cast<uint32_t>(i), 4, d[i])) {
            error = "DMA descriptor out of range";
            return false;
        }
    }
    if (d[0] != 0x31414D44u) {
        error = "bad DMA descriptor magic";
        return false;
    }
    const uint32_t src = d[2];
    const uint32_t dst = d[3];
    const uint32_t row_bytes = d[4];
    const uint32_t rows = d[5];
    const uint32_t src_stride = d[6];
    const uint32_t dst_stride = d[7];
    for (uint32_t r = 0; r < rows; r++) {
        for (uint32_t i = 0; i < row_bytes; i++) {
            uint32_t value = 0;
            if (!read_memory(src + r * src_stride + i, 1, value)) {
                error = "DMA source out of range";
                return false;
            }
            if (!write_memory(dst + r * dst_stride + i, 1, value)) {
                error = "DMA destination out of range";
                return false;
            }
        }
    }
    return true;
}

void Iss::execute(uint32_t raw)
{
    const Decoded d = decode(raw, pc_);
    if (!d.valid) {
        raise(2, raw);
        return;
    }

    switch (d.opcode) {
    case Opcode::Lui:
        set_reg(d.rd, static_cast<uint32_t>(d.imm));
        break;
    case Opcode::Auipc:
        set_reg(d.rd, pc_ + static_cast<uint32_t>(d.imm));
        break;
    case Opcode::Jal:
        set_reg(d.rd, pc_ + 4);
        next(pc_ + static_cast<uint32_t>(d.imm));
        break;
    case Opcode::Jalr: {
        const uint32_t target = (regs_[d.rs1] + static_cast<uint32_t>(d.imm)) & ~1u;
        if (d.rd == 0 && target == 0) {
            // Kernel-return sentinel: CP boots with ra=0; `ret` halts and
            // returns a0 as the kernel status.
            halted_ = true;
            exit_code_ = static_cast<int>(regs_[10] & 0xFFu);
            return;
        }
        const uint32_t link = pc_ + 4;
        set_reg(d.rd, link);
        next(target);
        break;
    }
    case Opcode::Branch: {
        bool take = false;
        switch (d.funct3) {
        case 0: take = regs_[d.rs1] == regs_[d.rs2]; break;
        case 1: take = regs_[d.rs1] != regs_[d.rs2]; break;
        case 4: take = as_i32(regs_[d.rs1]) < as_i32(regs_[d.rs2]); break;
        case 5: take = as_i32(regs_[d.rs1]) >= as_i32(regs_[d.rs2]); break;
        case 6: take = regs_[d.rs1] < regs_[d.rs2]; break;
        case 7: take = regs_[d.rs1] >= regs_[d.rs2]; break;
        default: raise(2, raw); return;
        }
        if (take) {
            next(pc_ + static_cast<uint32_t>(d.imm));
        }
        break;
    }
    case Opcode::Load: {
        const uint32_t address = regs_[d.rs1] + static_cast<uint32_t>(d.imm);
        uint32_t size = 0;
        bool sign = false;
        switch (d.funct3) {
        case 0: size = 1; sign = true; break;
        case 1: size = 2; sign = true; break;
        case 2: size = 4; break;
        case 4: size = 1; break;
        case 5: size = 2; break;
        default: raise(2, raw); return;
        }
        if ((size == 2 && (address & 1u)) || (size == 4 && (address & 3u))) {
            raise(4, address);
            return;
        }
        uint32_t value = 0;
        if (!access(address, size, false, value)) {
            raise(5, address);
            return;
        }
        if (size == 1 && sign) {
            value = static_cast<uint32_t>(static_cast<int32_t>(
                static_cast<int8_t>(value & 0xFFu)));
        } else if (size == 2 && sign) {
            value = static_cast<uint32_t>(static_cast<int32_t>(
                static_cast<int16_t>(value & 0xFFFFu)));
        }
        set_reg(d.rd, value);
        break;
    }
    case Opcode::Store: {
        const uint32_t address = regs_[d.rs1] + static_cast<uint32_t>(d.imm);
        uint32_t size = 0;
        switch (d.funct3) {
        case 0: size = 1; break;
        case 1: size = 2; break;
        case 2: size = 4; break;
        default: raise(2, raw); return;
        }
        if ((size == 2 && (address & 1u)) || (size == 4 && (address & 3u))) {
            raise(6, address);
            return;
        }
        if (!access(address, size, true, regs_[d.rs2])) {
            raise(7, address);
            return;
        }
        break;
    }
    case Opcode::OpImm: {
        const uint32_t a = regs_[d.rs1];
        const uint32_t shamt = static_cast<uint32_t>(d.imm) & 0x1Fu;
        uint32_t result = 0;
        switch (d.funct3) {
        case 0: result = a + static_cast<uint32_t>(d.imm); break;
        case 1: result = a << shamt; break;
        case 2: result = (as_i32(a) < d.imm) ? 1u : 0u; break;
        case 3: result = (a < static_cast<uint32_t>(d.imm)) ? 1u : 0u; break;
        case 4: result = a ^ static_cast<uint32_t>(d.imm); break;
        case 5:
            if (d.funct7 == 0x00) {
                result = a >> shamt;
            } else if (d.funct7 == 0x20) {
                result = static_cast<uint32_t>(as_i32(a) >> shamt);
            } else {
                raise(2, raw);
                return;
            }
            break;
        case 6: result = a | static_cast<uint32_t>(d.imm); break;
        case 7: result = a & static_cast<uint32_t>(d.imm); break;
        default: raise(2, raw); return;
        }
        set_reg(d.rd, result);
        break;
    }
    case Opcode::Op: {
        const uint32_t a = regs_[d.rs1];
        const uint32_t b = regs_[d.rs2];
        uint32_t result = 0;
        if (d.funct7 == 0x00) {
            switch (d.funct3) {
            case 0: result = a + b; break;
            case 1: result = a << (b & 31u); break;
            case 2: result = (as_i32(a) < as_i32(b)) ? 1u : 0u; break;
            case 3: result = (a < b) ? 1u : 0u; break;
            case 4: result = a ^ b; break;
            case 5: result = a >> (b & 31u); break;
            case 6: result = a | b; break;
            case 7: result = a & b; break;
            default: raise(2, raw); return;
            }
        } else if (d.funct7 == 0x20) {
            switch (d.funct3) {
            case 0: result = a - b; break;
            case 5: result = static_cast<uint32_t>(as_i32(a) >> (b & 31u)); break;
            default: raise(2, raw); return;
            }
        } else if (d.funct7 == 0x01) {
            const int64_t sa = as_i32(a);
            const int64_t sb = as_i32(b);
            const uint64_t ua = a;
            const uint64_t ub = b;
            switch (d.funct3) {
            case 0: result = static_cast<uint32_t>(sa * sb); break;
            case 1: result = static_cast<uint32_t>((sa * sb) >> 32); break;
            case 2: result = static_cast<uint32_t>((sa * static_cast<int64_t>(ub)) >> 32); break;
            case 3: result = static_cast<uint32_t>((ua * ub) >> 32); break;
            case 4:
                if (sb == 0) {
                    result = 0xFFFF'FFFFu;
                } else if (sa == std::numeric_limits<int32_t>::min() && sb == -1) {
                    result = static_cast<uint32_t>(sa);
                } else {
                    result = static_cast<uint32_t>(sa / sb);
                }
                break;
            case 5:
                if (b == 0) {
                    result = 0xFFFF'FFFFu;
                } else {
                    result = a / b;
                }
                break;
            case 6:
                if (sb == 0) {
                    result = a;
                } else if (sa == std::numeric_limits<int32_t>::min() && sb == -1) {
                    result = 0;
                } else {
                    result = static_cast<uint32_t>(sa % sb);
                }
                break;
            case 7:
                if (b == 0) {
                    result = a;
                } else {
                    result = a % b;
                }
                break;
            default: raise(2, raw); return;
            }
        } else {
            raise(2, raw);
            return;
        }
        set_reg(d.rd, result);
        break;
    }
    case Opcode::MiscMem:
        // FENCE and CACHE are no-ops in the functional ISS.
        break;
    case Opcode::System: {
        if (d.funct3 == 0) {
            if (raw == encode_i(Opcode::System, 0, 0, 0, 0)) {
                syscall();
            } else if (raw == encode_i(Opcode::System, 0, 0, 0, 1)) {
                halted_ = true;
                exit_code_ = 0;
                error_ = "breakpoint";
            } else if (raw == encode_i(Opcode::System, 0, 0, 0, 0x302)) {
                const uint32_t status = csrs_[kMstatus];
                const uint32_t mpie = (status >> 7) & 1u;
                csrs_[kMstatus] = (status & ~(1u << 3)) | (mpie << 3) |
                                  (1u << 7) | (status & (3u << 11));
                next(csrs_[kMepc]);
            } else if (raw == encode_i(Opcode::System, 0, 0, 0, 0x105)) {
                // WFI is a no-op in this single-core model.
            } else {
                raise(2, raw);
            }
            break;
        }
        const uint32_t csr_address = (raw >> 20) & 0xFFFu;
        const uint32_t old = csr(csr_address);
        const bool immediate = d.funct3 >= 5;
        const uint32_t source = immediate ? d.rs1 : regs_[d.rs1];
        uint32_t write_value = old;
        bool do_write = true;
        switch (d.funct3) {
        case 1:
        case 5:
            write_value = source;
            break;
        case 2:
        case 6:
            write_value = old | source;
            do_write = !(immediate ? (d.rs1 == 0) : (d.rs1 == 0));
            break;
        case 3:
        case 7:
            write_value = old & ~source;
            do_write = !(immediate ? (d.rs1 == 0) : (d.rs1 == 0));
            break;
        default:
            raise(2, raw);
            return;
        }
        if (do_write) {
            if (csr_address == kMstatus || csr_address == kMtvec ||
                csr_address == kMepc || csr_address == kMcause ||
                csr_address == kMtval || csr_address == kMscratch) {
                csrs_[csr_address & 0xFFFu] = write_value;
            } else {
                csrs_[csr_address & 0xFFFu] = write_value;
            }
        }
        set_reg(d.rd, old);
        break;
    }
    case Opcode::Tile: {
        std::string tile_error;
        bool ok = true;
        switch (d.funct3) {
        case 0: ok = tile_mma(regs_[d.rs1], tile_error); break;
        case 1: ok = tile_vec(regs_[d.rs1], tile_error); break;
        case 2: ok = tile_dma(regs_[d.rs1], tile_error); break;
        case 3: set_reg(d.rd, 0); break;
        case 4: break; // TILE.FENCE
        default: raise(2, raw); return;
        }
        if (!ok) {
            halted_ = true;
            faulted_ = true;
            exit_code_ = 1;
            error_ = "tile: " + tile_error;
        }
        break;
    }
    default:
        raise(2, raw);
        break;
    }
}

IssResult Iss::run(std::string *trace_output)
{
    while (!halted_ && instructions_ < options_.max_steps) {
        trapped_ = false;
        next_pc_ = pc_ + 4;

        uint32_t raw = 0;
        if ((pc_ & 3u) != 0) {
            raise(0, pc_);
        } else if (!fetch(pc_, raw)) {
            raise(1, pc_);
        } else {
            if (options_.trace || trace_output) {
                std::ostringstream os;
                os << hex(pc_) << "  " << hex(raw) << "  " << decode(raw, pc_).text
                   << "  a0=" << hex(regs_[10]) << " t0=" << hex(regs_[5])
                   << " s0=" << hex(regs_[8]) << "\n";
                if (trace_output) {
                    *trace_output += os.str();
                } else {
                    std::cout << os.str();
                }
            }
            execute(raw);
        }

        if (halted_) {
            break;
        }
        if (trapped_) {
            // raise() already set next_pc_.
        }
        pc_ = next_pc_;
        ++cycles_;
        ++instructions_;
    }

    IssResult result;
    result.halted = halted_;
    result.faulted = faulted_;
    result.exit_code = exit_code_;
    result.cycles = cycles_;
    result.instructions = instructions_;
    result.pc = pc_;
    result.error = error_;
    result.console = console_;
    if (!halted_ && error_.empty()) {
        result.error = "step limit reached";
    }
    return result;
}

bool Iss::fetch(uint32_t address, uint32_t &raw)
{
    return access(address, 4, false, raw);
}

}  // namespace clover
