#include "runtime.h"

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <utility>

namespace gxos {
namespace javascript {
namespace {

bool isAsciiDigit(char value)
{
    return value >= '0' && value <= '9';
}

bool isAsciiHexDigit(char value)
{
    return isAsciiDigit(value) ||
        (value >= 'a' && value <= 'f') ||
        (value >= 'A' && value <= 'F');
}

unsigned hexValue(char value)
{
    if (isAsciiDigit(value)) return static_cast<unsigned>(value - '0');
    if (value >= 'a' && value <= 'f') {
        return static_cast<unsigned>(value - 'a') + 10u;
    }
    return static_cast<unsigned>(value - 'A') + 10u;
}

bool isAsciiWhitespace(char value)
{
    return value == ' ' || value == '\t' || value == '\n' || value == '\r' ||
        value == '\v' || value == '\f';
}

bool textEquals(SourceView text, const char* spelling)
{
    if (spelling == nullptr) return false;
    std::size_t length = 0;
    while (spelling[length] != '\0') ++length;
    if (text.data == nullptr || text.length != length) return false;
    for (std::size_t index = 0; index < length; ++index) {
        if (text.data[index] != spelling[index]) return false;
    }
    return true;
}

// This parser intentionally does not call locale-sensitive strtod.  It is
// used for both lexer-approved numeric literals and the JS3 primitive subset
// of string-to-number conversion.
bool parseNumber(SourceView input, double& result)
{
    if (input.data == nullptr && input.length != 0) return false;

    std::size_t begin = 0;
    while (begin < input.length && isAsciiWhitespace(input.data[begin])) ++begin;
    std::size_t end = input.length;
    while (end > begin && isAsciiWhitespace(input.data[end - 1])) --end;
    if (begin == end) {
        result = 0.0;
        return true;
    }

    bool negative = false;
    if (input.data[begin] == '+' || input.data[begin] == '-') {
        negative = input.data[begin] == '-';
        ++begin;
        if (begin == end) return false;
    }

    long double value = 0.0L;
    if (end - begin >= 2 && input.data[begin] == '0' &&
        (input.data[begin + 1] == 'x' || input.data[begin + 1] == 'X')) {
        begin += 2;
        if (begin == end) return false;
        for (std::size_t index = begin; index < end; ++index) {
            if (!isAsciiHexDigit(input.data[index])) return false;
            value = value * 16.0L + static_cast<long double>(
                hexValue(input.data[index]));
        }
        result = static_cast<double>(value);
        if (negative) result = -result;
        return true;
    }

    bool hasDigits = false;
    while (begin < end && isAsciiDigit(input.data[begin])) {
        hasDigits = true;
        value = value * 10.0L + static_cast<long double>(
            input.data[begin] - '0');
        ++begin;
    }

    std::size_t fractionalDigits = 0;
    if (begin < end && input.data[begin] == '.') {
        ++begin;
        while (begin < end && isAsciiDigit(input.data[begin])) {
            hasDigits = true;
            value = value * 10.0L + static_cast<long double>(
                input.data[begin] - '0');
            ++fractionalDigits;
            ++begin;
        }
    }
    if (!hasDigits) return false;

    long long exponent = 0;
    int exponentSign = 1;
    if (begin < end && (input.data[begin] == 'e' ||
        input.data[begin] == 'E')) {
        ++begin;
        if (begin < end && (input.data[begin] == '+' ||
            input.data[begin] == '-')) {
            exponentSign = input.data[begin] == '-' ? -1 : 1;
            ++begin;
        }
        if (begin == end || !isAsciiDigit(input.data[begin])) return false;
        while (begin < end && isAsciiDigit(input.data[begin])) {
            if (exponent < 1000000LL) {
                exponent = exponent * 10LL +
                    static_cast<long long>(input.data[begin] - '0');
                if (exponent > 1000000LL) exponent = 1000000LL;
            }
            ++begin;
        }
    }
    if (begin != end) return false;

    const long double scale = static_cast<long double>(exponentSign) *
        static_cast<long double>(exponent) -
        static_cast<long double>(fractionalDigits);
    if (value == 0.0L) {
        result = negative ? -0.0 : 0.0;
        return true;
    }
    if (scale > 100000.0L) {
        result = negative ? -std::numeric_limits<double>::infinity() :
            std::numeric_limits<double>::infinity();
        return true;
    }
    if (scale < -100000.0L) {
        result = negative ? -0.0 : 0.0;
        return true;
    }
    value *= std::pow(10.0L, scale);
    result = static_cast<double>(value);
    if (negative) result = -result;
    return true;
}

std::string numberToString(double value, bool& succeeded)
{
    succeeded = true;
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return std::signbit(value) ? "-Infinity" : "Infinity";

    std::array<char, 128> buffer{};
    const auto conversion = std::to_chars(buffer.data(),
        buffer.data() + buffer.size(), value);
    if (conversion.ec != std::errc()) {
        succeeded = false;
        return std::string();
    }
    return std::string(buffer.data(), conversion.ptr);
}

} // namespace

class RuntimeContext::Evaluator {
public:
    explicit Evaluator(RuntimeContext& context) : context_(context) {}

    bool run()
    {
        if (!validNode(context_.ast_.root(), SourceLocation()) ||
            context_.ast_.node(context_.ast_.root()).kind !=
                AstNodeKind::Program) {
            fail(RuntimeErrorCode::InvalidAstState, SourceLocation());
            return false;
        }
        Control control;
        return executeStatement(context_.ast_.root(), false, control) &&
            control.kind == ControlKind::Normal;
    }

private:
    enum class ControlKind : std::uint8_t {
        Normal = 0,
        Break,
        Continue,
    };

    struct Control {
        ControlKind kind = ControlKind::Normal;
    };

    bool validNode(AstNodeId id, SourceLocation location)
    {
        if (id != kInvalidAstNodeId && id < context_.ast_.nodeCount() &&
            context_.ast_.node(id).kind != AstNodeKind::Invalid) {
            return true;
        }
        fail(RuntimeErrorCode::InvalidAstState, location);
        return false;
    }

    bool beginNode(AstNodeId id)
    {
        if (!validNode(id, SourceLocation())) return false;
        return context_.consumeStep(context_.ast_.node(id).location);
    }

    void fail(RuntimeErrorCode code, SourceLocation location)
    {
        context_.setRuntimeError(code, location);
    }

    SourceView nodeText(AstNodeId id)
    {
        if (!validNode(id, SourceLocation())) return SourceView();
        return context_.ast_.nodeText(id);
    }

    bool identifierName(AstNodeId id, SourceView& name)
    {
        if (!validNode(id, SourceLocation())) return false;
        if (context_.ast_.node(id).kind != AstNodeKind::Identifier) {
            fail(RuntimeErrorCode::InvalidAstState, context_.ast_.node(id).location);
            return false;
        }
        name = nodeText(id);
        if (name.data == nullptr || name.length == 0) {
            fail(RuntimeErrorCode::InvalidAstState, context_.ast_.node(id).location);
            return false;
        }
        return true;
    }

    bool readIdentifier(AstNodeId id, Value& value)
    {
        SourceView name;
        if (!identifierName(id, name)) return false;
        const Value* found = context_.environment_.lookup(name);
        if (found != nullptr) {
            value = *found;
            return true;
        }
        // JS1 intentionally tokenizes `undefined` as an identifier.  JS3
        // supplies the primitive when it is not shadowed by a var binding.
        if (textEquals(name, "undefined")) {
            value = Value::undefined();
            return true;
        }
        fail(RuntimeErrorCode::UnknownIdentifier,
            context_.ast_.node(id).location);
        return false;
    }

    bool declare(SourceView name, Value value, SourceLocation location)
    {
        EnvironmentError error;
        if (context_.environment_.declare(name, value, error)) return true;
        switch (error.code) {
        case EnvironmentErrorCode::BindingLimitExceeded:
            fail(RuntimeErrorCode::BindingLimitExceeded, location);
            break;
        case EnvironmentErrorCode::BindingNameTooLong:
            fail(RuntimeErrorCode::BindingNameTooLong, location);
            break;
        case EnvironmentErrorCode::AllocationFailure:
            fail(RuntimeErrorCode::AllocationFailure, location);
            break;
        case EnvironmentErrorCode::None:
            fail(RuntimeErrorCode::InvalidAstState, location);
            break;
        }
        return false;
    }

    bool assign(SourceView name, Value value, SourceLocation location)
    {
        if (context_.environment_.assign(name, value)) return true;
        fail(RuntimeErrorCode::UnknownIdentifier, location);
        return false;
    }

    bool decodeString(AstNodeId id, Value& value)
    {
        const SourceLocation location = context_.ast_.node(id).location;
        const SourceView text = nodeText(id);
        if (text.data == nullptr || text.length < 2 ||
            (text.data[0] != '\'' && text.data[0] != '"') ||
            text.data[text.length - 1] != text.data[0]) {
            fail(RuntimeErrorCode::InvalidAstState, location);
            return false;
        }
        if (text.length - 2 > context_.limits_.maxRuntimeStringLength) {
            fail(RuntimeErrorCode::StringLimitExceeded, location);
            return false;
        }

        std::string decoded;
        try {
            decoded.reserve(text.length - 2);
            for (std::size_t index = 1; index + 1 < text.length; ++index) {
                char character = text.data[index];
                if (character == '\\') {
                    ++index;
                    if (index + 1 >= text.length) {
                        fail(RuntimeErrorCode::InvalidAstState, location);
                        return false;
                    }
                    switch (text.data[index]) {
                    case '\\': character = '\\'; break;
                    case '\'': character = '\''; break;
                    case '"': character = '"'; break;
                    case 'n': character = '\n'; break;
                    case 'r': character = '\r'; break;
                    case 't': character = '\t'; break;
                    case 'b': character = '\b'; break;
                    case 'f': character = '\f'; break;
                    default:
                        fail(RuntimeErrorCode::InvalidAstState, location);
                        return false;
                    }
                }
                if (decoded.size() >= context_.limits_.maxRuntimeStringLength) {
                    fail(RuntimeErrorCode::StringLimitExceeded, location);
                    return false;
                }
                decoded.push_back(character);
            }
        } catch (const std::bad_alloc&) {
            fail(RuntimeErrorCode::AllocationFailure, location);
            return false;
        }
        RuntimeErrorCode error = RuntimeErrorCode::None;
        if (!context_.createString(
            SourceView(decoded.data(), decoded.size()), value, error)) {
            fail(error, location);
            return false;
        }
        return true;
    }

    bool evalExpression(AstNodeId id, Value& value)
    {
        if (!beginNode(id)) return false;
        const AstNode& node = context_.ast_.node(id);
        switch (node.kind) {
        case AstNodeKind::Identifier:
            return readIdentifier(id, value);
        case AstNodeKind::NumericLiteral: {
            double number = 0.0;
            if (!parseNumber(nodeText(id), number)) {
                fail(RuntimeErrorCode::InvalidAstState, node.location);
                return false;
            }
            value = Value::number(number);
            return true;
        }
        case AstNodeKind::StringLiteral:
            return decodeString(id, value);
        case AstNodeKind::BooleanLiteral: {
            const SourceView text = nodeText(id);
            value = Value::boolean(textEquals(text, "true"));
            return true;
        }
        case AstNodeKind::NullLiteral:
            value = Value::nullValue();
            return true;
        case AstNodeKind::ThisExpression:
            fail(RuntimeErrorCode::UnsupportedFeature, node.location);
            return false;
        case AstNodeKind::UnaryExpression:
            return evalUnary(node, value);
        case AstNodeKind::BinaryExpression:
            return evalBinary(node, value);
        case AstNodeKind::LogicalExpression:
            return evalLogical(node, value);
        case AstNodeKind::AssignmentExpression:
            return evalAssignment(node, value);
        case AstNodeKind::UpdateExpression:
            return evalUpdate(node, value);
        case AstNodeKind::CallExpression:
        case AstNodeKind::MemberExpression:
        case AstNodeKind::NewExpression:
            fail(RuntimeErrorCode::UnsupportedFeature, node.location);
            return false;
        default:
            fail(RuntimeErrorCode::InvalidAstState, node.location);
            return false;
        }
    }

    bool toNumber(const Value& input, double& number)
    {
        switch (input.type()) {
        case ValueType::Undefined:
            number = std::numeric_limits<double>::quiet_NaN();
            return true;
        case ValueType::Null:
            number = 0.0;
            return true;
        case ValueType::Boolean:
            number = input.booleanValue() ? 1.0 : 0.0;
            return true;
        case ValueType::Number:
            number = input.numberValue();
            return true;
        case ValueType::String: {
            const std::string* text = context_.stringData(input);
            if (text == nullptr) {
                fail(RuntimeErrorCode::InvalidAstState, SourceLocation());
                return false;
            }
            if (!parseNumber(SourceView(text->data(), text->size()), number)) {
                number = std::numeric_limits<double>::quiet_NaN();
            }
            return true;
        }
        }
        fail(RuntimeErrorCode::InvalidOperandType, SourceLocation());
        return false;
    }

    bool primitiveString(const Value& input, std::string& text)
    {
        switch (input.type()) {
        case ValueType::Undefined: text = "undefined"; return true;
        case ValueType::Null: text = "null"; return true;
        case ValueType::Boolean: text = input.booleanValue() ? "true" : "false"; return true;
        case ValueType::Number: {
            bool succeeded = false;
            text = numberToString(input.numberValue(), succeeded);
            if (!succeeded) {
                fail(RuntimeErrorCode::InvalidOperandType, SourceLocation());
                return false;
            }
            return true;
        }
        case ValueType::String: {
            const std::string* string = context_.stringData(input);
            if (string == nullptr) {
                fail(RuntimeErrorCode::InvalidAstState, SourceLocation());
                return false;
            }
            text = *string;
            return true;
        }
        }
        fail(RuntimeErrorCode::InvalidOperandType, SourceLocation());
        return false;
    }

    bool addValues(const Value& left, const Value& right, Value& result,
        SourceLocation location)
    {
        if (!left.isString() && !right.isString()) {
            double leftNumber = 0.0;
            double rightNumber = 0.0;
            if (!toNumber(left, leftNumber) || !toNumber(right, rightNumber)) return false;
            result = Value::number(leftNumber + rightNumber);
            return true;
        }

        std::string leftText;
        std::string rightText;
        try {
            if (!primitiveString(left, leftText) ||
                !primitiveString(right, rightText)) return false;
            if (leftText.size() > context_.limits_.maxRuntimeStringLength ||
                rightText.size() > context_.limits_.maxRuntimeStringLength ||
                leftText.size() > context_.limits_.maxRuntimeStringLength -
                    (rightText.size() > context_.limits_.maxRuntimeStringLength
                        ? 0 : rightText.size())) {
                fail(RuntimeErrorCode::StringLimitExceeded, location);
                return false;
            }
            std::string combined;
            combined.reserve(leftText.size() + rightText.size());
            combined.append(leftText);
            combined.append(rightText);
            RuntimeErrorCode error = RuntimeErrorCode::None;
            if (!context_.createString(
                SourceView(combined.data(), combined.size()), result, error)) {
                fail(error, location);
                return false;
            }
        } catch (const std::bad_alloc&) {
            fail(RuntimeErrorCode::AllocationFailure, location);
            return false;
        }
        return true;
    }

    bool numericBinary(AstBinaryOperator operation, const Value& left,
        const Value& right, Value& result, SourceLocation location)
    {
        double leftNumber = 0.0;
        double rightNumber = 0.0;
        if (!toNumber(left, leftNumber) || !toNumber(right, rightNumber)) return false;
        switch (operation) {
        case AstBinaryOperator::Subtract:
            result = Value::number(leftNumber - rightNumber);
            return true;
        case AstBinaryOperator::Multiply:
            result = Value::number(leftNumber * rightNumber);
            return true;
        case AstBinaryOperator::Divide:
            if (rightNumber == 0.0) {
                if (leftNumber == 0.0 || std::isnan(leftNumber)) {
                    result = Value::number(std::numeric_limits<double>::quiet_NaN());
                } else {
                    const bool negative = std::signbit(leftNumber) !=
                        std::signbit(rightNumber);
                    result = Value::number(negative
                        ? -std::numeric_limits<double>::infinity()
                        : std::numeric_limits<double>::infinity());
                }
            } else {
                result = Value::number(leftNumber / rightNumber);
            }
            return true;
        case AstBinaryOperator::Remainder:
            result = Value::number(rightNumber == 0.0
                ? std::numeric_limits<double>::quiet_NaN()
                : std::fmod(leftNumber, rightNumber));
            return true;
        default:
            fail(RuntimeErrorCode::InvalidAstState, location);
            return false;
        }
    }

    bool evalUnary(const AstNode& node, Value& result)
    {
        if (!validNode(node.argument, node.location)) return false;
        Value argument;
        if (!evalExpression(node.argument, argument)) return false;
        switch (node.unaryOperator) {
        case AstUnaryOperator::LogicalNot:
            result = Value::boolean(!truthy(argument));
            return true;
        case AstUnaryOperator::Plus: {
            double number = 0.0;
            if (!toNumber(argument, number)) return false;
            result = Value::number(number);
            return true;
        }
        case AstUnaryOperator::Minus: {
            double number = 0.0;
            if (!toNumber(argument, number)) return false;
            result = Value::number(-number);
            return true;
        }
        }
        fail(RuntimeErrorCode::InvalidAstState, node.location);
        return false;
    }

    bool evalBinary(const AstNode& node, Value& result)
    {
        if (!validNode(node.left, node.location) ||
            !validNode(node.right, node.location)) return false;
        Value left;
        Value right;
        if (!evalExpression(node.left, left) ||
            !evalExpression(node.right, right)) return false;
        if (node.binaryOperator == AstBinaryOperator::Add) {
            return addValues(left, right, result, node.location);
        }
        if (node.binaryOperator == AstBinaryOperator::Subtract ||
            node.binaryOperator == AstBinaryOperator::Multiply ||
            node.binaryOperator == AstBinaryOperator::Divide ||
            node.binaryOperator == AstBinaryOperator::Remainder) {
            return numericBinary(node.binaryOperator, left, right, result,
                node.location);
        }
        if (node.binaryOperator == AstBinaryOperator::StrictEqual ||
            node.binaryOperator == AstBinaryOperator::StrictNotEqual) {
            const bool equal = strictEqual(left, right);
            result = Value::boolean(node.binaryOperator ==
                AstBinaryOperator::StrictEqual ? equal : !equal);
            return true;
        }
        if (node.binaryOperator == AstBinaryOperator::Equal ||
            node.binaryOperator == AstBinaryOperator::NotEqual) {
            const bool equal = looseEqual(left, right);
            result = Value::boolean(node.binaryOperator ==
                AstBinaryOperator::Equal ? equal : !equal);
            return true;
        }
        if (node.binaryOperator == AstBinaryOperator::Less ||
            node.binaryOperator == AstBinaryOperator::LessEqual ||
            node.binaryOperator == AstBinaryOperator::Greater ||
            node.binaryOperator == AstBinaryOperator::GreaterEqual) {
            double leftNumber = 0.0;
            double rightNumber = 0.0;
            if (!toNumber(left, leftNumber) || !toNumber(right, rightNumber)) return false;
            bool comparison = false;
            if (!std::isnan(leftNumber) && !std::isnan(rightNumber)) {
                switch (node.binaryOperator) {
                case AstBinaryOperator::Less: comparison = leftNumber < rightNumber; break;
                case AstBinaryOperator::LessEqual: comparison = leftNumber <= rightNumber; break;
                case AstBinaryOperator::Greater: comparison = leftNumber > rightNumber; break;
                case AstBinaryOperator::GreaterEqual: comparison = leftNumber >= rightNumber; break;
                default: break;
                }
            }
            result = Value::boolean(comparison);
            return true;
        }
        fail(RuntimeErrorCode::InvalidAstState, node.location);
        return false;
    }

    bool evalLogical(const AstNode& node, Value& result)
    {
        if (!validNode(node.left, node.location) ||
            !validNode(node.right, node.location)) return false;
        Value left;
        if (!evalExpression(node.left, left)) return false;
        if (node.logicalOperator == AstLogicalOperator::And) {
            if (!truthy(left)) {
                result = left;
                return true;
            }
        } else if (node.logicalOperator == AstLogicalOperator::Or) {
            if (truthy(left)) {
                result = left;
                return true;
            }
        } else {
            fail(RuntimeErrorCode::InvalidAstState, node.location);
            return false;
        }
        return evalExpression(node.right, result);
    }

    bool evalAssignment(const AstNode& node, Value& result)
    {
        if (!validNode(node.left, node.location) ||
            !validNode(node.right, node.location)) return false;
        if (context_.ast_.node(node.left).kind != AstNodeKind::Identifier) {
            fail(RuntimeErrorCode::InvalidAssignmentTarget, node.location);
            return false;
        }
        SourceView name;
        if (!identifierName(node.left, name)) return false;

        Value right;
        if (node.assignmentOperator == AstAssignmentOperator::Assign) {
            if (!evalExpression(node.right, right)) return false;
            if (!assign(name, right, context_.ast_.node(node.left).location)) return false;
            result = right;
            return true;
        }

        const Value* current = context_.environment_.lookup(name);
        if (current == nullptr) {
            fail(RuntimeErrorCode::UnknownIdentifier,
                context_.ast_.node(node.left).location);
            return false;
        }
        Value left = *current;
        if (!evalExpression(node.right, right)) return false;
        AstBinaryOperator binary = AstBinaryOperator::Add;
        switch (node.assignmentOperator) {
        case AstAssignmentOperator::Add: binary = AstBinaryOperator::Add; break;
        case AstAssignmentOperator::Subtract: binary = AstBinaryOperator::Subtract; break;
        case AstAssignmentOperator::Multiply: binary = AstBinaryOperator::Multiply; break;
        case AstAssignmentOperator::Divide: binary = AstBinaryOperator::Divide; break;
        case AstAssignmentOperator::Remainder: binary = AstBinaryOperator::Remainder; break;
        case AstAssignmentOperator::Assign:
            fail(RuntimeErrorCode::InvalidAstState, node.location);
            return false;
        }
        if (binary == AstBinaryOperator::Add) {
            if (!addValues(left, right, result, node.location)) return false;
        } else if (!numericBinary(binary, left, right, result, node.location)) {
            return false;
        }
        if (!assign(name, result, context_.ast_.node(node.left).location)) return false;
        return true;
    }

    bool evalUpdate(const AstNode& node, Value& result)
    {
        if (!validNode(node.argument, node.location)) return false;
        if (context_.ast_.node(node.argument).kind != AstNodeKind::Identifier) {
            fail(RuntimeErrorCode::InvalidAssignmentTarget, node.location);
            return false;
        }
        SourceView name;
        if (!identifierName(node.argument, name)) return false;
        const Value* current = context_.environment_.lookup(name);
        if (current == nullptr) {
            fail(RuntimeErrorCode::UnknownIdentifier,
                context_.ast_.node(node.argument).location);
            return false;
        }
        const Value old = *current;
        double number = 0.0;
        if (!toNumber(old, number)) return false;
        const double updated = node.updateOperator == AstUpdateOperator::Increment
            ? number + 1.0 : number - 1.0;
        const Value newValue = Value::number(updated);
        if (!assign(name, newValue, context_.ast_.node(node.argument).location)) return false;
        result = node.prefix ? newValue : old;
        return true;
    }

    bool executeStatement(AstNodeId id, bool inLoop, Control& control)
    {
        if (!beginNode(id)) return false;
        const AstNode& node = context_.ast_.node(id);
        switch (node.kind) {
        case AstNodeKind::Program:
        case AstNodeKind::BlockStatement:
            for (std::size_t index = 0; index < node.childCount; ++index) {
                const AstNodeId child = context_.ast_.childAt(id, index);
                if (!validNode(child, node.location)) return false;
                if (!executeStatement(child, inLoop, control)) return false;
                if (control.kind != ControlKind::Normal) return true;
            }
            return true;
        case AstNodeKind::EmptyStatement:
            return true;
        case AstNodeKind::VariableDeclaration:
            for (std::size_t index = 0; index < node.childCount; ++index) {
                const AstNodeId declaratorId = context_.ast_.childAt(id, index);
                if (!validNode(declaratorId, node.location)) return false;
                const AstNode& declarator = context_.ast_.node(declaratorId);
                if (declarator.kind != AstNodeKind::VariableDeclarator ||
                    !validNode(declarator.name, declarator.location)) {
                    fail(RuntimeErrorCode::InvalidAstState, declarator.location);
                    return false;
                }
                SourceView name;
                if (!identifierName(declarator.name, name)) return false;
                Value value = Value::undefined();
                if (!declare(name, value, context_.ast_.node(declarator.name).location)) return false;
                if (declarator.initializer != kInvalidAstNodeId) {
                    if (!validNode(declarator.initializer, declarator.location)) return false;
                    if (!evalExpression(declarator.initializer, value)) return false;
                    if (!context_.environment_.assign(name, value)) {
                        fail(RuntimeErrorCode::InvalidAstState, declarator.location);
                        return false;
                    }
                }
            }
            return true;
        case AstNodeKind::VariableDeclarator:
            fail(RuntimeErrorCode::InvalidAstState, node.location);
            return false;
        case AstNodeKind::ExpressionStatement: {
            if (!validNode(node.expression, node.location)) return false;
            Value value;
            if (!evalExpression(node.expression, value)) return false;
            context_.finalValue_ = value;
            return true;
        }
        case AstNodeKind::IfStatement: {
            if (!validNode(node.test, node.location) ||
                !validNode(node.consequent, node.location)) return false;
            Value test;
            if (!evalExpression(node.test, test)) return false;
            if (truthy(test)) return executeStatement(node.consequent, inLoop, control);
            if (node.alternate == kInvalidAstNodeId) return true;
            if (!validNode(node.alternate, node.location)) return false;
            return executeStatement(node.alternate, inLoop, control);
        }
        case AstNodeKind::WhileStatement: {
            if (!validNode(node.test, node.location) ||
                !validNode(node.body, node.location)) return false;
            while (true) {
                Value test;
                if (!evalExpression(node.test, test)) return false;
                if (!truthy(test)) return true;
                Control bodyControl;
                if (!executeStatement(node.body, true, bodyControl)) return false;
                if (bodyControl.kind == ControlKind::Break) return true;
                if (bodyControl.kind == ControlKind::Continue) continue;
            }
        }
        case AstNodeKind::ForStatement: {
            if (node.init != kInvalidAstNodeId) {
                if (!validNode(node.init, node.location)) return false;
                Control initControl;
                if (context_.ast_.node(node.init).kind == AstNodeKind::VariableDeclaration) {
                    if (!executeStatement(node.init, inLoop, initControl)) return false;
                } else {
                    Value initValue;
                    if (!evalExpression(node.init, initValue)) return false;
                }
                if (initControl.kind != ControlKind::Normal) {
                    fail(RuntimeErrorCode::InvalidAstState, node.location);
                    return false;
                }
            }
            while (true) {
                if (node.test != kInvalidAstNodeId) {
                    if (!validNode(node.test, node.location)) return false;
                    Value test;
                    if (!evalExpression(node.test, test)) return false;
                    if (!truthy(test)) return true;
                }
                Control bodyControl;
                if (!executeStatement(node.body, true, bodyControl)) return false;
                if (bodyControl.kind == ControlKind::Break) return true;
                if (node.update != kInvalidAstNodeId) {
                    if (!validNode(node.update, node.location)) return false;
                    Value update;
                    if (!evalExpression(node.update, update)) return false;
                }
                if (bodyControl.kind == ControlKind::Continue) continue;
            }
        }
        case AstNodeKind::BreakStatement:
            if (!inLoop) {
                fail(RuntimeErrorCode::IllegalBreak, node.location);
                return false;
            }
            control.kind = ControlKind::Break;
            return true;
        case AstNodeKind::ContinueStatement:
            if (!inLoop) {
                fail(RuntimeErrorCode::IllegalContinue, node.location);
                return false;
            }
            control.kind = ControlKind::Continue;
            return true;
        case AstNodeKind::ReturnStatement:
            fail(RuntimeErrorCode::IllegalReturn, node.location);
            return false;
        case AstNodeKind::FunctionDeclaration:
        case AstNodeKind::CallExpression:
            fail(RuntimeErrorCode::UnsupportedFeature, node.location);
            return false;
        default:
            fail(RuntimeErrorCode::InvalidAstState, node.location);
            return false;
        }
    }

    bool truthy(const Value& value) const
    {
        switch (value.type()) {
        case ValueType::Undefined:
        case ValueType::Null:
            return false;
        case ValueType::Boolean:
            return value.booleanValue();
        case ValueType::Number:
            return value.numberValue() != 0.0 && !std::isnan(value.numberValue());
        case ValueType::String: {
            const std::string* text = context_.stringData(value);
            return text != nullptr && !text->empty();
        }
        }
        return false;
    }

    bool strictEqual(const Value& left, const Value& right) const
    {
        if (left.type() != right.type()) return false;
        switch (left.type()) {
        case ValueType::Undefined:
        case ValueType::Null:
            return true;
        case ValueType::Boolean:
            return left.booleanValue() == right.booleanValue();
        case ValueType::Number:
            return !std::isnan(left.numberValue()) &&
                !std::isnan(right.numberValue()) &&
                left.numberValue() == right.numberValue();
        case ValueType::String: {
            const std::string* leftText = context_.stringData(left);
            const std::string* rightText = context_.stringData(right);
            return leftText != nullptr && rightText != nullptr &&
                *leftText == *rightText;
        }
        }
        return false;
    }

    bool looseEqual(const Value& left, const Value& right)
    {
        if ((left.isUndefined() || left.isNull()) &&
            (right.isUndefined() || right.isNull())) return true;
        if (left.type() == right.type()) return strictEqual(left, right);

        if (left.isBoolean()) {
            double number = 0.0;
            if (!toNumber(left, number)) return false;
            return looseEqual(Value::number(number), right);
        }
        if (right.isBoolean()) {
            double number = 0.0;
            if (!toNumber(right, number)) return false;
            return looseEqual(left, Value::number(number));
        }
        if ((left.isNumber() && right.isString()) ||
            (left.isString() && right.isNumber())) {
            double leftNumber = 0.0;
            double rightNumber = 0.0;
            if (!toNumber(left, leftNumber) || !toNumber(right, rightNumber)) return false;
            return !std::isnan(leftNumber) && !std::isnan(rightNumber) &&
                leftNumber == rightNumber;
        }
        return false;
    }

    RuntimeContext& context_;
};

RuntimeContext::RuntimeContext(RuntimeLimits limits)
    : limits_(limits),
      ast_(),
      environment_(EnvironmentLimits{limits.maxBindings,
          limits.maxBindingNameLength})
{
}

void RuntimeContext::reset()
{
    ast_.reset();
    sourceStorage_.clear();
    environment_.reset();
    strings_.clear();
    totalStringBytes_ = 0;
    executionSteps_ = 0;
    result_ = ScriptResult();
    finalValue_ = Value::undefined();
}

bool RuntimeContext::createString(SourceView text, Value& value,
    RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    if (text.data == nullptr && text.length != 0) {
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    }
    if (text.length > limits_.maxRuntimeStringLength ||
        strings_.size() >= limits_.maxRuntimeStringValues ||
        text.length > limits_.maxTotalRuntimeStringBytes ||
        totalStringBytes_ > limits_.maxTotalRuntimeStringBytes - text.length ||
        strings_.size() >= static_cast<std::size_t>(kInvalidRuntimeStringId)) {
        error = RuntimeErrorCode::StringLimitExceeded;
        return false;
    }
    try {
        strings_.emplace_back(text.data == nullptr ? "" : text.data, text.length);
    } catch (const std::bad_alloc&) {
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    }
    totalStringBytes_ += text.length;
    value = Value::string(static_cast<RuntimeStringId>(strings_.size() - 1));
    return true;
}

const std::string* RuntimeContext::stringData(const Value& value) const
{
    if (!value.isString() || value.stringId() >= strings_.size()) return nullptr;
    return &strings_[value.stringId()];
}

const std::string& RuntimeContext::stringValue(const Value& value) const
{
    static const std::string empty;
    const std::string* data = stringData(value);
    return data == nullptr ? empty : *data;
}

void RuntimeContext::setRuntimeError(RuntimeErrorCode code,
    SourceLocation location)
{
    if (result_.runtimeError.code != RuntimeErrorCode::None) return;
    result_.runtimeError.code = code;
    result_.runtimeError.location = location;
    result_.status = code == RuntimeErrorCode::ExecutionBudgetExceeded
        ? ScriptStatus::ExecutionBudgetExceeded : ScriptStatus::RuntimeFailure;
}

bool RuntimeContext::consumeStep(SourceLocation location)
{
    if (executionSteps_ >= limits_.maxExecutionSteps) {
        setRuntimeError(RuntimeErrorCode::ExecutionBudgetExceeded, location);
        return false;
    }
    ++executionSteps_;
    result_.executionSteps = executionSteps_;
    return true;
}

ScriptResult RuntimeContext::execute(SourceView source)
{
    reset();
    try {
        if (source.data == nullptr && source.length != 0) {
            Lexer lexer(limits_.lexer);
            const LexResult lexed = lexer.tokenize(source);
            result_.lexerError = lexed.error;
            result_.status = ScriptStatus::LexicalFailure;
            return result_;
        }
        if (source.length > limits_.lexer.maxSourceBytes) {
            Lexer lexer(limits_.lexer);
            const LexResult lexed = lexer.tokenize(source);
            result_.lexerError = lexed.error;
            result_.status = ScriptStatus::LexicalFailure;
            return result_;
        }
        if (source.length != 0) sourceStorage_.assign(source.data, source.length);

        const SourceView ownedSource(sourceStorage_.data(), sourceStorage_.size());
        Lexer lexer(limits_.lexer);
        const LexResult lexed = lexer.tokenize(ownedSource);
        if (!lexed.succeeded()) {
            result_.lexerError = lexed.error;
            result_.status = ScriptStatus::LexicalFailure;
            return result_;
        }

        Parser parser(limits_.parser);
        ParseResult parsed = parser.parse(ownedSource, lexed);
        if (!parsed.succeeded()) {
            result_.parserError = parsed.error;
            result_.status = ScriptStatus::ParseFailure;
            return result_;
        }
        ast_ = std::move(parsed.ast);

        Evaluator evaluator(*this);
        if (!evaluator.run()) return result_;
        result_.status = ScriptStatus::Success;
        return result_;
    } catch (const std::bad_alloc&) {
        setRuntimeError(RuntimeErrorCode::AllocationFailure, SourceLocation());
        return result_;
    }
}

ScriptResult executeScript(SourceView source, RuntimeContext& context)
{
    return context.execute(source);
}

const char* runtimeErrorCodeName(RuntimeErrorCode code)
{
    switch (code) {
    case RuntimeErrorCode::None: return "None";
    case RuntimeErrorCode::UnknownIdentifier: return "UnknownIdentifier";
    case RuntimeErrorCode::InvalidAssignmentTarget:
        return "InvalidAssignmentTarget";
    case RuntimeErrorCode::InvalidOperandType: return "InvalidOperandType";
    case RuntimeErrorCode::UnsupportedFeature: return "UnsupportedFeature";
    case RuntimeErrorCode::BindingLimitExceeded: return "BindingLimitExceeded";
    case RuntimeErrorCode::BindingNameTooLong: return "BindingNameTooLong";
    case RuntimeErrorCode::StringLimitExceeded: return "StringLimitExceeded";
    case RuntimeErrorCode::ExecutionBudgetExceeded:
        return "ExecutionBudgetExceeded";
    case RuntimeErrorCode::IllegalBreak: return "IllegalBreak";
    case RuntimeErrorCode::IllegalContinue: return "IllegalContinue";
    case RuntimeErrorCode::IllegalReturn: return "IllegalReturn";
    case RuntimeErrorCode::InvalidAstState: return "InvalidAstState";
    case RuntimeErrorCode::AllocationFailure: return "AllocationFailure";
    }
    return "Invalid";
}

const char* scriptStatusName(ScriptStatus status)
{
    switch (status) {
    case ScriptStatus::Success: return "Success";
    case ScriptStatus::LexicalFailure: return "LexicalFailure";
    case ScriptStatus::ParseFailure: return "ParseFailure";
    case ScriptStatus::RuntimeFailure: return "RuntimeFailure";
    case ScriptStatus::ExecutionBudgetExceeded:
        return "ExecutionBudgetExceeded";
    }
    return "Invalid";
}

} // namespace javascript
} // namespace gxos
