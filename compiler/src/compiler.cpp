#include "clover/compiler.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace clover {
namespace {

// ============================ tokens ============================

enum class TokKind { End, Ident, Number, String, Char, Punct };

struct Token {
    TokKind kind = TokKind::End;
    std::string text;
    std::string str;
    int64_t value = 0;
    int line = 1;
    int column = 1;
};

class Lexer {
  public:
    Lexer(std::string source, std::string filename)
        : source_(std::move(source)), filename_(std::move(filename))
    {
    }

    std::vector<Token> tokenize(std::string &error)
    {
        std::vector<Token> tokens;
        while (true) {
            skip_space_and_comments();
            if (pos_ >= source_.size()) {
                Token end;
                end.kind = TokKind::End;
                end.line = line_;
                end.column = column_;
                tokens.push_back(end);
                return tokens;
            }
            const char c = source_[pos_];
            if (c == '#' ) {
                // M2 has no full preprocessor; ignore directives rather than
                // failing on a stray #line/#pragma.
                while (pos_ < source_.size() && source_[pos_] != '\n') {
                    advance();
                }
                continue;
            }
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                tokens.push_back(identifier());
            } else if (std::isdigit(static_cast<unsigned char>(c))) {
                tokens.push_back(number());
            } else if (c == '"') {
                tokens.push_back(string_literal());
            } else if (c == '\'') {
                tokens.push_back(char_literal());
            } else {
                tokens.push_back(punct());
            }
            if (!error_.empty()) {
                error = error_;
                return tokens;
            }
        }
    }

  private:
    char peek(size_t offset = 0) const
    {
        return pos_ + offset < source_.size() ? source_[pos_ + offset] : '\0';
    }

    void advance()
    {
        if (pos_ >= source_.size()) {
            return;
        }
        if (source_[pos_] == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        ++pos_;
    }

    void skip_space_and_comments()
    {
        while (pos_ < source_.size()) {
            const char c = source_[pos_];
            if (std::isspace(static_cast<unsigned char>(c))) {
                advance();
            } else if (c == '/' && peek(1) == '/') {
                while (pos_ < source_.size() && source_[pos_] != '\n') {
                    advance();
                }
            } else if (c == '/' && peek(1) == '*') {
                advance();
                advance();
                while (pos_ < source_.size() &&
                       !(source_[pos_] == '*' && peek(1) == '/')) {
                    advance();
                }
                if (pos_ < source_.size()) {
                    advance();
                    advance();
                }
            } else {
                break;
            }
        }
    }

    Token identifier()
    {
        Token token;
        token.kind = TokKind::Ident;
        token.line = line_;
        token.column = column_;
        while (pos_ < source_.size() &&
               (std::isalnum(static_cast<unsigned char>(source_[pos_])) ||
                source_[pos_] == '_')) {
            token.text.push_back(source_[pos_]);
            advance();
        }
        return token;
    }

    Token number()
    {
        Token token;
        token.kind = TokKind::Number;
        token.line = line_;
        token.column = column_;
        std::string text;
        if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
            text.push_back(source_[pos_]);
            advance();
            text.push_back(source_[pos_]);
            advance();
            while (std::isxdigit(static_cast<unsigned char>(peek()))) {
                text.push_back(source_[pos_]);
                advance();
            }
        } else if (peek() == '0' && (peek(1) == 'b' || peek(1) == 'B')) {
            text.push_back(source_[pos_]);
            advance();
            text.push_back(source_[pos_]);
            advance();
            while (peek() == '0' || peek() == '1') {
                text.push_back(source_[pos_]);
                advance();
            }
        } else {
            while (std::isdigit(static_cast<unsigned char>(peek()))) {
                text.push_back(source_[pos_]);
                advance();
            }
        }
        // Integer suffixes.
        while (peek() == 'u' || peek() == 'U' || peek() == 'l' || peek() == 'L') {
            advance();
        }
        token.text = text;
        token.value = std::strtoll(text.c_str(), nullptr, 0);
        return token;
    }

    int escape(char &out)
    {
        advance();  // backslash
        if (pos_ >= source_.size()) {
            return -1;
        }
        const char c = source_[pos_];
        advance();
        switch (c) {
        case 'n': out = '\n'; return 0;
        case 't': out = '\t'; return 0;
        case 'r': out = '\r'; return 0;
        case '0': out = '\0'; return 0;
        case '\\': out = '\\'; return 0;
        case '\'': out = '\''; return 0;
        case '"': out = '"'; return 0;
        case 'x': {
            int value = 0;
            int digits = 0;
            while (std::isxdigit(static_cast<unsigned char>(peek())) && digits < 2) {
                const char h = peek();
                value = value * 16 + (std::isdigit(static_cast<unsigned char>(h))
                                          ? h - '0'
                                          : std::tolower(h) - 'a' + 10);
                advance();
                ++digits;
            }
            out = static_cast<char>(value);
            return digits > 0 ? 0 : -1;
        }
        default:
            out = c;
            return 0;
        }
    }

    Token string_literal()
    {
        Token token;
        token.kind = TokKind::String;
        token.line = line_;
        token.column = column_;
        advance();  // opening quote
        while (pos_ < source_.size() && source_[pos_] != '"') {
            if (source_[pos_] == '\\') {
                char out = 0;
                if (escape(out) != 0) {
                    error_ = "bad escape in string";
                    return token;
                }
                token.str.push_back(out);
            } else {
                token.str.push_back(source_[pos_]);
                advance();
            }
        }
        if (pos_ < source_.size()) {
            advance();
        }
        token.text = token.str;
        return token;
    }

    Token char_literal()
    {
        Token token;
        token.kind = TokKind::Char;
        token.line = line_;
        token.column = column_;
        advance();  // opening quote
        char value = 0;
        if (pos_ < source_.size() && source_[pos_] == '\\') {
            if (escape(value) != 0) {
                error_ = "bad char escape";
                return token;
            }
        } else if (pos_ < source_.size()) {
            value = source_[pos_];
            advance();
        }
        if (pos_ < source_.size() && source_[pos_] == '\'') {
            advance();
        } else {
            error_ = "unterminated char literal";
        }
        token.value = static_cast<unsigned char>(value);
        return token;
    }

    Token punct()
    {
        static const char *kPuncts[] = {
            "<<=", ">>=", "...", "==", "!=", "<=", ">=", "&&", "||", "++",
            "--",  "+=",  "-=",  "*=", "/=", "%=", "&=", "|=", "^=", "<<",
            ">>",  "->",
        };
        Token token;
        token.kind = TokKind::Punct;
        token.line = line_;
        token.column = column_;
        for (const char *p : kPuncts) {
            const size_t length = std::char_traits<char>::length(p);
            if (source_.compare(pos_, length, p) == 0) {
                token.text = p;
                for (size_t i = 0; i < length; ++i) {
                    advance();
                }
                return token;
            }
        }
        token.text = std::string(1, source_[pos_]);
        advance();
        return token;
    }

    std::string source_;
    std::string filename_;
    size_t pos_ = 0;
    int line_ = 1;
    int column_ = 1;
    std::string error_;
};

// ============================ types / AST ============================

struct Type {
    enum Kind { Void, Bool, Char, Short, Int, Long, Ptr, Array } kind = Int;
    bool is_unsigned = false;
    Type *base = nullptr;
    int array_len = 0;
};

Type *type_void()
{
    static Type t;
    t.kind = Type::Void;
    return &t;
}

Type *type_bool()
{
    static Type t;
    t.kind = Type::Bool;
    return &t;
}

Type *type_char(bool unsig = false)
{
    static Type c;
    static Type uc;
    c.kind = Type::Char;
    c.is_unsigned = false;
    uc.kind = Type::Char;
    uc.is_unsigned = true;
    return unsig ? &uc : &c;
}

Type *type_short(bool unsig = false)
{
    static Type s;
    static Type us;
    s.kind = Type::Short;
    s.is_unsigned = false;
    us.kind = Type::Short;
    us.is_unsigned = true;
    return unsig ? &us : &s;
}

Type *type_int(bool unsig = false)
{
    static Type i;
    static Type u;
    i.kind = Type::Int;
    i.is_unsigned = false;
    u.kind = Type::Int;
    u.is_unsigned = true;
    return unsig ? &u : &i;
}

Type *type_long(bool unsig = false)
{
    static Type l;
    static Type ul;
    l.kind = Type::Long;
    l.is_unsigned = false;
    ul.kind = Type::Long;
    ul.is_unsigned = true;
    return unsig ? &ul : &l;
}

Type *type_ptr(Type *base)
{
    Type *t = new Type();
    t->kind = Type::Ptr;
    t->base = base;
    return t;
}

Type *type_array(Type *base, int length)
{
    Type *t = new Type();
    t->kind = Type::Array;
    t->base = base;
    t->array_len = length;
    return t;
}

int size_of(const Type *type)
{
    if (!type) {
        return 4;
    }
    switch (type->kind) {
    case Type::Void: return 0;
    case Type::Bool: return 1;
    case Type::Char: return 1;
    case Type::Short: return 2;
    case Type::Int: return 4;
    case Type::Long: return 4;
    case Type::Ptr: return 4;
    case Type::Array: return size_of(type->base) * type->array_len;
    }
    return 4;
}

int align_of(const Type *type)
{
    if (!type) {
        return 4;
    }
    switch (type->kind) {
    case Type::Void: return 1;
    case Type::Bool: return 1;
    case Type::Char: return 1;
    case Type::Short: return 2;
    case Type::Int: return 4;
    case Type::Long: return 4;
    case Type::Ptr: return 4;
    case Type::Array: return align_of(type->base);
    }
    return 4;
}

bool is_pointer(const Type *type)
{
    return type && (type->kind == Type::Ptr || type->kind == Type::Array);
}

bool is_array(const Type *type)
{
    return type && type->kind == Type::Array;
}

bool is_unsigned_type(const Type *type)
{
    if (!type) {
        return false;
    }
    if (type->kind == Type::Ptr || type->kind == Type::Array) {
        return true;
    }
    return type->is_unsigned;
}

struct Expr {
    enum Kind {
        IntLit,
        StrLit,
        Var,
        Binary,
        Unary,
        Assign,
        Call,
        Index,
        Conditional,
        Comma,
        Sizeof,
        Cast,
        PreInc,
        PostInc,
    } kind = IntLit;
    Type *type = nullptr;
    int64_t value = 0;
    std::string name;
    std::string str;
    std::string op;
    Expr *a = nullptr;
    Expr *b = nullptr;
    Expr *c = nullptr;
    std::vector<Expr *> args;
    Type *cast_type = nullptr;
    int line = 0;
};

struct Decl {
    Type *type = nullptr;
    std::string name;
    Expr *init = nullptr;
    int offset = 0;
    bool is_global = false;
    bool is_static = false;
};

struct Stmt {
    enum Kind {
        Block,
        ExprStmt,
        DeclStmt,
        If,
        While,
        DoWhile,
        For,
        Return,
        Break,
        Continue,
        Empty,
    } kind = Empty;
    std::vector<Stmt *> stmts;
    Expr *expr = nullptr;
    Stmt *body = nullptr;
    Stmt *else_body = nullptr;
    Stmt *init = nullptr;
    Stmt *inc = nullptr;
    Decl *decl = nullptr;
    int line = 0;
};

struct Function {
    Type *ret = nullptr;
    std::string name;
    std::vector<Decl *> params;
    std::vector<Decl *> locals;
    Stmt *body = nullptr;
    bool is_static = false;
    int locals_size = 0;
    int frame_size = 0;
    int line = 0;
};

struct Global {
    Type *type = nullptr;
    std::string name;
    Expr *init = nullptr;
    bool is_static = false;
};

struct Program {
    std::vector<Function *> functions;
    std::vector<Global *> globals;
};

// ============================ parser ============================

class Parser {
  public:
    Parser(std::vector<Token> tokens, std::string filename)
        : tokens_(std::move(tokens)), filename_(std::move(filename))
    {
    }

    Program *parse(std::string &error)
    {
        program_ = new Program();
        while (!at_end()) {
            if (match(";")) {
                continue;
            }
            DeclSpec spec;
            Type *base = parse_decl_spec(spec);
            if (!base) {
                break;
            }
            std::string name;
            Type *type = parse_declarator(base, name);
            if (!error_.empty()) {
                break;
            }
            if (check("(")) {
                Function *fn = new Function();
                fn->ret = type;
                fn->name = name;
                fn->is_static = spec.is_static;
                fn->line = previous_line();
                parse_params(fn);
                if (match(";")) {
                    // Prototype: keep it so calls can resolve the return type.
                } else {
                    fn->body = parse_block();
                }
                program_->functions.push_back(fn);
            } else {
                Global *global = new Global();
                global->type = type;
                global->name = name;
                global->is_static = spec.is_static;
                if (match("=")) {
                    global->init = parse_assignment();
                }
                if (!expect(";")) {
                    break;
                }
                program_->globals.push_back(global);
            }
            if (!error_.empty()) {
                break;
            }
        }
        error = error_;
        return program_;
    }

  private:
    struct DeclSpec {
        bool is_static = false;
    };

    const Token &cur() const { return tokens_[index_]; }
    const Token &peek(size_t offset = 1) const
    {
        return tokens_[std::min(index_ + offset, tokens_.size() - 1)];
    }
    bool at_end() const { return cur().kind == TokKind::End; }
    bool check(const std::string &text) const { return cur().text == text; }
    bool check_kind(TokKind kind) const { return cur().kind == kind; }
    bool match(const std::string &text)
    {
        if (check(text)) {
            ++index_;
            return true;
        }
        return false;
    }
    bool match_kind(TokKind kind)
    {
        if (check_kind(kind)) {
            ++index_;
            return true;
        }
        return false;
    }
    bool expect(const std::string &text)
    {
        if (match(text)) {
            return true;
        }
        fail("expected '" + text + "'");
        return false;
    }
    int previous_line() const
    {
        return index_ == 0 ? 1 : tokens_[index_ - 1].line;
    }
    void fail(const std::string &message)
    {
        if (!error_.empty()) {
            return;
        }
        std::ostringstream os;
        os << filename_ << ":" << cur().line << ":" << cur().column << ": "
           << message << " (at '" << cur().text << "')";
        error_ = os.str();
    }

    static bool is_qualifier(const std::string &text)
    {
        return text == "const" || text == "volatile" || text == "register" ||
               text == "auto" || text == "inline" || text == "extern";
    }

    static bool is_type_word(const std::string &text)
    {
        return text == "void" || text == "char" || text == "short" ||
               text == "int" || text == "long" || text == "signed" ||
               text == "unsigned" || text == "_Bool";
    }

    bool is_type_start(size_t offset = 0) const
    {
        const Token &token = peek(offset);
        return is_type_word(token.text) || is_qualifier(token.text) ||
               token.text == "static";
    }

    Type *parse_decl_spec(DeclSpec &spec)
    {
        while (is_qualifier(cur().text)) {
            ++index_;
        }
        if (match("static")) {
            spec.is_static = true;
        } else if (match("extern")) {
            spec.is_static = false;
        }
        while (is_qualifier(cur().text)) {
            ++index_;
        }
        bool unsig = false;
        bool saw_short = false;
        bool saw_long = false;
        bool saw_char = false;
        bool saw_void = false;
        bool saw_bool = false;
        bool saw_int = false;
        while (is_type_word(cur().text)) {
            const std::string word = cur().text;
            ++index_;
            if (word == "unsigned") {
                unsig = true;
            } else if (word == "signed") {
                unsig = false;
            } else if (word == "short") {
                saw_short = true;
            } else if (word == "long") {
                if (saw_long) {
                    fail("long long is not supported in v0.1");
                    return nullptr;
                }
                saw_long = true;
            } else if (word == "char") {
                saw_char = true;
            } else if (word == "void") {
                saw_void = true;
            } else if (word == "_Bool") {
                saw_bool = true;
            } else if (word == "int") {
                saw_int = true;
            }
        }
        if (saw_void) {
            return type_void();
        }
        if (saw_bool) {
            return type_bool();
        }
        if (saw_char) {
            return type_char(unsig);
        }
        if (saw_short) {
            return type_short(unsig);
        }
        if (saw_long) {
            (void)saw_int;
            return type_long(unsig);
        }
        if (saw_int) {
            return type_int(unsig);
        }
        fail("expected a type specifier");
        return nullptr;
    }

    Type *parse_declarator(Type *base, std::string &name, bool allow_abstract = false)
    {
        while (match("*")) {
            if (is_qualifier(cur().text)) {
                ++index_;
            }
            base = type_ptr(base);
        }
        if (!allow_abstract || check_kind(TokKind::Ident)) {
            if (!check_kind(TokKind::Ident)) {
                fail("expected identifier");
                return base;
            }
            name = cur().text;
            ++index_;
        }
        while (match("[")) {
            if (match("]")) {
               base = type_ptr(base);
                continue;
            }
            int64_t length = 0;
            if (!parse_const_int(length)) {
                return base;
            }
            if (!expect("]")) {
                return base;
            }
            base = type_array(base, static_cast<int>(length));
        }
        return base;
    }

    bool parse_const_int(int64_t &value)
    {
        bool negative = false;
        if (match("-")) {
            negative = true;
        } else {
            match("+");
        }
        if (check_kind(TokKind::Number) || check_kind(TokKind::Char)) {
            value = cur().value;
            ++index_;
            if (negative) {
                value = -value;
            }
            return true;
        }
        fail("expected integer constant");
        return false;
    }

    void parse_params(Function *fn)
    {
        if (!expect("(")) {
            return;
        }
        if (match(")")) {
            return;
        }
        if (check("void") && peek().text == ")") {
            ++index_;
            expect(")");
            return;
        }
        while (true) {
            DeclSpec spec;
            Type *base = parse_decl_spec(spec);
            if (!base) {
                return;
            }
            std::string name;
            Type *type = parse_declarator(base, name);
            if (type->kind == Type::Array) {
                type = type_ptr(type->base);
            }
            Decl *decl = new Decl();
            decl->type = type;
            decl->name = name;
            fn->params.push_back(decl);
            if (!match(",")) {
                break;
            }
        }
        expect(")");
    }

    Stmt *parse_block()
    {
        Stmt *block = new Stmt();
        block->kind = Stmt::Block;
        block->line = cur().line;
        if (!expect("{")) {
            return block;
        }
        while (!at_end() && !check("}")) {
            Stmt *stmt = parse_stmt();
            if (!stmt) {
                break;
            }
            block->stmts.push_back(stmt);
            if (!error_.empty()) {
                break;
            }
        }
        expect("}");
        return block;
    }

    Stmt *parse_stmt()
    {
        if (check("{")) {
            return parse_block();
        }
        if (match(";")) {
            return new Stmt();
        }
        if (match("if")) {
            Stmt *stmt = new Stmt();
            stmt->kind = Stmt::If;
            stmt->line = previous_line();
            expect("(");
            stmt->expr = parse_expression();
            expect(")");
            stmt->body = parse_stmt();
            if (match("else")) {
                stmt->else_body = parse_stmt();
            }
            return stmt;
        }
        if (match("while")) {
            Stmt *stmt = new Stmt();
            stmt->kind = Stmt::While;
            stmt->line = previous_line();
            expect("(");
            stmt->expr = parse_expression();
            expect(")");
            stmt->body = parse_stmt();
            return stmt;
        }
        if (match("do")) {
            Stmt *stmt = new Stmt();
            stmt->kind = Stmt::DoWhile;
            stmt->line = previous_line();
            stmt->body = parse_stmt();
            expect("while");
            expect("(");
            stmt->expr = parse_expression();
            expect(")");
            expect(";");
            return stmt;
        }
        if (match("for")) {
            Stmt *stmt = new Stmt();
            stmt->kind = Stmt::For;
            stmt->line = previous_line();
            expect("(");
            if (match(";")) {
                stmt->init = nullptr;
            } else if (is_type_start()) {
                stmt->init = parse_local_decl(true);
            } else {
                Stmt *init = new Stmt();
                init->kind = Stmt::ExprStmt;
                init->expr = parse_expression();
                expect(";");
                stmt->init = init;
            }
            if (!check(";")) {
                stmt->expr = parse_expression();
            }
            expect(";");
            if (!check(")")) {
                Stmt *inc = new Stmt();
                inc->kind = Stmt::ExprStmt;
                inc->expr = parse_expression();
                stmt->inc = inc;
            }
            expect(")");
            stmt->body = parse_stmt();
            return stmt;
        }
        if (match("return")) {
            Stmt *stmt = new Stmt();
            stmt->kind = Stmt::Return;
            stmt->line = previous_line();
            if (!check(";")) {
                stmt->expr = parse_expression();
            }
            expect(";");
            return stmt;
        }
        if (match("break")) {
            Stmt *stmt = new Stmt();
            stmt->kind = Stmt::Break;
            expect(";");
            return stmt;
        }
        if (match("continue")) {
            Stmt *stmt = new Stmt();
            stmt->kind = Stmt::Continue;
            expect(";");
            return stmt;
        }
        if (is_type_start()) {
            return parse_local_decl(true);
        }
        Stmt *stmt = new Stmt();
        stmt->kind = Stmt::ExprStmt;
        stmt->line = cur().line;
        stmt->expr = parse_expression();
        expect(";");
        return stmt;
    }

    Stmt *parse_local_decl(bool consume_semicolon)
    {
        DeclSpec spec;
        Type *base = parse_decl_spec(spec);
        std::vector<Stmt *> declarations;
        while (true) {
            Stmt *stmt = new Stmt();
            stmt->kind = Stmt::DeclStmt;
            stmt->line = cur().line;
            Decl *decl = new Decl();
            decl->type = base;
            Type *type = parse_declarator(base, decl->name);
            decl->type = type;
            decl->is_static = spec.is_static;
            if (match("=")) {
                decl->init = parse_assignment();
            }
            stmt->decl = decl;
            declarations.push_back(stmt);
            if (!match(",")) {
                break;
            }
            if (!error_.empty()) {
                break;
            }
        }
        if (consume_semicolon) {
            expect(";");
        }
        if (declarations.size() == 1) {
            return declarations[0];
        }
        Stmt *block = new Stmt();
        block->kind = Stmt::Block;
        block->stmts = declarations;
        return block;
    }

    Expr *parse_expression()
    {
        return parse_assignment();
    }

    Expr *parse_assignment()
    {
        Expr *lhs = parse_conditional();
        static const char *kOps[] = {"=", "+=", "-=", "*=", "/=", "%=",
                                     "&=", "|=", "^=", "<<=", ">>="};
        for (const char *op : kOps) {
            if (check(op)) {
                const std::string text = op;
                ++index_;
                Expr *rhs = parse_assignment();
                Expr *expr = new Expr();
                expr->kind = Expr::Assign;
                expr->op = text;
                expr->a = lhs;
                expr->b = rhs;
                expr->line = lhs->line;
                return expr;
            }
        }
        return lhs;
    }

    Expr *parse_conditional()
    {
        Expr *cond = parse_binary(1);
        if (match("?")) {
            Expr *expr = new Expr();
            expr->kind = Expr::Conditional;
            expr->a = cond;
            expr->b = parse_assignment();
            expect(":");
            expr->c = parse_conditional();
            return expr;
        }
        return cond;
    }

    static int precedence(const std::string &op)
    {
        if (op == "||") return 1;
        if (op == "&&") return 2;
        if (op == "|") return 3;
        if (op == "^") return 4;
        if (op == "&") return 5;
        if (op == "==" || op == "!=") return 6;
        if (op == "<" || op == "<=" || op == ">" || op == ">=") return 7;
        if (op == "<<" || op == ">>") return 8;
        if (op == "+" || op == "-") return 9;
        if (op == "*" || op == "/" || op == "%") return 10;
        return 0;
    }

    Expr *parse_binary(int min_precedence)
    {
        Expr *lhs = parse_unary();
        while (true) {
            const int prec = precedence(cur().text);
            if (prec == 0 || prec < min_precedence) {
                break;
            }
            const std::string op = cur().text;
            ++index_;
            Expr *rhs = parse_binary(prec + 1);
            Expr *expr = new Expr();
            expr->kind = Expr::Binary;
            expr->op = op;
            expr->a = lhs;
            expr->b = rhs;
            expr->line = lhs->line;
            lhs = expr;
        }
        return lhs;
    }

    Expr *parse_unary()
    {
        if (check("+") || check("-") || check("!") || check("~") || check("&") ||
            check("*")) {
            const std::string op = cur().text;
            const int line = cur().line;
            ++index_;
            Expr *expr = new Expr();
            expr->kind = Expr::Unary;
            expr->op = op;
            expr->a = parse_unary();
            expr->line = line;
            return expr;
        }
        if (check("++") || check("--")) {
            const std::string op = cur().text;
            const int line = cur().line;
            ++index_;
            Expr *expr = new Expr();
            expr->kind = Expr::PreInc;
            expr->op = op;
            expr->a = parse_unary();
            expr->line = line;
            return expr;
        }
        if (match("sizeof")) {
            Expr *expr = new Expr();
            expr->kind = Expr::Sizeof;
            if (check("(") && is_type_start(1)) {
                ++index_;
                expr->cast_type = parse_decl_spec(*(new DeclSpec()));
                std::string ignored;
                expr->cast_type = parse_declarator(expr->cast_type, ignored, true);
                expect(")");
            } else {
                expr->a = parse_unary();
            }
            return expr;
        }
        if (check("(") && is_type_start(1)) {
            ++index_;
            Type *type = parse_decl_spec(*(new DeclSpec()));
            std::string ignored;
            type = parse_declarator(type, ignored, true);
            expect(")");
            Expr *expr = new Expr();
            expr->kind = Expr::Cast;
            expr->cast_type = type;
            expr->a = parse_unary();
            return expr;
        }
        return parse_postfix();
    }

    Expr *parse_postfix()
    {
        Expr *expr = parse_primary();
        while (true) {
            if (match("[")) {
                Expr *index = parse_expression();
                expect("]");
                Expr *result = new Expr();
                result->kind = Expr::Index;
                result->a = expr;
                result->b = index;
                expr = result;
            } else if (match("(")) {
                Expr *call = new Expr();
                call->kind = Expr::Call;
                if (expr->kind != Expr::Var) {
                    fail("callee must be a function name");
                    return expr;
                }
                call->name = expr->name;
                call->line = expr->line;
                if (!match(")")) {
                    while (true) {
                        call->args.push_back(parse_assignment());
                        if (!match(",")) {
                            break;
                        }
                    }
                    expect(")");
                }
                expr = call;
            } else if (match("++") || match("--")) {
                Expr *result = new Expr();
                result->kind = Expr::PostInc;
                result->op = tokens_[index_ - 1].text;
                result->a = expr;
                expr = result;
            } else {
                break;
            }
        }
        return expr;
    }

    Expr *parse_primary()
    {
        if (match_kind(TokKind::Number) || match_kind(TokKind::Char)) {
            Expr *expr = new Expr();
            expr->kind = Expr::IntLit;
            expr->value = tokens_[index_ - 1].value;
            expr->line = tokens_[index_ - 1].line;
            return expr;
        }
        if (match_kind(TokKind::String)) {
            Expr *expr = new Expr();
            expr->kind = Expr::StrLit;
            expr->str = tokens_[index_ - 1].str;
            expr->line = tokens_[index_ - 1].line;
            return expr;
        }
        if (match_kind(TokKind::Ident)) {
            Expr *expr = new Expr();
            expr->kind = Expr::Var;
            expr->name = tokens_[index_ - 1].text;
            expr->line = tokens_[index_ - 1].line;
            return expr;
        }
        if (match("(")) {
            Expr *expr = parse_expression();
            expect(")");
            return expr;
        }
        fail("expected expression");
        Expr *expr = new Expr();
        expr->kind = Expr::IntLit;
        expr->value = 0;
        return expr;
    }

    std::vector<Token> tokens_;
    std::string filename_;
    size_t index_ = 0;
    std::string error_;
    Program *program_ = nullptr;
};

// ============================ analyzer ============================

constexpr int kTempSlots = 64;
constexpr int kOutgoingSlots = 8;

int align_to(int value, int alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

class Analyzer {
  public:
    bool analyze(Program *program, std::string &error)
    {
        program_ = program;
        for (Global *global : program->globals) {
            globals[global->name] = global;
        }
        for (Function *function : program->functions) {
            functions[function->name] = function;
        }
        for (Function *function : program->functions) {
            if (!function->body) {
                continue;
            }
            current_ = function;
            locals.clear();
            next_offset_ = 12;
            for (Decl *param : function->params) {
                add_local(param);
            }
            collect(function->body);
            function->locals_size = next_offset_ - 12;
            function->frame_size =
                align_to(8 + function->locals_size + kTempSlots * 4 +
                             kOutgoingSlots * 4,
                         16);
            infer_stmt(function->body);
        }
        error = error_;
        return error_.empty();
    }

    std::map<std::string, Function *> functions;
    std::map<std::string, Global *> globals;
    std::map<std::string, Decl *> locals;

  private:
    bool is_builtin(const std::string &name) const
    {
        return name == "__clover_exit" || name == "__clover_putchar" ||
               name == "__clover_cycle" || name == "__clover_break" ||
               name == "__clover_mma" || name == "__clover_vec" ||
               name == "__clover_dma" || name == "__clover_wait" ||
               name == "__clover_wait_timeout" || name == "__clover_fence" ||
               name == "__clover_cache";
    }

    void add_local(Decl *decl)
    {
        const int alignment = align_of(decl->type);
        next_offset_ = align_to(next_offset_, alignment);
        decl->offset = next_offset_;
        next_offset_ += size_of(decl->type);
        current_->locals.push_back(decl);
        locals[decl->name] = decl;
    }

    void collect(Stmt *stmt)
    {
        if (!stmt) {
            return;
        }
        switch (stmt->kind) {
        case Stmt::Block:
            for (Stmt *child : stmt->stmts) {
                collect(child);
            }
            break;
        case Stmt::DeclStmt:
            add_local(stmt->decl);
            break;
        case Stmt::If:
            collect(stmt->body);
            collect(stmt->else_body);
            break;
        case Stmt::While:
        case Stmt::DoWhile:
            collect(stmt->body);
            break;
        case Stmt::For:
            collect(stmt->init);
            collect(stmt->body);
            break;
        default:
            break;
        }
    }

    void infer_stmt(Stmt *stmt)
    {
        if (!stmt) {
            return;
        }
        switch (stmt->kind) {
        case Stmt::Block:
            for (Stmt *child : stmt->stmts) {
                infer_stmt(child);
            }
            break;
        case Stmt::ExprStmt:
            if (stmt->expr) {
                infer(stmt->expr);
            }
            break;
        case Stmt::DeclStmt:
            if (stmt->decl->init) {
                infer(stmt->decl->init);
            }
            break;
        case Stmt::If:
            infer(stmt->expr);
            infer_stmt(stmt->body);
            infer_stmt(stmt->else_body);
            break;
        case Stmt::While:
        case Stmt::DoWhile:
            infer(stmt->expr);
            infer_stmt(stmt->body);
            break;
        case Stmt::For:
            infer_stmt(stmt->init);
            if (stmt->expr) {
                infer(stmt->expr);
            }
            infer_stmt(stmt->inc);
            infer_stmt(stmt->body);
            break;
        case Stmt::Return:
            if (stmt->expr) {
                infer(stmt->expr);
            }
            break;
        default:
            break;
        }
    }

    Type *infer(Expr *expr)
    {
        if (!expr) {
            return type_int();
        }
        switch (expr->kind) {
        case Expr::IntLit:
            expr->type = type_int();
            break;
        case Expr::StrLit:
            expr->type = type_ptr(type_char());
            break;
        case Expr::Var: {
            auto lit = locals.find(expr->name);
            if (lit != locals.end()) {
                expr->type = lit->second->type;
                break;
            }
            auto git = globals.find(expr->name);
            if (git != globals.end()) {
                expr->type = git->second->type;
                break;
            }
            error_ = "undefined variable: " + expr->name;
            expr->type = type_int();
            break;
        }
        case Expr::Binary: {
            Type *a = infer(expr->a);
            Type *b = infer(expr->b);
            if ((expr->op == "+" || expr->op == "-") && is_pointer(a)) {
                expr->type = a;
            } else if (expr->op == "+" && is_pointer(b)) {
                expr->type = b;
            } else if (expr->op == "-" && is_pointer(a) && is_pointer(b)) {
                expr->type = type_int();
            } else if (expr->op == "==" || expr->op == "!=" || expr->op == "<" ||
                       expr->op == "<=" || expr->op == ">" || expr->op == ">=" ||
                       expr->op == "&&" || expr->op == "||") {
                expr->type = type_int();
            } else {
                expr->type = (is_unsigned_type(a) || is_unsigned_type(b))
                                 ? type_int(true)
                                 : type_int();
            }
            break;
        }
        case Expr::Unary: {
            Type *a = infer(expr->a);
            if (expr->op == "&") {
                expr->type = type_ptr(a);
            } else if (expr->op == "*") {
                expr->type = a->base ? a->base : type_int();
            } else if (expr->op == "!") {
                expr->type = type_int();
            } else {
                expr->type = a;
            }
            break;
        }
        case Expr::Assign:
            expr->type = infer(expr->a);
            infer(expr->b);
            break;
        case Expr::Call: {
            infer_args(expr);
            if (is_builtin(expr->name)) {
                expr->type = type_int();
                break;
            }
            auto it = functions.find(expr->name);
            if (it == functions.end()) {
                error_ = "undefined function: " + expr->name;
                expr->type = type_int();
            } else {
                expr->type = it->second->ret;
            }
            break;
        }
        case Expr::Index: {
            Type *base = infer(expr->a);
            infer(expr->b);
            if (is_array(base) || is_pointer(base)) {
                expr->type = base->base ? base->base : type_int();
            } else {
                error_ = "subscripted value is not an array or pointer";
                expr->type = type_int();
            }
            break;
        }
        case Expr::Conditional: {
            infer(expr->a);
            Type *b = infer(expr->b);
            Type *c = infer(expr->c);
            expr->type = is_pointer(b) ? b : (is_pointer(c) ? c : type_int());
            break;
        }
        case Expr::Comma:
            infer(expr->a);
            expr->type = infer(expr->b);
            break;
        case Expr::Sizeof:
            if (expr->cast_type) {
                expr->value = size_of(expr->cast_type);
            } else {
                expr->value = size_of(infer(expr->a));
            }
            expr->type = type_int();
            break;
        case Expr::Cast:
            infer(expr->a);
            expr->type = expr->cast_type;
            break;
        case Expr::PreInc:
        case Expr::PostInc:
            expr->type = infer(expr->a);
            break;
        }
        return expr->type;
    }

    void infer_args(Expr *expr)
    {
        for (Expr *arg : expr->args) {
            infer(arg);
        }
    }

    Program *program_ = nullptr;
    Function *current_ = nullptr;
    std::map<std::string, Decl *> locals_;
    int next_offset_ = 12;
    std::string error_;
};

// ============================ codegen ============================

class CodeGen {
  public:
    CodeGen(Program *program, Analyzer *analyzer, CompileOptions options)
        : program_(program), analyzer_(analyzer), options_(options)
    {
    }

    std::string generate(std::string &error)
    {
        emit(".section .text");
        if (options_.emit_start) {
            emit(".globl _start");
            emit("_start:");
            if (analyzer_->functions.find("main") == analyzer_->functions.end()) {
                error_ = "no main function for _start";
            }
            emit("    call main");
            emit("    li a7, 0");
            emit("    ecall");
        }
        for (Function *function : program_->functions) {
            if (function->body) {
                gen_function(function);
            }
        }
        gen_globals();
        gen_strings();
        error = error_;
        return output_;
    }

  private:
    void emit(const std::string &line)
    {
        output_ += line;
        output_ += '\n';
    }

    std::string new_label()
    {
        return ".L" + std::to_string(label_counter_++);
    }

    std::string intern_string(const std::string &value)
    {
        auto it = string_labels_.find(value);
        if (it != string_labels_.end()) {
            return it->second;
        }
        const std::string label = ".Lstr" + std::to_string(strings_.size());
        strings_.push_back({label, value});
        string_labels_[value] = label;
        return label;
    }

    static std::string escape_string(const std::string &value)
    {
        std::string out;
        for (char c : value) {
            switch (c) {
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\0': out += "\\0"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\x%02x",
                                  static_cast<unsigned char>(c));
                    out += buffer;
                } else {
                    out.push_back(c);
                }
                break;
            }
        }
        return out;
    }

    void gen_function(Function *function)
    {
        current_ = function;
        current_locals_.clear();
        for (Decl *local : function->locals) {
            current_locals_[local->name] = local;
        }
        temp_depth_ = 0;
        loop_stack_.clear();
        const std::string &name = function->name;
        if (!function->is_static) {
            emit(".globl " + name);
        }
        emit(name + ":");
        if (function->frame_size <= 2047) {
            emit("    addi sp, sp, -" + std::to_string(function->frame_size));
        } else {
            emit("    li t0, " + std::to_string(function->frame_size));
            emit("    sub sp, sp, t0");
        }
        emit_store("ra", "sp", function->frame_size - 4, type_int());
        emit_store("s0", "sp", function->frame_size - 8, type_int());
        if (function->frame_size <= 2047) {
            emit("    addi s0, sp, " + std::to_string(function->frame_size));
        } else {
            emit("    li t0, " + std::to_string(function->frame_size));
            emit("    add s0, sp, t0");
        }

        for (size_t i = 0; i < function->params.size(); ++i) {
            if (i >= static_cast<size_t>(kOutgoingSlots)) {
                error_ = "too many parameters in " + name;
                break;
            }
            emit_store("a" + std::to_string(i), "s0",
                       -(function->params[i]->offset +
                         size_of(function->params[i]->type)),
                       function->params[i]->type);
        }

        gen_stmt(function->body);
        emit_epilogue(function);
    }

    void emit_epilogue(Function *function)
    {
        (void)function;
        emit("    lw ra, -4(s0)");
        emit("    mv sp, s0");
        emit("    lw s0, -8(sp)");
        emit("    ret");
    }

    void gen_stmt(Stmt *stmt)
    {
        if (!stmt) {
            return;
        }
        switch (stmt->kind) {
        case Stmt::Block:
            for (Stmt *child : stmt->stmts) {
                gen_stmt(child);
            }
            break;
        case Stmt::Empty:
            break;
        case Stmt::ExprStmt:
            if (stmt->expr) {
                gen_expr(stmt->expr);
            }
            break;
        case Stmt::DeclStmt: {
            Decl *decl = stmt->decl;
            if (decl->init) {
                gen_addr_local(-(decl->offset + size_of(decl->type)));
                push_a0();
                gen_expr(decl->init);
                pop("t0");
                emit_store("a0", "t0", 0, decl->type);
            }
            break;
        }
        case Stmt::If: {
            const std::string else_label = new_label();
            const std::string end_label = new_label();
            gen_expr(stmt->expr);
            emit("    beqz a0, " + else_label);
            gen_stmt(stmt->body);
            emit("    j " + end_label);
            emit(else_label + ":");
            gen_stmt(stmt->else_body);
            emit(end_label + ":");
            break;
        }
        case Stmt::While: {
            const std::string start = new_label();
            const std::string end = new_label();
            loop_stack_.push_back({end, start});
            emit(start + ":");
            gen_expr(stmt->expr);
            emit("    beqz a0, " + end);
            gen_stmt(stmt->body);
            emit("    j " + start);
            emit(end + ":");
            loop_stack_.pop_back();
            break;
        }
        case Stmt::DoWhile: {
            const std::string start = new_label();
            const std::string cont = new_label();
            const std::string end = new_label();
            loop_stack_.push_back({end, cont});
            emit(start + ":");
            gen_stmt(stmt->body);
            emit(cont + ":");
            gen_expr(stmt->expr);
            emit("    bnez a0, " + start);
            emit(end + ":");
            loop_stack_.pop_back();
            break;
        }
        case Stmt::For: {
            const std::string start = new_label();
            const std::string cont = new_label();
            const std::string end = new_label();
            loop_stack_.push_back({end, cont});
            gen_stmt(stmt->init);
            emit(start + ":");
            if (stmt->expr) {
                gen_expr(stmt->expr);
                emit("    beqz a0, " + end);
            }
            gen_stmt(stmt->body);
            emit(cont + ":");
            gen_stmt(stmt->inc);
            emit("    j " + start);
            emit(end + ":");
            loop_stack_.pop_back();
            break;
        }
        case Stmt::Return:
            if (stmt->expr) {
                gen_expr(stmt->expr);
            }
            emit_epilogue(current_);
            break;
        case Stmt::Break:
            if (loop_stack_.empty()) {
                error_ = "break outside loop";
            } else {
                emit("    j " + loop_stack_.back().first);
            }
            break;
        case Stmt::Continue:
            if (loop_stack_.empty()) {
                error_ = "continue outside loop";
            } else {
                emit("    j " + loop_stack_.back().second);
            }
            break;
        }
    }

    void gen_expr(Expr *expr)
    {
        if (!expr) {
            emit("    li a0, 0");
            return;
        }
        switch (expr->kind) {
        case Expr::IntLit:
            emit("    li a0, " + std::to_string(expr->value));
            break;
        case Expr::StrLit:
            emit("    la a0, " + intern_string(expr->str));
            break;
        case Expr::Var:
            if (is_array(expr->type)) {
                gen_addr(expr);
            } else {
                gen_addr(expr);
                emit_load("a0", "a0", 0, expr->type);
            }
            break;
        case Expr::Binary:
            gen_binary(expr);
            break;
        case Expr::Unary:
            gen_unary(expr);
            break;
        case Expr::Assign:
            gen_assign(expr);
            break;
        case Expr::Call:
            gen_call(expr);
            break;
        case Expr::Index:
            gen_addr(expr);
            if (!is_array(expr->type)) {
                emit_load("a0", "a0", 0, expr->type);
            }
            break;
        case Expr::Conditional: {
            const std::string else_label = new_label();
            const std::string end_label = new_label();
            gen_expr(expr->a);
            emit("    beqz a0, " + else_label);
            gen_expr(expr->b);
            emit("    j " + end_label);
            emit(else_label + ":");
            gen_expr(expr->c);
            emit(end_label + ":");
            break;
        }
        case Expr::Comma:
            gen_expr(expr->a);
            gen_expr(expr->b);
            break;
        case Expr::Sizeof:
            emit("    li a0, " + std::to_string(expr->value));
            break;
        case Expr::Cast:
            gen_expr(expr->a);
            if (expr->cast_type && expr->cast_type->kind == Type::Char) {
                emit("    andi a0, a0, 0xff");
                if (!expr->cast_type->is_unsigned) {
                    emit("    slli a0, a0, 24");
                    emit("    srai a0, a0, 24");
                }
            } else if (expr->cast_type && expr->cast_type->kind == Type::Short) {
                emit("    andi a0, a0, 0xffff");
                if (!expr->cast_type->is_unsigned) {
                    emit("    slli a0, a0, 16");
                    emit("    srai a0, a0, 16");
                }
            }
            break;
        case Expr::PreInc:
        case Expr::PostInc:
            gen_inc(expr);
            break;
        }
    }

    void gen_addr(Expr *expr)
    {
        if (!expr) {
            emit("    li a0, 0");
            return;
        }
        if (expr->kind == Expr::Var) {
            auto lit = current_locals_.find(expr->name);
            if (lit != current_locals_.end()) {
                gen_addr_local(-(lit->second->offset +
                                 size_of(lit->second->type)));
                return;
            }
            emit("    la a0, " + expr->name);
            return;
        }
        if (expr->kind == Expr::Unary && expr->op == "*") {
            gen_expr(expr->a);
            return;
        }
        if (expr->kind == Expr::Index) {
            gen_expr(expr->a);
            push_a0();
            gen_expr(expr->b);
            const int scale = size_of(expr->type);
            if (scale > 1) {
                emit("    li t0, " + std::to_string(scale));
                emit("    mul a0, a0, t0");
            }
            pop("t0");
            emit("    add a0, t0, a0");
            return;
        }
        if (expr->kind == Expr::StrLit) {
            emit("    la a0, " + intern_string(expr->str));
            return;
        }
        error_ = "expression is not addressable";
        emit("    li a0, 0");
    }

    void gen_addr_local(int offset)
    {
        if (offset >= -2048 && offset <= 2047) {
            emit("    addi a0, s0, " + std::to_string(offset));
        } else {
            emit("    li a0, " + std::to_string(offset));
            emit("    add a0, s0, a0");
        }
    }

    void push_a0()
    {
        if (temp_depth_ >= kTempSlots) {
            error_ = "expression nesting too deep";
            return;
        }
        emit_store("a0", "s0", temp_offset(temp_depth_), type_int());
        ++temp_depth_;
    }

    void push_reg(const std::string &reg)
    {
        if (temp_depth_ >= kTempSlots) {
            error_ = "expression nesting too deep";
            return;
        }
        emit_store(reg, "s0", temp_offset(temp_depth_), type_int());
        ++temp_depth_;
    }

    void pop(const std::string &reg)
    {
        if (temp_depth_ <= 0) {
            error_ = "internal: expression stack underflow";
            return;
        }
        --temp_depth_;
        emit_load(reg, "s0", temp_offset(temp_depth_), type_int());
    }

    int temp_offset(int index) const
    {
        return -(12 + current_->locals_size + 4 * (index + 1));
    }

    int outgoing_offset(int index) const
    {
        return -(12 + current_->locals_size + kTempSlots * 4 + 4 * (index + 1));
    }

    void emit_load(const std::string &rd, const std::string &base, int offset,
                   Type *type)
    {
        const char *op = "lw";
        if (type && type->kind == Type::Char) {
            op = type->is_unsigned ? "lbu" : "lb";
        } else if (type && type->kind == Type::Short) {
            op = type->is_unsigned ? "lhu" : "lh";
        } else if (type && type->kind == Type::Bool) {
            op = "lbu";
        }
        if (offset >= -2048 && offset <= 2047) {
            emit("    " + std::string(op) + " " + rd + ", " +
                 std::to_string(offset) + "(" + base + ")");
        } else {
            emit("    li t6, " + std::to_string(offset));
            emit("    add t6, " + base + ", t6");
            emit("    " + std::string(op) + " " + rd + ", 0(t6)");
        }
    }

    void emit_store(const std::string &rs, const std::string &base, int offset,
                    Type *type)
    {
        const char *op = "sw";
        if (type && type->kind == Type::Char) {
            op = "sb";
        } else if (type && type->kind == Type::Short) {
            op = "sh";
        } else if (type && type->kind == Type::Bool) {
            op = "sb";
        }
        if (offset >= -2048 && offset <= 2047) {
            emit("    " + std::string(op) + " " + rs + ", " +
                 std::to_string(offset) + "(" + base + ")");
        } else {
            emit("    li t6, " + std::to_string(offset));
            emit("    add t6, " + base + ", t6");
            emit("    " + std::string(op) + " " + rs + ", 0(t6)");
        }
    }

    void gen_binary(Expr *expr)
    {
        if (expr->op == "&&" || expr->op == "||") {
            const std::string short_label = new_label();
            const std::string end_label = new_label();
            gen_expr(expr->a);
            if (expr->op == "&&") {
                emit("    beqz a0, " + short_label);
                gen_expr(expr->b);
                emit("    beqz a0, " + short_label);
                emit("    li a0, 1");
                emit("    j " + end_label);
                emit(short_label + ":");
                emit("    li a0, 0");
            } else {
                emit("    bnez a0, " + short_label);
                gen_expr(expr->b);
                emit("    bnez a0, " + short_label);
                emit("    li a0, 0");
                emit("    j " + end_label);
                emit(short_label + ":");
                emit("    li a0, 1");
            }
            emit(end_label + ":");
            return;
        }

        const bool pointer_lhs = is_pointer(expr->a->type);
        const bool pointer_rhs = is_pointer(expr->b->type);
        gen_expr(expr->a);
        push_a0();
        gen_expr(expr->b);
        pop("t0");

        if ((expr->op == "+" || expr->op == "-") && pointer_lhs && !pointer_rhs) {
            const int scale = size_of(expr->a->type->base);
            if (scale > 1) {
                emit("    li t1, " + std::to_string(scale));
                emit("    mul a0, a0, t1");
            }
            emit(std::string("    ") + (expr->op == "+" ? "add" : "sub") +
                 " a0, t0, a0");
            return;
        }
        if (expr->op == "+" && pointer_rhs && !pointer_lhs) {
            const int scale = size_of(expr->b->type->base);
            if (scale > 1) {
                emit("    li t1, " + std::to_string(scale));
                emit("    mul t0, t0, t1");
            }
            emit("    add a0, t0, a0");
            return;
        }
        if (expr->op == "-" && pointer_lhs && pointer_rhs) {
            const int scale = size_of(expr->a->type->base);
            emit("    sub a0, t0, a0");
            if (scale > 1) {
                emit("    li t1, " + std::to_string(scale));
                emit("    div a0, a0, t1");
            }
            return;
        }

        const bool unsig = is_unsigned_type(expr->a->type) ||
                           is_unsigned_type(expr->b->type);
        if (expr->op == "+") {
            emit("    add a0, t0, a0");
        } else if (expr->op == "-") {
            emit("    sub a0, t0, a0");
        } else if (expr->op == "*") {
            emit("    mul a0, t0, a0");
        } else if (expr->op == "/") {
            emit(std::string("    ") + (unsig ? "divu" : "div") + " a0, t0, a0");
        } else if (expr->op == "%") {
            emit(std::string("    ") + (unsig ? "remu" : "rem") + " a0, t0, a0");
        } else if (expr->op == "&") {
            emit("    and a0, t0, a0");
        } else if (expr->op == "|") {
            emit("    or a0, t0, a0");
        } else if (expr->op == "^") {
            emit("    xor a0, t0, a0");
        } else if (expr->op == "<<") {
            emit("    sll a0, t0, a0");
        } else if (expr->op == ">>") {
            emit(std::string("    ") + (unsig ? "srl" : "sra") + " a0, t0, a0");
        } else if (expr->op == "==") {
            emit("    sub a0, t0, a0");
            emit("    seqz a0, a0");
        } else if (expr->op == "!=") {
            emit("    sub a0, t0, a0");
            emit("    snez a0, a0");
        } else if (expr->op == "<") {
            emit(std::string("    ") + (unsig ? "sltu" : "slt") + " a0, t0, a0");
        } else if (expr->op == ">") {
            emit(std::string("    ") + (unsig ? "sltu" : "slt") + " a0, a0, t0");
        } else if (expr->op == "<=") {
            emit(std::string("    ") + (unsig ? "sltu" : "slt") + " a0, a0, t0");
            emit("    xori a0, a0, 1");
        } else if (expr->op == ">=") {
            emit(std::string("    ") + (unsig ? "sltu" : "slt") + " a0, t0, a0");
            emit("    xori a0, a0, 1");
        } else {
            error_ = "unsupported binary operator: " + expr->op;
        }
    }

    void gen_unary(Expr *expr)
    {
        if (expr->op == "&") {
            gen_addr(expr->a);
            return;
        }
        if (expr->op == "*") {
            gen_expr(expr->a);
            emit_load("a0", "a0", 0, expr->type);
            return;
        }
        gen_expr(expr->a);
        if (expr->op == "-") {
            emit("    sub a0, zero, a0");
        } else if (expr->op == "!") {
            emit("    seqz a0, a0");
        } else if (expr->op == "~") {
            emit("    xori a0, a0, -1");
        } else if (expr->op != "+") {
            error_ = "unsupported unary operator: " + expr->op;
        }
    }

    void gen_assign(Expr *expr)
    {
        if (expr->op == "=") {
            gen_addr(expr->a);
            push_a0();
            gen_expr(expr->b);
            pop("t0");
            emit_store("a0", "t0", 0, expr->a->type);
            return;
        }
        gen_addr(expr->a);
        push_a0();
        emit_load("t0", "s0", temp_offset(temp_depth_ - 1), type_int());
        emit_load("t1", "t0", 0, expr->a->type);
        push_reg("t1");
        gen_expr(expr->b);
        pop("t1");
        if ((expr->op == "+=" || expr->op == "-=") &&
            is_pointer(expr->a->type) && expr->a->type->base) {
            const int scale = size_of(expr->a->type->base);
            if (scale > 1) {
                emit("    li t2, " + std::to_string(scale));
                emit("    mul a0, a0, t2");
            }
        }
        emit("    mv t0, t1");
        const std::string op = expr->op.substr(0, expr->op.size() - 1);
        emit_compound_op(op, expr->a->type);
        pop("t0");
        emit_store("a0", "t0", 0, expr->a->type);
    }

    void emit_compound_op(const std::string &op, Type *type)
    {
        (void)type;
        if (op == "+") emit("    add a0, t0, a0");
        else if (op == "-") emit("    sub a0, t0, a0");
        else if (op == "*") emit("    mul a0, t0, a0");
        else if (op == "/") emit("    div a0, t0, a0");
        else if (op == "%") emit("    rem a0, t0, a0");
        else if (op == "&") emit("    and a0, t0, a0");
        else if (op == "|") emit("    or a0, t0, a0");
        else if (op == "^") emit("    xor a0, t0, a0");
        else if (op == "<<") emit("    sll a0, t0, a0");
        else if (op == ">>") emit("    sra a0, t0, a0");
        else error_ = "unsupported compound assignment: " + op;
    }

    void gen_inc(Expr *expr)
    {
        gen_addr(expr->a);
        push_a0();
        emit_load("t0", "s0", temp_offset(temp_depth_ - 1), type_int());
        emit_load("t1", "t0", 0, expr->a->type);
        emit("    mv t2, t1");
        int step = 1;
        if (is_pointer(expr->a->type) && expr->a->type->base) {
            step = size_of(expr->a->type->base);
        }
        emit("    addi t1, t1, " + std::to_string(expr->op == "++" ? step : -step));
        emit_store("t1", "t0", 0, expr->a->type);
        pop("t0");
        if (expr->kind == Expr::PreInc) {
            emit("    mv a0, t1");
        } else {
            emit("    mv a0, t2");
        }
    }

    void gen_call(Expr *expr)
    {
        if (expr->name == "__clover_mma" || expr->name == "__clover_vec" ||
            expr->name == "__clover_dma") {
            if (expr->args.empty()) {
                error_ = expr->name + " expects a descriptor pointer";
                return;
            }
            gen_expr(expr->args[0]);
            emit("    tile." + (expr->name == "__clover_mma" ? std::string("mma")
                               : expr->name == "__clover_vec" ? std::string("vec")
                                                              : std::string("dma")) + " a0");
            return;
        }
        if (expr->name == "__clover_wait") {
            if (expr->args.empty()) {
                emit("    li a0, 0");
            } else {
                gen_expr(expr->args[0]);
            }
            emit("    tile.wait a0, a0, zero");
            return;
        }
        if (expr->name == "__clover_wait_timeout") {
            if (expr->args.size() < 2) {
                error_ = "__clover_wait_timeout expects mask, timeout";
                return;
            }
            gen_expr(expr->args[0]);
            push_a0();
            gen_expr(expr->args[1]);
            emit("    mv a1, a0");
            pop("a0");
            emit("    tile.wait a0, a0, a1");
            return;
        }
        if (expr->name == "__clover_fence") {
            emit("    tile.fence");
            return;
        }
        if (expr->name == "__clover_cache") {
            if (expr->args.size() < 2) {
                error_ = "__clover_cache expects op, address";
                return;
            }
            gen_expr(expr->args[0]);
            push_a0();
            gen_expr(expr->args[1]);
            // CACHE encoding is op/address in rs1/rs2; keep the raw R-type
            // form for now via the assembler's cache.<op> mnemonics.
            error_ = "__clover_cache is not wired to a mnemonic yet";
            return;
        }
        if (expr->name == "__clover_exit") {
            if (!expr->args.empty()) {
                gen_expr(expr->args[0]);
            } else {
                emit("    li a0, 0");
            }
            emit("    li a7, 0");
            emit("    ecall");
            return;
        }
        if (expr->name == "__clover_putchar") {
            if (expr->args.empty()) {
                emit("    li a0, 0");
            } else {
                gen_expr(expr->args[0]);
            }
            emit("    li a7, 1");
            emit("    ecall");
            return;
        }
        if (expr->name == "__clover_cycle") {
            emit("    li a7, 2");
            emit("    ecall");
            return;
        }
        if (expr->name == "__clover_break") {
            emit("    li a7, 3");
            emit("    ecall");
            return;
        }
        if (expr->args.size() > static_cast<size_t>(kOutgoingSlots)) {
            error_ = "too many call arguments";
            return;
        }
        for (size_t i = 0; i < expr->args.size(); ++i) {
            gen_expr(expr->args[i]);
            emit_store("a0", "s0", outgoing_offset(static_cast<int>(i)),
                       type_int());
        }
        for (size_t i = 0; i < expr->args.size(); ++i) {
            emit_load("a" + std::to_string(i), "s0",
                      outgoing_offset(static_cast<int>(i)), type_int());
        }
        emit("    call " + expr->name);
    }

    void gen_strings()
    {
        if (strings_.empty()) {
            return;
        }
        emit(".section .rodata");
        for (const auto &[label, value] : strings_) {
            emit(label + ":");
            emit("    .asciz \"" + escape_string(value) + "\"");
        }
    }

    bool const_eval(Expr *expr, int64_t &value)
    {
        if (!expr) {
            value = 0;
            return true;
        }
        switch (expr->kind) {
        case Expr::IntLit:
            value = expr->value;
            return true;
        case Expr::Unary: {
            int64_t inner = 0;
            if (!const_eval(expr->a, inner)) return false;
            if (expr->op == "-") { value = -inner; return true; }
            if (expr->op == "+") { value = inner; return true; }
            if (expr->op == "~") { value = ~inner; return true; }
            if (expr->op == "!") { value = inner == 0; return true; }
            return false;
        }
        case Expr::Binary: {
            int64_t a = 0;
            int64_t b = 0;
            if (!const_eval(expr->a, a) || !const_eval(expr->b, b)) return false;
            if (expr->op == "+") { value = a + b; return true; }
            if (expr->op == "-") { value = a - b; return true; }
            if (expr->op == "*") { value = a * b; return true; }
            if (expr->op == "/" && b != 0) { value = a / b; return true; }
            if (expr->op == "%" && b != 0) { value = a % b; return true; }
            if (expr->op == "&") { value = a & b; return true; }
            if (expr->op == "|") { value = a | b; return true; }
            if (expr->op == "^") { value = a ^ b; return true; }
            if (expr->op == "<<") { value = a << b; return true; }
            if (expr->op == ">>") { value = a >> b; return true; }
            return false;
        }
        case Expr::Cast:
            return const_eval(expr->a, value);
        case Expr::Sizeof:
            value = expr->value;
            return true;
        default:
            return false;
        }
    }

    void gen_globals()
    {
        for (Global *global : program_->globals) {
            const int alignment = align_of(global->type);
            int align_power = 0;
            while ((1 << align_power) < alignment) {
                ++align_power;
            }
            if (!global->init) {
                emit(".section .bss");
                if (!global->is_static) emit(".globl " + global->name);
                emit("    .align " + std::to_string(align_power));
                emit(global->name + ":");
                emit("    .space " + std::to_string(size_of(global->type)));
                continue;
            }
            emit(".section .data");
            if (!global->is_static) emit(".globl " + global->name);
            emit("    .align " + std::to_string(align_power));
            emit(global->name + ":");
            if (global->init->kind == Expr::StrLit &&
                is_array(global->type) && global->type->base->kind == Type::Char) {
                emit("    .asciz \"" + escape_string(global->init->str) + "\"");
            } else if (global->init->kind == Expr::StrLit && is_pointer(global->type)) {
                emit("    .word " + intern_string(global->init->str));
            } else {
                int64_t value = 0;
                if (!const_eval(global->init, value)) {
                    error_ = "global initializer is not constant: " + global->name;
                    value = 0;
                }
                if (size_of(global->type) == 1) {
                    emit("    .byte " + std::to_string(value & 0xFF));
                } else if (size_of(global->type) == 2) {
                    emit("    .half " + std::to_string(value & 0xFFFF));
                } else {
                    emit("    .word " + std::to_string(static_cast<uint32_t>(value)));
                }
            }
        }
    }

    Program *program_ = nullptr;
    Analyzer *analyzer_ = nullptr;
    CompileOptions options_;
    Function *current_ = nullptr;
    std::map<std::string, Decl *> current_locals_;
    int temp_depth_ = 0;
    std::string output_;
    std::string error_;
    int label_counter_ = 0;
    std::vector<std::pair<std::string, std::string>> strings_;
    std::map<std::string, std::string> string_labels_;
    std::vector<std::pair<std::string, std::string>> loop_stack_;
};

}  // namespace

CompileResult compile_to_assembly(const std::string &source,
                                  const std::string &filename,
                                  const CompileOptions &options)
{
    CompileResult result;
    Lexer lexer(source, filename);
    std::string error;
    std::vector<Token> tokens = lexer.tokenize(error);
    if (!error.empty()) {
        result.error = filename + ": " + error;
        return result;
    }
    if (options.dump_tokens) {
        for (const Token &token : tokens) {
            if (token.kind == TokKind::End) {
                break;
            }
            result.error += token.text + " ";
        }
        result.error += "\n";
    }

    Parser parser(std::move(tokens), filename);
    Program *program = parser.parse(error);
    if (!error.empty()) {
        result.error = error;
        return result;
    }

    Analyzer analyzer;
    if (!analyzer.analyze(program, error)) {
        result.error = error;
        return result;
    }

    CodeGen codegen(program, &analyzer, options);
    result.assembly = codegen.generate(error);
    if (!error.empty()) {
        result.error = error;
        result.ok = false;
        return result;
    }
    result.ok = true;
    return result;
}

}  // namespace clover
