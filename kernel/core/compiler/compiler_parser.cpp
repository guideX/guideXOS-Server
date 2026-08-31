//
// Recursive-descent parser, resolver, and bounded call-graph analysis for the
// bootstrap language.
//

#include "compiler_parser.h"

namespace kernel {
namespace compiler {
namespace {

static CallSite s_callSites[COMPILER_MAX_FUNCTIONS][COMPILER_MAX_CALL_EXPRESSIONS] = {};
static uint16_t s_callArguments[COMPILER_MAX_FUNCTIONS][COMPILER_MAX_CALL_ARGUMENT_NODES] = {};

static bool token_is_name(const Token& token)
{
    return token.kind == TokenKind::Identifier || token.kind == TokenKind::KeywordGxMain;
}

static bool token_is_gx_main(const Token& token)
{
    return token.kind == TokenKind::KeywordGxMain;
}

static uint32_t token_index_or_eof(uint32_t index, uint32_t count)
{
    return index < count ? index : (count == 0 ? 0 : count - 1);
}

static bool token_text_equals(const char* source, const Token& token, const char* text)
{
    if (!source || !text) return false;
    uint32_t length = 0;
    while (text[length]) ++length;
    if (token.length != length) return false;
    for (uint32_t i = 0; i < length; ++i)
        if (source[token.location.offset + i] != text[i]) return false;
    return true;
}

static bool name_equals(const char* left, const char* right)
{
    if (!left || !right) return false;
    uint32_t i = 0;
    while (left[i] || right[i]) {
        if (left[i] != right[i]) return false;
        ++i;
    }
    return true;
}

static uint32_t name_length(const char* name)
{
    uint32_t length = 0;
    if (name) while (name[length]) ++length;
    return length;
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

class FunctionParser {
public:
    FunctionParser(const char* source, const Token* tokens, uint32_t tokenCount,
                   uint32_t* index, FunctionIR* output, const TranslationUnitIR* unit,
                   Diagnostics& diagnostics)
        : m_source(source), m_tokens(tokens), m_tokenCount(tokenCount), m_index(index),
          m_output(output), m_unit(unit), m_diagnostics(diagnostics), m_returnCount(0), m_callDepth(0),
          m_contextToken{}
    {
    }

    bool parse_body()
    {
        uint16_t rootBlock = COMPILER_INVALID_INDEX;
        if (!parse_block(0, 0, 0, &rootBlock)) return false;
        m_output->rootBlock = rootBlock;
        if (!block_guarantees_return(rootBlock)) {
            if (token_text_equals(m_source, m_nameToken, "gx_main")) {
                m_diagnostics.error(m_nameToken.location,
                                    "gx_main may reach end without returning a value", "function");
            } else {
                m_diagnostics.error_identifier_suffix(m_nameToken.location, "function ",
                                                      m_source + m_nameToken.location.offset,
                                                      m_nameToken.length,
                                                      " may reach end without returning a value", "function");
            }
            return false;
        }
        m_output->returnCount = m_returnCount;
        m_output->returnConstantValid = m_returnCount == 1 && evaluate_return_constant();
        if (!m_output->returnConstantValid) m_output->returnConstant = 0;
        return !m_diagnostics.has_error();
    }

    void set_name_token(const Token& token) { m_nameToken = token; }
    void set_context_token(const Token& token) { m_contextToken = token; }

private:
    const Token& current() const
    {
        return m_tokens[token_index_or_eof(*m_index, m_tokenCount)];
    }

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
        ++(*m_index);
        return true;
    }

    bool copy_identifier(char* output, uint32_t capacity, const Token& token)
    {
        if (!output || capacity == 0 || !token_is_name(token) ||
            token.length > COMPILER_MAX_IDENTIFIER_BYTES || token.length + 1 > capacity) {
            m_diagnostics.error(token.location, "identifier exceeds 63-byte limit", "identifier");
            return false;
        }
        for (uint32_t i = 0; i < token.length; ++i) output[i] = m_source[token.location.offset + i];
        output[token.length] = '\0';
        return true;
    }

    bool name_matches(const char* name, const Token& token) const
    {
        if (!name || !token_is_name(token)) return false;
        uint32_t length = 0;
        while (name[length]) ++length;
        if (length != token.length) return false;
        for (uint32_t i = 0; i < length; ++i)
            if (name[i] != m_source[token.location.offset + i]) return false;
        return true;
    }

    int32_t find_integer_parameter(const Token& token) const
    {
        for (uint32_t i = 0; i < m_output->integerParameterCount; ++i) {
            if (name_matches(m_output->parameters[i].name, token))
                return static_cast<int32_t>(m_output->parameters[i].slot);
        }
        return -1;
    }

    bool is_context_parameter(const Token& token) const
    {
        return m_output->usesAppContext && m_contextToken.length != 0 &&
            name_matches(m_output->parameterName, token);
    }

    int32_t find_local(const Token& token) const
    {
        for (uint32_t i = 0; i < m_output->localCount; ++i) {
            if (name_matches(m_output->locals[i].name, token))
                return static_cast<int32_t>(m_output->locals[i].slot);
        }
        return -1;
    }

    int32_t find_variable(const Token& token) const
    {
        const int32_t parameter = find_integer_parameter(token);
        return parameter >= 0 ? parameter : find_local(token);
    }

    int32_t find_global(const Token& token) const
    {
        if (!m_unit || !token_is_name(token)) return -1;
        for (uint32_t i = 0; i < m_unit->globalCount; ++i)
            if (name_matches(m_unit->globals[i].name, token)) return static_cast<int32_t>(i);
        return -1;
    }

    bool name_already_declared(const Token& token) const
    {
        if (is_context_parameter(token) || find_integer_parameter(token) >= 0 || find_local(token) >= 0)
            return true;
        return false;
    }

    bool add_local(const Token& token, uint16_t* slot)
    {
        if (name_already_declared(token)) {
            m_diagnostics.error_identifier(token.location, "duplicate local ",
                                           m_source + token.location.offset, token.length, "identifier");
            return false;
        }
        if (m_output->localCount >= COMPILER_MAX_LOCALS) {
            m_diagnostics.error(token.location, "too many local variables", "identifier");
            return false;
        }
        const uint16_t newSlot = static_cast<uint16_t>(m_output->integerParameterCount + m_output->localCount);
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
                          uint16_t elseBlock = COMPILER_INVALID_INDEX,
                          uint16_t globalIndex = COMPILER_INVALID_INDEX)
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
        statement.globalIndex = globalIndex;
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
        if (current().kind == TokenKind::Identifier) {
            if (m_index && *m_index + 1 < m_tokenCount &&
                m_tokens[*m_index + 1].kind == TokenKind::LeftParen)
                return parse_expression_statement(blockIndex);
            return parse_assignment(blockIndex);
        }
        if (current().kind == TokenKind::KeywordLog) return parse_log(blockIndex);
        if (current().kind == TokenKind::KeywordReturn) return parse_return(blockIndex);
        if (current().kind == TokenKind::KeywordIf)
            return parse_if(blockIndex, depth, conditionalDepth, loopDepth);
        if (current().kind == TokenKind::KeywordWhile)
            return parse_while(blockIndex, depth, conditionalDepth, loopDepth);
        if (current().kind == TokenKind::KeywordBreak)
            return parse_loop_control(blockIndex, loopDepth, StatementKind::Break,
                                      "break", "'break' is only valid inside a loop");
        if (current().kind == TokenKind::KeywordContinue)
            return parse_loop_control(blockIndex, loopDepth, StatementKind::Continue,
                                      "continue", "'continue' is only valid inside a loop");
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

    bool parse_expression_statement(uint16_t blockIndex)
    {
        const SourceLocation location = current().location;
        const uint16_t expression = parse_expression(0);
        if (expression == COMPILER_INVALID_INDEX) return false;
        if (!expect(TokenKind::Semicolon, "expected ';' after expression")) return false;
        return append_statement(blockIndex, StatementKind::EvaluateExpression, location, expression,
                                COMPILER_INVALID_INDEX, COMPILER_INVALID_INDEX);
    }

    bool parse_declaration(uint16_t blockIndex)
    {
        const SourceLocation location = current().location;
        ++(*m_index);
        if (!token_is_name(current()) || current().kind == TokenKind::KeywordGxMain) {
            error_current("expected identifier after 'int'");
            return false;
        }
        const Token name = current();
        uint16_t slot = COMPILER_INVALID_INDEX;
        if (!add_local(name, &slot)) return false;
        ++(*m_index);
        uint16_t expression = COMPILER_INVALID_INDEX;
        if (current().kind == TokenKind::Equal) {
            ++(*m_index);
            expression = parse_expression(0);
            if (expression == COMPILER_INVALID_INDEX) return false;
        } else {
            expression = make_constant(0, name.location);
            if (expression == COMPILER_INVALID_INDEX) return false;
        }
        if (!expect(TokenKind::Semicolon, "expected ';' after declaration")) return false;
        m_output->locals[m_output->localCount - 1U].initialized = true;
        return append_statement(blockIndex, StatementKind::DeclareLocal, location, expression, slot,
                                COMPILER_INVALID_INDEX);
    }

    bool parse_assignment(uint16_t blockIndex)
    {
        const Token name = current();
        const int32_t parameterSlot = find_integer_parameter(name);
        const int32_t slot = parameterSlot >= 0 ? parameterSlot : find_local(name);
        const int32_t global = slot < 0 ? find_global(name) : -1;
        if (slot < 0 && global < 0) {
            if (current().kind == TokenKind::Identifier && m_index && *m_index + 1 < m_tokenCount &&
                m_tokens[*m_index + 1].kind == TokenKind::LeftParen) {
                m_diagnostics.error_identifier(name.location, "function calls must be used as expressions: ",
                                               m_source + name.location.offset, name.length, "call");
            } else {
                m_diagnostics.error_identifier(name.location, "unknown identifier ",
                                               m_source + name.location.offset, name.length, "identifier");
            }
            return false;
        }
        ++(*m_index);
        if (!expect(TokenKind::Equal, "expected '=' in assignment")) return false;
        const uint16_t expression = parse_expression(0);
        if (expression == COMPILER_INVALID_INDEX) return false;
        if (!expect(TokenKind::Semicolon, "expected ';' after assignment")) return false;
        if (slot >= 0) {
            for (uint32_t i = 0; i < m_output->localCount; ++i)
                if (m_output->locals[i].slot == static_cast<uint16_t>(slot)) m_output->locals[i].initialized = true;
            return append_statement(blockIndex, StatementKind::StoreLocal, name.location, expression,
                                    static_cast<uint16_t>(slot), COMPILER_INVALID_INDEX);
        }
        return append_statement(blockIndex, StatementKind::StoreGlobal, name.location, expression,
                                COMPILER_INVALID_INDEX, COMPILER_INVALID_INDEX,
                                COMPILER_INVALID_INDEX, COMPILER_INVALID_INDEX,
                                static_cast<uint16_t>(global));
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
        ++(*m_index);
        if (!expect(TokenKind::LeftParen, "expected '(' after log")) return false;
        if (!token_is_name(current()) || !name_matches(m_output->parameterName, current())) {
            error_current("log must receive the context parameter");
            return false;
        }
        ++(*m_index);
        if (!expect(TokenKind::Comma, "expected ',' between log arguments")) return false;
        if (current().kind != TokenKind::StringLiteral) {
            error_current("expected string literal in log call");
            return false;
        }
        const Token stringToken = current();
        ++(*m_index);
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
        ++(*m_index);
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
        ++(*m_index);
        if (!expect(TokenKind::LeftParen, "expected '(' after 'if'")) return false;
        const uint16_t condition = parse_expression(0);
        if (condition == COMPILER_INVALID_INDEX) return false;
        if (!expect(TokenKind::RightParen, "expected ')' after if condition")) return false;
        uint16_t thenBlock = COMPILER_INVALID_INDEX;
        if (!parse_statement_body(depth, conditionalDepth + 1U, loopDepth, &thenBlock)) return false;
        uint16_t elseBlock = COMPILER_INVALID_INDEX;
        if (current().kind == TokenKind::KeywordElse) {
            ++(*m_index);
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
        ++(*m_index);
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

    bool parse_loop_control(uint16_t blockIndex, uint32_t loopDepth,
                            StatementKind kind, const char* keyword,
                            const char* outsideLoopMessage)
    {
        const SourceLocation location = current().location;
        if (loopDepth == 0) {
            m_diagnostics.error(location, outsideLoopMessage, keyword);
            return false;
        }
        ++(*m_index);
        if (!expect(TokenKind::Semicolon, kind == StatementKind::Break
                    ? "expected ';' after 'break'"
                    : "expected ';' after 'continue'")) return false;
        return append_statement(blockIndex, kind, location,
                                COMPILER_INVALID_INDEX, COMPILER_INVALID_INDEX,
                                COMPILER_INVALID_INDEX);
    }

    uint16_t make_expression(ExpressionKind kind, SourceLocation location,
                             uint16_t left, uint16_t right, uint16_t localIndex, int32_t value,
                             uint16_t callIndex = COMPILER_INVALID_INDEX)
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
        expression.callIndex = callIndex;
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
            ++(*m_index);
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
            ++(*m_index);
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
            ++(*m_index);
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
            ++(*m_index);
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
            ++(*m_index);
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
            ++(*m_index);
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
        ++(*m_index);
        if (current().kind == TokenKind::Integer) {
            const Token literal = current();
            ++(*m_index);
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

    uint16_t parse_call(const Token& nameToken, uint32_t depth)
    {
        if (m_callDepth >= COMPILER_MAX_CALL_NESTING) {
            m_diagnostics.error(nameToken.location, "call nesting limit exceeded", "call");
            return COMPILER_INVALID_INDEX;
        }
        if (m_output->callCount >= COMPILER_MAX_CALL_EXPRESSIONS) {
            m_diagnostics.error(nameToken.location, "too many call expressions", "call");
            return COMPILER_INVALID_INDEX;
        }
        const uint16_t callIndex = static_cast<uint16_t>(m_output->callCount++);
        CallSite& call = m_output->calls[callIndex];
        call = {};
        if (!copy_identifier(call.calleeName, sizeof(call.calleeName), nameToken)) return COMPILER_INVALID_INDEX;
        call.location = nameToken.location;
        call.argumentStart = 0;
        call.argumentCount = 0;
        call.calleeFunction = COMPILER_INVALID_INDEX;
        ++(*m_index);
        if (!expect(TokenKind::LeftParen, "expected '(' after function name")) return COMPILER_INVALID_INDEX;

        ++m_callDepth;
        uint16_t argumentExpressions[COMPILER_MAX_PARAMETERS] = {};
        uint16_t argumentCount = 0;
        if (current().kind != TokenKind::RightParen) {
            while (true) {
                if (argumentCount >= COMPILER_MAX_PARAMETERS) {
                    m_diagnostics.error(nameToken.location,
                                        "function call exceeds four-argument limit", "call");
                    return COMPILER_INVALID_INDEX;
                }
                const uint16_t argument = parse_expression(depth + 1U);
                if (argument == COMPILER_INVALID_INDEX) return COMPILER_INVALID_INDEX;
                argumentExpressions[argumentCount++] = argument;
                if (current().kind != TokenKind::Comma) break;
                ++(*m_index);
            }
        }
        --m_callDepth;
        if (!expect(TokenKind::RightParen, "expected ')' after function arguments")) return COMPILER_INVALID_INDEX;
        if (m_output->callArgumentCount > COMPILER_MAX_CALL_ARGUMENT_NODES - argumentCount) {
            m_diagnostics.error(nameToken.location, "call argument storage capacity exceeded", "call");
            return COMPILER_INVALID_INDEX;
        }
        call.argumentStart = static_cast<uint16_t>(m_output->callArgumentCount);
        call.argumentCount = argumentCount;
        for (uint32_t i = 0; i < argumentCount; ++i)
            m_output->callArguments[m_output->callArgumentCount++] = argumentExpressions[i];
        return make_expression(ExpressionKind::Call, nameToken.location,
                               COMPILER_INVALID_INDEX, COMPILER_INVALID_INDEX,
                               COMPILER_INVALID_INDEX, 0, callIndex);
    }

    uint16_t parse_primary(uint32_t depth)
    {
        const Token token = current();
        if (token.kind == TokenKind::Integer) {
            ++(*m_index);
            int32_t value = 0;
            if (!parse_integer(m_source, token, false, &value, m_diagnostics)) return COMPILER_INVALID_INDEX;
            return make_constant(value, token.location);
        }
        if (token_is_name(token)) {
            const bool call = *m_index + 1U < m_tokenCount && m_tokens[*m_index + 1U].kind == TokenKind::LeftParen;
            if (call) return parse_call(token, depth);
            ++(*m_index);
            const int32_t slot = find_variable(token);
            if (slot < 0) {
                const int32_t global = find_global(token);
                if (global >= 0) {
                    const uint16_t expression = make_expression(ExpressionKind::LoadGlobal, token.location,
                        COMPILER_INVALID_INDEX, COMPILER_INVALID_INDEX,
                        COMPILER_INVALID_INDEX, 0);
                    if (expression == COMPILER_INVALID_INDEX) return expression;
                    m_output->expressions[expression].globalIndex = static_cast<uint16_t>(global);
                    return expression;
                }
                if (is_context_parameter(token))
                    m_diagnostics.error(token.location, "gx_app_context parameter is only valid as log context", "identifier");
                else
                    m_diagnostics.error_identifier(token.location, "unknown identifier ",
                                                   m_source + token.location.offset, token.length, "identifier");
                return COMPILER_INVALID_INDEX;
            }
            bool initialized = false;
            for (uint32_t i = 0; i < m_output->integerParameterCount; ++i)
                if (m_output->parameters[i].slot == static_cast<uint16_t>(slot)) initialized = m_output->parameters[i].initialized;
            for (uint32_t i = 0; i < m_output->localCount; ++i)
                if (m_output->locals[i].slot == static_cast<uint16_t>(slot)) initialized = m_output->locals[i].initialized;
            if (!initialized) {
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
            ++(*m_index);
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
            depth > COMPILER_MAX_EXPRESSION_NESTING + COMPILER_MAX_CALL_NESTING + 2U) return false;
        const Expression& expression = m_output->expressions[index];
        uint32_t left = 0, right = 0;
        if (expression.kind == ExpressionKind::Constant) {
            *bits = static_cast<uint32_t>(expression.value);
            return true;
        }
        if (expression.kind == ExpressionKind::LoadLocal ||
            expression.kind == ExpressionKind::LoadGlobal || expression.kind == ExpressionKind::Call) return false;
        if (expression.kind == ExpressionKind::Negate)
            return evaluate_expression(expression.left, depth + 1U, bits) && (*bits = 0U - *bits, true);
        if (!evaluate_expression(expression.left, depth + 1U, &left)) return false;
        if (expression.kind == ExpressionKind::LogicalAnd) {
            if (left == 0) { *bits = 0; return true; }
            if (!evaluate_expression(expression.right, depth + 1U, &right)) return false;
            *bits = right != 0 ? 1U : 0U;
            return true;
        }
        if (expression.kind == ExpressionKind::LogicalOr) {
            if (left != 0) { *bits = 1U; return true; }
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
    uint32_t* m_index;
    FunctionIR* m_output;
    const TranslationUnitIR* m_unit;
    Diagnostics& m_diagnostics;
    uint32_t m_returnCount;
    uint32_t m_callDepth;
    Token m_nameToken;
    Token m_contextToken;
};

static int32_t find_function(const TranslationUnitIR& unit, const char* name)
{
    for (uint32_t i = 0; i < unit.functionCount; ++i)
        if (name_equals(unit.functionSymbols[i].name, name)) return static_cast<int32_t>(i);
    return -1;
}

static int32_t find_declaration(const TranslationUnitIR& unit, const char* name)
{
    for (uint32_t i = 0; i < unit.declarationCount; ++i)
        if (name_equals(unit.declarations[i].name, name)) return static_cast<int32_t>(i);
    return -1;
}

static int32_t find_global(const TranslationUnitIR& unit, const char* name)
{
    for (uint32_t i = 0; i < unit.globalCount; ++i)
        if (name_equals(unit.globals[i].name, name)) return static_cast<int32_t>(i);
    return -1;
}

static bool report_symbol_kind_conflict(const SourceLocation& location,
                                        const char* name, Diagnostics& diagnostics)
{
    diagnostics.error_identifier_suffix(location, "symbol ", name, name_length(name),
                                        " defined as both function and global", "identifier");
    return false;
}

static bool function_signature_matches(const FunctionIR& function,
                                       uint16_t parameterCount,
                                       bool usesAppContext)
{
    return function.parameterCount == parameterCount &&
        function.usesAppContext == usesAppContext;
}

static bool declaration_signature_matches(const FunctionDeclaration& declaration,
                                          uint16_t parameterCount,
                                          bool usesAppContext)
{
    return declaration.parameterCount == parameterCount &&
        declaration.usesAppContext == usesAppContext;
}

static void classify_recursive_sccs(TranslationUnitIR* unit)
{
    if (!unit) return;
    bool reachable[COMPILER_MAX_FUNCTIONS][COMPILER_MAX_FUNCTIONS] = {};
    for (uint32_t i = 0; i < unit->functionCount; ++i)
        for (uint32_t j = 0; j < unit->functionCount; ++j)
            reachable[i][j] = unit->callGraph[i][j];
    for (uint32_t k = 0; k < unit->functionCount; ++k)
        for (uint32_t i = 0; i < unit->functionCount; ++i)
            for (uint32_t j = 0; j < unit->functionCount; ++j)
                reachable[i][j] = reachable[i][j] || (reachable[i][k] && reachable[k][j]);

    unit->recursiveSccCount = 0;
    for (uint32_t i = 0; i < unit->functionCount; ++i)
        unit->recursiveFunction[i] = reachable[i][i];
    for (uint32_t i = 0; i < unit->functionCount; ++i) {
        if (!unit->recursiveFunction[i]) continue;
        bool firstRepresentative = true;
        for (uint32_t j = 0; j < i; ++j) {
            if (unit->recursiveFunction[j] && reachable[i][j] && reachable[j][i]) {
                firstRepresentative = false;
                break;
            }
        }
        if (firstRepresentative) ++unit->recursiveSccCount;
    }
}

} // namespace

bool parse_translation_unit(const char* source, const Token* tokens, uint32_t tokenCount,
                            TranslationUnitIR* output, Diagnostics& diagnostics,
                            CallSite* callStorage, uint16_t* callArgumentStorage)
{
    if (!source || !tokens || !output || tokenCount == 0 || !callStorage || !callArgumentStorage) {
        const SourceLocation location = {0, 1, 1};
        diagnostics.error(location, "invalid parser input", "parser");
        return false;
    }
    *output = {};
    output->entryFunction = COMPILER_INVALID_INDEX;
    for (uint32_t f = 0; f < COMPILER_MAX_FUNCTIONS; ++f) {
        for (uint32_t c = 0; c < COMPILER_MAX_CALL_EXPRESSIONS; ++c)
            callStorage[f * COMPILER_MAX_CALL_EXPRESSIONS + c] = {};
        for (uint32_t a = 0; a < COMPILER_MAX_CALL_ARGUMENT_NODES; ++a)
            callArgumentStorage[f * COMPILER_MAX_CALL_ARGUMENT_NODES + a] = COMPILER_INVALID_INDEX;
    }
    uint32_t index = 0;
    while (tokens[token_index_or_eof(index, tokenCount)].kind != TokenKind::EndOfFile) {
        const Token externalToken = tokens[token_index_or_eof(index, tokenCount)];
        if (externalToken.kind == TokenKind::Identifier && token_text_equals(source, externalToken, "extern")) {
            ++index;
            const Token intToken = tokens[token_index_or_eof(index, tokenCount)];
            if (intToken.kind != TokenKind::KeywordInt) {
                diagnostics.error(intToken.location, "expected 'int' after 'extern'", "parser");
                return false;
            }
            ++index;
            const Token nameToken = tokens[token_index_or_eof(index, tokenCount)];
            if (!token_is_name(nameToken) || nameToken.kind == TokenKind::KeywordGxMain) {
                diagnostics.error(nameToken.location, "expected identifier after 'extern int'", "global");
                return false;
            }
            char name[COMPILER_FUNCTION_NAME_CAPACITY] = {};
            for (uint32_t i = 0; i < nameToken.length; ++i) name[i] = source[nameToken.location.offset + i];
            name[nameToken.length] = '\0';
            for (uint32_t i = 0; i < output->functionCount; ++i)
                if (token_text_equals(source, nameToken, output->functionSymbols[i].name))
                    return report_symbol_kind_conflict(nameToken.location, name, diagnostics);
            for (uint32_t i = 0; i < output->declarationCount; ++i)
                if (token_text_equals(source, nameToken, output->declarations[i].name))
                    return report_symbol_kind_conflict(nameToken.location, name, diagnostics);
            ++index;
            if (tokens[token_index_or_eof(index, tokenCount)].kind != TokenKind::Semicolon) {
                diagnostics.error(tokens[token_index_or_eof(index, tokenCount)].location,
                                  "expected ';' after extern global declaration", "global");
                return false;
            }
            ++index;
            int32_t globalIndex = find_global(*output, name);
            if (globalIndex < 0) {
                if (output->globalCount >= COMPILER_MAX_GLOBALS) {
                    diagnostics.error(nameToken.location, "global capacity exceeded (maximum is 32)", "global");
                    return false;
                }
                globalIndex = static_cast<int32_t>(output->globalCount++);
                GlobalSymbolIR& global = output->globals[globalIndex];
                global = {};
                for (uint32_t i = 0; i <= nameToken.length; ++i) global.name[i] = name[i];
                global.location = nameToken.location;
                global.size = 4;
                global.alignment = 4;
            }
            continue;
        }
        if (externalToken.kind != TokenKind::KeywordInt) {
            diagnostics.error(tokens[token_index_or_eof(index, tokenCount)].location,
                              "expected external declaration beginning with 'int' or 'extern int'", "parser");
            return false;
        }
        ++index;
        const Token nameToken = tokens[token_index_or_eof(index, tokenCount)];
        if (!token_is_name(nameToken)) {
            diagnostics.error(nameToken.location, "expected identifier after 'int'", "external-declaration");
            return false;
        }
        const Token afterName = tokens[token_index_or_eof(index + 1U, tokenCount)];
        if (afterName.kind != TokenKind::LeftParen) {
            char name[COMPILER_FUNCTION_NAME_CAPACITY] = {};
            if (nameToken.kind == TokenKind::KeywordGxMain ||
                nameToken.length > COMPILER_MAX_IDENTIFIER_BYTES) {
                diagnostics.error(nameToken.location, "global name is not a valid ordinary identifier", "global");
                return false;
            }
            for (uint32_t i = 0; i < nameToken.length; ++i) name[i] = source[nameToken.location.offset + i];
            name[nameToken.length] = '\0';
            for (uint32_t i = 0; i < output->functionCount; ++i)
                if (token_text_equals(source, nameToken, output->functionSymbols[i].name))
                    return report_symbol_kind_conflict(nameToken.location, name, diagnostics);
            for (uint32_t i = 0; i < output->declarationCount; ++i)
                if (token_text_equals(source, nameToken, output->declarations[i].name))
                    return report_symbol_kind_conflict(nameToken.location, name, diagnostics);

            ++index;
            int32_t initialValue = 0;
            bool hasInitializer = false;
            if (tokens[token_index_or_eof(index, tokenCount)].kind == TokenKind::Equal) {
                hasInitializer = true;
                ++index;
                bool negative = false;
                if (tokens[token_index_or_eof(index, tokenCount)].kind == TokenKind::Minus) {
                    negative = true;
                    ++index;
                }
                const Token literal = tokens[token_index_or_eof(index, tokenCount)];
                if (literal.kind != TokenKind::Integer) {
                    diagnostics.error(literal.location, "global initializer must be a constant integer", "global");
                    return false;
                }
                int32_t magnitude = 0;
                if (!parse_integer(source, literal, negative, &magnitude, diagnostics)) return false;
                if (negative && magnitude != static_cast<int32_t>(0x80000000U))
                    initialValue = -magnitude;
                else initialValue = magnitude;
                ++index;
            }
            if (tokens[token_index_or_eof(index, tokenCount)].kind != TokenKind::Semicolon) {
                diagnostics.error(tokens[token_index_or_eof(index, tokenCount)].location,
                                  "global initializer must be a constant integer", "global");
                return false;
            }
            ++index;
            const int32_t existingIndex = find_global(*output, name);
            if (existingIndex >= 0) {
                GlobalSymbolIR& global = output->globals[existingIndex];
                if (global.isDefinition) {
                    diagnostics.error_identifier(nameToken.location, "duplicate definition for global ",
                                                  name, name_length(name), "global");
                    return false;
                }
                global.isDefinition = true;
                global.hasInitializer = hasInitializer;
                global.initialValue = initialValue;
                global.location = nameToken.location;
            } else {
                if (output->globalCount >= COMPILER_MAX_GLOBALS) {
                    diagnostics.error(nameToken.location, "global capacity exceeded (maximum is 32)", "global");
                    return false;
                }
                GlobalSymbolIR& global = output->globals[output->globalCount++];
                global = {};
                for (uint32_t i = 0; i <= nameToken.length; ++i) global.name[i] = name[i];
                global.isDefinition = true;
                global.hasInitializer = hasInitializer;
                global.initialValue = initialValue;
                global.location = nameToken.location;
                global.size = 4;
                global.alignment = 4;
            }
            continue;
        }
        if (output->functionCount >= COMPILER_MAX_FUNCTIONS) {
            diagnostics.error(nameToken.location, "function capacity exceeded (maximum is 16)", "function");
            return false;
        }
        for (uint32_t i = 0; i < output->globalCount; ++i) {
            if (token_text_equals(source, nameToken, output->globals[i].name))
                return report_symbol_kind_conflict(nameToken.location, output->globals[i].name, diagnostics);
        }
        FunctionIR& function = output->functions[output->functionCount];
        function = {};
        function.functionIndex = static_cast<uint16_t>(output->functionCount);
        function.calls = callStorage + output->functionCount * COMPILER_MAX_CALL_EXPRESSIONS;
        function.callArguments = callArgumentStorage + output->functionCount * COMPILER_MAX_CALL_ARGUMENT_NODES;
        function.returnExpression = COMPILER_INVALID_INDEX;
        function.rootBlock = COMPILER_INVALID_INDEX;
        function.codeLabel = COMPILER_INVALID_INDEX;
        function.location = nameToken.location;
        for (uint32_t i = 0; i < output->functionCount; ++i) {
            const FunctionSymbol& existing = output->functionSymbols[i];
            if (token_text_equals(source, nameToken, existing.name)) {
                diagnostics.error_identifier(nameToken.location, "duplicate function ",
                                              source + nameToken.location.offset, nameToken.length, "function");
                return false;
            }
        }
        for (uint32_t i = 0; i < nameToken.length; ++i) function.name[i] = source[nameToken.location.offset + i];
        function.name[nameToken.length] = '\0';
        ++index;

        if (tokens[token_index_or_eof(index, tokenCount)].kind != TokenKind::LeftParen) {
            diagnostics.error(tokens[token_index_or_eof(index, tokenCount)].location,
                              "expected '(' after function name", "function");
            return false;
        }
        ++index;
        FunctionParser parser(source, tokens, tokenCount, &index, &function, output, diagnostics);
        parser.set_name_token(nameToken);
        if (token_is_gx_main(nameToken)) {
            const Token typeToken = tokens[token_index_or_eof(index, tokenCount)];
            const bool legacyVoidPointer = typeToken.kind == TokenKind::KeywordVoid;
            if (!legacyVoidPointer && typeToken.kind != TokenKind::KeywordGxAppContext) {
                diagnostics.error(typeToken.location, "gx_main requires gx_app_context* parameter", "parameter");
                return false;
            }
            ++index;
            if (tokens[token_index_or_eof(index, tokenCount)].kind != TokenKind::Star) {
                diagnostics.error(tokens[token_index_or_eof(index, tokenCount)].location,
                                  "expected '*' in pointer parameter", "parameter");
                return false;
            }
            ++index;
            const Token parameter = tokens[token_index_or_eof(index, tokenCount)];
            if (!token_is_name(parameter) || parameter.kind == TokenKind::KeywordGxMain) {
                diagnostics.error(parameter.location, "expected parameter identifier", "parameter");
                return false;
            }
            for (uint32_t i = 0; i < parameter.length; ++i) function.parameterName[i] = source[parameter.location.offset + i];
            function.parameterName[parameter.length] = '\0';
            function.parameters[0] = {};
            for (uint32_t i = 0; i < parameter.length; ++i) function.parameters[0].name[i] = source[parameter.location.offset + i];
            function.parameters[0].kind = ParameterKind::AppContextPointer;
            function.parameters[0].slot = COMPILER_INVALID_INDEX;
            function.parameters[0].initialized = true;
            function.parameterCount = 1;
            function.usesAppContext = true;
            parser.set_context_token(parameter);
            ++index;
            if (tokens[token_index_or_eof(index, tokenCount)].kind != TokenKind::RightParen) {
                diagnostics.error(tokens[token_index_or_eof(index, tokenCount)].location,
                                  "gx_main accepts exactly one gx_app_context* parameter", "parameter");
                return false;
            }
        } else {
            uint32_t parameterIndex = 0;
            if (tokens[token_index_or_eof(index, tokenCount)].kind != TokenKind::RightParen) {
                while (true) {
                    if (parameterIndex >= COMPILER_MAX_PARAMETERS) {
                        diagnostics.error(tokens[token_index_or_eof(index, tokenCount)].location,
                                          "function parameter limit exceeded (maximum is 4)", "parameter");
                        return false;
                    }
                    if (tokens[token_index_or_eof(index, tokenCount)].kind != TokenKind::KeywordInt) {
                        diagnostics.error(tokens[token_index_or_eof(index, tokenCount)].location,
                                          "ordinary functions accept only int parameters", "parameter");
                        return false;
                    }
                    ++index;
                    const Token parameter = tokens[token_index_or_eof(index, tokenCount)];
                    const bool hasParameterName = token_is_name(parameter) && parameter.kind != TokenKind::KeywordGxMain;
                    if (!hasParameterName && parameter.kind != TokenKind::Comma &&
                        parameter.kind != TokenKind::RightParen) {
                        diagnostics.error(parameter.location, "expected parameter identifier", "parameter");
                        return false;
                    }
                    if (hasParameterName) {
                        for (uint32_t i = 0; i < parameterIndex; ++i) {
                            uint32_t length = 0;
                            while (function.parameters[i].name[length]) ++length;
                            bool same = length == parameter.length;
                            for (uint32_t j = 0; same && j < length; ++j)
                                if (function.parameters[i].name[j] != source[parameter.location.offset + j]) same = false;
                            if (same) {
                                diagnostics.error_identifier(parameter.location, "duplicate parameter ",
                                                              source + parameter.location.offset, parameter.length, "parameter");
                                return false;
                            }
                        }
                        ParameterSymbol& parameterSymbol = function.parameters[parameterIndex];
                        parameterSymbol = {};
                        for (uint32_t i = 0; i < parameter.length; ++i) parameterSymbol.name[i] = source[parameter.location.offset + i];
                        parameterSymbol.kind = ParameterKind::Integer;
                        parameterSymbol.slot = static_cast<uint16_t>(parameterIndex);
                        parameterSymbol.initialized = true;
                        ++index;
                    }
                    ++parameterIndex;
                    if (tokens[token_index_or_eof(index, tokenCount)].kind != TokenKind::Comma) break;
                    ++index;
                }
            }
            function.parameterCount = static_cast<uint16_t>(parameterIndex);
            function.integerParameterCount = static_cast<uint16_t>(parameterIndex);
        }
        if (tokens[token_index_or_eof(index, tokenCount)].kind != TokenKind::RightParen) {
            diagnostics.error(tokens[token_index_or_eof(index, tokenCount)].location,
                              "expected ')' after parameter list", "parameter");
            return false;
        }
        ++index;
        const bool declaration = tokens[token_index_or_eof(index, tokenCount)].kind == TokenKind::Semicolon;
        if (declaration) {
            const int32_t definition = find_function(*output, function.name);
            if (definition >= 0 && !function_signature_matches(output->functions[definition],
                                                                 function.parameterCount,
                                                                 function.usesAppContext)) {
                diagnostics.error_identifier(nameToken.location, "conflicting declaration for function ",
                                              function.name, nameToken.length, "function");
                return false;
            }
            const int32_t existing = find_declaration(*output, function.name);
            if (existing >= 0) {
                if (!declaration_signature_matches(output->declarations[existing], function.parameterCount,
                                                   function.usesAppContext)) {
                    diagnostics.error_identifier(nameToken.location, "conflicting declaration for function ",
                                                  function.name, nameToken.length, "function");
                    return false;
                }
            } else {
                if (output->declarationCount >= COMPILER_MAX_DECLARATIONS) {
                    diagnostics.error(nameToken.location, "declaration capacity exceeded (maximum is 16)", "function");
                    return false;
                }
                FunctionDeclaration& declarationRecord = output->declarations[output->declarationCount++];
                declarationRecord = {};
                for (uint32_t i = 0; i < nameToken.length; ++i) declarationRecord.name[i] = function.name[i];
                declarationRecord.name[nameToken.length] = '\0';
                declarationRecord.parameterCount = function.parameterCount;
                declarationRecord.usesAppContext = function.usesAppContext;
                declarationRecord.location = nameToken.location;
            }
            ++index;
            continue;
        }
        if (find_function(*output, function.name) >= 0) {
            diagnostics.error_identifier(nameToken.location, "duplicate function ",
                                          function.name, nameToken.length, "function");
            return false;
        }
        for (uint32_t p = 0; p < function.parameterCount; ++p) {
            if (function.parameters[p].name[0] == '\0') {
                diagnostics.error(tokens[token_index_or_eof(index, tokenCount)].location,
                                  "function definitions require parameter identifiers", "parameter");
                return false;
            }
        }
        const int32_t declarationIndex = find_declaration(*output, function.name);
        if (declarationIndex >= 0 &&
            !declaration_signature_matches(output->declarations[declarationIndex], function.parameterCount,
                                           function.usesAppContext)) {
            diagnostics.error_identifier(nameToken.location, "conflicting declaration for function ",
                                          function.name, nameToken.length, "function");
            return false;
        }
        FunctionSymbol& symbol = output->functionSymbols[output->functionCount];
        symbol = {};
        for (uint32_t i = 0; i < nameToken.length; ++i) symbol.name[i] = function.name[i];
        symbol.name[nameToken.length] = '\0';
        symbol.functionIndex = static_cast<uint16_t>(output->functionCount);
        symbol.parameterCount = function.parameterCount;
        symbol.location = nameToken.location;
        if (!parser.parse_body()) return false;
        if (token_is_gx_main(nameToken)) output->entryFunction = static_cast<uint16_t>(output->functionCount);
        ++output->functionCount;
    }

    for (uint32_t i = 0; i < output->functionCount; ++i) {
        FunctionIR& function = output->functions[i];
        for (uint32_t j = 0; j < function.callCount; ++j) {
            CallSite& call = function.calls[j];
            const int32_t callee = find_function(*output, call.calleeName);
            const int32_t declaration = find_declaration(*output, call.calleeName);
            uint32_t expected = 0;
            if (callee >= 0) expected = output->functions[callee].parameterCount;
            else if (declaration >= 0) expected = output->declarations[declaration].parameterCount;
            else {
                diagnostics.error_identifier(call.location, "unknown function ", call.calleeName,
                                              name_length(call.calleeName), "call");
                return false;
            }
            if (callee >= 0 && static_cast<uint16_t>(callee) == output->entryFunction) {
                diagnostics.error(call.location, "gx_main is not callable from source", "call");
                return false;
            }
            if (callee < 0 && name_equals(call.calleeName, "gx_main")) {
                diagnostics.error(call.location, "gx_main is not callable from source", "call");
                return false;
            }
            if (expected != call.argumentCount) {
                diagnostics.error_function_argument_count(call.location, call.calleeName,
                                                          name_length(call.calleeName),
                                                          expected, call.argumentCount);
                return false;
            }
            call.expectedParameterCount = static_cast<uint16_t>(expected);
            call.external = callee < 0;
            call.calleeFunction = callee < 0 ? COMPILER_INVALID_INDEX : static_cast<uint16_t>(callee);
            if (callee >= 0 && !output->callGraph[i][callee]) {
                if (output->callGraphEdgeCount >= COMPILER_MAX_CALL_GRAPH_EDGES) {
                    diagnostics.error(call.location, "call graph edge capacity exceeded", "call-graph");
                    return false;
                }
                output->callGraph[i][callee] = true;
                ++output->callGraphEdgeCount;
            }
        }
    }
    // Cycles are legal recursive SCCs in Phase 27M.  The backend instruments
    // every generated source-defined call with the bounded runtime guard.
    classify_recursive_sccs(output);
    return !diagnostics.has_error();
}

bool parse_translation_unit(const char* source, const Token* tokens, uint32_t tokenCount,
                            TranslationUnitIR* output, Diagnostics& diagnostics)
{
    for (uint32_t f = 0; f < COMPILER_MAX_FUNCTIONS; ++f) {
        for (uint32_t c = 0; c < COMPILER_MAX_CALL_EXPRESSIONS; ++c) s_callSites[f][c] = {};
        for (uint32_t a = 0; a < COMPILER_MAX_CALL_ARGUMENT_NODES; ++a) s_callArguments[f][a] = COMPILER_INVALID_INDEX;
    }
    return parse_translation_unit(source, tokens, tokenCount, output, diagnostics,
                                  &s_callSites[0][0], &s_callArguments[0][0]);
}

bool parse_function(const char* source, const Token* tokens, uint32_t tokenCount,
                    FunctionIR* output, Diagnostics& diagnostics)
{
    if (!output) return false;
    static TranslationUnitIR unit = {};
    unit = {};
    if (!parse_translation_unit(source, tokens, tokenCount, &unit, diagnostics)) return false;
    if (unit.functionCount != 1) {
        diagnostics.error(unit.entryFunction < unit.functionCount
                              ? unit.functions[unit.entryFunction].location
                              : (SourceLocation){0, 1, 1},
                          "parse_function accepts only one function; use parse_translation_unit for multiple functions",
                          "parser");
        return false;
    }
    *output = unit.functions[0];
    return true;
}

} // namespace compiler
} // namespace kernel
