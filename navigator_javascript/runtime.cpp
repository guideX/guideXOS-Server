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

#ifdef GXOS_JAVASCRIPT_LIFETIME_DIAGNOSTICS
#define GXOS_JS84_DIAGNOSTIC(statement) do { statement; } while (false)
#else
#define GXOS_JS84_DIAGNOSTIC(statement) do { } while (false)
#endif

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

bool parseEcmaArrayIndex(const std::string& key, std::uint32_t& index)
{
    const std::size_t maximum = static_cast<std::size_t>(
        std::numeric_limits<std::uint32_t>::max() - 1u);
    std::size_t parsed = 0u;
    if (!parseBoundedArrayIndex(key, maximum, parsed)) return false;
    index = static_cast<std::uint32_t>(parsed);
    return true;
}

std::size_t maximumSupportedArrayLength(const RuntimeLimits& limits)
{
    std::size_t maximum = limits.maxArrayElements;
    const std::size_t languageMaximum = static_cast<std::size_t>(
        kMaximumJavaScriptArrayLength);
    if (maximum > languageMaximum) maximum = languageMaximum;
    if (limits.maxArrayIndex !=
        std::numeric_limits<std::size_t>::max()) {
        const std::size_t indexMaximum = limits.maxArrayIndex + 1u;
        if (maximum > indexMaximum) maximum = indexMaximum;
    }
    return maximum;
}

bool propertyKeyMatchesArrayIndex(const std::string& key, std::size_t index)
{
    if (key.empty()) return false;
    char digits[sizeof(std::size_t) * 3u] = {};
    std::size_t digitCount = 0u;
    do {
        digits[digitCount++] = static_cast<char>('0' + index % 10u);
        index /= 10u;
    } while (index != 0u);
    if (key.size() != digitCount) return false;
    for (std::size_t position = 0u; position < digitCount; ++position) {
        if (key[position] != digits[digitCount - position - 1u])
            return false;
    }
    return true;
}

} // namespace

class RuntimeContext::Evaluator : public HostFunctionInvoker {
public:
    explicit Evaluator(RuntimeContext& context)
        : context_(context),
          initialActiveCallFrames_(context.activeCallFrames_) {}

    ~Evaluator()
    {
        // The evaluator owns the transient call-frame vector.  Clear the
        // public diagnostic counter even if a bounded allocation failure
        // interrupts a call before its normal unwind path runs.
        context_.activeCallFrames_ = initialActiveCallFrames_;
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

    bool invoke(const HostValue& function, const HostValue* arguments,
        std::size_t argumentCount, const HostValue& thisArgument,
        Value& result, RuntimeErrorCode& error) override
    {
        error = RuntimeErrorCode::None;
        if (function.type != HostValueType::Function ||
            function.functionId == kInvalidRuntimeFunctionId ||
            context_.functionAt(function.functionId) == nullptr) {
            error = RuntimeErrorCode::NotCallable;
            context_.setRuntimeError(error, SourceLocation());
            return false;
        }
        if (argumentCount != 0u && arguments == nullptr) {
            error = RuntimeErrorCode::InvalidHostReturn;
            context_.setRuntimeError(error, SourceLocation());
            return false;
        }

        std::vector<Value> callbackArguments;
        try {
            callbackArguments.reserve(argumentCount);
            for (std::size_t index = 0; index < argumentCount; ++index) {
                Value value;
                if (!context_.convertHostValue(arguments[index],
                        kInvalidRuntimeHostObjectId, value, error,
                        SourceLocation())) {
                    context_.setRuntimeError(error, SourceLocation());
                    return false;
                }
                callbackArguments.push_back(value);
            }
        } catch (const std::bad_alloc&) {
            error = RuntimeErrorCode::AllocationFailure;
            context_.setRuntimeError(error, SourceLocation());
            return false;
        }

        Value receiver;
        if (!context_.convertHostValue(thisArgument,
                kInvalidRuntimeHostObjectId, receiver, error,
                SourceLocation())) {
            context_.setRuntimeError(error, SourceLocation());
            return false;
        }
        const bool succeeded = invokeFunction(function.functionId,
            callbackArguments, SourceLocation(), result, receiver);
        error = context_.result_.runtimeError.code;
        return succeeded;
    }

    bool isTruthy(const Value& value) const override
    {
        return truthy(value);
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
        bool symbolNamespaceValue = false;
        bool objectNamespaceValue = false;
        RuntimeContext::PropertyKey key;
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
        // JS82 exposes only the stable well-known Symbol namespace. It does
        // not provide the callable Symbol() constructor or dynamic Symbols.
        if (textEquals(name, "Symbol")) {
            value = Value::symbolNamespace();
            return true;
        }
        // JS89 exposes Object as a bounded intrinsic namespace. It does not
        // add a runtime object or global binding; only Object.keys is present.
        if (textEquals(name, "Object")) {
            value = Value::objectNamespace();
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
        SourceLocation location, bool immutable = false)
    {
        Environment* target = context_.environmentAt(environment);
        if (target == nullptr) {
            fail(RuntimeErrorCode::InvalidAstState, location);
            return false;
        }
        EnvironmentError error;
        if (target->declare(name, value, error, immutable)) return true;
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
                if (current->isImmutable(name)) {
                    fail(RuntimeErrorCode::TypeError, location);
                    return false;
                }
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
        case AstNodeKind::ForOfStatement:
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
        case AstNodeKind::ForOfStatement:
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

    bool propertyKeyFromValue(const Value& value,
        RuntimeContext::PropertyKey& key,
        SourceLocation location)
    {
        key = RuntimeContext::PropertyKey();
        if (value.isSymbol()) {
            if (value.wellKnownSymbolValue() == WellKnownSymbol::None) {
                fail(RuntimeErrorCode::InvalidPropertyKey, location);
                return false;
            }
            key.symbol = value.wellKnownSymbolValue();
            return true;
        }
        if (value.isObject() || value.isFunction() ||
            value.isSymbolNamespace() ||
            value.isObjectNamespace()) {
            fail(RuntimeErrorCode::InvalidPropertyKey, location);
            return false;
        }
        if (!primitiveString(value, key.string)) {
            if (context_.result_.runtimeError.code == RuntimeErrorCode::None) {
                fail(RuntimeErrorCode::InvalidPropertyKey, location);
            }
            return false;
        }
        if (value.isNumber() && value.numberValue() == 0.0)
            key.string = "0";
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
        if (!object.isObject() && !object.isHostObject() &&
            !object.isString() && !object.isSymbolNamespace() &&
            !object.isObjectNamespace()) {
            fail(object.isNull() || object.isUndefined()
                ? (forWrite ? RuntimeErrorCode::CannotWriteProperty
                    : RuntimeErrorCode::CannotReadProperty)
                : RuntimeErrorCode::NotObject, node.location);
            return false;
        }
        if ((object.isSymbolNamespace() || object.isObjectNamespace()) &&
            forWrite) {
            fail(RuntimeErrorCode::NotObject, node.location);
            return false;
        }
        if (object.isObject() && context_.objectAt(object.objectId()) == nullptr) {
            fail(RuntimeErrorCode::CannotReadProperty, node.location);
            return false;
        }

        RuntimeContext::PropertyKey key;
        if (!node.computed) {
            SourceView keyView;
            if (!identifierName(node.property, keyView)) return false;
            try {
                key.string.assign(keyView.data, keyView.length);
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
        } else if (object.isSymbolNamespace()) {
            reference.symbolNamespaceValue = true;
            reference.receiver = object;
        } else if (object.isObjectNamespace()) {
            reference.objectNamespaceValue = true;
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
            value = !reference.key.isSymbol() &&
                reference.key.string == "length"
                ? Value::number(static_cast<double>(
                    context_.stringValue(reference.receiver).size()))
                : Value::undefined();
            return true;
        }
        if (reference.symbolNamespaceValue) {
            value = !reference.key.isSymbol() &&
                reference.key.string == "iterator"
                ? Value::wellKnownSymbol(WellKnownSymbol::Iterator)
                : Value::undefined();
            return true;
        }
        if (reference.objectNamespaceValue) {
            value = !reference.key.isSymbol() &&
                reference.key.string == "keys"
                ? Value::function(context_.objectKeysFunction_)
                : !reference.key.isSymbol() &&
                    reference.key.string == "getOwnPropertyDescriptor"
                    ? Value::function(
                        context_.objectGetOwnPropertyDescriptorFunction_)
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

    bool readValueProperty(const Value& base,
        const RuntimeContext::PropertyKey& key, Value& value,
        SourceLocation location)
    {
        MemberReference reference;
        reference.receiver = base;
        reference.key = key;
        reference.location = location;
        if (base.isObject()) {
            if (context_.objectAt(base.objectId()) == nullptr) {
                fail(RuntimeErrorCode::CannotReadProperty, location);
                return false;
            }
            reference.object = base.objectId();
        } else if (base.isHostObject()) {
            reference.hostObject = base.hostObjectId();
            reference.hostObjectValue = true;
        } else if (base.isString()) {
            reference.stringPrimitive = true;
        } else if (base.isSymbolNamespace()) {
            reference.symbolNamespaceValue = true;
        } else if (base.isObjectNamespace()) {
            reference.objectNamespaceValue = true;
        } else if (base.isNull() || base.isUndefined()) {
            fail(RuntimeErrorCode::CannotReadProperty, location);
            return false;
        } else {
            if (!context_.consumeStep(location)) return false;
            value = Value::undefined();
            return true;
        }
        return readMember(reference, value);
    }

    bool readValueProperty(const Value& base, const char* key,
        Value& value, SourceLocation location)
    {
        RuntimeContext::PropertyKey propertyKey;
        try {
            propertyKey.string = key == nullptr ? "" : key;
        } catch (const std::bad_alloc&) {
            fail(RuntimeErrorCode::AllocationFailure, location);
            return false;
        }
        return readValueProperty(base, propertyKey, value, location);
    }

    bool writeMember(const MemberReference& reference, Value value)
    {
        if (reference.stringPrimitive) {
            fail(RuntimeErrorCode::NotObject, reference.location);
            return false;
        }
        if (reference.objectNamespaceValue) {
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
        std::vector<RuntimeContext::RuntimeArrayElement> elements;
        try {
            for (std::size_t index = 0; index < node.childCount; ++index) {
                const AstNodeId elementId = context_.ast_.childAt(id, index);
                if (!validNode(elementId, node.location)) return false;
                if (context_.ast_.node(elementId).kind ==
                    AstNodeKind::ArrayElision) continue;
                Value element;
                if (!evalExpression(elementId, element)) return false;
                elements.push_back({index, element});
            }
        } catch (const std::bad_alloc&) {
            fail(RuntimeErrorCode::AllocationFailure, node.location);
            return false;
        }
        if (!context_.consumeStep(node.location)) return false;
        RuntimeObjectId array = kInvalidRuntimeObjectId;
        RuntimeErrorCode error = RuntimeErrorCode::None;
        if (!context_.createArray(node.childCount, std::move(elements), array,
                error)) {
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
        case AstNodeKind::ArrayElision:
            fail(RuntimeErrorCode::InvalidAstState, node.location);
            return false;
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
        Value callerThis, std::size_t previousFrameCount,
        std::size_t previousActiveCallFrames)
    {
        currentEnvironment_ = callerEnvironment;
        currentThis_ = callerThis;
        if (frames_.size() > previousFrameCount) {
            frames_.resize(previousFrameCount);
        }
        context_.activeCallFrames_ = previousActiveCallFrames;
    }

    bool invokeFunction(RuntimeFunctionId functionId,
        const std::vector<Value>& arguments, SourceLocation callSite,
        Value& result, Value receiver = Value::undefined())
    {
        const RuntimeContext::FunctionRecord* functionRecord =
            context_.functionAt(functionId);
        if (functionRecord == nullptr) {
            fail(RuntimeErrorCode::InvalidFunction, callSite);
            return false;
        }
        // Reentrant callbacks may append host/native function records and
        // reallocate functions_. Keep the active invocation metadata stable
        // across those calls rather than retaining a vector element pointer.
        const RuntimeContext::FunctionRecord function = *functionRecord;
        if (function.kind == RuntimeContext::FunctionRecord::Kind::Native) {
            return invokeNative(function, arguments, callSite, receiver, result);
        }
        if (function.kind == RuntimeContext::FunctionRecord::Kind::Host) {
            return context_.invokeHostMethod(function, arguments, receiver,
                callSite, result, *this);
        }
        if (function.declaration == kInvalidAstNodeId ||
            function.closureEnvironment == kInvalidEnvironmentId) {
            fail(RuntimeErrorCode::InvalidFunction, callSite);
            return false;
        }
        const AstNode& declaration = context_.ast_.node(function.declaration);
        if ((declaration.kind != AstNodeKind::FunctionDeclaration &&
            declaration.kind != AstNodeKind::FunctionExpression) ||
            !validNode(declaration.body, callSite)) {
            fail(RuntimeErrorCode::InvalidFunction, callSite);
            return false;
        }
        if (context_.activeCallFrames_ >= context_.limits_.maxCallDepth) {
            fail(RuntimeErrorCode::CallDepthExceeded, callSite);
            return false;
        }

        EnvironmentId callEnvironment = kInvalidEnvironmentId;
        RuntimeErrorCode error = RuntimeErrorCode::None;
        if (!context_.createEnvironment(function.closureEnvironment,
            callEnvironment, error)) {
            fail(error, callSite);
            return false;
        }

        const EnvironmentId callerEnvironment = currentEnvironment_;
        const Value callerThis = currentThis_;
        const std::size_t previousFrameCount = frames_.size();
        const std::size_t previousActiveCallFrames =
            context_.activeCallFrames_;
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
        context_.activeCallFrames_ = previousActiveCallFrames + 1u;
        currentEnvironment_ = callEnvironment;
        currentThis_ = receiver;

        for (std::size_t index = 0; index < declaration.childCount; ++index) {
            const AstNodeId parameterId = context_.ast_.childAt(
                function.declaration, index);
            SourceView name;
            if (!identifierName(parameterId, name)) {
                unwindFrame(callerEnvironment, callerThis, previousFrameCount,
                    previousActiveCallFrames);
                return false;
            }
            const Value argument = index < arguments.size()
                ? arguments[index] : Value::undefined();
            if (!declareIn(callEnvironment, name, argument,
                context_.ast_.node(parameterId).location)) {
                unwindFrame(callerEnvironment, callerThis, previousFrameCount,
                    previousActiveCallFrames);
                return false;
            }
            Environment* target = context_.environmentAt(callEnvironment);
            if (target == nullptr || !target->assign(name, argument)) {
                fail(RuntimeErrorCode::InvalidAstState,
                    context_.ast_.node(parameterId).location);
                unwindFrame(callerEnvironment, callerThis, previousFrameCount,
                    previousActiveCallFrames);
                return false;
            }
        }

        if (!instantiateDeclarations(declaration.body, callEnvironment)) {
            unwindFrame(callerEnvironment, callerThis, previousFrameCount,
                previousActiveCallFrames);
            return false;
        }

        Control control;
        const bool executed = executeStatement(declaration.body, false, true,
            control);
        if (!executed) {
            unwindFrame(callerEnvironment, callerThis, previousFrameCount,
                previousActiveCallFrames);
            return false;
        }
        if (control.kind == ControlKind::Return) {
            result = control.value;
        } else if (control.kind == ControlKind::Normal) {
            result = Value::undefined();
        } else {
            fail(RuntimeErrorCode::InvalidAstState, declaration.location);
            unwindFrame(callerEnvironment, callerThis, previousFrameCount,
                previousActiveCallFrames);
            return false;
        }
        unwindFrame(callerEnvironment, callerThis, previousFrameCount,
            previousActiveCallFrames);
        return true;
    }

    bool hasOwnProperty(RuntimeObjectId objectId, const std::string& key) const
    {
        const RuntimeContext::RuntimeObject* object =
            context_.objectAt(objectId);
        if (object == nullptr) return false;
        if (object->array && key == "length") return true;
        std::uint32_t standardIndex = 0u;
        if (object->array && parseEcmaArrayIndex(key, standardIndex)) {
            const std::size_t index = static_cast<std::size_t>(standardIndex);
            if (index > context_.limits_.maxArrayIndex) return false;
            const std::size_t position =
                RuntimeContext::findArrayElementPosition(*object, index);
            return index < object->arrayLength &&
                position < object->elements.size() &&
                object->elements[position].index == index;
        }
        for (const RuntimeContext::RuntimeProperty& property :
            object->properties) {
            if (property.symbolKey == WellKnownSymbol::None &&
                property.key == key) return true;
        }
        return false;
    }

    bool hasOwnProperty(RuntimeObjectId objectId,
        const RuntimeContext::PropertyKey& key) const
    {
        const RuntimeContext::RuntimeObject* object =
            context_.objectAt(objectId);
        if (object == nullptr) return false;
        if (!key.isSymbol()) {
            if (object->array && key.string == "length") return true;
            std::uint32_t standardIndex = 0u;
            if (object->array && parseEcmaArrayIndex(key.string,
                    standardIndex)) {
                const std::size_t index =
                    static_cast<std::size_t>(standardIndex);
                if (index > context_.limits_.maxArrayIndex) return false;
                const std::size_t position =
                    RuntimeContext::findArrayElementPosition(*object, index);
                return index < object->arrayLength &&
                    position < object->elements.size() &&
                    object->elements[position].index == index;
            }
        }
        for (const RuntimeContext::RuntimeProperty& property :
            object->properties) {
            if (key.isSymbol()
                    ? property.symbolKey == key.symbol
                    : property.symbolKey == WellKnownSymbol::None &&
                        property.key == key.string) return true;
        }
        return false;
    }

    bool hasArrayIndex(RuntimeObjectId objectId, std::size_t index,
        bool& present, SourceLocation location)
    {
        present = false;
        RuntimeObjectId current = objectId;
        for (std::size_t depth = 0u;
            depth < context_.limits_.maxPrototypeDepth; ++depth) {
            if (!context_.consumeStep(location)) return false;
            const RuntimeContext::RuntimeObject* object =
                context_.objectAt(current);
            if (object == nullptr) {
                fail(RuntimeErrorCode::CannotReadProperty, location);
                return false;
            }
            if (object->array && index < object->arrayLength) {
                const std::size_t position =
                    RuntimeContext::findArrayElementPosition(*object, index);
                if (position < object->elements.size() &&
                    object->elements[position].index == index) {
                    present = true;
                    return true;
                }
            }
            for (const RuntimeContext::RuntimeProperty& property :
                object->properties) {
                if (property.symbolKey == WellKnownSymbol::None &&
                    propertyKeyMatchesArrayIndex(property.key, index)) {
                    present = true;
                    return true;
                }
            }
            if (object->prototype == kInvalidRuntimeObjectId) return true;
            current = object->prototype;
        }
        fail(RuntimeErrorCode::PrototypeChainExceeded, location);
        return false;
    }

    bool traverseArray(const Value& receiver, const Value& callback,
        const Value& thisArgument, SourceLocation callSite, bool map,
        bool filter, Value& result)
    {
        const RuntimeObjectId arrayId = receiver.isObject()
            ? receiver.objectId() : kInvalidRuntimeObjectId;
        const RuntimeContext::RuntimeObject* array =
            context_.objectAt(arrayId);
        if (array == nullptr || !array->array) {
            fail(RuntimeErrorCode::InvalidReceiver, callSite);
            return false;
        }
        if (!callback.isFunction()) {
            fail(RuntimeErrorCode::NotCallable, callSite);
            return false;
        }
        const RuntimeFunctionId callbackId = callback.functionId();
        if (context_.functionAt(callbackId) == nullptr) {
            fail(RuntimeErrorCode::InvalidFunction, callSite);
            return false;
        }

        // Keep only the stable object ID and captured length across callbacks:
        // reentrant code may grow objects_ and invalidate RuntimeObject pointers.
        const std::size_t initialLength = array->arrayLength;
        if (initialLength > maximumSupportedArrayLength(context_.limits_)) {
            fail(RuntimeErrorCode::InvalidAstState, callSite);
            return false;
        }

        std::vector<Value> callbackArguments;
        std::vector<RuntimeContext::RuntimeArrayElement> mappedElements;
        bool callbackArgumentsReady = false;
        for (std::size_t index = 0u; index < initialLength; ++index) {
            bool present = false;
            if (!hasArrayIndex(arrayId, index, present, callSite)) return false;
            if (!present) continue;

            Value value = Value::undefined();
            RuntimeErrorCode lookupError = RuntimeErrorCode::None;
            if (!context_.readArrayIndex(arrayId, index, value, lookupError,
                    callSite)) {
                fail(lookupError, callSite);
                return false;
            }
            if (!callbackArgumentsReady) {
                try {
                    callbackArguments.resize(3u, Value::undefined());
                } catch (const std::bad_alloc&) {
                    fail(RuntimeErrorCode::AllocationFailure, callSite);
                    return false;
                }
                callbackArgumentsReady = true;
            }
            callbackArguments[0] = value;
            callbackArguments[1] = Value::number(
                static_cast<double>(index));
            callbackArguments[2] = receiver;
            Value callbackResult = Value::undefined();
            if (!invokeFunction(callbackId, callbackArguments, callSite,
                    callbackResult, thisArgument)) return false;

            if (map || (filter && truthy(callbackResult))) {
                try {
                    mappedElements.emplace_back(filter
                            ? mappedElements.size() : index,
                        filter ? value : callbackResult);
                } catch (const std::bad_alloc&) {
                    fail(RuntimeErrorCode::AllocationFailure, callSite);
                    return false;
                }
            }
        }

        if (!map && !filter) {
            result = Value::undefined();
            return true;
        }

        // Compact transient scratch storage before it becomes persistent Array
        // storage. Sparse results therefore retain only their occupied slots.
        if (mappedElements.capacity() != mappedElements.size()) {
            std::vector<RuntimeContext::RuntimeArrayElement> compactElements;
            try {
                compactElements.reserve(mappedElements.size());
                for (const RuntimeContext::RuntimeArrayElement& element :
                    mappedElements) {
                    compactElements.push_back(element);
                }
            } catch (const std::bad_alloc&) {
                fail(RuntimeErrorCode::AllocationFailure, callSite);
                return false;
            }
            mappedElements.swap(compactElements);
        }

        const std::size_t resultLength = filter
            ? mappedElements.size() : initialLength;
        RuntimeObjectId mappedArrayId = kInvalidRuntimeObjectId;
        RuntimeErrorCode allocationError = RuntimeErrorCode::None;
        if (!context_.createArray(resultLength, std::move(mappedElements),
                mappedArrayId, allocationError)) {
            fail(allocationError, callSite);
            return false;
        }
        result = Value::object(mappedArrayId);
        return true;
    }

    bool createObjectKeys(RuntimeObjectId sourceId, SourceLocation callSite,
        Value& result)
    {
        const RuntimeContext::RuntimeObject* source =
            context_.objectAt(sourceId);
        if (source == nullptr || sourceId == context_.objectPrototype_ ||
            sourceId == context_.arrayPrototype_ ||
            sourceId == context_.mathObject_ ||
            sourceId == context_.eventConstantsObject_ ||
            source->arrayIterator) {
            fail(RuntimeErrorCode::InvalidReceiver, callSite);
            return false;
        }
        for (const RuntimeContext::RuntimeProperty& property :
            source->properties) {
            if (property.symbolKey == WellKnownSymbol::None &&
                (!property.attributes.isValid() ||
                    !property.attributes.isFullyKnown())) {
                // Host-created Event fields have known write protection but
                // unknown enumeration/configuration attributes. Keep those
                // receivers outside Object.keys until their contract is
                // qualified independently.
                fail(RuntimeErrorCode::InvalidReceiver, callSite);
                return false;
            }
        }

        std::size_t keyCount = source->array ? source->elements.size() : 0u;
        std::size_t keyBytes = 0u;
        const auto addKeySize = [this, &keyBytes, callSite](
            std::size_t length) {
                if (length > context_.limits_.maxRuntimeStringLength ||
                    length > context_.limits_.maxTotalRuntimeStringBytes ||
                    keyBytes > context_.limits_.maxTotalRuntimeStringBytes -
                        length) {
                    fail(RuntimeErrorCode::StringLimitExceeded, callSite);
                    return false;
                }
                keyBytes += length;
                return true;
            };
        const auto indexLength = [](std::size_t index) {
            std::size_t length = 1u;
            while (index >= 10u) {
                index /= 10u;
                ++length;
            }
            return length;
        };

        std::size_t previousArrayIndex = 0u;
        if (source->array) {
            for (std::size_t position = 0u;
                position < source->elements.size(); ++position) {
                if (!context_.consumeStep(callSite)) return false;
                const std::size_t index = source->elements[position].index;
                if (index >= source->arrayLength ||
                    index > context_.limits_.maxArrayIndex ||
                    !source->elements[position].attributes.isValid() ||
                    !source->elements[position].attributes.isFullyKnown() ||
                    !source->elements[position].attributes.enumerable() ||
                    (position != 0u && index <= previousArrayIndex)) {
                    fail(RuntimeErrorCode::InvalidAstState, callSite);
                    return false;
                }
                previousArrayIndex = index;
                if (!addKeySize(indexLength(index))) return false;
            }
        }

        for (const RuntimeContext::RuntimeProperty& property :
            source->properties) {
            if (!context_.consumeStep(callSite)) return false;
            if (property.symbolKey != WellKnownSymbol::None ||
                (source->array && property.key == "length")) continue;
            if (!property.attributes.enumerable()) continue;
            std::uint32_t standardIndex = 0u;
            if (source->array && parseEcmaArrayIndex(property.key,
                    standardIndex)) {
                // Indexed Array properties are stored only in the sorted
                // elements vector. A duplicate here means the own-key store
                // is internally inconsistent.
                fail(RuntimeErrorCode::InvalidAstState, callSite);
                return false;
            }
            if (keyCount == std::numeric_limits<std::size_t>::max()) {
                fail(RuntimeErrorCode::ArrayLimitExceeded, callSite);
                return false;
            }
            ++keyCount;
            if (!addKeySize(property.key.size())) return false;
        }

        const std::size_t maximumResultLength =
            maximumSupportedArrayLength(context_.limits_);
        if (keyCount > context_.limits_.maxArrayElements ||
            keyCount > maximumResultLength ||
            keyCount > context_.limits_.maxTotalArrayElements ||
            context_.totalArrayElements_ >
                context_.limits_.maxTotalArrayElements - keyCount) {
            fail(RuntimeErrorCode::ArrayLimitExceeded, callSite);
            return false;
        }
        if (context_.objects_.size() >= context_.limits_.maxObjects) {
            fail(RuntimeErrorCode::ObjectLimitExceeded, callSite);
            return false;
        }
        if (keyCount > context_.limits_.maxRuntimeStringValues ||
            context_.strings_.size() >
                context_.limits_.maxRuntimeStringValues - keyCount ||
            keyBytes > context_.limits_.maxTotalRuntimeStringBytes ||
            context_.totalStringBytes_ >
                context_.limits_.maxTotalRuntimeStringBytes - keyBytes) {
            fail(RuntimeErrorCode::StringLimitExceeded, callSite);
            return false;
        }

        std::vector<RuntimeContext::RuntimeArrayElement> keyElements;
        try {
#if defined(GXOS_JAVASCRIPT_TEST_FAILURE_INJECTION)
            if (context_.failNextObjectKeysScratchAllocationForTesting_) {
                context_.failNextObjectKeysScratchAllocationForTesting_ = false;
                throw std::bad_alloc();
            }
#endif
            keyElements.reserve(keyCount);
        } catch (const std::bad_alloc&) {
            fail(RuntimeErrorCode::AllocationFailure, callSite);
            return false;
        }

        const auto appendKey = [this, &keyElements, callSite](SourceView key) {
            if (!context_.consumeStep(callSite)) return false;
            Value keyValue;
            RuntimeErrorCode error = RuntimeErrorCode::None;
#if defined(GXOS_JAVASCRIPT_TEST_FAILURE_INJECTION)
            if (context_.failNextObjectKeysStringCreationForTesting_) {
                context_.failNextObjectKeysStringCreationForTesting_ = false;
                fail(RuntimeErrorCode::AllocationFailure, callSite);
                return false;
            }
#endif
            if (!context_.createString(key, keyValue, error)) {
                fail(error, callSite);
                return false;
            }
            try {
#if defined(GXOS_JAVASCRIPT_TEST_FAILURE_INJECTION)
                if (context_.failNextObjectKeysElementInsertionForTesting_) {
                    context_.failNextObjectKeysElementInsertionForTesting_ =
                        false;
                    throw std::bad_alloc();
                }
#endif
                keyElements.push_back({keyElements.size(), keyValue});
            } catch (const std::bad_alloc&) {
                fail(RuntimeErrorCode::AllocationFailure, callSite);
                return false;
            }
            return true;
        };
        const auto appendIndex = [this, &appendKey, callSite](
            std::size_t index) {
            char buffer[32] = {};
            const auto conversion = std::to_chars(buffer,
                buffer + sizeof(buffer), index);
            if (conversion.ec != std::errc()) {
                fail(RuntimeErrorCode::InvalidAstState, callSite);
                return false;
            }
            return appendKey(SourceView(buffer, static_cast<std::size_t>(
                conversion.ptr - buffer)));
        };

        if (source->array) {
            for (const RuntimeContext::RuntimeArrayElement& element :
                source->elements) {
                if (!element.attributes.enumerable()) continue;
                if (!appendIndex(element.index)) return false;
            }
        } else {
            bool hasPreviousIndex = false;
            std::uint32_t previousIndex = 0u;
            while (true) {
                bool found = false;
                std::uint32_t smallestIndex = 0u;
                std::size_t smallestProperty = 0u;
                for (std::size_t position = 0u;
                    position < source->properties.size(); ++position) {
                    if (!context_.consumeStep(callSite)) return false;
                    const RuntimeContext::RuntimeProperty& property =
                        source->properties[position];
                    if (property.symbolKey != WellKnownSymbol::None ||
                        !property.attributes.enumerable()) continue;
                    std::uint32_t index = 0u;
                    if (!parseEcmaArrayIndex(property.key, index) ||
                        (hasPreviousIndex && index <= previousIndex)) continue;
                    if (!found || index < smallestIndex) {
                        found = true;
                        smallestIndex = index;
                        smallestProperty = position;
                    }
                }
                if (!found) break;
                const std::string& key =
                    source->properties[smallestProperty].key;
                if (!appendKey(SourceView(key.data(), key.size()))) return false;
                previousIndex = smallestIndex;
                hasPreviousIndex = true;
            }
        }

        for (const RuntimeContext::RuntimeProperty& property :
            source->properties) {
            if (!context_.consumeStep(callSite)) return false;
            if (property.symbolKey != WellKnownSymbol::None ||
                (source->array && property.key == "length") ||
                !property.attributes.enumerable()) continue;
            std::uint32_t standardIndex = 0u;
            if (!source->array && parseEcmaArrayIndex(property.key,
                    standardIndex)) continue;
            if (!appendKey(SourceView(property.key.data(), property.key.size())))
                return false;
        }
        if (keyElements.size() != keyCount) {
            fail(RuntimeErrorCode::InvalidAstState, callSite);
            return false;
        }

        RuntimeErrorCode error = RuntimeErrorCode::None;
        RuntimeObjectId resultId = kInvalidRuntimeObjectId;
        if (!context_.createArray(keyCount, std::move(keyElements), resultId,
                error)) {
            fail(error, callSite);
            return false;
        }
        result = Value::object(resultId);
        return true;
    }

    bool createDescriptorSnapshot(const Value& propertyValue,
        const RuntimeContext::PropertyAttributes& attributes,
        SourceLocation callSite, Value& result)
    {
        if (!attributes.isValid() || !attributes.isFullyKnown()) {
            fail(RuntimeErrorCode::UnsupportedFeature, callSite);
            return false;
        }

        static const char valueKey[] = "value";
        static const char writableKey[] = "writable";
        static const char enumerableKey[] = "enumerable";
        static const char configurableKey[] = "configurable";
        const std::size_t requiredKeyBytes =
            sizeof(valueKey) - 1u + sizeof(writableKey) - 1u +
            sizeof(enumerableKey) - 1u + sizeof(configurableKey) - 1u;
        if (context_.objects_.size() >= context_.limits_.maxObjects) {
            fail(RuntimeErrorCode::ObjectLimitExceeded, callSite);
            return false;
        }
        if (context_.limits_.maxPropertiesPerObject < 4u ||
            context_.limits_.maxTotalProperties < 4u ||
            context_.totalPropertyCount_ >
                context_.limits_.maxTotalProperties - 4u) {
            fail(RuntimeErrorCode::PropertyLimitExceeded, callSite);
            return false;
        }
        if (context_.limits_.maxPropertyNameLength <
                sizeof(configurableKey) - 1u) {
            fail(RuntimeErrorCode::PropertyNameTooLong, callSite);
            return false;
        }
        if (requiredKeyBytes > context_.limits_.maxTotalPropertyKeyBytes ||
            context_.totalPropertyKeyBytes_ >
                context_.limits_.maxTotalPropertyKeyBytes - requiredKeyBytes) {
            fail(RuntimeErrorCode::PropertyLimitExceeded, callSite);
            return false;
        }

        const std::vector<Value> noElements;
        RuntimeObjectId descriptorId = kInvalidRuntimeObjectId;
        RuntimeErrorCode error = RuntimeErrorCode::None;
        if (!context_.createObject(false, noElements, descriptorId, error)) {
            fail(error, callSite);
            return false;
        }

        std::size_t insertedProperties = 0u;
        std::size_t insertedKeyBytes = 0u;
        const auto addField = [this, descriptorId, callSite,
            &insertedProperties, &insertedKeyBytes](const char* key,
                Value value) {
            RuntimeErrorCode fieldError = RuntimeErrorCode::None;
            RuntimeContext::PropertyKey fieldKey;
            try {
                fieldKey.string.assign(key);
            } catch (const std::bad_alloc&) {
                fail(RuntimeErrorCode::AllocationFailure, callSite);
                return false;
            }
            if (!context_.writeProperty(descriptorId, fieldKey, value,
                    fieldError,
                    RuntimeContext::PropertyAttributes::ordinaryData())) {
                fail(fieldError, callSite);
                return false;
            }
            ++insertedProperties;
            insertedKeyBytes += fieldKey.string.size();
            return true;
        };

        if (!addField(valueKey, propertyValue) ||
            !addField(writableKey, Value::boolean(attributes.writable())) ||
            !addField(enumerableKey,
                Value::boolean(attributes.enumerable())) ||
            !addField(configurableKey,
                Value::boolean(attributes.configurable()))) {
            RuntimeContext::RuntimeObject* unpublished =
                context_.objectAt(descriptorId);
            if (unpublished != nullptr &&
                static_cast<std::size_t>(descriptorId) + 1u ==
                    context_.objects_.size()) {
                context_.totalPropertyCount_ -= insertedProperties;
                context_.totalPropertyKeyBytes_ -= insertedKeyBytes;
                context_.objects_.pop_back();
            }
            result = Value::undefined();
            return false;
        }

        result = Value::object(descriptorId);
        return true;
    }

    bool createOwnPropertyDescriptor(RuntimeObjectId sourceId,
        const RuntimeContext::PropertyKey& key, SourceLocation callSite,
        Value& result)
    {
        const RuntimeContext::RuntimeObject* source =
            context_.objectAt(sourceId);
        if (source == nullptr || sourceId == context_.objectPrototype_ ||
            sourceId == context_.arrayPrototype_ ||
            sourceId == context_.mathObject_ ||
            sourceId == context_.eventConstantsObject_ ||
            source->arrayIterator) {
            fail(RuntimeErrorCode::InvalidReceiver, callSite);
            return false;
        }
        if (!key.isSymbol() &&
            key.string.size() > context_.limits_.maxPropertyNameLength) {
            fail(RuntimeErrorCode::PropertyNameTooLong, callSite);
            return false;
        }
        if (!context_.consumeStep(callSite)) return false;

        if (!hasOwnProperty(sourceId, key)) {
            result = Value::undefined();
            return true;
        }

        Value propertyValue = Value::undefined();
        RuntimeContext::PropertyAttributes attributes =
            RuntimeContext::PropertyAttributes::unknown();
        if (!key.isSymbol() && source->array && key.string == "length") {
            propertyValue = Value::number(
                static_cast<double>(source->arrayLength));
            attributes = RuntimeContext::PropertyAttributes::arrayLength();
        } else {
            bool found = false;
            std::uint32_t standardIndex = 0u;
            if (!key.isSymbol() && source->array &&
                parseEcmaArrayIndex(key.string, standardIndex)) {
                const std::size_t index =
                    static_cast<std::size_t>(standardIndex);
                if (index <= context_.limits_.maxArrayIndex &&
                    index < source->arrayLength) {
                    const std::size_t position =
                        RuntimeContext::findArrayElementPosition(*source,
                            index);
                    if (position < source->elements.size() &&
                        source->elements[position].index == index) {
                        propertyValue = source->elements[position].value;
                        attributes = source->elements[position].attributes;
                        found = true;
                    }
                }
            }
            if (!found) {
                for (const RuntimeContext::RuntimeProperty& property :
                    source->properties) {
                    const bool matches = key.isSymbol()
                        ? property.symbolKey == key.symbol
                        : property.symbolKey == WellKnownSymbol::None &&
                            property.key == key.string;
                    if (matches) {
                        propertyValue = property.value;
                        attributes = property.attributes;
                        found = true;
                        break;
                    }
                }
            }
            if (!found) {
                fail(RuntimeErrorCode::InvalidAstState, callSite);
                return false;
            }
        }

        if (!attributes.isValid() || !attributes.isFullyKnown()) {
            fail(RuntimeErrorCode::UnsupportedFeature, callSite);
            return false;
        }
        return createDescriptorSnapshot(propertyValue, attributes,
            callSite, result);
    }

    bool invokeNative(const RuntimeContext::FunctionRecord& function,
        const std::vector<Value>& arguments, SourceLocation callSite,
        Value receiver, Value& result)
    {
        if (context_.activeCallFrames_ >= context_.limits_.maxCallDepth) {
            fail(RuntimeErrorCode::CallDepthExceeded, callSite);
            return false;
        }
        if (!context_.consumeStep(callSite)) return false;

        const Value callerThis = currentThis_;
        const std::size_t previousFrameCount = frames_.size();
        const std::size_t previousActiveCallFrames =
            context_.activeCallFrames_;
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
        context_.activeCallFrames_ = previousActiveCallFrames + 1u;
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
        case RuntimeContext::NativeFunctionId::ObjectKeys: {
            const Value& target = argumentOrUndefined(0u);
            if (!target.isObject()) {
                fail(RuntimeErrorCode::InvalidReceiver, callSite);
                succeeded = false;
                break;
            }
            succeeded = createObjectKeys(target.objectId(), callSite, result);
            break;
        }
        case RuntimeContext::NativeFunctionId::ObjectGetOwnPropertyDescriptor: {
            const Value& target = argumentOrUndefined(0u);
            if (!target.isObject() ||
                context_.objectAt(target.objectId()) == nullptr) {
                fail(RuntimeErrorCode::InvalidReceiver, callSite);
                succeeded = false;
                break;
            }
            RuntimeContext::PropertyKey key;
            if (!propertyKeyFromValue(argumentOrUndefined(1u), key,
                    callSite)) {
                succeeded = false;
                break;
            }
            succeeded = createOwnPropertyDescriptor(target.objectId(), key,
                callSite, result);
            break;
        }
        case RuntimeContext::NativeFunctionId::ObjectHasOwnProperty: {
            if (!receiver.isObject() ||
                context_.objectAt(receiver.objectId()) == nullptr) {
                fail(RuntimeErrorCode::InvalidReceiver, callSite);
                succeeded = false;
                break;
            }
            RuntimeContext::PropertyKey key;
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
            const std::size_t oldLength = object->arrayLength;
            const std::size_t maximumLength =
                maximumSupportedArrayLength(context_.limits_);
            if (oldLength > maximumLength ||
                arguments.size() > maximumLength - oldLength) {
                GXOS_JS84_DIAGNOSTIC(
                    ++context_.diagnostics_.failedArraySlotAllocations);
                fail(RuntimeErrorCode::ArrayLimitExceeded, callSite);
                succeeded = false;
                break;
            }
            if (arguments.size() > context_.limits_.maxTotalArrayElements ||
                context_.totalArrayElements_ >
                    context_.limits_.maxTotalArrayElements - arguments.size()) {
                GXOS_JS84_DIAGNOSTIC(
                    ++context_.diagnostics_.failedArraySlotAllocations);
                fail(RuntimeErrorCode::ArrayLimitExceeded, callSite);
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
                object->elements.reserve(object->elements.size() +
                    arguments.size());
                for (std::size_t index = 0; index < arguments.size(); ++index) {
                    object->elements.push_back({oldLength + index,
                        arguments[index]});
                }
            } catch (const std::bad_alloc&) {
                GXOS_JS84_DIAGNOSTIC(
                    ++context_.diagnostics_.failedArraySlotAllocations);
                fail(RuntimeErrorCode::AllocationFailure, callSite);
                succeeded = false;
                break;
            }
            context_.totalArrayElements_ += arguments.size();
            object->arrayLength = oldLength + arguments.size();
            GXOS_JS84_DIAGNOSTIC({
                context_.diagnostics_.arraySlotsAllocated += arguments.size();
                if (context_.diagnostics_.arraySlotHighWater <
                    context_.totalArrayElements_) {
                    context_.diagnostics_.arraySlotHighWater =
                        context_.totalArrayElements_;
                }
            });
            result = Value::number(static_cast<double>(object->arrayLength));
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
            if (object->arrayLength == 0u) {
                result = Value::undefined();
                break;
            }
            const std::size_t removedIndex = object->arrayLength - 1u;
            const std::size_t position =
                RuntimeContext::findArrayElementPosition(*object, removedIndex);
            if (position < object->elements.size() &&
                object->elements[position].index == removedIndex) {
                result = object->elements[position].value;
                object->elements.erase(object->elements.begin() +
                    static_cast<std::ptrdiff_t>(position));
                --context_.totalArrayElements_;
            } else {
                result = Value::undefined();
            }
            object->arrayLength = removedIndex;
            break;
        }
        case RuntimeContext::NativeFunctionId::ArrayForEach:
        case RuntimeContext::NativeFunctionId::ArrayMap:
        case RuntimeContext::NativeFunctionId::ArrayFilter: {
            const bool map = function.nativeFunction ==
                static_cast<std::uint8_t>(
                    RuntimeContext::NativeFunctionId::ArrayMap);
            const bool filter = function.nativeFunction ==
                static_cast<std::uint8_t>(
                    RuntimeContext::NativeFunctionId::ArrayFilter);
            succeeded = traverseArray(receiver, argumentOrUndefined(0u),
                argumentOrUndefined(1u), callSite, map, filter, result);
            break;
        }
        case RuntimeContext::NativeFunctionId::ArrayIteratorCreate:
        case RuntimeContext::NativeFunctionId::ArrayIteratorCreateKeys:
        case RuntimeContext::NativeFunctionId::ArrayIteratorCreateEntries: {
            const RuntimeContext::NativeFunctionId native =
                static_cast<RuntimeContext::NativeFunctionId>(
                    function.nativeFunction);
            RuntimeContext::ArrayIteratorMode mode =
                RuntimeContext::ArrayIteratorMode::Values;
            if (native ==
                RuntimeContext::NativeFunctionId::ArrayIteratorCreateKeys) {
                mode = RuntimeContext::ArrayIteratorMode::Keys;
            } else if (native ==
                RuntimeContext::NativeFunctionId::ArrayIteratorCreateEntries) {
                mode = RuntimeContext::ArrayIteratorMode::Entries;
            }
            const RuntimeObjectId arrayId = receiver.isObject()
                ? receiver.objectId() : kInvalidRuntimeObjectId;
            const RuntimeContext::RuntimeObject* array =
                context_.objectAt(arrayId);
            if (array == nullptr || !array->array) {
                fail(RuntimeErrorCode::InvalidReceiver, callSite);
                succeeded = false;
                break;
            }
            const std::vector<Value> noElements;
            RuntimeObjectId iteratorId = kInvalidRuntimeObjectId;
            RuntimeErrorCode error = RuntimeErrorCode::None;
            const std::size_t objectCountBefore = context_.objects_.size();
            const std::size_t propertyCountBefore =
                context_.totalPropertyCount_;
            const std::size_t propertyKeyBytesBefore =
                context_.totalPropertyKeyBytes_;
            if (!context_.createObject(false, noElements, iteratorId, error,
                    context_.objectPrototype_)) {
                fail(error, callSite);
                succeeded = false;
                break;
            }
            RuntimeContext::PropertyKey symbolIteratorKey;
            symbolIteratorKey.symbol = WellKnownSymbol::Iterator;
            if (!context_.writeProperty(iteratorId, "next",
                    Value::function(context_.arrayIteratorNextFunction_),
                    error,
                    RuntimeContext::PropertyAttributes::writableOnly(true)) ||
                !context_.writeProperty(iteratorId, symbolIteratorKey,
                    Value::function(context_.arrayIteratorSelfFunction_),
                    error,
                    RuntimeContext::PropertyAttributes::writableOnly(true))) {
                context_.objects_.resize(objectCountBefore);
                context_.totalPropertyCount_ = propertyCountBefore;
                context_.totalPropertyKeyBytes_ = propertyKeyBytesBefore;
                fail(error, callSite);
                succeeded = false;
                break;
            }
            RuntimeContext::RuntimeObject* iterator =
                context_.objectAt(iteratorId);
            if (iterator == nullptr) {
                fail(RuntimeErrorCode::InvalidAstState, callSite);
                succeeded = false;
                break;
            }
            iterator->arrayIterator = true;
            iterator->arrayIteratorMode = mode;
            iterator->arrayIteratorSource = arrayId;
            result = Value::object(iteratorId);
            break;
        }
        case RuntimeContext::NativeFunctionId::ArrayIteratorNext: {
            const RuntimeObjectId iteratorId = receiver.isObject()
                ? receiver.objectId() : kInvalidRuntimeObjectId;
            const RuntimeContext::RuntimeObject* iterator =
                context_.objectAt(iteratorId);
            if (iterator == nullptr || !iterator->arrayIterator) {
                fail(RuntimeErrorCode::InvalidReceiver, callSite);
                succeeded = false;
                break;
            }

            const RuntimeObjectId arrayId = iterator->arrayIteratorSource;
            const std::size_t index = iterator->arrayIteratorIndex;
            const bool wasExhausted = iterator->arrayIteratorExhausted;
            const RuntimeContext::ArrayIteratorMode mode =
                iterator->arrayIteratorMode;
            if (mode != RuntimeContext::ArrayIteratorMode::Values &&
                mode != RuntimeContext::ArrayIteratorMode::Keys &&
                mode != RuntimeContext::ArrayIteratorMode::Entries) {
                fail(RuntimeErrorCode::InvalidAstState, callSite);
                succeeded = false;
                break;
            }
            Value yielded = Value::undefined();
            bool done = wasExhausted;
            if (!wasExhausted) {
                const RuntimeContext::RuntimeObject* array =
                    context_.objectAt(arrayId);
                if (array == nullptr || !array->array) {
                    fail(RuntimeErrorCode::InvalidAstState, callSite);
                    succeeded = false;
                    break;
                }
                if (index < array->arrayLength) {
                    if (mode ==
                        RuntimeContext::ArrayIteratorMode::Keys) {
                        yielded = Value::number(static_cast<double>(index));
                    } else {
                        RuntimeErrorCode lookupError =
                            RuntimeErrorCode::None;
                        if (!context_.readArrayIndex(arrayId, index, yielded,
                                lookupError, callSite)) {
                            fail(lookupError, callSite);
                            succeeded = false;
                            break;
                        }
                    }
                } else {
                    done = true;
                }
            }

            if (!succeeded) break;

            RuntimeErrorCode error = RuntimeErrorCode::None;
            const bool resultCreated = mode ==
                    RuntimeContext::ArrayIteratorMode::Entries && !done
                ? context_.createArrayIteratorEntryResult(index, yielded,
                    result, error)
                : context_.createArrayIteratorResult(yielded, done, result,
                    error);
            if (!resultCreated) {
                fail(error, callSite);
                succeeded = false;
                break;
            }
            RuntimeContext::RuntimeObject* updatedIterator =
                context_.objectAt(iteratorId);
            if (updatedIterator == nullptr ||
                !updatedIterator->arrayIterator) {
                fail(RuntimeErrorCode::InvalidAstState, callSite);
                succeeded = false;
                break;
            }
            if (done) {
                updatedIterator->arrayIteratorExhausted = true;
            } else {
                updatedIterator->arrayIteratorIndex = index + 1u;
            }
            break;
        }
        case RuntimeContext::NativeFunctionId::ArrayIteratorSelf: {
            const RuntimeContext::RuntimeObject* iterator = receiver.isObject()
                ? context_.objectAt(receiver.objectId()) : nullptr;
            if (iterator == nullptr || !iterator->arrayIterator) {
                fail(RuntimeErrorCode::InvalidReceiver, callSite);
                succeeded = false;
                break;
            }
            result = receiver;
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
        case RuntimeContext::NativeFunctionId::EventPreventDefault:
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
                const RuntimeContext::NativeFunctionId native =
                    static_cast<RuntimeContext::NativeFunctionId>(
                        function.nativeFunction);
                if (native == RuntimeContext::NativeFunctionId::EventStopPropagation ||
                    native == RuntimeContext::NativeFunctionId::EventStopImmediatePropagation)
                    context_.eventPropagationStopped_ = true;
                if (native == RuntimeContext::NativeFunctionId::EventStopImmediatePropagation) {
                    context_.eventImmediatePropagationStopped_ = true;
                }
                if (native == RuntimeContext::NativeFunctionId::EventPreventDefault &&
                    context_.eventCancelable_) {
                    context_.eventDefaultPrevented_ = true;
                    RuntimeErrorCode propertyError = RuntimeErrorCode::None;
                    if (!context_.updateExistingProperty(context_.eventObject_,
                            "defaultPrevented", Value::boolean(true),
                            propertyError)) {
                        fail(propertyError, callSite);
                        succeeded = false;
                    }
                }
            }
            result = Value::undefined();
            break;
        default:
            fail(RuntimeErrorCode::InvalidNativeFunction, callSite);
            succeeded = false;
            break;
        }

        unwindFrame(currentEnvironment_, callerThis, previousFrameCount,
            previousActiveCallFrames);
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
        case ValueType::Symbol:
        case ValueType::SymbolNamespace:
        case ValueType::ObjectNamespace:
            fail(RuntimeErrorCode::InvalidOperandType, SourceLocation());
            return false;
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
        case ValueType::Symbol:
        case ValueType::SymbolNamespace:
        case ValueType::ObjectNamespace:
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

    bool closeIterator(const Value& iterator, SourceLocation location)
    {
        Value returnMethod;
        if (!readValueProperty(iterator, "return", returnMethod, location))
            return false;
        if (returnMethod.isUndefined() || returnMethod.isNull()) return true;
        if (!returnMethod.isFunction()) {
            fail(RuntimeErrorCode::NotCallable, location);
            return false;
        }

        const std::vector<Value> noArguments;
        Value closeResult;
        if (!invokeFunction(returnMethod.functionId(), noArguments, location,
                closeResult, iterator)) return false;
        if (!closeResult.isObject() && !closeResult.isHostObject()) {
            fail(RuntimeErrorCode::NotObject, location);
            return false;
        }
        return true;
    }

    bool closeIteratorPreservingError(const Value& iterator,
        SourceLocation location)
    {
        const RuntimeError originalError = context_.result_.runtimeError;
        const ScriptStatus originalStatus = context_.result_.status;
        (void)closeIterator(iterator, location);
        if (originalError.code != RuntimeErrorCode::None) {
            context_.result_.runtimeError = originalError;
            context_.result_.status = originalStatus;
        }
        return false;
    }

    bool executeForOf(const AstNode& node, bool inFunction,
        Control& control)
    {
        if (!validNode(node.name, node.location) ||
            !validNode(node.expression, node.location) ||
            !validNode(node.body, node.location)) return false;
        if (context_.ast_.node(node.name).kind != AstNodeKind::Identifier) {
            fail(RuntimeErrorCode::InvalidAstState, node.location);
            return false;
        }

        Value iterable;
        if (!evalExpression(node.expression, iterable)) return false;
        RuntimeContext::PropertyKey iteratorKey;
        iteratorKey.symbol = WellKnownSymbol::Iterator;
        Value iteratorMethod;
        if (!readValueProperty(iterable, iteratorKey, iteratorMethod,
                node.location)) return false;
        if (!iteratorMethod.isFunction()) {
            fail(RuntimeErrorCode::NotCallable, node.location);
            return false;
        }

        const std::vector<Value> noArguments;
        Value iterator;
        if (!invokeFunction(iteratorMethod.functionId(), noArguments,
                node.location, iterator, iterable)) return false;
        if (!iterator.isObject() && !iterator.isHostObject()) {
            fail(RuntimeErrorCode::NotObject, node.location);
            return false;
        }

        Value nextMethod;
        if (!readValueProperty(iterator, "next", nextMethod, node.location))
            return false;
        if (!nextMethod.isFunction()) {
            fail(RuntimeErrorCode::NotCallable, node.location);
            return false;
        }

        SourceView bindingName;
        if (!identifierName(node.name, bindingName)) return false;
        const EnvironmentId loopEnvironment = currentEnvironment_;
        while (true) {
            Value iteratorResult;
            if (!invokeFunction(nextMethod.functionId(), noArguments,
                    node.location, iteratorResult, iterator)) return false;
            if (!iteratorResult.isObject() &&
                !iteratorResult.isHostObject()) {
                fail(RuntimeErrorCode::NotObject, node.location);
                return false;
            }

            Value done;
            if (!readValueProperty(iteratorResult, "done", done,
                    node.location)) return false;
            if (truthy(done)) return true;

            Value yielded;
            if (!readValueProperty(iteratorResult, "value", yielded,
                    node.location)) return false;

            EnvironmentId iterationEnvironment = kInvalidEnvironmentId;
            RuntimeErrorCode error = RuntimeErrorCode::None;
            if (!context_.createEnvironment(loopEnvironment,
                    iterationEnvironment, error)) {
                fail(error, node.location);
                return closeIteratorPreservingError(iterator, node.location);
            }
            currentEnvironment_ = iterationEnvironment;
            if (!declareIn(iterationEnvironment, bindingName, yielded,
                    context_.ast_.node(node.name).location,
                    node.immutableBinding)) {
                currentEnvironment_ = loopEnvironment;
                return closeIteratorPreservingError(iterator, node.location);
            }

            Control bodyControl;
            const bool bodySucceeded = executeStatement(node.body, true,
                inFunction, bodyControl);
            currentEnvironment_ = loopEnvironment;
            if (!bodySucceeded)
                return closeIteratorPreservingError(iterator, node.location);
            if (bodyControl.kind == ControlKind::Break)
                return closeIterator(iterator, node.location);
            if (bodyControl.kind == ControlKind::Return) {
                if (!closeIterator(iterator, node.location)) return false;
                control = bodyControl;
                return true;
            }
            // Continue and normal completion both advance this same iterator.
        }
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
        case AstNodeKind::ForOfStatement:
            return executeForOf(node, inFunction, control);
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
        case ValueType::Symbol:
        case ValueType::SymbolNamespace:
        case ValueType::ObjectNamespace:
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
        case ValueType::Symbol:
            return left.wellKnownSymbolValue() != WellKnownSymbol::None &&
                left.wellKnownSymbolValue() == right.wellKnownSymbolValue();
        case ValueType::SymbolNamespace:
            return true;
        case ValueType::ObjectNamespace:
            return true;
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
    std::size_t initialActiveCallFrames_ = 0u;
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
    const bool succeeded = invokeFunctionForTesting(function, arguments, result,
        error);
    if (!succeeded) result_.status = ScriptStatus::RuntimeFailure;
    return succeeded;
}

bool RuntimeContext::createOrUpdateEventObject(SourceView type,
    const HostObjectReference& target,
    const HostObjectReference& currentTarget, SourceView key,
    SourceView code, bool bubbles, bool cancelable,
    const HostObjectReference* relatedTarget, Value& result,
    RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    if (type.length == 0 ||
        (type.data == nullptr && type.length != 0)) {
        error = RuntimeErrorCode::InvalidHostReturn;
        return false;
    }

    Value typeValue;
    Value keyValue;
    Value codeValue;
    // Event properties use cached runtime strings. Keyboard transitions can
    // arrive indefinitely, but their bounded semantic values are retained
    // once per distinct spelling rather than once per hardware transition.
    if (!createOrGetCachedString(type, typeValue, error) ||
        !createOrGetCachedString(key, keyValue, error) ||
        !createOrGetCachedString(code, codeValue, error)) return false;
    const bool hasKeyboardProperties = key.length != 0 || code.length != 0;
    eventCancelable_ = cancelable;

    RuntimeHostObjectId targetObject = kInvalidRuntimeHostObjectId;
    if (!createHostObject(target, targetObject, error, true)) return false;
    RuntimeHostObjectId currentTargetObject = kInvalidRuntimeHostObjectId;
    if (!createHostObject(currentTarget, currentTargetObject, error, true))
        return false;
    Value relatedTargetValue = Value::nullValue();
    if (relatedTarget != nullptr) {
        RuntimeHostObjectId relatedTargetObject = kInvalidRuntimeHostObjectId;
        if (!createHostObject(*relatedTarget, relatedTargetObject, error, true))
            return false;
        relatedTargetValue = Value::hostObject(relatedTargetObject);
    }

    if (eventObject_ == kInvalidRuntimeObjectId && eventDispatchDepth_ > 0u)
        eventObject_ = eventObjectCache_[eventDispatchDepth_ - 1u];
    if (eventObject_ == kInvalidRuntimeObjectId ||
        objectAt(eventObject_) == nullptr) {
        const std::vector<Value> noElements;
        if (!createObject(false, noElements, eventObject_, error)) return false;
        if (eventDispatchDepth_ > 0u)
            eventObjectCache_[eventDispatchDepth_ - 1u] = eventObject_;
        // These native operations use the active Event receiver and dispatch
        // state, so one function record of each kind is sufficient for every
        // cached nested Event object. Sharing avoids consuming three more
        // entries from the fixed native-function pool at each nesting depth.
        const auto discardIncompleteEventObject = [this]() {
            eventObject_ = kInvalidRuntimeObjectId;
            if (eventDispatchDepth_ > 0u)
                eventObjectCache_[eventDispatchDepth_ - 1u] =
                    kInvalidRuntimeObjectId;
        };
        if (eventStopPropagationFunction_ == kInvalidRuntimeFunctionId &&
            !createNativeFunction(NativeFunctionId::EventStopPropagation,
                eventStopPropagationFunction_, error)) {
            discardIncompleteEventObject();
            return false;
        }
        if (eventStopImmediatePropagationFunction_ ==
                kInvalidRuntimeFunctionId &&
            !createNativeFunction(NativeFunctionId::EventStopImmediatePropagation,
                eventStopImmediatePropagationFunction_, error)) {
            discardIncompleteEventObject();
            return false;
        }
        if (eventPreventDefaultFunction_ == kInvalidRuntimeFunctionId &&
            !createNativeFunction(NativeFunctionId::EventPreventDefault,
                eventPreventDefaultFunction_, error)) {
            discardIncompleteEventObject();
            return false;
        }
        if (!writeProperty(eventObject_, "type", typeValue, error, true) ||
            !writeProperty(eventObject_, "target",
                Value::hostObject(targetObject), error, true) ||
            !writeProperty(eventObject_, "relatedTarget", relatedTargetValue,
                error, true) ||
            !writeProperty(eventObject_, "currentTarget",
                Value::hostObject(currentTargetObject), error, true) ||
            !writeProperty(eventObject_, "bubbles", Value::boolean(bubbles),
                error, true) ||
            !writeProperty(eventObject_, "cancelable", Value::boolean(cancelable),
                error, true) ||
            !writeProperty(eventObject_, "stopPropagation",
                Value::function(eventStopPropagationFunction_), error, true) ||
            !writeProperty(eventObject_, "stopImmediatePropagation",
                Value::function(eventStopImmediatePropagationFunction_), error,
                true) ||
            !writeProperty(eventObject_, "preventDefault",
                Value::function(eventPreventDefaultFunction_), error, true) ||
            !writeProperty(eventObject_, "defaultPrevented",
                Value::boolean(false), error, true) ||
            !writeProperty(eventObject_, "eventPhase",
                Value::number(static_cast<double>(eventPhase_)), error, true)) {
            discardIncompleteEventObject();
            return false;
        }
        if (hasKeyboardProperties &&
            (!writeProperty(eventObject_, "key", keyValue, error, true) ||
                !writeProperty(eventObject_, "code", codeValue, error, true))) {
            discardIncompleteEventObject();
            return false;
        }
    } else if (!updateExistingProperty(eventObject_, "type", typeValue, error) ||
        !updateExistingProperty(eventObject_, "target",
            Value::hostObject(targetObject), error) ||
        !updateExistingProperty(eventObject_, "relatedTarget", relatedTargetValue,
            error) ||
        !updateExistingProperty(eventObject_, "currentTarget",
            Value::hostObject(currentTargetObject), error) ||
        !updateExistingProperty(eventObject_, "bubbles", Value::boolean(bubbles),
            error) ||
        !updateExistingProperty(eventObject_, "cancelable",
            Value::boolean(cancelable), error)) {
        return false;
    }

    if (hasKeyboardProperties) {
        RuntimeErrorCode propertyError = RuntimeErrorCode::None;
        if (!updateExistingProperty(eventObject_, "key", keyValue,
                propertyError) &&
            !writeProperty(eventObject_, "key", keyValue, error, true))
            return false;
        propertyError = RuntimeErrorCode::None;
        if (!updateExistingProperty(eventObject_, "code", codeValue,
                propertyError) &&
            !writeProperty(eventObject_, "code", codeValue, error, true))
            return false;
    } else {
        RuntimeErrorCode propertyError = RuntimeErrorCode::None;
        (void)updateExistingProperty(eventObject_, "key", keyValue,
            propertyError);
        propertyError = RuntimeErrorCode::None;
        (void)updateExistingProperty(eventObject_, "code", codeValue,
            propertyError);
    }

    result = Value::object(eventObject_);
    return true;
}

bool RuntimeContext::beginEventDispatch()
{
    if (eventDispatchDepth_ >= kMaxEventDispatchDepth) return false;
    if (eventDispatchDepth_ > 0u) {
        EventDispatchState& saved = eventDispatchStack_[eventDispatchDepth_];
        saved.eventObject = eventObject_;
        saved.eventPhase = eventPhase_;
        saved.propagationStopped = eventPropagationStopped_;
        saved.immediatePropagationStopped = eventImmediatePropagationStopped_;
        saved.defaultPrevented = eventDefaultPrevented_;
        saved.cancelable = eventCancelable_;
    }
    ++eventDispatchDepth_;
    eventObject_ = eventObjectCache_[eventDispatchDepth_ - 1u];
    eventDispatchActive_ = true;
    eventPhase_ = kEventPhaseNone;
    eventPropagationStopped_ = false;
    eventImmediatePropagationStopped_ = false;
    eventDefaultPrevented_ = false;
    RuntimeErrorCode ignored = RuntimeErrorCode::None;
    if (eventObject_ != kInvalidRuntimeObjectId &&
        objectAt(eventObject_) != nullptr)
        (void)updateExistingProperty(eventObject_, "defaultPrevented",
            Value::boolean(false), ignored);
    if (eventObject_ != kInvalidRuntimeObjectId &&
        objectAt(eventObject_) != nullptr)
        (void)updateExistingProperty(eventObject_, "eventPhase",
            Value::number(static_cast<double>(eventPhase_)), ignored);
    return true;
}

void RuntimeContext::endEventDispatch()
{
    if (eventDispatchDepth_ == 0u) return;
    if (eventObject_ != kInvalidRuntimeObjectId &&
        objectAt(eventObject_) != nullptr) {
        RuntimeErrorCode ignored = RuntimeErrorCode::None;
        (void)updateExistingProperty(eventObject_, "eventPhase",
            Value::number(static_cast<double>(kEventPhaseNone)), ignored);
    }
    --eventDispatchDepth_;
    if (eventDispatchDepth_ > 0u) {
        const EventDispatchState& saved = eventDispatchStack_[eventDispatchDepth_];
        eventObject_ = saved.eventObject;
        eventPhase_ = saved.eventPhase;
        eventPropagationStopped_ = saved.propagationStopped;
        eventImmediatePropagationStopped_ = saved.immediatePropagationStopped;
        eventDefaultPrevented_ = saved.defaultPrevented;
        eventCancelable_ = saved.cancelable;
        eventDispatchActive_ = true;
        return;
    }
    eventDispatchActive_ = false;
    eventObject_ = eventObjectCache_[0];
    eventPhase_ = kEventPhaseNone;
    eventPropagationStopped_ = false;
    eventImmediatePropagationStopped_ = false;
    eventDefaultPrevented_ = false;
    RuntimeErrorCode ignored = RuntimeErrorCode::None;
    if (eventObject_ != kInvalidRuntimeObjectId &&
        objectAt(eventObject_) != nullptr)
        (void)updateExistingProperty(eventObject_, "eventPhase",
            Value::number(static_cast<double>(eventPhase_)), ignored);
}

void RuntimeContext::setEventPhase(std::uint8_t phase)
{
    if (phase > kEventPhaseBubbling) phase = kEventPhaseNone;
    eventPhase_ = phase;
    RuntimeErrorCode ignored = RuntimeErrorCode::None;
    if (eventObject_ != kInvalidRuntimeObjectId &&
        objectAt(eventObject_) != nullptr)
        (void)updateExistingProperty(eventObject_, "eventPhase",
            Value::number(static_cast<double>(eventPhase_)), ignored);
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
    GXOS_JS84_DIAGNOSTIC(diagnostics_ = RuntimeDiagnostics());
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
    eventConstantsObject_ = kInvalidRuntimeObjectId;
    eventObject_ = kInvalidRuntimeObjectId;
    eventObjectCache_.fill(kInvalidRuntimeObjectId);
    eventDispatchStack_ = {};
    eventDispatchDepth_ = 0;
    eventStopPropagationFunction_ = kInvalidRuntimeFunctionId;
    arrayIteratorNextFunction_ = kInvalidRuntimeFunctionId;
    arrayIteratorSelfFunction_ = kInvalidRuntimeFunctionId;
    objectKeysFunction_ = kInvalidRuntimeFunctionId;
    objectGetOwnPropertyDescriptorFunction_ = kInvalidRuntimeFunctionId;
    eventStopImmediatePropagationFunction_ = kInvalidRuntimeFunctionId;
    eventPreventDefaultFunction_ = kInvalidRuntimeFunctionId;
    builtInsInitialized_ = false;
    eventDispatchActive_ = false;
    eventPhase_ = kEventPhaseNone;
    eventPropagationStopped_ = false;
    eventImmediatePropagationStopped_ = false;
    eventDefaultPrevented_ = false;
    eventCancelable_ = true;
#if defined(GXOS_JAVASCRIPT_TEST_FAILURE_INJECTION)
    failNextArrayCreationForTesting_ = false;
    failNextArraySlotInsertionForTesting_ = false;
    failArraySlotInsertionAfterCountForTesting_ = false;
    arraySlotInsertionsBeforeFailureForTesting_ = 0u;
    failNextObjectKeysScratchAllocationForTesting_ = false;
    failNextObjectKeysStringCreationForTesting_ = false;
    failNextObjectKeysElementInsertionForTesting_ = false;
    failNextPropertyInsertionForTesting_ = false;
    failPropertyInsertionAfterCountForTesting_ = false;
    propertyInsertionsBeforeFailureForTesting_ = 0u;
#endif
    hostObjects_.clear();
    hostMethods_.clear();
    hostGlobalSpecs_.clear();
    liveHostObjectCount_ = 0;
    hostOperations_ = 0;
    hostCallActive_ = false;
    hostReentryAllowed_ = false;
    GXOS_JS84_DIAGNOSTIC(
        diagnostics_.environmentPoolHighWater = environmentCount());
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

bool RuntimeContext::createOrGetCachedString(SourceView text, Value& value,
    RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    if (text.data == nullptr && text.length != 0) {
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    }
    if (text.length > limits_.maxRuntimeStringLength) {
        error = RuntimeErrorCode::StringLimitExceeded;
        return false;
    }
    const char* bytes = text.data == nullptr ? "" : text.data;
    for (std::size_t index = 0; index < strings_.size(); ++index) {
        const std::string& existing = strings_[index];
        if (existing.size() == text.length &&
            (text.length == 0 ||
                std::char_traits<char>::compare(existing.data(), bytes,
                    text.length) == 0)) {
            value = Value::string(static_cast<RuntimeStringId>(index));
            return true;
        }
    }
    return createString(text, value, error);
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
        GXOS_JS84_DIAGNOSTIC(++diagnostics_.failedEnvironmentAllocations);
        error = RuntimeErrorCode::EnvironmentLimitExceeded;
        return false;
    }
    try {
        environments_.emplace_back(
            EnvironmentLimits{limits_.maxFunctionEnvironmentBindings,
                limits_.maxBindingNameLength}, parent);
    } catch (const std::bad_alloc&) {
        GXOS_JS84_DIAGNOSTIC(++diagnostics_.failedEnvironmentAllocations);
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    }
    GXOS_JS84_DIAGNOSTIC({
        ++diagnostics_.environmentsAllocated;
        if (diagnostics_.environmentPoolHighWater < environmentCount())
            diagnostics_.environmentPoolHighWater = environmentCount();
    });
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
        GXOS_JS84_DIAGNOSTIC(++diagnostics_.failedObjectAllocations);
        error = RuntimeErrorCode::ObjectLimitExceeded;
        return false;
    }
    if (!array && !initialElements.empty()) {
        error = RuntimeErrorCode::InvalidAstState;
        return false;
    }
    if (array) {
        if (initialElements.size() > maximumSupportedArrayLength(limits_) ||
            initialElements.size() > limits_.maxTotalArrayElements ||
            totalArrayElements_ > limits_.maxTotalArrayElements -
                initialElements.size()) {
            GXOS_JS84_DIAGNOSTIC(
                ++diagnostics_.failedArraySlotAllocations);
            error = RuntimeErrorCode::ArrayLimitExceeded;
            return false;
        }
        std::vector<RuntimeArrayElement> indexedElements;
        try {
            indexedElements.reserve(initialElements.size());
            for (std::size_t index = 0u; index < initialElements.size();
                ++index) {
                indexedElements.push_back({index, initialElements[index]});
            }
        } catch (const std::bad_alloc&) {
            GXOS_JS84_DIAGNOSTIC({
                ++diagnostics_.failedObjectAllocations;
                ++diagnostics_.failedArraySlotAllocations;
            });
            error = RuntimeErrorCode::AllocationFailure;
            return false;
        }
        return createArray(initialElements.size(),
            std::move(indexedElements), result, error, prototype);
    }
    try {
        RuntimeObject object;
        object.prototype = prototype;
        if (object.prototype == kInvalidRuntimeObjectId &&
            builtInsInitialized_) {
            object.prototype = objectPrototype_;
        }
        objects_.push_back(std::move(object));
    } catch (const std::bad_alloc&) {
        GXOS_JS84_DIAGNOSTIC({
            ++diagnostics_.failedObjectAllocations;
            if (array && !initialElements.empty())
                ++diagnostics_.failedArraySlotAllocations;
        });
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    }
    GXOS_JS84_DIAGNOSTIC({
        ++diagnostics_.runtimeObjectsAllocated;
        if (diagnostics_.objectPoolHighWater < objects_.size())
            diagnostics_.objectPoolHighWater = objects_.size();
    });
    result = static_cast<RuntimeObjectId>(objects_.size() - 1u);
    return true;
}

bool RuntimeContext::createArray(std::size_t logicalLength,
    std::vector<RuntimeArrayElement>&& initialElements,
    RuntimeObjectId& result, RuntimeErrorCode& error,
    RuntimeObjectId prototype)
{
    error = RuntimeErrorCode::None;
    if (objects_.size() >= limits_.maxObjects ||
        objects_.size() >= static_cast<std::size_t>(kInvalidRuntimeObjectId)) {
        GXOS_JS84_DIAGNOSTIC(++diagnostics_.failedObjectAllocations);
        error = RuntimeErrorCode::ObjectLimitExceeded;
        return false;
    }
    if (logicalLength > maximumSupportedArrayLength(limits_) ||
        initialElements.size() > limits_.maxArrayElements ||
        initialElements.size() > limits_.maxTotalArrayElements ||
        totalArrayElements_ > limits_.maxTotalArrayElements -
            initialElements.size()) {
        GXOS_JS84_DIAGNOSTIC(++diagnostics_.failedArraySlotAllocations);
        error = RuntimeErrorCode::ArrayLimitExceeded;
        return false;
    }
    std::size_t previousIndex = 0u;
    for (std::size_t position = 0u; position < initialElements.size();
        ++position) {
        const std::size_t index = initialElements[position].index;
        const PropertyAttributes& attributes =
            initialElements[position].attributes;
        if (index >= logicalLength || !attributes.isValid() ||
            !attributes.isFullyKnown() || !attributes.writable() ||
            !attributes.enumerable() || !attributes.configurable() ||
            (position != 0u && index <= previousIndex)) {
            error = RuntimeErrorCode::InvalidAstState;
            return false;
        }
        previousIndex = index;
    }
#if defined(GXOS_JAVASCRIPT_TEST_FAILURE_INJECTION)
    if (failNextArrayCreationForTesting_) {
        failNextArrayCreationForTesting_ = false;
        GXOS_JS84_DIAGNOSTIC({
            ++diagnostics_.failedObjectAllocations;
            ++diagnostics_.failedArraySlotAllocations;
        });
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    }
#endif
    try {
        RuntimeObject object;
        object.array = true;
        object.arrayLength = logicalLength;
        object.prototype = prototype;
        if (object.prototype == kInvalidRuntimeObjectId &&
            builtInsInitialized_) {
            object.prototype = arrayPrototype_;
        }
        object.elements = std::move(initialElements);
        objects_.push_back(std::move(object));
    } catch (const std::bad_alloc&) {
        GXOS_JS84_DIAGNOSTIC({
            ++diagnostics_.failedObjectAllocations;
            ++diagnostics_.failedArraySlotAllocations;
        });
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    }
    GXOS_JS84_DIAGNOSTIC({
        ++diagnostics_.runtimeObjectsAllocated;
        if (diagnostics_.objectPoolHighWater < objects_.size())
            diagnostics_.objectPoolHighWater = objects_.size();
    });
    result = static_cast<RuntimeObjectId>(objects_.size() - 1u);
    totalArrayElements_ += objects_.back().elements.size();
    GXOS_JS84_DIAGNOSTIC({
        diagnostics_.arraySlotsAllocated += objects_.back().elements.size();
        if (diagnostics_.arraySlotHighWater < totalArrayElements_)
            diagnostics_.arraySlotHighWater = totalArrayElements_;
    });
    return true;
}

std::size_t RuntimeContext::findArrayElementPosition(
    const RuntimeObject& object, std::size_t index)
{
    for (std::size_t position = 0u; position < object.elements.size();
        ++position) {
        if (object.elements[position].index >= index) return position;
    }
    return object.elements.size();
}

bool RuntimeContext::readArrayIndex(RuntimeObjectId objectId,
    std::size_t index, Value& value, RuntimeErrorCode& error,
    SourceLocation location)
{
    error = RuntimeErrorCode::None;
    RuntimeObjectId current = objectId;
    for (std::size_t depth = 0u; depth < limits_.maxPrototypeDepth; ++depth) {
        if (!consumeStep(location)) {
            error = RuntimeErrorCode::ExecutionBudgetExceeded;
            return false;
        }
        const RuntimeObject* object = objectAt(current);
        if (object == nullptr) {
            error = RuntimeErrorCode::CannotReadProperty;
            return false;
        }
        if (object->array && index < object->arrayLength) {
            const std::size_t position =
                findArrayElementPosition(*object, index);
            if (position < object->elements.size() &&
                object->elements[position].index == index) {
                value = object->elements[position].value;
                return true;
            }
        }
        for (const RuntimeProperty& property : object->properties) {
            if (property.symbolKey == WellKnownSymbol::None &&
                propertyKeyMatchesArrayIndex(property.key, index)) {
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

bool RuntimeContext::setArrayLength(RuntimeObjectId objectId,
    const Value& value, RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    RuntimeObject* object = objectAt(objectId);
    if (object == nullptr || !object->array) {
        error = RuntimeErrorCode::CannotWriteProperty;
        return false;
    }
    const PropertyAttributes lengthAttributes =
        PropertyAttributes::arrayLength();
    if (!lengthAttributes.isValid() || !lengthAttributes.hasWritable() ||
        !lengthAttributes.writable() || !lengthAttributes.hasEnumerable() ||
        lengthAttributes.enumerable() ||
        !lengthAttributes.hasConfigurable() ||
        lengthAttributes.configurable()) {
        error = RuntimeErrorCode::InvalidAstState;
        return false;
    }
    if (!value.isNumber()) {
        error = RuntimeErrorCode::TypeError;
        return false;
    }
    const double requestedLength = value.numberValue();
    if (!std::isfinite(requestedLength) || requestedLength < 0.0 ||
        std::floor(requestedLength) != requestedLength ||
        requestedLength > static_cast<double>(
            kMaximumJavaScriptArrayLength)) {
        error = RuntimeErrorCode::InvalidArrayLength;
        return false;
    }
    if (requestedLength > static_cast<double>(
            maximumSupportedArrayLength(limits_))) {
        error = RuntimeErrorCode::ArrayLimitExceeded;
        return false;
    }
    const std::size_t newLength = static_cast<std::size_t>(requestedLength);
    if (newLength < object->arrayLength) {
        std::size_t firstRemoved = object->elements.size();
        for (std::size_t position = 0u;
            position < object->elements.size(); ++position) {
            if (object->elements[position].index >= newLength) {
                firstRemoved = position;
                break;
            }
        }
        const std::size_t removed = object->elements.size() - firstRemoved;
        if (removed > totalArrayElements_) {
            error = RuntimeErrorCode::InvalidAstState;
            return false;
        }
        object->elements.resize(firstRemoved);
        totalArrayElements_ -= removed;
    }
    object->arrayLength = newLength;
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
            Value::function(function), installError,
            PropertyAttributes::writableOnly(true));
    };

    if (!install(objectPrototype_, "hasOwnProperty",
        NativeFunctionId::ObjectHasOwnProperty, error)) return false;
    if (!install(arrayPrototype_, "push", NativeFunctionId::ArrayPush, error) ||
        !install(arrayPrototype_, "pop", NativeFunctionId::ArrayPop, error)) {
        return false;
    }
    RuntimeFunctionId arrayIteratorCreate = kInvalidRuntimeFunctionId;
    if (!createNativeFunction(NativeFunctionId::ArrayIteratorCreate,
            arrayIteratorCreate, error)) return false;
    RuntimeFunctionId arrayIteratorCreateKeys =
        kInvalidRuntimeFunctionId;
    if (!createNativeFunction(NativeFunctionId::ArrayIteratorCreateKeys,
            arrayIteratorCreateKeys, error)) return false;
    RuntimeFunctionId arrayIteratorCreateEntries =
        kInvalidRuntimeFunctionId;
    if (!createNativeFunction(NativeFunctionId::ArrayIteratorCreateEntries,
            arrayIteratorCreateEntries, error)) return false;
    const Value arrayIteratorMethod = Value::function(arrayIteratorCreate);
    PropertyKey symbolIteratorKey;
    symbolIteratorKey.symbol = WellKnownSymbol::Iterator;
    if (!writeProperty(arrayPrototype_, "values", arrayIteratorMethod,
            error, PropertyAttributes::writableOnly(true)) ||
        !writeProperty(arrayPrototype_, "keys",
            Value::function(arrayIteratorCreateKeys), error,
            PropertyAttributes::writableOnly(true)) ||
        !writeProperty(arrayPrototype_, "entries",
            Value::function(arrayIteratorCreateEntries), error,
            PropertyAttributes::writableOnly(true)) ||
        !writeProperty(arrayPrototype_, symbolIteratorKey,
            arrayIteratorMethod, error,
            PropertyAttributes::writableOnly(true)) ||
        !createNativeFunction(NativeFunctionId::ArrayIteratorNext,
            arrayIteratorNextFunction_, error) ||
        !createNativeFunction(NativeFunctionId::ArrayIteratorSelf,
            arrayIteratorSelfFunction_, error)) {
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
    if (!createNativeFunction(NativeFunctionId::ObjectKeys,
            objectKeysFunction_, error) ||
        !createNativeFunction(
            NativeFunctionId::ObjectGetOwnPropertyDescriptor,
            objectGetOwnPropertyDescriptorFunction_, error) ||
        !install(arrayPrototype_, "forEach", NativeFunctionId::ArrayForEach,
            error) ||
        !install(arrayPrototype_, "map", NativeFunctionId::ArrayMap,
            error) ||
        !install(arrayPrototype_, "filter", NativeFunctionId::ArrayFilter,
            error)) return false;
    const char eventName[] = "Event";
    // Event constants are optional only when an explicitly tiny caller limit
    // cannot fit the bounded namespace. This keeps the older runtime limit
    // contracts meaningful while the default realm exposes Event normally.
    const std::size_t eventConstantKeyBytes =
        sizeof("NONE") - 1u + sizeof("CAPTURING_PHASE") - 1u +
        sizeof("AT_TARGET") - 1u + sizeof("BUBBLING_PHASE") - 1u;
    const bool canInstallEventConstants =
        limits_.maxObjects > objects_.size() &&
        limits_.maxPropertiesPerObject >= 4u &&
        limits_.maxTotalProperties >= totalPropertyCount_ + 4u &&
        limits_.maxPropertyNameLength >= sizeof("CAPTURING_PHASE") - 1u &&
        limits_.maxTotalPropertyKeyBytes >=
            totalPropertyKeyBytes_ + eventConstantKeyBytes &&
        limits_.maxBindings >= 2u &&
        limits_.maxBindingNameLength >= sizeof(eventName) - 1u;
    if (canInstallEventConstants) {
        if (!createObject(false, noElements, eventConstantsObject_, error,
                objectPrototype_)) return false;
        if (!writeProperty(eventConstantsObject_, "NONE",
                Value::number(static_cast<double>(kEventPhaseNone)), error,
                true) ||
            !writeProperty(eventConstantsObject_, "CAPTURING_PHASE",
                Value::number(static_cast<double>(kEventPhaseCapturing)), error,
                true) ||
            !writeProperty(eventConstantsObject_, "AT_TARGET",
                Value::number(static_cast<double>(kEventPhaseAtTarget)), error,
                true) ||
            !writeProperty(eventConstantsObject_, "BUBBLING_PHASE",
                Value::number(static_cast<double>(kEventPhaseBubbling)), error,
                true)) return false;
        if (!environment_.declare(
                SourceView(eventName, sizeof(eventName) - 1u),
                Value::object(eventConstantsObject_), environmentError)) {
            error = RuntimeErrorCode::BuiltInInitializationFailed;
            return false;
        }
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

bool RuntimeContext::readObjectPropertyForHost(RuntimeObjectId object,
    const std::string& key, Value& value, RuntimeErrorCode& error)
{
    return readProperty(object, key, value, error, SourceLocation());
}

bool RuntimeContext::createCollectionIteratorResult(const HostValue& value,
    bool done, Value& result, RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    result = Value::undefined();
    const std::size_t objectCountBefore = objects_.size();
    const std::size_t propertyCountBefore = totalPropertyCount_;
    const std::size_t propertyKeyBytesBefore = totalPropertyKeyBytes_;
    const std::size_t arrayElementCountBefore = totalArrayElements_;
    const std::vector<Value> noElements;
    RuntimeObjectId object = kInvalidRuntimeObjectId;
    if (!createObject(false, noElements, object, error)) return false;
    GXOS_JS84_DIAGNOSTIC(++diagnostics_.iteratorResultObjectsAllocated);

    const auto rollback = [this, objectCountBefore, propertyCountBefore,
        propertyKeyBytesBefore, arrayElementCountBefore]() {
        GXOS_JS84_DIAGNOSTIC(++diagnostics_.collectionResultRollbacks);
        objects_.resize(objectCountBefore);
        totalPropertyCount_ = propertyCountBefore;
        totalPropertyKeyBytes_ = propertyKeyBytesBefore;
        totalArrayElements_ = arrayElementCountBefore;
    };

    // Allocate the complete outer shape before converting a possible host
    // value. A later property-capacity failure must not retain a newly
    // canonicalized Element handle from this otherwise unpublished result.
    if (!writeProperty(object, "value", Value::undefined(), error) ||
        !writeProperty(object, "done", Value::boolean(done), error)) {
        rollback();
        return false;
    }

    Value convertedValue;
    if (!convertHostValue(value, kInvalidRuntimeHostObjectId, convertedValue,
            error, SourceLocation())) {
        rollback();
        return false;
    }
    RuntimeObject* record = objectAt(object);
    if (record == nullptr || record->properties.size() != 2u ||
        record->properties[0].symbolKey != WellKnownSymbol::None ||
        record->properties[0].key != "value" ||
        record->properties[1].symbolKey != WellKnownSymbol::None ||
        record->properties[1].key != "done") {
        error = RuntimeErrorCode::InvalidAstState;
        rollback();
        return false;
    }
    record->properties[0].value = convertedValue;
    result = Value::object(object);
    GXOS_JS84_DIAGNOSTIC(++diagnostics_.iteratorResultObjectsPublished);
    return true;
}

bool RuntimeContext::createArrayIteratorResult(const Value& value, bool done,
    Value& result, RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    result = Value::undefined();
    const std::size_t objectCountBefore = objects_.size();
    const std::size_t propertyCountBefore = totalPropertyCount_;
    const std::size_t propertyKeyBytesBefore = totalPropertyKeyBytes_;
    const std::size_t arrayElementCountBefore = totalArrayElements_;
    const std::vector<Value> noElements;
    RuntimeObjectId object = kInvalidRuntimeObjectId;
    if (!createObject(false, noElements, object, error)) return false;
    GXOS_JS84_DIAGNOSTIC(++diagnostics_.iteratorResultObjectsAllocated);

    const auto rollback = [this, objectCountBefore, propertyCountBefore,
        propertyKeyBytesBefore, arrayElementCountBefore]() {
        GXOS_JS84_DIAGNOSTIC(++diagnostics_.arrayIteratorResultRollbacks);
        objects_.resize(objectCountBefore);
        totalPropertyCount_ = propertyCountBefore;
        totalPropertyKeyBytes_ = propertyKeyBytesBefore;
        totalArrayElements_ = arrayElementCountBefore;
    };

    if (!writeProperty(object, "value", Value::undefined(), error) ||
        !writeProperty(object, "done", Value::boolean(done), error)) {
        rollback();
        return false;
    }
    RuntimeObject* record = objectAt(object);
    if (record == nullptr || record->properties.size() != 2u ||
        record->properties[0].symbolKey != WellKnownSymbol::None ||
        record->properties[0].key != "value" ||
        record->properties[1].symbolKey != WellKnownSymbol::None ||
        record->properties[1].key != "done") {
        error = RuntimeErrorCode::InvalidAstState;
        rollback();
        return false;
    }
    record->properties[0].value = value;
    result = Value::object(object);
    GXOS_JS84_DIAGNOSTIC(++diagnostics_.iteratorResultObjectsPublished);
    return true;
}

bool RuntimeContext::createArrayIteratorEntryResult(std::size_t index,
    const Value& value, Value& result, RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    result = Value::undefined();
    const std::size_t objectCountBefore = objects_.size();
    const std::size_t propertyCountBefore = totalPropertyCount_;
    const std::size_t propertyKeyBytesBefore = totalPropertyKeyBytes_;
    const std::size_t arrayElementCountBefore = totalArrayElements_;

    const auto rollback = [this, objectCountBefore, propertyCountBefore,
        propertyKeyBytesBefore, arrayElementCountBefore]() {
        GXOS_JS84_DIAGNOSTIC(++diagnostics_.arrayIteratorEntryRollbacks);
        objects_.resize(objectCountBefore);
        totalPropertyCount_ = propertyCountBefore;
        totalPropertyKeyBytes_ = propertyKeyBytesBefore;
        totalArrayElements_ = arrayElementCountBefore;
    };

    std::vector<RuntimeArrayElement> noElements;
    RuntimeObjectId pairId = kInvalidRuntimeObjectId;
    if (!createArray(2u, std::move(noElements), pairId, error)) return false;
    GXOS_JS84_DIAGNOSTIC(++diagnostics_.iteratorEntryArraysAllocated);

    if (!writeProperty(pairId, std::string("0"),
            Value::number(static_cast<double>(index)), error) ||
        !writeProperty(pairId, std::string("1"), value, error)) {
        rollback();
        return false;
    }

    if (!createArrayIteratorResult(Value::object(pairId), false, result,
            error)) {
        rollback();
        return false;
    }
    GXOS_JS84_DIAGNOSTIC(++diagnostics_.iteratorEntryArraysPublished);
    return true;
}

bool RuntimeContext::createCollectionIteratorEntryResult(
    std::uint32_t index, const HostValue& element, Value& result,
    RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    result = Value::undefined();

    const std::size_t objectCountBefore = objects_.size();
    const std::size_t propertyCountBefore = totalPropertyCount_;
    const std::size_t propertyKeyBytesBefore = totalPropertyKeyBytes_;
    const std::size_t arrayElementCountBefore = totalArrayElements_;
    const std::size_t hostObjectCountBefore = hostObjects_.size();
    const std::size_t liveHostObjectCountBefore = liveHostObjectCount_;

    std::size_t reusableHostObjectSlot = hostObjects_.size();
    HostObjectRecord reusableHostObjectBefore;
    bool elementAlreadyRegistered = false;
    if (element.type == HostValueType::HostObject) {
        for (std::size_t slot = 0u; slot < hostObjects_.size(); ++slot) {
            const HostObjectRecord& record = hostObjects_[slot];
            if (record.live && record.reference == element.hostObject) {
                elementAlreadyRegistered = true;
                break;
            }
            if (!record.live && reusableHostObjectSlot == hostObjects_.size()) {
                reusableHostObjectSlot = slot;
                reusableHostObjectBefore = record;
            }
        }
    }

    const auto rollback = [this, objectCountBefore, propertyCountBefore,
        propertyKeyBytesBefore, arrayElementCountBefore, hostObjectCountBefore,
        liveHostObjectCountBefore, reusableHostObjectSlot,
        reusableHostObjectBefore, elementAlreadyRegistered]() {
        GXOS_JS84_DIAGNOSTIC(++diagnostics_.collectionEntryRollbacks);
        objects_.resize(objectCountBefore);
        totalPropertyCount_ = propertyCountBefore;
        totalPropertyKeyBytes_ = propertyKeyBytesBefore;
        totalArrayElements_ = arrayElementCountBefore;
        if (!elementAlreadyRegistered) {
            if (reusableHostObjectSlot < hostObjectCountBefore) {
                hostObjects_[reusableHostObjectSlot] =
                    reusableHostObjectBefore;
            } else {
                hostObjects_.resize(hostObjectCountBefore);
            }
            liveHostObjectCount_ = liveHostObjectCountBefore;
        }
    };

    Value convertedElement;
    if (!convertHostValue(element, kInvalidRuntimeHostObjectId,
            convertedElement, error, SourceLocation())) {
        rollback();
        return false;
    }

    std::vector<Value> pairElements;
    try {
        pairElements.reserve(2u);
        pairElements.push_back(Value::number(static_cast<double>(index)));
        pairElements.push_back(convertedElement);
    } catch (const std::bad_alloc&) {
        error = RuntimeErrorCode::AllocationFailure;
        rollback();
        return false;
    }

    RuntimeObjectId pairObject = kInvalidRuntimeObjectId;
    if (!createObject(true, pairElements, pairObject, error)) {
        rollback();
        return false;
    }
    GXOS_JS84_DIAGNOSTIC(++diagnostics_.iteratorEntryArraysAllocated);

    if (!createCollectionIteratorResult(HostValue::object(pairObject), false,
            result, error)) {
        rollback();
        return false;
    }
    GXOS_JS84_DIAGNOSTIC(++diagnostics_.iteratorEntryArraysPublished);
    return true;
}

bool RuntimeContext::toNumberForHostArgument(const HostValue& input,
    double& number)
{
    switch (input.type) {
    case HostValueType::Undefined:
        number = std::numeric_limits<double>::quiet_NaN();
        return true;
    case HostValueType::Null:
        number = 0.0;
        return true;
    case HostValueType::Boolean:
        number = input.booleanValue ? 1.0 : 0.0;
        return true;
    case HostValueType::Number:
        number = input.numberValue;
        return true;
    case HostValueType::String:
        if (input.stringValue.data == nullptr &&
            input.stringValue.length != 0u) return false;
        if (!parseNumber(input.stringValue, number))
            number = std::numeric_limits<double>::quiet_NaN();
        return true;
    case HostValueType::Function:
    case HostValueType::Object:
    case HostValueType::HostObject:
    case HostValueType::Method:
        return false;
    }
    return false;
}

bool RuntimeContext::readProperty(RuntimeObjectId objectId,
    const std::string& key, Value& value, RuntimeErrorCode& error,
    SourceLocation location)
{
    PropertyKey propertyKey;
    propertyKey.string = key;
    return readProperty(objectId, propertyKey, value, error, location);
}

bool RuntimeContext::readProperty(RuntimeObjectId objectId,
    const PropertyKey& key, Value& value, RuntimeErrorCode& error,
    SourceLocation location)
{
    error = RuntimeErrorCode::None;
    if (key.isSymbol() && key.symbol != WellKnownSymbol::Iterator) {
        error = RuntimeErrorCode::InvalidPropertyKey;
        return false;
    }
    if (!key.isSymbol() && key.string.size() > limits_.maxPropertyNameLength) {
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
        if (!key.isSymbol() && object->array && key.string == "length") {
            value = Value::number(static_cast<double>(object->arrayLength));
            return true;
        }
        std::uint32_t standardIndex = 0u;
        if (!key.isSymbol() && object->array &&
            parseEcmaArrayIndex(key.string, standardIndex)) {
            const std::size_t index =
                static_cast<std::size_t>(standardIndex);
            if (index > limits_.maxArrayIndex) {
                error = RuntimeErrorCode::ArrayIndexOutOfRange;
                return false;
            }
            if (index < object->arrayLength) {
                const std::size_t position =
                    findArrayElementPosition(*object, index);
                if (position < object->elements.size() &&
                    object->elements[position].index == index) {
                    value = object->elements[position].value;
                    return true;
                }
            }
        }
        for (const RuntimeProperty& property : object->properties) {
            const bool matches = key.isSymbol()
                ? property.symbolKey == key.symbol
                : property.symbolKey == WellKnownSymbol::None &&
                    property.key == key.string;
            if (matches) {
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
    PropertyKey propertyKey;
    propertyKey.string = key;
    return writeProperty(objectId, propertyKey, value, error,
        readOnly ? PropertyAttributes::writableOnly(false)
            : PropertyAttributes::ordinaryData());
}

bool RuntimeContext::writeProperty(RuntimeObjectId objectId,
    const PropertyKey& key, Value value, RuntimeErrorCode& error,
    bool readOnly)
{
    return writeProperty(objectId, key, value, error,
        readOnly ? PropertyAttributes::writableOnly(false)
            : PropertyAttributes::ordinaryData());
}

bool RuntimeContext::writeProperty(RuntimeObjectId objectId,
    const std::string& key, Value value, RuntimeErrorCode& error,
    const PropertyAttributes& attributes)
{
    PropertyKey propertyKey;
    propertyKey.string = key;
    return writeProperty(objectId, propertyKey, value, error, attributes);
}

bool RuntimeContext::writeProperty(RuntimeObjectId objectId,
    const PropertyKey& key, Value value, RuntimeErrorCode& error,
    const PropertyAttributes& attributes)
{
    error = RuntimeErrorCode::None;
    if (!attributes.isValid() || !attributes.hasWritable()) {
        error = RuntimeErrorCode::InvalidAstState;
        return false;
    }
    if (key.isSymbol() && key.symbol != WellKnownSymbol::Iterator) {
        error = RuntimeErrorCode::InvalidPropertyKey;
        return false;
    }
    RuntimeObject* object = objectAt(objectId);
    if (object == nullptr) {
        error = RuntimeErrorCode::CannotWriteProperty;
        return false;
    }
    if (!key.isSymbol() && key.string.size() > limits_.maxPropertyNameLength) {
        error = RuntimeErrorCode::PropertyNameTooLong;
        return false;
    }
    if (!key.isSymbol() && object->array && key.string == "length") {
        return setArrayLength(objectId, value, error);
    }
    std::uint32_t standardIndex = 0u;
    if (!key.isSymbol() && object->array &&
        parseEcmaArrayIndex(key.string, standardIndex)) {
        if (attributes.bits != PropertyAttributes::ordinaryData().bits) {
            error = RuntimeErrorCode::InvalidAstState;
            return false;
        }
        const std::size_t index =
            static_cast<std::size_t>(standardIndex);
        if (index > limits_.maxArrayIndex) {
            error = RuntimeErrorCode::ArrayIndexOutOfRange;
            return false;
        }
        if (index >= maximumSupportedArrayLength(limits_)) {
            error = RuntimeErrorCode::ArrayLimitExceeded;
            return false;
        }
        const std::size_t position =
            findArrayElementPosition(*object, index);
        if (position < object->elements.size() &&
            object->elements[position].index == index) {
            object->elements[position].value = value;
        } else {
            if (object->elements.size() >= limits_.maxArrayElements ||
                totalArrayElements_ >= limits_.maxTotalArrayElements) {
                GXOS_JS84_DIAGNOSTIC(
                    ++diagnostics_.failedArraySlotAllocations);
                error = RuntimeErrorCode::ArrayLimitExceeded;
                return false;
            }
            try {
#if defined(GXOS_JAVASCRIPT_TEST_FAILURE_INJECTION)
                if (failNextArraySlotInsertionForTesting_) {
                    failNextArraySlotInsertionForTesting_ = false;
                    throw std::bad_alloc();
                }
                if (failArraySlotInsertionAfterCountForTesting_) {
                    if (arraySlotInsertionsBeforeFailureForTesting_ == 0u) {
                        failArraySlotInsertionAfterCountForTesting_ = false;
                        throw std::bad_alloc();
                    }
                    --arraySlotInsertionsBeforeFailureForTesting_;
                }
#endif
                object->elements.push_back({index, value});
            } catch (const std::bad_alloc&) {
                GXOS_JS84_DIAGNOSTIC(
                    ++diagnostics_.failedArraySlotAllocations);
                error = RuntimeErrorCode::AllocationFailure;
                return false;
            }
            for (std::size_t cursor = object->elements.size() - 1u;
                cursor > position; --cursor) {
                object->elements[cursor] = object->elements[cursor - 1u];
            }
            object->elements[position] = {index, value};
            ++totalArrayElements_;
            GXOS_JS84_DIAGNOSTIC({
                ++diagnostics_.arraySlotsAllocated;
                if (diagnostics_.arraySlotHighWater < totalArrayElements_)
                    diagnostics_.arraySlotHighWater = totalArrayElements_;
            });
        }
        const std::size_t newLength = index + 1u;
        if (newLength > object->arrayLength) object->arrayLength = newLength;
        return true;
    }

    for (RuntimeProperty& property : object->properties) {
        const bool matches = key.isSymbol()
            ? property.symbolKey == key.symbol
            : property.symbolKey == WellKnownSymbol::None &&
                property.key == key.string;
        if (matches) {
            if (!property.attributes.isValid() ||
                !property.attributes.hasWritable()) {
                error = RuntimeErrorCode::InvalidAstState;
                return false;
            }
            // Non-writable properties retain the established deterministic
            // no-op assignment behavior. Host event refreshes use the
            // dedicated updateExistingProperty path below.
            if (!property.attributes.writable()) return true;
            property.value = value;
            return true;
        }
    }
    if (object->properties.size() >= limits_.maxPropertiesPerObject ||
        totalPropertyCount_ >= limits_.maxTotalProperties) {
        GXOS_JS84_DIAGNOSTIC(++diagnostics_.failedPropertyAllocations);
        error = RuntimeErrorCode::PropertyLimitExceeded;
        return false;
    }
    const std::size_t keyBytes = key.isSymbol() ? 0u : key.string.size();
    if (keyBytes > limits_.maxTotalPropertyKeyBytes ||
        totalPropertyKeyBytes_ > limits_.maxTotalPropertyKeyBytes - keyBytes) {
        GXOS_JS84_DIAGNOSTIC(++diagnostics_.failedPropertyAllocations);
        error = RuntimeErrorCode::PropertyLimitExceeded;
        return false;
    }
    try {
        RuntimeProperty property;
        if (!key.isSymbol()) property.key = key.string;
        property.symbolKey = key.symbol;
        property.value = value;
        property.attributes = attributes;
#if defined(GXOS_JAVASCRIPT_TEST_FAILURE_INJECTION)
        if (failNextPropertyInsertionForTesting_) {
            failNextPropertyInsertionForTesting_ = false;
            throw std::bad_alloc();
        }
        if (failPropertyInsertionAfterCountForTesting_) {
            if (propertyInsertionsBeforeFailureForTesting_ == 0u) {
                failPropertyInsertionAfterCountForTesting_ = false;
                throw std::bad_alloc();
            }
            --propertyInsertionsBeforeFailureForTesting_;
        }
#endif
        object->properties.push_back(std::move(property));
    } catch (const std::bad_alloc&) {
        GXOS_JS84_DIAGNOSTIC(++diagnostics_.failedPropertyAllocations);
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    }
    ++totalPropertyCount_;
    totalPropertyKeyBytes_ += keyBytes;
    GXOS_JS84_DIAGNOSTIC({
        ++diagnostics_.runtimePropertiesAllocated;
        if (diagnostics_.propertyPoolHighWater < totalPropertyCount_)
            diagnostics_.propertyPoolHighWater = totalPropertyCount_;
    });
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
        if (property.symbolKey == WellKnownSymbol::None &&
            property.key == key) {
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
    case HostResultCode::TypeError:
        return RuntimeErrorCode::TypeError;
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
    case HostResultCode::IteratorLimitExceeded:
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

bool RuntimeContext::resolveHostObjectIgnoringGeneration(
    RuntimeHostObjectId object, HostObjectReference& reference) const
{
    if (object == kInvalidRuntimeHostObjectId) return false;
    const std::size_t slot = hostObjectSlot(object);
    if (slot >= hostObjects_.size()) return false;
    const HostObjectRecord& record = hostObjects_[slot];
    if (!record.reference.valid()) return false;
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
    if (hostCallActive_ && !hostReentryAllowed_) {
        error = RuntimeErrorCode::HostReentryUnsupported;
        return false;
    }

    HostResult result;
    const bool previousHostCallActive = hostCallActive_;
    hostCallActive_ = true;
    try {
        result = hostAdapter_->validate(reference);
    } catch (const std::bad_alloc&) {
        hostCallActive_ = previousHostCallActive;
        error = RuntimeErrorCode::HostCallFailed;
        return false;
    } catch (...) {
        hostCallActive_ = previousHostCallActive;
        error = RuntimeErrorCode::HostCallFailed;
        return false;
    }
    hostCallActive_ = previousHostCallActive;
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
        GXOS_JS84_DIAGNOSTIC(++diagnostics_.failedHostHandleAllocations);
        error = RuntimeErrorCode::HostObjectLimitExceeded;
        return false;
    }
    try {
        if (slot == hostObjects_.size()) hostObjects_.push_back(HostObjectRecord());
        hostObjects_[slot].live = true;
        hostObjects_[slot].reference = input;
    } catch (const std::bad_alloc&) {
        GXOS_JS84_DIAGNOSTIC(++diagnostics_.failedHostHandleAllocations);
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    }
    ++liveHostObjectCount_;
    GXOS_JS84_DIAGNOSTIC({
        ++diagnostics_.hostHandlesCreated;
        if (diagnostics_.hostHandleLiveHighWater < liveHostObjectCount_)
            diagnostics_.hostHandleLiveHighWater = liveHostObjectCount_;
        if (diagnostics_.hostHandleRegistryHighWater < hostObjects_.size())
            diagnostics_.hostHandleRegistryHighWater = hostObjects_.size();
    });
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
    PropertyKey propertyKey;
    propertyKey.string = key;
    return readHostProperty(object, propertyKey, value, error, location);
}

bool RuntimeContext::readHostProperty(RuntimeHostObjectId object,
    const PropertyKey& key, Value& value, RuntimeErrorCode& error,
    SourceLocation location)
{
    error = RuntimeErrorCode::None;
    if (key.isSymbol() && key.symbol != WellKnownSymbol::Iterator) {
        error = RuntimeErrorCode::InvalidPropertyKey;
        return false;
    }
    if (!key.isSymbol() &&
        key.string.size() > limits_.maxHostPropertyNameLength) {
        error = RuntimeErrorCode::PropertyNameTooLong;
        return false;
    }
    if (!consumeHostOperation(location)) {
        error = RuntimeErrorCode::HostOperationBudgetExceeded;
        return false;
    }
    if (hostAdapter_ == nullptr) {
        error = RuntimeErrorCode::InvalidHostObject;
        return false;
    }
    HostObjectReference reference;
    bool stalePureProperty = false;
    if (!resolveHostObject(object, reference, error)) {
        if (!resolveHostObjectIgnoringGeneration(object, reference) ||
            reference.generation == hostGeneration_ ||
            key.isSymbol() || !hostAdapter_->allowsStaleHostProperty(reference,
                SourceView(key.string.data(), key.string.size()))) return false;
        stalePureProperty = true;
    }
    if (hostCallActive_ && !hostReentryAllowed_) {
        error = RuntimeErrorCode::HostReentryUnsupported;
        return false;
    }

    HostResult validation;
    HostResult operation;
    HostValue hostValue;
    const bool previousHostCallActive = hostCallActive_;
    hostCallActive_ = true;
    try {
        validation = stalePureProperty ? HostResult() :
            hostAdapter_->validate(reference);
        if (validation.succeeded()) {
            operation = key.isSymbol()
                ? hostAdapter_->getSymbolProperty(reference, key.symbol,
                    hostValue)
                : hostAdapter_->getProperty(reference,
                    SourceView(key.string.data(), key.string.size()),
                    hostValue);
        }
    } catch (const std::bad_alloc&) {
        hostCallActive_ = previousHostCallActive;
        error = RuntimeErrorCode::HostCallFailed;
        return false;
    } catch (...) {
        hostCallActive_ = previousHostCallActive;
        error = RuntimeErrorCode::HostCallFailed;
        return false;
    }
    hostCallActive_ = previousHostCallActive;
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
    PropertyKey propertyKey;
    propertyKey.string = key;
    return writeHostProperty(object, propertyKey, value, error, location);
}

bool RuntimeContext::writeHostProperty(RuntimeHostObjectId object,
    const PropertyKey& key, Value value, RuntimeErrorCode& error,
    SourceLocation location)
{
    error = RuntimeErrorCode::None;
    if (key.isSymbol() && key.symbol != WellKnownSymbol::Iterator) {
        error = RuntimeErrorCode::InvalidPropertyKey;
        return false;
    }
    if (!key.isSymbol() &&
        key.string.size() > limits_.maxHostPropertyNameLength) {
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
    if (hostCallActive_ && !hostReentryAllowed_) {
        error = RuntimeErrorCode::HostReentryUnsupported;
        return false;
    }
    HostResult validation;
    HostResult operation;
    const bool previousHostCallActive = hostCallActive_;
    hostCallActive_ = true;
    try {
        validation = hostAdapter_->validate(reference);
        if (validation.succeeded()) {
            operation = key.isSymbol()
                ? hostAdapter_->setSymbolProperty(reference, key.symbol,
                    hostValue)
                : hostAdapter_->setProperty(reference,
                    SourceView(key.string.data(), key.string.size()),
                    hostValue);
        }
    } catch (const std::bad_alloc&) {
        hostCallActive_ = previousHostCallActive;
        error = RuntimeErrorCode::HostCallFailed;
        return false;
    } catch (...) {
        hostCallActive_ = previousHostCallActive;
        error = RuntimeErrorCode::HostCallFailed;
        return false;
    }
    hostCallActive_ = previousHostCallActive;
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
    case ValueType::Symbol:
    case ValueType::SymbolNamespace:
    case ValueType::ObjectNamespace:
        error = RuntimeErrorCode::HostInvalidValue;
        (void)location;
        return false;
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
    SourceLocation location, Value& result,
    HostFunctionInvoker& functionInvoker)
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
            if (!resolveHostObjectIgnoringGeneration(receiver.hostObjectId(),
                    target) || target.generation == hostGeneration_ ||
                hostAdapter_ == nullptr ||
                !hostAdapter_->allowsStaleHostMethod(function.hostMethod)) {
                setRuntimeError(error, location);
                return false;
            }
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
                if (error != RuntimeErrorCode::StaleHostObject ||
                    !argument.isHostObject() || hostAdapter_ == nullptr ||
                    !hostAdapter_->allowsStaleHostMethodArgument(
                        function.hostMethod)) {
                    setRuntimeError(error, location);
                    return false;
                }
                // The slot for a stale handle may already identify a new
                // generation's object. Never recover its serial from that
                // reused slot; pass an invalid host reference so the adapter
                // can return the predicate's safe false sentinel.
                hostArgument = HostValue::fromHostObject(HostObjectReference());
                error = RuntimeErrorCode::None;
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
    const bool methodAllowsReentry =
        hostAdapter_->allowsReentrantCall(function.hostMethod);
    if (hostCallActive_ && !methodAllowsReentry) {
        setRuntimeError(RuntimeErrorCode::HostReentryUnsupported, location);
        return false;
    }

    ++activeCallFrames_;
    HostResult validation;
    HostResult operation;
    HostValue hostResult;
    RuntimeErrorCode callbackError = RuntimeErrorCode::None;
    const bool previousHostCallActive = hostCallActive_;
    const bool previousHostReentryAllowed = hostReentryAllowed_;
    hostCallActive_ = true;
    if (methodAllowsReentry) hostReentryAllowed_ = true;
    const bool stalePureMethod = target.generation != hostGeneration_ &&
        hostAdapter_->allowsStaleHostMethod(function.hostMethod);
    try {
        validation = stalePureMethod ? HostResult() :
            hostAdapter_->validate(target);
        if (validation.succeeded()) {
            operation = hostAdapter_->callWithRuntime(*this, functionInvoker,
                callbackError, receiverReference, function.hostMethod,
                hostArguments.empty() ? nullptr : hostArguments.data(),
                hostArguments.size(), hostResult);
        }
    } catch (const std::bad_alloc&) {
        hostCallActive_ = previousHostCallActive;
        hostReentryAllowed_ = previousHostReentryAllowed;
        --activeCallFrames_;
        setRuntimeError(RuntimeErrorCode::HostCallFailed, location);
        return false;
    } catch (...) {
        hostCallActive_ = previousHostCallActive;
        hostReentryAllowed_ = previousHostReentryAllowed;
        --activeCallFrames_;
        setRuntimeError(RuntimeErrorCode::HostCallFailed, location);
        return false;
    }
    hostCallActive_ = previousHostCallActive;
    hostReentryAllowed_ = previousHostReentryAllowed;
    if (!validation.succeeded()) {
        --activeCallFrames_;
        setRuntimeError(mapHostResult(validation.code), location);
        return false;
    }
    if (!operation.succeeded()) {
        --activeCallFrames_;
        setRuntimeError(callbackError == RuntimeErrorCode::None
            ? mapHostResult(operation.code) : callbackError, location);
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
    case RuntimeErrorCode::InvalidArrayLength: return "InvalidArrayLength";
    case RuntimeErrorCode::InvalidPropertyKey: return "InvalidPropertyKey";
    case RuntimeErrorCode::InvalidReceiver: return "InvalidReceiver";
    case RuntimeErrorCode::TypeError: return "TypeError";
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
