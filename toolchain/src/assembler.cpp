#include "clover/assembler.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace clover {
namespace {

enum class Section { Text, Rodata, Data, Bss };

struct LabelRef {
    Section section = Section::Text;
    uint32_t offset = 0;
};

struct ParsedLine {
    std::string file;
    int line = 0;
    Section section = Section::Text;
    std::vector<std::string> labels;
    std::string mnemonic;
    std::vector<std::string> operands;
    std::string raw;
};

struct SectionBytes {
    std::vector<uint8_t> bytes;
    uint32_t bss_size = 0;
    uint32_t base = 0;

    uint32_t offset() const
    {
        if (!bytes.empty() || base != 0) {
            return static_cast<uint32_t>(bytes.size());
        }
        return bss_size;
    }
};

std::string trim(std::string s)
{
    size_t begin = 0;
    while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin]))) {
        ++begin;
    }
    size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(begin, end - begin);
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string strip_comment(const std::string &line)
{
    bool in_string = false;
    bool in_char = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (in_string) {
            if (c == '\\') {
                ++i;
            } else if (c == '"') {
                in_string = false;
            }
        } else if (in_char) {
            if (c == '\\') {
                ++i;
            } else if (c == '\'') {
                in_char = false;
            }
        } else if (c == '"') {
            in_string = true;
        } else if (c == '\'') {
            in_char = true;
        } else if (c == '#') {
            return line.substr(0, i);
        }
    }
    return line;
}

std::vector<std::string> split_operands(const std::string &text)
{
    std::vector<std::string> result;
    std::string current;
    int depth = 0;
    bool in_string = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (in_string) {
            current.push_back(c);
            if (c == '\\' && i + 1 < text.size()) {
                current.push_back(text[++i]);
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
            current.push_back(c);
        } else if (c == '(') {
            ++depth;
            current.push_back(c);
        } else if (c == ')') {
            --depth;
            current.push_back(c);
        } else if (c == ',' && depth == 0) {
            result.push_back(trim(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!trim(current).empty()) {
        result.push_back(trim(current));
    }
    return result;
}

bool is_identifier_start(char c)
{
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' ||
           c == '.' || c == '$';
}

std::string encode_string(const std::string &literal, std::string &error)
{
    if (literal.size() < 2 || literal.front() != '"' || literal.back() != '"') {
        error = "malformed string literal";
        return {};
    }
    std::string out;
    for (size_t i = 1; i + 1 < literal.size(); ++i) {
        char c = literal[i];
        if (c != '\\') {
            out.push_back(c);
            continue;
        }
        if (++i + 1 > literal.size()) {
            error = "unterminated escape";
            return {};
        }
        c = literal[i];
        switch (c) {
        case 'n': out.push_back('\n'); break;
        case 't': out.push_back('\t'); break;
        case 'r': out.push_back('\r'); break;
        case '0': out.push_back('\0'); break;
        case '\\': out.push_back('\\'); break;
        case '"': out.push_back('"'); break;
        case '\'': out.push_back('\''); break;
        case 'x': {
            int value = 0;
            int digits = 0;
            while (i + 1 < literal.size() &&
                   std::isxdigit(static_cast<unsigned char>(literal[i + 1])) &&
                   digits < 2) {
                const char h = literal[++i];
                value = value * 16 + (std::isdigit(static_cast<unsigned char>(h))
                                          ? h - '0'
                                          : std::tolower(h) - 'a' + 10);
                ++digits;
            }
            if (digits == 0) {
                error = "bad \\x escape";
                return {};
            }
            out.push_back(static_cast<char>(value));
            break;
        }
        default:
            out.push_back(c);
            break;
        }
    }
    return out;
}

struct EvalContext {
    const std::map<std::string, uint32_t> *symbols = nullptr;
    const std::map<std::string, int64_t> *constants = nullptr;
};

int64_t parse_number(const std::string &text, bool &ok)
{
    ok = true;
    if (text.empty()) {
        ok = false;
        return 0;
    }
    std::string s = text;
    bool negative = false;
    if (s[0] == '-') {
        negative = true;
        s = s.substr(1);
    } else if (s[0] == '+') {
        s = s.substr(1);
    }
    if (s.empty()) {
        ok = false;
        return 0;
    }
    int base = 10;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s = s.substr(2);
    } else if (s.size() > 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
        base = 2;
        s = s.substr(2);
    } else if (s.size() > 2 && s[0] == '0' && (s[1] == 'o' || s[1] == 'O')) {
        base = 8;
        s = s.substr(2);
    }
    char *end = nullptr;
    const long long value = std::strtoll(s.c_str(), &end, base);
    if (!end || *end != '\0') {
        ok = false;
        return 0;
    }
    return negative ? -value : value;
}

int64_t parse_char_literal(const std::string &text, bool &ok)
{
    ok = false;
    if (text.size() < 3 || text.front() != '\'' || text.back() != '\'') {
        return 0;
    }
    std::string body = text.substr(1, text.size() - 2);
    if (body.empty()) {
        return 0;
    }
    if (body[0] != '\\') {
        if (body.size() != 1) {
            return 0;
        }
        ok = true;
        return static_cast<unsigned char>(body[0]);
    }
    const char esc = body.size() > 1 ? body[1] : '\0';
    ok = true;
    switch (esc) {
    case 'n': return '\n';
    case 't': return '\t';
    case 'r': return '\r';
    case '0': return 0;
    case '\\': return '\\';
    case '\'': return '\'';
    case '"': return '"';
    default: ok = false; return 0;
    }
}

bool eval_expr(const std::string &input, const EvalContext &ctx, int64_t &out,
               std::string &error);

bool eval_term(const std::string &input, const EvalContext &ctx, int64_t &out,
               std::string &error)
{
    std::string s = trim(input);
    if (s.empty()) {
        error = "empty expression";
        return false;
    }
    bool ok = false;
    if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') {
        const int64_t value = parse_char_literal(s, ok);
        if (ok) {
            out = value;
            return true;
        }
    }
    const int64_t number = parse_number(s, ok);
    if (ok) {
        out = number;
        return true;
    }
    if (!s.empty() && s[0] == '(') {
        int depth = 0;
        size_t close = std::string::npos;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '(') {
                ++depth;
            } else if (s[i] == ')') {
                --depth;
                if (depth == 0) {
                    close = i;
                    break;
                }
            }
        }
        if (close == s.size() - 1) {
            return eval_expr(s.substr(1, s.size() - 2), ctx, out, error);
        }
    }
    if (ctx.constants) {
        auto it = ctx.constants->find(s);
        if (it != ctx.constants->end()) {
            out = it->second;
            return true;
        }
    }
    if (ctx.symbols) {
        auto it = ctx.symbols->find(s);
        if (it != ctx.symbols->end()) {
            out = it->second;
            return true;
        }
    }
    error = "undefined symbol or bad expression: " + s;
    return false;
}

bool eval_expr(const std::string &input, const EvalContext &ctx, int64_t &out,
               std::string &error)
{
    std::string s = trim(input);
    if (s.empty()) {
        error = "empty expression";
        return false;
    }

    // %hi(...) / %lo(...)
    if (s.rfind("%hi(", 0) == 0 || s.rfind("%lo(", 0) == 0) {
        const bool hi = s[1] == 'h';
        if (s.back() != ')') {
            error = "malformed %hi/%lo expression: " + s;
            return false;
        }
        int64_t value = 0;
        if (!eval_expr(s.substr(4, s.size() - 5), ctx, value, error)) {
            return false;
        }
        if (hi) {
            out = (value + 0x800) >> 12;
        } else {
            out = value & 0xFFF;
            if (out & 0x800) {
                out -= 0x1000;
            }
        }
        return true;
    }

    // Split on the last top-level +/- that is not a unary sign.
    int depth = 0;
    for (int i = static_cast<int>(s.size()) - 1; i > 0; --i) {
        const char c = s[static_cast<size_t>(i)];
        if (c == ')') {
            ++depth;
        } else if (c == '(') {
            --depth;
        } else if (depth == 0 && (c == '+' || c == '-')) {
            const char prev = s[static_cast<size_t>(i) - 1];
            if (prev == 'e' || prev == 'E') {
                continue;
            }
            int64_t lhs = 0;
            int64_t rhs = 0;
            if (!eval_expr(s.substr(0, static_cast<size_t>(i)), ctx, lhs, error) ||
                !eval_expr(s.substr(static_cast<size_t>(i) + 1), ctx, rhs, error)) {
                return false;
            }
            out = (c == '+') ? lhs + rhs : lhs - rhs;
            return true;
        }
    }

    if (s[0] == '-' || s[0] == '+') {
        int64_t value = 0;
        if (!eval_expr(s.substr(1), ctx, value, error)) {
            return false;
        }
        out = (s[0] == '-') ? -value : value;
        return true;
    }

    return eval_term(s, ctx, out, error);
}

uint32_t align_up(uint32_t value, uint32_t alignment)
{
    if (alignment == 0) {
        return value;
    }
    return (value + alignment - 1) & ~(alignment - 1);
}

uint32_t pseudo_words(const std::string &mnemonic)
{
    const std::string m = lower(mnemonic);
    if (m == "li" || m == "la" || m == "call" || m == "tail") {
        return 2;
    }
    return 1;
}

uint32_t csr_number(const std::string &name)
{
    static const std::map<std::string, uint32_t> kCsrs = {
        {"mstatus", 0x300}, {"mie", 0x304}, {"mtvec", 0x305},
        {"mscratch", 0x340}, {"mepc", 0x341}, {"mcause", 0x342},
        {"mtval", 0x343}, {"mip", 0x344}, {"cycle", 0xC00},
        {"instret", 0xC02}, {"tile_status", 0x7C0}, {"tile_error", 0x7C1},
        {"dma_status", 0x7C2}, {"cp_status", 0x7C3}, {"hart_id", 0x7C4},
    };
    if (name.size() > 2 && name[0] == '0' && (name[1] == 'x' || name[1] == 'X')) {
        bool ok = false;
        const int64_t value = parse_number(name, ok);
        if (ok) {
            return static_cast<uint32_t>(value);
        }
    }
    auto it = kCsrs.find(name);
    if (it != kCsrs.end()) {
        return it->second;
    }
    bool ok = false;
    const int64_t value = parse_number(name, ok);
    return ok ? static_cast<uint32_t>(value) : 0xFFFFFFFFu;
}

bool parse_mem(const std::string &text, uint32_t &rs1, int64_t &imm,
               std::string &error)
{
    const std::string s = trim(text);
    const size_t open = s.find('(');
    const size_t close = s.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close < open) {
        error = "expected imm(reg) operand: " + s;
        return false;
    }
    const std::string imm_text = trim(s.substr(0, open));
    const std::string reg_text = trim(s.substr(open + 1, close - open - 1));
    const auto reg = reg_from_name(reg_text);
    if (!reg) {
        error = "bad register: " + reg_text;
        return false;
    }
    rs1 = *reg;
    bool ok = false;
    imm = parse_number(imm_text.empty() ? "0" : imm_text, ok);
    if (!ok) {
        error = "bad immediate: " + imm_text;
        return false;
    }
    return true;
}

Section parse_section(const std::string &name)
{
    std::string s = name;
    const size_t comma = s.find(',');
    if (comma != std::string::npos) {
        s = s.substr(0, comma);
    }
    s = trim(s);
    if (s == ".text") {
        return Section::Text;
    }
    if (s == ".rodata" || s == ".srodata") {
        return Section::Rodata;
    }
    if (s == ".data" || s == ".sdata") {
        return Section::Data;
    }
    if (s == ".bss" || s == ".sbss") {
        return Section::Bss;
    }
    throw std::runtime_error("unknown section: " + s);
}

}  // namespace

bool assemble(const std::string &source, const std::string &filename,
              const AsmOptions &options, AsmResult &result, std::string &error)
{
    std::vector<ParsedLine> lines;
    std::map<std::string, LabelRef> label_refs;
    std::map<std::string, int64_t> constants;
    std::map<std::string, bool> globals;
    Section section = Section::Text;
    SectionBytes sections[4];
    uint32_t current_offset = 0;

    auto fail = [&](int line, const std::string &message) {
        error = filename + ":" + std::to_string(line) + ": " + message;
        return false;
    };

    auto section_index = [](Section s) { return static_cast<int>(s); };

    // ---------------- first pass: labels, sizes, constants ----------------
    std::istringstream input(source);
    std::string raw;
    int line_number = 0;
    while (std::getline(input, raw)) {
        ++line_number;
        std::string text = trim(strip_comment(raw));
        if (text.empty()) {
            continue;
        }
        ParsedLine parsed;
        parsed.file = filename;
        parsed.line = line_number;
        parsed.raw = text;

        // Leading labels.
        while (true) {
            bool in_string = false;
            size_t colon = std::string::npos;
            for (size_t i = 0; i < text.size(); ++i) {
                if (text[i] == '"') {
                    in_string = !in_string;
                } else if (text[i] == ':' && !in_string) {
                    colon = i;
                    break;
                }
            }
            if (colon == std::string::npos) {
                break;
            }
            std::string label = trim(text.substr(0, colon));
            if (label.empty() || !is_identifier_start(label[0])) {
                break;
            }
            label_refs[label] = {section, current_offset};
            parsed.labels.push_back(label);
            text = trim(text.substr(colon + 1));
        }
        if (text.empty()) {
            lines.push_back(parsed);
            continue;
        }

        size_t space = 0;
        while (space < text.size() &&
               !std::isspace(static_cast<unsigned char>(text[space]))) {
            ++space;
        }
        parsed.mnemonic = text.substr(0, space);
        parsed.operands = split_operands(text.substr(space));
        parsed.section = section;
        lines.push_back(parsed);

        const std::string m = lower(parsed.mnemonic);
        if (m == ".text") {
            section = Section::Text;
            current_offset = sections[section_index(section)].offset();
            continue;
        }
        if (m == ".rodata" || m == ".srodata") {
            section = Section::Rodata;
            current_offset = sections[section_index(section)].offset();
            continue;
        }
        if (m == ".data" || m == ".sdata") {
            section = Section::Data;
            current_offset = sections[section_index(section)].offset();
            continue;
        }
        if (m == ".bss" || m == ".sbss") {
            section = Section::Bss;
            current_offset = sections[section_index(section)].offset();
            continue;
        }
        if (m == ".section") {
            try {
                section = parse_section(parsed.operands.empty() ? ".text"
                                                                 : parsed.operands[0]);
            } catch (const std::exception &e) {
                return fail(line_number, e.what());
            }
            current_offset = sections[section_index(section)].offset();
            continue;
        }
        if (m == ".globl" || m == ".global") {
            for (const auto &op : parsed.operands) {
                globals[op] = true;
            }
            continue;
        }
        if (m == ".local") {
            for (const auto &op : parsed.operands) {
                globals[op] = false;
            }
            continue;
        }
        if (m == ".equ" || m == ".set") {
            if (parsed.operands.size() != 2) {
                return fail(line_number, ".equ expects name, value");
            }
            EvalContext ctx{nullptr, &constants};
            int64_t value = 0;
            std::string eval_error;
            if (!eval_expr(parsed.operands[1], ctx, value, eval_error)) {
                return fail(line_number, eval_error);
            }
            constants[parsed.operands[0]] = value;
            continue;
        }
        if (m == ".align" || m == ".p2align" || m == ".balign") {
            if (parsed.operands.empty()) {
                return fail(line_number, m + " expects an argument");
            }
            EvalContext ctx{nullptr, &constants};
            int64_t value = 0;
            std::string eval_error;
            if (!eval_expr(parsed.operands[0], ctx, value, eval_error)) {
                return fail(line_number, eval_error);
            }
            uint32_t alignment = (m == ".balign")
                                     ? static_cast<uint32_t>(value)
                                     : (1u << static_cast<uint32_t>(value));
            if (alignment == 0) {
                alignment = 1;
            }
            if (section == Section::Bss) {
                sections[section_index(section)].bss_size =
                    align_up(sections[section_index(section)].bss_size, alignment);
                current_offset = sections[section_index(section)].bss_size;
            } else {
                auto &bytes = sections[section_index(section)].bytes;
                bytes.resize(align_up(static_cast<uint32_t>(bytes.size()), alignment), 0);
                current_offset = static_cast<uint32_t>(bytes.size());
            }
            continue;
        }
        if (m == ".space" || m == ".zero" || m == ".skip") {
            if (parsed.operands.empty()) {
                return fail(line_number, m + " expects a size");
            }
            EvalContext ctx{nullptr, &constants};
            int64_t count = 0;
            std::string eval_error;
            if (!eval_expr(parsed.operands[0], ctx, count, eval_error)) {
                return fail(line_number, eval_error);
            }
            if (count < 0) {
                return fail(line_number, "negative .space");
            }
            if (section == Section::Bss) {
                sections[section_index(section)].bss_size +=
                    static_cast<uint32_t>(count);
                current_offset = sections[section_index(section)].bss_size;
            } else {
                auto &bytes = sections[section_index(section)].bytes;
                bytes.resize(bytes.size() + static_cast<size_t>(count), 0);
                current_offset = static_cast<uint32_t>(bytes.size());
            }
            continue;
        }
        if (m == ".byte" || m == ".half" || m == ".word" || m == ".short" ||
            m == ".long" || m == ".int" || m == ".asciz" || m == ".string") {
            uint32_t size = 0;
            for (const auto &op : parsed.operands) {
                if (!op.empty() && op.front() == '"') {
                    std::string string_error;
                    const std::string bytes = encode_string(op, string_error);
                    if (!string_error.empty()) {
                        return fail(line_number, string_error);
                    }
                    size += static_cast<uint32_t>(bytes.size());
                    if (m == ".asciz" || m == ".string") {
                        size += 1;
                    }
                } else {
                    size += (m == ".byte") ? 1 : (m == ".half" || m == ".short") ? 2 : 4;
                }
            }
            if (section == Section::Bss) {
                sections[section_index(section)].bss_size += size;
                current_offset = sections[section_index(section)].bss_size;
            } else {
                auto &bytes = sections[section_index(section)].bytes;
                bytes.resize(bytes.size() + size, 0);
                current_offset = static_cast<uint32_t>(bytes.size());
            }
            continue;
        }
        if (!m.empty() && m[0] == '.') {
            // Ignore metadata directives.
            continue;
        }
        const uint32_t words = pseudo_words(m);
        if (section == Section::Bss) {
            return fail(line_number, "instructions are not allowed in .bss");
        }
        auto &bytes = sections[section_index(section)].bytes;
        bytes.resize(bytes.size() + words * 4, 0);
        current_offset = static_cast<uint32_t>(bytes.size());
    }

    // ---------------- layout ----------------
    SectionBytes &text = sections[section_index(Section::Text)];
    SectionBytes &rodata = sections[section_index(Section::Rodata)];
    SectionBytes &data = sections[section_index(Section::Data)];
    SectionBytes &bss = sections[section_index(Section::Bss)];

    text.base = options.base;
    rodata.base = align_up(text.base + static_cast<uint32_t>(text.bytes.size()), 16);
    data.base = align_up(rodata.base + static_cast<uint32_t>(rodata.bytes.size()), 16);
    bss.base = align_up(data.base + static_cast<uint32_t>(data.bytes.size()), 16);

    std::map<std::string, uint32_t> symbols;
    for (const auto &[name, ref] : label_refs) {
        symbols[name] = sections[section_index(ref.section)].base + ref.offset;
    }

    result.base = options.base;
    result.text_size = static_cast<uint32_t>(text.bytes.size());
    result.rodata_off = rodata.base - options.base;
    result.rodata_size = static_cast<uint32_t>(rodata.bytes.size());
    result.data_off = data.base - options.base;
    result.data_size = static_cast<uint32_t>(data.bytes.size());
    result.bss_size = bss.bss_size;
    result.symbols = symbols;

    if (!options.entry_symbol.empty()) {
        auto it = symbols.find(options.entry_symbol);
        if (it == symbols.end()) {
            error = "entry symbol not found: " + options.entry_symbol;
            return false;
        }
        result.entry = it->second;
    } else {
        result.entry = options.base;
    }

    // ---------------- second pass ----------------
    for (auto &s : sections) {
        s.bytes.clear();
    }
    Section current_section = Section::Text;
    for (const auto &line : lines) {
        if (line.labels.empty() && line.mnemonic.empty()) {
            continue;
        }
        if (line.mnemonic.empty()) {
            continue;
        }
        const std::string m = lower(line.mnemonic);
        if (m == ".text") {
            current_section = Section::Text;
            continue;
        }
        if (m == ".rodata" || m == ".srodata") {
            current_section = Section::Rodata;
            continue;
        }
        if (m == ".data" || m == ".sdata") {
            current_section = Section::Data;
            continue;
        }
        if (m == ".bss" || m == ".sbss") {
            current_section = Section::Bss;
            continue;
        }
        if (m == ".section") {
            try {
                current_section = parse_section(line.operands.empty()
                                                    ? ".text"
                                                    : line.operands[0]);
            } catch (const std::exception &e) {
                return fail(line.line, e.what());
            }
            continue;
        }
        if (m == ".globl" || m == ".global" || m == ".local" || m == ".equ" ||
            m == ".set" || m == ".file" || m == ".type" || m == ".size" ||
            m == ".ident" || m == ".option" || m == ".cfi_startproc" ||
            m == ".cfi_endproc" || m == ".cfi_def_cfa_offset" ||
            m == ".cfi_offset") {
            continue;
        }

        SectionBytes &out = sections[section_index(current_section)];
        if (current_section == Section::Bss) {
            continue;
        }

        auto emit32 = [&](uint32_t value) {
            out.bytes.push_back(static_cast<uint8_t>(value & 0xFF));
            out.bytes.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
            out.bytes.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
            out.bytes.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
        };

        EvalContext ctx{&symbols, &constants};
        auto eval = [&](const std::string &expr, int64_t &value) {
            std::string eval_error;
            if (!eval_expr(expr, ctx, value, eval_error)) {
                error = filename + ":" + std::to_string(line.line) + ": " +
                        eval_error;
                return false;
            }
            return true;
        };
        auto reg = [&](const std::string &name, uint32_t &value) {
            auto r = reg_from_name(name);
            if (!r) {
                error = filename + ":" + std::to_string(line.line) +
                        ": bad register: " + name;
                return false;
            }
            value = *r;
            return true;
        };
        auto pc = [&]() {
            return out.base + static_cast<uint32_t>(out.bytes.size());
        };
        auto need = [&](size_t n) {
            if (line.operands.size() != n) {
                error = filename + ":" + std::to_string(line.line) + ": " +
                        m + " expects " + std::to_string(n) + " operands";
                return false;
            }
            return true;
        };

        if (m == ".byte" || m == ".half" || m == ".short" || m == ".word" ||
            m == ".long" || m == ".int" || m == ".asciz" || m == ".string") {
            for (const auto &op : line.operands) {
                if (!op.empty() && op.front() == '"') {
                    std::string string_error;
                    const std::string bytes = encode_string(op, string_error);
                    if (!string_error.empty()) {
                        return fail(line.line, string_error);
                    }
                    out.bytes.insert(out.bytes.end(), bytes.begin(), bytes.end());
                    if (m == ".asciz" || m == ".string") {
                        out.bytes.push_back(0);
                    }
                } else {
                    int64_t value = 0;
                    if (!eval(op, value)) {
                        return false;
                    }
                    if (m == ".byte") {
                        out.bytes.push_back(static_cast<uint8_t>(value));
                    } else if (m == ".half" || m == ".short") {
                        out.bytes.push_back(static_cast<uint8_t>(value & 0xFF));
                        out.bytes.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
                    } else {
                        emit32(static_cast<uint32_t>(value));
                    }
                }
            }
            continue;
        }
        if (m == ".align" || m == ".p2align" || m == ".balign") {
            int64_t value = 0;
            if (!eval(line.operands[0], value)) {
                return false;
            }
            uint32_t alignment = (m == ".balign") ? static_cast<uint32_t>(value)
                                                  : (1u << static_cast<uint32_t>(value));
            if (alignment == 0) {
                alignment = 1;
            }
            out.bytes.resize(align_up(static_cast<uint32_t>(out.bytes.size()),
                                      alignment),
                             0);
            continue;
        }
        if (m == ".space" || m == ".zero" || m == ".skip") {
            int64_t count = 0;
            if (!eval(line.operands[0], count)) {
                return false;
            }
            out.bytes.resize(out.bytes.size() + static_cast<size_t>(count), 0);
            continue;
        }
        if (m[0] == '.') {
            continue;
        }

        // Pseudo instructions.
        if (m == "nop") {
            emit32(encode_i(Opcode::OpImm, zero, 0, zero, 0));
            continue;
        }
        if (m == "mv") {
            if (!need(2)) return false;
            uint32_t rd, rs;
            if (!reg(line.operands[0], rd) || !reg(line.operands[1], rs)) return false;
            emit32(encode_i(Opcode::OpImm, rd, 0, rs, 0));
            continue;
        }
        if (m == "li") {
            if (!need(2)) return false;
            uint32_t rd;
            if (!reg(line.operands[0], rd)) return false;
            int64_t value = 0;
            if (!eval(line.operands[1], value)) return false;
            const uint32_t imm = static_cast<uint32_t>(value);
            const uint32_t hi = (imm + 0x800u) >> 12;
            const int32_t lo = static_cast<int32_t>(imm & 0xFFFu) -
                               (static_cast<int32_t>(imm & 0x800u) ? 0x1000 : 0);
            emit32(encode_u(Opcode::Lui, rd, hi));
            emit32(encode_i(Opcode::OpImm, rd, 0, rd, lo));
            continue;
        }
        if (m == "la") {
            if (!need(2)) return false;
            uint32_t rd;
            if (!reg(line.operands[0], rd)) return false;
            int64_t value = 0;
            if (!eval(line.operands[1], value)) return false;
            const uint32_t imm = static_cast<uint32_t>(value);
            const uint32_t hi = (imm + 0x800u) >> 12;
            const int32_t lo = static_cast<int32_t>(imm & 0xFFFu) -
                               (static_cast<int32_t>(imm & 0x800u) ? 0x1000 : 0);
            emit32(encode_u(Opcode::Lui, rd, hi));
            emit32(encode_i(Opcode::OpImm, rd, 0, rd, lo));
            continue;
        }
        if (m == "not") {
            if (!need(2)) return false;
            uint32_t rd, rs;
            if (!reg(line.operands[0], rd) || !reg(line.operands[1], rs)) return false;
            emit32(encode_i(Opcode::OpImm, rd, 4, rs, -1));
            continue;
        }
        if (m == "neg") {
            if (!need(2)) return false;
            uint32_t rd, rs;
            if (!reg(line.operands[0], rd) || !reg(line.operands[1], rs)) return false;
            emit32(encode_r(Opcode::Op, rd, 0, zero, rs, 0x20));
            continue;
        }
        if (m == "ret") {
            emit32(encode_i(Opcode::Jalr, zero, 0, ra, 0));
            continue;
        }
        if (m == "jr") {
            if (!need(1)) return false;
            uint32_t rs;
            if (!reg(line.operands[0], rs)) return false;
            emit32(encode_i(Opcode::Jalr, zero, 0, rs, 0));
            continue;
        }
        if (m == "j") {
            if (!need(1)) return false;
            int64_t target = 0;
            if (!eval(line.operands[0], target)) return false;
            const int64_t offset = target - static_cast<int64_t>(pc());
            if (offset < -(1 << 20) || offset > (1 << 20) - 2 || (offset & 1)) {
                return fail(line.line, "jal out of range");
            }
            emit32(encode_j(Opcode::Jal, zero, static_cast<int32_t>(offset)));
            continue;
        }
        if (m == "call" || m == "tail") {
            if (!need(1)) return false;
            int64_t target = 0;
            if (!eval(line.operands[0], target)) return false;
            const uint32_t imm = static_cast<uint32_t>(target);
            const uint32_t hi = (imm + 0x800u) >> 12;
            const int32_t lo = static_cast<int32_t>(imm & 0xFFFu) -
                               (static_cast<int32_t>(imm & 0x800u) ? 0x1000 : 0);
            const uint32_t base_reg = (m == "call") ? ra : t1;
            emit32(encode_u(Opcode::Lui, base_reg, hi));
            emit32(encode_i(Opcode::Jalr, (m == "call") ? ra : zero, 0,
                            base_reg, lo));
            continue;
        }
        if (m == "beqz" || m == "bnez") {
            if (!need(2)) return false;
            uint32_t rs;
            if (!reg(line.operands[0], rs)) return false;
            int64_t target = 0;
            if (!eval(line.operands[1], target)) return false;
            const int64_t offset = target - static_cast<int64_t>(pc());
            if (offset < -4096 || offset > 4094 || (offset & 1)) {
                return fail(line.line, "branch out of range");
            }
            const uint32_t funct3 = (m == "beqz") ? 0 : 1;
            emit32(encode_b(Opcode::Branch, funct3, rs, zero,
                            static_cast<int32_t>(offset)));
            continue;
        }
        if (m == "seqz" || m == "snez" || m == "sltz" || m == "sgtz") {
            if (!need(2)) return false;
            uint32_t rd, rs;
            if (!reg(line.operands[0], rd) || !reg(line.operands[1], rs)) return false;
            if (m == "seqz") {
                emit32(encode_i(Opcode::OpImm, rd, 3, rs, 1));
            } else if (m == "snez") {
                emit32(encode_r(Opcode::Op, rd, 3, zero, rs, 0));
            } else if (m == "sltz") {
                emit32(encode_r(Opcode::Op, rd, 2, rs, zero, 0));
            } else {
                emit32(encode_r(Opcode::Op, rd, 2, zero, rs, 0));
            }
            continue;
        }

        // R-type ALU.
        struct RInfo { uint32_t funct3; uint32_t funct7; };
        static const std::map<std::string, RInfo> kR = {
            {"add", {0, 0x00}}, {"sub", {0, 0x20}}, {"sll", {1, 0x00}},
            {"slt", {2, 0x00}}, {"sltu", {3, 0x00}}, {"xor", {4, 0x00}},
            {"srl", {5, 0x00}}, {"sra", {5, 0x20}}, {"or", {6, 0x00}},
            {"and", {7, 0x00}}, {"mul", {0, 0x01}}, {"mulh", {1, 0x01}},
            {"mulhsu", {2, 0x01}}, {"mulhu", {3, 0x01}}, {"div", {4, 0x01}},
            {"divu", {5, 0x01}}, {"rem", {6, 0x01}}, {"remu", {7, 0x01}},
        };
        auto rit = kR.find(m);
        if (rit != kR.end()) {
            if (!need(3)) return false;
            uint32_t rd, rs1, rs2;
            if (!reg(line.operands[0], rd) || !reg(line.operands[1], rs1) ||
                !reg(line.operands[2], rs2)) {
                return false;
            }
            emit32(encode_r(Opcode::Op, rd, rit->second.funct3, rs1, rs2,
                            rit->second.funct7));
            continue;
        }

        // I-type ALU.
        struct IInfo { uint32_t funct3; uint32_t funct7; };
        static const std::map<std::string, IInfo> kI = {
            {"addi", {0, 0}}, {"slti", {2, 0}}, {"sltiu", {3, 0}},
            {"xori", {4, 0}}, {"ori", {6, 0}}, {"andi", {7, 0}},
        };
        auto iit = kI.find(m);
        if (iit != kI.end()) {
            if (!need(3)) return false;
            uint32_t rd, rs1;
            if (!reg(line.operands[0], rd) || !reg(line.operands[1], rs1)) return false;
            int64_t imm = 0;
            if (!eval(line.operands[2], imm)) return false;
            emit32(encode_i(Opcode::OpImm, rd, iit->second.funct3, rs1,
                            static_cast<int32_t>(imm)));
            continue;
        }
        if (m == "slli" || m == "srli" || m == "srai") {
            if (!need(3)) return false;
            uint32_t rd, rs1;
            if (!reg(line.operands[0], rd) || !reg(line.operands[1], rs1)) return false;
            int64_t shamt = 0;
            if (!eval(line.operands[2], shamt)) return false;
            const uint32_t imm = static_cast<uint32_t>(shamt & 0x1F);
            if (m == "slli") {
                emit32(encode_i(Opcode::OpImm, rd, 1, rs1, static_cast<int32_t>(imm)));
            } else {
                const uint32_t funct7 = (m == "srai") ? 0x20 : 0;
                emit32(encode_i(Opcode::OpImm, rd, 5, rs1,
                                static_cast<int32_t>((funct7 << 5) | imm)));
            }
            continue;
        }

        // Loads.
        struct LoadInfo { uint32_t funct3; };
        static const std::map<std::string, LoadInfo> kLoads = {
            {"lb", {0}}, {"lh", {1}}, {"lw", {2}}, {"lbu", {4}}, {"lhu", {5}},
        };
        auto lit = kLoads.find(m);
        if (lit != kLoads.end()) {
            if (!need(2)) return false;
            uint32_t rd, rs1;
            int64_t imm = 0;
            std::string mem_error;
            if (!reg(line.operands[0], rd)) return false;
            if (!parse_mem(line.operands[1], rs1, imm, mem_error)) {
                return fail(line.line, mem_error);
            }
            emit32(encode_i(Opcode::Load, rd, lit->second.funct3, rs1,
                            static_cast<int32_t>(imm)));
            continue;
        }

        // Stores.
        struct StoreInfo { uint32_t funct3; };
        static const std::map<std::string, StoreInfo> kStores = {
            {"sb", {0}}, {"sh", {1}}, {"sw", {2}},
        };
        auto sit = kStores.find(m);
        if (sit != kStores.end()) {
            if (!need(2)) return false;
            uint32_t rs2, rs1;
            int64_t imm = 0;
            std::string mem_error;
            if (!reg(line.operands[0], rs2)) return false;
            if (!parse_mem(line.operands[1], rs1, imm, mem_error)) {
                return fail(line.line, mem_error);
            }
            emit32(encode_s(Opcode::Store, sit->second.funct3, rs1, rs2,
                            static_cast<int32_t>(imm)));
            continue;
        }

        // Branches.
        struct BranchInfo { uint32_t funct3; };
        static const std::map<std::string, BranchInfo> kBranch = {
            {"beq", {0}}, {"bne", {1}}, {"blt", {4}},
            {"bge", {5}}, {"bltu", {6}}, {"bgeu", {7}},
        };
        auto bit = kBranch.find(m);
        if (bit != kBranch.end()) {
            if (!need(3)) return false;
            uint32_t rs1, rs2;
            if (!reg(line.operands[0], rs1) || !reg(line.operands[1], rs2)) return false;
            int64_t target = 0;
            if (!eval(line.operands[2], target)) return false;
            const int64_t offset = target - static_cast<int64_t>(pc());
            if (offset < -4096 || offset > 4094 || (offset & 1)) {
                return fail(line.line, "branch out of range or misaligned");
            }
            emit32(encode_b(Opcode::Branch, bit->second.funct3, rs1, rs2,
                            static_cast<int32_t>(offset)));
            continue;
        }

        if (m == "lui" || m == "auipc") {
            if (!need(2)) return false;
            uint32_t rd;
            if (!reg(line.operands[0], rd)) return false;
            int64_t imm = 0;
            if (!eval(line.operands[1], imm)) return false;
            emit32(encode_u(m == "lui" ? Opcode::Lui : Opcode::Auipc, rd,
                            static_cast<uint32_t>(imm)));
            continue;
        }
        if (m == "jal") {
            if (!need(2)) return false;
            uint32_t rd;
            if (!reg(line.operands[0], rd)) return false;
            int64_t target = 0;
            if (!eval(line.operands[1], target)) return false;
            const int64_t offset = target - static_cast<int64_t>(pc());
            if (offset < -(1 << 20) || offset > (1 << 20) - 2 || (offset & 1)) {
                return fail(line.line, "jal out of range");
            }
            emit32(encode_j(Opcode::Jal, rd, static_cast<int32_t>(offset)));
            continue;
        }
        if (m == "jalr") {
            if (line.operands.size() == 3) {
                uint32_t rd, rs1;
                if (!reg(line.operands[0], rd) || !reg(line.operands[1], rs1)) return false;
                int64_t imm = 0;
                if (!eval(line.operands[2], imm)) return false;
                emit32(encode_i(Opcode::Jalr, rd, 0, rs1, static_cast<int32_t>(imm)));
                continue;
            }
            if (line.operands.size() == 2) {
                uint32_t rd, rs1;
                int64_t imm = 0;
                std::string mem_error;
                if (!reg(line.operands[0], rd)) return false;
                if (!parse_mem(line.operands[1], rs1, imm, mem_error)) {
                    return fail(line.line, mem_error);
                }
                emit32(encode_i(Opcode::Jalr, rd, 0, rs1, static_cast<int32_t>(imm)));
                continue;
            }
            return fail(line.line, "jalr expects 2 or 3 operands");
        }
        if (m == "ecall" || m == "ebreak" || m == "mret" || m == "wfi") {
            uint32_t imm = 0;
            if (m == "ebreak") imm = 1;
            else if (m == "mret") imm = 0x302;
            else if (m == "wfi") imm = 0x105;
            emit32(encode_i(Opcode::System, zero, 0, zero, static_cast<int32_t>(imm)));
            continue;
        }
        if (m == "fence") {
            emit32(encode_i(Opcode::MiscMem, zero, 0, zero, 0));
            continue;
        }
        if (m.rfind("cache.", 0) == 0) {
            uint32_t op = 0;
            if (m == "cache.flush.d") op = 0;
            else if (m == "cache.inval.d") op = 1;
            else if (m == "cache.flush.inval.d") op = 2;
            else if (m == "cache.inval.i") op = 3;
            else if (m == "cache.flush.all") op = 4;
            else return fail(line.line, "unknown cache op: " + m);
            uint32_t addr = zero;
            if (!line.operands.empty() && !reg(line.operands[0], addr)) return false;
            emit32(encode_r(Opcode::MiscMem, zero, 1, op, addr, 0));
            continue;
        }
        if (m == "tile.mma" || m == "tile.vec" || m == "tile.dma") {
            if (!need(1)) return false;
            uint32_t rs1;
            if (!reg(line.operands[0], rs1)) return false;
            const uint32_t funct3 = (m == "tile.mma") ? 0 : (m == "tile.vec") ? 1 : 2;
            emit32(encode_tile(funct3, zero, rs1, zero));
            continue;
        }
        if (m == "tile.wait") {
            if (line.operands.size() != 3 && line.operands.size() != 2) {
                return fail(line.line, "tile.wait expects rd, mask[, timeout]");
            }
            uint32_t rd, mask, timeout = zero;
            if (!reg(line.operands[0], rd) || !reg(line.operands[1], mask)) return false;
            if (line.operands.size() == 3) {
                if (!reg_from_name(line.operands[2])) {
                    int64_t value = 0;
                    if (!eval(line.operands[2], value) || value != 0) {
                        return fail(line.line, "tile.wait timeout must be a register or 0");
                    }
                    timeout = zero;
                } else if (!reg(line.operands[2], timeout)) {
                    return false;
                }
            }
            emit32(encode_tile(3, rd, mask, timeout));
            continue;
        }
        if (m == "tile.fence") {
            emit32(encode_tile(4, zero, zero, zero));
            continue;
        }
        if (m == "csrrw" || m == "csrrs" || m == "csrrc" || m == "csrrwi" ||
            m == "csrrsi" || m == "csrrci") {
            if (!need(3)) return false;
            uint32_t rd;
            if (!reg(line.operands[0], rd)) return false;
            const uint32_t csr = csr_number(line.operands[1]);
            if (csr == 0xFFFFFFFFu) {
                return fail(line.line, "bad CSR: " + line.operands[1]);
            }
            const uint32_t funct3 = (m == "csrrw") ? 1 : (m == "csrrs") ? 2
                                   : (m == "csrrc") ? 3 : (m == "csrrwi") ? 5
                                   : (m == "csrrsi") ? 6 : 7;
            uint32_t source = 0;
            if (funct3 >= 5) {
                int64_t imm = 0;
                if (!eval(line.operands[2], imm)) return false;
                source = static_cast<uint32_t>(imm) & 0x1F;
            } else {
                if (!reg(line.operands[2], source)) return false;
            }
            const uint32_t raw = ((csr & 0xFFFu) << 20) |
                                 ((source & 0x1Fu) << 15) |
                                 ((funct3 & 0x7u) << 12) |
                                 ((rd & 0x1Fu) << 7) |
                                 static_cast<uint32_t>(Opcode::System);
            emit32(raw);
            continue;
        }

        return fail(line.line, "unknown instruction: " + line.mnemonic);
    }

    // ---------------- build image ----------------
    result.image.assign(result.rodata_off, 0);
    std::copy(text.bytes.begin(), text.bytes.end(), result.image.begin());
    result.image.resize(result.rodata_off + result.rodata_size, 0);
    std::copy(rodata.bytes.begin(), rodata.bytes.end(),
              result.image.begin() + result.rodata_off);
    result.image.resize(result.data_off + result.data_size, 0);
    std::copy(data.bytes.begin(), data.bytes.end(),
              result.image.begin() + result.data_off);

    // Listing.
    for (uint32_t pc = text.base; pc < text.base + text.bytes.size(); pc += 4) {
        const uint32_t offset = pc - text.base;
        uint32_t raw = 0;
        for (int i = 0; i < 4; ++i) {
            raw |= static_cast<uint32_t>(text.bytes[offset + i]) << (8 * i);
        }
        std::ostringstream os;
        os << "0x" << std::hex << pc << ": " << decode(raw, pc).text;
        result.listing.push_back(os.str());
    }

    return true;
}

bool assemble_file(const std::string &path, const AsmOptions &options,
                   AsmResult &result, std::string &error)
{
    std::ifstream in(path);
    if (!in) {
        error = "cannot open " + path;
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return assemble(buffer.str(), path, options, result, error);
}

}  // namespace clover
