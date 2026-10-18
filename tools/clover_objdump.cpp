#include "clover/isa.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

void usage()
{
    std::cerr
        << "usage: clover-objdump [options] image.bin\n"
        << "  --base ADDR       load base (default 0x10000000)\n"
        << "  --length N        only disassemble N bytes\n"
        << "  --map FILE        symbol map (0xADDR name)\n";
}

}  // namespace

int main(int argc, char **argv)
{
    std::string input;
    std::string map_path;
    uint32_t base = 0x1000'0000u;
    uint32_t length = 0;
    bool have_length = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            usage();
            return 0;
        } else if (arg == "--base") {
            if (i + 1 >= argc) { usage(); return 2; }
            base = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 0));
        } else if (arg == "--length") {
            if (i + 1 >= argc) { usage(); return 2; }
            length = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 0));
            have_length = true;
        } else if (arg == "--map") {
            if (i + 1 >= argc) { usage(); return 2; }
            map_path = argv[++i];
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "unknown option: " << arg << "\n";
            return 2;
        } else if (input.empty()) {
            input = arg;
        } else {
            std::cerr << "unexpected argument: " << arg << "\n";
            return 2;
        }
    }

    if (input.empty()) {
        usage();
        return 2;
    }

    std::ifstream in(input, std::ios::binary);
    if (!in) {
        std::cerr << "cannot open " << input << "\n";
        return 1;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());

    std::map<uint32_t, std::string> symbols;
    if (!map_path.empty()) {
        std::ifstream map(map_path);
        std::string line;
        while (std::getline(map, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }
            std::istringstream parser(line);
            std::string address;
            std::string name;
            parser >> address >> name;
            if (!address.empty() && !name.empty()) {
                symbols[static_cast<uint32_t>(
                    std::strtoul(address.c_str(), nullptr, 0))] = name;
            }
        }
        std::cout << "symbols:\n";
        for (const auto &[address, name] : symbols) {
            std::cout << "  0x" << std::hex << address << " " << name << "\n";
        }
        std::cout << std::dec << "\n";
    }

    const uint32_t limit =
        have_length ? std::min<uint32_t>(length, static_cast<uint32_t>(bytes.size()))
                    : static_cast<uint32_t>(bytes.size());
    for (uint32_t offset = 0; offset + 4 <= limit; offset += 4) {
        uint32_t raw = 0;
        for (int i = 0; i < 4; ++i) {
            raw |= static_cast<uint32_t>(bytes[offset + i]) << (8 * i);
        }
        const uint32_t pc = base + offset;
        const clover::Decoded decoded = clover::decode(raw, pc);
        std::cout << "0x" << std::hex << pc << "  " << raw << "  "
                  << decoded.text;
        auto symbol = symbols.find(decoded.target);
        if (decoded.target != 0 && symbol != symbols.end()) {
            std::cout << " <" << symbol->second << ">";
        }
        std::cout << "\n";
    }
    return 0;
}
