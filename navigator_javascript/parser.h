#pragma once

#include "ast.h"
#include "frontend_storage.h"

#include <cstddef>
#include <cstdint>
#if !defined(GXOS_NAVIGATOR_FRONTEND_FREESTANDING)
#include <vector>
#endif

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
    DuplicateParameter,
    DuplicateBinding,
    InvalidLexicalDeclaration,
    AllocationFailure,
    TokenCapacityExceeded,
    SourceTooLarge,
    InvalidTokenInput,
    AstStorageExhausted,
    AstChildStorageExhausted,
    ScratchStorageLimitExceeded,
    SourceLifetimeInvalidation,
    InternalInvariantFailure,
    ParserStackBudgetExceeded,
};

struct ParserError {
    ParserErrorCode code = ParserErrorCode::None;
    SourceLocation location;
    TokenType actual = TokenType::EndOfInput;
    TokenType expected = TokenType::EndOfInput;
    bool hasExpected = false;
    std::size_t tokenIndex = 0;
};

// A non-owning view of caller-owned tokens. When generation is non-null, the
// parser checks that the originating TokenBuffer has not been reused.
struct TokenView {
    const Token* data = nullptr;
    std::size_t count = 0;
    SourceView source;
    std::uint64_t generation = 0;
    const std::uint64_t* liveGeneration = nullptr;

    bool validGeneration() const
    {
        return liveGeneration == nullptr || *liveGeneration == generation;
    }

    std::size_t size() const { return count; }
    bool empty() const { return count == 0u; }
    const Token& operator[](std::size_t index) const { return data[index]; }
    const Token& back() const { return data[count - 1u]; }
};

inline TokenView makeTokenView(SourceView source, const TokenBuffer& tokens)
{
    return TokenView{tokens.data, tokens.count, source, tokens.generation,
        &tokens.generation};
}

constexpr std::size_t kDefaultMaxJavaScriptParserDepth = 256u;
constexpr std::size_t kDefaultMaxJavaScriptStatements = 4096u;
constexpr std::size_t kDefaultMaxJavaScriptFunctionParameters = 64u;
constexpr std::size_t kDefaultMaxJavaScriptCallArguments = 64u;
constexpr std::size_t kDefaultMaxJavaScriptBlockNesting = 128u;
constexpr std::size_t kDefaultMaxJavaScriptExpressionNesting = 256u;
constexpr std::size_t kDefaultMaxJavaScriptObjectLiteralProperties = 256u;
constexpr std::size_t kDefaultMaxJavaScriptArrayLiteralElements = 1024u;
// Covers the parser's maximum live syntax-list and lexical-validation work
// buffers at the configured 16,384-node limit with alignment headroom.
constexpr std::size_t kDefaultJavaScriptParserScratchBytes =
    512u * 1024u + 64u;
// Bounds recursive parser frames while reserving room for the parseInto entry
// frame and caller on the existing 128 KiB native application stack.
constexpr std::size_t kDefaultJavaScriptParserStackBytes = 96u * 1024u;
constexpr std::size_t kJavaScriptParserStackEntryReserveBytes = 4u * 1024u;

// Caller-side workspace sizing for TokenView input. The 2x node estimate
// bounds AST growth from declarations and expression wrappers; 32 bytes per
// node covers lexical-validation records and syntax-list scratch, plus 64 B
// alignment slack. The configured token and AST caps bound this arithmetic.
constexpr std::size_t parserScratchBytesForTokenCount(std::size_t tokenCount)
{
    const std::size_t boundedTokenCount =
        tokenCount > kDefaultMaxJavaScriptTokens
            ? kDefaultMaxJavaScriptTokens : tokenCount;
    const std::size_t nodeEstimate =
        boundedTokenCount > kDefaultMaxJavaScriptAstNodes / 2u
            ? kDefaultMaxJavaScriptAstNodes : boundedTokenCount * 2u;
    return nodeEstimate * 32u + 64u;
}

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
    std::size_t maxParserStackBytes = kDefaultJavaScriptParserStackBytes;
};

struct ParseResult {
    Ast ast;
    ParserError error;

    bool succeeded() const { return error.code == ParserErrorCode::None; }
};

class Parser {
public:
    explicit Parser(ParserLimits limits = ParserLimits()) : limits_(limits) {}

#if !defined(GXOS_NAVIGATOR_FRONTEND_FREESTANDING)
    ParseResult parse(SourceView source, const std::vector<Token>& tokens) const;
    ParseResult parse(SourceView source, const LexResult& lexed) const;
#endif
    bool parseInto(const TokenView& tokens, Ast& output, FrontendArena& scratch,
        ParserError& error) const;

private:
    ParserLimits limits_;
};

const char* parserErrorCodeName(ParserErrorCode code);

} // namespace javascript
} // namespace gxos
