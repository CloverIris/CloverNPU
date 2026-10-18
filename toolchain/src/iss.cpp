#include "clover/iss.h"

#include <algorithm>
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

bool Iss::load_image(const std::vector<uint8_t> &image, std::string &error)
{
    if (image.size() > kDramSize) {
        error = "image too large";
        return false;
    }
    if (options_.load_base < kDramBase ||
        options_.load_base + image.size() > kDramBase + kDramSize) {
        error = "image does not fit in the DRAM window";
        return false;
    }
    std::copy(image.begin(), image.end(),
              dram_.begin() + (options_.load_base - kDramBase));

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
    case Opcode::Tile:
        // Functional tile-engine model is added in M4.  For now TILE.WAIT
        // succeeds and the other TILE ops are accepted no-ops.
        if (d.funct3 == 3) {
            set_reg(d.rd, 0);
        } else if (d.funct3 > 4) {
            raise(2, raw);
        }
        break;
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
                   << "\n";
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
