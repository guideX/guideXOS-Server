#pragma once

#include "ast.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gxos {
namespace javascript {

enum class ParserErrorCode : std::uint8_t {
    None = 0,
    LexerFailure,
    UnexpectedToken,
    ExpectedToken,
    InvalidExpression,
    InvalidAssignmentTarget,
    UnexpectedEndOfInput,
    AstNodeLimitExceeded,
    NestingLimitExceeded,
    TooManyStatements,
    TooManyParameters,
    TooManyArguments,
    TooManyObjectProperties,
    TooManyArrayElements,
    AllocationFailure,
};

struct ParserError {
    ParserErrorCode code = ParserErrorCode::None;
    SourceLocation location;
    TokenType actual = TokenType::EndOfInput;
    TokenType expected = TokenType::EndOfInput;
    bool hasExpected = false;
};

constexpr std::size_t kDefaultMaxJavaScriptAstNodes = 16384u;
constexpr std::size_t kDefaultMaxJavaScriptParserDepth = 256u;
constexpr std::size_t kDefaultMaxJavaScriptStatements = 4096u;
constexpr std::size_t kDefaultMaxJavaScriptFunctionParameters = 64u;
constexpr std::size_t kDefaultMaxJavaScriptCallArguments = 64u;
constexpr std::size_t kDefaultMaxJavaScriptBlockNesting = 128u;
constexpr std::size_t kDefaultMaxJavaScriptExpressionNesting = 256u;
constexpr std::size_t kDefaultMaxJavaScriptObjectLiteralProperties = 256u;
constexpr std::size_t kDefaultMaxJavaScriptArrayLiteralElements = 1024u;

struct ParserLimits {
    std::size_t maxAstNodes = kDefaultMaxJavaScriptAstNodes;
    std::size_t maxParserDepth = kDefaultMaxJavaScriptParserDepth;
    std::size_t maxStatements = kDefaultMaxJavaScriptStatements;
    std::size_t maxFunctionParameters = kDefaultMaxJavaScriptFunctionParameters;
    std::size_t maxCallArguments = kDefaultMaxJavaScriptCallArguments;
    std::size_t maxBlockNesting = kDefaultMaxJavaScriptBlockNesting;
    std::size_t maxExpressionNesting = kDefaultMaxJavaScriptExpressionNesting;
    std::size_t maxObjectLiteralProperties =
        kDefaultMaxJavaScriptObjectLiteralProperties;
    std::size_t maxArrayLiteralElements =
        kDefaultMaxJavaScriptArrayLiteralElements;
};

struct ParseResult {
    Ast ast;
    ParserError error;

    bool succeeded() const { return error.code == ParserErrorCode::None; }
};

class Parser {
public:
    explicit Parser(ParserLimits limits = ParserLimits()) : limits_(limits) {}

    ParseResult parse(SourceView source, const std::vector<Token>& tokens) const;
    ParseResult parse(SourceView source, const LexResult& lexed) const;

private:
    ParserLimits limits_;
};

const char* parserErrorCodeName(ParserErrorCode code);

} // namespace javascript
} // namespace gxos
