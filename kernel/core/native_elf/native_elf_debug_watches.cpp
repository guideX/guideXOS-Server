#include "native_elf_debug_watches.h"

namespace kernel {
namespace native_elf {
namespace {

static const uint16_t kInvalidIndex = 0xffffu;
static const int64_t kInt64Min = (-9223372036854775807LL - 1LL);
static const int64_t kInt64Max = 9223372036854775807LL;
static const uint64_t kInt64MinMagnitude = 0x8000000000000000ULL;

enum class TokenKind : uint8_t {
    Identifier, Integer, LeftParen, RightParen, Plus, Minus, Star, Slash, Percent, End
};

enum class NodeKind : uint8_t { Integer, Identifier, Unary, Binary };
enum class Operator : uint8_t { Positive, Negative, Add, Subtract, Multiply, Divide, Modulo };

struct Token {
    TokenKind kind;
    uint32_t offset;
    uint32_t length;
    uint64_t integerValue;
};

struct Node {
    NodeKind kind;
    Operator operation;
    uint16_t left;
    uint16_t right;
    uint32_t offset;
    uint64_t integerValue;
    char identifier[NATIVE_DEBUG_WATCH_MAX_IDENTIFIER_BYTES];
};

struct Ast {
    Token tokens[NATIVE_DEBUG_WATCH_MAX_TOKENS];
    Node nodes[NATIVE_DEBUG_WATCH_MAX_AST_NODES];
    uint32_t tokenCount;
    uint32_t nodeCount;
    uint32_t operatorCount;
};

struct Value {
    NativeDebugWatchValueType type;
    int64_t signedValue;
    uint64_t unsignedValue;
};

static uint32_t text_length(const char* text, uint32_t capacity)
{
    if (!text) return 0;
    uint32_t length = 0;
    while (length < capacity && text[length] != '\0') ++length;
    return length;
}

static void copy_text(char* destination, uint32_t capacity, const char* source)
{
    if (!destination || capacity == 0) return;
    uint32_t index = 0;
    if (source) {
        while (index + 1 < capacity && source[index] != '\0') {
            destination[index] = source[index];
            ++index;
        }
    }
    destination[index] = '\0';
}

static bool text_equals(const char* left, const char* right)
{
    if (!left || !right) return left == right;
    uint32_t index = 0;
    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) return false;
        ++index;
    }
    return left[index] == right[index];
}

static bool identifier_start(char value)
{
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || value == '_';
}

static bool identifier_part(char value)
{
    return identifier_start(value) || (value >= '0' && value <= '9');
}

static bool hex_digit(char value, uint64_t* digit)
{
    if (value >= '0' && value <= '9') {
        if (digit) *digit = static_cast<uint64_t>(value - '0');
        return true;
    }
    if (value >= 'a' && value <= 'f') {
        if (digit) *digit = static_cast<uint64_t>(value - 'a' + 10);
        return true;
    }
    if (value >= 'A' && value <= 'F') {
        if (digit) *digit = static_cast<uint64_t>(value - 'A' + 10);
        return true;
    }
    return false;
}

static void set_failure(NativeDebugWatchResult* result, NativeDebugWatchStatus status,
                        const char* diagnostic, uint32_t offset = 0)
{
    if (!result) return;
    result->status = status;
    result->type = NativeDebugWatchValueType::SignedInt32;
    result->rawValue = 0;
    result->signedValue = 0;
    result->unsignedValue = 0;
    result->diagnosticOffset = offset;
    copy_text(result->diagnostic, sizeof(result->diagnostic), diagnostic);
    result->formatted[0] = '\0';
}

static bool push_token(Ast* ast, TokenKind kind, uint32_t offset, uint32_t length,
                       uint64_t integerValue, NativeDebugWatchResult* result)
{
    if (!ast || !result || ast->tokenCount >= NATIVE_DEBUG_WATCH_MAX_TOKENS) {
        set_failure(result, NativeDebugWatchStatus::TooComplex, "token limit exceeded", offset);
        return false;
    }
    ast->tokens[ast->tokenCount++] = {kind, offset, length, integerValue};
    return true;
}

static bool tokenize(const char* expression, uint32_t length, Ast* ast,
                     NativeDebugWatchResult* result)
{
    uint32_t offset = 0;
    while (offset < length) {
        const char value = expression[offset];
        if (value == ' ' || value == '\t' || value == '\r' || value == '\n') {
            ++offset;
            continue;
        }
        if (identifier_start(value)) {
            const uint32_t start = offset++;
            while (offset < length && identifier_part(expression[offset])) ++offset;
            if (offset - start >= NATIVE_DEBUG_WATCH_MAX_IDENTIFIER_BYTES) {
                set_failure(result, NativeDebugWatchStatus::SyntaxError,
                            "identifier is too long", start);
                return false;
            }
            if (!push_token(ast, TokenKind::Identifier, start, offset - start, 0, result))
                return false;
            continue;
        }
        if (value >= '0' && value <= '9') {
            const uint32_t start = offset;
            const bool hexadecimal = value == '0' && offset + 1 < length &&
                (expression[offset + 1] == 'x' || expression[offset + 1] == 'X');
            if (hexadecimal) offset += 2;
            uint32_t digits = 0;
            uint64_t parsed = 0;
            while (offset < length) {
                uint64_t digit = 0;
                const bool valid = hexadecimal
                    ? hex_digit(expression[offset], &digit)
                    : (expression[offset] >= '0' && expression[offset] <= '9' &&
                       (digit = static_cast<uint64_t>(expression[offset] - '0'), true));
                if (!valid) break;
                const uint64_t base = hexadecimal ? 16ULL : 10ULL;
                if (parsed > (~static_cast<uint64_t>(0) - digit) / base) {
                    set_failure(result, NativeDebugWatchStatus::SyntaxError,
                                "integer literal overflows", start);
                    return false;
                }
                parsed = parsed * base + digit;
                ++digits;
                ++offset;
            }
            if (digits == 0) {
                set_failure(result, NativeDebugWatchStatus::SyntaxError,
                            "malformed hexadecimal literal", start);
                return false;
            }
            if (offset < length && identifier_start(expression[offset])) {
                set_failure(result, NativeDebugWatchStatus::SyntaxError,
                            "malformed integer literal", offset);
                return false;
            }
            if (!push_token(ast, TokenKind::Integer, start, offset - start, parsed, result))
                return false;
            continue;
        }

        TokenKind kind = TokenKind::End;
        switch (value) {
        case '(': kind = TokenKind::LeftParen; break;
        case ')': kind = TokenKind::RightParen; break;
        case '+': kind = TokenKind::Plus; break;
        case '-': kind = TokenKind::Minus; break;
        case '*': kind = TokenKind::Star; break;
        case '/': kind = TokenKind::Slash; break;
        case '%': kind = TokenKind::Percent; break;
        default:
            set_failure(result, NativeDebugWatchStatus::UnsupportedOperator,
                        "operator or token is not supported", offset);
            return false;
        }
        if (!push_token(ast, kind, offset, 1, 0, result)) return false;
        ++offset;
    }
    return push_token(ast, TokenKind::End, length, 0, 0, result);
}

class Parser {
public:
    Parser(const char* source, Ast* ast, NativeDebugWatchResult* result)
        : source_(source), ast_(ast), result_(result), cursor_(0), depth_(0) {}

    uint16_t parse()
    {
        const uint16_t root = parse_expression(0);
        if (root == kInvalidIndex) return kInvalidIndex;
        if (current().kind != TokenKind::End) {
            fail(current().kind == TokenKind::LeftParen
                     ? NativeDebugWatchStatus::UnsupportedOperator
                     : NativeDebugWatchStatus::SyntaxError,
                 current().kind == TokenKind::LeftParen
                     ? "function calls are not supported" : "unexpected trailing token");
            return kInvalidIndex;
        }
        return root;
    }

private:
    const Token& current() const { return ast_->tokens[cursor_]; }

    bool fail(NativeDebugWatchStatus status, const char* message)
    {
        set_failure(result_, status, message, current().offset);
        return false;
    }

    bool accept(TokenKind kind)
    {
        if (current().kind != kind) return false;
        ++cursor_;
        return true;
    }

    uint16_t add_node(NodeKind kind, Operator operation, const Token& token,
                      uint16_t left, uint16_t right, uint64_t integerValue,
                      const char* identifier)
    {
        if (ast_->nodeCount >= NATIVE_DEBUG_WATCH_MAX_AST_NODES) {
            fail(NativeDebugWatchStatus::TooComplex, "AST node limit exceeded");
            return kInvalidIndex;
        }
        Node& node = ast_->nodes[ast_->nodeCount];
        node = {};
        node.kind = kind;
        node.operation = operation;
        node.left = left;
        node.right = right;
        node.offset = token.offset;
        node.integerValue = integerValue;
        if (identifier) copy_text(node.identifier, sizeof(node.identifier), identifier);
        return static_cast<uint16_t>(ast_->nodeCount++);
    }

    static bool binary_operator(TokenKind kind, Operator* operation, uint32_t* precedence)
    {
        if (!operation || !precedence) return false;
        switch (kind) {
        case TokenKind::Plus: *operation = Operator::Add; *precedence = 1; return true;
        case TokenKind::Minus: *operation = Operator::Subtract; *precedence = 1; return true;
        case TokenKind::Star: *operation = Operator::Multiply; *precedence = 2; return true;
        case TokenKind::Slash: *operation = Operator::Divide; *precedence = 2; return true;
        case TokenKind::Percent: *operation = Operator::Modulo; *precedence = 2; return true;
        default: return false;
        }
    }

    uint16_t parse_expression(uint32_t minimumPrecedence)
    {
        if (++depth_ > NATIVE_DEBUG_WATCH_MAX_PARSE_DEPTH) {
            fail(NativeDebugWatchStatus::TooComplex, "parse-depth limit exceeded");
            --depth_;
            return kInvalidIndex;
        }
        uint16_t left = parse_unary();
        if (left == kInvalidIndex) { --depth_; return kInvalidIndex; }
        while (true) {
            Operator operation = Operator::Add;
            uint32_t precedence = 0;
            if (!binary_operator(current().kind, &operation, &precedence) ||
                precedence < minimumPrecedence) break;
            const Token operatorToken = current();
            ++cursor_;
            if (++ast_->operatorCount > NATIVE_DEBUG_WATCH_MAX_OPERATORS) {
                fail(NativeDebugWatchStatus::TooComplex, "operator limit exceeded");
                --depth_;
                return kInvalidIndex;
            }
            const uint16_t right = parse_expression(precedence + 1);
            if (right == kInvalidIndex) { --depth_; return kInvalidIndex; }
            left = add_node(NodeKind::Binary, operation, operatorToken, left, right, 0, nullptr);
            if (left == kInvalidIndex) { --depth_; return kInvalidIndex; }
        }
        --depth_;
        return left;
    }

    uint16_t parse_unary()
    {
        const Token token = current();
        Operator operation = Operator::Positive;
        bool unary = true;
        if (token.kind == TokenKind::Plus) operation = Operator::Positive;
        else if (token.kind == TokenKind::Minus) operation = Operator::Negative;
        else unary = false;
        if (!unary) return parse_primary();
        ++cursor_;
        if (++ast_->operatorCount > NATIVE_DEBUG_WATCH_MAX_OPERATORS) {
            fail(NativeDebugWatchStatus::TooComplex, "operator limit exceeded");
            return kInvalidIndex;
        }
        const uint16_t child = parse_unary();
        if (child == kInvalidIndex) return kInvalidIndex;
        return add_node(NodeKind::Unary, operation, token, child, kInvalidIndex, 0, nullptr);
    }

    uint16_t parse_primary()
    {
        const Token token = current();
        if (accept(TokenKind::Integer))
            return add_node(NodeKind::Integer, Operator::Positive, token, kInvalidIndex,
                            kInvalidIndex, token.integerValue, nullptr);
        if (accept(TokenKind::Identifier)) {
            char identifier[NATIVE_DEBUG_WATCH_MAX_IDENTIFIER_BYTES] = {};
            for (uint32_t i = 0; i < token.length && i + 1 < sizeof(identifier); ++i)
                identifier[i] = source_[token.offset + i];
            return add_node(NodeKind::Identifier, Operator::Positive, token, kInvalidIndex,
                            kInvalidIndex, 0, identifier);
        }
        if (accept(TokenKind::LeftParen)) {
            const uint16_t expression = parse_expression(0);
            if (expression == kInvalidIndex) return kInvalidIndex;
            if (!accept(TokenKind::RightParen)) {
                fail(NativeDebugWatchStatus::SyntaxError, "expected ')' ");
                return kInvalidIndex;
            }
            return expression;
        }
        fail(token.kind == TokenKind::Star
                 ? NativeDebugWatchStatus::UnsupportedOperator
                 : NativeDebugWatchStatus::SyntaxError,
             token.kind == TokenKind::Star
                 ? "pointer dereference is not supported" : "expected value");
        return kInvalidIndex;
    }

    const char* source_;
    Ast* ast_;
    NativeDebugWatchResult* result_;
    uint32_t cursor_;
    uint32_t depth_;
};

static Value make_integer(int64_t value)
{
    Value output = {};
    output.type = NativeDebugWatchValueType::SignedInt32;
    output.signedValue = value;
    output.unsignedValue = static_cast<uint64_t>(value);
    return output;
}

static Value make_pointer(uint64_t value)
{
    Value output = {};
    output.type = NativeDebugWatchValueType::Pointer;
    output.unsignedValue = value;
    return output;
}

static bool is_integer(const Value& value)
{
    return value.type == NativeDebugWatchValueType::SignedInt32;
}

static bool checked_add(int64_t left, int64_t right, int64_t* output)
{
    if ((right > 0 && left > kInt64Max - right) ||
        (right < 0 && left < kInt64Min - right)) return false;
    *output = left + right;
    return true;
}

static bool checked_subtract(int64_t left, int64_t right, int64_t* output)
{
    if ((right < 0 && left > kInt64Max + right) ||
        (right > 0 && left < kInt64Min + right)) return false;
    *output = left - right;
    return true;
}

static bool checked_multiply(int64_t left, int64_t right, int64_t* output)
{
    if (left == 0 || right == 0) { *output = 0; return true; }
    if (left == -1 && right == kInt64Min) return false;
    if (right == -1 && left == kInt64Min) return false;
    if (left > 0) {
        if (right > 0 && left > kInt64Max / right) return false;
        if (right < 0 && right < kInt64Min / left) return false;
    } else {
        if (right > 0 && left < kInt64Min / right) return false;
        if (right < 0 && left < kInt64Max / right) return false;
    }
    *output = left * right;
    return true;
}

static bool resolve_identifier(const Node& node, const NativeDebugWatchFrame& frame,
                               Value* value, NativeDebugWatchResult* result)
{
    const gx_development_debug_variables* variables = frame.variables;
    if (!variables || variables->size < sizeof(gx_development_debug_variables) ||
        variables->variableCount > GX_DEVELOPMENT_DEBUG_MAX_VARIABLES) {
        set_failure(result, NativeDebugWatchStatus::InvalidSelectedFrame,
                    "selected variable snapshot is invalid", node.offset);
        return false;
    }
    const gx_development_debug_variable* found = nullptr;
    for (uint32_t index = 0; index < variables->variableCount; ++index) {
        const gx_development_debug_variable& variable = variables->variables[index];
        if (text_equals(node.identifier, variable.name)) { found = &variable; break; }
    }
    if (found) {
        if (found->availability != GX_DEVELOPMENT_DEBUG_VARIABLE_AVAILABILITY_AVAILABLE ||
            (found->flags & (GX_DEVELOPMENT_DEBUG_VARIABLE_VALIDATED |
                             GX_DEVELOPMENT_DEBUG_VARIABLE_LIVE |
                             GX_DEVELOPMENT_DEBUG_VARIABLE_VALUE_VALID)) !=
                (GX_DEVELOPMENT_DEBUG_VARIABLE_VALIDATED |
                 GX_DEVELOPMENT_DEBUG_VARIABLE_LIVE |
                 GX_DEVELOPMENT_DEBUG_VARIABLE_VALUE_VALID)) {
            set_failure(result, NativeDebugWatchStatus::NotLive,
                        "variable is not live in selected frame", node.offset);
            return false;
        }
        if (found->type == GX_DEVELOPMENT_DEBUG_VARIABLE_TYPE_SIGNED_INT32) {
            *value = make_integer(found->signedValue);
            return true;
        }
        if (found->type == GX_DEVELOPMENT_DEBUG_VARIABLE_TYPE_POINTER) {
            *value = make_pointer(found->rawValue);
            return true;
        }
        set_failure(result, NativeDebugWatchStatus::UnsupportedType,
                    "variable type is not supported by watch evaluator", node.offset);
        return false;
    }
    if (frame.resolveIdentifier) {
        const NativeDebugWatchResolveStatus status =
            frame.resolveIdentifier(frame.resolverContext, node.identifier);
        if (status == NativeDebugWatchResolveStatus::NotLive) {
            set_failure(result, NativeDebugWatchStatus::NotLive,
                        "variable is not live in selected frame", node.offset);
            return false;
        }
        if (status == NativeDebugWatchResolveStatus::UnsupportedType) {
            set_failure(result, NativeDebugWatchStatus::UnsupportedType,
                        "variable type is not supported by watch evaluator", node.offset);
            return false;
        }
        if (status == NativeDebugWatchResolveStatus::MetadataUnavailable) {
            set_failure(result, NativeDebugWatchStatus::MetadataUnavailable,
                        "variable metadata is unavailable for selected frame", node.offset);
            return false;
        }
    } else if (!frame.variableMetadataAvailable) {
        set_failure(result, NativeDebugWatchStatus::MetadataUnavailable,
                    "variable metadata is unavailable for selected frame", node.offset);
        return false;
    }
    set_failure(result, NativeDebugWatchStatus::UnknownIdentifier,
                "identifier is not visible in selected frame", node.offset);
    return false;
}

static bool evaluate_node(const Ast& ast, uint16_t index, const NativeDebugWatchFrame& frame,
                          Value* output, NativeDebugWatchResult* result, uint32_t depth)
{
    if (index == kInvalidIndex || index >= ast.nodeCount ||
        depth > NATIVE_DEBUG_WATCH_MAX_EVALUATION_DEPTH) {
        set_failure(result, NativeDebugWatchStatus::TooComplex,
                    "evaluation depth limit exceeded");
        return false;
    }
    const Node& node = ast.nodes[index];
    if (node.kind == NodeKind::Integer) {
        if (node.integerValue > kInt64Max && node.integerValue != kInt64MinMagnitude) {
            set_failure(result, NativeDebugWatchStatus::Overflow,
                        "integer literal is outside signed 64-bit range", node.offset);
            return false;
        }
        *output = make_integer(static_cast<int64_t>(node.integerValue));
        return true;
    }
    if (node.kind == NodeKind::Identifier)
        return resolve_identifier(node, frame, output, result);

    if (node.kind == NodeKind::Unary && node.operation == Operator::Negative &&
        node.left < ast.nodeCount && ast.nodes[node.left].kind == NodeKind::Integer &&
        ast.nodes[node.left].integerValue == kInt64MinMagnitude) {
        *output = make_integer(kInt64Min);
        return true;
    }

    Value left = {};
    if (!evaluate_node(ast, node.left, frame, &left, result, depth + 1)) return false;
    if (node.kind == NodeKind::Unary) {
        if (!is_integer(left)) {
            set_failure(result, NativeDebugWatchStatus::UnsupportedOperator,
                        "unary operator requires an integer", node.offset);
            return false;
        }
        if (node.operation == Operator::Positive) { *output = left; return true; }
        if (node.operation == Operator::Negative) {
            if (left.signedValue == kInt64Min) {
                set_failure(result, NativeDebugWatchStatus::Overflow,
                            "signed 64-bit arithmetic overflow", node.offset);
                return false;
            }
            *output = make_integer(-left.signedValue);
            return true;
        }
        set_failure(result, NativeDebugWatchStatus::UnsupportedOperator,
                    "unsupported unary operator", node.offset);
        return false;
    }

    Value right = {};
    if (!evaluate_node(ast, node.right, frame, &right, result, depth + 1)) return false;
    if (!is_integer(left) || !is_integer(right)) {
        set_failure(result, NativeDebugWatchStatus::UnsupportedOperator,
                    "pointer arithmetic is not supported", node.offset);
        return false;
    }
    int64_t computed = 0;
    bool valid = true;
    switch (node.operation) {
    case Operator::Add: valid = checked_add(left.signedValue, right.signedValue, &computed); break;
    case Operator::Subtract: valid = checked_subtract(left.signedValue, right.signedValue, &computed); break;
    case Operator::Multiply: valid = checked_multiply(left.signedValue, right.signedValue, &computed); break;
    case Operator::Divide:
        if (right.signedValue == 0) {
            set_failure(result, NativeDebugWatchStatus::DivideByZero, "division by zero", node.offset);
            return false;
        }
        if (left.signedValue == kInt64Min && right.signedValue == -1) valid = false;
        else computed = left.signedValue / right.signedValue;
        break;
    case Operator::Modulo:
        if (right.signedValue == 0) {
            set_failure(result, NativeDebugWatchStatus::DivideByZero, "modulo by zero", node.offset);
            return false;
        }
        if (left.signedValue == kInt64Min && right.signedValue == -1) valid = false;
        else computed = left.signedValue % right.signedValue;
        break;
    default: valid = false; break;
    }
    if (!valid) {
        set_failure(result, NativeDebugWatchStatus::Overflow,
                    "signed 64-bit arithmetic overflow", node.offset);
        return false;
    }
    *output = make_integer(computed);
    return true;
}

static void append_decimal(char* output, uint32_t capacity, uint32_t* used, uint64_t value)
{
    char reversed[32] = {};
    uint32_t count = 0;
    do { reversed[count++] = static_cast<char>('0' + value % 10); value /= 10; }
    while (value != 0 && count < sizeof(reversed));
    while (count != 0 && *used + 1 < capacity) output[(*used)++] = reversed[--count];
    if (*used < capacity) output[*used] = '\0';
}

static void format_value(const Value& value, NativeDebugWatchResult* result)
{
    if (value.type == NativeDebugWatchValueType::Pointer) {
        const char digits[] = "0123456789abcdef";
        uint32_t used = 0;
        copy_text(result->formatted, sizeof(result->formatted), "0x");
        used = 2;
        for (int32_t shift = 60; shift >= 0 && used + 1 < sizeof(result->formatted); shift -= 4)
            result->formatted[used++] = digits[(value.unsignedValue >> shift) & 0xfu];
        result->formatted[used] = '\0';
        return;
    }
    uint32_t used = 0;
    if (value.signedValue < 0) {
        result->formatted[used++] = '-';
        result->formatted[used] = '\0';
        append_decimal(result->formatted, sizeof(result->formatted), &used,
                       static_cast<uint64_t>(-(value.signedValue + 1)) + 1ULL);
    } else {
        append_decimal(result->formatted, sizeof(result->formatted), &used,
                       static_cast<uint64_t>(value.signedValue));
    }
}

} // namespace

const char* native_debug_watch_status_name(NativeDebugWatchStatus status)
{
    switch (status) {
    case NativeDebugWatchStatus::Success: return "success";
    case NativeDebugWatchStatus::SyntaxError: return "syntax-error";
    case NativeDebugWatchStatus::UnknownIdentifier: return "unknown-identifier";
    case NativeDebugWatchStatus::NotLive: return "not-in-scope";
    case NativeDebugWatchStatus::InvalidSelectedFrame: return "invalid-selected-frame";
    case NativeDebugWatchStatus::StaleGeneration: return "stale-generation";
    case NativeDebugWatchStatus::UnsupportedType: return "unsupported-type";
    case NativeDebugWatchStatus::UnsupportedOperator: return "unsupported-operator";
    case NativeDebugWatchStatus::DivideByZero: return "divide-by-zero";
    case NativeDebugWatchStatus::Overflow: return "overflow";
    case NativeDebugWatchStatus::TooComplex: return "too-complex";
    case NativeDebugWatchStatus::ExpressionTooLong: return "expression-too-long";
    case NativeDebugWatchStatus::Running: return "running";
    case NativeDebugWatchStatus::MetadataUnavailable: return "metadata-unavailable";
    }
    return "unknown";
}

const char* native_debug_watch_value_type_name(NativeDebugWatchValueType type)
{
    switch (type) {
    case NativeDebugWatchValueType::SignedInt32: return "signed-integer";
    case NativeDebugWatchValueType::Pointer: return "pointer";
    }
    return "unknown";
}

bool native_debug_watch_evaluate(const char* expression, const NativeDebugWatchFrame& frame,
                                 NativeDebugWatchResult* result)
{
    if (!result) return false;
    *result = {};
    result->status = NativeDebugWatchStatus::SyntaxError;
    result->type = NativeDebugWatchValueType::SignedInt32;
    result->frameIndex = frame.frameIndex;
    result->sessionGeneration = frame.sessionGeneration;
    result->stopGeneration = frame.stopGeneration;
    if (!frame.paused) {
        set_failure(result, NativeDebugWatchStatus::Running, "watch requires a paused target");
        return false;
    }
    if (frame.frameIndex >= GX_DEVELOPMENT_DEBUG_MAX_CALL_STACK_FRAMES ||
        frame.sessionGeneration == 0 || frame.stopGeneration == 0 ||
        frame.instructionPointer == 0 || frame.framePointer == 0 || !frame.variables) {
        set_failure(result, NativeDebugWatchStatus::InvalidSelectedFrame,
                    "selected frame is not validated");
        return false;
    }
    if (frame.variables->size < sizeof(gx_development_debug_variables) ||
        frame.variables->variableCount > GX_DEVELOPMENT_DEBUG_MAX_VARIABLES) {
        set_failure(result, NativeDebugWatchStatus::InvalidSelectedFrame,
                    "selected variable snapshot is invalid");
        return false;
    }
    if (frame.variables->status == GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_STALE ||
        frame.variables->sessionGeneration != frame.sessionGeneration ||
        frame.variables->stopGeneration != frame.stopGeneration) {
        set_failure(result, NativeDebugWatchStatus::StaleGeneration,
                    "debug variable snapshot is stale");
        return false;
    }
    if (frame.variables->status == GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_NO_PAUSED_CONTEXT) {
        set_failure(result, NativeDebugWatchStatus::Running, "target has no paused context");
        return false;
    }
    if (frame.variables->status == GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_INVALID_FRAME ||
        frame.variables->framePointer != frame.framePointer ||
        frame.variables->instructionPointer != frame.instructionPointer) {
        set_failure(result, NativeDebugWatchStatus::InvalidSelectedFrame,
                    "selected frame identity is not authenticated");
        return false;
    }
    if (frame.variables->status != GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_SUCCESS &&
        frame.variables->status != GX_DEVELOPMENT_DEBUG_VARIABLES_STATUS_TRUNCATED) {
        set_failure(result, NativeDebugWatchStatus::InvalidSelectedFrame,
                    "selected variable snapshot is not usable");
        return false;
    }
    const uint32_t length = text_length(expression, NATIVE_DEBUG_WATCH_MAX_EXPRESSION_BYTES + 1);
    if (length == 0) {
        set_failure(result, NativeDebugWatchStatus::SyntaxError, "empty expression");
        return false;
    }
    if (length > NATIVE_DEBUG_WATCH_MAX_EXPRESSION_BYTES) {
        set_failure(result, NativeDebugWatchStatus::ExpressionTooLong,
                    "expression length limit exceeded");
        return false;
    }
    Ast ast = {};
    if (!tokenize(expression, length, &ast, result)) return false;
    result->tokenCount = ast.tokenCount;
    Parser parser(expression, &ast, result);
    const uint16_t root = parser.parse();
    result->nodeCount = ast.nodeCount;
    result->operatorCount = ast.operatorCount;
    if (root == kInvalidIndex) return false;
    Value value = {};
    if (!evaluate_node(ast, root, frame, &value, result, 0)) return false;
    result->status = NativeDebugWatchStatus::Success;
    result->type = value.type;
    result->signedValue = value.signedValue;
    result->unsignedValue = value.unsignedValue;
    result->rawValue = value.type == NativeDebugWatchValueType::Pointer
        ? value.unsignedValue : static_cast<uint64_t>(value.signedValue);
    result->diagnostic[0] = '\0';
    format_value(value, result);
    return true;
}

} // namespace native_elf
} // namespace kernel
