#pragma once

#include "source.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gxos {
namespace javascript {

enum class TokenType : std::uint8_t {
    EndOfInput = 0,
    Identifier,
    NumericLiteral,
    StringLiteral,

    KeywordVar,
    KeywordFunction,
    KeywordReturn,
    KeywordIf,
    KeywordElse,
    KeywordWhile,
    KeywordFor,
    KeywordBreak,
    KeywordContinue,
    KeywordTrue,
    KeywordFalse,
    KeywordNull,
    KeywordNew,
    KeywordThis,

    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    LeftBracket,
    RightBracket,
    Dot,
    Comma,
    Semicolon,
    Colon,
    Question,

    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Assign,
    Equal,
    StrictEqual,
    NotEqual,
    StrictNotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Not,
    LogicalAnd,
    LogicalOr,
    Increment,
    Decrement,
    PlusAssign,
    MinusAssign,
    StarAssign,
    SlashAssign,
    PercentAssign,
};

struct SourceLocation {
    std::size_t offset = 0;
    std::size_t length = 0;
    std::size_t line = 1;
    std::size_t column = 1;
};

struct Token {
    TokenType type = TokenType::EndOfInput;
    SourceLocation location;
    SourceView lexeme;
};

enum class LexerErrorCode : std::uint8_t {
    None = 0,
    InvalidSource,
    SourceTooLarge,
    TooManyTokens,
    TokenTooLong,
    UnexpectedCharacter,
    UnsupportedEscape,
    UnterminatedString,
    UnterminatedBlockComment,
    MalformedNumericLiteral,
};

struct LexerError {
    LexerErrorCode code = LexerErrorCode::None;
    SourceLocation location;
    std::uint8_t offendingByte = 0;
    bool hasOffendingByte = false;
};

constexpr std::size_t kDefaultMaxJavaScriptSourceBytes = 1024u * 1024u;
constexpr std::size_t kDefaultMaxJavaScriptTokens = 8192u;
constexpr std::size_t kDefaultMaxJavaScriptTokenBytes = 64u * 1024u;

struct LexerLimits {
    std::size_t maxSourceBytes = kDefaultMaxJavaScriptSourceBytes;
    // The end-of-input token counts against this limit.
    std::size_t maxTokenCount = kDefaultMaxJavaScriptTokens;
    std::size_t maxTokenBytes = kDefaultMaxJavaScriptTokenBytes;
};

struct LexResult {
    std::vector<Token> tokens;
    LexerError error;

    bool succeeded() const { return error.code == LexerErrorCode::None; }
};

class Lexer {
public:
    explicit Lexer(LexerLimits limits = LexerLimits()) : limits_(limits) {}

    LexResult tokenize(SourceView source) const;

private:
    LexerLimits limits_;
};

const char* tokenTypeName(TokenType type);
const char* lexerErrorCodeName(LexerErrorCode code);

} // namespace javascript
} // namespace gxos
