#include "parser.h"

#include <cstddef>
#include <limits>
#include <new>
#include <utility>

namespace gxos {
namespace javascript {
namespace {

SourceLocation spanLocation(SourceLocation first, SourceLocation last)
{
    const std::size_t firstEnd = first.offset + first.length;
    const std::size_t lastEnd = last.offset + last.length;
    const std::size_t end = lastEnd >= firstEnd ? lastEnd : firstEnd;
    first.length = end >= first.offset ? end - first.offset : 0;
    return first;
}

class ParserState {
public:
    ParserState(SourceView source, const std::vector<Token>& tokens,
        ParserLimits limits)
        : source_(source), tokens_(tokens), limits_(limits), ast_(source)
    {
        eofToken_.type = TokenType::EndOfInput;
        eofToken_.location.offset = source.length;
        eofToken_.location.line = 1;
        eofToken_.location.column = 1;
    }

    ParseResult run()
    {
        if (tokens_.empty() || tokens_.back().type != TokenType::EndOfInput) {
            fail(ParserErrorCode::UnexpectedEndOfInput, eofToken_.location,
                TokenType::EndOfInput, true);
            return result();
        }

        std::vector<AstNodeId> statements;
        while (!at(TokenType::EndOfInput)) {
            if (pos_ >= tokens_.size()) {
                fail(ParserErrorCode::UnexpectedEndOfInput,
                    eofToken_.location, TokenType::EndOfInput, true);
                break;
            }
            const AstNodeId statement = parseStatement();
            if (statement == kInvalidAstNodeId) break;
            statements.push_back(statement);
        }

        if (!failed() && pos_ != tokens_.size() - 1) {
            fail(ParserErrorCode::UnexpectedToken, current().location,
                TokenType::EndOfInput, true);
        }

        if (!failed()) {
            SourceLocation programLocation = current().location;
            if (!statements.empty()) {
                programLocation = spanLocation(
                    ast_.node(statements.front()).location,
                    ast_.node(statements.back()).location);
            }
            const AstNodeId program = makeNode(AstNodeKind::Program,
                programLocation);
            if (program != kInvalidAstNodeId && !setChildren(program, statements)) {
                fail(ParserErrorCode::AstNodeLimitExceeded, current().location,
                    TokenType::EndOfInput, false);
            } else if (program != kInvalidAstNodeId && !ast_.setRoot(program)) {
                fail(ParserErrorCode::AstNodeLimitExceeded, current().location,
                    TokenType::EndOfInput, false);
            }
        }

        return result();
    }

private:
    class DepthGuard {
    public:
        DepthGuard(ParserState& state, SourceLocation location)
            : state_(state), active_(false)
        {
            if (state_.parserDepth_ >= state_.limits_.maxParserDepth) {
                state_.fail(ParserErrorCode::NestingLimitExceeded, location,
                    TokenType::EndOfInput, false);
                return;
            }
            ++state_.parserDepth_;
            active_ = true;
        }

        ~DepthGuard()
        {
            if (active_) --state_.parserDepth_;
        }

        bool entered() const { return active_; }

    private:
        ParserState& state_;
        bool active_;
    };

    class ExpressionGuard {
    public:
        ExpressionGuard(ParserState& state, SourceLocation location)
            : state_(state), active_(false)
        {
            if (state_.expressionDepth_ >= state_.limits_.maxExpressionNesting) {
                state_.fail(ParserErrorCode::NestingLimitExceeded, location,
                    TokenType::EndOfInput, false);
                return;
            }
            ++state_.expressionDepth_;
            active_ = true;
        }

        ~ExpressionGuard()
        {
            if (active_) --state_.expressionDepth_;
        }

        bool entered() const { return active_; }

    private:
        ParserState& state_;
        bool active_;
    };

    class BlockGuard {
    public:
        BlockGuard(ParserState& state, SourceLocation location)
            : state_(state), active_(false)
        {
            if (state_.blockNesting_ >= state_.limits_.maxBlockNesting) {
                state_.fail(ParserErrorCode::NestingLimitExceeded, location,
                    TokenType::EndOfInput, false);
                return;
            }
            ++state_.blockNesting_;
            active_ = true;
        }

        ~BlockGuard()
        {
            if (active_) --state_.blockNesting_;
        }

        bool entered() const { return active_; }

    private:
        ParserState& state_;
        bool active_;
    };

    ParseResult result()
    {
        if (failed()) ast_.reset(source_);
        ParseResult output;
        output.ast = std::move(ast_);
        output.error = error_;
        return output;
    }

    bool failed() const { return error_.code != ParserErrorCode::None; }

    const Token& current() const
    {
        if (pos_ < tokens_.size()) return tokens_[pos_];
        return eofToken_;
    }

    bool at(TokenType type) const { return current().type == type; }

    const Token& advance()
    {
        const Token& token = current();
        if (pos_ < tokens_.size()) ++pos_;
        return token;
    }

    void fail(ParserErrorCode code, SourceLocation location,
        TokenType expected = TokenType::EndOfInput, bool hasExpected = false)
    {
        if (failed()) return;
        error_.code = code;
        error_.location = location;
        error_.actual = current().type;
        error_.expected = expected;
        error_.hasExpected = hasExpected;
    }

    bool expect(TokenType type)
    {
        if (at(type)) {
            advance();
            return true;
        }
        if (at(TokenType::EndOfInput)) {
            fail(ParserErrorCode::UnexpectedEndOfInput, current().location,
                type, true);
        } else {
            fail(ParserErrorCode::ExpectedToken, current().location, type, true);
        }
        return false;
    }

    bool expectIdentifier(Token& token)
    {
        if (at(TokenType::Identifier)) {
            token = advance();
            return true;
        }
        if (at(TokenType::EndOfInput)) {
            fail(ParserErrorCode::UnexpectedEndOfInput, current().location,
                TokenType::Identifier, true);
        } else {
            fail(ParserErrorCode::ExpectedToken, current().location,
                TokenType::Identifier, true);
        }
        return false;
    }

    AstNodeId makeNode(AstNodeKind kind, SourceLocation location)
    {
        if (failed()) return kInvalidAstNodeId;
        if (ast_.nodeCount() >= limits_.maxAstNodes) {
            fail(ParserErrorCode::AstNodeLimitExceeded, location,
                TokenType::EndOfInput, false);
            return kInvalidAstNodeId;
        }

        AstNode node;
        node.kind = kind;
        node.location = location;
        const AstNodeId id = ast_.addNode(node);
        if (id == kInvalidAstNodeId) {
            fail(ParserErrorCode::AstNodeLimitExceeded, location,
                TokenType::EndOfInput, false);
        }
        return id;
    }

    bool setChildren(AstNodeId parent, const std::vector<AstNodeId>& children)
    {
        if (children.size() > limits_.maxAstNodes) return false;
        return ast_.setChildren(parent, children.data(), children.size());
    }

    AstNodeId makeLeaf(AstNodeKind kind, const Token& token)
    {
        return makeNode(kind, token.location);
    }

    AstNodeId makeBinary(AstNodeKind kind, AstNodeId left, AstNodeId right,
        SourceLocation location)
    {
        const AstNodeId id = makeNode(kind, location);
        if (id == kInvalidAstNodeId) return id;
        ast_.node(id).left = left;
        ast_.node(id).right = right;
        return id;
    }

    AstNodeId parseStatement()
    {
        DepthGuard depth(*this, current().location);
        if (!depth.entered()) return kInvalidAstNodeId;
        if (statementCount_ >= limits_.maxStatements) {
            fail(ParserErrorCode::TooManyStatements, current().location,
                TokenType::EndOfInput, false);
            return kInvalidAstNodeId;
        }
        ++statementCount_;

        if (at(TokenType::EndOfInput)) {
            fail(ParserErrorCode::UnexpectedEndOfInput, current().location,
                TokenType::EndOfInput, false);
            return kInvalidAstNodeId;
        }
        if (at(TokenType::Semicolon)) {
            const Token semicolon = advance();
            return makeLeaf(AstNodeKind::EmptyStatement, semicolon);
        }
        if (at(TokenType::LeftBrace)) return parseBlock();
        if (at(TokenType::KeywordVar)) return parseVariableDeclaration(true);
        if (at(TokenType::KeywordFunction)) return parseFunctionDeclaration();
        if (at(TokenType::KeywordReturn)) return parseReturnStatement();
        if (at(TokenType::KeywordIf)) return parseIfStatement();
        if (at(TokenType::KeywordWhile)) return parseWhileStatement();
        if (at(TokenType::KeywordFor)) return parseForStatement();
        if (at(TokenType::KeywordBreak)) return parseBreakOrContinue(false);
        if (at(TokenType::KeywordContinue)) return parseBreakOrContinue(true);

        const SourceLocation start = current().location;
        const AstNodeId expression = parseExpression();
        if (expression == kInvalidAstNodeId) return kInvalidAstNodeId;
        if (!expect(TokenType::Semicolon)) return kInvalidAstNodeId;
        const SourceLocation end = tokens_[pos_ - 1].location;
        const AstNodeId statement = makeNode(AstNodeKind::ExpressionStatement,
            spanLocation(start, end));
        if (statement != kInvalidAstNodeId) ast_.node(statement).expression = expression;
        return statement;
    }

    AstNodeId parseBlock()
    {
        DepthGuard depth(*this, current().location);
        if (!depth.entered()) return kInvalidAstNodeId;
        const Token open = advance();
        BlockGuard block(*this, open.location);
        if (!block.entered()) return kInvalidAstNodeId;

        std::vector<AstNodeId> statements;
        while (!at(TokenType::RightBrace) && !at(TokenType::EndOfInput)) {
            const AstNodeId statement = parseStatement();
            if (statement == kInvalidAstNodeId) return kInvalidAstNodeId;
            statements.push_back(statement);
        }
        if (!expect(TokenType::RightBrace)) return kInvalidAstNodeId;
        const Token close = tokens_[pos_ - 1];
        const AstNodeId blockNode = makeNode(AstNodeKind::BlockStatement,
            spanLocation(open.location, close.location));
        if (blockNode != kInvalidAstNodeId && !setChildren(blockNode, statements)) {
            fail(ParserErrorCode::AstNodeLimitExceeded, close.location,
                TokenType::EndOfInput, false);
            return kInvalidAstNodeId;
        }
        return blockNode;
    }

    AstNodeId parseVariableDeclaration(bool requireSemicolon)
    {
        const Token keyword = advance();
        std::vector<AstNodeId> declarators;
        while (true) {
            Token nameToken;
            if (!expectIdentifier(nameToken)) return kInvalidAstNodeId;
            const AstNodeId name = makeLeaf(AstNodeKind::Identifier, nameToken);
            if (name == kInvalidAstNodeId) return kInvalidAstNodeId;

            AstNodeId initializer = kInvalidAstNodeId;
            SourceLocation end = nameToken.location;
            if (at(TokenType::Assign)) {
                advance();
                initializer = parseExpression();
                if (initializer == kInvalidAstNodeId) return kInvalidAstNodeId;
                end = ast_.node(initializer).location;
            }

            const AstNodeId declarator = makeNode(AstNodeKind::VariableDeclarator,
                spanLocation(nameToken.location, end));
            if (declarator == kInvalidAstNodeId) return kInvalidAstNodeId;
            ast_.node(declarator).name = name;
            ast_.node(declarator).initializer = initializer;
            declarators.push_back(declarator);

            if (!at(TokenType::Comma)) break;
            advance();
        }

        SourceLocation end = ast_.node(declarators.back()).location;
        if (requireSemicolon) {
            if (!expect(TokenType::Semicolon)) return kInvalidAstNodeId;
            end = tokens_[pos_ - 1].location;
        }
        const AstNodeId declaration = makeNode(AstNodeKind::VariableDeclaration,
            spanLocation(keyword.location, end));
        if (declaration != kInvalidAstNodeId && !setChildren(declaration, declarators)) {
            fail(ParserErrorCode::AstNodeLimitExceeded, keyword.location,
                TokenType::EndOfInput, false);
            return kInvalidAstNodeId;
        }
        return declaration;
    }

    AstNodeId parseFunctionDeclaration()
    {
        const Token keyword = advance();
        Token nameToken;
        if (!expectIdentifier(nameToken)) return kInvalidAstNodeId;
        const AstNodeId name = makeLeaf(AstNodeKind::Identifier, nameToken);
        if (name == kInvalidAstNodeId) return kInvalidAstNodeId;
        if (!expect(TokenType::LeftParen)) return kInvalidAstNodeId;

        std::vector<AstNodeId> parameters;
        while (!at(TokenType::RightParen)) {
            if (at(TokenType::EndOfInput)) {
                fail(ParserErrorCode::UnexpectedEndOfInput, current().location,
                    TokenType::RightParen, true);
                return kInvalidAstNodeId;
            }
            if (parameters.size() >= limits_.maxFunctionParameters) {
                fail(ParserErrorCode::TooManyParameters, current().location,
                    TokenType::Identifier, true);
                return kInvalidAstNodeId;
            }
            Token parameterToken;
            if (!expectIdentifier(parameterToken)) return kInvalidAstNodeId;
            const AstNodeId parameter = makeLeaf(AstNodeKind::Identifier,
                parameterToken);
            if (parameter == kInvalidAstNodeId) return kInvalidAstNodeId;
            parameters.push_back(parameter);
            if (!at(TokenType::Comma)) break;
            advance();
        }
        if (!expect(TokenType::RightParen)) return kInvalidAstNodeId;
        const AstNodeId body = parseBlock();
        if (body == kInvalidAstNodeId) return kInvalidAstNodeId;

        const AstNodeId function = makeNode(AstNodeKind::FunctionDeclaration,
            spanLocation(keyword.location, ast_.node(body).location));
        if (function == kInvalidAstNodeId) return kInvalidAstNodeId;
        ast_.node(function).name = name;
        ast_.node(function).body = body;
        if (!setChildren(function, parameters)) {
            fail(ParserErrorCode::AstNodeLimitExceeded, keyword.location,
                TokenType::EndOfInput, false);
            return kInvalidAstNodeId;
        }
        return function;
    }

    AstNodeId parseReturnStatement()
    {
        const Token keyword = advance();
        AstNodeId expression = kInvalidAstNodeId;
        if (!at(TokenType::Semicolon)) {
            expression = parseExpression();
            if (expression == kInvalidAstNodeId) return kInvalidAstNodeId;
        }
        if (!expect(TokenType::Semicolon)) return kInvalidAstNodeId;
        const Token semicolon = tokens_[pos_ - 1];
        const AstNodeId statement = makeNode(AstNodeKind::ReturnStatement,
            spanLocation(keyword.location, semicolon.location));
        if (statement != kInvalidAstNodeId) ast_.node(statement).expression = expression;
        return statement;
    }

    AstNodeId parseIfStatement()
    {
        const Token keyword = advance();
        if (!expect(TokenType::LeftParen)) return kInvalidAstNodeId;
        const AstNodeId test = parseExpression();
        if (test == kInvalidAstNodeId) return kInvalidAstNodeId;
        if (!expect(TokenType::RightParen)) return kInvalidAstNodeId;
        const AstNodeId consequent = parseStatement();
        if (consequent == kInvalidAstNodeId) return kInvalidAstNodeId;

        AstNodeId alternate = kInvalidAstNodeId;
        SourceLocation end = ast_.node(consequent).location;
        if (at(TokenType::KeywordElse)) {
            advance();
            alternate = parseStatement();
            if (alternate == kInvalidAstNodeId) return kInvalidAstNodeId;
            end = ast_.node(alternate).location;
        }

        const AstNodeId statement = makeNode(AstNodeKind::IfStatement,
            spanLocation(keyword.location, end));
        if (statement != kInvalidAstNodeId) {
            ast_.node(statement).test = test;
            ast_.node(statement).consequent = consequent;
            ast_.node(statement).alternate = alternate;
        }
        return statement;
    }

    AstNodeId parseWhileStatement()
    {
        const Token keyword = advance();
        if (!expect(TokenType::LeftParen)) return kInvalidAstNodeId;
        const AstNodeId test = parseExpression();
        if (test == kInvalidAstNodeId) return kInvalidAstNodeId;
        if (!expect(TokenType::RightParen)) return kInvalidAstNodeId;
        const AstNodeId body = parseStatement();
        if (body == kInvalidAstNodeId) return kInvalidAstNodeId;

        const AstNodeId statement = makeNode(AstNodeKind::WhileStatement,
            spanLocation(keyword.location, ast_.node(body).location));
        if (statement != kInvalidAstNodeId) {
            ast_.node(statement).test = test;
            ast_.node(statement).body = body;
        }
        return statement;
    }

    AstNodeId parseForStatement()
    {
        const Token keyword = advance();
        if (!expect(TokenType::LeftParen)) return kInvalidAstNodeId;

        AstNodeId init = kInvalidAstNodeId;
        if (!at(TokenType::Semicolon)) {
            init = at(TokenType::KeywordVar)
                ? parseVariableDeclaration(false) : parseExpression();
            if (init == kInvalidAstNodeId) return kInvalidAstNodeId;
        }
        if (!expect(TokenType::Semicolon)) return kInvalidAstNodeId;

        AstNodeId test = kInvalidAstNodeId;
        if (!at(TokenType::Semicolon)) {
            test = parseExpression();
            if (test == kInvalidAstNodeId) return kInvalidAstNodeId;
        }
        if (!expect(TokenType::Semicolon)) return kInvalidAstNodeId;

        AstNodeId update = kInvalidAstNodeId;
        if (!at(TokenType::RightParen)) {
            update = parseExpression();
            if (update == kInvalidAstNodeId) return kInvalidAstNodeId;
        }
        if (!expect(TokenType::RightParen)) return kInvalidAstNodeId;
        const AstNodeId body = parseStatement();
        if (body == kInvalidAstNodeId) return kInvalidAstNodeId;

        const AstNodeId statement = makeNode(AstNodeKind::ForStatement,
            spanLocation(keyword.location, ast_.node(body).location));
        if (statement != kInvalidAstNodeId) {
            ast_.node(statement).init = init;
            ast_.node(statement).test = test;
            ast_.node(statement).update = update;
            ast_.node(statement).body = body;
        }
        return statement;
    }

    AstNodeId parseBreakOrContinue(bool continueStatement)
    {
        const Token keyword = advance();
        if (!expect(TokenType::Semicolon)) return kInvalidAstNodeId;
        const Token semicolon = tokens_[pos_ - 1];
        return makeNode(continueStatement ? AstNodeKind::ContinueStatement
                                           : AstNodeKind::BreakStatement,
            spanLocation(keyword.location, semicolon.location));
    }

    AstNodeId parseExpression()
    {
        DepthGuard depth(*this, current().location);
        if (!depth.entered()) return kInvalidAstNodeId;
        ExpressionGuard expressionDepth(*this, current().location);
        if (!expressionDepth.entered()) return kInvalidAstNodeId;
        return parseAssignmentExpression();
    }

    AstNodeId parseAssignmentExpression()
    {
        DepthGuard depth(*this, current().location);
        if (!depth.entered()) return kInvalidAstNodeId;
        const AstNodeId left = parseLogicalOrExpression();
        if (left == kInvalidAstNodeId) return kInvalidAstNodeId;
        if (!isAssignmentToken(current().type)) return left;

        const Token operatorToken = advance();
        if (!isAssignable(left)) {
            fail(ParserErrorCode::InvalidAssignmentTarget,
                ast_.node(left).location, TokenType::EndOfInput, false);
            return kInvalidAstNodeId;
        }
        const AstNodeId right = parseAssignmentExpression();
        if (right == kInvalidAstNodeId) return kInvalidAstNodeId;
        const AstNodeId assignment = makeBinary(AstNodeKind::AssignmentExpression,
            left, right, spanLocation(ast_.node(left).location,
                ast_.node(right).location));
        if (assignment != kInvalidAstNodeId) {
            ast_.node(assignment).assignmentOperator =
                assignmentOperator(operatorToken.type);
        }
        return assignment;
    }

    AstNodeId parseLogicalOrExpression()
    {
        AstNodeId left = parseLogicalAndExpression();
        while (!failed() && at(TokenType::LogicalOr)) {
            advance();
            const AstNodeId right = parseLogicalAndExpression();
            if (right == kInvalidAstNodeId) return kInvalidAstNodeId;
            const AstNodeId logical = makeBinary(AstNodeKind::LogicalExpression,
                left, right, spanLocation(ast_.node(left).location,
                    ast_.node(right).location));
            if (logical == kInvalidAstNodeId) return logical;
            ast_.node(logical).logicalOperator = AstLogicalOperator::Or;
            left = logical;
        }
        return left;
    }

    AstNodeId parseLogicalAndExpression()
    {
        AstNodeId left = parseEqualityExpression();
        while (!failed() && at(TokenType::LogicalAnd)) {
            advance();
            const AstNodeId right = parseEqualityExpression();
            if (right == kInvalidAstNodeId) return kInvalidAstNodeId;
            const AstNodeId logical = makeBinary(AstNodeKind::LogicalExpression,
                left, right, spanLocation(ast_.node(left).location,
                    ast_.node(right).location));
            if (logical == kInvalidAstNodeId) return logical;
            ast_.node(logical).logicalOperator = AstLogicalOperator::And;
            left = logical;
        }
        return left;
    }

    AstNodeId parseEqualityExpression()
    {
        AstNodeId left = parseRelationalExpression();
        while (!failed() && isEqualityToken(current().type)) {
            const TokenType type = advance().type;
            const AstNodeId right = parseRelationalExpression();
            if (right == kInvalidAstNodeId) return kInvalidAstNodeId;
            const AstNodeId binary = makeBinary(AstNodeKind::BinaryExpression,
                left, right, spanLocation(ast_.node(left).location,
                    ast_.node(right).location));
            if (binary == kInvalidAstNodeId) return binary;
            ast_.node(binary).binaryOperator = binaryOperator(type);
            left = binary;
        }
        return left;
    }

    AstNodeId parseRelationalExpression()
    {
        AstNodeId left = parseAdditiveExpression();
        while (!failed() && isRelationalToken(current().type)) {
            const TokenType type = advance().type;
            const AstNodeId right = parseAdditiveExpression();
            if (right == kInvalidAstNodeId) return kInvalidAstNodeId;
            const AstNodeId binary = makeBinary(AstNodeKind::BinaryExpression,
                left, right, spanLocation(ast_.node(left).location,
                    ast_.node(right).location));
            if (binary == kInvalidAstNodeId) return binary;
            ast_.node(binary).binaryOperator = binaryOperator(type);
            left = binary;
        }
        return left;
    }

    AstNodeId parseAdditiveExpression()
    {
        AstNodeId left = parseMultiplicativeExpression();
        while (!failed() &&
            (at(TokenType::Plus) || at(TokenType::Minus))) {
            const TokenType type = advance().type;
            const AstNodeId right = parseMultiplicativeExpression();
            if (right == kInvalidAstNodeId) return kInvalidAstNodeId;
            const AstNodeId binary = makeBinary(AstNodeKind::BinaryExpression,
                left, right, spanLocation(ast_.node(left).location,
                    ast_.node(right).location));
            if (binary == kInvalidAstNodeId) return binary;
            ast_.node(binary).binaryOperator = binaryOperator(type);
            left = binary;
        }
        return left;
    }

    AstNodeId parseMultiplicativeExpression()
    {
        AstNodeId left = parseUnaryExpression();
        while (!failed() && (at(TokenType::Star) || at(TokenType::Slash) ||
            at(TokenType::Percent))) {
            const TokenType type = advance().type;
            const AstNodeId right = parseUnaryExpression();
            if (right == kInvalidAstNodeId) return kInvalidAstNodeId;
            const AstNodeId binary = makeBinary(AstNodeKind::BinaryExpression,
                left, right, spanLocation(ast_.node(left).location,
                    ast_.node(right).location));
            if (binary == kInvalidAstNodeId) return binary;
            ast_.node(binary).binaryOperator = binaryOperator(type);
            left = binary;
        }
        return left;
    }

    AstNodeId parseUnaryExpression()
    {
        DepthGuard depth(*this, current().location);
        if (!depth.entered()) return kInvalidAstNodeId;

        if (at(TokenType::Not) || at(TokenType::Plus) || at(TokenType::Minus)) {
            const Token operatorToken = advance();
            const AstNodeId argument = parseUnaryExpression();
            if (argument == kInvalidAstNodeId) return kInvalidAstNodeId;
            const AstNodeId unary = makeNode(AstNodeKind::UnaryExpression,
                spanLocation(operatorToken.location, ast_.node(argument).location));
            if (unary != kInvalidAstNodeId) {
                ast_.node(unary).argument = argument;
                ast_.node(unary).unaryOperator = unaryOperator(operatorToken.type);
            }
            return unary;
        }

        if (at(TokenType::Increment) || at(TokenType::Decrement)) {
            const Token operatorToken = advance();
            const AstNodeId argument = parseUnaryExpression();
            if (argument == kInvalidAstNodeId) return kInvalidAstNodeId;
            if (!isAssignable(argument)) {
                fail(ParserErrorCode::InvalidAssignmentTarget,
                    ast_.node(argument).location, TokenType::EndOfInput, false);
                return kInvalidAstNodeId;
            }
            const AstNodeId update = makeNode(AstNodeKind::UpdateExpression,
                spanLocation(operatorToken.location, ast_.node(argument).location));
            if (update != kInvalidAstNodeId) {
                ast_.node(update).argument = argument;
                ast_.node(update).prefix = true;
                ast_.node(update).updateOperator = updateOperator(operatorToken.type);
            }
            return update;
        }

        if (at(TokenType::KeywordNew)) {
            const AstNodeId expression = parseNewExpression();
            if (expression == kInvalidAstNodeId) return kInvalidAstNodeId;
            return parsePostfixTail(expression);
        }
        return parsePostfixExpression();
    }

    AstNodeId parsePostfixExpression()
    {
        const AstNodeId expression = parsePrimaryExpression();
        if (expression == kInvalidAstNodeId) return kInvalidAstNodeId;
        return parsePostfixTail(expression);
    }

    AstNodeId parsePostfixTail(AstNodeId expression)
    {
        bool allowUpdate = true;
        while (!failed()) {
            if (at(TokenType::Dot) || at(TokenType::LeftBracket)) {
                expression = parseMemberTail(expression);
                if (expression == kInvalidAstNodeId) return expression;
                continue;
            }
            if (at(TokenType::LeftParen)) {
                expression = parseCallExpression(expression);
                if (expression == kInvalidAstNodeId) return expression;
                continue;
            }
            if (allowUpdate && (at(TokenType::Increment) || at(TokenType::Decrement))) {
                const Token operatorToken = advance();
                if (!isAssignable(expression)) {
                    fail(ParserErrorCode::InvalidAssignmentTarget,
                        ast_.node(expression).location, TokenType::EndOfInput,
                        false);
                    return kInvalidAstNodeId;
                }
                const AstNodeId update = makeNode(AstNodeKind::UpdateExpression,
                    spanLocation(ast_.node(expression).location,
                        operatorToken.location));
                if (update == kInvalidAstNodeId) return update;
                ast_.node(update).argument = expression;
                ast_.node(update).prefix = false;
                ast_.node(update).updateOperator = updateOperator(operatorToken.type);
                expression = update;
                allowUpdate = false;
                continue;
            }
            break;
        }
        return expression;
    }

    AstNodeId parseMemberTail(AstNodeId object)
    {
        if (at(TokenType::Dot)) {
            advance();
            Token propertyToken;
            if (!expectIdentifier(propertyToken)) return kInvalidAstNodeId;
            const AstNodeId property = makeLeaf(AstNodeKind::Identifier,
                propertyToken);
            if (property == kInvalidAstNodeId) return kInvalidAstNodeId;
            const AstNodeId member = makeNode(AstNodeKind::MemberExpression,
                spanLocation(ast_.node(object).location, propertyToken.location));
            if (member != kInvalidAstNodeId) {
                ast_.node(member).object = object;
                ast_.node(member).property = property;
                ast_.node(member).computed = false;
            }
            return member;
        }

        const Token open = advance();
        const AstNodeId property = parseExpression();
        if (property == kInvalidAstNodeId) return kInvalidAstNodeId;
        if (!expect(TokenType::RightBracket)) return kInvalidAstNodeId;
        const Token close = tokens_[pos_ - 1];
        const AstNodeId member = makeNode(AstNodeKind::MemberExpression,
            spanLocation(ast_.node(object).location, close.location));
        if (member != kInvalidAstNodeId) {
            ast_.node(member).object = object;
            ast_.node(member).property = property;
            ast_.node(member).computed = true;
        }
        (void)open;
        return member;
    }

    bool parseArgumentList(std::vector<AstNodeId>& arguments,
        SourceLocation& closeLocation)
    {
        if (!expect(TokenType::LeftParen)) return false;
        while (!at(TokenType::RightParen)) {
            if (at(TokenType::EndOfInput)) {
                fail(ParserErrorCode::UnexpectedEndOfInput, current().location,
                    TokenType::RightParen, true);
                return false;
            }
            if (arguments.size() >= limits_.maxCallArguments) {
                fail(ParserErrorCode::TooManyArguments, current().location,
                    TokenType::EndOfInput, false);
                return false;
            }
            const AstNodeId argument = parseExpression();
            if (argument == kInvalidAstNodeId) return false;
            arguments.push_back(argument);
            if (!at(TokenType::Comma)) break;
            advance();
        }
        if (!expect(TokenType::RightParen)) return false;
        closeLocation = tokens_[pos_ - 1].location;
        return true;
    }

    AstNodeId parseCallExpression(AstNodeId callee)
    {
        std::vector<AstNodeId> arguments;
        SourceLocation closeLocation;
        if (!parseArgumentList(arguments, closeLocation)) return kInvalidAstNodeId;
        const AstNodeId call = makeNode(AstNodeKind::CallExpression,
            spanLocation(ast_.node(callee).location, closeLocation));
        if (call == kInvalidAstNodeId) return kInvalidAstNodeId;
        ast_.node(call).callee = callee;
        if (!setChildren(call, arguments)) {
            fail(ParserErrorCode::AstNodeLimitExceeded, closeLocation,
                TokenType::EndOfInput, false);
            return kInvalidAstNodeId;
        }
        return call;
    }

    AstNodeId parseNewExpression()
    {
        const Token keyword = advance();
        AstNodeId callee = parsePrimaryExpression();
        if (callee == kInvalidAstNodeId) return kInvalidAstNodeId;
        while (at(TokenType::Dot) || at(TokenType::LeftBracket)) {
            callee = parseMemberTail(callee);
            if (callee == kInvalidAstNodeId) return kInvalidAstNodeId;
        }

        std::vector<AstNodeId> arguments;
        SourceLocation end = ast_.node(callee).location;
        if (at(TokenType::LeftParen)) {
            if (!parseArgumentList(arguments, end)) return kInvalidAstNodeId;
        }
        const AstNodeId expression = makeNode(AstNodeKind::NewExpression,
            spanLocation(keyword.location, end));
        if (expression == kInvalidAstNodeId) return kInvalidAstNodeId;
        ast_.node(expression).callee = callee;
        if (!setChildren(expression, arguments)) {
            fail(ParserErrorCode::AstNodeLimitExceeded, end,
                TokenType::EndOfInput, false);
            return kInvalidAstNodeId;
        }
        return expression;
    }

    AstNodeId parsePrimaryExpression()
    {
        if (at(TokenType::Identifier)) return makeLeaf(AstNodeKind::Identifier, advance());
        if (at(TokenType::NumericLiteral)) return makeLeaf(AstNodeKind::NumericLiteral, advance());
        if (at(TokenType::StringLiteral)) return makeLeaf(AstNodeKind::StringLiteral, advance());
        if (at(TokenType::KeywordTrue) || at(TokenType::KeywordFalse)) {
            return makeLeaf(AstNodeKind::BooleanLiteral, advance());
        }
        if (at(TokenType::KeywordNull)) return makeLeaf(AstNodeKind::NullLiteral, advance());
        if (at(TokenType::KeywordThis)) return makeLeaf(AstNodeKind::ThisExpression, advance());
        if (at(TokenType::LeftBrace)) return parseObjectLiteral();
        if (at(TokenType::LeftBracket)) return parseArrayLiteral();
        if (at(TokenType::LeftParen)) {
            const Token open = advance();
            const AstNodeId expression = parseExpression();
            if (expression == kInvalidAstNodeId) return kInvalidAstNodeId;
            if (!expect(TokenType::RightParen)) return kInvalidAstNodeId;
            const Token close = tokens_[pos_ - 1];
            ast_.node(expression).location = spanLocation(open.location,
                close.location);
            return expression;
        }

        if (at(TokenType::EndOfInput)) {
            fail(ParserErrorCode::UnexpectedEndOfInput, current().location,
                TokenType::Identifier, true);
        } else {
            fail(ParserErrorCode::InvalidExpression, current().location,
                TokenType::Identifier, false);
        }
        return kInvalidAstNodeId;
    }

    AstNodeId parseObjectLiteral()
    {
        const Token open = advance();
        std::vector<AstNodeId> properties;
        while (!at(TokenType::RightBrace)) {
            if (at(TokenType::EndOfInput)) {
                // Preserve the parser's established invalid-expression
                // diagnostic for a brace that cannot begin a complete
                // literal, while still reporting bounded literal errors.
                fail(ParserErrorCode::InvalidExpression, open.location,
                    TokenType::Identifier, false);
                return kInvalidAstNodeId;
            }
            if (properties.size() >= limits_.maxObjectLiteralProperties) {
                fail(ParserErrorCode::TooManyObjectProperties, current().location,
                    TokenType::RightBrace, true);
                return kInvalidAstNodeId;
            }

            Token keyToken;
            if (at(TokenType::Identifier) || at(TokenType::StringLiteral)) {
                keyToken = advance();
            } else {
                fail(ParserErrorCode::ExpectedToken, current().location,
                    TokenType::Identifier, true);
                return kInvalidAstNodeId;
            }
            const AstNodeId key = makeLeaf(
                keyToken.type == TokenType::StringLiteral
                    ? AstNodeKind::StringLiteral : AstNodeKind::Identifier,
                keyToken);
            if (key == kInvalidAstNodeId || !expect(TokenType::Colon)) {
                return kInvalidAstNodeId;
            }
            const AstNodeId initializer = parseExpression();
            if (initializer == kInvalidAstNodeId) return kInvalidAstNodeId;
            const AstNodeId property = makeNode(AstNodeKind::ObjectProperty,
                spanLocation(keyToken.location, ast_.node(initializer).location));
            if (property == kInvalidAstNodeId) return property;
            ast_.node(property).key = key;
            ast_.node(property).initializer = initializer;
            properties.push_back(property);

            if (!at(TokenType::Comma)) break;
            advance();
            if (at(TokenType::RightBrace)) break;
        }
        if (!expect(TokenType::RightBrace)) return kInvalidAstNodeId;
        const Token close = tokens_[pos_ - 1];
        const AstNodeId object = makeNode(AstNodeKind::ObjectLiteral,
            spanLocation(open.location, close.location));
        if (object != kInvalidAstNodeId && !setChildren(object, properties)) {
            fail(ParserErrorCode::AstNodeLimitExceeded, close.location,
                TokenType::EndOfInput, false);
            return kInvalidAstNodeId;
        }
        return object;
    }

    AstNodeId parseArrayLiteral()
    {
        const Token open = advance();
        std::vector<AstNodeId> elements;
        while (!at(TokenType::RightBracket)) {
            if (at(TokenType::EndOfInput)) {
                fail(ParserErrorCode::UnexpectedEndOfInput, current().location,
                    TokenType::RightBracket, true);
                return kInvalidAstNodeId;
            }
            if (elements.size() >= limits_.maxArrayLiteralElements) {
                fail(ParserErrorCode::TooManyArrayElements, current().location,
                    TokenType::RightBracket, true);
                return kInvalidAstNodeId;
            }
            const AstNodeId element = parseExpression();
            if (element == kInvalidAstNodeId) return kInvalidAstNodeId;
            elements.push_back(element);
            if (!at(TokenType::Comma)) break;
            advance();
            if (at(TokenType::RightBracket)) break;
        }
        if (!expect(TokenType::RightBracket)) return kInvalidAstNodeId;
        const Token close = tokens_[pos_ - 1];
        const AstNodeId array = makeNode(AstNodeKind::ArrayLiteral,
            spanLocation(open.location, close.location));
        if (array != kInvalidAstNodeId && !setChildren(array, elements)) {
            fail(ParserErrorCode::AstNodeLimitExceeded, close.location,
                TokenType::EndOfInput, false);
            return kInvalidAstNodeId;
        }
        return array;
    }

    bool isAssignable(AstNodeId expression) const
    {
        const AstNodeKind kind = ast_.node(expression).kind;
        return kind == AstNodeKind::Identifier ||
            kind == AstNodeKind::MemberExpression;
    }

    static bool isAssignmentToken(TokenType type)
    {
        return type == TokenType::Assign || type == TokenType::PlusAssign ||
            type == TokenType::MinusAssign || type == TokenType::StarAssign ||
            type == TokenType::SlashAssign || type == TokenType::PercentAssign;
    }

    static bool isEqualityToken(TokenType type)
    {
        return type == TokenType::Equal || type == TokenType::StrictEqual ||
            type == TokenType::NotEqual || type == TokenType::StrictNotEqual;
    }

    static bool isRelationalToken(TokenType type)
    {
        return type == TokenType::Less || type == TokenType::LessEqual ||
            type == TokenType::Greater || type == TokenType::GreaterEqual;
    }

    static AstUnaryOperator unaryOperator(TokenType type)
    {
        switch (type) {
        case TokenType::Not: return AstUnaryOperator::LogicalNot;
        case TokenType::Plus: return AstUnaryOperator::Plus;
        case TokenType::Minus: return AstUnaryOperator::Minus;
        default: return AstUnaryOperator::LogicalNot;
        }
    }

    static AstBinaryOperator binaryOperator(TokenType type)
    {
        switch (type) {
        case TokenType::Plus: return AstBinaryOperator::Add;
        case TokenType::Minus: return AstBinaryOperator::Subtract;
        case TokenType::Star: return AstBinaryOperator::Multiply;
        case TokenType::Slash: return AstBinaryOperator::Divide;
        case TokenType::Percent: return AstBinaryOperator::Remainder;
        case TokenType::Equal: return AstBinaryOperator::Equal;
        case TokenType::StrictEqual: return AstBinaryOperator::StrictEqual;
        case TokenType::NotEqual: return AstBinaryOperator::NotEqual;
        case TokenType::StrictNotEqual: return AstBinaryOperator::StrictNotEqual;
        case TokenType::Less: return AstBinaryOperator::Less;
        case TokenType::LessEqual: return AstBinaryOperator::LessEqual;
        case TokenType::Greater: return AstBinaryOperator::Greater;
        case TokenType::GreaterEqual: return AstBinaryOperator::GreaterEqual;
        default: return AstBinaryOperator::Add;
        }
    }

    static AstAssignmentOperator assignmentOperator(TokenType type)
    {
        switch (type) {
        case TokenType::Assign: return AstAssignmentOperator::Assign;
        case TokenType::PlusAssign: return AstAssignmentOperator::Add;
        case TokenType::MinusAssign: return AstAssignmentOperator::Subtract;
        case TokenType::StarAssign: return AstAssignmentOperator::Multiply;
        case TokenType::SlashAssign: return AstAssignmentOperator::Divide;
        case TokenType::PercentAssign: return AstAssignmentOperator::Remainder;
        default: return AstAssignmentOperator::Assign;
        }
    }

    static AstUpdateOperator updateOperator(TokenType type)
    {
        return type == TokenType::Decrement
            ? AstUpdateOperator::Decrement : AstUpdateOperator::Increment;
    }

    SourceView source_;
    const std::vector<Token>& tokens_;
    ParserLimits limits_;
    std::size_t pos_ = 0;
    std::size_t parserDepth_ = 0;
    std::size_t expressionDepth_ = 0;
    std::size_t blockNesting_ = 0;
    std::size_t statementCount_ = 0;
    Ast ast_;
    ParserError error_;
    Token eofToken_;
};

} // namespace

ParseResult Parser::parse(SourceView source, const std::vector<Token>& tokens) const
{
    try {
        ParserState state(source, tokens, limits_);
        return state.run();
    } catch (const std::bad_alloc&) {
        ParseResult result;
        result.ast.reset(source);
        result.error.code = ParserErrorCode::AllocationFailure;
        result.error.location.offset = source.length;
        result.error.location.line = 1;
        result.error.location.column = 1;
        result.error.actual = TokenType::EndOfInput;
        return result;
    }
}

ParseResult Parser::parse(SourceView source, const LexResult& lexed) const
{
    if (!lexed.succeeded()) {
        ParseResult result;
        result.ast.reset(source);
        result.error.code = ParserErrorCode::LexerFailure;
        result.error.location = lexed.error.location;
        result.error.actual = TokenType::EndOfInput;
        return result;
    }
    return parse(source, lexed.tokens);
}

const char* parserErrorCodeName(ParserErrorCode code)
{
    switch (code) {
    case ParserErrorCode::None: return "None";
    case ParserErrorCode::LexerFailure: return "LexerFailure";
    case ParserErrorCode::UnexpectedToken: return "UnexpectedToken";
    case ParserErrorCode::ExpectedToken: return "ExpectedToken";
    case ParserErrorCode::InvalidExpression: return "InvalidExpression";
    case ParserErrorCode::InvalidAssignmentTarget: return "InvalidAssignmentTarget";
    case ParserErrorCode::UnexpectedEndOfInput: return "UnexpectedEndOfInput";
    case ParserErrorCode::AstNodeLimitExceeded: return "AstNodeLimitExceeded";
    case ParserErrorCode::NestingLimitExceeded: return "NestingLimitExceeded";
    case ParserErrorCode::TooManyStatements: return "TooManyStatements";
    case ParserErrorCode::TooManyParameters: return "TooManyParameters";
    case ParserErrorCode::TooManyArguments: return "TooManyArguments";
    case ParserErrorCode::TooManyObjectProperties:
        return "TooManyObjectProperties";
    case ParserErrorCode::TooManyArrayElements:
        return "TooManyArrayElements";
    case ParserErrorCode::AllocationFailure: return "AllocationFailure";
    }
    return "Unknown";
}

} // namespace javascript
} // namespace gxos
