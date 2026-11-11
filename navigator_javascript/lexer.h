#pragma once

#include "source.h"

#include <cstddef>
#include <cstdint>
#if !defined(GXOS_NAVIGATOR_FRONTEND_FREESTANDING)
#include <vector>
#endif

namespace gxos {
namespace javascript {

enum class TokenType : std::uint8_t {
    EndOfInput = 0,
    Identifier,
    NumericLiteral,
    StringLiteral,

    KeywordVar,
    KeywordLet,
    KeywordConst,
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
    Ellipsis,
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
    Arrow,
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
    TokenCapacityExceeded,
    InvalidTokenStorage,
    AllocationFailure,
};

struct LexerError {
    LexerErrorCode code = LexerErrorCode::None;
    SourceLocation location;
    std::uint8_t offendingByte = 0;
    bool hasOffendingByte = false;
};

// Caller-owned, fixed-capacity token storage for the shared freestanding
// lexer. The lexer resets count before each call and publishes a non-zero
// count only when tokenization succeeds.
struct TokenBuffer {
    Token* data = nullptr;
    std::size_t capacity = 0;
    std::size_t count = 0;
};

struct LexerResult {
    LexerError error;

    bool succeeded() const { return error.code == LexerErrorCode::None; }
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

#if !defined(GXOS_NAVIGATOR_FRONTEND_FREESTANDING)
struct LexResult {
    std::vector<Token> tokens;
    LexerError error;

    bool succeeded() const { return error.code == LexerErrorCode::None; }
};
#endif

class Lexer {
public:
    explicit Lexer(LexerLimits limits = LexerLimits()) : limits_(limits) {}

    LexerResult tokenizeInto(SourceView source, TokenBuffer& output) const;

#if !defined(GXOS_NAVIGATOR_FRONTEND_FREESTANDING)
    // Hosted convenience adapter. It calls the same scanner as tokenizeInto
    // and translates allocation failure into an explicit lexer error.
    LexResult tokenize(SourceView source) const;
#endif

private:
    LexerLimits limits_;
};

const char* tokenTypeName(TokenType type);
const char* lexerErrorCodeName(LexerErrorCode code);

} // namespace javascript
} // namespace gxos
