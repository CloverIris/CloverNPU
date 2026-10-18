#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "clover/isa.h"

namespace clover {

struct AsmOptions {
    uint32_t base = kResetVector;
    std::string entry_symbol = "_start";
};

struct AsmResult {
    uint32_t base = kResetVector;
    uint32_t entry = kResetVector;
    uint32_t text_size = 0;
    uint32_t rodata_off = 0;
    uint32_t rodata_size = 0;
    uint32_t data_off = 0;
    uint32_t data_size = 0;
    uint32_t bss_size = 0;
    std::vector<uint8_t> image;
    std::map<std::string, uint32_t> symbols;
    std::vector<std::string> listing;
};

bool assemble(const std::string &source, const std::string &filename,
              const AsmOptions &options, AsmResult &result, std::string &error);

bool assemble_file(const std::string &path, const AsmOptions &options,
                   AsmResult &result, std::string &error);

}  // namespace clover
