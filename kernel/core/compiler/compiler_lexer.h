//
// Bounded lexer for the bare-metal compiler bootstrap.
//

#pragma once

#include "compiler_diagnostics.h"

namespace kernel {
namespace compiler {

static const uint32_t COMPILER_MAX_TOKENS = 256;

enum class TokenKind : uint8_t {
    EndOfFile,
    Identifier,
    Integer,
    Star,
    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    Semicolon,
    Minus,
    Plus,
};

struct Token {
    TokenKind kind;
    SourceLocation location;
    uint32_t length;
};

const char* token_kind_name(TokenKind kind);

bool lex_source(const char* source,
                uint32_t sourceLength,
                Token* tokens,
                uint32_t tokenCapacity,
                uint32_t* tokenCount,
                Diagnostics& diagnostics);

} // namespace compiler
} // namespace kernel
