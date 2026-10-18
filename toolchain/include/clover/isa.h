#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace clover {

constexpr uint32_t kResetVector = 0x1000'0000u;
constexpr uint32_t kInstructionBytes = 4u;

enum class Opcode : uint32_t {
    Invalid = 0x00,
    Load = 0x03,
    MiscMem = 0x0F,
    OpImm = 0x13,
    Auipc = 0x17,
    Store = 0x23,
    Tile = 0x2B,
    Op = 0x33,
    Lui = 0x37,
    Branch = 0x63,
    Jalr = 0x67,
    Jal = 0x6F,
    System = 0x73,
};

enum Reg : uint32_t {
    zero = 0,
    ra = 1,
    sp = 2,
    gp = 3,
    tp = 4,
    t0 = 5,
    t1 = 6,
    t2 = 7,
    s0 = 8,
    fp = 8,
    s1 = 9,
    a0 = 10,
    a1 = 11,
    a2 = 12,
    a3 = 13,
    a4 = 14,
    a5 = 15,
    a6 = 16,
    a7 = 17,
    s2 = 18,
    s3 = 19,
    s4 = 20,
    s5 = 21,
    s6 = 22,
    s7 = 23,
    s8 = 24,
    s9 = 25,
    s10 = 26,
    s11 = 27,
    t3 = 28,
    t4 = 29,
    t5 = 30,
    t6 = 31,
};

struct Decoded {
    bool valid = false;
    uint32_t raw = 0;
    Opcode opcode = Opcode::Invalid;
    uint32_t rd = 0;
    uint32_t rs1 = 0;
    uint32_t rs2 = 0;
    uint32_t funct3 = 0;
    uint32_t funct7 = 0;
    int32_t imm = 0;
    uint32_t target = 0;
    std::string text;
};

std::string_view reg_name(uint32_t reg);
std::optional<uint32_t> reg_from_name(std::string_view name);

uint32_t encode_r(Opcode opcode, uint32_t rd, uint32_t funct3, uint32_t rs1,
                  uint32_t rs2, uint32_t funct7);
uint32_t encode_i(Opcode opcode, uint32_t rd, uint32_t funct3, uint32_t rs1,
                  int32_t imm);
uint32_t encode_s(Opcode opcode, uint32_t funct3, uint32_t rs1, uint32_t rs2,
                  int32_t imm);
uint32_t encode_b(Opcode opcode, uint32_t funct3, uint32_t rs1, uint32_t rs2,
                  int32_t offset);
uint32_t encode_u(Opcode opcode, uint32_t rd, uint32_t imm20);
uint32_t encode_j(Opcode opcode, uint32_t rd, int32_t offset);

uint32_t encode_tile(uint32_t funct3, uint32_t rd, uint32_t rs1, uint32_t rs2,
                     uint32_t funct7 = 0);

Decoded decode(uint32_t raw, uint32_t pc = 0);

}  // namespace clover
