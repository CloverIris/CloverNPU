#include "clover/iss.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void usage()
{
    std::cerr
        << "usage: clover-iss --image FILE [options]\n"
        << "  --base ADDR        load base (default 0x10000000)\n"
        << "  --entry ADDR       entry PC (default: base)\n"
        << "  --stack ADDR       initial sp (default 0x0003fff0)\n"
        << "  --bss N            zero N bytes after the image\n"
        << "  --max-steps N      instruction limit (default 10000000)\n"
        << "  --trace            print instruction trace\n"
        << "  --dump-regs        print registers on exit\n"
        << "  --echo             echo guest putchar output\n"
        << "  --quiet            suppress the summary\n";
}

uint32_t parse_addr(const std::string &text)
{
    return static_cast<uint32_t>(std::strtoull(text.c_str(), nullptr, 0));
}

uint64_t parse_u64(const std::string &text)
{
    return std::strtoull(text.c_str(), nullptr, 0);
}

}  // namespace

int main(int argc, char **argv)
{
    std::string image_path;
    clover::IssOptions options;
    bool dump_regs = false;
    bool quiet = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            usage();
            return 0;
        } else if (arg == "--image") {
            if (i + 1 >= argc) {
                usage();
                return 2;
            }
            image_path = argv[++i];
        } else if (arg == "--base") {
            if (i + 1 >= argc) {
                usage();
                return 2;
            }
            options.load_base = parse_addr(argv[++i]);
            options.entry = options.load_base;
        } else if (arg == "--entry") {
            if (i + 1 >= argc) {
                usage();
                return 2;
            }
            options.entry = parse_addr(argv[++i]);
        } else if (arg == "--stack") {
            if (i + 1 >= argc) {
                usage();
                return 2;
            }
            options.stack_top = parse_addr(argv[++i]);
        } else if (arg == "--bss") {
            if (i + 1 >= argc) {
                usage();
                return 2;
            }
            options.bss_size = parse_addr(argv[++i]);
        } else if (arg == "--max-steps") {
            if (i + 1 >= argc) {
                usage();
                return 2;
            }
            options.max_steps = parse_u64(argv[++i]);
        } else if (arg == "--trace") {
            options.trace = true;
        } else if (arg == "--dump-regs") {
            dump_regs = true;
        } else if (arg == "--echo") {
            options.echo_console = true;
        } else if (arg == "--quiet") {
            quiet = true;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "unknown option: " << arg << "\n";
            return 2;
        } else if (image_path.empty()) {
            image_path = arg;
        } else {
            std::cerr << "unexpected argument: " << arg << "\n";
            return 2;
        }
    }

    if (image_path.empty()) {
        usage();
        return 2;
    }

    std::ifstream in(image_path, std::ios::binary);
    if (!in) {
        std::cerr << "cannot open " << image_path << "\n";
        return 1;
    }
    std::vector<uint8_t> image((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());

    clover::Iss iss(options);
    std::string error;
    if (!iss.load_image(image, error)) {
        std::cerr << error << "\n";
        return 1;
    }

    const clover::IssResult result = iss.run();
    if (!quiet) {
        std::cout << "clover-iss: exit=" << result.exit_code
                  << " cycles=" << result.cycles
                  << " instret=" << result.instructions
                  << " pc=0x" << std::hex << result.pc << std::dec << "\n";
    }
    if (!result.console.empty() && !options.echo_console) {
        std::cout << result.console;
        if (result.console.back() != '\n') {
            std::cout << "\n";
        }
    }
    if (dump_regs) {
        for (uint32_t i = 0; i < 32; ++i) {
            std::cout << "x" << std::dec << i << "=" << std::hex << iss.reg(i)
                      << ((i % 4 == 3) ? "\n" : " ");
        }
        std::cout << std::dec;
    }
    if (!result.error.empty()) {
        std::cerr << "clover-iss: " << result.error << "\n";
        if (!result.halted || result.faulted) {
            return 1;
        }
    }
    return result.exit_code & 0xFF;
}
