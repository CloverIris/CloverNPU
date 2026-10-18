#include "clover/assembler.h"
#include "clover/compiler.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

void usage()
{
    std::cerr
        << "usage: cloverc [options] input.c\n"
        << "  -S                  emit Clover-S assembly\n"
        << "  -o FILE             output (default a.bin / a.s with -S)\n"
        << "  --asm FILE          also write assembly to FILE\n"
        << "  --map FILE          write symbol map\n"
        << "  --base ADDR         load base (default 0x10000000)\n"
        << "  --entry SYMBOL      entry symbol (default _start)\n"
        << "  --no-start          do not emit the _start wrapper\n"
        << "  --dump-tokens       dump tokens before compiling\n";
}

}  // namespace

int main(int argc, char **argv)
{
    std::string input;
    std::string output;
    std::string asm_path;
    std::string map_path;
    std::string base_text = "0x10000000";
    std::string entry_symbol = "_start";
    bool emit_asm = false;
    clover::CompileOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            usage();
            return 0;
        } else if (arg == "-S") {
            emit_asm = true;
        } else if (arg == "-o") {
            if (i + 1 >= argc) { usage(); return 2; }
            output = argv[++i];
        } else if (arg == "--asm") {
            if (i + 1 >= argc) { usage(); return 2; }
            asm_path = argv[++i];
        } else if (arg == "--map") {
            if (i + 1 >= argc) { usage(); return 2; }
            map_path = argv[++i];
        } else if (arg == "--base") {
            if (i + 1 >= argc) { usage(); return 2; }
            base_text = argv[++i];
        } else if (arg == "--entry") {
            if (i + 1 >= argc) { usage(); return 2; }
            entry_symbol = argv[++i];
        } else if (arg == "--no-start") {
            options.emit_start = false;
            entry_symbol.clear();
        } else if (arg == "--dump-tokens") {
            options.dump_tokens = true;
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
    std::ostringstream source;
    source << in.rdbuf();

    clover::CompileResult compiled =
        clover::compile_to_assembly(source.str(), input, options);
    if (!compiled.ok) {
        std::cerr << compiled.error;
        if (!compiled.error.empty() && compiled.error.back() != '\n') {
            std::cerr << "\n";
        }
        return 1;
    }

    if (emit_asm) {
        const std::string path = output.empty() ? "a.s" : output;
        std::ofstream out(path);
        if (!out) {
            std::cerr << "cannot write " << path << "\n";
            return 1;
        }
        out << compiled.assembly;
        std::cout << "cloverc: " << input << " -> " << path << "\n";
        return 0;
    }

    if (!asm_path.empty()) {
        std::ofstream out(asm_path);
        if (!out) {
            std::cerr << "cannot write " << asm_path << "\n";
            return 1;
        }
        out << compiled.assembly;
    }

    clover::AsmOptions asm_options;
    asm_options.base =
        static_cast<uint32_t>(std::strtoul(base_text.c_str(), nullptr, 0));
    asm_options.entry_symbol = entry_symbol;
    clover::AsmResult assembled;
    std::string error;
    if (!clover::assemble(compiled.assembly, input + ".s", asm_options,
                          assembled, error)) {
        std::cerr << error << "\n";
        if (!asm_path.empty()) {
            std::cerr << "assembly kept at " << asm_path << "\n";
        }
        return 1;
    }

    const std::string path = output.empty() ? "a.bin" : output;
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "cannot write " << path << "\n";
        return 1;
    }
    out.write(reinterpret_cast<const char *>(assembled.image.data()),
              static_cast<std::streamsize>(assembled.image.size()));

    if (!map_path.empty()) {
        std::ofstream map(map_path);
        for (const auto &[name, value] : assembled.symbols) {
            map << "0x" << std::hex << value << " " << name << "\n";
        }
    }

    std::cout << "cloverc: " << input << " -> " << path << " (entry=0x"
              << std::hex << assembled.entry << ", text=" << std::dec
              << assembled.text_size << ", data=" << assembled.data_size
              << ", bss=" << assembled.bss_size << ")\n";
    return 0;
}
