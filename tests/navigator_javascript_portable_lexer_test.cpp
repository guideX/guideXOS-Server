#include "navigator_javascript/frontend_storage.h"
#include "navigator_javascript/ast.h"
#include "navigator_javascript/lexer.h"

using gxos::javascript::Ast;
using gxos::javascript::AstNode;
using gxos::javascript::AstNodeId;
using gxos::javascript::AstNodeKind;
using gxos::javascript::AstStorageView;
using gxos::javascript::FrontendArena;
using gxos::javascript::FrontendArenaMark;
using gxos::javascript::FrontendStorageError;
using gxos::javascript::Lexer;
using gxos::javascript::LexerErrorCode;
using gxos::javascript::LexerLimits;
using gxos::javascript::SourceView;
using gxos::javascript::Token;
using gxos::javascript::TokenBuffer;
using gxos::javascript::TokenType;

namespace {

char maximumSource[gxos::javascript::kDefaultMaxJavaScriptSourceBytes] = {};
char oversizedSource[gxos::javascript::kDefaultMaxJavaScriptSourceBytes + 1u] = {};
char maximumTokenSource[gxos::javascript::kDefaultMaxJavaScriptTokens * 2u] = {};
Token maximumSourceTokens[2] = {};
Token maximumTokenStorage[gxos::javascript::kDefaultMaxJavaScriptTokens] = {};
AstNode maximumAstNodes[gxos::javascript::kDefaultMaxJavaScriptAstNodes] = {};

bool isAligned(const void* pointer, std::size_t alignment)
{
    return (reinterpret_cast<std::uintptr_t>(pointer) & (alignment - 1u)) == 0;
}

bool testPortableTokenStream()
{
    const char source[] =
        "let x = (a, ...rest) => a + 1; // line\r\nconst s = \"ok\";";
    Token storage[24] = {};
    TokenBuffer output{storage, 24, 7};
    const Lexer lexer;
    const auto result = lexer.tokenizeInto(
        SourceView(source, sizeof(source) - 1u), output);
    if (!result.succeeded() || output.count != 20u) return false;

    const TokenType expected[] = {
        TokenType::KeywordLet, TokenType::Identifier, TokenType::Assign,
        TokenType::LeftParen, TokenType::Identifier, TokenType::Comma,
        TokenType::Ellipsis, TokenType::Identifier, TokenType::RightParen,
        TokenType::Arrow, TokenType::Identifier, TokenType::Plus,
        TokenType::NumericLiteral, TokenType::Semicolon, TokenType::KeywordConst,
        TokenType::Identifier, TokenType::Assign, TokenType::StringLiteral,
        TokenType::Semicolon, TokenType::EndOfInput,
    };
    for (std::size_t index = 0; index < output.count; ++index) {
        if (storage[index].type != expected[index]) return false;
    }

    return storage[0].location.offset == 0u &&
        storage[0].location.length == 3u &&
        storage[6].location.offset == 12u &&
        storage[6].location.length == 3u &&
        storage[9].location.offset == 21u &&
        storage[9].location.length == 2u &&
        storage[14].location.line == 2u &&
        storage[14].location.column == 1u &&
        storage[17].location.offset == 50u &&
        storage[17].location.length == 4u &&
        storage[19].location.offset == 55u &&
        storage[19].location.line == 2u &&
        storage[19].location.column == 16u;
}

bool testCapacityErrorsAndReset()
{
    Token storage[2] = {};
    TokenBuffer output{storage, 2, 99};
    const Lexer lexer;
    const char source[] = "a b";
    const auto capacity = lexer.tokenizeInto(
        SourceView(source, sizeof(source) - 1u), output);
    if (capacity.error.code != LexerErrorCode::TokenCapacityExceeded ||
        output.count != 0u) return false;

    LexerLimits limits;
    limits.maxTokenCount = 2;
    const Lexer twoTokenLimit(limits);
    const char oneIdentifier[] = "a";
    const auto success = twoTokenLimit.tokenizeInto(
        SourceView(oneIdentifier, 1u), output);
    if (!success.succeeded() || output.count != 2u ||
        storage[1].type != TokenType::EndOfInput) return false;

    const auto exhausted = twoTokenLimit.tokenizeInto(
        SourceView(source, sizeof(source) - 1u), output);
    return exhausted.error.code == LexerErrorCode::TooManyTokens &&
        output.count == 0u;
}

bool testInputAndStorageErrors()
{
    Token storage[4] = {};
    TokenBuffer output{storage, 4, 3};
    const Lexer lexer;
    const auto empty = lexer.tokenizeInto(SourceView(), output);
    if (!empty.succeeded() || output.count != 1u ||
        storage[0].type != TokenType::EndOfInput) return false;

    const auto invalidSource = lexer.tokenizeInto(SourceView(nullptr, 1u), output);
    if (invalidSource.error.code != LexerErrorCode::InvalidSource ||
        output.count != 0u) return false;

    TokenBuffer invalidStorage{nullptr, 4, 1};
    const char input[] = "x";
    const auto invalidOutput = lexer.tokenizeInto(
        SourceView(input, 1u), invalidStorage);
    if (invalidOutput.error.code != LexerErrorCode::InvalidTokenStorage ||
        invalidStorage.count != 0u) return false;

    LexerLimits limits;
    limits.maxSourceBytes = 0;
    const Lexer zeroSourceLimit(limits);
    const auto sourceLimit = zeroSourceLimit.tokenizeInto(
        SourceView(input, 1u), output);
    return sourceLimit.error.code == LexerErrorCode::SourceTooLarge &&
        output.count == 0u;
}

bool testMaximumSourceAndTokenCapacities()
{
    const std::size_t sourceCapacity =
        gxos::javascript::kDefaultMaxJavaScriptSourceBytes;
    for (std::size_t index = 0; index < sourceCapacity; ++index)
        maximumSource[index] = 'a';
    oversizedSource[0] = 'a';

    LexerLimits sourceLimits;
    sourceLimits.maxTokenBytes = sourceCapacity;
    TokenBuffer sourceOutput{maximumSourceTokens, 2u, 0u};
    const Lexer sourceLexer(sourceLimits);
    const auto maximumSourceResult = sourceLexer.tokenizeInto(
        SourceView(maximumSource, sourceCapacity), sourceOutput);
    if (!maximumSourceResult.succeeded() || sourceOutput.count != 2u ||
        maximumSourceTokens[0].location.length != sourceCapacity ||
        maximumSourceTokens[1].type != TokenType::EndOfInput) return false;

    const Lexer defaultLexer;
    const auto oversizedResult = defaultLexer.tokenizeInto(
        SourceView(oversizedSource, sourceCapacity + 1u), sourceOutput);
    if (oversizedResult.error.code != LexerErrorCode::SourceTooLarge ||
        sourceOutput.count != 0u) return false;

    const std::size_t exactIdentifierCount =
        gxos::javascript::kDefaultMaxJavaScriptTokens - 1u;
    std::size_t sourceLength = 0;
    for (std::size_t index = 0; index < exactIdentifierCount; ++index) {
        maximumTokenSource[sourceLength++] = 'a';
        if (index + 1u != exactIdentifierCount)
            maximumTokenSource[sourceLength++] = ' ';
    }
    TokenBuffer tokenOutput{maximumTokenStorage,
        gxos::javascript::kDefaultMaxJavaScriptTokens, 0u};
    const auto maximumTokens = defaultLexer.tokenizeInto(
        SourceView(maximumTokenSource, sourceLength), tokenOutput);
    if (!maximumTokens.succeeded() ||
        tokenOutput.count != gxos::javascript::kDefaultMaxJavaScriptTokens)
        return false;

    maximumTokenSource[sourceLength++] = ' ';
    maximumTokenSource[sourceLength++] = 'a';
    const auto tooManyTokens = defaultLexer.tokenizeInto(
        SourceView(maximumTokenSource, sourceLength), tokenOutput);
    return tooManyTokens.error.code == LexerErrorCode::TooManyTokens &&
        tokenOutput.count == 0u;
}

bool testArenaBoundsAndAlignment()
{
    alignas(64) unsigned char storage[512] = {};
    FrontendArena arena(storage + 1u, sizeof(storage) - 1u);
    if (!arena.valid()) return false;

    void* result = reinterpret_cast<void*>(1u);
    FrontendStorageError error = FrontendStorageError::None;
    if (!arena.allocate(0u, 64u, result, error) || result != nullptr ||
        arena.used() != 0u) return false;

    if (!arena.allocate(17u, 16u, result, error) ||
        !isAligned(result, 16u) || arena.used() < 17u) return false;
    const FrontendArenaMark checkpoint = arena.mark();
    const std::size_t checkpointOffset = arena.used();

    if (!arena.allocate(32u, 64u, result, error) ||
        !isAligned(result, 64u)) return false;
    const std::size_t beforeInvalidRestore = arena.used();
    FrontendArenaMark invalid = checkpoint;
    invalid.offset = beforeInvalidRestore + 1u;
    if (arena.restore(invalid, error) ||
        error != FrontendStorageError::InvalidMark ||
        arena.used() != beforeInvalidRestore) return false;

    if (!arena.restore(checkpoint, error) ||
        arena.used() != checkpointOffset) return false;

    const std::size_t beforeFailure = arena.used();
    if (arena.allocate(1000u, 16u, result, error) || result != nullptr ||
        error != FrontendStorageError::Exhausted ||
        arena.used() != beforeFailure) return false;
    if (arena.allocate(static_cast<std::size_t>(-1), 8u, result, error) ||
        error != FrontendStorageError::SizeOverflow ||
        arena.used() != beforeFailure) return false;
    if (arena.allocate(1u, 3u, result, error) ||
        error != FrontendStorageError::InvalidAlignment ||
        arena.used() != beforeFailure) return false;

    arena.reset();
    return arena.used() == 0u && arena.highWater() == 0u &&
        !arena.restore(checkpoint, error) &&
        error == FrontendStorageError::InvalidMark;
}

bool testBoundedAstStorageAndAppend()
{
    char source[] = "a+b";
    AstNode sourceNodes[4] = {};
    AstNodeId sourceChildren[4] = {};
    Ast sourceAst(SourceView(source, sizeof(source) - 1u),
        AstStorageView{sourceNodes, 4u, sourceChildren, 4u});

    AstNode left;
    left.kind = AstNodeKind::Identifier;
    left.location = {0u, 1u, 1u, 1u};
    const AstNodeId leftId = sourceAst.addNode(left);
    AstNode right;
    right.kind = AstNodeKind::Identifier;
    right.location = {2u, 1u, 1u, 3u};
    const AstNodeId rightId = sourceAst.addNode(right);
    AstNode program;
    program.kind = AstNodeKind::Program;
    program.location = {0u, 3u, 1u, 1u};
    const AstNodeId programId = sourceAst.addNode(program);
    const AstNodeId children[] = {leftId, rightId};
    if (leftId != 0u || rightId != 1u || programId != 2u ||
        !sourceAst.setChildren(programId, children, 2u) ||
        !sourceAst.setRoot(programId) ||
        sourceAst.childAt(programId, 1u) != rightId ||
        sourceAst.nodeText(leftId).length != 1u ||
        sourceAst.nodeText(leftId).data[0] != 'a') return false;

    char destinationSource[] = "za+b";
    AstNode destinationNodes[8] = {};
    AstNodeId destinationChildren[8] = {};
    Ast destination(SourceView(destinationSource,
        sizeof(destinationSource) - 1u),
        AstStorageView{destinationNodes, 8u, destinationChildren, 8u});
    AstNode prefix;
    prefix.kind = AstNodeKind::EmptyStatement;
    prefix.location = {0u, 1u, 1u, 1u};
    if (destination.addNode(prefix) != 0u) return false;

    AstNodeId appendedRoot = gxos::javascript::kInvalidAstNodeId;
    if (!destination.appendFrom(sourceAst, 1u, 4u, appendedRoot) ||
        appendedRoot != 3u || destination.nodeCount() != 4u ||
        destination.root() != gxos::javascript::kInvalidAstNodeId ||
        destination.childAt(appendedRoot, 0u) != 1u ||
        destination.childAt(appendedRoot, 1u) != 2u ||
        destination.node(1u).location.line != 5u ||
        destination.nodeText(1u).data[0] != 'a') return false;
    if (!destination.setRoot(appendedRoot)) return false;
    const std::size_t appendedCount = destination.nodeCount();
    AstNodeId overflowRoot = 3u;
    if (destination.appendFrom(sourceAst,
            static_cast<std::size_t>(-1), 0u, overflowRoot) ||
        overflowRoot != gxos::javascript::kInvalidAstNodeId ||
        destination.nodeCount() != appendedCount) return false;

    AstNode limitedNodes[8] = {};
    AstNodeId limitedChildren[1] = {};
    Ast limited(SourceView(destinationSource,
        sizeof(destinationSource) - 1u),
        AstStorageView{limitedNodes, 8u, limitedChildren, 1u});
    if (limited.addNode(prefix) != 0u) return false;
    AstNodeId failedRoot = 17u;
    if (limited.appendFrom(sourceAst, 1u, 0u, failedRoot) ||
        failedRoot != gxos::javascript::kInvalidAstNodeId ||
        limited.nodeCount() != 1u) return false;

    limited.reset();
    return limited.nodeCount() == 0u &&
        limited.root() == gxos::javascript::kInvalidAstNodeId &&
        limited.addNode(prefix) == 0u;
}

bool testMaximumAstNodeCapacity()
{
    Ast ast(SourceView(), AstStorageView{maximumAstNodes,
        gxos::javascript::kDefaultMaxJavaScriptAstNodes, nullptr, 0u});
    for (std::size_t index = 0;
        index < gxos::javascript::kDefaultMaxJavaScriptAstNodes; ++index) {
        AstNode node;
        node.kind = AstNodeKind::EmptyStatement;
        if (ast.addNode(node) != static_cast<AstNodeId>(index)) return false;
    }
    if (ast.nodeCount() != gxos::javascript::kDefaultMaxJavaScriptAstNodes ||
        ast.addNode(AstNode()) != gxos::javascript::kInvalidAstNodeId ||
        ast.nodeCount() != gxos::javascript::kDefaultMaxJavaScriptAstNodes)
        return false;
    ast.reset();
    return ast.nodeCount() == 0u &&
        ast.addNode(AstNode()) == 0u;
}

} // namespace

int main()
{
    if (!testPortableTokenStream()) return 1;
    if (!testCapacityErrorsAndReset()) return 2;
    if (!testInputAndStorageErrors()) return 3;
    if (!testArenaBoundsAndAlignment()) return 4;
    if (!testBoundedAstStorageAndAppend()) return 5;
    if (!testMaximumSourceAndTokenCapacities()) return 6;
    if (!testMaximumAstNodeCapacity()) return 7;
    return 0;
}
