#include "parser.h"

#include <cstddef>
#include <limits>
#include <new>
#include <string>
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

        if (!failed() && !validateLexicalSemantics()) return result();

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

    bool identifierSpells(const Token& token, const char* spelling) const
    {
        if (token.type != TokenType::Identifier || spelling == nullptr)
            return false;
        std::size_t length = 0u;
        while (spelling[length] != '\0') ++length;
        if (token.lexeme.length != length || token.lexeme.data == nullptr)
            return false;
        for (std::size_t index = 0u; index < length; ++index)
            if (token.lexeme.data[index] != spelling[index]) return false;
        return true;
    }

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

    bool atIdentifierName() const
    {
        return at(TokenType::Identifier) || at(TokenType::KeywordLet) ||
            at(TokenType::KeywordConst);
    }

    Token advanceIdentifierName()
    {
        return advance();
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
        if (at(TokenType::KeywordVar) || at(TokenType::KeywordLet) ||
            at(TokenType::KeywordConst)) return parseVariableDeclaration(true);
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
        AstDeclarationKind declarationKind = AstDeclarationKind::Var;
        if (keyword.type == TokenType::KeywordLet)
            declarationKind = AstDeclarationKind::Let;
        else if (keyword.type == TokenType::KeywordConst)
            declarationKind = AstDeclarationKind::Const;
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
            } else if (declarationKind == AstDeclarationKind::Const) {
                fail(ParserErrorCode::InvalidLexicalDeclaration,
                    nameToken.location, TokenType::Assign, true);
                return kInvalidAstNodeId;
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
        if (declaration != kInvalidAstNodeId) {
            ast_.node(declaration).declarationKind = declarationKind;
            if (!setChildren(declaration, declarators)) {
                fail(ParserErrorCode::AstNodeLimitExceeded, keyword.location,
                    TokenType::EndOfInput, false);
                return kInvalidAstNodeId;
            }
        }
        return declaration;
    }

    bool parseFormalParameters(std::vector<AstNodeId>& parameters,
        bool arrowParameters, bool allowTrailingComma,
        bool& hasNonSimpleParameters)
    {
        bool hasDuplicate = false;
        SourceLocation duplicateLocation;
        while (!at(TokenType::RightParen)) {
            if (at(TokenType::EndOfInput)) {
                fail(ParserErrorCode::UnexpectedEndOfInput,
                    current().location, TokenType::RightParen, true);
                return false;
            }
            if (parameters.size() >= limits_.maxFunctionParameters) {
                fail(ParserErrorCode::TooManyParameters, current().location,
                    TokenType::Identifier, true);
                return false;
            }

            const bool rest = at(TokenType::Ellipsis);
            if (rest) {
                advance();
                hasNonSimpleParameters = true;
            }
            Token parameterToken;
            if (!expectIdentifier(parameterToken)) return false;
            const AstNodeId parameter = makeLeaf(AstNodeKind::Identifier,
                parameterToken);
            if (parameter == kInvalidAstNodeId) return false;
            ast_.node(parameter).prefix = rest;

            for (AstNodeId prior : parameters) {
                if (sameNodeName(prior, parameter)) {
                    hasDuplicate = true;
                    duplicateLocation = parameterToken.location;
                    break;
                }
            }

            if (at(TokenType::Assign)) {
                const Token assign = advance();
                if (rest) {
                    fail(ParserErrorCode::InvalidLexicalDeclaration,
                        assign.location, TokenType::Identifier, false);
                    return false;
                }
                const AstNodeId initializer = parseAssignmentExpression();
                if (initializer == kInvalidAstNodeId) return false;
                ast_.node(parameter).initializer = initializer;
                hasNonSimpleParameters = true;
            }

            parameters.push_back(parameter);
            if (rest) {
                if (at(TokenType::Comma)) {
                    fail(ParserErrorCode::InvalidLexicalDeclaration,
                        current().location, TokenType::RightParen, true);
                    return false;
                }
                if (!at(TokenType::RightParen)) {
                    fail(ParserErrorCode::ExpectedToken, current().location,
                        TokenType::RightParen, true);
                    return false;
                }
                break;
            }
            if (!at(TokenType::Comma)) break;
            advance();
            if (at(TokenType::RightParen)) {
                if (!allowTrailingComma) {
                    fail(ParserErrorCode::InvalidLexicalDeclaration,
                        current().location, TokenType::Identifier, true);
                    return false;
                }
                break;
            }
        }

        if (hasDuplicate && (arrowParameters || hasNonSimpleParameters)) {
            fail(ParserErrorCode::DuplicateParameter, duplicateLocation,
                TokenType::Identifier, false);
            return false;
        }
        return true;
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
        bool hasNonSimpleParameters = false;
        if (!parseFormalParameters(parameters, false, true,
                hasNonSimpleParameters)) return kInvalidAstNodeId;
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

    // JS9 only needs anonymous function expressions for direct onclick
    // assignment. Keep the existing declaration grammar and bounds, without
    // adding named expressions or broader function syntax.
    AstNodeId parseFunctionExpression()
    {
        const Token keyword = advance();
        if (!expect(TokenType::LeftParen)) return kInvalidAstNodeId;

        std::vector<AstNodeId> parameters;
        bool hasNonSimpleParameters = false;
        if (!parseFormalParameters(parameters, false, true,
                hasNonSimpleParameters)) return kInvalidAstNodeId;
        if (!expect(TokenType::RightParen)) return kInvalidAstNodeId;
        const AstNodeId body = parseBlock();
        if (body == kInvalidAstNodeId) return kInvalidAstNodeId;

        const AstNodeId function = makeNode(AstNodeKind::FunctionExpression,
            spanLocation(keyword.location, ast_.node(body).location));
        if (function == kInvalidAstNodeId) return kInvalidAstNodeId;
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

        const bool constBinding = at(TokenType::KeywordConst) ||
            identifierSpells(current(), "const");
        const bool letBinding = at(TokenType::KeywordLet) ||
            identifierSpells(current(), "let");
        if (constBinding || letBinding) {
            advance();
            Token nameToken;
            if (!expectIdentifier(nameToken)) return kInvalidAstNodeId;
            const AstNodeId name = makeLeaf(AstNodeKind::Identifier,
                nameToken);
            if (name == kInvalidAstNodeId) return kInvalidAstNodeId;
            if (!identifierSpells(current(), "of")) {
                if (at(TokenType::EndOfInput)) {
                    fail(ParserErrorCode::UnexpectedEndOfInput,
                        current().location, TokenType::Identifier, true);
                } else {
                    fail(ParserErrorCode::UnexpectedToken,
                        current().location, TokenType::Identifier, true);
                }
                return kInvalidAstNodeId;
            }
            advance();
            const AstNodeId iterable = parseExpression();
            if (iterable == kInvalidAstNodeId ||
                !expect(TokenType::RightParen)) return kInvalidAstNodeId;
            const AstNodeId body = parseStatement();
            if (body == kInvalidAstNodeId) return kInvalidAstNodeId;

            const AstNodeId statement = makeNode(AstNodeKind::ForOfStatement,
                spanLocation(keyword.location, ast_.node(body).location));
            if (statement != kInvalidAstNodeId) {
                ast_.node(statement).name = name;
                ast_.node(statement).expression = iterable;
                ast_.node(statement).body = body;
                ast_.node(statement).immutableBinding = constBinding;
            }
            return statement;
        }

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
        if (arrowAhead()) return parseArrowFunction();
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

    const Token& tokenAt(std::size_t position) const
    {
        if (position < tokens_.size()) return tokens_[position];
        return eofToken_;
    }

    bool arrowAhead() const
    {
        if (at(TokenType::Identifier))
            return tokenAt(pos_ + 1u).type == TokenType::Arrow;
        if (!at(TokenType::LeftParen)) return false;

        std::size_t scan = pos_ + 1u;
        std::size_t parenDepth = 1u;
        std::size_t bracketDepth = 0u;
        std::size_t braceDepth = 0u;
        std::size_t parameterSeparators = 0u;
        while (scan < tokens_.size()) {
            const TokenType type = tokenAt(scan).type;
            if (type == TokenType::EndOfInput) return false;
            if (type == TokenType::LeftParen) {
                ++parenDepth;
            } else if (type == TokenType::RightParen) {
                if (parenDepth == 1u && bracketDepth == 0u &&
                    braceDepth == 0u) {
                    return tokenAt(scan + 1u).type == TokenType::Arrow;
                }
                if (parenDepth != 0u) --parenDepth;
            } else if (type == TokenType::LeftBracket) {
                ++bracketDepth;
            } else if (type == TokenType::RightBracket) {
                if (bracketDepth != 0u) --bracketDepth;
            } else if (type == TokenType::LeftBrace) {
                ++braceDepth;
            } else if (type == TokenType::RightBrace) {
                if (braceDepth != 0u) --braceDepth;
            } else if (type == TokenType::Comma && parenDepth == 1u &&
                bracketDepth == 0u && braceDepth == 0u) {
                ++parameterSeparators;
                if (parameterSeparators >= limits_.maxFunctionParameters)
                    return false;
            }
            ++scan;
        }
        return false;
    }

    bool sameIdentifierText(const Token& left, const Token& right) const
    {
        if (left.lexeme.length != right.lexeme.length) return false;
        if (left.lexeme.length == 0u) return true;
        return left.lexeme.data != nullptr && right.lexeme.data != nullptr &&
            std::char_traits<char>::compare(left.lexeme.data,
                right.lexeme.data, left.lexeme.length) == 0;
    }

    bool sameNodeName(AstNodeId left, AstNodeId right) const
    {
        const SourceView leftName = ast_.nodeText(left);
        const SourceView rightName = ast_.nodeText(right);
        if (leftName.length != rightName.length) return false;
        if (leftName.length == 0u) return true;
        return leftName.data != nullptr && rightName.data != nullptr &&
            std::char_traits<char>::compare(leftName.data, rightName.data,
                leftName.length) == 0;
    }

    struct ScopeVisit {
        AstNodeId id = kInvalidAstNodeId;
        AstNodeId parent = kInvalidAstNodeId;
    };

    struct BindingNameRef {
        AstNodeId name = kInvalidAstNodeId;
        SourceLocation location;
    };

    bool addName(std::vector<BindingNameRef>& names, AstNodeId name,
        SourceLocation location, bool rejectDuplicate)
    {
        for (const BindingNameRef& prior : names) {
            if (sameNodeName(prior.name, name) && rejectDuplicate) {
                fail(ParserErrorCode::DuplicateBinding, location,
                    TokenType::Identifier, false);
                return false;
            }
        }
        names.push_back({name, location});
        return true;
    }

    bool collectDeclarationNames(AstNodeId declaration,
        std::vector<BindingNameRef>& names, bool rejectDuplicate)
    {
        const AstNode& node = ast_.node(declaration);
        for (std::size_t index = 0; index < node.childCount; ++index) {
            const AstNodeId declaratorId = ast_.childAt(declaration, index);
            const AstNode& declarator = ast_.node(declaratorId);
            if (declarator.kind != AstNodeKind::VariableDeclarator ||
                declarator.name == kInvalidAstNodeId) {
                fail(ParserErrorCode::InvalidLexicalDeclaration,
                    declarator.location, TokenType::Identifier, true);
                return false;
            }
            if (!addName(names, declarator.name,
                    ast_.node(declarator.name).location, rejectDuplicate))
                return false;
        }
        return true;
    }

    bool collectVarNamesInRegion(AstNodeId region,
        std::vector<BindingNameRef>& names)
    {
        std::vector<AstNodeId> pending;
        std::vector<bool> visited(ast_.nodeCount(), false);
        pending.push_back(region);
        while (!pending.empty()) {
            const AstNodeId id = pending.back();
            pending.pop_back();
            if (id >= ast_.nodeCount() || visited[id]) continue;
            visited[id] = true;
            const AstNode& node = ast_.node(id);
            if (id != region &&
                (node.kind == AstNodeKind::FunctionDeclaration ||
                 node.kind == AstNodeKind::FunctionExpression ||
                 node.kind == AstNodeKind::ArrowFunctionExpression)) continue;
            if (node.kind == AstNodeKind::VariableDeclaration &&
                node.declarationKind == AstDeclarationKind::Var &&
                !collectDeclarationNames(id, names, false)) return false;
            for (std::size_t index = 0; index < node.childCount; ++index)
                pending.push_back(ast_.childAt(id, index));
            const AstNodeId scalarChildren[] = {
                node.name, node.initializer, node.expression, node.argument,
                node.left, node.right, node.test, node.consequent,
                node.alternate, node.init, node.update, node.body, node.callee,
                node.object, node.property, node.key
            };
            for (AstNodeId child : scalarChildren)
                if (child != kInvalidAstNodeId) pending.push_back(child);
        }
        return true;
    }

    bool validateScope(AstNodeId scope, const AstNodeId* parameters,
        std::size_t parameterCount)
    {
        std::vector<BindingNameRef> varNames;
        std::vector<BindingNameRef> directLexicalNames;
        std::vector<ScopeVisit> pending;
        std::vector<bool> visited(ast_.nodeCount(), false);
        const AstNode& scopeNode = ast_.node(scope);
        if (scopeNode.kind != AstNodeKind::Program &&
            scopeNode.kind != AstNodeKind::BlockStatement) {
            fail(ParserErrorCode::InvalidLexicalDeclaration,
                scopeNode.location, TokenType::LeftBrace, true);
            return false;
        }

        for (std::size_t index = 0; index < scopeNode.childCount; ++index) {
            const AstNodeId child = ast_.childAt(scope, index);
            const AstNode& declaration = ast_.node(child);
            if (declaration.kind == AstNodeKind::VariableDeclaration &&
                declaration.declarationKind != AstDeclarationKind::Var) {
                if (!collectDeclarationNames(child, directLexicalNames, true))
                    return false;
            }
        }

        pending.push_back({scope, kInvalidAstNodeId});
        while (!pending.empty()) {
            const ScopeVisit visit = pending.back();
            pending.pop_back();
            if (visit.id >= ast_.nodeCount() || visited[visit.id]) continue;
            visited[visit.id] = true;
            const AstNode& node = ast_.node(visit.id);

            if (visit.id != scope &&
                (node.kind == AstNodeKind::FunctionDeclaration ||
                 node.kind == AstNodeKind::FunctionExpression ||
                 node.kind == AstNodeKind::ArrowFunctionExpression)) {
                if (node.kind == AstNodeKind::FunctionDeclaration &&
                    visit.parent == scope) {
                    if (node.name == kInvalidAstNodeId ||
                        !addName(varNames, node.name,
                            ast_.node(node.name).location, false)) return false;
                }
                continue;
            }

            if (node.kind == AstNodeKind::VariableDeclaration) {
                if (node.declarationKind == AstDeclarationKind::Var) {
                    if (!collectDeclarationNames(visit.id, varNames, false))
                        return false;
                } else {
                    const AstNodeKind parentKind = visit.parent < ast_.nodeCount()
                        ? ast_.node(visit.parent).kind : AstNodeKind::Invalid;
                    if (parentKind != AstNodeKind::Program &&
                        parentKind != AstNodeKind::BlockStatement) {
                        fail(ParserErrorCode::InvalidLexicalDeclaration,
                            node.location, TokenType::LeftBrace, true);
                        return false;
                    }
                    if (visit.parent != scope &&
                        parentKind != AstNodeKind::BlockStatement) return false;
                }
            }
            if (node.kind == AstNodeKind::BlockStatement && visit.id != scope) {
                std::vector<BindingNameRef> blockLexicalNames;
                std::vector<BindingNameRef> blockVarNames;
                for (std::size_t index = 0; index < node.childCount; ++index) {
                    const AstNodeId child = ast_.childAt(visit.id, index);
                    const AstNode& declaration = ast_.node(child);
                    if (declaration.kind == AstNodeKind::VariableDeclaration &&
                        declaration.declarationKind != AstDeclarationKind::Var) {
                        if (!collectDeclarationNames(child, blockLexicalNames,
                                true)) return false;
                    }
                }
                if (!collectVarNamesInRegion(visit.id, blockVarNames))
                    return false;
                for (const BindingNameRef& lexical : blockLexicalNames) {
                    for (const BindingNameRef& variable : blockVarNames) {
                        if (sameNodeName(lexical.name, variable.name)) {
                            fail(ParserErrorCode::DuplicateBinding,
                                lexical.location, TokenType::Identifier, false);
                            return false;
                        }
                    }
                }
            }
            if (node.kind == AstNodeKind::ForOfStatement) {
                if (node.name == kInvalidAstNodeId) {
                    fail(ParserErrorCode::InvalidLexicalDeclaration,
                        node.location, TokenType::Identifier, true);
                    return false;
                }
                std::vector<BindingNameRef> bodyVarNames;
                if (!collectVarNamesInRegion(node.body, bodyVarNames))
                    return false;
                for (const BindingNameRef& variable : bodyVarNames) {
                    if (sameNodeName(node.name, variable.name)) {
                        fail(ParserErrorCode::DuplicateBinding,
                            ast_.node(node.name).location,
                            TokenType::Identifier, false);
                        return false;
                    }
                }
            }

            if (node.kind == AstNodeKind::FunctionDeclaration ||
                node.kind == AstNodeKind::FunctionExpression ||
                node.kind == AstNodeKind::ArrowFunctionExpression) {
                // The scope root itself is not a nested function; every
                // function node encountered below is visited independently.
            }

            for (std::size_t index = 0; index < node.childCount; ++index) {
                const AstNodeId child = ast_.childAt(visit.id, index);
                pending.push_back({child, visit.id});
            }
            const AstNodeId scalarChildren[] = {
                node.name, node.initializer, node.expression, node.argument,
                node.left, node.right, node.test, node.consequent, node.alternate,
                node.init, node.update, node.body, node.callee, node.object,
                node.property, node.key
            };
            for (AstNodeId child : scalarChildren) {
                if (child != kInvalidAstNodeId)
                    pending.push_back({child, visit.id});
            }
        }

        for (const BindingNameRef& lexical : directLexicalNames) {
            for (const BindingNameRef& variable : varNames) {
                if (sameNodeName(lexical.name, variable.name)) {
                    fail(ParserErrorCode::DuplicateBinding,
                        lexical.location, TokenType::Identifier, false);
                    return false;
                }
            }
        }
        for (std::size_t index = 0; index < parameterCount; ++index) {
            for (const BindingNameRef& lexical : directLexicalNames) {
                if (sameNodeName(parameters[index], lexical.name)) {
                    fail(ParserErrorCode::DuplicateBinding,
                        lexical.location, TokenType::Identifier, false);
                    return false;
                }
            }
        }
        return true;
    }

    bool validateLexicalSemantics()
    {
        if (!validateScope(ast_.root(), nullptr, 0u)) return false;
        for (AstNodeId id = 0; id < ast_.nodeCount(); ++id) {
            const AstNode& node = ast_.node(id);
            if (node.kind != AstNodeKind::FunctionDeclaration &&
                node.kind != AstNodeKind::FunctionExpression &&
                node.kind != AstNodeKind::ArrowFunctionExpression) continue;
            if (node.kind == AstNodeKind::ArrowFunctionExpression) {
                for (std::size_t left = 0; left < node.childCount; ++left) {
                    for (std::size_t right = left + 1u;
                        right < node.childCount; ++right) {
                        if (sameNodeName(ast_.childAt(id, left),
                                ast_.childAt(id, right))) {
                            fail(ParserErrorCode::DuplicateParameter,
                                ast_.node(ast_.childAt(id, right)).location,
                                TokenType::Identifier, false);
                            return false;
                        }
                    }
                }
            }
            if (node.body != kInvalidAstNodeId) {
                std::vector<AstNodeId> parameters;
                parameters.reserve(node.childCount);
                for (std::size_t index = 0; index < node.childCount; ++index)
                    parameters.push_back(ast_.childAt(id, index));
                if (!validateScope(node.body, parameters.data(),
                        parameters.size())) return false;
            }
        }
        return true;
    }

    AstNodeId parseArrowFunction()
    {
        const Token start = current();
        std::vector<AstNodeId> parameters;
        bool hasNonSimpleParameters = false;
        if (at(TokenType::Identifier)) {
            const Token parameterToken = advance();
            parameters.push_back(makeLeaf(AstNodeKind::Identifier,
                parameterToken));
        } else {
            expect(TokenType::LeftParen);
            if (!parseFormalParameters(parameters, true, false,
                    hasNonSimpleParameters)) return kInvalidAstNodeId;
            if (!expect(TokenType::RightParen)) return kInvalidAstNodeId;
        }
        if (!expect(TokenType::Arrow)) return kInvalidAstNodeId;

        AstNodeId body = kInvalidAstNodeId;
        AstNodeId expression = kInvalidAstNodeId;
        if (at(TokenType::LeftBrace)) body = parseBlock();
        else expression = parseAssignmentExpression();
        if (body == kInvalidAstNodeId && expression == kInvalidAstNodeId)
            return kInvalidAstNodeId;
        const SourceLocation end = body != kInvalidAstNodeId
            ? ast_.node(body).location : ast_.node(expression).location;
        const AstNodeId function = makeNode(
            AstNodeKind::ArrowFunctionExpression,
            spanLocation(start.location, end));
        if (function == kInvalidAstNodeId) return function;
        ast_.node(function).body = body;
        ast_.node(function).expression = expression;
        if (!setChildren(function, parameters)) {
            fail(ParserErrorCode::AstNodeLimitExceeded, start.location,
                TokenType::EndOfInput, false);
            return kInvalidAstNodeId;
        }
        return function;
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
            if (atIdentifierName()) propertyToken = advanceIdentifierName();
            else if (!expectIdentifier(propertyToken)) return kInvalidAstNodeId;
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
        if (at(TokenType::KeywordFunction)) return parseFunctionExpression();
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
            if (atIdentifierName() || at(TokenType::StringLiteral)) {
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
            if (at(TokenType::Comma)) {
                const Token comma = advance();
                const AstNodeId elision = makeNode(AstNodeKind::ArrayElision,
                    comma.location);
                if (elision == kInvalidAstNodeId) return elision;
                elements.push_back(elision);
                continue;
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
    case ParserErrorCode::DuplicateParameter: return "DuplicateParameter";
    case ParserErrorCode::DuplicateBinding: return "DuplicateBinding";
    case ParserErrorCode::InvalidLexicalDeclaration:
        return "InvalidLexicalDeclaration";
    case ParserErrorCode::AllocationFailure: return "AllocationFailure";
    }
    return "Unknown";
}

} // namespace javascript
} // namespace gxos
