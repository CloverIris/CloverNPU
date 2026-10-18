#pragma once

#include <string>

namespace clover {

struct CompileOptions {
    bool emit_start = true;
    bool dump_tokens = false;
    bool dump_ast = false;
    bool bounds_check = false;
};

struct CompileResult {
    std::string assembly;
    std::string error;
    bool ok = false;
};

CompileResult compile_to_assembly(const std::string &source,
                                  const std::string &filename,
                                  const CompileOptions &options = {});

}  // namespace clover
