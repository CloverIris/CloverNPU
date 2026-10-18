#include "clover/isa.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace clover {
namespace {

constexpr std::array<std::string_view, 32> kRegNames = {
    "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
    "s0",   "s1", "a0", "a1", "a2", "a3", "a4", "a5",
    "a6",   "a7", "s2", "s3", "s4", "s5", "s6", "s7",
    "s8",   "s9", "s10", "s11", "t3", "t4", "t5", "t6",
};

std::string hex(uint32_t value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "0x%x", value);
    return buffer;
}

std::string fmt3(const char *op, uint32_t rd, uint32_t rs1, uint32_t rs2)
{
    std::ostringstream os;
    os << op << " " << reg_name(rd) << ", " << reg_name(rs1) << ", "
       << reg_name(rs2);
    return os.str();
}

std::string fmt_imm(const char *op, uint32_t rd, uint32_t rs1, int32_t imm)
{
    std::ostringstream os;
    os << op << " " << reg_name(rd) << ", " << reg_name(rs1) << ", " << imm;
    return os.str();
}

std::string fmt_load(const char *op, uint32_t rd, uint32_t rs1, int32_t imm)
{
    std::ostringstream os;
    os << op << " " << reg_name(rd) << ", " << imm << "(" << reg_name(rs1)
       << ")";
    return os.str();
}

std::string fmt_store(const char *op, uint32_t rs2, uint32_t rs1, int32_t imm)
{
    std::ostringstream os;
    os << op << " " << reg_name(rs2) << ", " << imm << "(" << reg_name(rs1)
       << ")";
    return os.str();
}

std::string fmt_branch(const char *op, uint32_t rs1, uint32_t rs2, uint32_t target)
{
    std::ostringstream os;
    os << op << " " << reg_name(rs1) << ", " << reg_name(rs2) << ", "
       << hex(target);
    return os.str();
}

int32_t sign_extend(uint32_t value, unsigned bits)
{
    const uint32_t sign = 1u << (bits - 1);
    return static_cast<int32_t>((value ^ sign) - sign);
}

}  // namespace

std::string_view reg_name(uint32_t reg)
{
    return kRegNames[reg & 31u];
}

std::optional<uint32_t> reg_from_name(std::string_view name)
{
    for (uint32_t i = 0; i < kRegNames.size(); ++i) {
        if (kRegNames[i] == name) {
            return i;
        }
    }
    if (name.size() >= 2 && name[0] == 'x') {
        uint32_t value = 0;
        for (size_t i = 1; i < name.size(); ++i) {
            if (name[i] < '0' || name[i] > '9') {
                return std::nullopt;
            }
            value = value * 10 + static_cast<uint32_t>(name[i] - '0');
        }
        if (value < 32) {
            return value;
        }
    }
    return std::nullopt;
}

uint32_t encode_r(Opcode opcode, uint32_t rd, uint32_t funct3, uint32_t rs1,
                  uint32_t rs2, uint32_t funct7)
{
    return ((funct7 & 0x7Fu) << 25) | ((rs2 & 0x1Fu) << 20) |
           ((rs1 & 0x1Fu) << 15) | ((funct3 & 0x7u) << 12) |
           ((rd & 0x1Fu) << 7) | (static_cast<uint32_t>(opcode) & 0x7Fu);
}

uint32_t encode_i(Opcode opcode, uint32_t rd, uint32_t funct3, uint32_t rs1,
                  int32_t imm)
{
    return ((static_cast<uint32_t>(imm) & 0xFFFu) << 20) |
           ((rs1 & 0x1Fu) << 15) | ((funct3 & 0x7u) << 12) |
           ((rd & 0x1Fu) << 7) | (static_cast<uint32_t>(opcode) & 0x7Fu);
}

uint32_t encode_s(Opcode opcode, uint32_t funct3, uint32_t rs1, uint32_t rs2,
                  int32_t imm)
{
    const uint32_t u = static_cast<uint32_t>(imm);
    return (((u >> 5) & 0x7Fu) << 25) | ((rs2 & 0x1Fu) << 20) |
           ((rs1 & 0x1Fu) << 15) | ((funct3 & 0x7u) << 12) |
           ((u & 0x1Fu) << 7) | (static_cast<uint32_t>(opcode) & 0x7Fu);
}

uint32_t encode_b(Opcode opcode, uint32_t funct3, uint32_t rs1, uint32_t rs2,
                  int32_t offset)
{
    const uint32_t u = static_cast<uint32_t>(offset);
    return (((u >> 12) & 0x1u) << 31) | (((u >> 5) & 0x3Fu) << 25) |
           ((rs2 & 0x1Fu) << 20) | ((rs1 & 0x1Fu) << 15) |
           ((funct3 & 0x7u) << 12) | (((u >> 1) & 0xFu) << 8) |
           (((u >> 11) & 0x1u) << 7) | (static_cast<uint32_t>(opcode) & 0x7Fu);
}

uint32_t encode_u(Opcode opcode, uint32_t rd, uint32_t imm20)
{
    return ((imm20 & 0xFFFFFu) << 12) | ((rd & 0x1Fu) << 7) |
           (static_cast<uint32_t>(opcode) & 0x7Fu);
}

uint32_t encode_j(Opcode opcode, uint32_t rd, int32_t offset)
{
    const uint32_t u = static_cast<uint32_t>(offset);
    return (((u >> 20) & 0x1u) << 31) | (((u >> 1) & 0x3FFu) << 21) |
           (((u >> 11) & 0x1u) << 20) | (((u >> 12) & 0xFFu) << 12) |
           ((rd & 0x1Fu) << 7) | (static_cast<uint32_t>(opcode) & 0x7Fu);
}

uint32_t encode_tile(uint32_t funct3, uint32_t rd, uint32_t rs1, uint32_t rs2,
                     uint32_t funct7)
{
    return encode_r(Opcode::Tile, rd, funct3, rs1, rs2, funct7);
}

Decoded decode(uint32_t raw, uint32_t pc)
{
    Decoded d;
    d.raw = raw;
    d.opcode = static_cast<Opcode>(raw & 0x7Fu);
    d.rd = (raw >> 7) & 0x1Fu;
    d.funct3 = (raw >> 12) & 0x7u;
    d.rs1 = (raw >> 15) & 0x1Fu;
    d.rs2 = (raw >> 20) & 0x1Fu;
    d.funct7 = (raw >> 25) & 0x7Fu;

    const int32_t imm_i = static_cast<int32_t>(raw) >> 20;
    const int32_t imm_s =
        sign_extend((((raw >> 25) & 0x7Fu) << 5) | ((raw >> 7) & 0x1Fu), 12);
    const int32_t imm_b = sign_extend(
        (((raw >> 31) & 0x1u) << 12) | (((raw >> 25) & 0x3Fu) << 5) |
            (((raw >> 8) & 0xFu) << 1) | (((raw >> 7) & 0x1u) << 11),
        13);
    const int32_t imm_j = sign_extend(
        (((raw >> 31) & 0x1u) << 20) | (((raw >> 21) & 0x3FFu) << 1) |
            (((raw >> 20) & 0x1u) << 11) | (((raw >> 12) & 0xFFu) << 12),
        21);

    switch (d.opcode) {
    case Opcode::Lui:
        d.valid = true;
        d.imm = static_cast<int32_t>(raw & 0xFFFFF000u);
        d.text = "lui " + std::string(reg_name(d.rd)) + ", " + hex(raw >> 12);
        break;
    case Opcode::Auipc:
        d.valid = true;
        d.imm = static_cast<int32_t>(raw & 0xFFFFF000u);
        d.text = "auipc " + std::string(reg_name(d.rd)) + ", " + hex(raw >> 12);
        break;
    case Opcode::Jal: {
        d.valid = true;
        d.imm = imm_j;
        d.target = pc + static_cast<uint32_t>(imm_j);
        std::ostringstream os;
        os << "jal " << reg_name(d.rd) << ", " << hex(d.target);
        d.text = os.str();
        break;
    }
    case Opcode::Jalr:
        if (d.funct3 == 0) {
            d.valid = true;
            d.imm = imm_i;
            d.text = "jalr " + std::string(reg_name(d.rd)) + ", " +
                     std::to_string(imm_i) + "(" + std::string(reg_name(d.rs1)) +
                     ")";
        }
        break;
    case Opcode::Branch: {
        d.valid = true;
        d.imm = imm_b;
        d.target = pc + static_cast<uint32_t>(imm_b);
        const char *name = nullptr;
        switch (d.funct3) {
        case 0: name = "beq"; break;
        case 1: name = "bne"; break;
        case 4: name = "blt"; break;
        case 5: name = "bge"; break;
        case 6: name = "bltu"; break;
        case 7: name = "bgeu"; break;
        default: d.valid = false; break;
        }
        if (name) {
            d.text = fmt_branch(name, d.rs1, d.rs2, d.target);
        }
        break;
    }
    case Opcode::Load: {
        d.valid = true;
        d.imm = imm_i;
        const char *name = nullptr;
        switch (d.funct3) {
        case 0: name = "lb"; break;
        case 1: name = "lh"; break;
        case 2: name = "lw"; break;
        case 4: name = "lbu"; break;
        case 5: name = "lhu"; break;
        default: d.valid = false; break;
        }
        if (name) {
            d.text = fmt_load(name, d.rd, d.rs1, imm_i);
        }
        break;
    }
    case Opcode::Store: {
        d.valid = true;
        d.imm = imm_s;
        const char *name = nullptr;
        switch (d.funct3) {
        case 0: name = "sb"; break;
        case 1: name = "sh"; break;
        case 2: name = "sw"; break;
        default: d.valid = false; break;
        }
        if (name) {
            d.text = fmt_store(name, d.rs2, d.rs1, imm_s);
        }
        break;
    }
    case Opcode::OpImm: {
        d.valid = true;
        d.imm = imm_i;
        switch (d.funct3) {
        case 0: d.text = fmt_imm("addi", d.rd, d.rs1, imm_i); break;
        case 1:
            d.text = fmt_imm("slli", d.rd, d.rs1, imm_i & 0x1F);
            break;
        case 2: d.text = fmt_imm("slti", d.rd, d.rs1, imm_i); break;
        case 3: d.text = fmt_imm("sltiu", d.rd, d.rs1, imm_i); break;
        case 4: d.text = fmt_imm("xori", d.rd, d.rs1, imm_i); break;
        case 5:
            if (d.funct7 == 0x00) {
                d.text = fmt_imm("srli", d.rd, d.rs1, imm_i & 0x1F);
            } else if (d.funct7 == 0x20) {
                d.text = fmt_imm("srai", d.rd, d.rs1, imm_i & 0x1F);
            } else {
                d.valid = false;
            }
            break;
        case 6: d.text = fmt_imm("ori", d.rd, d.rs1, imm_i); break;
        case 7: d.text = fmt_imm("andi", d.rd, d.rs1, imm_i); break;
        }
        break;
    }
    case Opcode::Op: {
        d.valid = true;
        if (d.funct7 == 0x00) {
            switch (d.funct3) {
            case 0: d.text = fmt3("add", d.rd, d.rs1, d.rs2); break;
            case 1: d.text = fmt3("sll", d.rd, d.rs1, d.rs2); break;
            case 2: d.text = fmt3("slt", d.rd, d.rs1, d.rs2); break;
            case 3: d.text = fmt3("sltu", d.rd, d.rs1, d.rs2); break;
            case 4: d.text = fmt3("xor", d.rd, d.rs1, d.rs2); break;
            case 5: d.text = fmt3("srl", d.rd, d.rs1, d.rs2); break;
            case 6: d.text = fmt3("or", d.rd, d.rs1, d.rs2); break;
            case 7: d.text = fmt3("and", d.rd, d.rs1, d.rs2); break;
            }
        } else if (d.funct7 == 0x20) {
            if (d.funct3 == 0) {
                d.text = fmt3("sub", d.rd, d.rs1, d.rs2);
            } else if (d.funct3 == 5) {
                d.text = fmt3("sra", d.rd, d.rs1, d.rs2);
            } else {
                d.valid = false;
            }
        } else if (d.funct7 == 0x01) {
            const char *name = nullptr;
            switch (d.funct3) {
            case 0: name = "mul"; break;
            case 1: name = "mulh"; break;
            case 2: name = "mulhsu"; break;
            case 3: name = "mulhu"; break;
            case 4: name = "div"; break;
            case 5: name = "divu"; break;
            case 6: name = "rem"; break;
            case 7: name = "remu"; break;
            }
            if (name) {
                d.text = fmt3(name, d.rd, d.rs1, d.rs2);
            } else {
                d.valid = false;
            }
        } else {
            d.valid = false;
        }
        break;
    }
    case Opcode::MiscMem:
        if (d.funct3 == 0) {
            d.valid = true;
            d.text = "fence";
        } else if (d.funct3 == 1) {
            static const char *kCacheOps[] = {
                "cache.flush.d", "cache.inval.d", "cache.flush.inval.d",
                "cache.inval.i", "cache.flush.all",
            };
            if (d.rs1 < 5) {
                d.valid = true;
                d.text = kCacheOps[d.rs1];
            }
        }
        break;
    case Opcode::System:
        if (d.funct3 == 0) {
            if (raw == encode_i(Opcode::System, 0, 0, 0, 0)) {
                d.valid = true;
                d.text = "ecall";
            } else if (raw == encode_i(Opcode::System, 0, 0, 0, 1)) {
                d.valid = true;
                d.text = "ebreak";
            } else if (raw == encode_i(Opcode::System, 0, 0, 0, 0x302)) {
                d.valid = true;
                d.text = "mret";
            } else if (raw == encode_i(Opcode::System, 0, 0, 0, 0x105)) {
                d.valid = true;
                d.text = "wfi";
            }
        } else if (d.funct3 == 1 || d.funct3 == 2 || d.funct3 == 3 ||
                   d.funct3 == 5 || d.funct3 == 6 || d.funct3 == 7) {
            static const char *kNames[] = {"", "csrrw", "csrrs", "csrrc",
                                           "", "csrrwi", "csrrsi", "csrrci"};
            d.valid = true;
            std::ostringstream os;
            os << kNames[d.funct3] << " " << reg_name(d.rd) << ", 0x"
               << std::hex << (raw >> 20) << ", ";
            if (d.funct3 >= 5) {
                os << "zimm=" << std::dec << d.rs1;
            } else {
                os << reg_name(d.rs1);
            }
            d.text = os.str();
        }
        break;
    case Opcode::Tile: {
        d.valid = true;
        std::ostringstream os;
        switch (d.funct3) {
        case 0:
            os << "tile.mma " << reg_name(d.rs1);
            break;
        case 1:
            os << "tile.vec " << reg_name(d.rs1);
            break;
        case 2:
            os << "tile.dma " << reg_name(d.rs1);
            break;
        case 3:
            os << "tile.wait " << reg_name(d.rd) << ", " << reg_name(d.rs1)
               << ", " << reg_name(d.rs2);
            break;
        case 4:
            os << "tile.fence";
            break;
        default:
            d.valid = false;
            break;
        }
        d.text = os.str();
        break;
    }
    default:
        break;
    }

    if (!d.valid) {
        std::ostringstream os;
        os << ".word 0x" << std::hex << raw;
        d.text = os.str();
    }
    return d;
}

}  // namespace clover
