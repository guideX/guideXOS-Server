//
// Recursive-descent parser and resolver for the bounded bootstrap language.
//

#include "compiler_parser.h"

namespace kernel {
namespace compiler {
namespace {

static bool same_token_text(const char* source, const Token& left, const Token& right)
{
    if (!source || left.length != right.length) return false;
    for (uint32_t i = 0; i < left.length; ++i)
        if (source[left.location.offset + i] != source[right.location.offset + i]) return false;
    return true;
}

static bool is_identifier_token(const Token& token)
{
    return token.kind == TokenKind::Identifier;
}

static uint32_t token_index_or_eof(uint32_t index, uint32_t count)
{
    return index < count ? index : (count == 0 ? 0 : count - 1);
}

static int32_t bits_to_i32(uint32_t bits)
{
    return static_cast<int32_t>(bits);
}

static bool parse_integer(const char* source, const Token& token, bool allowMin,
                          int32_t* value, Diagnostics& diagnostics)
{
    if (!source || !value) return false;
    const uint64_t limit = allowMin ? 2147483648ULL : 2147483647ULL;
    uint64_t accumulated = 0;
    for (uint32_t i = 0; i < token.length; ++i) {
        const uint32_t digit = static_cast<uint32_t>(source[token.location.offset + i] - '0');
        if (accumulated > (limit - digit) / 10ULL) {
            diagnostics.error(token.location, "integer literal outside signed 32-bit range", "integer-literal");
            return false;
        }
        accumulated = accumulated * 10ULL + digit;
    }
    if (allowMin && accumulated == 2147483648ULL) *value = static_cast<int32_t>(0x80000000U);
    else *value = static_cast<int32_t>(accumulated);
    return true;
}

static bool parse_string_literal(const char* source, const Token& token,
                                 char* output, uint32_t outputCapacity,
                                 uint32_t* outputBytes, Diagnostics& diagnostics)
{
    if (!source || !output || !outputBytes || outputCapacity == 0 || token.length < 2 ||
        token.kind != TokenKind::StringLiteral || source[token.location.offset] != '"' ||
        source[token.location.offset + token.length - 1] != '"') {
        diagnostics.error(token.location, "invalid string literal", "string-literal");
        return false;
    }
    uint32_t written = 0;
    for (uint32_t i = 1; i + 1 < token.length; ++i) {
        char value = source[token.location.offset + i];
        if (value == '\\') {
            ++i;
            if (i + 1 >= token.length) {
                diagnostics.error(token.location, "invalid string escape", "string-literal");
                return false;
            }
            const char escaped = source[token.location.offset + i];
            if (escaped == 'n') value = '\n';
            else if (escaped == '\\') value = '\\';
            else if (escaped == '"') value = '"';
            else {
                diagnostics.error(token.location, "unsupported string escape", "string-literal");
                return false;
            }
        }
        if (written + 1 >= outputCapacity || written >= COMPILER_MAX_STRING_LITERAL_BYTES) {
            diagnostics.error(token.location, "string literal exceeds 255-byte limit", "string-literal");
            return false;
        }
        output[written++] = value;
    }
    output[written] = '\0';
    *outputBytes = written;
    return true;
}

class Parser {
public:
    Parser(const char* source, const Token* tokens, uint32_t tokenCount,
           FunctionIR* output, Diagnostics& diagnostics)
        : m_source(source), m_tokens(tokens), m_tokenCount(tokenCount), m_index(0),
          m_output(output), m_diagnostics(diagnostics), m_returnCount(0), m_parameterToken{}
    {
    }

    bool parse()
    {
        if (!expect(TokenKind::KeywordInt, "expected return type 'int'")) return false;
        if (!expect(TokenKind::KeywordGxMain, "expected function name 'gx_main'")) return false;
        if (!expect(TokenKind::LeftParen, "expected '(' after function name")) return false;
        if (current().kind == TokenKind::KeywordVoid) {
            ++m_index;
        } else if (current().kind == TokenKind::KeywordGxAppContext) {
            m_output->usesAppContext = true;
            ++m_index;
        } else {
            error_current("expected 'void' or 'gx_app_context' parameter type");
            return false;
        }
        if (!expect(TokenKind::Star, "expected '*' in pointer parameter")) return false;
        if (!is_identifier_token(current())) {
            error_current("expected parameter identifier");
            return false;
        }
        m_parameterToken = current();
        if (!copy_identifier(m_output->parameterName, sizeof(m_output->parameterName), m_parameterToken)) return false;
        ++m_index;
        if (!expect(TokenKind::RightParen, "expected ')' after parameter")) return false;

        uint16_t rootBlock = COMPILER_INVALID_INDEX;
        if (!parse_block(0, 0, 0, &rootBlock)) return false;
        m_output->rootBlock = rootBlock;
        if (current().kind != TokenKind::EndOfFile) {
            error_current("expected end of source after function");
            return false;
        }
        if (!block_guarantees_return(rootBlock)) {
            m_diagnostics.error(current().location,
                                "gx_main may reach end without returning a value", "function");
            return false;
        }

        m_output->name[0] = 'g'; m_output->name[1] = 'x'; m_output->name[2] = '_';
        m_output->name[3] = 'm'; m_output->name[4] = 'a'; m_output->name[5] = 'i';
        m_output->name[6] = 'n'; m_output->name[7] = '\0';
        m_output->returnCount = m_returnCount;
        m_output->returnConstantValid = m_returnCount == 1 && evaluate_return_constant();
        if (!m_output->returnConstantValid) m_output->returnConstant = 0;
        return !m_diagnostics.has_error();
    }

private:
    const Token& current() const { return m_tokens[token_index_or_eof(m_index, m_tokenCount)]; }

    void error_current(const char* message)
    {
        m_diagnostics.error(current().location, message, token_kind_name(current().kind));
    }

    bool expect(TokenKind kind, const char* message)
    {
        if (current().kind != kind) {
            error_current(message);
            return false;
        }
        ++m_index;
        return true;
    }

    bool copy_identifier(char* output, uint32_t capacity, const Token& token)
    {
        if (!output || capacity == 0 || token.length > COMPILER_MAX_IDENTIFIER_BYTES || token.length + 1 > capacity) {
            m_diagnostics.error(token.location, "identifier exceeds 63-byte limit", "identifier");
            return false;
        }
        for (uint32_t i = 0; i < token.length; ++i) output[i] = m_source[token.location.offset + i];
        output[token.length] = '\0';
        return true;
    }

    int32_t find_local_text(const Token& token) const
    {
        for (uint32_t i = 0; i < m_output->localCount; ++i) {
            uint32_t length = 0;
            while (m_output->locals[i].name[length]) ++length;
            if (length != token.length) continue;
            bool same = true;
            for (uint32_t j = 0; j < length; ++j)
                if (m_output->locals[i].name[j] != m_source[token.location.offset + j]) same = false;
            if (same) return static_cast<int32_t>(i);
        }
        return -1;
    }

    bool add_local(const Token& token, uint16_t* slot)
    {
        if (find_local_text(token) >= 0) {
            m_diagnostics.error_identifier(token.location, "duplicate local ",
                                           m_source + token.location.offset, token.length, "identifier");
            return false;
        }
        if (m_output->localCount >= COMPILER_MAX_LOCALS) {
            m_diagnostics.error(token.location, "too many local variables", "identifier");
            return false;
        }
        const uint16_t newSlot = static_cast<uint16_t>(m_output->localCount);
        LocalSymbol& local = m_output->locals[m_output->localCount++];
        local = {};
        local.slot = newSlot;
        local.initialized = false;
        if (!copy_identifier(local.name, sizeof(local.name), token)) return false;
        if (slot) *slot = newSlot;
        return true;
    }

    bool create_block(uint32_t depth, uint16_t* blockIndex)
    {
        if (depth > COMPILER_MAX_BLOCK_NESTING) {
            error_current("block nesting limit exceeded");
            return false;
        }
        if (m_output->blockCount >= COMPILER_MAX_BLOCKS) {
            m_diagnostics.error(current().location, "too many blocks", "block");
            return false;
        }
        const uint16_t index = static_cast<uint16_t>(m_output->blockCount++);
        Block& block = m_output->blocks[index];
        block = {};
        block.firstStatement = COMPILER_INVALID_INDEX;
        block.lastStatement = COMPILER_INVALID_INDEX;
        block.depth = static_cast<uint16_t>(depth);
        if (blockIndex) *blockIndex = index;
        return true;
    }

    bool append_statement(uint16_t blockIndex, StatementKind kind, SourceLocation location,
                          uint16_t expression, uint16_t localIndex, uint16_t stringIndex,
                          uint16_t thenBlock = COMPILER_INVALID_INDEX,
                          uint16_t elseBlock = COMPILER_INVALID_INDEX)
    {
        if (blockIndex >= m_output->blockCount) return false;
        if (m_output->statementCount >= COMPILER_MAX_STATEMENTS) {
            m_diagnostics.error(location, "too many statements", "statement");
            return false;
        }
        const uint16_t statementIndex = static_cast<uint16_t>(m_output->statementCount++);
        Statement& statement = m_output->statements[statementIndex];
        statement = {};
        statement.kind = kind;
        statement.expression = expression;
        statement.localIndex = localIndex;
        statement.stringIndex = stringIndex;
        statement.thenBlock = thenBlock;
        statement.elseBlock = elseBlock;
        statement.nextStatement = COMPILER_INVALID_INDEX;
        statement.location = location;
        Block& block = m_output->blocks[blockIndex];
        if (block.firstStatement == COMPILER_INVALID_INDEX) block.firstStatement = statementIndex;
        else m_output->statements[block.lastStatement].nextStatement = statementIndex;
        block.lastStatement = statementIndex;
        return true;
    }

    bool parse_block(uint32_t depth, uint32_t conditionalDepth, uint32_t loopDepth,
                     uint16_t* blockIndex)
    {
        if (!expect(TokenKind::LeftBrace, "expected '{' before block")) return false;
        uint16_t block = COMPILER_INVALID_INDEX;
        if (!create_block(depth, &block)) return false;
        while (current().kind != TokenKind::RightBrace && current().kind != TokenKind::EndOfFile) {
            if (!parse_statement(block, depth, conditionalDepth, loopDepth)) return false;
        }
        if (!expect(TokenKind::RightBrace, "expected '}' after block")) return false;
        if (blockIndex) *blockIndex = block;
        return true;
    }

    bool parse_statement_body(uint32_t depth, uint32_t conditionalDepth, uint32_t loopDepth,
                              uint16_t* blockIndex)
    {
        if (current().kind == TokenKind::LeftBrace)
            return parse_block(depth + 1U, conditionalDepth, loopDepth, blockIndex);
        uint16_t block = COMPILER_INVALID_INDEX;
        if (!create_block(depth + 1U, &block)) return false;
        if (!parse_statement(block, depth + 1U, conditionalDepth, loopDepth)) return false;
        if (blockIndex) *blockIndex = block;
        return true;
    }

    bool parse_statement(uint16_t blockIndex, uint32_t depth, uint32_t conditionalDepth,
                         uint32_t loopDepth)
    {
        if (current().kind == TokenKind::KeywordInt) return parse_declaration(blockIndex);
        if (current().kind == TokenKind::Identifier) return parse_assignment(blockIndex);
        if (current().kind == TokenKind::KeywordLog) return parse_log(blockIndex);
        if (current().kind == TokenKind::KeywordReturn) return parse_return(blockIndex);
        if (current().kind == TokenKind::KeywordIf)
            return parse_if(blockIndex, depth, conditionalDepth, loopDepth);
        if (current().kind == TokenKind::KeywordWhile)
            return parse_while(blockIndex, depth, conditionalDepth, loopDepth);
        if (current().kind == TokenKind::LeftBrace) {
            uint16_t child = COMPILER_INVALID_INDEX;
            const SourceLocation location = current().location;
            if (!parse_block(depth + 1U, conditionalDepth, loopDepth, &child)) return false;
            return append_statement(blockIndex, StatementKind::Block, location,
                                    COMPILER_INVALID_INDEX, COMPILER_INVALID_INDEX,
                                    COMPILER_INVALID_INDEX, child);
        }
        error_current("expected statement");
        return false;
    }

    bool parse_declaration(uint16_t blockIndex)
    {
        const SourceLocation location = current().location;
        ++m_index;
        if (!is_identifier_token(current())) {
            error_current("expected identifier after 'int'");
            return false;
        }
        const Token name = current();
        uint16_t slot = COMPILER_INVALID_INDEX;
        if (!add_local(name, &slot)) return false;
        ++m_index;
        uint16_t expression = COMPILER_INVALID_INDEX;
        if (current().kind == TokenKind::Equal) {
            ++m_index;
            expression = parse_expression(0);
            if (expression == COMPILER_INVALID_INDEX) return false;
        } else {
            expression = make_constant(0, name.location);
            if (expression == COMPILER_INVALID_INDEX) return false;
        }
        if (!expect(TokenKind::Semicolon, "expected ';' after declaration")) return false;
        m_output->locals[slot].initialized = true;
        return append_statement(blockIndex, StatementKind::DeclareLocal, location, expression, slot,
                                COMPILER_INVALID_INDEX);
    }

    bool parse_assignment(uint16_t blockIndex)
    {
        const Token name = current();
        const int32_t slot = find_local_text(name);
        if (slot < 0) {
            m_diagnostics.error_identifier(name.location, "unknown identifier ",
                                           m_source + name.location.offset, name.length, "identifier");
            return false;
        }
        ++m_index;
        if (!expect(TokenKind::Equal, "expected '=' in assignment")) return false;
        const uint16_t expression = parse_expression(0);
        if (expression == COMPILER_INVALID_INDEX) return false;
        if (!expect(TokenKind::Semicolon, "expected ';' after assignment")) return false;
        m_output->locals[slot].initialized = true;
        return append_statement(blockIndex, StatementKind::StoreLocal, name.location, expression,
                                static_cast<uint16_t>(slot), COMPILER_INVALID_INDEX);
    }

    bool add_string(const Token& token, uint16_t* index)
    {
        if (m_output->stringCount >= COMPILER_MAX_STRING_LITERALS) {
            m_diagnostics.error(token.location, "too many string literals", "string-literal");
            return false;
        }
        StringLiteral& literal = m_output->strings[m_output->stringCount];
        uint32_t bytes = 0;
        if (!parse_string_literal(m_source, token, literal.data, sizeof(literal.data), &bytes, m_diagnostics)) return false;
        if (m_output->stringDataBytes + bytes + 1U > COMPILER_MAX_TOTAL_STRING_DATA) {
            m_diagnostics.error(token.location, "total string data exceeds 2048-byte limit", "string-literal");
            return false;
        }
        m_output->stringOffsets[m_output->stringCount] = static_cast<uint16_t>(m_output->stringDataBytes);
        literal.bytes = static_cast<uint16_t>(bytes);
        m_output->stringDataBytes += bytes + 1U;
        if (m_output->stringCount == 0) {
            m_output->logMessageBytes = bytes;
            for (uint32_t i = 0; i <= bytes; ++i) m_output->logMessage[i] = literal.data[i];
        }
        if (index) *index = static_cast<uint16_t>(m_output->stringCount);
        ++m_output->stringCount;
        return true;
    }

    bool parse_log(uint16_t blockIndex)
    {
        const SourceLocation location = current().location;
        if (!m_output->usesAppContext) {
            error_current("host log requires a gx_app_context parameter");
            return false;
        }
        ++m_index;
        if (!expect(TokenKind::LeftParen, "expected '(' after log")) return false;
        if (!is_identifier_token(current()) || !same_token_text(m_source, current(), m_parameterToken)) {
            error_current("log must receive the context parameter");
            return false;
        }
        ++m_index;
        if (!expect(TokenKind::Comma, "expected ',' between log arguments")) return false;
        if (current().kind != TokenKind::StringLiteral) {
            error_current("expected string literal in log call");
            return false;
        }
        const Token stringToken = current();
        ++m_index;
        uint16_t stringIndex = COMPILER_INVALID_INDEX;
        if (!add_string(stringToken, &stringIndex)) return false;
        if (!expect(TokenKind::RightParen, "expected ')' after log arguments")) return false;
        if (!expect(TokenKind::Semicolon, "expected ';' after log call")) return false;
        m_output->hasHostLog = true;
        return append_statement(blockIndex, StatementKind::HostLog, location,
                                COMPILER_INVALID_INDEX, COMPILER_INVALID_INDEX, stringIndex);
    }

    bool parse_return(uint16_t blockIndex)
    {
        const SourceLocation location = current().location;
        ++m_index;
        const uint16_t expression = parse_expression(0);
        if (expression == COMPILER_INVALID_INDEX) return false;
        if (!expect(TokenKind::Semicolon, "expected ';' after return expression")) return false;
        if (!append_statement(blockIndex, StatementKind::Return, location, expression,
                              COMPILER_INVALID_INDEX, COMPILER_INVALID_INDEX)) return false;
        m_output->returnExpression = expression;
        ++m_returnCount;
        return true;
    }

    bool parse_if(uint16_t blockIndex, uint32_t depth, uint32_t conditionalDepth,
                  uint32_t loopDepth)
    {
        if (conditionalDepth >= COMPILER_MAX_CONDITIONAL_NESTING) {
            error_current("conditional nesting limit exceeded");
            return false;
        }
        const SourceLocation location = current().location;
        ++m_index;
        if (!expect(TokenKind::LeftParen, "expected '(' after 'if'")) return false;
        const uint16_t condition = parse_expression(0);
        if (condition == COMPILER_INVALID_INDEX) return false;
        if (!expect(TokenKind::RightParen, "expected ')' after if condition")) return false;
        uint16_t thenBlock = COMPILER_INVALID_INDEX;
        if (!parse_statement_body(depth, conditionalDepth + 1U, loopDepth, &thenBlock)) return false;
        uint16_t elseBlock = COMPILER_INVALID_INDEX;
        if (current().kind == TokenKind::KeywordElse) {
            ++m_index;
            if (!parse_statement_body(depth, conditionalDepth + 1U, loopDepth, &elseBlock)) return false;
        }
        return append_statement(blockIndex, StatementKind::If, location, condition,
                                COMPILER_INVALID_INDEX, COMPILER_INVALID_INDEX,
                                thenBlock, elseBlock);
    }

    bool parse_while(uint16_t blockIndex, uint32_t depth, uint32_t conditionalDepth,
                     uint32_t loopDepth)
    {
        if (loopDepth >= COMPILER_MAX_LOOP_NESTING) {
            error_current("loop nesting limit exceeded");
            return false;
        }
        const SourceLocation location = current().location;
        ++m_index;
        if (!expect(TokenKind::LeftParen, "expected '(' after 'while'")) return false;
        const uint16_t condition = parse_expression(0);
        if (condition == COMPILER_INVALID_INDEX) return false;
        if (!expect(TokenKind::RightParen, "expected ')' after while condition")) return false;
        uint16_t bodyBlock = COMPILER_INVALID_INDEX;
        if (!parse_statement_body(depth, conditionalDepth, loopDepth + 1U, &bodyBlock)) return false;
        return append_statement(blockIndex, StatementKind::While, location, condition,
                                COMPILER_INVALID_INDEX, COMPILER_INVALID_INDEX,
                                bodyBlock, COMPILER_INVALID_INDEX);
    }

    uint16_t make_expression(ExpressionKind kind, SourceLocation location,
                             uint16_t left, uint16_t right, uint16_t localIndex, int32_t value)
    {
        if (m_output->expressionCount >= COMPILER_MAX_EXPRESSION_NODES) {
            m_diagnostics.error(location, "too many expression nodes", "expression");
            return COMPILER_INVALID_INDEX;
        }
        const uint16_t index = static_cast<uint16_t>(m_output->expressionCount++);
        Expression& expression = m_output->expressions[index];
        expression = {};
        expression.kind = kind;
        expression.left = left;
        expression.right = right;
        expression.localIndex = localIndex;
        expression.value = value;
        expression.location = location;
        return index;
    }

    uint16_t make_constant(int32_t value, SourceLocation location)
    {
        return make_expression(ExpressionKind::Constant, location,
                               COMPILER_INVALID_INDEX, COMPILER_INVALID_INDEX,
                               COMPILER_INVALID_INDEX, value);
    }

    uint16_t parse_expression(uint32_t depth)
    {
        if (depth > COMPILER_MAX_EXPRESSION_NESTING) {
            error_current("expression nesting limit exceeded");
            return COMPILER_INVALID_INDEX;
        }
        return parse_logical_or(depth);
    }

    uint16_t parse_logical_or(uint32_t depth)
    {
        uint16_t left = parse_logical_and(depth);
        while (left != COMPILER_INVALID_INDEX && current().kind == TokenKind::LogicalOr) {
            const Token operatorToken = current();
            ++m_index;
            const uint16_t right = parse_logical_and(depth);
            if (right == COMPILER_INVALID_INDEX) return COMPILER_INVALID_INDEX;
            left = make_expression(ExpressionKind::LogicalOr, operatorToken.location,
                                   left, right, COMPILER_INVALID_INDEX, 0);
        }
        return left;
    }

    uint16_t parse_logical_and(uint32_t depth)
    {
        uint16_t left = parse_equality(depth);
        while (left != COMPILER_INVALID_INDEX && current().kind == TokenKind::LogicalAnd) {
            const Token operatorToken = current();
            ++m_index;
            const uint16_t right = parse_equality(depth);
            if (right == COMPILER_INVALID_INDEX) return COMPILER_INVALID_INDEX;
            left = make_expression(ExpressionKind::LogicalAnd, operatorToken.location,
                                   left, right, COMPILER_INVALID_INDEX, 0);
        }
        return left;
    }

    uint16_t parse_equality(uint32_t depth)
    {
        uint16_t left = parse_relational(depth);
        while (left != COMPILER_INVALID_INDEX &&
               (current().kind == TokenKind::EqualEqual || current().kind == TokenKind::NotEqual)) {
            const Token operatorToken = current();
            const TokenKind operation = current().kind;
            ++m_index;
            const uint16_t right = parse_relational(depth);
            if (right == COMPILER_INVALID_INDEX) return COMPILER_INVALID_INDEX;
            left = make_expression(operation == TokenKind::EqualEqual ? ExpressionKind::Equal : ExpressionKind::NotEqual,
                                   operatorToken.location, left, right, COMPILER_INVALID_INDEX, 0);
        }
        return left;
    }

    uint16_t parse_relational(uint32_t depth)
    {
        uint16_t left = parse_additive(depth);
        while (left != COMPILER_INVALID_INDEX &&
               (current().kind == TokenKind::Less || current().kind == TokenKind::LessEqual ||
                current().kind == TokenKind::Greater || current().kind == TokenKind::GreaterEqual)) {
            const Token operatorToken = current();
            const TokenKind operation = current().kind;
            ++m_index;
            const uint16_t right = parse_additive(depth);
            if (right == COMPILER_INVALID_INDEX) return COMPILER_INVALID_INDEX;
            ExpressionKind kind = ExpressionKind::Less;
            if (operation == TokenKind::LessEqual) kind = ExpressionKind::LessEqual;
            else if (operation == TokenKind::Greater) kind = ExpressionKind::Greater;
            else if (operation == TokenKind::GreaterEqual) kind = ExpressionKind::GreaterEqual;
            left = make_expression(kind, operatorToken.location, left, right,
                                   COMPILER_INVALID_INDEX, 0);
        }
        return left;
    }

    uint16_t parse_additive(uint32_t depth)
    {
        uint16_t left = parse_multiplicative(depth);
        while (left != COMPILER_INVALID_INDEX &&
               (current().kind == TokenKind::Plus || current().kind == TokenKind::Minus)) {
            const Token operatorToken = current();
            const TokenKind operation = current().kind;
            ++m_index;
            const uint16_t right = parse_multiplicative(depth);
            if (right == COMPILER_INVALID_INDEX) return COMPILER_INVALID_INDEX;
            left = make_expression(operation == TokenKind::Plus ? ExpressionKind::Add : ExpressionKind::Subtract,
                                   operatorToken.location, left, right, COMPILER_INVALID_INDEX, 0);
        }
        return left;
    }

    uint16_t parse_multiplicative(uint32_t depth)
    {
        uint16_t left = parse_unary(depth);
        while (left != COMPILER_INVALID_INDEX && current().kind == TokenKind::Star) {
            const Token operatorToken = current();
            ++m_index;
            const uint16_t right = parse_unary(depth);
            if (right == COMPILER_INVALID_INDEX) return COMPILER_INVALID_INDEX;
            left = make_expression(ExpressionKind::Multiply, operatorToken.location,
                                   left, right, COMPILER_INVALID_INDEX, 0);
        }
        return left;
    }

    uint16_t parse_unary(uint32_t depth)
    {
        if (current().kind != TokenKind::Minus) return parse_primary(depth);
        const SourceLocation location = current().location;
        ++m_index;
        if (current().kind == TokenKind::Integer) {
            const Token literal = current();
            ++m_index;
            int32_t magnitude = 0;
            if (!parse_integer(m_source, literal, true, &magnitude, m_diagnostics)) return COMPILER_INVALID_INDEX;
            if (magnitude == static_cast<int32_t>(0x80000000U)) return make_constant(magnitude, location);
            const uint16_t constant = make_constant(magnitude, literal.location);
            return constant == COMPILER_INVALID_INDEX ? constant :
                make_expression(ExpressionKind::Negate, location, constant,
                                COMPILER_INVALID_INDEX, COMPILER_INVALID_INDEX, 0);
        }
        const uint16_t child = parse_unary(depth + 1U);
        if (child == COMPILER_INVALID_INDEX) return child;
        return make_expression(ExpressionKind::Negate, location, child,
                               COMPILER_INVALID_INDEX, COMPILER_INVALID_INDEX, 0);
    }

    uint16_t parse_primary(uint32_t depth)
    {
        const Token token = current();
        if (token.kind == TokenKind::Integer) {
            ++m_index;
            int32_t value = 0;
            if (!parse_integer(m_source, token, false, &value, m_diagnostics)) return COMPILER_INVALID_INDEX;
            return make_constant(value, token.location);
        }
        if (token.kind == TokenKind::Identifier) {
            ++m_index;
            const int32_t slot = find_local_text(token);
            if (slot < 0) {
                m_diagnostics.error_identifier(token.location, "unknown identifier ",
                                               m_source + token.location.offset, token.length, "identifier");
                return COMPILER_INVALID_INDEX;
            }
            if (!m_output->locals[slot].initialized) {
                m_diagnostics.error(token.location, "local used before initialization", "identifier");
                return COMPILER_INVALID_INDEX;
            }
            return make_expression(ExpressionKind::LoadLocal, token.location,
                                   COMPILER_INVALID_INDEX, COMPILER_INVALID_INDEX,
                                   static_cast<uint16_t>(slot), 0);
        }
        if (token.kind == TokenKind::LeftParen) {
            if (depth >= COMPILER_MAX_EXPRESSION_NESTING) {
                m_diagnostics.error(token.location, "expression nesting limit exceeded", "'('");
                return COMPILER_INVALID_INDEX;
            }
            ++m_index;
            const uint16_t expression = parse_expression(depth + 1U);
            if (expression == COMPILER_INVALID_INDEX) return expression;
            if (!expect(TokenKind::RightParen, "expected ')' after expression")) return COMPILER_INVALID_INDEX;
            return expression;
        }
        error_current("expected expression");
        return COMPILER_INVALID_INDEX;
    }

    bool evaluate_expression(uint16_t index, uint32_t depth, uint32_t* bits) const
    {
        if (!bits || index == COMPILER_INVALID_INDEX || index >= m_output->expressionCount ||
            depth > COMPILER_MAX_EXPRESSION_NESTING + 2U) return false;
        const Expression& expression = m_output->expressions[index];
        uint32_t left = 0, right = 0;
        if (expression.kind == ExpressionKind::Constant) {
            *bits = static_cast<uint32_t>(expression.value);
            return true;
        }
        if (expression.kind == ExpressionKind::LoadLocal) return false;
        if (expression.kind == ExpressionKind::Negate)
            return evaluate_expression(expression.left, depth + 1U, bits) && (*bits = 0U - *bits, true);
        if (!evaluate_expression(expression.left, depth + 1U, &left)) return false;
        if (expression.kind == ExpressionKind::LogicalAnd) {
            if (left == 0) {
                *bits = 0;
                return true;
            }
            if (!evaluate_expression(expression.right, depth + 1U, &right)) return false;
            *bits = right != 0 ? 1U : 0U;
            return true;
        }
        if (expression.kind == ExpressionKind::LogicalOr) {
            if (left != 0) {
                *bits = 1U;
                return true;
            }
            if (!evaluate_expression(expression.right, depth + 1U, &right)) return false;
            *bits = right != 0 ? 1U : 0U;
            return true;
        }
        if (!evaluate_expression(expression.right, depth + 1U, &right)) return false;
        switch (expression.kind) {
            case ExpressionKind::Add: *bits = left + right; return true;
            case ExpressionKind::Subtract: *bits = left - right; return true;
            case ExpressionKind::Multiply: *bits = left * right; return true;
            case ExpressionKind::Equal: *bits = bits_to_i32(left) == bits_to_i32(right) ? 1U : 0U; return true;
            case ExpressionKind::NotEqual: *bits = bits_to_i32(left) != bits_to_i32(right) ? 1U : 0U; return true;
            case ExpressionKind::Less: *bits = bits_to_i32(left) < bits_to_i32(right) ? 1U : 0U; return true;
            case ExpressionKind::LessEqual: *bits = bits_to_i32(left) <= bits_to_i32(right) ? 1U : 0U; return true;
            case ExpressionKind::Greater: *bits = bits_to_i32(left) > bits_to_i32(right) ? 1U : 0U; return true;
            case ExpressionKind::GreaterEqual: *bits = bits_to_i32(left) >= bits_to_i32(right) ? 1U : 0U; return true;
            default: return false;
        }
    }

    bool evaluate_return_constant()
    {
        uint32_t bits = 0;
        if (!evaluate_expression(m_output->returnExpression, 0, &bits)) return false;
        m_output->returnConstant = bits_to_i32(bits);
        return true;
    }

    bool block_guarantees_return(uint16_t blockIndex) const
    {
        if (blockIndex == COMPILER_INVALID_INDEX || blockIndex >= m_output->blockCount) return false;
        const Block& block = m_output->blocks[blockIndex];
        uint16_t statementIndex = block.firstStatement;
        uint32_t visited = 0;
        while (statementIndex != COMPILER_INVALID_INDEX && visited++ < COMPILER_MAX_STATEMENTS) {
            const Statement& statement = m_output->statements[statementIndex];
            if (statement.kind == StatementKind::Return) return true;
            if (statement.kind == StatementKind::Block && block_guarantees_return(statement.thenBlock)) return true;
            if (statement.kind == StatementKind::If && statement.elseBlock != COMPILER_INVALID_INDEX &&
                block_guarantees_return(statement.thenBlock) && block_guarantees_return(statement.elseBlock)) return true;
            statementIndex = statement.nextStatement;
        }
        return false;
    }

    const char* m_source;
    const Token* m_tokens;
    uint32_t m_tokenCount;
    uint32_t m_index;
    FunctionIR* m_output;
    Diagnostics& m_diagnostics;
    uint32_t m_returnCount;
    Token m_parameterToken;
};

} // namespace

bool parse_function(const char* source, const Token* tokens, uint32_t tokenCount,
                    FunctionIR* output, Diagnostics& diagnostics)
{
    if (!source || !tokens || !output || tokenCount == 0) {
        const SourceLocation location = {0, 1, 1};
        diagnostics.error(location, "invalid parser input", "parser");
        return false;
    }
    *output = {};
    output->returnExpression = COMPILER_INVALID_INDEX;
    output->rootBlock = COMPILER_INVALID_INDEX;
    Parser parser(source, tokens, tokenCount, output, diagnostics);
    return parser.parse();
}

} // namespace compiler
} // namespace kernel
