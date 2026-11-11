#include "navigator_javascript/lexer.h"
#include "navigator_javascript/parser.h"

#include <cstddef>
#include <cstdint>
#if !defined(GXOS_NAVIGATOR_FRONTEND_FREESTANDING)
#include <vector>
#endif

using namespace gxos::javascript;

namespace {

Token tokenStorage[kDefaultMaxJavaScriptTokens];
AstNode astNodes[kDefaultMaxJavaScriptAstNodes];
AstNodeId astChildren[kDefaultMaxJavaScriptAstNodes];
alignas(64) unsigned char scratchBytes[
    kDefaultJavaScriptParserScratchBytes];
AstNode smallAstNodes[8];
AstNodeId smallAstChildren[8];
unsigned char tinyScratch[1];
Token retainedTokensA[64];
Token retainedTokensB[64];
AstNode retainedNodesA[64];
AstNodeId retainedChildrenA[64];
AstNode retainedNodesB[64];
AstNodeId retainedChildrenB[64];
alignas(64) unsigned char retainedScratchA[8192];
alignas(64) unsigned char retainedScratchB[8192];
#if !defined(GXOS_NAVIGATOR_FRONTEND_FREESTANDING)
char maximumSource[kDefaultMaxJavaScriptSourceBytes];
char nestedIfSource[250u * 5u + 2u];
char nestedArraySource[50u * 2u + 3u];
char arrayBoundarySource[kDefaultMaxJavaScriptArrayLiteralElements * 2u + 8u];
#endif

#if !defined(GXOS_NAVIGATOR_FRONTEND_FREESTANDING)
std::size_t textLength(const char* text)
{
    std::size_t length = 0u;
    while (text[length] != '\0') ++length;
    return length;
}

std::size_t makeNestedArraySource(std::size_t levels)
{
    std::size_t length = 0u;
    for (std::size_t index = 0; index < levels; ++index)
        nestedArraySource[length++] = '[';
    nestedArraySource[length++] = '1';
    for (std::size_t index = 0; index < levels; ++index)
        nestedArraySource[length++] = ']';
    nestedArraySource[length++] = ';';
    nestedArraySource[length] = '\0';
    return length;
}

std::size_t makeArrayLiteralSource(std::size_t elements)
{
    std::size_t length = 0u;
    arrayBoundarySource[length++] = '[';
    for (std::size_t index = 0; index < elements; ++index) {
        if (index != 0u) arrayBoundarySource[length++] = ',';
        arrayBoundarySource[length++] = '1';
    }
    arrayBoundarySource[length++] = ']';
    arrayBoundarySource[length++] = ';';
    arrayBoundarySource[length] = '\0';
    return length;
}
#endif

bool equalText(const char* left, const char* right, std::size_t length)
{
    for (std::size_t index = 0; index < length; ++index)
        if (left[index] != right[index]) return false;
    return true;
}

#if !defined(GXOS_NAVIGATOR_FRONTEND_FREESTANDING)
bool sameSourceView(SourceView left, SourceView right)
{
    if (left.length != right.length) return false;
    for (std::size_t index = 0; index < left.length; ++index)
        if (left.data[index] != right.data[index]) return false;
    return true;
}

bool sameNode(const AstNode& left, const AstNode& right)
{
    return left.kind == right.kind &&
        left.location.offset == right.location.offset &&
        left.location.length == right.location.length &&
        left.location.line == right.location.line &&
        left.location.column == right.location.column &&
        left.childOffset == right.childOffset &&
        left.childCount == right.childCount &&
        left.name == right.name && left.initializer == right.initializer &&
        left.expression == right.expression && left.argument == right.argument &&
        left.left == right.left && left.right == right.right &&
        left.test == right.test && left.consequent == right.consequent &&
        left.alternate == right.alternate && left.init == right.init &&
        left.update == right.update && left.body == right.body &&
        left.callee == right.callee && left.object == right.object &&
        left.property == right.property && left.key == right.key &&
        left.unaryOperator == right.unaryOperator &&
        left.binaryOperator == right.binaryOperator &&
        left.logicalOperator == right.logicalOperator &&
        left.assignmentOperator == right.assignmentOperator &&
        left.updateOperator == right.updateOperator &&
        left.declarationKind == right.declarationKind &&
        left.computed == right.computed && left.prefix == right.prefix &&
        left.immutableBinding == right.immutableBinding;
}

bool sameTokens(const LexResult& hosted, const TokenBuffer& portable)
{
    if (hosted.succeeded() != (portable.count != 0u)) return false;
    if (!hosted.succeeded()) return true;
    if (hosted.tokens.size() != portable.count) return false;
    for (std::size_t index = 0; index < portable.count; ++index) {
        const Token& left = hosted.tokens[index];
        const Token& right = portable.data[index];
        if (left.type != right.type ||
            left.location.offset != right.location.offset ||
            left.location.length != right.location.length ||
            left.location.line != right.location.line ||
            left.location.column != right.location.column ||
            left.lexeme.length != right.lexeme.length) return false;
        for (std::size_t byte = 0; byte < left.lexeme.length; ++byte)
            if (left.lexeme.data[byte] != right.lexeme.data[byte]) return false;
    }
    return true;
}

bool sameErrors(const ParserError& left, const ParserError& right)
{
    return left.code == right.code &&
        left.location.offset == right.location.offset &&
        left.location.length == right.location.length &&
        left.location.line == right.location.line &&
        left.location.column == right.location.column &&
        left.actual == right.actual && left.expected == right.expected &&
        left.hasExpected == right.hasExpected &&
        left.tokenIndex == right.tokenIndex;
}

bool sameAsts(const Ast& left, const Ast& right)
{
    if (left.root() != right.root() || left.nodeCount() != right.nodeCount())
        return false;
    for (AstNodeId id = 0; id < left.nodeCount(); ++id) {
        if (!sameNode(left.node(id), right.node(id))) return false;
        const SourceView leftText = left.nodeText(id);
        const SourceView rightText = right.nodeText(id);
        if (!sameSourceView(leftText, rightText)) return false;
        if (left.childCount(id) != right.childCount(id)) return false;
        for (std::size_t index = 0; index < left.childCount(id); ++index)
            if (left.childAt(id, index) != right.childAt(id, index))
                return false;
    }
    return true;
}

bool comparePath(const char* sourceText, bool expectedSuccess)
{
    const SourceView source(sourceText, textLength(sourceText));
    const Lexer lexer;
    const LexResult hostedTokens = lexer.tokenize(source);
    TokenBuffer portableTokens{tokenStorage, kDefaultMaxJavaScriptTokens,
        0u, 0u};
    const LexerResult portableLex = lexer.tokenizeInto(source, portableTokens);
    if (hostedTokens.succeeded() != portableLex.succeeded() ||
        !sameTokens(hostedTokens, portableTokens)) return false;
    if (!hostedTokens.succeeded()) return !expectedSuccess;

    Parser parser;
    ParseResult hosted = parser.parse(source, hostedTokens);
    Ast portable(source, AstStorageView{astNodes,
        kDefaultMaxJavaScriptAstNodes, astChildren,
        kDefaultMaxJavaScriptAstNodes});
    FrontendArena scratch(scratchBytes, sizeof(scratchBytes));
    ParserError portableError;
    const bool portableSucceeded = parser.parseInto(
        makeTokenView(source, portableTokens), portable, scratch,
        portableError);
    if (hosted.succeeded() != portableSucceeded ||
        portableSucceeded != expectedSuccess) return false;
    if (!sameErrors(hosted.error, portableError)) return false;
    if (portableSucceeded) return sameAsts(hosted.ast, portable);
    return portable.root() == kInvalidAstNodeId && portable.nodeCount() == 0u;
}

bool testSharedGrammarCorpus()
{
    const char* const valid[] = {
        "",
        "   \r\n\t",
        "1 + 2 * 3;",
        "var x=1; let y=x+2; const z=y*3;",
        "function f(a=1,...rest) { return a+rest[0]; }",
        "const add=(a,b=2)=>a+b;",
        "var q=(x=>x)(3);",
        "var b=((index===0&&acc===0&&value===10)||(index===1&&acc===10&&value===20));",
        "var nested=[f(1,{x:[2,3]})];",
        "var a=[1,,3]; var o={x:1, 'y':a[0]};",
        "if (x) { y(); } else { z = a.b; }",
        "for (let x of xs) { if (x) continue; }",
        "var n=new Box(a.b[0], call(2));"
    };
    for (const char* source : valid)
        if (!comparePath(source, true)) return false;

    const char* const invalid[] = {
        "var = 1;",
        "foo(;",
        "function f(...rest, tail) {}",
        "var x={a:1;",
        "var x=[1, 2;",
        "if (true) { x();"
    };
    for (const char* source : invalid)
        if (!comparePath(source, false)) return false;
    return true;
}

bool testScratchListBoundaries()
{
    const std::size_t maximumLength = makeArrayLiteralSource(
        kDefaultMaxJavaScriptArrayLiteralElements);
    const SourceView maximumSourceView(arrayBoundarySource, maximumLength);
    const Lexer lexer;
    const Parser parser;
    TokenBuffer tokens{tokenStorage, kDefaultMaxJavaScriptTokens, 0u, 0u};
    if (!lexer.tokenizeInto(maximumSourceView, tokens).succeeded()) return false;

    Ast output(maximumSourceView, AstStorageView{astNodes,
        kDefaultMaxJavaScriptAstNodes, astChildren,
        kDefaultMaxJavaScriptAstNodes});
    FrontendArena scratch(scratchBytes, sizeof(scratchBytes));
    ParserError error;
    if (!parser.parseInto(makeTokenView(maximumSourceView, tokens), output,
            scratch, error) || error.code != ParserErrorCode::None)
        return false;
    AstNodeId array = kInvalidAstNodeId;
    for (AstNodeId id = 0; id < output.nodeCount(); ++id)
        if (output.node(id).kind == AstNodeKind::ArrayLiteral) array = id;
    if (array == kInvalidAstNodeId || output.childCount(array) !=
            kDefaultMaxJavaScriptArrayLiteralElements) return false;

    const std::size_t excessLength = makeArrayLiteralSource(
        kDefaultMaxJavaScriptArrayLiteralElements + 1u);
    const SourceView excessSourceView(arrayBoundarySource, excessLength);
    if (!lexer.tokenizeInto(excessSourceView, tokens).succeeded()) return false;
    if (parser.parseInto(makeTokenView(excessSourceView, tokens), output,
            scratch, error) ||
        error.code != ParserErrorCode::TooManyArrayElements ||
        error.tokenIndex >= tokens.count || scratch.used() != 0u ||
        output.root() != kInvalidAstNodeId || output.nodeCount() != 0u)
        return false;

    const char retrySource[] = "[];";
    const SourceView retryView(retrySource, sizeof(retrySource) - 1u);
    for (std::size_t repeat = 0; repeat < 3u; ++repeat) {
        if (!lexer.tokenizeInto(retryView, tokens).succeeded() ||
            !parser.parseInto(makeTokenView(retryView, tokens), output,
                scratch, error) ||
            error.code != ParserErrorCode::None ||
            output.root() == kInvalidAstNodeId ||
            output.nodeCount() == 0u) return false;
    }
    return true;
}

bool testHandAuthoredExpectedResults()
{
    const Lexer lexer;
    const Parser parser;
    const char precedenceSource[] = "1+2*3;";
    const SourceView precedenceView(precedenceSource,
        sizeof(precedenceSource) - 1u);
    const LexResult precedenceTokens = lexer.tokenize(precedenceView);
    if (!precedenceTokens.succeeded()) return false;
    ParseResult precedence = parser.parse(precedenceView, precedenceTokens);
    if (!precedence.succeeded() || precedence.ast.nodeCount() != 7u ||
        precedence.ast.root() != 6u ||
        precedence.ast.childAt(6u, 0u) != 5u) return false;
    const AstNode& statement = precedence.ast.node(5u);
    if (statement.kind != AstNodeKind::ExpressionStatement ||
        statement.expression != 4u) return false;
    const AstNode& add = precedence.ast.node(4u);
    if (add.kind != AstNodeKind::BinaryExpression ||
        add.binaryOperator != AstBinaryOperator::Add || add.left != 0u ||
        add.right != 3u) return false;
    const AstNode& multiply = precedence.ast.node(3u);
    if (multiply.kind != AstNodeKind::BinaryExpression ||
        multiply.binaryOperator != AstBinaryOperator::Multiply ||
        multiply.left != 1u || multiply.right != 2u ||
        add.location.offset != 0u || add.location.length != 5u ||
        statement.location.length != 6u) return false;

    const char malformedSource[] = "var = 1;";
    const SourceView malformedView(malformedSource,
        sizeof(malformedSource) - 1u);
    const LexResult malformedTokens = lexer.tokenize(malformedView);
    if (!malformedTokens.succeeded()) return false;
    const ParseResult malformed = parser.parse(malformedView, malformedTokens);
    if (malformed.succeeded() ||
        malformed.error.code != ParserErrorCode::ExpectedToken ||
        malformed.error.location.offset != 4u ||
        malformed.error.location.line != 1u ||
        malformed.error.location.column != 5u ||
        malformed.error.tokenIndex != 1u) return false;

    char nestedParentheses[402];
    for (std::size_t index = 0; index < 200u; ++index)
        nestedParentheses[index] = '(';
    nestedParentheses[200] = '1';
    for (std::size_t index = 0; index < 200u; ++index)
        nestedParentheses[201u + index] = ')';
    nestedParentheses[401] = ';';
    const SourceView nestedView(nestedParentheses,
        sizeof(nestedParentheses));
    const LexResult nestedTokens = lexer.tokenize(nestedView);
    if (!nestedTokens.succeeded() ||
        !parser.parse(nestedView, nestedTokens).succeeded()) return false;

    std::size_t nestedIfLength = 0u;
    for (std::size_t index = 0; index < 250u; ++index) {
        nestedIfSource[nestedIfLength++] = 'i';
        nestedIfSource[nestedIfLength++] = 'f';
        nestedIfSource[nestedIfLength++] = '(';
        nestedIfSource[nestedIfLength++] = 'a';
        nestedIfSource[nestedIfLength++] = ')';
    }
    nestedIfSource[nestedIfLength++] = ';';
    nestedIfSource[nestedIfLength] = '\0';
    if (!comparePath(nestedIfSource, true)) return false;

    const std::size_t acceptedArrayLength = makeNestedArraySource(30u);
    if (!comparePath(nestedArraySource, true)) return false;
    const std::size_t limitedArrayLength = makeNestedArraySource(50u);
    const SourceView limitedArrayView(nestedArraySource,
        limitedArrayLength);
    const LexResult limitedArrayTokens = lexer.tokenize(limitedArrayView);
    if (!limitedArrayTokens.succeeded()) return false;
    const ParseResult limitedArray =
        parser.parse(limitedArrayView, limitedArrayTokens);
    if (limitedArray.succeeded() || limitedArray.error.code !=
            ParserErrorCode::ParserStackBudgetExceeded ||
        limitedArray.error.location.offset >= limitedArrayLength ||
        limitedArray.error.tokenIndex >= limitedArrayTokens.tokens.size() ||
        limitedArray.ast.root() != kInvalidAstNodeId ||
        limitedArray.ast.nodeCount() != 0u ||
        acceptedArrayLength == 0u ||
        !comparePath(nestedArraySource, false)) return false;

    const char metadataSource[] =
        "const f=(x=1,...rest)=>x+rest[0]; var a=[1,,3]; var o={x:1,y:2};";
    const SourceView metadataView(metadataSource, sizeof(metadataSource) - 1u);
    const LexResult metadataTokens = lexer.tokenize(metadataView);
    if (!metadataTokens.succeeded()) return false;
    ParseResult metadata = parser.parse(metadataView, metadataTokens);
    if (!metadata.succeeded()) return false;
    const Ast& ast = metadata.ast;
    const AstNodeId program = ast.root();
    if (ast.childCount(program) != 3u ||
        ast.node(ast.childAt(program, 0u)).kind !=
            AstNodeKind::VariableDeclaration ||
        ast.node(ast.childAt(program, 1u)).kind !=
            AstNodeKind::VariableDeclaration ||
        ast.node(ast.childAt(program, 2u)).kind !=
            AstNodeKind::VariableDeclaration) return false;
    AstNodeId arrow = kInvalidAstNodeId;
    AstNodeId array = kInvalidAstNodeId;
    AstNodeId object = kInvalidAstNodeId;
    for (AstNodeId id = 0; id < ast.nodeCount(); ++id) {
        if (ast.node(id).kind == AstNodeKind::ArrowFunctionExpression)
            arrow = id;
        if (ast.node(id).kind == AstNodeKind::ArrayLiteral) array = id;
        if (ast.node(id).kind == AstNodeKind::ObjectLiteral) object = id;
    }
    if (arrow == kInvalidAstNodeId || array == kInvalidAstNodeId ||
        object == kInvalidAstNodeId || ast.childCount(arrow) != 2u ||
        ast.node(ast.childAt(arrow, 0u)).initializer == kInvalidAstNodeId ||
        !ast.node(ast.childAt(arrow, 1u)).prefix ||
        ast.node(arrow).expression == kInvalidAstNodeId ||
        ast.childCount(array) != 3u ||
        ast.node(ast.childAt(array, 1u)).kind != AstNodeKind::ArrayElision ||
        ast.childCount(object) != 2u) return false;
    const AstNodeId firstProperty = ast.childAt(object, 0u);
    const AstNodeId secondProperty = ast.childAt(object, 1u);
    return ast.node(firstProperty).kind == AstNodeKind::ObjectProperty &&
        ast.node(secondProperty).kind == AstNodeKind::ObjectProperty &&
        sameSourceView(ast.nodeText(ast.node(firstProperty).key),
            SourceView("x", 1u)) &&
        sameSourceView(ast.nodeText(ast.node(secondProperty).key),
            SourceView("y", 1u));
}

bool testParserMaximumSourceLength()
{
    for (std::size_t index = 0; index < sizeof(maximumSource); ++index)
        maximumSource[index] = ' ';
    const SourceView source(maximumSource, sizeof(maximumSource));
    const Lexer lexer;
    const LexResult tokens = lexer.tokenize(source);
    if (!tokens.succeeded() || tokens.tokens.size() != 1u) return false;
    const ParseResult result = Parser().parse(source, tokens);
    return result.succeeded() && result.ast.nodeCount() == 1u &&
        result.ast.node(result.ast.root()).kind == AstNodeKind::Program &&
        result.ast.node(result.ast.root()).location.offset == sizeof(maximumSource);
}
#endif

bool testPortableErrorAndCapacityPaths()
{
    const Lexer lexer;
    const Parser parser;
    const char sourceText[] = "x;";
    const SourceView source(sourceText, sizeof(sourceText) - 1u);
    TokenBuffer tokens{tokenStorage, kDefaultMaxJavaScriptTokens, 0u, 0u};
    if (!lexer.tokenizeInto(source, tokens).succeeded()) return false;
    TokenView stale = makeTokenView(source, tokens);
    ++tokens.generation;
    Ast staleAst(source, AstStorageView{astNodes,
        kDefaultMaxJavaScriptAstNodes, astChildren,
        kDefaultMaxJavaScriptAstNodes});
    FrontendArena scratch(scratchBytes, sizeof(scratchBytes));
    ParserError error;
    if (parser.parseInto(stale, staleAst, scratch, error) ||
        error.code != ParserErrorCode::SourceLifetimeInvalidation ||
        error.tokenIndex != 0u || staleAst.root() != kInvalidAstNodeId ||
        staleAst.nodeCount() != 0u) return false;

    TokenView tooMany{tokenStorage, kDefaultMaxJavaScriptTokens + 1u,
        source, 0u, nullptr};
    Ast tokenLimited(source, AstStorageView{astNodes,
        kDefaultMaxJavaScriptAstNodes, astChildren,
        kDefaultMaxJavaScriptAstNodes});
    if (parser.parseInto(tooMany, tokenLimited, scratch, error) ||
        error.code != ParserErrorCode::TokenCapacityExceeded ||
        tokenLimited.root() != kInvalidAstNodeId) return false;

    TokenView sourceTooLarge{tokens.data, tokens.count,
        SourceView(sourceText, kDefaultMaxJavaScriptSourceBytes + 1u),
        tokens.generation, &tokens.generation};
    Ast sourceLimited(source, AstStorageView{astNodes,
        kDefaultMaxJavaScriptAstNodes, astChildren,
        kDefaultMaxJavaScriptAstNodes});
    if (parser.parseInto(sourceTooLarge, sourceLimited, scratch, error) ||
        error.code != ParserErrorCode::SourceTooLarge ||
        sourceLimited.nodeCount() != 0u) return false;

    Token invalidToken;
    invalidToken.type = TokenType::EndOfInput;
    invalidToken.location = SourceLocation{0u, 0u, 1u, 1u};
    TokenView invalidInput{&invalidToken, 1u, source, 0u, nullptr};
    Ast invalidAst(source, AstStorageView{astNodes,
        kDefaultMaxJavaScriptAstNodes, astChildren,
        kDefaultMaxJavaScriptAstNodes});
    if (parser.parseInto(invalidInput, invalidAst, scratch, error) ||
        error.code != ParserErrorCode::InvalidTokenInput ||
        invalidAst.nodeCount() != 0u) return false;

    AstNode oneNode[1];
    AstNodeId oneChild[1];
    Ast astLimited(source, AstStorageView{oneNode, 1u, oneChild, 1u});
    if (parser.parseInto(makeTokenView(source, tokens), astLimited, scratch,
            error) || error.code != ParserErrorCode::AstStorageExhausted ||
        astLimited.root() != kInvalidAstNodeId ||
        astLimited.nodeCount() != 0u) return false;

    AstNode childNodes[16];
    Ast childLimited(source, AstStorageView{childNodes, 16u, nullptr, 0u});
    if (parser.parseInto(makeTokenView(source, tokens), childLimited, scratch,
            error) || error.code != ParserErrorCode::AstChildStorageExhausted ||
        childLimited.root() != kInvalidAstNodeId ||
        childLimited.nodeCount() != 0u) return false;

    Ast scratchLimited(source, AstStorageView{smallAstNodes, 8u,
        smallAstChildren, 8u});
    FrontendArena tiny(tinyScratch, sizeof(tinyScratch));
    if (parser.parseInto(makeTokenView(source, tokens), scratchLimited, tiny,
            error) || error.code !=
                ParserErrorCode::ScratchStorageLimitExceeded ||
        scratchLimited.root() != kInvalidAstNodeId ||
        scratchLimited.nodeCount() != 0u) return false;

    ParserLimits tinyStackLimits;
    tinyStackLimits.maxParserStackBytes =
        kJavaScriptParserStackEntryReserveBytes;
    const Parser tinyStackParser(tinyStackLimits);
    Ast stackLimited(source, AstStorageView{smallAstNodes, 8u,
        smallAstChildren, 8u});
    if (tinyStackParser.parseInto(makeTokenView(source, tokens),
            stackLimited, scratch, error) ||
        error.code != ParserErrorCode::ParserStackBudgetExceeded ||
        error.tokenIndex != 0u || stackLimited.root() != kInvalidAstNodeId ||
        stackLimited.nodeCount() != 0u) return false;

    const auto priorGeneration = tokens.generation;
    if (!lexer.tokenizeInto(source, tokens).succeeded() ||
        tokens.generation == priorGeneration) return false;
    Ast reused(source, AstStorageView{astNodes,
        kDefaultMaxJavaScriptAstNodes, astChildren,
        kDefaultMaxJavaScriptAstNodes});
    if (!parser.parseInto(makeTokenView(source, tokens), reused, scratch,
            error) || error.code != ParserErrorCode::None ||
        reused.root() == kInvalidAstNodeId || reused.nodeCount() == 0u)
        return false;
    return true;
}

bool testPortableSourceRetentionAcrossParseReuse()
{
    char sourceA[] = "var retained=42;";
    char sourceB[] = "var other=7;";
    const SourceView viewA(sourceA, sizeof(sourceA) - 1u);
    const SourceView viewB(sourceB, sizeof(sourceB) - 1u);
    TokenBuffer tokensA{retainedTokensA, 64u, 0u, 0u};
    TokenBuffer tokensB{retainedTokensB, 64u, 0u, 0u};
    const Lexer lexer;
    if (!lexer.tokenizeInto(viewA, tokensA).succeeded() ||
        !lexer.tokenizeInto(viewB, tokensB).succeeded()) return false;
    Ast astA(viewA, AstStorageView{retainedNodesA, 64u,
        retainedChildrenA, 64u});
    Ast astB(viewB, AstStorageView{retainedNodesB, 64u,
        retainedChildrenB, 64u});
    FrontendArena scratchA(retainedScratchA, sizeof(retainedScratchA));
    FrontendArena scratchB(retainedScratchB, sizeof(retainedScratchB));
    const Parser parser;
    ParserError errorA;
    ParserError errorB;
    if (!parser.parseInto(makeTokenView(viewA, tokensA), astA, scratchA,
            errorA) ||
        !parser.parseInto(makeTokenView(viewB, tokensB), astB, scratchB,
            errorB)) return false;
    bool foundName = false;
    bool foundValue = false;
    bool foundOther = false;
    for (AstNodeId id = 0; id < astA.nodeCount(); ++id) {
        const AstNode& node = astA.node(id);
        if (node.kind == AstNodeKind::Identifier &&
            astA.nodeText(id).length == 8u &&
            equalText(astA.nodeText(id).data, "retained", 8u))
            foundName = true;
        if (node.kind == AstNodeKind::NumericLiteral &&
            astA.nodeText(id).length == 2u &&
            equalText(astA.nodeText(id).data, "42", 2u)) foundValue = true;
    }
    for (AstNodeId id = 0; id < astB.nodeCount(); ++id) {
        const AstNode& node = astB.node(id);
        if (node.kind == AstNodeKind::Identifier &&
            astB.nodeText(id).length == 5u &&
            equalText(astB.nodeText(id).data, "other", 5u)) foundOther = true;
    }
    return foundName && foundValue && foundOther;
}

} // namespace

int main()
{
#if !defined(GXOS_NAVIGATOR_FRONTEND_FREESTANDING)
    if (!testSharedGrammarCorpus()) return 1;
    if (!testHandAuthoredExpectedResults()) return 2;
    if (!testScratchListBoundaries()) return 3;
    if (!testParserMaximumSourceLength()) return 4;
#endif
    if (!testPortableErrorAndCapacityPaths()) return 5;
    if (!testPortableSourceRetentionAcrossParseReuse()) return 6;
    return 0;
}
