#include "clover/assembler.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void usage()
{
    std::cerr
        << "usage: clover-as [options] input.s\n"
        << "  -o, --output FILE    output flat image (default a.bin)\n"
        << "  --base ADDR          load base (default 0x10000000)\n"
        << "  --entry SYMBOL       entry symbol (default _start)\n"
        << "  --map FILE           write symbol map\n"
        << "  --listing FILE       write disassembly listing\n";
}

uint32_t parse_addr(const std::string &text)
{
    return static_cast<uint32_t>(std::strtoul(text.c_str(), nullptr, 0));
}

}  // namespace

int main(int argc, char **argv)
{
    std::string input;
    std::string output = "a.bin";
    std::string map_path;
    std::string listing_path;
    clover::AsmOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            usage();
            return 0;
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argc) {
                usage();
                return 2;
            }
            output = argv[++i];
        } else if (arg == "--base") {
            if (i + 1 >= argc) {
                usage();
                return 2;
            }
            options.base = parse_addr(argv[++i]);
        } else if (arg == "--entry") {
            if (i + 1 >= argc) {
                usage();
                return 2;
            }
            options.entry_symbol = argv[++i];
        } else if (arg == "--map") {
            if (i + 1 >= argc) {
                usage();
                return 2;
            }
            map_path = argv[++i];
        } else if (arg == "--listing") {
            if (i + 1 >= argc) {
                usage();
                return 2;
            }
            listing_path = argv[++i];
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "unknown option: " << arg << "\n";
            return 2;
        } else {
            input = arg;
        }
    }

    if (input.empty()) {
        usage();
        return 2;
    }

    clover::AsmResult result;
    std::string error;
    if (!clover::assemble_file(input, options, result, error)) {
        std::cerr << error << "\n";
        return 1;
    }

    std::ofstream out(output, std::ios::binary);
    if (!out) {
        std::cerr << "cannot write " << output << "\n";
        return 1;
    }
    out.write(reinterpret_cast<const char *>(result.image.data()),
              static_cast<std::streamsize>(result.image.size()));
    out.close();

    if (!map_path.empty()) {
        std::ofstream map(map_path);
        if (!map) {
            std::cerr << "cannot write " << map_path << "\n";
            return 1;
        }
        map << "# Clover symbol map\n";
        for (const auto &[name, value] : result.symbols) {
            map << "0x" << std::hex << value << " " << name << "\n";
        }
    }

    if (!listing_path.empty()) {
        std::ofstream listing(listing_path);
        if (!listing) {
            std::cerr << "cannot write " << listing_path << "\n";
            return 1;
        }
        for (const auto &line : result.listing) {
            listing << line << "\n";
        }
    }

    std::cout << "clover-as: " << input << " -> " << output
              << " (entry=0x" << std::hex << result.entry
              << ", text=" << std::dec << result.text_size
              << ", rodata=" << result.rodata_size
              << ", data=" << result.data_size
              << ", bss=" << result.bss_size << ")\n";
    return 0;
}
