#include "runtime.h"

#include <algorithm>
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

RuntimeHostObjectId makeHostObjectId(std::size_t slot,
    HostGenerationId generation)
{
    return (static_cast<RuntimeHostObjectId>(generation) << 32u) |
        static_cast<RuntimeHostObjectId>(slot);
}

std::size_t hostObjectSlot(RuntimeHostObjectId id)
{
    return static_cast<std::size_t>(id & 0xffffffffull);
}

HostGenerationId hostObjectGeneration(RuntimeHostObjectId id)
{
    return static_cast<HostGenerationId>(id >> 32u);
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

bool isCanonicalArrayIndexSpelling(const std::string& key)
{
    if (key.empty()) return false;
    if (key == "0") return true;
    if (key[0] == '0') return false;
    for (const char character : key) {
        if (!isAsciiDigit(character)) return false;
    }
    return true;
}

bool parseBoundedArrayIndex(const std::string& key, std::size_t maximum,
    std::size_t& index)
{
    if (!isCanonicalArrayIndexSpelling(key)) return false;
    std::size_t value = 0;
    for (const char character : key) {
        const std::size_t digit = static_cast<std::size_t>(character - '0');
        if (value > maximum / 10u ||
            (value == maximum / 10u && digit > maximum % 10u)) {
            return false;
        }
        value = value * 10u + digit;
    }
    index = value;
    return true;
}

} // namespace

class RuntimeContext::Evaluator {
public:
    explicit Evaluator(RuntimeContext& context) : context_(context) {}

    ~Evaluator()
    {
        // The evaluator owns the transient call-frame vector.  Clear the
        // public diagnostic counter even if a bounded allocation failure
        // interrupts a call before its normal unwind path runs.
        context_.activeCallFrames_ = 0;
    }

    bool run()
    {
        if (!validNode(context_.ast_.root(), SourceLocation()) ||
            context_.ast_.node(context_.ast_.root()).kind !=
                AstNodeKind::Program) {
            fail(RuntimeErrorCode::InvalidAstState, SourceLocation());
            return false;
        }
        if (!validateFunctionPolicy(context_.ast_.root(), true)) return false;
        if (!instantiateDeclarations(context_.ast_.root(),
            kGlobalEnvironmentId)) return false;
        Control control;
        return executeStatement(context_.ast_.root(), false, false, control) &&
            control.kind == ControlKind::Normal;
    }

    bool invokeForTesting(RuntimeFunctionId function,
        const std::vector<Value>& arguments, Value& result,
        RuntimeErrorCode& error)
    {
        if (function == kInvalidRuntimeFunctionId) {
            error = RuntimeErrorCode::InvalidFunction;
            fail(error, SourceLocation());
            return false;
        }
        const bool succeeded = invokeFunction(function, arguments,
            SourceLocation(), result);
        error = context_.result_.runtimeError.code;
        return succeeded;
    }

private:
    enum class ControlKind : std::uint8_t {
        Normal = 0,
        Break,
        Continue,
        Return,
    };

    struct Control {
        ControlKind kind = ControlKind::Normal;
        Value value = Value::undefined();
    };

    struct CallFrame {
        RuntimeFunctionId function = kInvalidRuntimeFunctionId;
        EnvironmentId environment = kInvalidEnvironmentId;
        EnvironmentId callerEnvironment = kInvalidEnvironmentId;
        SourceLocation callSite;
        Value thisValue = Value::undefined();
    };

    struct MemberReference {
        RuntimeObjectId object = kInvalidRuntimeObjectId;
        RuntimeHostObjectId hostObject = kInvalidRuntimeHostObjectId;
        Value receiver = Value::undefined();
        bool stringPrimitive = false;
        bool hostObjectValue = false;
        std::string key;
        SourceLocation location;
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
        const Value* found = lookup(name);
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

    const Value* lookup(SourceView name) const
    {
        EnvironmentId environment = currentEnvironment_;
        while (environment != kInvalidEnvironmentId) {
            const Environment* current = context_.environmentAt(environment);
            if (current == nullptr) return nullptr;
            const Value* found = current->lookup(name);
            if (found != nullptr) return found;
            environment = current->parent();
        }
        return nullptr;
    }

    bool declareIn(EnvironmentId environment, SourceView name, Value value,
        SourceLocation location)
    {
        Environment* target = context_.environmentAt(environment);
        if (target == nullptr) {
            fail(RuntimeErrorCode::InvalidAstState, location);
            return false;
        }
        EnvironmentError error;
        if (target->declare(name, value, error)) return true;
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

    bool declare(SourceView name, Value value, SourceLocation location)
    {
        return declareIn(currentEnvironment_, name, value, location);
    }

    bool bindValueIn(EnvironmentId environment, SourceView name, Value value,
        SourceLocation location)
    {
        Environment* target = context_.environmentAt(environment);
        if (target == nullptr) {
            fail(RuntimeErrorCode::InvalidAstState, location);
            return false;
        }
        if (target->lookup(name) != nullptr) {
            if (!target->assign(name, value)) {
                fail(RuntimeErrorCode::InvalidAstState, location);
                return false;
            }
            return true;
        }
        return declareIn(environment, name, value, location);
    }

    bool assign(SourceView name, Value value, SourceLocation location)
    {
        EnvironmentId environment = currentEnvironment_;
        while (environment != kInvalidEnvironmentId) {
            Environment* current = context_.environmentAt(environment);
            if (current == nullptr) {
                fail(RuntimeErrorCode::InvalidAstState, location);
                return false;
            }
            if (current->lookup(name) != nullptr) {
                if (!current->assign(name, value)) {
                    fail(RuntimeErrorCode::InvalidAstState, location);
                    return false;
                }
                return true;
            }
            environment = current->parent();
        }
        fail(RuntimeErrorCode::UnknownIdentifier, location);
        return false;
    }

    bool validateFunctionPolicy(AstNodeId id, bool allowDirectFunction)
    {
        if (!validNode(id, SourceLocation())) return false;
        const AstNode& node = context_.ast_.node(id);
        switch (node.kind) {
        case AstNodeKind::Program:
        case AstNodeKind::BlockStatement:
            for (std::size_t index = 0; index < node.childCount; ++index) {
                const AstNodeId child = context_.ast_.childAt(id, index);
                if (!validNode(child, node.location) ||
                    !validateFunctionPolicy(child, allowDirectFunction)) {
                    return false;
                }
            }
            return true;
        case AstNodeKind::FunctionDeclaration:
            if (!allowDirectFunction) {
                fail(RuntimeErrorCode::UnsupportedFunctionConstruct,
                    node.location);
                return false;
            }
            if (!validNode(node.body, node.location)) return false;
            // Only declarations directly in a program or function body are
            // supported.  A block reached through if/while/for passes false.
            return validateFunctionPolicy(node.body, true);
        case AstNodeKind::FunctionExpression:
            return validNode(node.body, node.location) &&
                validateFunctionPolicy(node.body, true);
        case AstNodeKind::IfStatement:
            if (!validNode(node.consequent, node.location) ||
                !validateFunctionPolicy(node.consequent, false)) return false;
            if (node.alternate == kInvalidAstNodeId) return true;
            return validNode(node.alternate, node.location) &&
                validateFunctionPolicy(node.alternate, false);
        case AstNodeKind::WhileStatement:
            return validNode(node.body, node.location) &&
                validateFunctionPolicy(node.body, false);
        case AstNodeKind::ForStatement:
            return validNode(node.body, node.location) &&
                validateFunctionPolicy(node.body, false);
        default:
            return true;
        }
    }

    bool hoistVariables(AstNodeId id, EnvironmentId environment)
    {
        if (!validNode(id, SourceLocation())) return false;
        const AstNode& node = context_.ast_.node(id);
        switch (node.kind) {
        case AstNodeKind::Program:
        case AstNodeKind::BlockStatement:
            for (std::size_t index = 0; index < node.childCount; ++index) {
                if (!hoistVariables(context_.ast_.childAt(id, index),
                    environment)) return false;
            }
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
                if (!identifierName(declarator.name, name) ||
                    !declareIn(environment, name, Value::undefined(),
                        context_.ast_.node(declarator.name).location)) {
                    return false;
                }
            }
            return true;
        case AstNodeKind::FunctionDeclaration:
            // A nested function's var bindings belong to its invocation.
            return true;
        case AstNodeKind::FunctionExpression:
            // An expression function's var bindings belong to its invocation.
            return true;
        case AstNodeKind::IfStatement:
            if (!hoistVariables(node.consequent, environment)) return false;
            return node.alternate == kInvalidAstNodeId ||
                hoistVariables(node.alternate, environment);
        case AstNodeKind::WhileStatement:
            return hoistVariables(node.body, environment);
        case AstNodeKind::ForStatement:
            if (node.init != kInvalidAstNodeId &&
                !hoistVariables(node.init, environment)) return false;
            return hoistVariables(node.body, environment);
        default:
            return true;
        }
    }

    bool sameName(AstNodeId left, AstNodeId right)
    {
        SourceView leftName;
        SourceView rightName;
        return identifierName(left, leftName) && identifierName(right, rightName) &&
            leftName.length == rightName.length &&
            (leftName.length == 0 ||
                std::equal(leftName.data, leftName.data + leftName.length,
                    rightName.data));
    }

    bool hasLaterFunctionDeclaration(AstNodeId container,
        std::size_t currentIndex, AstNodeId name)
    {
        const AstNode& node = context_.ast_.node(container);
        for (std::size_t index = currentIndex + 1; index < node.childCount;
            ++index) {
            const AstNodeId child = context_.ast_.childAt(container, index);
            if (!validNode(child, node.location)) return false;
            const AstNode& candidate = context_.ast_.node(child);
            if (candidate.kind == AstNodeKind::FunctionDeclaration &&
                sameName(candidate.name, name)) return true;
        }
        return false;
    }

    bool hoistFunctions(AstNodeId container, EnvironmentId environment)
    {
        if (!validNode(container, SourceLocation())) return false;
        const AstNode& node = context_.ast_.node(container);
        if (node.kind != AstNodeKind::Program &&
            node.kind != AstNodeKind::BlockStatement) {
            fail(RuntimeErrorCode::InvalidAstState, node.location);
            return false;
        }
        for (std::size_t index = 0; index < node.childCount; ++index) {
            const AstNodeId child = context_.ast_.childAt(container, index);
            if (!validNode(child, node.location)) return false;
            const AstNode& declaration = context_.ast_.node(child);
            if (declaration.kind != AstNodeKind::FunctionDeclaration) continue;
            if (hasLaterFunctionDeclaration(container, index,
                declaration.name)) continue;
            SourceView name;
            if (!identifierName(declaration.name, name)) return false;
            RuntimeFunctionId function = kInvalidRuntimeFunctionId;
            RuntimeErrorCode error = RuntimeErrorCode::None;
            if (!context_.createFunction(child, environment, function,
                error)) {
                fail(error, declaration.location);
                return false;
            }
            if (!bindValueIn(environment, name, Value::function(function),
                context_.ast_.node(declaration.name).location)) return false;
        }
        return true;
    }

    bool instantiateDeclarations(AstNodeId container,
        EnvironmentId environment)
    {
        return hoistVariables(container, environment) &&
            hoistFunctions(container, environment);
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

    bool propertyKeyFromValue(const Value& value, std::string& key,
        SourceLocation location)
    {
        if (value.isObject() || value.isFunction()) {
            fail(RuntimeErrorCode::InvalidPropertyKey, location);
            return false;
        }
        if (!primitiveString(value, key)) {
            if (context_.result_.runtimeError.code == RuntimeErrorCode::None) {
                fail(RuntimeErrorCode::InvalidPropertyKey, location);
            }
            return false;
        }
        if (value.isNumber() && value.numberValue() == 0.0) key = "0";
        return true;
    }

    bool resolveMember(AstNodeId id, MemberReference& reference,
        bool forWrite = false)
    {
        if (!validNode(id, SourceLocation())) return false;
        const AstNode& node = context_.ast_.node(id);
        if (node.kind != AstNodeKind::MemberExpression ||
            !validNode(node.object, node.location) ||
            !validNode(node.property, node.location)) {
            fail(RuntimeErrorCode::InvalidAstState, node.location);
            return false;
        }
        Value object;
        if (!evalExpression(node.object, object)) return false;
        if (!object.isObject() && !object.isHostObject() && !object.isString()) {
            fail(object.isNull() || object.isUndefined()
                ? (forWrite ? RuntimeErrorCode::CannotWriteProperty
                    : RuntimeErrorCode::CannotReadProperty)
                : RuntimeErrorCode::NotObject, node.location);
            return false;
        }
        if (object.isObject() && context_.objectAt(object.objectId()) == nullptr) {
            fail(RuntimeErrorCode::CannotReadProperty, node.location);
            return false;
        }

        std::string key;
        if (!node.computed) {
            SourceView keyView;
            if (!identifierName(node.property, keyView)) return false;
            try {
                key.assign(keyView.data, keyView.length);
            } catch (const std::bad_alloc&) {
                fail(RuntimeErrorCode::AllocationFailure, node.location);
                return false;
            }
        } else {
            Value property;
            if (!evalExpression(node.property, property) ||
                !propertyKeyFromValue(property, key, node.location)) return false;
        }
        if (object.isString()) {
            if (forWrite) {
                fail(RuntimeErrorCode::NotObject, node.location);
                return false;
            }
            reference.stringPrimitive = true;
            reference.receiver = object;
        } else if (object.isHostObject()) {
            reference.hostObject = object.hostObjectId();
            reference.hostObjectValue = true;
            reference.receiver = object;
        } else {
            reference.object = object.objectId();
            reference.receiver = object;
        }
        reference.key = std::move(key);
        reference.location = node.location;
        return true;
    }

    bool readMember(const MemberReference& reference, Value& value)
    {
        if (!context_.consumeStep(reference.location)) return false;
        if (reference.stringPrimitive) {
            value = reference.key == "length"
                ? Value::number(static_cast<double>(
                    context_.stringValue(reference.receiver).size()))
                : Value::undefined();
            return true;
        }
        if (reference.hostObjectValue) {
            RuntimeErrorCode error = RuntimeErrorCode::None;
            if (context_.readHostProperty(reference.hostObject, reference.key,
                value, error, reference.location)) return true;
            fail(error, reference.location);
            return false;
        }
        RuntimeErrorCode error = RuntimeErrorCode::None;
        if (context_.readProperty(reference.object, reference.key, value, error,
            reference.location)) {
            return true;
        }
        fail(error, reference.location);
        return false;
    }

    bool writeMember(const MemberReference& reference, Value value)
    {
        if (reference.stringPrimitive) {
            fail(RuntimeErrorCode::NotObject, reference.location);
            return false;
        }
        if (!context_.consumeStep(reference.location)) return false;
        if (reference.hostObjectValue) {
            RuntimeErrorCode error = RuntimeErrorCode::None;
            if (context_.writeHostProperty(reference.hostObject, reference.key,
                value, error, reference.location)) return true;
            fail(error, reference.location);
            return false;
        }
        RuntimeErrorCode error = RuntimeErrorCode::None;
        if (context_.writeProperty(reference.object, reference.key, value, error)) {
            return true;
        }
        fail(error, reference.location);
        return false;
    }

    bool evalObjectLiteral(AstNodeId id, const AstNode& node, Value& result)
    {
        if (!context_.consumeStep(node.location)) return false;
        const std::vector<Value> noElements;
        RuntimeObjectId object = kInvalidRuntimeObjectId;
        RuntimeErrorCode error = RuntimeErrorCode::None;
        if (!context_.createObject(false, noElements, object, error)) {
            fail(error, node.location);
            return false;
        }
        result = Value::object(object);
        for (std::size_t index = 0; index < node.childCount; ++index) {
            const AstNodeId propertyId = context_.ast_.childAt(id, index);
            if (!validNode(propertyId, node.location)) return false;
            const AstNode& property = context_.ast_.node(propertyId);
            if (property.kind != AstNodeKind::ObjectProperty ||
                !validNode(property.key, property.location) ||
                !validNode(property.initializer, property.location)) {
                fail(RuntimeErrorCode::InvalidAstState, property.location);
                return false;
            }
            std::string key;
            if (context_.ast_.node(property.key).kind == AstNodeKind::Identifier) {
                SourceView keyView;
                if (!identifierName(property.key, keyView)) return false;
                try {
                    key.assign(keyView.data, keyView.length);
                } catch (const std::bad_alloc&) {
                    fail(RuntimeErrorCode::AllocationFailure, property.location);
                    return false;
                }
            } else if (context_.ast_.node(property.key).kind == AstNodeKind::StringLiteral) {
                Value keyValue;
                if (!decodeString(property.key, keyValue)) return false;
                const std::string* keyText = context_.stringData(keyValue);
                if (keyText == nullptr) {
                    fail(RuntimeErrorCode::InvalidAstState, property.location);
                    return false;
                }
                key = *keyText;
            } else {
                fail(RuntimeErrorCode::InvalidAstState, property.location);
                return false;
            }
            Value value;
            if (!evalExpression(property.initializer, value)) return false;
            if (!context_.consumeStep(property.location)) return false;
            error = RuntimeErrorCode::None;
            if (!context_.writeProperty(object, key, value, error)) {
                fail(error, property.location);
                return false;
            }
        }
        return true;
    }

    bool evalArrayLiteral(AstNodeId id, const AstNode& node, Value& result)
    {
        std::vector<Value> elements;
        try {
            elements.reserve(node.childCount);
            for (std::size_t index = 0; index < node.childCount; ++index) {
                const AstNodeId elementId = context_.ast_.childAt(id, index);
                if (!validNode(elementId, node.location)) return false;
                Value element;
                if (!evalExpression(elementId, element)) return false;
                elements.push_back(element);
            }
        } catch (const std::bad_alloc&) {
            fail(RuntimeErrorCode::AllocationFailure, node.location);
            return false;
        }
        if (!context_.consumeStep(node.location)) return false;
        RuntimeObjectId array = kInvalidRuntimeObjectId;
        RuntimeErrorCode error = RuntimeErrorCode::None;
        if (!context_.createObject(true, elements, array, error)) {
            fail(error, node.location);
            return false;
        }
        result = Value::object(array);
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
            value = currentThis_;
            return true;
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
            return evalCall(id, node, value);
        case AstNodeKind::FunctionExpression: {
            RuntimeFunctionId function = kInvalidRuntimeFunctionId;
            RuntimeErrorCode error = RuntimeErrorCode::None;
            if (!context_.createFunction(id, currentEnvironment_, function,
                error)) {
                fail(error, node.location);
                return false;
            }
            value = Value::function(function);
            return true;
        }
        case AstNodeKind::MemberExpression: {
            MemberReference reference;
            if (!resolveMember(id, reference)) return false;
            return readMember(reference, value);
        }
        case AstNodeKind::ObjectLiteral:
            return evalObjectLiteral(id, node, value);
        case AstNodeKind::ArrayLiteral:
            return evalArrayLiteral(id, node, value);
        case AstNodeKind::ObjectProperty:
            case AstNodeKind::NewExpression:
            fail(RuntimeErrorCode::UnsupportedFeature, node.location);
            return false;
        default:
            fail(RuntimeErrorCode::InvalidAstState, node.location);
            return false;
        }
    }

    bool evalCall(AstNodeId callId, const AstNode& node, Value& result)
    {
        if (!validNode(node.callee, node.location)) return false;
        if (node.childCount > context_.limits_.parser.maxCallArguments) {
            fail(RuntimeErrorCode::InvalidAstState, node.location);
            return false;
        }

        Value callee;
        Value receiver = Value::undefined();
        if (context_.ast_.node(node.callee).kind == AstNodeKind::MemberExpression) {
            MemberReference reference;
            if (!resolveMember(node.callee, reference)) return false;
            if (!readMember(reference, callee)) return false;
            receiver = reference.receiver;
        } else if (!evalExpression(node.callee, callee)) {
            return false;
        }
        if (!callee.isFunction()) {
            fail(RuntimeErrorCode::NotCallable, node.location);
            return false;
        }

        std::vector<Value> arguments;
        try {
            arguments.reserve(node.childCount);
            for (std::size_t index = 0; index < node.childCount; ++index) {
                const AstNodeId argumentId = context_.ast_.childAt(callId, index);
                if (!validNode(argumentId, node.location)) return false;
                Value argument;
                if (!evalExpression(argumentId, argument)) return false;
                arguments.push_back(argument);
            }
        } catch (const std::bad_alloc&) {
            fail(RuntimeErrorCode::AllocationFailure, node.location);
            return false;
        }
        return invokeFunction(callee.functionId(), arguments, node.location,
            result, receiver);
    }

    void unwindFrame(EnvironmentId callerEnvironment,
        Value callerThis, std::size_t previousFrameCount)
    {
        currentEnvironment_ = callerEnvironment;
        currentThis_ = callerThis;
        if (frames_.size() > previousFrameCount) {
            frames_.resize(previousFrameCount);
        }
        context_.activeCallFrames_ = frames_.size();
    }

    bool invokeFunction(RuntimeFunctionId functionId,
        const std::vector<Value>& arguments, SourceLocation callSite,
        Value& result, Value receiver = Value::undefined())
    {
        const RuntimeContext::FunctionRecord* function =
            context_.functionAt(functionId);
        if (function == nullptr) {
            fail(RuntimeErrorCode::InvalidFunction, callSite);
            return false;
        }
        if (function->kind == RuntimeContext::FunctionRecord::Kind::Native) {
            return invokeNative(*function, arguments, callSite, receiver, result);
        }
        if (function->kind == RuntimeContext::FunctionRecord::Kind::Host) {
            return context_.invokeHostMethod(*function, arguments, receiver,
                callSite, result);
        }
        if (function->declaration == kInvalidAstNodeId ||
            function->closureEnvironment == kInvalidEnvironmentId) {
            fail(RuntimeErrorCode::InvalidFunction, callSite);
            return false;
        }
        const AstNode& declaration = context_.ast_.node(function->declaration);
        if ((declaration.kind != AstNodeKind::FunctionDeclaration &&
            declaration.kind != AstNodeKind::FunctionExpression) ||
            !validNode(declaration.body, callSite)) {
            fail(RuntimeErrorCode::InvalidFunction, callSite);
            return false;
        }
        if (frames_.size() >= context_.limits_.maxCallDepth) {
            fail(RuntimeErrorCode::CallDepthExceeded, callSite);
            return false;
        }

        EnvironmentId callEnvironment = kInvalidEnvironmentId;
        RuntimeErrorCode error = RuntimeErrorCode::None;
        if (!context_.createEnvironment(function->closureEnvironment,
            callEnvironment, error)) {
            fail(error, callSite);
            return false;
        }

        const EnvironmentId callerEnvironment = currentEnvironment_;
        const Value callerThis = currentThis_;
        const std::size_t previousFrameCount = frames_.size();
        try {
            CallFrame frame;
            frame.function = functionId;
            frame.environment = callEnvironment;
            frame.callerEnvironment = callerEnvironment;
            frame.callSite = callSite;
            frame.thisValue = receiver;
            frames_.push_back(frame);
        } catch (const std::bad_alloc&) {
            fail(RuntimeErrorCode::AllocationFailure, callSite);
            return false;
        }
        context_.activeCallFrames_ = frames_.size();
        currentEnvironment_ = callEnvironment;
        currentThis_ = receiver;

        for (std::size_t index = 0; index < declaration.childCount; ++index) {
            const AstNodeId parameterId = context_.ast_.childAt(
                function->declaration, index);
            SourceView name;
            if (!identifierName(parameterId, name)) {
                unwindFrame(callerEnvironment, callerThis, previousFrameCount);
                return false;
            }
            const Value argument = index < arguments.size()
                ? arguments[index] : Value::undefined();
            if (!declareIn(callEnvironment, name, argument,
                context_.ast_.node(parameterId).location)) {
                unwindFrame(callerEnvironment, callerThis, previousFrameCount);
                return false;
            }
            Environment* target = context_.environmentAt(callEnvironment);
            if (target == nullptr || !target->assign(name, argument)) {
                fail(RuntimeErrorCode::InvalidAstState,
                    context_.ast_.node(parameterId).location);
                unwindFrame(callerEnvironment, callerThis, previousFrameCount);
                return false;
            }
        }

        if (!instantiateDeclarations(declaration.body, callEnvironment)) {
            unwindFrame(callerEnvironment, callerThis, previousFrameCount);
            return false;
        }

        Control control;
        const bool executed = executeStatement(declaration.body, false, true,
            control);
        if (!executed) {
            unwindFrame(callerEnvironment, callerThis, previousFrameCount);
            return false;
        }
        if (control.kind == ControlKind::Return) {
            result = control.value;
        } else if (control.kind == ControlKind::Normal) {
            result = Value::undefined();
        } else {
            fail(RuntimeErrorCode::InvalidAstState, declaration.location);
            unwindFrame(callerEnvironment, callerThis, previousFrameCount);
            return false;
        }
        unwindFrame(callerEnvironment, callerThis, previousFrameCount);
        return true;
    }

    bool hasOwnProperty(RuntimeObjectId objectId, const std::string& key) const
    {
        const RuntimeContext::RuntimeObject* object =
            context_.objectAt(objectId);
        if (object == nullptr) return false;
        if (object->array && key == "length") return true;
        if (object->array && isCanonicalArrayIndexSpelling(key)) {
            std::size_t index = 0;
            if (parseBoundedArrayIndex(key, context_.limits_.maxArrayIndex,
                index)) {
                return index < object->elements.size();
            }
            return false;
        }
        for (const RuntimeContext::RuntimeProperty& property :
            object->properties) {
            if (property.key == key) return true;
        }
        return false;
    }

    bool invokeNative(const RuntimeContext::FunctionRecord& function,
        const std::vector<Value>& arguments, SourceLocation callSite,
        Value receiver, Value& result)
    {
        if (frames_.size() >= context_.limits_.maxCallDepth) {
            fail(RuntimeErrorCode::CallDepthExceeded, callSite);
            return false;
        }
        if (!context_.consumeStep(callSite)) return false;

        const Value callerThis = currentThis_;
        const std::size_t previousFrameCount = frames_.size();
        try {
            CallFrame frame;
            frame.function = static_cast<RuntimeFunctionId>(
                function.nativeFunction);
            frame.callerEnvironment = currentEnvironment_;
            frame.callSite = callSite;
            frame.thisValue = receiver;
            frames_.push_back(frame);
        } catch (const std::bad_alloc&) {
            fail(RuntimeErrorCode::AllocationFailure, callSite);
            return false;
        }
        context_.activeCallFrames_ = frames_.size();
        currentThis_ = receiver;

        bool succeeded = true;
        const auto argumentOrUndefined = [&arguments](std::size_t index) {
            return index < arguments.size() ? arguments[index]
                : Value::undefined();
        };
        const auto numericArgument = [this, &argumentOrUndefined,
            callSite](std::size_t index, double& number) {
            if (!context_.consumeStep(callSite)) return false;
            return toNumber(argumentOrUndefined(index), number);
        };

        switch (static_cast<RuntimeContext::NativeFunctionId>(
            function.nativeFunction)) {
        case RuntimeContext::NativeFunctionId::ObjectHasOwnProperty: {
            if (!receiver.isObject() ||
                context_.objectAt(receiver.objectId()) == nullptr) {
                fail(RuntimeErrorCode::InvalidReceiver, callSite);
                succeeded = false;
                break;
            }
            std::string key;
            if (!propertyKeyFromValue(argumentOrUndefined(0), key, callSite)) {
                succeeded = false;
                break;
            }
            result = Value::boolean(hasOwnProperty(receiver.objectId(), key));
            break;
        }
        case RuntimeContext::NativeFunctionId::ArrayPush: {
            RuntimeContext::RuntimeObject* object = receiver.isObject()
                ? context_.objectAt(receiver.objectId()) : nullptr;
            if (object == nullptr || !object->array) {
                fail(RuntimeErrorCode::InvalidReceiver, callSite);
                succeeded = false;
                break;
            }
            const std::size_t oldLength = object->elements.size();
            if (arguments.size() > context_.limits_.maxArrayElements -
                (oldLength > context_.limits_.maxArrayElements
                    ? context_.limits_.maxArrayElements : oldLength)) {
                fail(RuntimeErrorCode::ArrayLimitExceeded, callSite);
                succeeded = false;
                break;
            }
            if (arguments.size() > context_.limits_.maxTotalArrayElements ||
                context_.totalArrayElements_ >
                    context_.limits_.maxTotalArrayElements - arguments.size()) {
                fail(RuntimeErrorCode::ArrayLimitExceeded, callSite);
                succeeded = false;
                break;
            }
            if (arguments.size() > context_.limits_.maxArrayIndex + 1u ||
                oldLength > context_.limits_.maxArrayIndex + 1u -
                    arguments.size()) {
                fail(RuntimeErrorCode::ArrayIndexOutOfRange, callSite);
                succeeded = false;
                break;
            }
            for (std::size_t index = 0; index < arguments.size(); ++index) {
                if (!context_.consumeStep(callSite)) {
                    succeeded = false;
                    break;
                }
            }
            if (!succeeded) break;
            try {
                object->elements.resize(oldLength + arguments.size());
                for (std::size_t index = 0; index < arguments.size(); ++index) {
                    object->elements[oldLength + index] = arguments[index];
                }
            } catch (const std::bad_alloc&) {
                fail(RuntimeErrorCode::AllocationFailure, callSite);
                succeeded = false;
                break;
            }
            context_.totalArrayElements_ += arguments.size();
            result = Value::number(static_cast<double>(object->elements.size()));
            break;
        }
        case RuntimeContext::NativeFunctionId::ArrayPop: {
            RuntimeContext::RuntimeObject* object = receiver.isObject()
                ? context_.objectAt(receiver.objectId()) : nullptr;
            if (object == nullptr || !object->array) {
                fail(RuntimeErrorCode::InvalidReceiver, callSite);
                succeeded = false;
                break;
            }
            if (object->elements.empty()) {
                result = Value::undefined();
                break;
            }
            result = object->elements.back();
            object->elements.pop_back();
            --context_.totalArrayElements_;
            break;
        }
        case RuntimeContext::NativeFunctionId::MathAbs: {
            double number = 0.0;
            succeeded = numericArgument(0, number);
            if (succeeded) result = Value::number(std::fabs(number));
            break;
        }
        case RuntimeContext::NativeFunctionId::MathMin:
        case RuntimeContext::NativeFunctionId::MathMax: {
            const bool minimum = static_cast<RuntimeContext::NativeFunctionId>(
                function.nativeFunction) == RuntimeContext::NativeFunctionId::MathMin;
            double best = minimum ? std::numeric_limits<double>::infinity()
                : -std::numeric_limits<double>::infinity();
            for (std::size_t index = 0; index < arguments.size(); ++index) {
                double number = 0.0;
                if (!numericArgument(index, number)) {
                    succeeded = false;
                    break;
                }
                if (std::isnan(number)) {
                    best = number;
                    break;
                }
                best = minimum ? std::min(best, number) : std::max(best, number);
            }
            if (succeeded) result = Value::number(best);
            break;
        }
        case RuntimeContext::NativeFunctionId::MathFloor:
        case RuntimeContext::NativeFunctionId::MathCeil:
        case RuntimeContext::NativeFunctionId::MathRound: {
            double number = 0.0;
            if (!numericArgument(0, number)) {
                succeeded = false;
                break;
            }
            const RuntimeContext::NativeFunctionId native =
                static_cast<RuntimeContext::NativeFunctionId>(
                    function.nativeFunction);
            result = Value::number(native ==
                RuntimeContext::NativeFunctionId::MathFloor ? std::floor(number) :
                native == RuntimeContext::NativeFunctionId::MathCeil
                    ? std::ceil(number) : std::round(number));
            break;
        }
        case RuntimeContext::NativeFunctionId::EventStopPropagation:
        case RuntimeContext::NativeFunctionId::EventStopImmediatePropagation:
            if (!receiver.isObject() ||
                receiver.objectId() != context_.eventObject_ ||
                context_.objectAt(receiver.objectId()) == nullptr) {
                fail(RuntimeErrorCode::InvalidReceiver, callSite);
                succeeded = false;
                break;
            }
            // A retained Event can still call its method after dispatch, but
            // it must not set state that a later click might observe.
            if (context_.eventDispatchActive_) {
                context_.eventPropagationStopped_ = true;
                if (static_cast<RuntimeContext::NativeFunctionId>(
                        function.nativeFunction) ==
                    RuntimeContext::NativeFunctionId::EventStopImmediatePropagation) {
                    context_.eventImmediatePropagationStopped_ = true;
                }
            }
            result = Value::undefined();
            break;
        default:
            fail(RuntimeErrorCode::InvalidNativeFunction, callSite);
            succeeded = false;
            break;
        }

        unwindFrame(currentEnvironment_, callerThis, previousFrameCount);
        return succeeded;
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
        case ValueType::Function:
            number = std::numeric_limits<double>::quiet_NaN();
            return true;
        case ValueType::Object:
            number = std::numeric_limits<double>::quiet_NaN();
            return true;
        case ValueType::HostObject:
            number = std::numeric_limits<double>::quiet_NaN();
            return true;
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
        case ValueType::Function:
            fail(RuntimeErrorCode::InvalidOperandType, SourceLocation());
            return false;
        case ValueType::Object:
            fail(RuntimeErrorCode::InvalidOperandType, SourceLocation());
            return false;
        case ValueType::HostObject:
            fail(RuntimeErrorCode::InvalidOperandType, SourceLocation());
            return false;
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
        const AstNodeKind leftKind = context_.ast_.node(node.left).kind;
        if (leftKind != AstNodeKind::Identifier &&
            leftKind != AstNodeKind::MemberExpression) {
            fail(RuntimeErrorCode::InvalidAssignmentTarget, node.location);
            return false;
        }

        if (leftKind == AstNodeKind::MemberExpression) {
            MemberReference reference;
            if (!resolveMember(node.left, reference, true)) return false;
            Value right;
            if (node.assignmentOperator == AstAssignmentOperator::Assign) {
                if (!evalExpression(node.right, right) ||
                    !writeMember(reference, right)) return false;
                result = right;
                return true;
            }
            Value left;
            if (!readMember(reference, left) ||
                !evalExpression(node.right, right)) return false;
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
            return writeMember(reference, result);
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

        const Value* current = lookup(name);
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
        const AstNodeKind argumentKind = context_.ast_.node(node.argument).kind;
        if (argumentKind != AstNodeKind::Identifier &&
            argumentKind != AstNodeKind::MemberExpression) {
            fail(RuntimeErrorCode::InvalidAssignmentTarget, node.location);
            return false;
        }
        if (argumentKind == AstNodeKind::MemberExpression) {
            MemberReference reference;
            if (!resolveMember(node.argument, reference, true)) return false;
            Value old;
            if (!readMember(reference, old)) return false;
            double number = 0.0;
            if (!toNumber(old, number)) return false;
            const double updated = node.updateOperator == AstUpdateOperator::Increment
                ? number + 1.0 : number - 1.0;
            const Value newValue = Value::number(updated);
            if (!writeMember(reference, newValue)) return false;
            result = node.prefix ? newValue : old;
            return true;
        }
        SourceView name;
        if (!identifierName(node.argument, name)) return false;
        const Value* current = lookup(name);
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

    bool executeStatement(AstNodeId id, bool inLoop, bool inFunction,
        Control& control)
    {
        if (!beginNode(id)) return false;
        const AstNode& node = context_.ast_.node(id);
        switch (node.kind) {
        case AstNodeKind::Program:
        case AstNodeKind::BlockStatement:
            for (std::size_t index = 0; index < node.childCount; ++index) {
                const AstNodeId child = context_.ast_.childAt(id, index);
                if (!validNode(child, node.location)) return false;
                if (!executeStatement(child, inLoop, inFunction, control)) return false;
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
                    if (!assign(name, value, declarator.location)) {
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
            if (truthy(test)) {
                return executeStatement(node.consequent, inLoop, inFunction,
                    control);
            }
            if (node.alternate == kInvalidAstNodeId) return true;
            if (!validNode(node.alternate, node.location)) return false;
            return executeStatement(node.alternate, inLoop, inFunction,
                control);
        }
        case AstNodeKind::WhileStatement: {
            if (!validNode(node.test, node.location) ||
                !validNode(node.body, node.location)) return false;
            while (true) {
                Value test;
                if (!evalExpression(node.test, test)) return false;
                if (!truthy(test)) return true;
                Control bodyControl;
                if (!executeStatement(node.body, true, inFunction,
                    bodyControl)) return false;
                if (bodyControl.kind == ControlKind::Break) return true;
                if (bodyControl.kind == ControlKind::Return) {
                    control = bodyControl;
                    return true;
                }
                if (bodyControl.kind == ControlKind::Continue) continue;
            }
        }
        case AstNodeKind::ForStatement: {
            if (node.init != kInvalidAstNodeId) {
                if (!validNode(node.init, node.location)) return false;
                Control initControl;
                if (context_.ast_.node(node.init).kind == AstNodeKind::VariableDeclaration) {
                    if (!executeStatement(node.init, inLoop, inFunction,
                        initControl)) return false;
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
                if (!executeStatement(node.body, true, inFunction,
                    bodyControl)) return false;
                if (bodyControl.kind == ControlKind::Break) return true;
                if (bodyControl.kind == ControlKind::Return) {
                    control = bodyControl;
                    return true;
                }
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
            if (!inFunction) {
                fail(RuntimeErrorCode::IllegalReturn, node.location);
                return false;
            }
            control.value = Value::undefined();
            if (node.expression != kInvalidAstNodeId) {
                if (!validNode(node.expression, node.location) ||
                    !evalExpression(node.expression, control.value)) return false;
            }
            control.kind = ControlKind::Return;
            return true;
        case AstNodeKind::FunctionDeclaration:
            // Direct declarations were installed by declaration
            // instantiation before statement execution.
            return true;
        case AstNodeKind::FunctionExpression:
            fail(RuntimeErrorCode::InvalidAstState, node.location);
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
        case ValueType::Function:
            return true;
        case ValueType::Object:
            return true;
        case ValueType::HostObject:
            return true;
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
        case ValueType::Function:
            return left.functionId() != kInvalidRuntimeFunctionId &&
                left.functionId() == right.functionId();
        case ValueType::Object:
            return left.objectId() != kInvalidRuntimeObjectId &&
                left.objectId() == right.objectId();
        case ValueType::HostObject:
            return left.hostObjectId() != kInvalidRuntimeHostObjectId &&
                left.hostObjectId() == right.hostObjectId();
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
    EnvironmentId currentEnvironment_ = kGlobalEnvironmentId;
    Value currentThis_ = Value::undefined();
    std::vector<CallFrame> frames_;
};

bool RuntimeContext::invokeFunctionForTesting(const Value& function,
    const std::vector<Value>& arguments, Value& result, RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    if (!function.isFunction()) {
        error = RuntimeErrorCode::NotCallable;
        setRuntimeError(error, SourceLocation());
        return false;
    }
    Evaluator evaluator(*this);
    return evaluator.invokeForTesting(function.functionId(), arguments, result,
        error);
}

bool RuntimeContext::invokeFunctionInSameRealm(const Value& function,
    const std::vector<Value>& arguments, Value& result, RuntimeErrorCode& error)
{
    result_ = ScriptResult();
    finalValue_ = Value::undefined();
    activeCallFrames_ = 0;
    const bool succeeded = invokeFunctionForTesting(function, arguments, result,
        error);
    if (!succeeded) result_.status = ScriptStatus::RuntimeFailure;
    return succeeded;
}

bool RuntimeContext::createOrUpdateEventObject(SourceView type,
    const HostObjectReference& target,
    const HostObjectReference& currentTarget, Value& result,
    RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    if (type.length == 0 ||
        (type.data == nullptr && type.length != 0)) {
        error = RuntimeErrorCode::InvalidHostReturn;
        return false;
    }

    RuntimeHostObjectId targetObject = kInvalidRuntimeHostObjectId;
    if (!createHostObject(target, targetObject, error, true)) return false;
    RuntimeHostObjectId currentTargetObject = kInvalidRuntimeHostObjectId;
    if (!createHostObject(currentTarget, currentTargetObject, error, true))
        return false;

    if (eventObject_ == kInvalidRuntimeObjectId ||
        objectAt(eventObject_) == nullptr) {
        Value typeValue;
        if (!createString(type, typeValue, error)) return false;
        const std::vector<Value> noElements;
        if (!createObject(false, noElements, eventObject_, error)) return false;
        if (!createNativeFunction(NativeFunctionId::EventStopPropagation,
                eventStopPropagationFunction_, error)) {
            eventObject_ = kInvalidRuntimeObjectId;
            eventStopPropagationFunction_ = kInvalidRuntimeFunctionId;
            eventStopImmediatePropagationFunction_ = kInvalidRuntimeFunctionId;
            return false;
        }
        if (!createNativeFunction(NativeFunctionId::EventStopImmediatePropagation,
                eventStopImmediatePropagationFunction_, error)) {
            eventObject_ = kInvalidRuntimeObjectId;
            eventStopPropagationFunction_ = kInvalidRuntimeFunctionId;
            eventStopImmediatePropagationFunction_ = kInvalidRuntimeFunctionId;
            return false;
        }
        if (!writeProperty(eventObject_, "type", typeValue, error, true) ||
            !writeProperty(eventObject_, "target",
                Value::hostObject(targetObject), error, true) ||
            !writeProperty(eventObject_, "currentTarget",
                Value::hostObject(currentTargetObject), error, true) ||
            !writeProperty(eventObject_, "stopPropagation",
                Value::function(eventStopPropagationFunction_), error, true) ||
            !writeProperty(eventObject_, "stopImmediatePropagation",
                Value::function(eventStopImmediatePropagationFunction_), error,
                true)) {
            eventObject_ = kInvalidRuntimeObjectId;
            eventStopPropagationFunction_ = kInvalidRuntimeFunctionId;
            eventStopImmediatePropagationFunction_ = kInvalidRuntimeFunctionId;
            return false;
        }
    } else if (!updateExistingProperty(eventObject_, "target",
            Value::hostObject(targetObject), error) ||
        !updateExistingProperty(eventObject_, "currentTarget",
            Value::hostObject(currentTargetObject), error)) {
        return false;
    }

    result = Value::object(eventObject_);
    return true;
}

void RuntimeContext::beginEventDispatch()
{
    eventDispatchActive_ = true;
    eventPropagationStopped_ = false;
    eventImmediatePropagationStopped_ = false;
}

void RuntimeContext::endEventDispatch()
{
    eventDispatchActive_ = false;
    eventPropagationStopped_ = false;
    eventImmediatePropagationStopped_ = false;
}

RuntimeContext::RuntimeContext(RuntimeLimits limits)
    : limits_(limits),
      ast_(),
      environment_(EnvironmentLimits{limits.maxBindings,
          limits.maxBindingNameLength})
{
    reset();
}

void RuntimeContext::clearRuntimeState()
{
    ast_.reset();
    sourceStorage_.clear();
    realmSourceStorage_.clear();
    realmSourceLineCount_ = 0;
    environment_.reset();
    environments_.clear();
    functions_.clear();
    strings_.clear();
    objects_.clear();
    totalStringBytes_ = 0;
    totalPropertyCount_ = 0;
    totalPropertyKeyBytes_ = 0;
    totalArrayElements_ = 0;
    userFunctionCount_ = 0;
    nativeFunctionCount_ = 0;
    hostMethodCount_ = 0;
    executionSteps_ = 0;
    finalValue_ = Value::undefined();
    activeCallFrames_ = 0;
    objectPrototype_ = kInvalidRuntimeObjectId;
    arrayPrototype_ = kInvalidRuntimeObjectId;
    mathObject_ = kInvalidRuntimeObjectId;
    eventObject_ = kInvalidRuntimeObjectId;
    eventStopPropagationFunction_ = kInvalidRuntimeFunctionId;
    eventStopImmediatePropagationFunction_ = kInvalidRuntimeFunctionId;
    builtInsInitialized_ = false;
    eventDispatchActive_ = false;
    eventPropagationStopped_ = false;
    eventImmediatePropagationStopped_ = false;
    hostObjects_.clear();
    hostMethods_.clear();
    hostGlobalSpecs_.clear();
    liveHostObjectCount_ = 0;
    hostOperations_ = 0;
    hostCallActive_ = false;
}

void RuntimeContext::reset()
{
    const bool advanced = advanceHostGeneration(result_.runtimeError.code);
    clearRuntimeState();
    result_ = ScriptResult();

    if (!advanced) {
        setRuntimeError(RuntimeErrorCode::HostGenerationLimitExceeded,
            SourceLocation());
        return;
    }

    RuntimeErrorCode error = RuntimeErrorCode::None;
    if (!initializeBuiltIns(error)) {
        // Initialization is transactional from the caller's perspective:
        // a context with insufficient limits has no partially usable globals
        // or prototype objects after reset.
        clearRuntimeState();
        setRuntimeError(RuntimeErrorCode::BuiltInInitializationFailed,
            SourceLocation());
    }
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

bool RuntimeContext::createEnvironment(EnvironmentId parent,
    EnvironmentId& result, RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    if (environmentAt(parent) == nullptr) {
        error = RuntimeErrorCode::InvalidFunction;
        return false;
    }
    if (environmentCount() >= limits_.maxEnvironments ||
        environments_.size() >=
            static_cast<std::size_t>(kInvalidEnvironmentId - 1u)) {
        error = RuntimeErrorCode::EnvironmentLimitExceeded;
        return false;
    }
    try {
        environments_.emplace_back(
            EnvironmentLimits{limits_.maxFunctionEnvironmentBindings,
                limits_.maxBindingNameLength}, parent);
    } catch (const std::bad_alloc&) {
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    }
    result = static_cast<EnvironmentId>(environments_.size());
    return true;
}

Environment* RuntimeContext::environmentAt(EnvironmentId id)
{
    if (id == kGlobalEnvironmentId) return &environment_;
    if (id == kInvalidEnvironmentId || id == 0u) return nullptr;
    const std::size_t index = static_cast<std::size_t>(id - 1u);
    return index < environments_.size() ? &environments_[index] : nullptr;
}

const Environment* RuntimeContext::environmentAt(EnvironmentId id) const
{
    if (id == kGlobalEnvironmentId) return &environment_;
    if (id == kInvalidEnvironmentId || id == 0u) return nullptr;
    const std::size_t index = static_cast<std::size_t>(id - 1u);
    return index < environments_.size() ? &environments_[index] : nullptr;
}

bool RuntimeContext::createFunction(AstNodeId declaration,
    EnvironmentId closure, RuntimeFunctionId& result, RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    if (declaration == kInvalidAstNodeId || declaration >= ast_.nodeCount() ||
        (ast_.node(declaration).kind != AstNodeKind::FunctionDeclaration &&
            ast_.node(declaration).kind != AstNodeKind::FunctionExpression) ||
        environmentAt(closure) == nullptr) {
        error = RuntimeErrorCode::InvalidFunction;
        return false;
    }
    if (userFunctionCount_ >= limits_.maxFunctions ||
        functions_.size() >=
            static_cast<std::size_t>(kInvalidRuntimeFunctionId)) {
        error = RuntimeErrorCode::FunctionLimitExceeded;
        return false;
    }
    try {
        FunctionRecord function;
        function.kind = FunctionRecord::Kind::User;
        function.declaration = declaration;
        function.closureEnvironment = closure;
        functions_.push_back(function);
    } catch (const std::bad_alloc&) {
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    }
    ++userFunctionCount_;
    result = static_cast<RuntimeFunctionId>(functions_.size() - 1u);
    return true;
}

bool RuntimeContext::createNativeFunction(NativeFunctionId native,
    RuntimeFunctionId& result, RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    if (nativeFunctionCount_ >= limits_.maxNativeFunctions ||
        functions_.size() >=
            static_cast<std::size_t>(kInvalidRuntimeFunctionId)) {
        error = RuntimeErrorCode::NativeFunctionLimitExceeded;
        return false;
    }
    try {
        FunctionRecord function;
        function.kind = FunctionRecord::Kind::Native;
        function.nativeFunction = static_cast<std::uint8_t>(native);
        functions_.push_back(function);
    } catch (const std::bad_alloc&) {
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    }
    ++nativeFunctionCount_;
    result = static_cast<RuntimeFunctionId>(functions_.size() - 1u);
    return true;
}

bool RuntimeContext::createHostMethod(RuntimeHostObjectId object,
    std::uint32_t method, bool requiresReceiver, bool sharedAcrossReceivers,
    RuntimeFunctionId& result, RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    const RuntimeHostObjectId cacheOwner = sharedAcrossReceivers
        ? kInvalidRuntimeHostObjectId : object;
    for (const HostMethodRecord& record : hostMethods_) {
        if (record.hostObject == cacheOwner && record.method == method &&
            record.requiresReceiver == requiresReceiver) {
            result = record.function;
            return true;
        }
    }
    if (hostMethodCount_ >= limits_.maxHostMethodValues ||
        functions_.size() >= static_cast<std::size_t>(kInvalidRuntimeFunctionId)) {
        error = RuntimeErrorCode::HostMethodLimitExceeded;
        return false;
    }
    try {
        FunctionRecord function;
        function.kind = FunctionRecord::Kind::Host;
        function.hostObject = cacheOwner;
        function.hostMethod = method;
        function.hostMethodRequiresReceiver = requiresReceiver;
        functions_.push_back(function);

        HostMethodRecord record;
        record.hostObject = cacheOwner;
        record.method = method;
        record.requiresReceiver = requiresReceiver;
        record.function = static_cast<RuntimeFunctionId>(functions_.size() - 1u);
        hostMethods_.push_back(record);
    } catch (const std::bad_alloc&) {
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    }
    ++hostMethodCount_;
    result = static_cast<RuntimeFunctionId>(functions_.size() - 1u);
    return true;
}

const RuntimeContext::FunctionRecord* RuntimeContext::functionAt(
    RuntimeFunctionId id) const
{
    if (id == kInvalidRuntimeFunctionId || id >= functions_.size()) {
        return nullptr;
    }
    return &functions_[id];
}

bool RuntimeContext::createObject(bool array,
    const std::vector<Value>& initialElements, RuntimeObjectId& result,
    RuntimeErrorCode& error, RuntimeObjectId prototype)
{
    error = RuntimeErrorCode::None;
    if (objects_.size() >= limits_.maxObjects ||
        objects_.size() >= static_cast<std::size_t>(kInvalidRuntimeObjectId)) {
        error = RuntimeErrorCode::ObjectLimitExceeded;
        return false;
    }
    if (!array && !initialElements.empty()) {
        error = RuntimeErrorCode::InvalidAstState;
        return false;
    }
    if (array && (initialElements.size() > limits_.maxArrayElements ||
        initialElements.size() > limits_.maxTotalArrayElements ||
        totalArrayElements_ > limits_.maxTotalArrayElements -
            initialElements.size())) {
        error = RuntimeErrorCode::ArrayLimitExceeded;
        return false;
    }
    try {
        RuntimeObject object;
        object.array = array;
        object.prototype = prototype;
        if (object.prototype == kInvalidRuntimeObjectId &&
            builtInsInitialized_) {
            object.prototype = array ? arrayPrototype_ : objectPrototype_;
        }
        if (array) object.elements = initialElements;
        objects_.push_back(std::move(object));
    } catch (const std::bad_alloc&) {
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    }
    result = static_cast<RuntimeObjectId>(objects_.size() - 1u);
    if (array) totalArrayElements_ += initialElements.size();
    return true;
}

bool RuntimeContext::initializeBuiltIns(RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    const std::vector<Value> noElements;

    if (!createObject(false, noElements, objectPrototype_, error,
        kInvalidRuntimeObjectId)) return false;
    if (!createObject(false, noElements, arrayPrototype_, error,
        objectPrototype_)) return false;
    if (!createObject(false, noElements, mathObject_, error,
        objectPrototype_)) return false;

    const auto install = [this](RuntimeObjectId object, const char* name,
        NativeFunctionId native, RuntimeErrorCode& installError) {
        RuntimeFunctionId function = kInvalidRuntimeFunctionId;
        if (!createNativeFunction(native, function, installError)) return false;
        return writeProperty(object, std::string(name),
            Value::function(function), installError);
    };

    if (!install(objectPrototype_, "hasOwnProperty",
        NativeFunctionId::ObjectHasOwnProperty, error)) return false;
    if (!install(arrayPrototype_, "push", NativeFunctionId::ArrayPush, error) ||
        !install(arrayPrototype_, "pop", NativeFunctionId::ArrayPop, error)) {
        return false;
    }
    if (!install(mathObject_, "abs", NativeFunctionId::MathAbs, error) ||
        !install(mathObject_, "min", NativeFunctionId::MathMin, error) ||
        !install(mathObject_, "max", NativeFunctionId::MathMax, error) ||
        !install(mathObject_, "floor", NativeFunctionId::MathFloor, error) ||
        !install(mathObject_, "ceil", NativeFunctionId::MathCeil, error) ||
        !install(mathObject_, "round", NativeFunctionId::MathRound, error)) {
        return false;
    }

    const char mathName[] = "Math";
    EnvironmentError environmentError;
    if (!environment_.declare(
        SourceView(mathName, sizeof(mathName) - 1u), Value::object(mathObject_),
        environmentError)) {
        error = RuntimeErrorCode::BuiltInInitializationFailed;
        return false;
    }
    builtInsInitialized_ = true;
    return true;
}

RuntimeContext::RuntimeObject* RuntimeContext::objectAt(RuntimeObjectId id)
{
    if (id == kInvalidRuntimeObjectId || id >= objects_.size()) return nullptr;
    return &objects_[id];
}

const RuntimeContext::RuntimeObject* RuntimeContext::objectAt(
    RuntimeObjectId id) const
{
    if (id == kInvalidRuntimeObjectId || id >= objects_.size()) return nullptr;
    return &objects_[id];
}

RuntimeObjectId RuntimeContext::prototypeOf(RuntimeObjectId object) const
{
    const RuntimeObject* record = objectAt(object);
    return record == nullptr ? kInvalidRuntimeObjectId : record->prototype;
}

bool RuntimeContext::setPrototypeForTesting(RuntimeObjectId object,
    RuntimeObjectId prototype)
{
    RuntimeObject* record = objectAt(object);
    if (record == nullptr) return false;
    if (prototype != kInvalidRuntimeObjectId && objectAt(prototype) == nullptr) {
        return false;
    }
    record->prototype = prototype;
    return true;
}

bool RuntimeContext::readPropertyForTesting(RuntimeObjectId object,
    const std::string& key, Value& value, RuntimeErrorCode& error)
{
    return readProperty(object, key, value, error, SourceLocation());
}

bool RuntimeContext::readProperty(RuntimeObjectId objectId,
    const std::string& key, Value& value, RuntimeErrorCode& error,
    SourceLocation location)
{
    error = RuntimeErrorCode::None;
    if (key.size() > limits_.maxPropertyNameLength) {
        error = RuntimeErrorCode::PropertyNameTooLong;
        return false;
    }
    RuntimeObjectId current = objectId;
    for (std::size_t depth = 0; depth < limits_.maxPrototypeDepth; ++depth) {
        if (!consumeStep(location)) {
            error = RuntimeErrorCode::ExecutionBudgetExceeded;
            return false;
        }
        const RuntimeObject* object = objectAt(current);
        if (object == nullptr) {
            error = RuntimeErrorCode::CannotReadProperty;
            return false;
        }
        if (object->array && key == "length") {
            value = Value::number(static_cast<double>(object->elements.size()));
            return true;
        }
        if (object->array && isCanonicalArrayIndexSpelling(key)) {
            std::size_t index = 0;
            if (!parseBoundedArrayIndex(key, limits_.maxArrayIndex, index)) {
                error = RuntimeErrorCode::ArrayIndexOutOfRange;
                return false;
            }
            if (index < object->elements.size()) {
                value = object->elements[index];
                return true;
            }
        }
        for (const RuntimeProperty& property : object->properties) {
            if (property.key == key) {
                value = property.value;
                return true;
            }
        }
        if (object->prototype == kInvalidRuntimeObjectId) {
            value = Value::undefined();
            return true;
        }
        current = object->prototype;
    }
    error = RuntimeErrorCode::PrototypeChainExceeded;
    return false;
}

bool RuntimeContext::writeProperty(RuntimeObjectId objectId,
    const std::string& key, Value value, RuntimeErrorCode& error,
    bool readOnly)
{
    error = RuntimeErrorCode::None;
    RuntimeObject* object = objectAt(objectId);
    if (object == nullptr) {
        error = RuntimeErrorCode::CannotWriteProperty;
        return false;
    }
    if (key.size() > limits_.maxPropertyNameLength) {
        error = RuntimeErrorCode::PropertyNameTooLong;
        return false;
    }
    if (object->array && key == "length") {
        error = RuntimeErrorCode::CannotWriteProperty;
        return false;
    }
    if (object->array && isCanonicalArrayIndexSpelling(key)) {
        std::size_t index = 0;
        if (!parseBoundedArrayIndex(key, limits_.maxArrayIndex, index)) {
            error = RuntimeErrorCode::ArrayIndexOutOfRange;
            return false;
        }
        if (index >= limits_.maxArrayElements) {
            error = RuntimeErrorCode::ArrayLimitExceeded;
            return false;
        }
        const std::size_t newLength = index + 1u;
        if (newLength > object->elements.size()) {
            const std::size_t growth = newLength - object->elements.size();
            if (growth > limits_.maxTotalArrayElements ||
                totalArrayElements_ > limits_.maxTotalArrayElements - growth) {
                error = RuntimeErrorCode::ArrayLimitExceeded;
                return false;
            }
            try {
                object->elements.resize(newLength, Value::undefined());
            } catch (const std::bad_alloc&) {
                error = RuntimeErrorCode::AllocationFailure;
                return false;
            }
            totalArrayElements_ += growth;
        }
        object->elements[index] = value;
        return true;
    }

    for (RuntimeProperty& property : object->properties) {
        if (property.key == key) {
            // Host-created Event properties are immutable from script. A
            // non-strict assignment is a deterministic no-op; the host can
            // still refresh the cached handle values through the dedicated
            // updateExistingProperty path below.
            if (property.readOnly) return true;
            property.value = value;
            return true;
        }
    }
    if (object->properties.size() >= limits_.maxPropertiesPerObject ||
        totalPropertyCount_ >= limits_.maxTotalProperties) {
        error = RuntimeErrorCode::PropertyLimitExceeded;
        return false;
    }
    if (key.size() > limits_.maxTotalPropertyKeyBytes ||
        totalPropertyKeyBytes_ > limits_.maxTotalPropertyKeyBytes - key.size()) {
        error = RuntimeErrorCode::PropertyLimitExceeded;
        return false;
    }
    try {
        RuntimeProperty property;
        property.key = key;
        property.value = value;
        property.readOnly = readOnly;
        object->properties.push_back(std::move(property));
    } catch (const std::bad_alloc&) {
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    }
    ++totalPropertyCount_;
    totalPropertyKeyBytes_ += key.size();
    return true;
}

bool RuntimeContext::updateExistingProperty(RuntimeObjectId objectId,
    const std::string& key, Value value, RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    RuntimeObject* object = objectAt(objectId);
    if (object == nullptr) {
        error = RuntimeErrorCode::CannotWriteProperty;
        return false;
    }
    for (RuntimeProperty& property : object->properties) {
        if (property.key == key) {
            property.value = value;
            return true;
        }
    }
    error = RuntimeErrorCode::CannotWriteProperty;
    return false;
}

RuntimeErrorCode RuntimeContext::mapHostResult(HostResultCode code) const
{
    switch (code) {
    case HostResultCode::Success: return RuntimeErrorCode::None;
    case HostResultCode::PropertyNotFound:
        return RuntimeErrorCode::HostPropertyNotFound;
    case HostResultCode::PropertyReadOnly:
        return RuntimeErrorCode::HostPropertyReadOnly;
    case HostResultCode::PropertyWriteFailed:
        return RuntimeErrorCode::HostPropertyWriteFailed;
    case HostResultCode::InvalidObject:
        return RuntimeErrorCode::InvalidHostObject;
    case HostResultCode::StaleObject:
        return RuntimeErrorCode::StaleHostObject;
    case HostResultCode::CallFailed:
        return RuntimeErrorCode::HostCallFailed;
    case HostResultCode::ReentryUnsupported:
        return RuntimeErrorCode::HostReentryUnsupported;
    case HostResultCode::InvalidReturn:
        return RuntimeErrorCode::InvalidHostReturn;
    case HostResultCode::InvalidValue:
        return RuntimeErrorCode::HostInvalidValue;
    case HostResultCode::DocumentLookupLimitExceeded:
        return RuntimeErrorCode::DocumentLookupLimitExceeded;
    case HostResultCode::DocumentTextLimitExceeded:
        return RuntimeErrorCode::DocumentTextLimitExceeded;
    case HostResultCode::DocumentMutationLimitExceeded:
        return RuntimeErrorCode::DocumentMutationLimitExceeded;
    case HostResultCode::CallbackLimitExceeded:
        return RuntimeErrorCode::HostCallbackLimitExceeded;
    }
    return RuntimeErrorCode::HostCallFailed;
}

bool RuntimeContext::consumeHostOperation(SourceLocation location)
{
    if (hostOperations_ >= limits_.maxHostOperations) {
        setRuntimeError(RuntimeErrorCode::HostOperationBudgetExceeded, location);
        return false;
    }
    ++hostOperations_;
    return true;
}

bool RuntimeContext::resolveHostObject(RuntimeHostObjectId object,
    HostObjectReference& reference, RuntimeErrorCode& error) const
{
    error = RuntimeErrorCode::None;
    if (object == kInvalidRuntimeHostObjectId) {
        error = RuntimeErrorCode::InvalidHostObject;
        return false;
    }
    if (hostObjectGeneration(object) != hostGeneration_) {
        error = RuntimeErrorCode::StaleHostObject;
        return false;
    }
    const std::size_t slot = hostObjectSlot(object);
    if (slot >= hostObjects_.size()) {
        error = RuntimeErrorCode::InvalidHostObject;
        return false;
    }
    const HostObjectRecord& record = hostObjects_[slot];
    if (!record.live || record.reference.generation != hostGeneration_) {
        error = RuntimeErrorCode::StaleHostObject;
        return false;
    }
    reference = record.reference;
    return true;
}

bool RuntimeContext::validateAdapterReference(
    const HostObjectReference& reference, RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    if (hostAdapter_ == nullptr) {
        error = RuntimeErrorCode::InvalidHostObject;
        return false;
    }
    if (hostCallActive_) {
        error = RuntimeErrorCode::HostReentryUnsupported;
        return false;
    }

    HostResult result;
    hostCallActive_ = true;
    try {
        result = hostAdapter_->validate(reference);
    } catch (const std::bad_alloc&) {
        hostCallActive_ = false;
        error = RuntimeErrorCode::HostCallFailed;
        return false;
    } catch (...) {
        hostCallActive_ = false;
        error = RuntimeErrorCode::HostCallFailed;
        return false;
    }
    hostCallActive_ = false;
    if (!result.succeeded()) {
        error = mapHostResult(result.code);
        return false;
    }
    return true;
}

bool RuntimeContext::createHostObject(const HostObjectReference& input,
    RuntimeHostObjectId& result, RuntimeErrorCode& error, bool validateAdapter)
{
    error = RuntimeErrorCode::None;
    if (!input.valid() || input.generation != hostGeneration_) {
        error = input.valid() ? RuntimeErrorCode::StaleHostObject
            : RuntimeErrorCode::InvalidHostObject;
        return false;
    }
    if (!consumeHostOperation(SourceLocation())) return false;
    if (validateAdapter && !validateAdapterReference(input, error)) return false;

    for (std::size_t index = 0; index < hostObjects_.size(); ++index) {
        const HostObjectRecord& record = hostObjects_[index];
        if (record.live && record.reference == input) {
            result = makeHostObjectId(index, hostGeneration_);
            return true;
        }
    }

    std::size_t slot = hostObjects_.size();
    for (std::size_t index = 0; index < hostObjects_.size(); ++index) {
        if (!hostObjects_[index].live) {
            slot = index;
            break;
        }
    }
    if (slot == hostObjects_.size() &&
        hostObjects_.size() >= limits_.maxHostObjects) {
        error = RuntimeErrorCode::HostObjectLimitExceeded;
        return false;
    }
    try {
        if (slot == hostObjects_.size()) hostObjects_.push_back(HostObjectRecord());
        hostObjects_[slot].live = true;
        hostObjects_[slot].reference = input;
    } catch (const std::bad_alloc&) {
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    }
    ++liveHostObjectCount_;
    result = makeHostObjectId(slot, hostGeneration_);
    return true;
}

bool RuntimeContext::registerHostObject(HostInstanceId instance,
    HostObjectKind kind, RuntimeHostObjectId& result, RuntimeErrorCode& error)
{
    HostObjectReference reference;
    reference.instanceId = instance;
    reference.generation = hostGeneration_;
    reference.kind = kind;
    return createHostObject(reference, result, error, true);
}

bool RuntimeContext::installHostGlobalInternal(const HostGlobalSpec& spec,
    RuntimeErrorCode& error, bool recordSpec)
{
    error = RuntimeErrorCode::None;
    if (spec.name.size() > limits_.maxBindingNameLength) {
        error = RuntimeErrorCode::BindingNameTooLong;
        return false;
    }
    RuntimeHostObjectId object = kInvalidRuntimeHostObjectId;
    if (!registerHostObject(spec.instance, spec.kind, object, error)) return false;
    const Value value = Value::hostObject(object);
    const SourceView name(spec.name.data(), spec.name.size());
    EnvironmentError environmentError;
    if (environment_.lookup(name) != nullptr) {
        if (!environment_.assign(name, value)) {
            error = RuntimeErrorCode::InvalidAstState;
            return false;
        }
    } else if (!environment_.declare(name, value, environmentError)) {
        switch (environmentError.code) {
        case EnvironmentErrorCode::BindingLimitExceeded:
            error = RuntimeErrorCode::BindingLimitExceeded;
            break;
        case EnvironmentErrorCode::BindingNameTooLong:
            error = RuntimeErrorCode::BindingNameTooLong;
            break;
        case EnvironmentErrorCode::AllocationFailure:
            error = RuntimeErrorCode::AllocationFailure;
            break;
        case EnvironmentErrorCode::None:
            error = RuntimeErrorCode::InvalidAstState;
            break;
        }
        return false;
    }

    if (recordSpec) {
        try {
            bool replaced = false;
            for (HostGlobalSpec& existing : hostGlobalSpecs_) {
                if (existing.name == spec.name) {
                    existing = spec;
                    replaced = true;
                    break;
                }
            }
            if (!replaced) hostGlobalSpecs_.push_back(spec);
        } catch (const std::bad_alloc&) {
            error = RuntimeErrorCode::AllocationFailure;
            return false;
        }
    }
    return true;
}

bool RuntimeContext::installHostGlobal(const std::string& name,
    HostInstanceId instance, HostObjectKind kind, RuntimeErrorCode& error)
{
    HostGlobalSpec spec;
    spec.name = name;
    spec.instance = instance;
    spec.kind = kind;
    return installHostGlobalInternal(spec, error, true);
}

bool RuntimeContext::advanceHostGeneration(RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    if (limits_.maxHostGenerations == 0 ||
        hostGeneration_ >= limits_.maxHostGenerations ||
        hostGeneration_ == std::numeric_limits<HostGenerationId>::max()) {
        error = RuntimeErrorCode::HostGenerationLimitExceeded;
        return false;
    }
    ++hostGeneration_;
    return true;
}

bool RuntimeContext::invalidateHostGeneration(RuntimeErrorCode& error)
{
    if (!advanceHostGeneration(error)) return false;
    for (HostObjectRecord& record : hostObjects_) record.live = false;
    liveHostObjectCount_ = 0;
    hostMethods_.clear();
    hostMethodCount_ = 0;
    hostOperations_ = 0;
    return true;
}

bool RuntimeContext::readHostProperty(RuntimeHostObjectId object,
    const std::string& key, Value& value, RuntimeErrorCode& error,
    SourceLocation location)
{
    error = RuntimeErrorCode::None;
    if (key.size() > limits_.maxHostPropertyNameLength) {
        error = RuntimeErrorCode::PropertyNameTooLong;
        return false;
    }
    if (!consumeHostOperation(location)) {
        error = RuntimeErrorCode::HostOperationBudgetExceeded;
        return false;
    }
    HostObjectReference reference;
    if (!resolveHostObject(object, reference, error)) return false;
    if (hostAdapter_ == nullptr) {
        error = RuntimeErrorCode::InvalidHostObject;
        return false;
    }
    if (hostCallActive_) {
        error = RuntimeErrorCode::HostReentryUnsupported;
        return false;
    }

    HostResult validation;
    HostResult operation;
    HostValue hostValue;
    hostCallActive_ = true;
    try {
        validation = hostAdapter_->validate(reference);
        if (validation.succeeded()) {
            operation = hostAdapter_->getProperty(reference,
                SourceView(key.data(), key.size()), hostValue);
        }
    } catch (const std::bad_alloc&) {
        hostCallActive_ = false;
        error = RuntimeErrorCode::HostCallFailed;
        return false;
    } catch (...) {
        hostCallActive_ = false;
        error = RuntimeErrorCode::HostCallFailed;
        return false;
    }
    hostCallActive_ = false;
    if (!validation.succeeded()) {
        error = mapHostResult(validation.code);
        return false;
    }
    if (!operation.succeeded()) {
        if (operation.code == HostResultCode::PropertyNotFound) {
            value = Value::undefined();
            return true;
        }
        error = mapHostResult(operation.code);
        return false;
    }
    return convertHostValue(hostValue, object, value, error, location);
}

bool RuntimeContext::writeHostProperty(RuntimeHostObjectId object,
    const std::string& key, Value value, RuntimeErrorCode& error,
    SourceLocation location)
{
    error = RuntimeErrorCode::None;
    if (key.size() > limits_.maxHostPropertyNameLength) {
        error = RuntimeErrorCode::PropertyNameTooLong;
        return false;
    }
    if (!consumeHostOperation(location)) {
        error = RuntimeErrorCode::HostOperationBudgetExceeded;
        return false;
    }
    HostObjectReference reference;
    if (!resolveHostObject(object, reference, error)) return false;
    HostValue hostValue;
    if (!convertValueToHost(value, hostValue, error, location)) return false;
    if (hostAdapter_ == nullptr) {
        error = RuntimeErrorCode::InvalidHostObject;
        return false;
    }
    if (hostCallActive_) {
        error = RuntimeErrorCode::HostReentryUnsupported;
        return false;
    }
    HostResult validation;
    HostResult operation;
    hostCallActive_ = true;
    try {
        validation = hostAdapter_->validate(reference);
        if (validation.succeeded()) {
            operation = hostAdapter_->setProperty(reference,
                SourceView(key.data(), key.size()), hostValue);
        }
    } catch (const std::bad_alloc&) {
        hostCallActive_ = false;
        error = RuntimeErrorCode::HostCallFailed;
        return false;
    } catch (...) {
        hostCallActive_ = false;
        error = RuntimeErrorCode::HostCallFailed;
        return false;
    }
    hostCallActive_ = false;
    if (!validation.succeeded()) {
        error = mapHostResult(validation.code);
        return false;
    }
    if (!operation.succeeded()) {
        error = mapHostResult(operation.code);
        return false;
    }
    return true;
}

bool RuntimeContext::convertValueToHost(const Value& value, HostValue& result,
    RuntimeErrorCode& error, SourceLocation location)
{
    error = RuntimeErrorCode::None;
    switch (value.type()) {
    case ValueType::Undefined:
        result = HostValue::undefined();
        return true;
    case ValueType::Null:
        result = HostValue::nullValue();
        return true;
    case ValueType::Boolean:
        result = HostValue::boolean(value.booleanValue());
        return true;
    case ValueType::Number:
        result = HostValue::number(value.numberValue());
        return true;
    case ValueType::String: {
        const std::string* text = stringData(value);
        if (text == nullptr) {
            error = RuntimeErrorCode::HostCallFailed;
            return false;
        }
        result = HostValue::string(SourceView(text->data(), text->size()));
        return true;
    }
    case ValueType::Object:
        if (objectAt(value.objectId()) == nullptr) {
            error = RuntimeErrorCode::HostCallFailed;
            return false;
        }
        result = HostValue::object(value.objectId());
        return true;
    case ValueType::HostObject: {
        HostObjectReference reference;
        if (!resolveHostObject(value.hostObjectId(), reference, error)) return false;
        result.type = HostValueType::HostObject;
        result.hostObject = reference;
        return true;
    }
    case ValueType::Function:
        if (functionAt(value.functionId()) == nullptr) {
            error = RuntimeErrorCode::InvalidFunction;
            return false;
        }
        result = HostValue::function(value.functionId());
        return true;
    }
    error = RuntimeErrorCode::HostCallFailed;
    (void)location;
    return false;
}

bool RuntimeContext::convertHostValue(const HostValue& value,
    RuntimeHostObjectId methodOwner, Value& result, RuntimeErrorCode& error,
    SourceLocation location)
{
    error = RuntimeErrorCode::None;
    switch (value.type) {
    case HostValueType::Undefined:
        result = Value::undefined();
        return true;
    case HostValueType::Null:
        result = Value::nullValue();
        return true;
    case HostValueType::Boolean:
        result = Value::boolean(value.booleanValue);
        return true;
    case HostValueType::Number:
        result = Value::number(value.numberValue);
        return true;
    case HostValueType::String: {
        if (value.stringValue.data == nullptr && value.stringValue.length != 0) {
            error = RuntimeErrorCode::InvalidHostReturn;
            return false;
        }
        if (!createString(value.stringValue, result, error)) return false;
        return true;
    }
    case HostValueType::Function:
        if (functionAt(value.functionId) == nullptr) {
            error = RuntimeErrorCode::InvalidHostReturn;
            return false;
        }
        result = Value::function(value.functionId);
        return true;
    case HostValueType::Object:
        if (objectAt(value.objectId) == nullptr) {
            error = RuntimeErrorCode::InvalidHostReturn;
            return false;
        }
        result = Value::object(value.objectId);
        return true;
    case HostValueType::HostObject: {
        if (!value.hostObject.valid() ||
            value.hostObject.generation != hostGeneration_) {
            error = RuntimeErrorCode::InvalidHostReturn;
            return false;
        }
        RuntimeHostObjectId object = kInvalidRuntimeHostObjectId;
        if (!createHostObject(value.hostObject, object, error, true)) return false;
        result = Value::hostObject(object);
        return true;
    }
    case HostValueType::Method: {
        if (!value.methodSharedAcrossReceivers &&
            methodOwner == kInvalidRuntimeHostObjectId) {
            error = RuntimeErrorCode::InvalidHostReturn;
            return false;
        }
        RuntimeFunctionId function = kInvalidRuntimeFunctionId;
        if (!createHostMethod(methodOwner, value.methodId,
            value.methodRequiresReceiver, value.methodSharedAcrossReceivers,
            function, error)) return false;
        result = Value::function(function);
        return true;
    }
    }
    error = RuntimeErrorCode::InvalidHostReturn;
    (void)location;
    return false;
}

bool RuntimeContext::invokeHostMethod(const FunctionRecord& function,
    const std::vector<Value>& arguments, Value receiver,
    SourceLocation location, Value& result)
{
    if (activeCallFrames_ >= limits_.maxCallDepth) {
        setRuntimeError(RuntimeErrorCode::CallDepthExceeded, location);
        return false;
    }
    HostObjectReference owner;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    if (function.hostObject != kInvalidRuntimeHostObjectId &&
        !resolveHostObject(function.hostObject, owner, error)) {
        setRuntimeError(error, location);
        return false;
    }

    const HostObjectReference* receiverReference = nullptr;
    HostObjectReference target = owner;
    if (function.hostMethodRequiresReceiver) {
        if (!receiver.isHostObject()) {
            setRuntimeError(RuntimeErrorCode::InvalidReceiver, location);
            return false;
        }
        if (function.hostObject != kInvalidRuntimeHostObjectId &&
            receiver.hostObjectId() != function.hostObject) {
            setRuntimeError(RuntimeErrorCode::InvalidReceiver, location);
            return false;
        }
        if (!resolveHostObject(receiver.hostObjectId(), target, error)) {
            setRuntimeError(error, location);
            return false;
        }
        receiverReference = &target;
    }
    if (arguments.size() > limits_.parser.maxCallArguments) {
        setRuntimeError(RuntimeErrorCode::HostCallFailed, location);
        return false;
    }
    std::vector<HostValue> hostArguments;
    try {
        hostArguments.reserve(arguments.size());
        for (const Value& argument : arguments) {
            HostValue hostArgument;
            if (!convertValueToHost(argument, hostArgument, error, location)) {
                setRuntimeError(error, location);
                return false;
            }
            hostArguments.push_back(hostArgument);
        }
    } catch (const std::bad_alloc&) {
        setRuntimeError(RuntimeErrorCode::AllocationFailure, location);
        return false;
    }
    if (!consumeHostOperation(location)) return false;
    if (hostAdapter_ == nullptr) {
        setRuntimeError(RuntimeErrorCode::InvalidHostObject, location);
        return false;
    }
    if (hostCallActive_) {
        setRuntimeError(RuntimeErrorCode::HostReentryUnsupported, location);
        return false;
    }

    ++activeCallFrames_;
    HostResult validation;
    HostResult operation;
    HostValue hostResult;
    hostCallActive_ = true;
    try {
        validation = hostAdapter_->validate(target);
        if (validation.succeeded()) {
            operation = hostAdapter_->call(receiverReference, function.hostMethod,
                hostArguments.empty() ? nullptr : hostArguments.data(),
                hostArguments.size(), hostResult);
        }
    } catch (const std::bad_alloc&) {
        hostCallActive_ = false;
        --activeCallFrames_;
        setRuntimeError(RuntimeErrorCode::HostCallFailed, location);
        return false;
    } catch (...) {
        hostCallActive_ = false;
        --activeCallFrames_;
        setRuntimeError(RuntimeErrorCode::HostCallFailed, location);
        return false;
    }
    hostCallActive_ = false;
    if (!validation.succeeded()) {
        --activeCallFrames_;
        setRuntimeError(mapHostResult(validation.code), location);
        return false;
    }
    if (!operation.succeeded()) {
        --activeCallFrames_;
        setRuntimeError(mapHostResult(operation.code), location);
        return false;
    }
    const bool converted = convertHostValue(hostResult, function.hostObject,
        result, error, location);
    --activeCallFrames_;
    if (!converted) {
        setRuntimeError(error, location);
        return false;
    }
    return true;
}

bool RuntimeContext::readHostPropertyForTesting(RuntimeHostObjectId object,
    const std::string& key, Value& value, RuntimeErrorCode& error)
{
    return readHostProperty(object, key, value, error, SourceLocation());
}

bool RuntimeContext::writeHostPropertyForTesting(RuntimeHostObjectId object,
    const std::string& key, Value value, RuntimeErrorCode& error)
{
    return writeHostProperty(object, key, value, error, SourceLocation());
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
    const std::vector<HostGlobalSpec> hostGlobals = hostGlobalSpecs_;
    reset();
    if (!builtInsInitialized_) return result_;
    for (const HostGlobalSpec& spec : hostGlobals) {
        RuntimeErrorCode hostError = RuntimeErrorCode::None;
        if (!installHostGlobalInternal(spec, hostError, false)) {
            setRuntimeError(hostError, SourceLocation());
            return result_;
        }
    }
    hostGlobalSpecs_ = hostGlobals;
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

ScriptResult RuntimeContext::executeInSameRealm(SourceView source)
{
    result_ = ScriptResult();
    finalValue_ = Value::undefined();
    activeCallFrames_ = 0;
    try {
        if (!builtInsInitialized_) {
            setRuntimeError(RuntimeErrorCode::BuiltInInitializationFailed,
                SourceLocation());
            return result_;
        }
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
        sourceStorage_.assign(source.data == nullptr ? "" : source.data,
            source.length);
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

        // If a caller used execute() once before entering a same-realm phase,
        // preserve that script's AST/source as the first realm segment.
        if (realmSourceStorage_.empty() && ast_.nodeCount() != 0) {
            realmSourceStorage_ = sourceStorage_;
            realmSourceLineCount_ = static_cast<std::size_t>(std::count(
                realmSourceStorage_.begin(), realmSourceStorage_.end(), '\n'));
            ast_.setSource(SourceView(realmSourceStorage_.data(),
                realmSourceStorage_.size()));
        }

        const bool hasPriorSource = !realmSourceStorage_.empty();
        const std::size_t separatorBytes = hasPriorSource ? 1u : 0u;
        if (realmSourceStorage_.size() > limits_.maxRealmSourceBytes ||
            separatorBytes > limits_.maxRealmSourceBytes -
                realmSourceStorage_.size() ||
            source.length > limits_.maxRealmSourceBytes -
                realmSourceStorage_.size() - separatorBytes) {
            setRuntimeError(RuntimeErrorCode::RealmSourceLimitExceeded,
                SourceLocation());
            return result_;
        }
        const std::size_t sourceOffset = realmSourceStorage_.size() +
            separatorBytes;
        const std::size_t lineOffset = realmSourceLineCount_ +
            (hasPriorSource ? 1u : 0u);
        if (hasPriorSource) realmSourceStorage_.push_back('\n');
        realmSourceStorage_ += sourceStorage_;
        realmSourceLineCount_ += static_cast<std::size_t>(std::count(
            sourceStorage_.begin(), sourceStorage_.end(), '\n')) +
            (hasPriorSource ? 1u : 0u);

        AstNodeId root = kInvalidAstNodeId;
        if (!ast_.appendFrom(parsed.ast, sourceOffset, lineOffset, root)) {
            setRuntimeError(RuntimeErrorCode::AllocationFailure,
                SourceLocation());
            return result_;
        }
        ast_.setSource(SourceView(realmSourceStorage_.data(),
            realmSourceStorage_.size()));
        if (!ast_.setRoot(root)) {
            setRuntimeError(RuntimeErrorCode::InvalidAstState,
                SourceLocation());
            return result_;
        }
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
    case RuntimeErrorCode::UnsupportedFunctionConstruct:
        return "UnsupportedFunctionConstruct";
    case RuntimeErrorCode::NotCallable: return "NotCallable";
    case RuntimeErrorCode::CallDepthExceeded: return "CallDepthExceeded";
    case RuntimeErrorCode::EnvironmentLimitExceeded:
        return "EnvironmentLimitExceeded";
    case RuntimeErrorCode::FunctionLimitExceeded:
        return "FunctionLimitExceeded";
    case RuntimeErrorCode::InvalidFunction: return "InvalidFunction";
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
    case RuntimeErrorCode::NotObject: return "NotObject";
    case RuntimeErrorCode::CannotReadProperty: return "CannotReadProperty";
    case RuntimeErrorCode::CannotWriteProperty: return "CannotWriteProperty";
    case RuntimeErrorCode::ObjectLimitExceeded: return "ObjectLimitExceeded";
    case RuntimeErrorCode::PropertyLimitExceeded:
        return "PropertyLimitExceeded";
    case RuntimeErrorCode::PropertyNameTooLong: return "PropertyNameTooLong";
    case RuntimeErrorCode::ArrayLimitExceeded: return "ArrayLimitExceeded";
    case RuntimeErrorCode::ArrayIndexOutOfRange:
        return "ArrayIndexOutOfRange";
    case RuntimeErrorCode::InvalidPropertyKey: return "InvalidPropertyKey";
    case RuntimeErrorCode::InvalidReceiver: return "InvalidReceiver";
    case RuntimeErrorCode::NativeFunctionLimitExceeded:
        return "NativeFunctionLimitExceeded";
    case RuntimeErrorCode::InvalidNativeFunction: return "InvalidNativeFunction";
    case RuntimeErrorCode::PrototypeChainExceeded:
        return "PrototypeChainExceeded";
    case RuntimeErrorCode::BuiltInInitializationFailed:
        return "BuiltInInitializationFailed";
    case RuntimeErrorCode::HostObjectLimitExceeded:
        return "HostObjectLimitExceeded";
    case RuntimeErrorCode::InvalidHostObject:
        return "InvalidHostObject";
    case RuntimeErrorCode::StaleHostObject:
        return "StaleHostObject";
    case RuntimeErrorCode::HostPropertyNotFound:
        return "HostPropertyNotFound";
    case RuntimeErrorCode::HostPropertyReadOnly:
        return "HostPropertyReadOnly";
    case RuntimeErrorCode::HostPropertyWriteFailed:
        return "HostPropertyWriteFailed";
    case RuntimeErrorCode::HostCallFailed:
        return "HostCallFailed";
    case RuntimeErrorCode::HostOperationBudgetExceeded:
        return "HostOperationBudgetExceeded";
    case RuntimeErrorCode::HostReentryUnsupported:
        return "HostReentryUnsupported";
    case RuntimeErrorCode::HostGenerationLimitExceeded:
        return "HostGenerationLimitExceeded";
    case RuntimeErrorCode::InvalidHostReturn:
        return "InvalidHostReturn";
    case RuntimeErrorCode::HostMethodLimitExceeded:
        return "HostMethodLimitExceeded";
    case RuntimeErrorCode::HostInvalidValue:
        return "HostInvalidValue";
    case RuntimeErrorCode::DocumentLookupLimitExceeded:
        return "DocumentLookupLimitExceeded";
    case RuntimeErrorCode::DocumentTextLimitExceeded:
        return "DocumentTextLimitExceeded";
    case RuntimeErrorCode::DocumentMutationLimitExceeded:
        return "DocumentMutationLimitExceeded";
    case RuntimeErrorCode::HostCallbackLimitExceeded:
        return "HostCallbackLimitExceeded";
    case RuntimeErrorCode::RealmSourceLimitExceeded:
        return "RealmSourceLimitExceeded";
    case RuntimeErrorCode::PropagationPathLimitExceeded:
        return "PropagationPathLimitExceeded";
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
