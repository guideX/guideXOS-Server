#include "runtime.h"
#include "json_parser.h"
#include "json_number_host.h"
#include "json_number_impl.inl"
#include "json_serializer.h"
#include "json_writer.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
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

RuntimeErrorCode runtimeEnvironmentError(EnvironmentErrorCode code)
{
    switch (code) {
    case EnvironmentErrorCode::None:
    case EnvironmentErrorCode::InvalidStorage:
    case EnvironmentErrorCode::InvalidAlignment:
    case EnvironmentErrorCode::InvalidInput:
    case EnvironmentErrorCode::InvalidReference:
    case EnvironmentErrorCode::InvalidEnvironment:
    case EnvironmentErrorCode::StaleGeneration:
    case EnvironmentErrorCode::BindingAlreadyInitialized:
    case EnvironmentErrorCode::SizeOverflow:
    case EnvironmentErrorCode::ParentCycle:
    case EnvironmentErrorCode::InternalInvariantFailure:
        return RuntimeErrorCode::InvalidAstState;
    case EnvironmentErrorCode::MissingBinding:
        return RuntimeErrorCode::UnknownIdentifier;
    case EnvironmentErrorCode::UninitializedBinding:
        return RuntimeErrorCode::UninitializedBinding;
    case EnvironmentErrorCode::ImmutableAssignment:
        return RuntimeErrorCode::TypeError;
    case EnvironmentErrorCode::DuplicateLexicalBinding:
        return RuntimeErrorCode::DuplicateBinding;
    case EnvironmentErrorCode::BindingLimitExceeded:
    case EnvironmentErrorCode::CapacityExceeded:
        return RuntimeErrorCode::BindingLimitExceeded;
    case EnvironmentErrorCode::BindingNameTooLong:
        return RuntimeErrorCode::BindingNameTooLong;
    case EnvironmentErrorCode::AllocationFailure:
        return RuntimeErrorCode::AllocationFailure;
    }
    return RuntimeErrorCode::InvalidAstState;
}

} // namespace

class RuntimeContext::Evaluator : public HostFunctionInvoker {
public:
    explicit Evaluator(RuntimeContext& context)
        : context_(context),
          initialActiveCallFrames_(context.activeCallFrames_),
          currentEnvironment_(context.globalEnvironmentId_) {}

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
        for (AstNodeId id = 0; id < context_.ast_.nodeCount(); ++id) {
            const AstNodeKind kind = context_.ast_.node(id).kind;
            if (kind == AstNodeKind::FunctionExpression ||
                kind == AstNodeKind::ArrowFunctionExpression) {
                if (!validateFunctionPolicy(id, true)) return false;
            }
        }
        if (!preflightGlobalBindings(context_.ast_.root())) return false;
        if (!instantiateDeclarations(context_.ast_.root(),
            context_.globalEnvironmentId_) ||
            !instantiateLexicalBindings(context_.ast_.root(),
                context_.globalEnvironmentId_)) return false;
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
        if (argumentCount > context_.limits_.parser.maxCallArguments) {
            error = RuntimeErrorCode::TooManyArguments;
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
        EnvironmentId parameterEnvironment = kInvalidEnvironmentId;
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
        bool jsonNamespaceValue = false;
        RuntimeContext::PropertyKey key;
        SourceLocation location;
    };

    bool rejectExistingGlobalBinding(SourceView name, bool varLike,
        SourceLocation location)
    {
        const Binding* existing = context_.environment_.resolve(name);
        if (existing == nullptr || (varLike && !existing->lexical))
            return true;
        fail(RuntimeErrorCode::DuplicateBinding, location);
        return false;
    }

    bool preflightGlobalBindings(AstNodeId program)
    {
        if (!validNode(program, SourceLocation()) ||
            context_.ast_.node(program).kind != AstNodeKind::Program) {
            fail(RuntimeErrorCode::InvalidAstState, SourceLocation());
            return false;
        }
        const AstNode& root = context_.ast_.node(program);
        for (std::size_t index = 0; index < root.childCount; ++index) {
            const AstNodeId child = context_.ast_.childAt(program, index);
            if (!validNode(child, root.location)) return false;
            const AstNode& declaration = context_.ast_.node(child);
            if (declaration.kind == AstNodeKind::VariableDeclaration &&
                declaration.declarationKind != AstDeclarationKind::Var) {
                for (std::size_t bindingIndex = 0;
                    bindingIndex < declaration.childCount; ++bindingIndex) {
                    const AstNodeId declaratorId = context_.ast_.childAt(
                        child, bindingIndex);
                    if (!validNode(declaratorId, declaration.location))
                        return false;
                    const AstNode& declarator =
                        context_.ast_.node(declaratorId);
                    SourceView name;
                    if (declarator.kind != AstNodeKind::VariableDeclarator ||
                        !identifierName(declarator.name, name) ||
                        !rejectExistingGlobalBinding(name, false,
                            context_.ast_.node(declarator.name).location))
                        return false;
                }
            } else if (declaration.kind == AstNodeKind::FunctionDeclaration) {
                SourceView name;
                if (!identifierName(declaration.name, name) ||
                    !rejectExistingGlobalBinding(name, true,
                        context_.ast_.node(declaration.name).location))
                    return false;
            }
        }

        std::vector<AstNodeId> pending;
        pending.push_back(program);
        while (!pending.empty()) {
            const AstNodeId id = pending.back();
            pending.pop_back();
            if (!validNode(id, SourceLocation())) return false;
            const AstNode& node = context_.ast_.node(id);
            if (id != program &&
                (node.kind == AstNodeKind::FunctionDeclaration ||
                 node.kind == AstNodeKind::FunctionExpression ||
                 node.kind == AstNodeKind::ArrowFunctionExpression))
                continue;

            if (node.kind == AstNodeKind::VariableDeclaration &&
                node.declarationKind == AstDeclarationKind::Var) {
                for (std::size_t bindingIndex = 0;
                    bindingIndex < node.childCount; ++bindingIndex) {
                    const AstNodeId declaratorId = context_.ast_.childAt(
                        id, bindingIndex);
                    if (!validNode(declaratorId, node.location)) return false;
                    const AstNode& declarator =
                        context_.ast_.node(declaratorId);
                    SourceView name;
                    if (declarator.kind != AstNodeKind::VariableDeclarator ||
                        !identifierName(declarator.name, name) ||
                        !rejectExistingGlobalBinding(name, true,
                            context_.ast_.node(declarator.name).location))
                        return false;
                }
            }

            for (std::size_t index = 0; index < node.childCount; ++index)
                pending.push_back(context_.ast_.childAt(id, index));
            const AstNodeId scalarChildren[] = {
                node.name, node.initializer, node.expression, node.argument,
                node.left, node.right, node.test, node.consequent,
                node.alternate, node.init, node.update, node.body,
                node.callee, node.object, node.property, node.key
            };
            for (AstNodeId child : scalarChildren)
                if (child != kInvalidAstNodeId)
                    pending.push_back(child);
        }
        return true;
    }

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
        EnvironmentError environmentError;
        if (context_.environmentStore_.readChain(currentEnvironment_, name,
                value, environmentError)) {
            return true;
        }
        if (environmentError.code != EnvironmentErrorCode::MissingBinding) {
            fail(runtimeEnvironmentError(environmentError.code),
                context_.ast_.node(id).location);
            return false;
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
        // JSON is a bounded intrinsic namespace. Like Object, it does not
        // consume a user binding slot or a RuntimeObject record.
        if (textEquals(name, "JSON")) {
            value = Value::jsonNamespace();
            return true;
        }
        fail(RuntimeErrorCode::UnknownIdentifier,
            context_.ast_.node(id).location);
        return false;
    }

    const Value* lookup(SourceView name) const
    {
        const Binding* found = nullptr;
        EnvironmentError ignored;
        return resolveBinding(name, found, ignored) ? &found->value : nullptr;
    }

    bool resolveBinding(SourceView name, const Binding*& found,
        EnvironmentError& error) const
    {
        return context_.environmentStore_.resolveChain(currentEnvironment_,
            name, found, error);
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
        fail(runtimeEnvironmentError(error.code), location);
        return false;
    }

    bool declareLexicalIn(EnvironmentId environment, SourceView name,
        bool immutable, SourceLocation location,
        const Value& value = Value::undefined(), bool initialized = false)
    {
        Environment* target = context_.environmentAt(environment);
        if (target == nullptr) {
            fail(RuntimeErrorCode::InvalidAstState, location);
            return false;
        }
        EnvironmentError error;
        if (target->declareLexical(name, immutable, error, value,
                initialized)) return true;
        fail(runtimeEnvironmentError(error.code), location);
        return false;
    }

    bool initialize(SourceView name, Value value, SourceLocation location)
    {
        EnvironmentError error;
        if (context_.environmentStore_.initializeChain(currentEnvironment_,
                name, value, error)) return true;
        fail(runtimeEnvironmentError(error.code), location);
        return false;
    }

    bool declare(SourceView name, Value value, SourceLocation location)
    {
        if (!frames_.empty() &&
            frames_.back().parameterEnvironment != kInvalidEnvironmentId) {
            const Environment* parameters = context_.environmentAt(
                frames_.back().parameterEnvironment);
            if (parameters != nullptr && parameters->resolve(name) != nullptr)
                return true;
        }
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
            EnvironmentError error;
            if (!target->assign(name, value, error)) {
                fail(runtimeEnvironmentError(error.code), location);
                return false;
            }
            return true;
        }
        return declareIn(environment, name, value, location);
    }

    bool assign(SourceView name, Value value, SourceLocation location)
    {
        EnvironmentError error;
        if (context_.environmentStore_.assignChain(currentEnvironment_, name,
                value, error)) return true;
        fail(runtimeEnvironmentError(error.code), location);
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
        case AstNodeKind::ArrowFunctionExpression:
            if (node.body != kInvalidAstNodeId)
                return validNode(node.body, node.location) &&
                    validateFunctionPolicy(node.body, true);
            return validNode(node.expression, node.location);
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

    bool hoistVariables(AstNodeId id, EnvironmentId environment,
        EnvironmentId parameterEnvironment = kInvalidEnvironmentId)
    {
        if (!validNode(id, SourceLocation())) return false;
        const AstNode& node = context_.ast_.node(id);
        switch (node.kind) {
        case AstNodeKind::Program:
        case AstNodeKind::BlockStatement:
            for (std::size_t index = 0; index < node.childCount; ++index) {
                if (!hoistVariables(context_.ast_.childAt(id, index),
                    environment, parameterEnvironment)) return false;
            }
            return true;
        case AstNodeKind::VariableDeclaration:
            if (node.declarationKind != AstDeclarationKind::Var) return true;
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
                const Environment* parameters = parameterEnvironment ==
                        kInvalidEnvironmentId
                    ? nullptr : context_.environmentAt(parameterEnvironment);
                if (parameters != nullptr && parameters->resolve(name) != nullptr)
                    continue;
                if (!declareIn(environment, name, Value::undefined(),
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
            if (!hoistVariables(node.consequent, environment,
                    parameterEnvironment)) return false;
            return node.alternate == kInvalidAstNodeId ||
                hoistVariables(node.alternate, environment,
                    parameterEnvironment);
        case AstNodeKind::WhileStatement:
            return hoistVariables(node.body, environment,
                parameterEnvironment);
        case AstNodeKind::ForStatement:
            if (node.init != kInvalidAstNodeId &&
                !hoistVariables(node.init, environment,
                    parameterEnvironment)) return false;
            return hoistVariables(node.body, environment,
                parameterEnvironment);
        case AstNodeKind::ForOfStatement:
            return hoistVariables(node.body, environment,
                parameterEnvironment);
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

    bool hoistFunctions(AstNodeId container, EnvironmentId environment,
        EnvironmentId parameterEnvironment = kInvalidEnvironmentId)
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
            EnvironmentId targetEnvironment = environment;
            if (parameterEnvironment != kInvalidEnvironmentId) {
                const Environment* parameters =
                    context_.environmentAt(parameterEnvironment);
                if (parameters != nullptr && parameters->resolve(name) != nullptr)
                    targetEnvironment = parameterEnvironment;
            }
            if (!bindValueIn(targetEnvironment, name, Value::function(function),
                context_.ast_.node(declaration.name).location)) return false;
        }
        return true;
    }

    bool instantiateDeclarations(AstNodeId container,
        EnvironmentId environment,
        EnvironmentId parameterEnvironment = kInvalidEnvironmentId)
    {
        return hoistVariables(container, environment, parameterEnvironment) &&
            hoistFunctions(container, environment, parameterEnvironment);
    }

    bool instantiateLexicalBindings(AstNodeId container,
        EnvironmentId environment)
    {
        if (!validNode(container, SourceLocation())) return false;
        const AstNode& scope = context_.ast_.node(container);
        if (scope.kind != AstNodeKind::Program &&
            scope.kind != AstNodeKind::BlockStatement) {
            fail(RuntimeErrorCode::InvalidAstState, scope.location);
            return false;
        }
        for (std::size_t index = 0; index < scope.childCount; ++index) {
            const AstNodeId child = context_.ast_.childAt(container, index);
            if (!validNode(child, scope.location)) return false;
            const AstNode& declaration = context_.ast_.node(child);
            if (declaration.kind != AstNodeKind::VariableDeclaration ||
                declaration.declarationKind == AstDeclarationKind::Var)
                continue;
            const bool immutable =
                declaration.declarationKind == AstDeclarationKind::Const;
            for (std::size_t bindingIndex = 0;
                bindingIndex < declaration.childCount; ++bindingIndex) {
                const AstNodeId declaratorId =
                    context_.ast_.childAt(child, bindingIndex);
                if (!validNode(declaratorId, declaration.location))
                    return false;
                const AstNode& declarator = context_.ast_.node(declaratorId);
                SourceView name;
                if (declarator.kind != AstNodeKind::VariableDeclarator ||
                    !identifierName(declarator.name, name) ||
                    !declareLexicalIn(environment, name, immutable,
                        context_.ast_.node(declarator.name).location)) {
                    if (context_.result_.runtimeError.code ==
                        RuntimeErrorCode::None)
                        fail(RuntimeErrorCode::InvalidAstState,
                            declarator.location);
                    return false;
                }
            }
        }
        return true;
    }

    bool hasDirectLexicalBindings(AstNodeId container) const
    {
        if (container == kInvalidAstNodeId ||
            container >= context_.ast_.nodeCount()) return false;
        const AstNode& scope = context_.ast_.node(container);
        for (std::size_t index = 0; index < scope.childCount; ++index) {
            const AstNode& child = context_.ast_.node(
                context_.ast_.childAt(container, index));
            if (child.kind == AstNodeKind::VariableDeclaration &&
                child.declarationKind != AstDeclarationKind::Var) return true;
        }
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
            value.isObjectNamespace() || value.isJsonNamespace()) {
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
            !object.isObjectNamespace() && !object.isJsonNamespace()) {
            fail(object.isNull() || object.isUndefined()
                ? (forWrite ? RuntimeErrorCode::CannotWriteProperty
                    : RuntimeErrorCode::CannotReadProperty)
                : RuntimeErrorCode::NotObject, node.location);
            return false;
        }
        if ((object.isSymbolNamespace() || object.isObjectNamespace() ||
                object.isJsonNamespace()) &&
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
        } else if (object.isJsonNamespace()) {
            reference.jsonNamespaceValue = true;
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
            SourceView string;
            RuntimeErrorCode ignored = RuntimeErrorCode::None;
            (void)context_.stringView(reference.receiver, string, ignored);
            value = !reference.key.isSymbol() &&
                reference.key.string == "length"
                ? Value::number(static_cast<double>(string.length))
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
        if (reference.jsonNamespaceValue) {
            value = !reference.key.isSymbol() &&
                reference.key.string == "parse"
                ? Value::function(context_.jsonParseFunction_)
                : !reference.key.isSymbol() &&
                    reference.key.string == "stringify"
                    ? Value::function(context_.jsonStringifyFunction_)
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
        } else if (base.isJsonNamespace()) {
            reference.jsonNamespaceValue = true;
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
        if (reference.objectNamespaceValue || reference.jsonNamespaceValue) {
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
                SourceView keyText;
                RuntimeErrorCode stringError = RuntimeErrorCode::None;
                if (!context_.stringView(keyValue, keyText, stringError)) {
                    fail(RuntimeErrorCode::InvalidAstState, property.location);
                    return false;
                }
                try {
                    key.assign(keyText.data == nullptr ? "" : keyText.data,
                        keyText.length);
                } catch (const std::bad_alloc&) {
                    fail(RuntimeErrorCode::AllocationFailure,
                        property.location);
                    return false;
                }
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
        case AstNodeKind::FunctionExpression:
        case AstNodeKind::ArrowFunctionExpression: {
            RuntimeFunctionId function = kInvalidRuntimeFunctionId;
            RuntimeErrorCode error = RuntimeErrorCode::None;
            if (!context_.createFunction(id, currentEnvironment_, function,
                error, node.kind == AstNodeKind::ArrowFunctionExpression,
                currentThis_)) {
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
        if (arguments.size() > context_.limits_.parser.maxCallArguments) {
            fail(RuntimeErrorCode::TooManyArguments, callSite);
            return false;
        }
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
            declaration.kind != AstNodeKind::FunctionExpression &&
            declaration.kind != AstNodeKind::ArrowFunctionExpression) ||
            (declaration.body == kInvalidAstNodeId &&
             (declaration.kind != AstNodeKind::ArrowFunctionExpression ||
              declaration.expression == kInvalidAstNodeId)) ||
            (declaration.body != kInvalidAstNodeId &&
             !validNode(declaration.body, callSite))) {
            fail(RuntimeErrorCode::InvalidFunction, callSite);
            return false;
        }
        if (context_.activeCallFrames_ >= context_.limits_.maxCallDepth) {
            fail(RuntimeErrorCode::CallDepthExceeded, callSite);
            return false;
        }

        bool hasNonSimpleParameters = false;
        for (std::size_t index = 0; index < declaration.childCount; ++index) {
            const AstNodeId parameterId = context_.ast_.childAt(
                function.declaration, index);
            if (!validNode(parameterId, declaration.location)) return false;
            const AstNode& parameter = context_.ast_.node(parameterId);
            if (parameter.kind != AstNodeKind::Identifier) {
                fail(RuntimeErrorCode::InvalidAstState, parameter.location);
                return false;
            }
            hasNonSimpleParameters = hasNonSimpleParameters ||
                parameter.initializer != kInvalidAstNodeId || parameter.prefix;
        }

        EnvironmentId callEnvironment = kInvalidEnvironmentId;
        RuntimeErrorCode error = RuntimeErrorCode::None;
        if (!context_.createEnvironment(function.closureEnvironment,
            callEnvironment, error, hasNonSimpleParameters
                ? EnvironmentKind::Parameters : EnvironmentKind::Function)) {
            fail(error, callSite);
            return false;
        }
        const EnvironmentId parameterEnvironment = hasNonSimpleParameters
            ? callEnvironment : kInvalidEnvironmentId;

        const EnvironmentId callerEnvironment = currentEnvironment_;
        const Value callerThis = currentThis_;
        const std::size_t previousFrameCount = frames_.size();
        const std::size_t previousActiveCallFrames =
            context_.activeCallFrames_;
        try {
            CallFrame frame;
            frame.function = functionId;
            frame.environment = callEnvironment;
            frame.parameterEnvironment = parameterEnvironment;
            frame.callerEnvironment = callerEnvironment;
            frame.callSite = callSite;
            const Value activeThis = function.arrowFunction
                ? function.lexicalThis : receiver;
            frame.thisValue = activeThis;
            frames_.push_back(frame);
        } catch (const std::bad_alloc&) {
            fail(RuntimeErrorCode::AllocationFailure, callSite);
            return false;
        }
        context_.activeCallFrames_ = previousActiveCallFrames + 1u;
        currentEnvironment_ = callEnvironment;
        currentThis_ = function.arrowFunction ? function.lexicalThis : receiver;

        if (hasNonSimpleParameters) {
            for (std::size_t index = 0; index < declaration.childCount; ++index) {
                const AstNodeId parameterId = context_.ast_.childAt(
                    function.declaration, index);
                SourceView name;
                if (!identifierName(parameterId, name) ||
                    !declareLexicalIn(parameterEnvironment, name, false,
                        context_.ast_.node(parameterId).location)) {
                    unwindFrame(callerEnvironment, callerThis,
                        previousFrameCount, previousActiveCallFrames);
                    return false;
                }
            }

            for (std::size_t index = 0; index < declaration.childCount; ++index) {
                const AstNodeId parameterId = context_.ast_.childAt(
                    function.declaration, index);
                const AstNode& parameter = context_.ast_.node(parameterId);
                SourceView name;
                if (!identifierName(parameterId, name)) {
                    unwindFrame(callerEnvironment, callerThis,
                        previousFrameCount, previousActiveCallFrames);
                    return false;
                }

                Value value = index < arguments.size()
                    ? arguments[index] : Value::undefined();
                if (parameter.prefix) {
                    const std::size_t firstRestArgument =
                        std::min(index, arguments.size());
                    const std::size_t restCount =
                        arguments.size() - firstRestArgument;
                    std::vector<RuntimeContext::RuntimeArrayElement> elements;
                    try {
                        elements.reserve(restCount);
                        for (std::size_t restIndex = 0u;
                            restIndex < restCount; ++restIndex) {
                            elements.emplace_back(restIndex,
                                arguments[firstRestArgument + restIndex]);
                        }
                    } catch (const std::bad_alloc&) {
                        fail(RuntimeErrorCode::AllocationFailure,
                            parameter.location);
                        unwindFrame(callerEnvironment, callerThis,
                            previousFrameCount, previousActiveCallFrames);
                        return false;
                    }
                    RuntimeObjectId restArray = kInvalidRuntimeObjectId;
                    if (!context_.createArray(restCount, std::move(elements),
                            restArray, error)) {
                        fail(error, parameter.location);
                        unwindFrame(callerEnvironment, callerThis,
                            previousFrameCount, previousActiveCallFrames);
                        return false;
                    }
                    value = Value::object(restArray);
                } else if (value.isUndefined() &&
                    parameter.initializer != kInvalidAstNodeId) {
                    if (!validNode(parameter.initializer, parameter.location) ||
                        !evalExpression(parameter.initializer, value)) {
                        unwindFrame(callerEnvironment, callerThis,
                            previousFrameCount, previousActiveCallFrames);
                        return false;
                    }
                }

                Environment* target =
                    context_.environmentAt(parameterEnvironment);
                if (target == nullptr || !target->initialize(name, value)) {
                    fail(RuntimeErrorCode::InvalidAstState,
                        parameter.location);
                    unwindFrame(callerEnvironment, callerThis,
                        previousFrameCount, previousActiveCallFrames);
                    return false;
                }
            }

            EnvironmentId bodyEnvironment = kInvalidEnvironmentId;
            if (!context_.createEnvironment(parameterEnvironment,
                    bodyEnvironment, error,
                    EnvironmentKind::FunctionBody)) {
                fail(error, callSite);
                unwindFrame(callerEnvironment, callerThis,
                    previousFrameCount, previousActiveCallFrames);
                return false;
            }
            callEnvironment = bodyEnvironment;
            currentEnvironment_ = bodyEnvironment;
            if (!frames_.empty()) frames_.back().environment = bodyEnvironment;
        } else {
            for (std::size_t index = 0; index < declaration.childCount; ++index) {
                const AstNodeId parameterId = context_.ast_.childAt(
                    function.declaration, index);
                SourceView name;
                if (!identifierName(parameterId, name)) {
                    unwindFrame(callerEnvironment, callerThis,
                        previousFrameCount, previousActiveCallFrames);
                    return false;
                }
                const Value argument = index < arguments.size()
                    ? arguments[index] : Value::undefined();
                if (!declareIn(callEnvironment, name, argument,
                    context_.ast_.node(parameterId).location)) {
                    unwindFrame(callerEnvironment, callerThis,
                        previousFrameCount, previousActiveCallFrames);
                    return false;
                }
                Environment* target = context_.environmentAt(callEnvironment);
                if (target == nullptr || !target->assign(name, argument)) {
                    fail(RuntimeErrorCode::InvalidAstState,
                        context_.ast_.node(parameterId).location);
                    unwindFrame(callerEnvironment, callerThis,
                        previousFrameCount, previousActiveCallFrames);
                    return false;
                }
            }
        }

        if (declaration.body != kInvalidAstNodeId &&
            (!instantiateDeclarations(declaration.body, callEnvironment,
                 parameterEnvironment) ||
             !instantiateLexicalBindings(declaration.body, callEnvironment))) {
            unwindFrame(callerEnvironment, callerThis, previousFrameCount,
                previousActiveCallFrames);
            return false;
        }

        Control control;
        bool executed = false;
        if (declaration.kind == AstNodeKind::ArrowFunctionExpression &&
            declaration.body == kInvalidAstNodeId) {
            executed = evalExpression(declaration.expression, control.value);
            if (executed) control.kind = ControlKind::Return;
        } else {
            executed = beginNode(declaration.body) &&
                executeStatementList(declaration.body, false, true, control);
        }
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
        RuntimeContext::PropertyKey propertyKey;
        propertyKey.string = key;
        bool found = false;
        RuntimeErrorCode ignored = RuntimeErrorCode::None;
        return context_.hasOwnStoredProperty(objectId, propertyKey, found,
            ignored) && found;
    }

    bool hasOwnProperty(RuntimeObjectId objectId,
        const RuntimeContext::PropertyKey& key) const
    {
        bool found = false;
        RuntimeErrorCode ignored = RuntimeErrorCode::None;
        return context_.hasOwnStoredProperty(objectId, key, found, ignored) &&
            found;
    }

    bool hasArrayIndex(RuntimeObjectId objectId, std::size_t index,
        bool& present, SourceLocation location)
    {
        present = false;
        RuntimeObjectId current = objectId;
        for (std::size_t depth = 0u;
            depth < context_.limits_.maxPrototypeDepth; ++depth) {
            if (!context_.consumeStep(location)) return false;
            if (context_.objectAt(current) == nullptr) {
                fail(RuntimeErrorCode::CannotReadProperty, location);
                return false;
            }
            RuntimeContext::PropertyKey key;
            key.string = std::to_string(index);
            RuntimeErrorCode ownError = RuntimeErrorCode::None;
            if (!context_.hasOwnStoredProperty(current, key, present,
                    ownError)) {
                fail(ownError, location);
                return false;
            }
            if (present) return true;
            RuntimeObjectId prototype = kInvalidRuntimeObjectId;
            PortableObjectError objectError;
            if (!context_.objectStore_.prototypeOf(current, prototype,
                    objectError)) {
                fail(context_.mapPortableObjectError(objectError.code),
                    location);
                return false;
            }
            if (prototype == kInvalidRuntimeObjectId) return true;
            current = prototype;
        }
        fail(RuntimeErrorCode::PrototypeChainExceeded, location);
        return false;
    }

    enum class ArrayTraversalKind : std::uint8_t {
        ForEach = 0,
        Map,
        Filter,
        Some,
        Every,
        Find,
        FindIndex,
        FindLast,
    };

    enum class ArrayTraversalControl : std::uint8_t {
        Continue = 0,
        Stop,
        Error,
    };

    ArrayTraversalControl traverseArray(const Value& receiver,
        const Value& callback,
        const Value& thisArgument, SourceLocation callSite,
        ArrayTraversalKind kind, Value& result)
    {
        const RuntimeObjectId arrayId = receiver.isObject()
            ? receiver.objectId() : kInvalidRuntimeObjectId;
        const RuntimeContext::RuntimeObject* array =
            context_.objectAt(arrayId);
        bool arrayIsArray = false;
        if (array == nullptr ||
            !context_.isArrayObject(arrayId, arrayIsArray) ||
            !arrayIsArray) {
            fail(RuntimeErrorCode::InvalidReceiver, callSite);
            return ArrayTraversalControl::Error;
        }
        if (!callback.isFunction()) {
            fail(RuntimeErrorCode::NotCallable, callSite);
            return ArrayTraversalControl::Error;
        }
        const RuntimeFunctionId callbackId = callback.functionId();
        if (context_.functionAt(callbackId) == nullptr) {
            fail(RuntimeErrorCode::InvalidFunction, callSite);
            return ArrayTraversalControl::Error;
        }

        // Keep only the stable object ID and captured length across callbacks.
        std::size_t initialLength = 0u;
        if (!context_.getArrayLength(arrayId, initialLength)) {
            fail(RuntimeErrorCode::InvalidAstState, callSite);
            return ArrayTraversalControl::Error;
        }
        if (initialLength > maximumSupportedArrayLength(context_.limits_)) {
            fail(RuntimeErrorCode::InvalidAstState, callSite);
            return ArrayTraversalControl::Error;
        }

        std::vector<Value> callbackArguments;
        std::vector<RuntimeContext::RuntimeArrayElement> mappedElements;
        bool callbackArgumentsReady = false;
        const bool reverse = kind == ArrayTraversalKind::FindLast;
        std::size_t cursor = reverse ? initialLength : 0u;
        while (reverse ? cursor > 0u : cursor < initialLength) {
            const std::size_t index = reverse ? --cursor : cursor++;
            bool present = false;
            if (!hasArrayIndex(arrayId, index, present, callSite))
                return ArrayTraversalControl::Error;
            const bool visitMissingIndex = kind == ArrayTraversalKind::Find ||
                kind == ArrayTraversalKind::FindIndex ||
                kind == ArrayTraversalKind::FindLast;
            if (!present && !visitMissingIndex) continue;

            Value value = Value::undefined();
            RuntimeErrorCode lookupError = RuntimeErrorCode::None;
            if (!context_.readArrayIndex(arrayId, index, value, lookupError,
                    callSite)) {
                fail(lookupError, callSite);
                return ArrayTraversalControl::Error;
            }
            if (!callbackArgumentsReady) {
                try {
                    callbackArguments.resize(3u, Value::undefined());
                } catch (const std::bad_alloc&) {
                    fail(RuntimeErrorCode::AllocationFailure, callSite);
                    return ArrayTraversalControl::Error;
                }
                callbackArgumentsReady = true;
            }
            callbackArguments[0] = value;
            callbackArguments[1] = Value::number(
                static_cast<double>(index));
            callbackArguments[2] = receiver;
            Value callbackResult = Value::undefined();
            if (!invokeFunction(callbackId, callbackArguments, callSite,
                    callbackResult, thisArgument))
                return ArrayTraversalControl::Error;

            if (kind == ArrayTraversalKind::Some && truthy(callbackResult)) {
                result = Value::boolean(true);
                return ArrayTraversalControl::Stop;
            }
            if (kind == ArrayTraversalKind::Every && !truthy(callbackResult)) {
                result = Value::boolean(false);
                return ArrayTraversalControl::Stop;
            }
            if (kind == ArrayTraversalKind::Find && truthy(callbackResult)) {
                result = value;
                return ArrayTraversalControl::Stop;
            }
            if (kind == ArrayTraversalKind::FindIndex &&
                truthy(callbackResult)) {
                result = Value::number(static_cast<double>(index));
                return ArrayTraversalControl::Stop;
            }
            if (kind == ArrayTraversalKind::FindLast &&
                truthy(callbackResult)) {
                result = value;
                return ArrayTraversalControl::Stop;
            }

            if (kind == ArrayTraversalKind::Map ||
                (kind == ArrayTraversalKind::Filter &&
                    truthy(callbackResult))) {
                try {
                    mappedElements.emplace_back(kind ==
                            ArrayTraversalKind::Filter
                            ? mappedElements.size() : index,
                        kind == ArrayTraversalKind::Filter
                            ? value : callbackResult);
                } catch (const std::bad_alloc&) {
                    fail(RuntimeErrorCode::AllocationFailure, callSite);
                    return ArrayTraversalControl::Error;
                }
            }
        }

        if (kind != ArrayTraversalKind::Map &&
            kind != ArrayTraversalKind::Filter) {
            if (kind == ArrayTraversalKind::Some) {
                result = Value::boolean(false);
            } else if (kind == ArrayTraversalKind::Every) {
                result = Value::boolean(true);
            } else if (kind == ArrayTraversalKind::FindIndex) {
                result = Value::number(-1.0);
            } else {
                result = Value::undefined();
            }
            return ArrayTraversalControl::Continue;
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
                return ArrayTraversalControl::Error;
            }
            mappedElements.swap(compactElements);
        }

        const std::size_t resultLength = kind == ArrayTraversalKind::Filter
            ? mappedElements.size() : initialLength;
        RuntimeObjectId mappedArrayId = kInvalidRuntimeObjectId;
        RuntimeErrorCode allocationError = RuntimeErrorCode::None;
        if (!context_.createArray(resultLength, std::move(mappedElements),
                mappedArrayId, allocationError)) {
            fail(allocationError, callSite);
            return ArrayTraversalControl::Error;
        }
        result = Value::object(mappedArrayId);
        return ArrayTraversalControl::Continue;
    }

#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    ArrayTraversalControl reduceArray(const Value& receiver,
        const Value& callback, bool hasInitialValue,
        const Value& initialValue, SourceLocation callSite, Value& result)
    {
        const RuntimeObjectId arrayId = receiver.isObject()
            ? receiver.objectId() : kInvalidRuntimeObjectId;
        const RuntimeContext::RuntimeObject* array =
            context_.objectAt(arrayId);
        bool arrayIsArray = false;
        if (array == nullptr ||
            !context_.isArrayObject(arrayId, arrayIsArray) ||
            !arrayIsArray) {
            fail(RuntimeErrorCode::InvalidReceiver, callSite);
            return ArrayTraversalControl::Error;
        }
        if (!callback.isFunction()) {
            fail(RuntimeErrorCode::NotCallable, callSite);
            return ArrayTraversalControl::Error;
        }
        const RuntimeFunctionId callbackId = callback.functionId();
        if (context_.functionAt(callbackId) == nullptr) {
            fail(RuntimeErrorCode::InvalidFunction, callSite);
            return ArrayTraversalControl::Error;
        }

        // Retain only the stable source ID and the entry length.
        std::size_t initialLength = 0u;
        if (!context_.getArrayLength(arrayId, initialLength)) {
            fail(RuntimeErrorCode::InvalidAstState, callSite);
            return ArrayTraversalControl::Error;
        }
        if (initialLength > maximumSupportedArrayLength(context_.limits_)) {
            fail(RuntimeErrorCode::InvalidAstState, callSite);
            return ArrayTraversalControl::Error;
        }

        Value accumulator = initialValue;
        bool accumulatorInitialized = hasInitialValue;
        std::vector<Value> callbackArguments;
        bool callbackArgumentsReady = false;
        for (std::size_t index = 0u; index < initialLength; ++index) {
            bool present = false;
            if (!hasArrayIndex(arrayId, index, present, callSite))
                return ArrayTraversalControl::Error;
            if (!present) continue;

            Value value = Value::undefined();
            RuntimeErrorCode lookupError = RuntimeErrorCode::None;
            if (!context_.readArrayIndex(arrayId, index, value, lookupError,
                    callSite)) {
                fail(lookupError, callSite);
                return ArrayTraversalControl::Error;
            }
            if (!accumulatorInitialized) {
                accumulator = value;
                accumulatorInitialized = true;
                continue;
            }

            if (!callbackArgumentsReady) {
                try {
                    callbackArguments.resize(4u, Value::undefined());
                } catch (const std::bad_alloc&) {
                    fail(RuntimeErrorCode::AllocationFailure, callSite);
                    return ArrayTraversalControl::Error;
                }
                callbackArgumentsReady = true;
            }
            callbackArguments[0] = accumulator;
            callbackArguments[1] = value;
            callbackArguments[2] = Value::number(
                static_cast<double>(index));
            callbackArguments[3] = receiver;
            Value callbackResult = Value::undefined();
            if (!invokeFunction(callbackId, callbackArguments, callSite,
                    callbackResult)) {
                return ArrayTraversalControl::Error;
            }
            // Value is a small tagged handle. Runtime object records are
            // append-only for a realm, so replacing this local handle cannot
            // invalidate the callback's returned object or Array.
            accumulator = callbackResult;
        }

        if (!accumulatorInitialized) {
            fail(RuntimeErrorCode::TypeError, callSite);
            return ArrayTraversalControl::Error;
        }
        result = accumulator;
        return ArrayTraversalControl::Continue;
    }

#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    ArrayTraversalControl reduceRightArray(const Value& receiver,
        const Value& callback, bool hasInitialValue,
        const Value& initialValue, SourceLocation callSite, Value& result)
    {
        const RuntimeObjectId arrayId = receiver.isObject()
            ? receiver.objectId() : kInvalidRuntimeObjectId;
        const RuntimeContext::RuntimeObject* array =
            context_.objectAt(arrayId);
        bool arrayIsArray = false;
        if (array == nullptr ||
            !context_.isArrayObject(arrayId, arrayIsArray) ||
            !arrayIsArray) {
            fail(RuntimeErrorCode::InvalidReceiver, callSite);
            return ArrayTraversalControl::Error;
        }
        if (!callback.isFunction()) {
            fail(RuntimeErrorCode::NotCallable, callSite);
            return ArrayTraversalControl::Error;
        }
        const RuntimeFunctionId callbackId = callback.functionId();
        if (context_.functionAt(callbackId) == nullptr) {
            fail(RuntimeErrorCode::InvalidFunction, callSite);
            return ArrayTraversalControl::Error;
        }

        // Retain only the stable source ID and captured length across callback
        // execution. The cursor is a one-past-end size_t, so decrementing it
        // before lookup terminates at zero without converting -1 to unsigned.
        std::size_t initialLength = 0u;
        if (!context_.getArrayLength(arrayId, initialLength)) {
            fail(RuntimeErrorCode::InvalidAstState, callSite);
            return ArrayTraversalControl::Error;
        }
        if (initialLength > maximumSupportedArrayLength(context_.limits_)) {
            fail(RuntimeErrorCode::InvalidAstState, callSite);
            return ArrayTraversalControl::Error;
        }

        Value accumulator = initialValue;
        bool accumulatorInitialized = hasInitialValue;
        std::vector<Value> callbackArguments;
        bool callbackArgumentsReady = false;
        std::size_t cursor = initialLength;
        while (cursor > 0u) {
            const std::size_t index = --cursor;
            bool present = false;
            if (!hasArrayIndex(arrayId, index, present, callSite))
                return ArrayTraversalControl::Error;
            if (!present) continue;

            Value value = Value::undefined();
            RuntimeErrorCode lookupError = RuntimeErrorCode::None;
            if (!context_.readArrayIndex(arrayId, index, value, lookupError,
                    callSite)) {
                fail(lookupError, callSite);
                return ArrayTraversalControl::Error;
            }
            if (!accumulatorInitialized) {
                accumulator = value;
                accumulatorInitialized = true;
                continue;
            }

            if (!callbackArgumentsReady) {
                try {
                    callbackArguments.resize(4u, Value::undefined());
                } catch (const std::bad_alloc&) {
                    fail(RuntimeErrorCode::AllocationFailure, callSite);
                    return ArrayTraversalControl::Error;
                }
                callbackArgumentsReady = true;
            }
            callbackArguments[0] = accumulator;
            callbackArguments[1] = value;
            callbackArguments[2] = Value::number(
                static_cast<double>(index));
            callbackArguments[3] = receiver;
            Value callbackResult = Value::undefined();
            if (!invokeFunction(callbackId, callbackArguments, callSite,
                    callbackResult)) {
                return ArrayTraversalControl::Error;
            }
            // Runtime reference handles use stable IDs into append-only realm
            // storage, so replacing this local preserves returned references.
            accumulator = callbackResult;
        }

        if (!accumulatorInitialized) {
            fail(RuntimeErrorCode::TypeError, callSite);
            return ArrayTraversalControl::Error;
        }
        result = accumulator;
        return ArrayTraversalControl::Continue;
    }

    bool createObjectKeys(RuntimeObjectId sourceId, SourceLocation callSite,
        Value& result)
    {
        const RuntimeContext::RuntimeObject* source =
            context_.objectAt(sourceId);
        bool sourceArray = false;
        std::size_t sourceLength = 0u;
        std::vector<RuntimeContext::RuntimeProperty> sourceProperties;
        std::vector<RuntimeContext::RuntimeArrayElement> sourceElements;
        if (source == nullptr || sourceId == context_.objectPrototype_ ||
            sourceId == context_.arrayPrototype_ ||
            sourceId == context_.mathObject_ ||
            sourceId == context_.eventConstantsObject_ ||
            source->arrayIterator) {
            fail(RuntimeErrorCode::InvalidReceiver, callSite);
            return false;
        }
        RuntimeErrorCode snapshotError = RuntimeErrorCode::None;
        if (!context_.isArrayObject(sourceId, sourceArray) ||
            (sourceArray && !context_.getArrayLength(sourceId,
                sourceLength)) ||
            !context_.snapshotOwnProperties(sourceId, sourceProperties,
                snapshotError) ||
            (sourceArray && !context_.snapshotArrayElements(sourceId,
                sourceElements, snapshotError))) {
            fail(snapshotError == RuntimeErrorCode::None
                    ? RuntimeErrorCode::InvalidAstState : snapshotError,
                callSite);
            return false;
        }
        for (const RuntimeContext::RuntimeProperty& property :
            sourceProperties) {
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

        std::size_t keyCount = sourceArray ? sourceElements.size() : 0u;
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
        if (sourceArray) {
            for (std::size_t position = 0u;
                position < sourceElements.size(); ++position) {
                if (!context_.consumeStep(callSite)) return false;
                const std::size_t index = sourceElements[position].index;
                if (index >= sourceLength ||
                    index > context_.limits_.maxArrayIndex ||
                    !sourceElements[position].attributes.isValid() ||
                    !sourceElements[position].attributes.isFullyKnown() ||
                    !sourceElements[position].attributes.enumerable() ||
                    (position != 0u && index <= previousArrayIndex)) {
                    fail(RuntimeErrorCode::InvalidAstState, callSite);
                    return false;
                }
                previousArrayIndex = index;
                if (!addKeySize(indexLength(index))) return false;
            }
        }

        for (const RuntimeContext::RuntimeProperty& property :
            sourceProperties) {
            if (!context_.consumeStep(callSite)) return false;
            if (property.symbolKey != WellKnownSymbol::None ||
                (sourceArray && property.key == "length")) continue;
            if (!property.attributes.enumerable()) continue;
            std::uint32_t standardIndex = 0u;
            if (sourceArray && parseEcmaArrayIndex(property.key,
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
            context_.objectStore_.arrayElementCount() >
                context_.limits_.maxTotalArrayElements - keyCount) {
            fail(RuntimeErrorCode::ArrayLimitExceeded, callSite);
            return false;
        }
        if (context_.objectStore_.objectCount() >= context_.limits_.maxObjects) {
            fail(RuntimeErrorCode::ObjectLimitExceeded, callSite);
            return false;
        }
        if (keyCount > context_.limits_.maxRuntimeStringValues ||
            context_.stringStore_.count() >
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

        if (sourceArray) {
            for (const RuntimeContext::RuntimeArrayElement& element :
                sourceElements) {
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
                    position < sourceProperties.size(); ++position) {
                    if (!context_.consumeStep(callSite)) return false;
                    const RuntimeContext::RuntimeProperty& property =
                        sourceProperties[position];
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
                    sourceProperties[smallestProperty].key;
                if (!appendKey(SourceView(key.data(), key.size()))) return false;
                previousIndex = smallestIndex;
                hasPreviousIndex = true;
            }
        }

        for (const RuntimeContext::RuntimeProperty& property :
            sourceProperties) {
            if (!context_.consumeStep(callSite)) return false;
            if (property.symbolKey != WellKnownSymbol::None ||
                (sourceArray && property.key == "length") ||
                !property.attributes.enumerable()) continue;
            std::uint32_t standardIndex = 0u;
            if (!sourceArray && parseEcmaArrayIndex(property.key,
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
        if (context_.objectStore_.objectCount() >=
            context_.limits_.maxObjects) {
            fail(RuntimeErrorCode::ObjectLimitExceeded, callSite);
            return false;
        }
        if (context_.limits_.maxPropertiesPerObject < 4u ||
            context_.limits_.maxTotalProperties < 4u ||
            context_.objectStore_.propertyCount() >
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
            context_.objectStore_.propertyNameBytesUsed() >
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

        const std::size_t objectCountBefore =
            context_.objectStore_.objectCount() - 1u;
        const auto addField = [this, descriptorId, callSite,
            objectCountBefore](const char* key, Value value) {
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
            return true;
        };

        if (!addField(valueKey, propertyValue) ||
            !addField(writableKey, Value::boolean(attributes.writable())) ||
            !addField(enumerableKey,
                Value::boolean(attributes.enumerable())) ||
            !addField(configurableKey,
                Value::boolean(attributes.configurable()))) {
            RuntimeErrorCode rollbackError = RuntimeErrorCode::None;
            (void)context_.rollbackObjects(objectCountBefore, rollbackError);
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
        PortablePropertyEntry entry;
        bool found = false;
        RuntimeErrorCode entryError = RuntimeErrorCode::None;
        if (!context_.getOwnPropertyEntry(sourceId, key, entry, found,
                entryError)) {
            fail(entryError, callSite);
            return false;
        }
        if (!found) {
            fail(RuntimeErrorCode::InvalidAstState, callSite);
            return false;
        }
        propertyValue = entry.value;
        attributes.bits = entry.attributes;

        if (!attributes.isValid() || !attributes.isFullyKnown()) {
            fail(RuntimeErrorCode::UnsupportedFeature, callSite);
            return false;
        }
        return createDescriptorSnapshot(propertyValue, attributes,
            callSite, result);
    }

    bool parseJson(const std::vector<Value>& arguments,
        SourceLocation callSite, Value& result)
    {
        result = Value::undefined();
        if (arguments.empty() || !arguments[0].isString()) {
            fail(RuntimeErrorCode::TypeError, callSite);
            return false;
        }
        if (arguments.size() > 2u) {
            fail(RuntimeErrorCode::UnsupportedFeature, callSite);
            return false;
        }
        SourceView inputSource;
        RuntimeErrorCode stringError = RuntimeErrorCode::None;
        if (!context_.stringView(arguments[0], inputSource, stringError)) {
            fail(RuntimeErrorCode::TypeError, callSite);
            return false;
        }
        if (inputSource.length > context_.limits_.maxRuntimeStringLength) {
            fail(RuntimeErrorCode::StringLimitExceeded, callSite);
            return false;
        }
        const std::size_t objectCountBefore =
            context_.objectStore_.objectCount();
        const std::size_t adapterCountBefore = context_.objectAdapters_.size();
        const RuntimeStringMark stringMark = context_.stringStore_.mark();
        const std::size_t stringBytesBefore = context_.totalStringBytes_;
        const auto rollback = [&]() {
            RuntimeErrorCode rollbackError = RuntimeErrorCode::None;
            if (!context_.rollbackObjects(objectCountBefore, rollbackError)) {
                fail(rollbackError, callSite);
            }
            RuntimeStringError stringRollbackError;
            if (!context_.stringStore_.restore(stringMark,
                    stringRollbackError)) {
                fail(RuntimeErrorCode::InvalidAstState, callSite);
            }
            context_.objectAdapters_.resize(adapterCountBefore);
            context_.totalStringBytes_ = stringBytesBefore;
        };

        const std::size_t maximumDepth = std::min(
            context_.limits_.maxJsonNestingDepth,
            kDefaultMaxJavaScriptJsonNestingDepth);
        const std::size_t maximumInput = 64u * 1024u;
        const std::size_t maximumDecodedString = std::min(
            context_.limits_.maxRuntimeStringLength, maximumInput);
        const std::size_t maximumKey =
            context_.limits_.maxPropertyNameLength;
        if (maximumDepth != 0u && maximumKey >
                (std::numeric_limits<std::size_t>::max)() / maximumDepth) {
            fail(RuntimeErrorCode::PortableCapacityExceeded, callSite);
            return false;
        }

        std::vector<char> decodedScratch;
        std::vector<char> keyScratch;
        try {
            decodedScratch.resize(maximumDecodedString);
            keyScratch.resize(maximumDepth * maximumKey);
        } catch (const std::bad_alloc&) {
            fail(RuntimeErrorCode::AllocationFailure, callSite);
            return false;
        }

        PortableJsonParseLimits jsonLimits;
        jsonLimits.maxInputBytes = maximumInput;
        jsonLimits.maxStringLength = maximumDecodedString;
        jsonLimits.maxStringValues = context_.limits_.maxRuntimeStringValues;
        jsonLimits.maxStringBytes = context_.limits_.maxTotalRuntimeStringBytes;
        jsonLimits.maxNestingDepth = maximumDepth;
        jsonLimits.maxArrayLength = maximumSupportedArrayLength(
            context_.limits_);
        jsonLimits.maxArrayElements = context_.limits_.maxArrayElements;
        jsonLimits.maxTotalArrayElements =
            context_.limits_.maxTotalArrayElements;
        jsonLimits.propertyAttributes =
            RuntimeContext::PropertyAttributes::ordinaryData().bits;
        jsonLimits.numberConverter = &parseHostedJsonBinary64;
        jsonLimits.maxSteps = context_.executionSteps_ >=
                context_.limits_.maxExecutionSteps
            ? 0u : context_.limits_.maxExecutionSteps -
                context_.executionSteps_;
        jsonLimits.objectPrototype = context_.builtInsInitialized_
            ? context_.objectPrototype_ : kInvalidRuntimeObjectId;
        jsonLimits.arrayPrototype = context_.builtInsInitialized_
            ? context_.arrayPrototype_ : kInvalidRuntimeObjectId;

        Value parsed = Value::undefined();
        PortableJsonParseError jsonError;
        PortableJsonParseStats jsonStats;
        const bool succeeded = parsePortableJson(inputSource,
            context_.objectStore_, context_.stringStore_, jsonLimits,
            decodedScratch.data(), decodedScratch.size(), keyScratch.data(),
            keyScratch.size(), nullptr, nullptr, parsed, jsonError,
            &jsonStats);
        context_.executionSteps_ += jsonStats.stepsConsumed;
        context_.result_.executionSteps = context_.executionSteps_;
#ifdef GXOS_JAVASCRIPT_LIFETIME_DIAGNOSTICS
        context_.diagnostics_.runtimeObjectsAllocated +=
            jsonStats.objectsCreated;
        context_.diagnostics_.arraySlotsAllocated +=
            jsonStats.arrayElementsCreated;
        if (context_.diagnostics_.objectPoolHighWater <
                context_.objectStore_.objectCount()) {
            context_.diagnostics_.objectPoolHighWater =
                context_.objectStore_.objectCount();
        }
        if (context_.diagnostics_.arraySlotHighWater <
                context_.objectStore_.arrayElementCount()) {
            context_.diagnostics_.arraySlotHighWater =
                context_.objectStore_.arrayElementCount();
        }
#endif
        if (!succeeded) {
            RuntimeErrorCode mapped = RuntimeErrorCode::SyntaxError;
            switch (jsonError.code) {
            case PortableJsonParseErrorCode::None:
            case PortableJsonParseErrorCode::SyntaxError:
            case PortableJsonParseErrorCode::InvalidUnicode:
            case PortableJsonParseErrorCode::InvalidNumber:
                mapped = RuntimeErrorCode::SyntaxError;
                break;
            case PortableJsonParseErrorCode::NumericConversionUnavailable:
                mapped = RuntimeErrorCode::UnsupportedFeature;
                break;
            case PortableJsonParseErrorCode::InputLimitExceeded:
            case PortableJsonParseErrorCode::StringLimitExceeded:
                mapped = RuntimeErrorCode::StringLimitExceeded;
                break;
            case PortableJsonParseErrorCode::NestingLimitExceeded:
                mapped = RuntimeErrorCode::JsonNestingLimitExceeded;
                break;
            case PortableJsonParseErrorCode::ObjectStoreFailure:
                mapped = context_.mapPortableObjectError(
                    jsonError.objectError);
                break;
            case PortableJsonParseErrorCode::StringStoreFailure:
                mapped = jsonError.stringError ==
                        RuntimeStringErrorCode::CapacityExceeded
                    ? RuntimeErrorCode::StringLimitExceeded
                    : RuntimeErrorCode::AllocationFailure;
                break;
            case PortableJsonParseErrorCode::ArrayLimitExceeded:
                mapped = RuntimeErrorCode::ArrayLimitExceeded;
                break;
            case PortableJsonParseErrorCode::ExecutionBudgetExceeded:
                mapped = RuntimeErrorCode::ExecutionBudgetExceeded;
                break;
            case PortableJsonParseErrorCode::InvalidInput:
            case PortableJsonParseErrorCode::InternalInvariantFailure:
                mapped = RuntimeErrorCode::InvalidAstState;
                break;
            }
            fail(mapped, callSite);
            return false;
        }

        try {
            context_.objectAdapters_.resize(
                context_.objectStore_.objectCount());
        } catch (const std::bad_alloc&) {
            fail(RuntimeErrorCode::AllocationFailure, callSite);
            rollback();
            return false;
        }
        context_.totalStringBytes_ = context_.stringStore_.bytesUsed();
        if (arguments.size() > 1u && arguments[1].isFunction()) {
            struct ReviverFrame {
                RuntimeObjectId holder = kInvalidRuntimeObjectId;
                std::string key;
                Value value = Value::undefined();
                std::size_t parentContainerDepth = 0u;
                std::size_t containerDepth = 0u;
                std::size_t capturedArrayLength = 0u;
                std::size_t nextArrayIndex = 0u;
                std::size_t nextObjectKey = 0u;
                std::size_t snapshotKeyBytes = 0u;
                bool initialized = false;
                bool array = false;
                std::vector<std::string> keys;
            };

            const Value reviver = arguments[1];
            std::vector<ReviverFrame> traversal;
            std::vector<Value> callbackArguments;
            try {
                traversal.reserve(maximumDepth + 1u);
                callbackArguments.reserve(2u);
            } catch (const std::bad_alloc&) {
                fail(RuntimeErrorCode::AllocationFailure, callSite);
                rollback();
                return false;
            }

            RuntimeObjectId rootHolder = kInvalidRuntimeObjectId;
            const std::vector<Value> noElements;
            RuntimeErrorCode holderError = RuntimeErrorCode::None;
            if (!context_.createObject(false, noElements, rootHolder,
                    holderError) ||
                !context_.writeProperty(rootHolder, std::string(), parsed,
                    holderError)) {
                fail(holderError, callSite);
                rollback();
                return false;
            }

            try {
                ReviverFrame root;
                root.holder = rootHolder;
                traversal.push_back(std::move(root));
            } catch (const std::bad_alloc&) {
                fail(RuntimeErrorCode::AllocationFailure, callSite);
                rollback();
                return false;
            }

            std::size_t activeSnapshotKeyCount = 0u;
            std::size_t activeSnapshotKeyBytes = 0u;
            bool traversalSucceeded = true;
            bool reviverCallbackStarted = false;
            Value revived = Value::undefined();
            try {
                while (!traversal.empty()) {
                    ReviverFrame& frame = traversal.back();
                    if (!frame.initialized) {
                        if (!context_.consumeStep(callSite)) {
                            traversalSucceeded = false;
                            break;
                        }
                        RuntimeErrorCode propertyError =
                            RuntimeErrorCode::None;
                        if (!context_.readProperty(frame.holder, frame.key,
                                frame.value, propertyError, callSite)) {
                            fail(propertyError, callSite);
                            traversalSucceeded = false;
                            break;
                        }
                        frame.initialized = true;

                        if (frame.value.isObject()) {
                            const RuntimeObjectId containerId =
                                frame.value.objectId();
                            if (context_.objectAt(containerId) == nullptr) {
                                fail(RuntimeErrorCode::TypeError, callSite);
                                traversalSucceeded = false;
                                break;
                            }
                            if (frame.parentContainerDepth >= maximumDepth) {
                                fail(RuntimeErrorCode::JsonNestingLimitExceeded,
                                    callSite);
                                traversalSucceeded = false;
                                break;
                            }
                            frame.containerDepth =
                                frame.parentContainerDepth + 1u;
                            if (!context_.isArrayObject(containerId,
                                    frame.array)) {
                                fail(RuntimeErrorCode::TypeError, callSite);
                                traversalSucceeded = false;
                                break;
                            }
                            if (frame.array) {
                                if (!context_.getArrayLength(containerId,
                                        frame.capturedArrayLength)) {
                                    fail(RuntimeErrorCode::TypeError,
                                        callSite);
                                    traversalSucceeded = false;
                                    break;
                                }
                            } else {
                                std::vector<RuntimeContext::RuntimeProperty>
                                    ownProperties;
                                RuntimeErrorCode snapshotError =
                                    RuntimeErrorCode::None;
                                if (!context_.snapshotOwnProperties(
                                        containerId, ownProperties,
                                        snapshotError)) {
                                    fail(snapshotError, callSite);
                                    traversalSucceeded = false;
                                    break;
                                }
                                if (ownProperties.size() >
                                    context_.limits_.maxPropertiesPerObject) {
                                    fail(RuntimeErrorCode::PropertyLimitExceeded,
                                        callSite);
                                    traversalSucceeded = false;
                                    break;
                                }
                                for (const RuntimeContext::RuntimeProperty& property :
                                    ownProperties) {
                                    if (!context_.consumeStep(callSite)) {
                                        traversalSucceeded = false;
                                        break;
                                    }
                                    if (property.symbolKey !=
                                            WellKnownSymbol::None) continue;
                                    if (!property.attributes.isValid() ||
                                        !property.attributes.isFullyKnown()) {
                                        fail(RuntimeErrorCode::InvalidReceiver,
                                            callSite);
                                        traversalSucceeded = false;
                                        break;
                                    }
                                    if (!property.attributes.enumerable())
                                        continue;
                                    const std::size_t keyBytes =
                                        property.key.size();
                                    const std::size_t countCapacity =
                                        context_.limits_.maxTotalProperties;
                                    const std::size_t byteCapacity =
                                        context_.limits_.maxTotalPropertyKeyBytes;
                                    if (activeSnapshotKeyCount > countCapacity ||
                                        frame.keys.size() >= countCapacity -
                                            activeSnapshotKeyCount ||
                                        activeSnapshotKeyBytes > byteCapacity ||
                                        frame.snapshotKeyBytes > byteCapacity -
                                            activeSnapshotKeyBytes ||
                                        keyBytes > byteCapacity -
                                            activeSnapshotKeyBytes -
                                            frame.snapshotKeyBytes) {
                                        fail(RuntimeErrorCode::PropertyLimitExceeded,
                                            callSite);
                                        traversalSucceeded = false;
                                        break;
                                    }
                                    frame.keys.push_back(property.key);
                                    frame.snapshotKeyBytes += keyBytes;
                                }
                                if (!traversalSucceeded) break;
                                const auto orderedBefore = [](
                                    const std::string& left,
                                    const std::string& right) {
                                    std::uint32_t leftIndex = 0u;
                                    std::uint32_t rightIndex = 0u;
                                    const bool leftIsIndex =
                                        parseEcmaArrayIndex(left, leftIndex);
                                    const bool rightIsIndex =
                                        parseEcmaArrayIndex(right, rightIndex);
                                    if (leftIsIndex != rightIsIndex)
                                        return leftIsIndex;
                                    return leftIsIndex &&
                                        leftIndex < rightIndex;
                                };
                                std::stable_sort(frame.keys.begin(),
                                    frame.keys.end(), orderedBefore);
                                activeSnapshotKeyCount += frame.keys.size();
                                activeSnapshotKeyBytes +=
                                    frame.snapshotKeyBytes;
                            }
                        }
                    }

                    bool hasChild = false;
                    std::string childKey;
                    if (frame.value.isObject() && frame.array &&
                        frame.nextArrayIndex < frame.capturedArrayLength) {
                        char indexBuffer[32] = {};
                        const auto conversion = std::to_chars(indexBuffer,
                            indexBuffer + sizeof(indexBuffer),
                            frame.nextArrayIndex);
                        if (conversion.ec != std::errc()) {
                            fail(RuntimeErrorCode::InvalidAstState, callSite);
                            traversalSucceeded = false;
                            break;
                        }
                        childKey.assign(indexBuffer,
                            static_cast<std::size_t>(conversion.ptr -
                                indexBuffer));
                        ++frame.nextArrayIndex;
                        hasChild = true;
                    } else if (frame.value.isObject() && !frame.array &&
                        frame.nextObjectKey < frame.keys.size()) {
                        childKey = frame.keys[frame.nextObjectKey++];
                        hasChild = true;
                    }

                    if (hasChild) {
                        if (!context_.consumeStep(callSite)) {
                            traversalSucceeded = false;
                            break;
                        }
                        ReviverFrame child;
                        child.holder = frame.value.objectId();
                        child.key = std::move(childKey);
                        child.parentContainerDepth = frame.containerDepth;
                        traversal.push_back(std::move(child));
                        continue;
                    }

                    Value keyValue = Value::undefined();
                    RuntimeErrorCode stringError = RuntimeErrorCode::None;
                    if (!context_.createOrGetCachedString(SourceView(
                            frame.key.data(), frame.key.size()), keyValue,
                            stringError)) {
                        fail(stringError, callSite);
                        traversalSucceeded = false;
                        break;
                    }
                    callbackArguments.clear();
                    callbackArguments.push_back(keyValue);
                    callbackArguments.push_back(frame.value);
                    Value replacement = Value::undefined();
                    reviverCallbackStarted = true;
                    if (!invokeFunction(reviver.functionId(),
                            callbackArguments, callSite, replacement,
                            Value::object(frame.holder))) {
                        traversalSucceeded = false;
                        break;
                    }

                    RuntimeErrorCode replacementError =
                        RuntimeErrorCode::None;
                    if (replacement.isUndefined()) {
                        bool deleted = false;
                        if (!context_.deleteOwnProperty(frame.holder,
                                frame.key, deleted, replacementError)) {
                            fail(replacementError, callSite);
                            traversalSucceeded = false;
                            break;
                        }
                    } else if (!context_.writeProperty(frame.holder,
                            frame.key, replacement, replacementError)) {
                        fail(replacementError, callSite);
                        traversalSucceeded = false;
                        break;
                    }

                    const bool isRoot = traversal.size() == 1u;
                    if (isRoot) revived = replacement;
                    if (frame.keys.size() > activeSnapshotKeyCount ||
                        frame.snapshotKeyBytes > activeSnapshotKeyBytes) {
                        fail(RuntimeErrorCode::InvalidAstState, callSite);
                        traversalSucceeded = false;
                        break;
                    }
                    activeSnapshotKeyCount -= frame.keys.size();
                    activeSnapshotKeyBytes -= frame.snapshotKeyBytes;
                    traversal.pop_back();
                    if (isRoot) break;
                }
            } catch (const std::bad_alloc&) {
                fail(RuntimeErrorCode::AllocationFailure, callSite);
                traversalSucceeded = false;
            }
            if (!traversalSucceeded) {
                if (!reviverCallbackStarted) rollback();
                return false;
            }
            result = revived;
            return true;
        }

        result = parsed;
        return true;
    }

    bool stringifyJson(const std::vector<Value>& arguments,
        SourceLocation callSite, Value& result)
    {
        result = Value::undefined();
        if (arguments.size() > 1u) {
            fail(RuntimeErrorCode::UnsupportedFeature, callSite);
            return false;
        }

        const Value source = arguments.empty() ? Value::undefined()
            : arguments[0];
        const std::size_t maximumDepth = std::min(
            context_.limits_.maxJsonNestingDepth,
            kDefaultMaxJavaScriptJsonNestingDepth);
        const std::size_t outputCapacity = std::min<std::size_t>(
            context_.limits_.maxRuntimeStringLength, 64u * 1024u);
        const std::size_t previousExecutionSteps = context_.executionSteps_;

        std::vector<char> outputBuffer;
        std::vector<PortableJsonTraversalFrame> traversalFrames;
        try {
            outputBuffer.resize(outputCapacity);
            traversalFrames.resize(maximumDepth);
        } catch (const std::bad_alloc&) {
            fail(RuntimeErrorCode::AllocationFailure, callSite);
            return false;
        }

        struct SerializeStepContext {
            RuntimeContext* runtime = nullptr;
            SourceLocation callSite;
        } stepContext{&context_, callSite};
        const auto consumeStep = [](void* context) {
            SerializeStepContext* step =
                static_cast<SerializeStepContext*>(context);
            return step != nullptr && step->runtime != nullptr &&
                step->runtime->consumeStep(step->callSite);
        };
        const auto objectValidator = [](void* context,
            RuntimeObjectId objectId) {
            RuntimeContext* runtime = static_cast<RuntimeContext*>(context);
            if (runtime == nullptr) return
                PortableJsonObjectDisposition::HostedOperationRequired;
            const RuntimeContext::RuntimeObject* object =
                runtime->objectAt(objectId);
            if (object == nullptr || object->arrayIterator ||
                objectId == runtime->objectPrototype_ ||
                objectId == runtime->arrayPrototype_ ||
                objectId == runtime->mathObject_ ||
                objectId == runtime->eventConstantsObject_ ||
                objectId == runtime->eventObject_) {
                return PortableJsonObjectDisposition::HostedOperationRequired;
            }
            return PortableJsonObjectDisposition::Supported;
        };

        PortableJsonSerializeLimits limits;
        limits.maxOutputBytes = outputCapacity;
        limits.maxNestingDepth = maximumDepth;
        limits.maxArrayLength = maximumSupportedArrayLength(context_.limits_);
        limits.maxSteps = context_.executionSteps_ >=
                context_.limits_.maxExecutionSteps
            ? 0u : context_.limits_.maxExecutionSteps -
                context_.executionSteps_;

        PortableJsonOutputWriter output(outputBuffer.data(),
            outputBuffer.size());
        char numberScratch[384] = {};
        const PortableJsonSerializeResult serialized = serializePortableJson(
            source, context_.objectStore_, context_.stringStore_, output,
            PortableJsonSerializeStorageView{traversalFrames.data(),
                traversalFrames.size(), numberScratch, sizeof(numberScratch)},
            limits, consumeStep, &stepContext, objectValidator, &context_);

        if (serialized.status == PortableJsonSerializeStatus::Error &&
            (serialized.error.code ==
                    PortableJsonSerializeErrorCode::UnsupportedCallback ||
                serialized.error.code ==
                    PortableJsonSerializeErrorCode::HostedOperationRequired)) {
            // The shared pass used direct, callback-free store reads. Restore
            // its budget use before the one hosted callback-capable pass so
            // callbacks run exactly once.
            context_.executionSteps_ = previousExecutionSteps;
            std::vector<char>().swap(outputBuffer);
            std::vector<PortableJsonTraversalFrame>().swap(traversalFrames);
            return stringifyJsonHostedCallbacks(arguments, callSite, result);
        }

        if (serialized.status == PortableJsonSerializeStatus::NoResult)
            return true;
        if (serialized.status == PortableJsonSerializeStatus::Error) {
            switch (serialized.error.code) {
            case PortableJsonSerializeErrorCode::OutputCapacityExceeded:
                fail(RuntimeErrorCode::StringLimitExceeded, callSite);
                break;
            case PortableJsonSerializeErrorCode::ArrayLimitExceeded:
                fail(RuntimeErrorCode::ArrayLimitExceeded, callSite);
                break;
            case PortableJsonSerializeErrorCode::DepthLimitExceeded:
                fail(RuntimeErrorCode::JsonNestingLimitExceeded, callSite);
                break;
            case PortableJsonSerializeErrorCode::TraversalCapacityExceeded:
                fail(RuntimeErrorCode::PortableCapacityExceeded, callSite);
                break;
            case PortableJsonSerializeErrorCode::StepBudgetExceeded:
                fail(RuntimeErrorCode::ExecutionBudgetExceeded, callSite);
                break;
            case PortableJsonSerializeErrorCode::CycleDetected:
            case PortableJsonSerializeErrorCode::StaleObjectHandle:
            case PortableJsonSerializeErrorCode::UnsupportedValue:
            case PortableJsonSerializeErrorCode::InvalidValue:
                fail(RuntimeErrorCode::TypeError, callSite);
                break;
            case PortableJsonSerializeErrorCode::UnsupportedPropertyAttributes:
                fail(RuntimeErrorCode::UnsupportedFeature, callSite);
                break;
            case PortableJsonSerializeErrorCode::StaleStringHandle:
            case PortableJsonSerializeErrorCode::InvalidStringState:
            case PortableJsonSerializeErrorCode::NumericFormattingFailure:
            case PortableJsonSerializeErrorCode::InvalidInput:
            case PortableJsonSerializeErrorCode::InternalInvariantFailure:
                fail(RuntimeErrorCode::InvalidAstState, callSite);
                break;
            case PortableJsonSerializeErrorCode::ObjectStoreFailure:
                fail(context_.mapPortableObjectError(
                    serialized.error.objectError), callSite);
                break;
            case PortableJsonSerializeErrorCode::None:
            case PortableJsonSerializeErrorCode::UnsupportedCallback:
            case PortableJsonSerializeErrorCode::HostedOperationRequired:
                fail(RuntimeErrorCode::InvalidAstState, callSite);
                break;
            }
            return false;
        }

        RuntimeErrorCode stringError = RuntimeErrorCode::None;
        if (!context_.createString(serialized.output, result, stringError)) {
            fail(stringError, callSite);
            return false;
        }
        return true;
    }

    bool stringifyJsonHostedCallbacks(const std::vector<Value>& arguments,
        SourceLocation callSite, Value& result)
    {
        result = Value::undefined();
        if (arguments.size() > 1u) {
            fail(RuntimeErrorCode::UnsupportedFeature, callSite);
            return false;
        }
        const Value source = arguments.empty() ? Value::undefined()
            : arguments[0];
        const std::size_t maximumDepth = std::min(
            context_.limits_.maxJsonNestingDepth,
            kDefaultMaxJavaScriptJsonNestingDepth);
        const std::size_t outputCapacity = std::min<std::size_t>(
            context_.limits_.maxRuntimeStringLength, 64u * 1024u);
        std::vector<char> outputBuffer;
        PortableJsonOutputWriter output;
        std::array<RuntimeObjectId,
            kDefaultMaxJavaScriptJsonNestingDepth> activeObjects{};
        std::size_t activeObjectCount = 0u;

        struct OutputStepContext {
            RuntimeContext* runtime = nullptr;
            SourceLocation callSite;
        } outputStepContext{&context_, callSite};
        const PortableJsonWriteStepConsumer outputStep =
            [](void* context) {
                OutputStepContext* step =
                    static_cast<OutputStepContext*>(context);
                return step != nullptr && step->runtime != nullptr &&
                    step->runtime->consumeStep(step->callSite);
            };
        const auto reportOutputFailure = [&]() {
            switch (output.error()) {
            case PortableJsonWriteErrorCode::None:
                return true;
            case PortableJsonWriteErrorCode::CapacityExceeded:
                fail(RuntimeErrorCode::StringLimitExceeded, callSite);
                return false;
            case PortableJsonWriteErrorCode::InvalidUtf8:
            case PortableJsonWriteErrorCode::InvalidBuffer:
                fail(RuntimeErrorCode::TypeError, callSite);
                return false;
            case PortableJsonWriteErrorCode::StepBudgetExceeded:
                return false;
            }
            fail(RuntimeErrorCode::InvalidAstState, callSite);
            return false;
        };

        const auto appendBytes = [&](const char* bytes, std::size_t length) {
            return output.appendSpan(bytes, length)
                ? true : reportOutputFailure();
        };
        const auto appendText = [&](const char* text) {
            std::size_t length = 0u;
            while (text[length] != '\0') ++length;
            return appendBytes(text, length);
        };
        const auto appendQuotedBytes = [&](const char* text,
            std::size_t textLength) {
            return output.appendQuotedUtf8(SourceView(text, textLength),
                    outputStep, &outputStepContext)
                ? true : reportOutputFailure();
        };
        const auto appendQuoted = [&](const std::string& text) {
            return appendQuotedBytes(text.data(), text.size());
        };
        const auto prepareValue = [&](const Value& original,
            const std::string& propertyKey, Value& prepared,
            bool& omitted) {
            prepared = original;
            omitted = false;
            if (!prepared.isObject()) {
                omitted = prepared.isUndefined() || prepared.isFunction() ||
                    prepared.isSymbol();
                return true;
            }
            if (context_.objectAt(prepared.objectId()) == nullptr) {
                fail(RuntimeErrorCode::TypeError, callSite);
                return false;
            }
            Value toJson = Value::undefined();
            RuntimeErrorCode propertyError = RuntimeErrorCode::None;
            if (!context_.readProperty(prepared.objectId(), "toJSON", toJson,
                    propertyError, callSite)) {
                fail(propertyError, callSite);
                return false;
            }
            if (toJson.isFunction()) {
                Value keyValue = Value::undefined();
                RuntimeErrorCode stringError = RuntimeErrorCode::None;
                if (!context_.createOrGetCachedString(SourceView(
                        propertyKey.data(), propertyKey.size()), keyValue,
                        stringError)) {
                    fail(stringError, callSite);
                    return false;
                }
                std::vector<Value> toJsonArguments;
                try {
                    toJsonArguments.push_back(keyValue);
                } catch (const std::bad_alloc&) {
                    fail(RuntimeErrorCode::AllocationFailure, callSite);
                    return false;
                }
                Value replacement = Value::undefined();
                if (!invokeFunction(toJson.functionId(), toJsonArguments,
                        callSite, replacement, original)) return false;
                prepared = replacement;
            }
            omitted = prepared.isUndefined() || prepared.isFunction() ||
                prepared.isSymbol();
            return true;
        };

        auto emitValue = [&](auto&& self, const Value& value,
                             std::size_t parentDepth) -> bool {
            switch (value.type()) {
            case ValueType::Undefined:
            case ValueType::Function:
            case ValueType::Symbol:
                fail(RuntimeErrorCode::TypeError, callSite);
                return false;
            case ValueType::Null:
                return appendText("null");
            case ValueType::Boolean:
                return appendText(value.booleanValue() ? "true" : "false");
            case ValueType::Number: {
                if (!std::isfinite(value.numberValue()))
                    return appendText("null");
                if (value.numberValue() == 0.0) return appendText("0");
                bool converted = false;
                std::string number = numberToString(value.numberValue(),
                    converted);
                if (!converted) {
                    fail(RuntimeErrorCode::AllocationFailure, callSite);
                    return false;
                }
                return appendBytes(number.data(), number.size());
            }
            case ValueType::String: {
                SourceView text;
                RuntimeErrorCode stringError = RuntimeErrorCode::None;
                if (!context_.stringView(value, text, stringError)) {
                    fail(RuntimeErrorCode::InvalidAstState, callSite);
                    return false;
                }
                return appendQuotedBytes(text.data == nullptr ? "" :
                    text.data, text.length);
            }
            case ValueType::HostObject:
            case ValueType::SymbolNamespace:
            case ValueType::ObjectNamespace:
            case ValueType::JsonNamespace:
                fail(RuntimeErrorCode::TypeError, callSite);
                return false;
            case ValueType::Object:
                break;
            }

            const RuntimeObjectId objectId = value.objectId();
            const RuntimeContext::RuntimeObject* object =
                context_.objectAt(objectId);
            if (object == nullptr || object->arrayIterator ||
                objectId == context_.objectPrototype_ ||
                objectId == context_.arrayPrototype_ ||
                objectId == context_.mathObject_ ||
                objectId == context_.eventConstantsObject_ ||
                objectId == context_.eventObject_) {
                fail(RuntimeErrorCode::TypeError, callSite);
                return false;
            }
            bool objectArray = false;
            if (!context_.isArrayObject(objectId, objectArray)) {
                fail(RuntimeErrorCode::TypeError, callSite);
                return false;
            }
            if (parentDepth >= maximumDepth) {
                fail(RuntimeErrorCode::JsonNestingLimitExceeded, callSite);
                return false;
            }
            for (std::size_t index = 0u; index < activeObjectCount; ++index) {
                if (activeObjects[index] == objectId) {
                    fail(RuntimeErrorCode::TypeError, callSite);
                    return false;
                }
            }
            if (activeObjectCount >= activeObjects.size()) {
                fail(RuntimeErrorCode::JsonNestingLimitExceeded, callSite);
                return false;
            }
            activeObjects[activeObjectCount++] = objectId;
            const std::size_t childDepth = parentDepth + 1u;

            bool succeeded = true;
            if (objectArray) {
                std::size_t length = 0u;
                if (!context_.getArrayLength(objectId, length)) {
                    fail(RuntimeErrorCode::TypeError, callSite);
                    --activeObjectCount;
                    return false;
                }
                if (length > maximumSupportedArrayLength(context_.limits_)) {
                    fail(RuntimeErrorCode::ArrayLimitExceeded, callSite);
                    succeeded = false;
                } else if (!appendText("[")) {
                    succeeded = false;
                } else {
                    for (std::size_t index = 0u;
                        succeeded && index < length; ++index) {
                        if (index != 0u && !appendText(",")) {
                            succeeded = false;
                            break;
                        }
                        char digits[sizeof(std::size_t) * 3u] = {};
                        std::size_t digitCount = 0u;
                        std::size_t remaining = index;
                        do {
                            digits[digitCount++] = static_cast<char>('0' +
                                remaining % 10u);
                            remaining /= 10u;
                        } while (remaining != 0u);
                        std::string key;
                        try {
                            key.reserve(digitCount);
                            while (digitCount != 0u)
                                key.push_back(digits[--digitCount]);
                        } catch (const std::bad_alloc&) {
                            fail(RuntimeErrorCode::AllocationFailure,
                                callSite);
                            succeeded = false;
                            break;
                        }
                        Value element = Value::undefined();
                        RuntimeErrorCode propertyError =
                            RuntimeErrorCode::None;
                        if (!context_.readProperty(objectId, key, element,
                                propertyError, callSite)) {
                            fail(propertyError, callSite);
                            succeeded = false;
                            break;
                        }
                        Value prepared = Value::undefined();
                        bool omitted = false;
                        if (!prepareValue(element, key, prepared, omitted)) {
                            succeeded = false;
                            break;
                        }
                        if (omitted) {
                            succeeded = appendText("null");
                        } else {
                            succeeded = self(self, prepared, childDepth);
                        }
                    }
                    if (succeeded) succeeded = appendText("]");
                }
            } else {
                std::vector<std::string> keys;
                std::vector<RuntimeContext::RuntimeProperty> properties;
                RuntimeErrorCode snapshotError = RuntimeErrorCode::None;
                if (!context_.snapshotOwnProperties(objectId, properties,
                        snapshotError)) {
                    fail(snapshotError, callSite);
                    --activeObjectCount;
                    return false;
                }
                try {
                    keys.reserve(properties.size());
                    for (const RuntimeContext::RuntimeProperty& property :
                            properties) {
                        if (property.symbolKey != WellKnownSymbol::None)
                            continue;
                        if (!property.attributes.isValid() ||
                            !property.attributes.isFullyKnown()) {
                            fail(RuntimeErrorCode::UnsupportedFeature,
                                callSite);
                            succeeded = false;
                            break;
                        }
                        if (property.attributes.enumerable())
                            keys.push_back(property.key);
                    }
                    std::stable_sort(keys.begin(), keys.end(),
                        [](const std::string& left,
                           const std::string& right) {
                            std::uint32_t leftIndex = 0u;
                            std::uint32_t rightIndex = 0u;
                            const bool leftIsIndex = parseEcmaArrayIndex(
                                left, leftIndex);
                            const bool rightIsIndex = parseEcmaArrayIndex(
                                right, rightIndex);
                            if (leftIsIndex != rightIsIndex)
                                return leftIsIndex;
                            return leftIsIndex && rightIsIndex &&
                                leftIndex < rightIndex;
                        });
                } catch (const std::bad_alloc&) {
                    fail(RuntimeErrorCode::AllocationFailure, callSite);
                    succeeded = false;
                }
                if (succeeded && !appendText("{")) succeeded = false;
                std::size_t emittedProperties = 0u;
                for (std::size_t index = 0u;
                    succeeded && index < keys.size(); ++index) {
                    Value propertyValue = Value::undefined();
                    RuntimeErrorCode propertyError =
                        RuntimeErrorCode::None;
                    if (!context_.readProperty(objectId, keys[index],
                            propertyValue, propertyError, callSite)) {
                        fail(propertyError, callSite);
                        succeeded = false;
                        break;
                    }
                    Value prepared = Value::undefined();
                    bool omitted = false;
                    if (!prepareValue(propertyValue, keys[index], prepared,
                            omitted)) {
                        succeeded = false;
                        break;
                    }
                    if (omitted) continue;
                    if (emittedProperties != 0u && !appendText(",")) {
                        succeeded = false;
                        break;
                    }
                    if (!appendQuoted(keys[index]) || !appendText(":")) {
                        succeeded = false;
                        break;
                    }
                    if (!self(self, prepared, childDepth)) {
                        succeeded = false;
                        break;
                    }
                    ++emittedProperties;
                }
                if (succeeded) succeeded = appendText("}");
            }
            --activeObjectCount;
            return succeeded;
        };

        try {
            Value prepared = Value::undefined();
            bool omitted = false;
            if (!prepareValue(source, std::string(), prepared, omitted))
                return false;
            if (omitted) return true;
            outputBuffer.resize(outputCapacity);
            output.reset(outputBuffer.data(), outputBuffer.size());
            if (!emitValue(emitValue, prepared, 0u)) return false;
            SourceView outputText;
            if (!output.complete(outputText)) {
                reportOutputFailure();
                return false;
            }
            RuntimeErrorCode error = RuntimeErrorCode::None;
            if (!context_.createString(outputText, result, error)) {
                fail(error, callSite);
                return false;
            }
            return true;
        } catch (const std::bad_alloc&) {
            fail(RuntimeErrorCode::AllocationFailure, callSite);
            return false;
        }
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
        case RuntimeContext::NativeFunctionId::JsonParse:
            succeeded = parseJson(arguments, callSite, result);
            break;
        case RuntimeContext::NativeFunctionId::JsonStringify:
            succeeded = stringifyJson(arguments, callSite, result);
            break;
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
            const RuntimeObjectId arrayId = receiver.isObject()
                ? receiver.objectId() : kInvalidRuntimeObjectId;
            bool isArray = false;
            if (context_.objectAt(arrayId) == nullptr ||
                !context_.isArrayObject(arrayId, isArray) || !isArray) {
                fail(RuntimeErrorCode::InvalidReceiver, callSite);
                succeeded = false;
                break;
            }
            std::size_t oldLength = 0u;
            if (!context_.getArrayLength(arrayId, oldLength)) {
                fail(RuntimeErrorCode::InvalidReceiver, callSite);
                succeeded = false;
                break;
            }
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
            std::vector<RuntimeContext::RuntimeArrayElement> oldElements;
            RuntimeErrorCode snapshotError = RuntimeErrorCode::None;
            if (!context_.snapshotArrayElements(arrayId, oldElements,
                    snapshotError)) {
                fail(snapshotError, callSite);
                succeeded = false;
                break;
            }
            if (arguments.size() > context_.limits_.maxArrayElements ||
                oldElements.size() > context_.limits_.maxArrayElements -
                    arguments.size()) {
                GXOS_JS84_DIAGNOSTIC(
                    ++context_.diagnostics_.failedArraySlotAllocations);
                fail(RuntimeErrorCode::ArrayLimitExceeded, callSite);
                succeeded = false;
                break;
            }
            if (arguments.size() > context_.limits_.maxTotalArrayElements ||
                context_.objectStore_.arrayElementCount() >
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
            PortableObjectError objectError;
            for (std::size_t index = 0u; index < arguments.size(); ++index) {
                if (context_.objectStore_.setArrayElement(arrayId,
                        oldLength + index, arguments[index], objectError,
                        RuntimeContext::PropertyAttributes::ordinaryData().bits))
                    continue;
                RuntimeErrorCode rollbackError = RuntimeErrorCode::None;
                (void)context_.setArrayLength(arrayId,
                    Value::number(static_cast<double>(oldLength)),
                    rollbackError);
                GXOS_JS84_DIAGNOSTIC(
                    ++context_.diagnostics_.failedArraySlotAllocations);
                fail(context_.mapPortableObjectError(objectError.code),
                    callSite);
                succeeded = false;
                break;
            }
            if (!succeeded) break;
            GXOS_JS84_DIAGNOSTIC({
                context_.diagnostics_.arraySlotsAllocated += arguments.size();
                if (context_.diagnostics_.arraySlotHighWater <
                    context_.objectStore_.arrayElementCount()) {
                    context_.diagnostics_.arraySlotHighWater =
                        context_.objectStore_.arrayElementCount();
                }
            });
            result = Value::number(static_cast<double>(oldLength +
                arguments.size()));
            break;
        }
        case RuntimeContext::NativeFunctionId::ArrayPop: {
            const RuntimeObjectId arrayId = receiver.isObject()
                ? receiver.objectId() : kInvalidRuntimeObjectId;
            bool isArray = false;
            if (context_.objectAt(arrayId) == nullptr ||
                !context_.isArrayObject(arrayId, isArray) || !isArray) {
                fail(RuntimeErrorCode::InvalidReceiver, callSite);
                succeeded = false;
                break;
            }
            std::size_t oldLength = 0u;
            if (!context_.getArrayLength(arrayId, oldLength)) {
                fail(RuntimeErrorCode::InvalidReceiver, callSite);
                succeeded = false;
                break;
            }
            if (oldLength == 0u) {
                result = Value::undefined();
                break;
            }
            const std::size_t removedIndex = oldLength - 1u;
            RuntimeErrorCode readError = RuntimeErrorCode::None;
            if (!context_.readArrayIndex(arrayId, removedIndex, result,
                    readError, callSite)) {
                fail(readError, callSite);
                succeeded = false;
                break;
            }
            bool deleted = false;
            RuntimeErrorCode deleteError = RuntimeErrorCode::None;
            if (!context_.deleteOwnProperty(arrayId,
                    std::to_string(removedIndex), deleted, deleteError) ||
                !context_.setArrayLength(arrayId,
                    Value::number(static_cast<double>(removedIndex)),
                    deleteError)) {
                fail(deleteError, callSite);
                succeeded = false;
                break;
            }
            break;
        }
        case RuntimeContext::NativeFunctionId::ArrayForEach:
        case RuntimeContext::NativeFunctionId::ArrayMap:
        case RuntimeContext::NativeFunctionId::ArrayFilter:
        case RuntimeContext::NativeFunctionId::ArraySome:
        case RuntimeContext::NativeFunctionId::ArrayEvery:
        case RuntimeContext::NativeFunctionId::ArrayFind:
        case RuntimeContext::NativeFunctionId::ArrayFindIndex:
        case RuntimeContext::NativeFunctionId::ArrayFindLast: {
            const RuntimeContext::NativeFunctionId native =
                static_cast<RuntimeContext::NativeFunctionId>(
                    function.nativeFunction);
            ArrayTraversalKind kind = ArrayTraversalKind::ForEach;
            if (native == RuntimeContext::NativeFunctionId::ArrayMap) {
                kind = ArrayTraversalKind::Map;
            } else if (native ==
                    RuntimeContext::NativeFunctionId::ArrayFilter) {
                kind = ArrayTraversalKind::Filter;
            } else if (native ==
                    RuntimeContext::NativeFunctionId::ArraySome) {
                kind = ArrayTraversalKind::Some;
            } else if (native ==
                    RuntimeContext::NativeFunctionId::ArrayEvery) {
                kind = ArrayTraversalKind::Every;
            } else if (native ==
                    RuntimeContext::NativeFunctionId::ArrayFind) {
                kind = ArrayTraversalKind::Find;
            } else if (native ==
                    RuntimeContext::NativeFunctionId::ArrayFindIndex) {
                kind = ArrayTraversalKind::FindIndex;
            } else if (native ==
                    RuntimeContext::NativeFunctionId::ArrayFindLast) {
                kind = ArrayTraversalKind::FindLast;
            }
            const ArrayTraversalControl traversal = traverseArray(receiver,
                argumentOrUndefined(0u), argumentOrUndefined(1u), callSite,
                kind, result);
            succeeded = traversal != ArrayTraversalControl::Error;
            break;
        }
        case RuntimeContext::NativeFunctionId::ArrayReduce: {
            const bool hasInitialValue = arguments.size() > 1u;
            const Value initialValue = hasInitialValue
                ? arguments[1u] : Value::undefined();
            const ArrayTraversalControl traversal = reduceArray(receiver,
                argumentOrUndefined(0u), hasInitialValue, initialValue,
                callSite, result);
            succeeded = traversal != ArrayTraversalControl::Error;
            break;
        }
        case RuntimeContext::NativeFunctionId::ArrayReduceRight: {
            const bool hasInitialValue = arguments.size() > 1u;
            const Value initialValue = hasInitialValue
                ? arguments[1u] : Value::undefined();
            const ArrayTraversalControl traversal = reduceRightArray(receiver,
                argumentOrUndefined(0u), hasInitialValue, initialValue,
                callSite, result);
            succeeded = traversal != ArrayTraversalControl::Error;
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
            bool arrayIsArray = false;
            if (array == nullptr ||
                !context_.isArrayObject(arrayId, arrayIsArray) ||
                !arrayIsArray) {
                fail(RuntimeErrorCode::InvalidReceiver, callSite);
                succeeded = false;
                break;
            }
            const std::vector<Value> noElements;
            RuntimeObjectId iteratorId = kInvalidRuntimeObjectId;
            RuntimeErrorCode error = RuntimeErrorCode::None;
            const std::size_t objectCountBefore =
                context_.objectStore_.objectCount();
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
                RuntimeErrorCode rollbackError = RuntimeErrorCode::None;
                (void)context_.rollbackObjects(objectCountBefore,
                    rollbackError);
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
                bool arrayIsArray = false;
                if (array == nullptr ||
                    !context_.isArrayObject(arrayId, arrayIsArray) ||
                    !arrayIsArray) {
                    fail(RuntimeErrorCode::InvalidAstState, callSite);
                    succeeded = false;
                    break;
                }
                std::size_t arrayLength = 0u;
                if (!context_.getArrayLength(arrayId, arrayLength)) {
                    fail(RuntimeErrorCode::InvalidAstState, callSite);
                    succeeded = false;
                    break;
                }
                if (index < arrayLength) {
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
            SourceView text;
            RuntimeErrorCode stringError = RuntimeErrorCode::None;
            if (!context_.stringView(input, text, stringError)) {
                fail(RuntimeErrorCode::InvalidAstState, SourceLocation());
                return false;
            }
            if (!parseNumber(text, number)) {
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
        case ValueType::JsonNamespace:
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
            SourceView string;
            RuntimeErrorCode stringError = RuntimeErrorCode::None;
            if (!context_.stringView(input, string, stringError)) {
                fail(RuntimeErrorCode::InvalidAstState, SourceLocation());
                return false;
            }
            try {
                text.assign(string.data == nullptr ? "" : string.data,
                    string.length);
            } catch (const std::bad_alloc&) {
                fail(RuntimeErrorCode::AllocationFailure, SourceLocation());
                return false;
            }
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
        case ValueType::JsonNamespace:
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

        const Binding* current = nullptr;
        EnvironmentError environmentError;
        if (!resolveBinding(name, current, environmentError)) {
            fail(runtimeEnvironmentError(environmentError.code),
                context_.ast_.node(node.left).location);
            return false;
        }
        if (!current->initialized) {
            fail(RuntimeErrorCode::UninitializedBinding,
                context_.ast_.node(node.left).location);
            return false;
        }
        Value left = current->value;
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
        const Binding* current = nullptr;
        EnvironmentError environmentError;
        if (!resolveBinding(name, current, environmentError)) {
            fail(runtimeEnvironmentError(environmentError.code),
                context_.ast_.node(node.argument).location);
            return false;
        }
        if (!current->initialized) {
            fail(RuntimeErrorCode::UninitializedBinding,
                context_.ast_.node(node.argument).location);
            return false;
        }
        const Value old = current->value;
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
                    iterationEnvironment, error,
                    EnvironmentKind::Iteration)) {
                fail(error, node.location);
                return closeIteratorPreservingError(iterator, node.location);
            }
            currentEnvironment_ = iterationEnvironment;
            if (!declareLexicalIn(iterationEnvironment, bindingName,
                    node.immutableBinding,
                    context_.ast_.node(node.name).location, yielded, true)) {
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

    bool executeStatementList(AstNodeId container, bool inLoop,
        bool inFunction, Control& control)
    {
        if (!validNode(container, SourceLocation())) return false;
        const AstNode& node = context_.ast_.node(container);
        for (std::size_t index = 0; index < node.childCount; ++index) {
            const AstNodeId child = context_.ast_.childAt(container, index);
            if (!validNode(child, node.location) ||
                !executeStatement(child, inLoop, inFunction, control))
                return false;
            if (control.kind != ControlKind::Normal) return true;
        }
        return true;
    }

    bool executeStatement(AstNodeId id, bool inLoop, bool inFunction,
        Control& control)
    {
        if (!beginNode(id)) return false;
        const AstNode& node = context_.ast_.node(id);
        switch (node.kind) {
        case AstNodeKind::Program:
            return executeStatementList(id, inLoop, inFunction, control);
        case AstNodeKind::BlockStatement: {
            const EnvironmentId previousEnvironment = currentEnvironment_;
            if (hasDirectLexicalBindings(id)) {
                EnvironmentId blockEnvironment = kInvalidEnvironmentId;
                RuntimeErrorCode error = RuntimeErrorCode::None;
                if (!context_.createEnvironment(previousEnvironment,
                        blockEnvironment, error,
                        EnvironmentKind::Block)) {
                    fail(error, node.location);
                    return false;
                }
                currentEnvironment_ = blockEnvironment;
                if (!instantiateLexicalBindings(id, blockEnvironment)) {
                    currentEnvironment_ = previousEnvironment;
                    return false;
                }
            }
            const bool succeeded = executeStatementList(id, inLoop,
                inFunction, control);
            currentEnvironment_ = previousEnvironment;
            return succeeded;
        }
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
                const bool isVar = node.declarationKind ==
                    AstDeclarationKind::Var;
                Value value = Value::undefined();
                if (isVar && !declare(name, value,
                        context_.ast_.node(declarator.name).location))
                    return false;
                if (declarator.initializer != kInvalidAstNodeId) {
                    if (!validNode(declarator.initializer, declarator.location)) return false;
                    if (!evalExpression(declarator.initializer, value)) return false;
                    if (!(isVar ? assign(name, value, declarator.location)
                            : initialize(name, value, declarator.location))) {
                        return false;
                    }
                } else if (!isVar && !initialize(name, Value::undefined(),
                        declarator.location)) {
                    return false;
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
            SourceView text;
            RuntimeErrorCode ignored = RuntimeErrorCode::None;
            return context_.stringView(value, text, ignored) &&
                text.length != 0u;
        }
        case ValueType::Function:
            return true;
        case ValueType::Object:
            return true;
        case ValueType::HostObject:
        case ValueType::Symbol:
        case ValueType::SymbolNamespace:
        case ValueType::ObjectNamespace:
        case ValueType::JsonNamespace:
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
            SourceView leftText;
            SourceView rightText;
            RuntimeErrorCode ignored = RuntimeErrorCode::None;
            if (!context_.stringView(left, leftText, ignored) ||
                !context_.stringView(right, rightText, ignored) ||
                leftText.length != rightText.length) return false;
            for (std::size_t index = 0u; index < leftText.length; ++index)
                if (leftText.data[index] != rightText.data[index])
                    return false;
            return true;
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
        case ValueType::JsonNamespace:
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
    EnvironmentId currentEnvironment_ = kInvalidEnvironmentId;
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
    : limits_(limits), ast_()
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
    environmentStore_.reset(kInvalidRuntimeGeneration);
    environment_.bind(nullptr, kInvalidEnvironmentId, EnvironmentLimits());
    environments_.clear();
    functions_.clear();
    portableFunctions_.reset(kInvalidRuntimeGeneration);
    stringStore_.reset(kInvalidRuntimeGeneration);
    objectStore_ = PortableObjectStore();
    objectAdapters_.clear();
    totalStringBytes_ = 0;
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
    globalEnvironmentId_ = kInvalidEnvironmentId;
    eventObjectCache_.fill(kInvalidRuntimeObjectId);
    eventDispatchStack_ = {};
    eventDispatchDepth_ = 0;
    eventStopPropagationFunction_ = kInvalidRuntimeFunctionId;
    arrayIteratorNextFunction_ = kInvalidRuntimeFunctionId;
    arrayIteratorSelfFunction_ = kInvalidRuntimeFunctionId;
    objectKeysFunction_ = kInvalidRuntimeFunctionId;
    objectGetOwnPropertyDescriptorFunction_ = kInvalidRuntimeFunctionId;
    jsonParseFunction_ = kInvalidRuntimeFunctionId;
    jsonStringifyFunction_ = kInvalidRuntimeFunctionId;
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
    runtimeGeneration_ = kInvalidRuntimeGeneration;
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

    if (hostGeneration_ > 0xfffeu) {
        setRuntimeError(RuntimeErrorCode::HostGenerationLimitExceeded,
            SourceLocation());
        return;
    }
    runtimeGeneration_ = static_cast<RuntimeGeneration>(hostGeneration_);

    RuntimeErrorCode error = RuntimeErrorCode::None;
    if (!initializePortableStorage(error)) {
        setRuntimeError(error, SourceLocation());
        return;
    }
    if (!initializeBuiltIns(error)) {
        // Initialization is transactional from the caller's perspective:
        // a context with insufficient limits has no partially usable globals
        // or prototype objects after reset.
        clearRuntimeState();
        setRuntimeError(RuntimeErrorCode::BuiltInInitializationFailed,
            SourceLocation());
    }
}

RuntimeErrorCode RuntimeContext::mapPortableObjectError(
    PortableObjectErrorCode code) const
{
    switch (code) {
    case PortableObjectErrorCode::None: return RuntimeErrorCode::None;
    case PortableObjectErrorCode::InvalidStorage:
        return RuntimeErrorCode::PortableCapacityExceeded;
    case PortableObjectErrorCode::InvalidObject:
    case PortableObjectErrorCode::InvalidArray:
    case PortableObjectErrorCode::InvalidProperty:
    case PortableObjectErrorCode::InvalidPrototype:
    case PortableObjectErrorCode::PrototypeCycle:
    case PortableObjectErrorCode::ArithmeticOverflow:
    case PortableObjectErrorCode::InternalInvariantFailure:
        return RuntimeErrorCode::InvalidAstState;
    case PortableObjectErrorCode::StaleGeneration:
        return RuntimeErrorCode::StaleObjectReference;
    case PortableObjectErrorCode::InvalidIndex:
        return RuntimeErrorCode::ArrayIndexOutOfRange;
    case PortableObjectErrorCode::InvalidLength:
        return RuntimeErrorCode::InvalidArrayLength;
    case PortableObjectErrorCode::PrototypeDepthExceeded:
        return RuntimeErrorCode::PrototypeChainExceeded;
    case PortableObjectErrorCode::NameTooLong:
        return RuntimeErrorCode::PropertyNameTooLong;
    case PortableObjectErrorCode::ObjectCapacityExceeded:
        return RuntimeErrorCode::ObjectLimitExceeded;
    case PortableObjectErrorCode::PropertyCapacityExceeded:
    case PortableObjectErrorCode::NameStorageExhausted:
        return RuntimeErrorCode::PropertyLimitExceeded;
    case PortableObjectErrorCode::ArrayCapacityExceeded:
        return RuntimeErrorCode::ArrayLimitExceeded;
    }
    return RuntimeErrorCode::InvalidAstState;
}

bool RuntimeContext::initializePortableStorage(RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    if (limits_.maxEnvironments == 0u ||
        limits_.maxEnvironments > kRuntimeHandleInvalidPart ||
        limits_.maxTotalEnvironmentBindings >=
            static_cast<std::size_t>(kInvalidBindingIndex) ||
        limits_.maxBindings > 0xffffu ||
        limits_.maxFunctionEnvironmentBindings > 0xffffu ||
        limits_.maxBindingNameLength > 0xffffu) {
        error = RuntimeErrorCode::EnvironmentLimitExceeded;
        return false;
    }
    if (limits_.maxRuntimeStringValues > kRuntimeHandleInvalidPart) {
        error = RuntimeErrorCode::StringLimitExceeded;
        return false;
    }
    try {
        environmentRecordStorage_.resize(limits_.maxEnvironments);
        bindingStorage_.resize(limits_.maxTotalEnvironmentBindings);
        bindingNameStorage_.resize(limits_.maxEnvironmentBindingNameBytes);
        runtimeStringRecordStorage_.resize(limits_.maxRuntimeStringValues);
        runtimeStringStorage_.resize(limits_.maxTotalRuntimeStringBytes);
        objectRecordStorage_.resize(limits_.maxObjects);
        objectPropertyStorage_.resize(limits_.maxTotalProperties);
        arrayElementStorage_.resize(limits_.maxTotalArrayElements);
        propertyNameStorage_.resize(limits_.maxTotalPropertyKeyBytes);
        environments_.reserve(limits_.maxEnvironments - 1u);
        objectAdapters_.reserve(limits_.maxObjects);
    } catch (const std::bad_alloc&) {
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    } catch (const std::length_error&) {
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    }

    EnvironmentError environmentError;
    const EnvironmentStorageView environmentStorage{
        environmentRecordStorage_.data(), environmentRecordStorage_.size(),
        bindingStorage_.data(), bindingStorage_.size(),
        bindingNameStorage_.data(), bindingNameStorage_.size()};
    if (!environmentStore_.configure(environmentStorage, runtimeGeneration_,
            environmentError)) {
        error = environmentError.code == EnvironmentErrorCode::AllocationFailure
            ? RuntimeErrorCode::AllocationFailure
            : RuntimeErrorCode::InvalidAstState;
        return false;
    }
    RuntimeStringError stringError;
    const RuntimeStringStorageView stringStorage{
        runtimeStringRecordStorage_.data(),
        runtimeStringRecordStorage_.size(), runtimeStringStorage_.data(),
        runtimeStringStorage_.size()};
    if (!stringStore_.configure(stringStorage, runtimeGeneration_,
            stringError)) {
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    }
    const PortableObjectStorageView objectStorage{
        objectRecordStorage_.data(), objectRecordStorage_.size(),
        objectPropertyStorage_.data(), objectPropertyStorage_.size(),
        arrayElementStorage_.data(), arrayElementStorage_.size(),
        propertyNameStorage_.data(), propertyNameStorage_.size()};
    const PortableObjectLimits objectLimits{
        limits_.maxObjects,
        limits_.maxTotalProperties,
        limits_.maxPropertiesPerObject,
        limits_.maxPropertyNameLength,
        limits_.maxTotalPropertyKeyBytes,
        limits_.maxArrayElements < limits_.maxTotalArrayElements
            ? limits_.maxArrayElements : limits_.maxTotalArrayElements,
        limits_.maxTotalArrayElements,
        limits_.maxArrayIndex,
        maximumSupportedArrayLength(limits_),
        limits_.maxPrototypeDepth};
    PortableObjectError objectError;
    if (!objectStore_.configure(objectStorage, objectLimits,
            runtimeGeneration_, objectError)) {
        error = objectError.code == PortableObjectErrorCode::InvalidStorage
            ? RuntimeErrorCode::PortableCapacityExceeded
            : mapPortableObjectError(objectError.code);
        return false;
    }
    EnvironmentId global = kInvalidEnvironmentId;
    if (!environmentStore_.createRoot(
            EnvironmentLimits{limits_.maxBindings,
                limits_.maxBindingNameLength}, global, environmentError)) {
        error = environmentError.code ==
                EnvironmentErrorCode::CapacityExceeded
            ? RuntimeErrorCode::EnvironmentLimitExceeded
            : RuntimeErrorCode::InvalidAstState;
        return false;
    }
    globalEnvironmentId_ = global;
    environment_.bind(&environmentStore_, global,
        EnvironmentLimits{limits_.maxBindings,
            limits_.maxBindingNameLength});
    // The global record is part of the bounded environment pool too. Keep
    // diagnostics aligned with the store's public count before any function
    // or block scope is allocated.
    GXOS_JS84_DIAGNOSTIC(
        diagnostics_.environmentPoolHighWater = environmentCount());
    return true;
}

bool RuntimeContext::configurePortableExecutionStorage(
    RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    const std::size_t handleCapacity = static_cast<std::size_t>(
        kRuntimeHandleInvalidPart - functions_.size());
    const auto boundedAdd = [](std::size_t left, std::size_t right,
        std::size_t cap) {
        return left >= cap || right >= cap - left ? cap : left + right;
    };
    std::size_t functionCapacity = limits_.maxFunctions > userFunctionCount_
        ? limits_.maxFunctions - userFunctionCount_ : 0u;
    functionCapacity = boundedAdd(functionCapacity,
        limits_.maxNativeFunctions > nativeFunctionCount_
            ? limits_.maxNativeFunctions - nativeFunctionCount_ : 0u,
        handleCapacity);
    functionCapacity = boundedAdd(functionCapacity,
        limits_.maxHostMethodValues > hostMethodCount_
            ? limits_.maxHostMethodValues - hostMethodCount_ : 0u,
        handleCapacity);

    try {
        portableFunctionStorage_.resize(functionCapacity);
        portableCallFrameStorage_.resize(limits_.maxCallDepth);
        portableArgumentStorage_.resize(limits_.maxPortableArgumentValues);
    } catch (const std::bad_alloc&) {
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    } catch (const std::length_error&) {
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    }
    FunctionStoreError functionError;
    if (!portableFunctions_.configure(
            FunctionStorageView{portableFunctionStorage_.data(),
                portableFunctionStorage_.size()}, runtimeGeneration_,
            static_cast<std::uint32_t>(functions_.size()), functionError)) {
        error = functionError.code == FunctionStoreErrorCode::InvalidStorage ||
            functionError.code == FunctionStoreErrorCode::InvalidAlignment
            ? RuntimeErrorCode::InvalidAstState
            : RuntimeErrorCode::FunctionLimitExceeded;
        return false;
    }
    return true;
}

RuntimeErrorCode RuntimeContext::mapPortableExecutionError(
    PortableExecutionErrorCode code) const
{
    switch (code) {
    case PortableExecutionErrorCode::None: return RuntimeErrorCode::None;
    case PortableExecutionErrorCode::UnsupportedOperation:
        return RuntimeErrorCode::UnsupportedFeature;
    case PortableExecutionErrorCode::InvalidAst:
    case PortableExecutionErrorCode::InternalInvariantFailure:
        return RuntimeErrorCode::InvalidAstState;
    case PortableExecutionErrorCode::MissingBinding:
        return RuntimeErrorCode::UnknownIdentifier;
    case PortableExecutionErrorCode::UninitializedBinding:
        return RuntimeErrorCode::UninitializedBinding;
    case PortableExecutionErrorCode::DuplicateBinding:
        return RuntimeErrorCode::DuplicateBinding;
    case PortableExecutionErrorCode::ImmutableAssignment:
    case PortableExecutionErrorCode::TypeError:
        return RuntimeErrorCode::TypeError;
    case PortableExecutionErrorCode::InvalidAssignmentTarget:
        return RuntimeErrorCode::InvalidAssignmentTarget;
    case PortableExecutionErrorCode::InvalidInvocationTarget:
        return RuntimeErrorCode::NotCallable;
    case PortableExecutionErrorCode::InvalidFunctionReference:
    case PortableExecutionErrorCode::InvalidEnvironmentReference:
    case PortableExecutionErrorCode::StaleReference:
        return RuntimeErrorCode::InvalidFunction;
    case PortableExecutionErrorCode::EnvironmentCapacityExhausted:
        return RuntimeErrorCode::EnvironmentLimitExceeded;
    case PortableExecutionErrorCode::BindingCapacityExhausted:
        return RuntimeErrorCode::BindingLimitExceeded;
    case PortableExecutionErrorCode::FunctionCapacityExhausted:
        return RuntimeErrorCode::FunctionLimitExceeded;
    case PortableExecutionErrorCode::StringCapacityExhausted:
        return RuntimeErrorCode::StringLimitExceeded;
    case PortableExecutionErrorCode::ArgumentCapacityExhausted:
        return RuntimeErrorCode::PortableCapacityExceeded;
    case PortableExecutionErrorCode::StepBudgetExhausted:
        return RuntimeErrorCode::ExecutionBudgetExceeded;
    case PortableExecutionErrorCode::CallDepthExhausted:
        return RuntimeErrorCode::CallDepthExceeded;
    case PortableExecutionErrorCode::AllocationFailure:
        return RuntimeErrorCode::AllocationFailure;
    case PortableExecutionErrorCode::IllegalReturn:
        return RuntimeErrorCode::IllegalReturn;
    case PortableExecutionErrorCode::PropertyCapacityExhausted:
        return RuntimeErrorCode::PropertyLimitExceeded;
    case PortableExecutionErrorCode::ObjectCapacityExhausted:
        return RuntimeErrorCode::ObjectLimitExceeded;
    case PortableExecutionErrorCode::ArrayCapacityExhausted:
        return RuntimeErrorCode::ArrayLimitExceeded;
    case PortableExecutionErrorCode::InvalidObjectReference:
        return RuntimeErrorCode::NotObject;
    case PortableExecutionErrorCode::StaleObjectReference:
        return RuntimeErrorCode::StaleObjectReference;
    case PortableExecutionErrorCode::InvalidPropertyReference:
        return RuntimeErrorCode::InvalidPropertyKey;
    case PortableExecutionErrorCode::PropertyNameTooLong:
        return RuntimeErrorCode::PropertyNameTooLong;
    case PortableExecutionErrorCode::InvalidArrayIndex:
        return RuntimeErrorCode::ArrayIndexOutOfRange;
    case PortableExecutionErrorCode::InvalidArrayLength:
        return RuntimeErrorCode::InvalidArrayLength;
    case PortableExecutionErrorCode::PrototypeDepthExceeded:
        return RuntimeErrorCode::PrototypeChainExceeded;
    }
    return RuntimeErrorCode::InvalidAstState;
}

bool RuntimeContext::createString(SourceView text, Value& value,
    RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    if (text.data == nullptr && text.length != 0) {
        error = RuntimeErrorCode::InvalidAstState;
        return false;
    }
    if (text.length > limits_.maxRuntimeStringLength ||
        stringStore_.count() >= limits_.maxRuntimeStringValues ||
        text.length > limits_.maxTotalRuntimeStringBytes ||
        totalStringBytes_ > limits_.maxTotalRuntimeStringBytes - text.length ||
        stringStore_.count() >= kRuntimeHandleInvalidPart) {
        error = RuntimeErrorCode::StringLimitExceeded;
        return false;
    }
    RuntimeStringError stringError;
    RuntimeStringId id = kInvalidRuntimeStringId;
    if (!stringStore_.create(text, limits_.maxRuntimeStringLength, id,
            stringError)) {
        error = stringError.code == RuntimeStringErrorCode::CapacityExceeded
            ? RuntimeErrorCode::StringLimitExceeded
            : RuntimeErrorCode::AllocationFailure;
        return false;
    }
    totalStringBytes_ += text.length;
    value = Value::string(id);
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
    for (std::size_t index = 0; index < stringStore_.count(); ++index) {
        const RuntimeStringId id = makeRuntimeHandle(runtimeGeneration_,
            static_cast<std::uint32_t>(index));
        SourceView existing;
        RuntimeStringError stringError;
        if (!stringStore_.view(id, existing, stringError)) {
            error = RuntimeErrorCode::InvalidAstState;
            return false;
        }
        bool matches = existing.length == text.length;
        for (std::size_t offset = 0u; matches && offset < text.length;
            ++offset) {
            const char left = existing.data == nullptr
                ? '\0' : existing.data[offset];
            const char right = text.data == nullptr ? '\0' : text.data[offset];
            matches = left == right;
        }
        if (matches) {
            value = Value::string(id);
            return true;
        }
    }
    return createString(text, value, error);
}

bool RuntimeContext::createEnvironment(EnvironmentId parent,
    EnvironmentId& result, RuntimeErrorCode& error, EnvironmentKind kind)
{
    error = RuntimeErrorCode::None;
    if (environmentAt(parent) == nullptr) {
        error = RuntimeErrorCode::InvalidFunction;
        return false;
    }
    EnvironmentError environmentError;
    const EnvironmentLimits limits{limits_.maxFunctionEnvironmentBindings,
        limits_.maxBindingNameLength};
    EnvironmentId created = kInvalidEnvironmentId;
    if (!environmentStore_.create(parent, kind, limits, created,
            environmentError)) {
        GXOS_JS84_DIAGNOSTIC(++diagnostics_.failedEnvironmentAllocations);
        error = environmentError.code == EnvironmentErrorCode::CapacityExceeded
            ? RuntimeErrorCode::EnvironmentLimitExceeded
            : environmentError.code == EnvironmentErrorCode::AllocationFailure
                ? RuntimeErrorCode::AllocationFailure
                : RuntimeErrorCode::InvalidAstState;
        return false;
    }
    // Portable execution can create environment records without constructing
    // hosted facades. Materialize every record through the new handle so the
    // vector index continues to match the store's stable slot number.
    if (environmentAt(created) == nullptr) {
        error = RuntimeErrorCode::InvalidAstState;
        return false;
    }
    GXOS_JS84_DIAGNOSTIC({
        ++diagnostics_.environmentsAllocated;
        if (diagnostics_.environmentPoolHighWater < environmentCount())
            diagnostics_.environmentPoolHighWater = environmentCount();
    });
    result = created;
    return true;
}

Environment* RuntimeContext::environmentAt(EnvironmentId id)
{
    if (!runtimeHandleIsValid(id, runtimeGeneration_)) return nullptr;
    const std::size_t slot = runtimeHandleSlot(id);
    EnvironmentError error;
    if (!environmentStore_.valid(id, error)) return nullptr;
    if (slot == 0u) return &environment_;
    if (slot > environments_.capacity()) return nullptr;
    while (environments_.size() < slot) {
        const std::size_t nextSlot = environments_.size() + 1u;
        const EnvironmentId nextId = makeRuntimeHandle(runtimeGeneration_,
            static_cast<std::uint32_t>(nextSlot));
        const EnvironmentRecord* record = environmentStore_.record(nextId,
            error);
        if (record == nullptr) return nullptr;
        environments_.emplace_back(&environmentStore_, nextId,
            EnvironmentLimits{record->bindingLimit,
                record->nameLengthLimit});
    }
    return &environments_[slot - 1u];
}

const Environment* RuntimeContext::environmentAt(EnvironmentId id) const
{
    return const_cast<RuntimeContext*>(this)->environmentAt(id);
}

bool RuntimeContext::createFunction(AstNodeId declaration,
    EnvironmentId closure, RuntimeFunctionId& result, RuntimeErrorCode& error,
    bool arrowFunction, Value lexicalThis)
{
    error = RuntimeErrorCode::None;
    if (declaration == kInvalidAstNodeId || declaration >= ast_.nodeCount() ||
        (ast_.node(declaration).kind != AstNodeKind::FunctionDeclaration &&
            ast_.node(declaration).kind != AstNodeKind::FunctionExpression &&
            ast_.node(declaration).kind != AstNodeKind::ArrowFunctionExpression) ||
        environmentAt(closure) == nullptr) {
        error = RuntimeErrorCode::InvalidFunction;
        return false;
    }
    if (userFunctionCount_ >= limits_.maxFunctions ||
        functionValueCount() >= kRuntimeHandleInvalidPart) {
        error = RuntimeErrorCode::FunctionLimitExceeded;
        return false;
    }
    FunctionRecord function;
    function.kind = FunctionRecord::Kind::User;
    function.declaration = declaration;
    function.closureEnvironment = closure;
    function.arrowFunction = arrowFunction ||
        ast_.node(declaration).kind == AstNodeKind::ArrowFunctionExpression;
    if (function.arrowFunction) function.lexicalThis = lexicalThis;
    if (!appendFunctionRecord(function, result, error)) return false;
    ++userFunctionCount_;
    return true;
}

bool RuntimeContext::appendFunctionRecord(const FunctionRecord& function,
    RuntimeFunctionId& result, RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    result = kInvalidRuntimeFunctionId;
    if (functionValueCount() >= kRuntimeHandleInvalidPart) {
        error = RuntimeErrorCode::FunctionLimitExceeded;
        return false;
    }
    if (portableFunctions_.count() != 0u) {
        FunctionStoreError storeError;
        if (portableFunctions_.append(function, result, storeError)) return true;
        error = storeError.code == FunctionStoreErrorCode::CapacityExceeded
            ? RuntimeErrorCode::FunctionLimitExceeded
            : storeError.code == FunctionStoreErrorCode::StaleGeneration
                ? RuntimeErrorCode::InvalidFunction
                : RuntimeErrorCode::InvalidAstState;
        return false;
    }
    try {
        functions_.push_back(function);
    } catch (const std::bad_alloc&) {
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    }
    result = makeRuntimeHandle(runtimeGeneration_,
        static_cast<std::uint32_t>(functions_.size() - 1u));
    return result != kInvalidRuntimeFunctionId;
}

bool RuntimeContext::createNativeFunction(NativeFunctionId native,
    RuntimeFunctionId& result, RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    if (nativeFunctionCount_ >= limits_.maxNativeFunctions ||
        functionValueCount() >= kRuntimeHandleInvalidPart) {
        error = RuntimeErrorCode::NativeFunctionLimitExceeded;
        return false;
    }
    FunctionRecord function;
    function.kind = FunctionRecord::Kind::Native;
    function.nativeFunction = static_cast<std::uint8_t>(native);
    if (!appendFunctionRecord(function, result, error)) {
        if (error == RuntimeErrorCode::FunctionLimitExceeded)
            error = RuntimeErrorCode::NativeFunctionLimitExceeded;
        return false;
    }
    ++nativeFunctionCount_;
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
        functionValueCount() >= kRuntimeHandleInvalidPart) {
        error = RuntimeErrorCode::HostMethodLimitExceeded;
        return false;
    }
    FunctionRecord function;
    function.kind = FunctionRecord::Kind::Host;
    function.hostObject = cacheOwner;
    function.hostMethod = method;
    function.hostMethodRequiresReceiver = requiresReceiver;
    RuntimeFunctionId functionId = kInvalidRuntimeFunctionId;
    if (!appendFunctionRecord(function, functionId, error)) {
        if (error == RuntimeErrorCode::FunctionLimitExceeded)
            error = RuntimeErrorCode::HostMethodLimitExceeded;
        return false;
    }
    try {
        HostMethodRecord record;
        record.hostObject = cacheOwner;
        record.method = method;
        record.requiresReceiver = requiresReceiver;
        record.function = functionId;
        hostMethods_.push_back(record);
    } catch (const std::bad_alloc&) {
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    }
    ++hostMethodCount_;
    result = functionId;
    return true;
}

const RuntimeContext::FunctionRecord* RuntimeContext::functionAt(
    RuntimeFunctionId id) const
{
    if (!runtimeHandleIsValid(id, runtimeGeneration_)) {
        return nullptr;
    }
    const std::size_t slot = runtimeHandleSlot(id);
    if (slot < functions_.size()) return &functions_[slot];
    if (portableFunctions_.count() == 0u) return nullptr;
    FunctionStoreError error;
    return portableFunctions_.get(id, error);
}

bool RuntimeContext::createObject(bool array,
    const std::vector<Value>& initialElements, RuntimeObjectId& result,
    RuntimeErrorCode& error, RuntimeObjectId prototype)
{
    error = RuntimeErrorCode::None;
    result = kInvalidRuntimeObjectId;
    if (!array && !initialElements.empty()) {
        error = RuntimeErrorCode::InvalidAstState;
        return false;
    }
    if (array) {
        if (initialElements.size() > maximumSupportedArrayLength(limits_) ||
            initialElements.size() > limits_.maxTotalArrayElements ||
            objectStore_.arrayElementCount() >
                limits_.maxTotalArrayElements -
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
    if (prototype == kInvalidRuntimeObjectId && builtInsInitialized_)
        prototype = objectPrototype_;
    PortableObjectError objectError;
    if (!objectStore_.createObject(prototype, result, objectError)) {
        GXOS_JS84_DIAGNOSTIC({
            if (objectError.code ==
                PortableObjectErrorCode::ObjectCapacityExceeded)
                ++diagnostics_.failedObjectAllocations;
        });
        error = mapPortableObjectError(objectError.code);
        return false;
    }
    try {
        objectAdapters_.emplace_back();
    } catch (const std::bad_alloc&) {
        PortableObjectError rollbackError;
        (void)objectStore_.rollbackObjects(objectStore_.objectCount() - 1u,
            rollbackError);
        result = kInvalidRuntimeObjectId;
        GXOS_JS84_DIAGNOSTIC({
            ++diagnostics_.failedObjectAllocations;
        });
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    }
    GXOS_JS84_DIAGNOSTIC({
        ++diagnostics_.runtimeObjectsAllocated;
        if (diagnostics_.objectPoolHighWater < objectStore_.objectCount())
            diagnostics_.objectPoolHighWater = objectStore_.objectCount();
    });
    return true;
}

bool RuntimeContext::createArray(std::size_t logicalLength,
    std::vector<RuntimeArrayElement>&& initialElements,
    RuntimeObjectId& result, RuntimeErrorCode& error,
    RuntimeObjectId prototype)
{
    error = RuntimeErrorCode::None;
    result = kInvalidRuntimeObjectId;
    if (logicalLength > maximumSupportedArrayLength(limits_) ||
        initialElements.size() > limits_.maxArrayElements ||
        initialElements.size() > limits_.maxTotalArrayElements ||
        objectStore_.arrayElementCount() >
            limits_.maxTotalArrayElements -
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
    if (prototype == kInvalidRuntimeObjectId && builtInsInitialized_)
        prototype = arrayPrototype_;
    PortableObjectError objectError;
    if (!objectStore_.createArray(logicalLength, prototype, result,
            objectError)) {
        GXOS_JS84_DIAGNOSTIC({
            if (objectError.code ==
                PortableObjectErrorCode::ObjectCapacityExceeded)
                ++diagnostics_.failedObjectAllocations;
            if (objectError.code ==
                PortableObjectErrorCode::ArrayCapacityExceeded)
                ++diagnostics_.failedArraySlotAllocations;
        });
        error = mapPortableObjectError(objectError.code);
        return false;
    }
    for (const RuntimeArrayElement& element : initialElements) {
        if (!objectStore_.setArrayElement(result, element.index,
                element.value, objectError, element.attributes.bits)) {
            const RuntimeErrorCode mapped =
                mapPortableObjectError(objectError.code);
            PortableObjectError rollbackError;
            (void)objectStore_.rollbackObjects(
                objectStore_.objectCount() - 1u, rollbackError);
            result = kInvalidRuntimeObjectId;
            GXOS_JS84_DIAGNOSTIC({
                ++diagnostics_.failedArraySlotAllocations;
            });
            error = mapped;
            return false;
        }
    }
    try {
        objectAdapters_.emplace_back();
    } catch (const std::bad_alloc&) {
        PortableObjectError rollbackError;
        (void)objectStore_.rollbackObjects(objectStore_.objectCount() - 1u,
            rollbackError);
        result = kInvalidRuntimeObjectId;
        GXOS_JS84_DIAGNOSTIC({
            ++diagnostics_.failedObjectAllocations;
            ++diagnostics_.failedArraySlotAllocations;
        });
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    }
    GXOS_JS84_DIAGNOSTIC({
        ++diagnostics_.runtimeObjectsAllocated;
        if (diagnostics_.objectPoolHighWater < objectStore_.objectCount())
            diagnostics_.objectPoolHighWater = objectStore_.objectCount();
    });
    GXOS_JS84_DIAGNOSTIC({
        diagnostics_.arraySlotsAllocated += initialElements.size();
        if (diagnostics_.arraySlotHighWater <
            objectStore_.arrayElementCount())
            diagnostics_.arraySlotHighWater =
                objectStore_.arrayElementCount();
    });
    return true;
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
        if (objectAt(current) == nullptr) {
            error = RuntimeErrorCode::CannotReadProperty;
            return false;
        }
        const std::string key = std::to_string(index);
        bool found = false;
        PortableObjectError objectError;
        if (!objectStore_.getOwnProperty(current,
                SourceView(key.data(), key.size()), value, found,
                objectError)) {
            error = mapPortableObjectError(objectError.code);
            return false;
        }
        if (found) return true;
        RuntimeObjectId prototype = kInvalidRuntimeObjectId;
        if (!objectStore_.prototypeOf(current, prototype, objectError)) {
            error = mapPortableObjectError(objectError.code);
            return false;
        }
        if (prototype == kInvalidRuntimeObjectId) {
            value = Value::undefined();
            return true;
        }
        current = prototype;
    }
    error = RuntimeErrorCode::PrototypeChainExceeded;
    return false;
}

bool RuntimeContext::setArrayLength(RuntimeObjectId objectId,
    const Value& value, RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    bool isArray = false;
    if (!isArrayObject(objectId, isArray) || !isArray) {
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
    PortableObjectError objectError;
    if (!objectStore_.setArrayLength(objectId, newLength, objectError)) {
        error = mapPortableObjectError(objectError.code);
        return false;
    }
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
        !createNativeFunction(NativeFunctionId::JsonParse,
            jsonParseFunction_, error) ||
        !createNativeFunction(NativeFunctionId::JsonStringify,
            jsonStringifyFunction_, error) ||
        !install(arrayPrototype_, "forEach", NativeFunctionId::ArrayForEach,
            error) ||
        !install(arrayPrototype_, "map", NativeFunctionId::ArrayMap,
            error) ||
        !install(arrayPrototype_, "filter", NativeFunctionId::ArrayFilter,
            error) ||
        !install(arrayPrototype_, "some", NativeFunctionId::ArraySome,
            error) ||
        !install(arrayPrototype_, "every", NativeFunctionId::ArrayEvery,
            error) ||
        !install(arrayPrototype_, "find", NativeFunctionId::ArrayFind,
            error) ||
        !install(arrayPrototype_, "findIndex", NativeFunctionId::ArrayFindIndex,
            error) ||
        !install(arrayPrototype_, "findLast", NativeFunctionId::ArrayFindLast,
            error) ||
        !install(arrayPrototype_, "reduce", NativeFunctionId::ArrayReduce,
            error) ||
        !install(arrayPrototype_, "reduceRight",
            NativeFunctionId::ArrayReduceRight,
            error)) return false;
    const char eventName[] = "Event";
    // Event constants are optional only when an explicitly tiny caller limit
    // cannot fit the bounded namespace. This keeps the older runtime limit
    // contracts meaningful while the default realm exposes Event normally.
    const std::size_t eventConstantKeyBytes =
        sizeof("NONE") - 1u + sizeof("CAPTURING_PHASE") - 1u +
        sizeof("AT_TARGET") - 1u + sizeof("BUBBLING_PHASE") - 1u;
    const bool canInstallEventConstants =
        objectStore_.objectCount() < limits_.maxObjects &&
        limits_.maxPropertiesPerObject >= 4u &&
        objectStore_.propertyCount() <= limits_.maxTotalProperties &&
        limits_.maxTotalProperties - objectStore_.propertyCount() >= 4u &&
        limits_.maxPropertyNameLength >= sizeof("CAPTURING_PHASE") - 1u &&
        objectStore_.propertyNameBytesUsed() <=
            limits_.maxTotalPropertyKeyBytes &&
        limits_.maxTotalPropertyKeyBytes -
            objectStore_.propertyNameBytesUsed() >= eventConstantKeyBytes &&
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
    PortableObjectError objectError;
    if (!objectStore_.validObject(id, objectError)) return nullptr;
    const std::size_t slot = runtimeHandleSlot(id);
    return slot < objectAdapters_.size() ? &objectAdapters_[slot] : nullptr;
}

const RuntimeContext::RuntimeObject* RuntimeContext::objectAt(
    RuntimeObjectId id) const
{
    PortableObjectError objectError;
    if (!objectStore_.validObject(id, objectError)) return nullptr;
    const std::size_t slot = runtimeHandleSlot(id);
    return slot < objectAdapters_.size() ? &objectAdapters_[slot] : nullptr;
}

RuntimeObjectId RuntimeContext::prototypeOf(RuntimeObjectId object) const
{
    RuntimeObjectId result = kInvalidRuntimeObjectId;
    PortableObjectError objectError;
    return objectStore_.prototypeOf(object, result, objectError)
        ? result : kInvalidRuntimeObjectId;
}

bool RuntimeContext::setPrototypeForTesting(RuntimeObjectId object,
    RuntimeObjectId prototype)
{
    PortableObjectError objectError;
    if (objectStore_.setPrototype(object, prototype, objectError)) return true;
    if (objectError.code != PortableObjectErrorCode::PrototypeCycle ||
        !runtimeHandleIsValid(object, objectStore_.generation()) ||
        !runtimeHandleIsValid(prototype, objectStore_.generation())) return false;
    const std::size_t slot = runtimeHandleSlot(object);
    if (slot >= objectRecordStorage_.size() ||
        slot >= objectStore_.objectCount()) return false;
    objectRecordStorage_[slot].prototype = prototype;
    return true;
}

bool RuntimeContext::rollbackObjects(std::size_t retainedCount,
    RuntimeErrorCode& error)
{
    PortableObjectError objectError;
    if (!objectStore_.rollbackObjects(retainedCount, objectError)) {
        error = mapPortableObjectError(objectError.code);
        return false;
    }
    if (objectAdapters_.size() > retainedCount)
        objectAdapters_.resize(retainedCount);
    return true;
}

bool RuntimeContext::isArrayObject(RuntimeObjectId object,
    bool& result) const
{
    PortableObjectError objectError;
    return objectStore_.isArray(object, result, objectError);
}

bool RuntimeContext::getArrayLength(RuntimeObjectId object,
    std::size_t& result) const
{
    PortableObjectError objectError;
    return objectStore_.arrayLength(object, result, objectError);
}

bool RuntimeContext::getOwnPropertyEntry(RuntimeObjectId object,
    const PropertyKey& key, PortablePropertyEntry& result, bool& found,
    RuntimeErrorCode& error) const
{
    PortableObjectError objectError;
    const bool success = key.isSymbol()
        ? objectStore_.getOwnPropertyEntry(object, key.symbol, result, found,
            objectError)
        : objectStore_.getOwnPropertyEntry(object,
            SourceView(key.string.data(), key.string.size()), result, found,
            objectError);
    error = success ? RuntimeErrorCode::None
        : mapPortableObjectError(objectError.code);
    if (success && found && result.isArrayLength)
        result.attributes = PropertyAttributes::arrayLength().bits;
    return success;
}

bool RuntimeContext::hasOwnStoredProperty(RuntimeObjectId object,
    const PropertyKey& key, bool& found, RuntimeErrorCode& error) const
{
    PortableObjectError objectError;
    const bool success = key.isSymbol()
        ? objectStore_.hasOwnProperty(object, key.symbol, found, objectError)
        : objectStore_.hasOwnProperty(object,
            SourceView(key.string.data(), key.string.size()), found,
            objectError);
    error = success ? RuntimeErrorCode::None
        : mapPortableObjectError(objectError.code);
    return success;
}

bool RuntimeContext::snapshotOwnProperties(RuntimeObjectId object,
    std::vector<RuntimeProperty>& result, RuntimeErrorCode& error) const
{
    result.clear();
    error = RuntimeErrorCode::None;
    const std::size_t maximumEntries = limits_.maxTotalProperties +
        limits_.maxTotalArrayElements + 1u;
    try {
        result.reserve(limits_.maxPropertiesPerObject);
        for (std::size_t ordinal = 0u; ordinal < maximumEntries; ++ordinal) {
            PortablePropertyEntry entry;
            bool found = false;
            PortableObjectError objectError;
            if (!objectStore_.getOwnPropertyAt(object, ordinal, entry, found,
                    objectError)) {
                error = mapPortableObjectError(objectError.code);
                return false;
            }
            if (!found) return true;
            if (entry.isArrayElement || entry.isArrayLength) continue;
            RuntimeProperty property;
            if (entry.key.data != nullptr && entry.key.length != 0u)
                property.key.assign(entry.key.data, entry.key.length);
            property.symbolKey = entry.symbol;
            property.value = entry.value;
            property.attributes.bits = entry.attributes;
            result.push_back(std::move(property));
        }
    } catch (const std::bad_alloc&) {
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    } catch (const std::length_error&) {
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    }
    error = RuntimeErrorCode::InvalidAstState;
    return false;
}

bool RuntimeContext::snapshotArrayElements(RuntimeObjectId object,
    std::vector<RuntimeArrayElement>& result, RuntimeErrorCode& error) const
{
    result.clear();
    error = RuntimeErrorCode::None;
    try {
        result.reserve(limits_.maxArrayElements);
        for (std::size_t ordinal = 0u;
            ordinal <= limits_.maxTotalArrayElements; ++ordinal) {
            PortableArrayEntry entry;
            bool found = false;
            PortableObjectError objectError;
            if (!objectStore_.getArrayElementAt(object, ordinal, entry,
                    found, objectError)) {
                error = mapPortableObjectError(objectError.code);
                return false;
            }
            if (!found) return true;
            RuntimeArrayElement element(entry.index, entry.value);
            element.attributes.bits = entry.attributes;
            result.push_back(element);
        }
    } catch (const std::bad_alloc&) {
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    } catch (const std::length_error&) {
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    }
    error = RuntimeErrorCode::InvalidAstState;
    return false;
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
    const std::size_t objectCountBefore = objectStore_.objectCount();
    const std::vector<Value> noElements;
    RuntimeObjectId object = kInvalidRuntimeObjectId;
    if (!createObject(false, noElements, object, error)) return false;
    GXOS_JS84_DIAGNOSTIC(++diagnostics_.iteratorResultObjectsAllocated);

    const auto rollback = [this, objectCountBefore]() {
        GXOS_JS84_DIAGNOSTIC(++diagnostics_.collectionResultRollbacks);
        RuntimeErrorCode ignored = RuntimeErrorCode::None;
        (void)rollbackObjects(objectCountBefore, ignored);
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
    std::vector<RuntimeProperty> resultProperties;
    if (!snapshotOwnProperties(object, resultProperties, error) ||
        resultProperties.size() != 2u ||
        resultProperties[0].symbolKey != WellKnownSymbol::None ||
        resultProperties[0].key != "value" ||
        resultProperties[1].symbolKey != WellKnownSymbol::None ||
        resultProperties[1].key != "done") {
        error = RuntimeErrorCode::InvalidAstState;
        rollback();
        return false;
    }
    if (!updateExistingProperty(object, "value", convertedValue, error)) {
        rollback();
        return false;
    }
    result = Value::object(object);
    GXOS_JS84_DIAGNOSTIC(++diagnostics_.iteratorResultObjectsPublished);
    return true;
}

bool RuntimeContext::createArrayIteratorResult(const Value& value, bool done,
    Value& result, RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    result = Value::undefined();
    const std::size_t objectCountBefore = objectStore_.objectCount();
    const std::vector<Value> noElements;
    RuntimeObjectId object = kInvalidRuntimeObjectId;
    if (!createObject(false, noElements, object, error)) return false;
    GXOS_JS84_DIAGNOSTIC(++diagnostics_.iteratorResultObjectsAllocated);

    const auto rollback = [this, objectCountBefore]() {
        GXOS_JS84_DIAGNOSTIC(++diagnostics_.arrayIteratorResultRollbacks);
        RuntimeErrorCode ignored = RuntimeErrorCode::None;
        (void)rollbackObjects(objectCountBefore, ignored);
    };

    if (!writeProperty(object, "value", Value::undefined(), error) ||
        !writeProperty(object, "done", Value::boolean(done), error)) {
        rollback();
        return false;
    }
    std::vector<RuntimeProperty> resultProperties;
    if (!snapshotOwnProperties(object, resultProperties, error) ||
        resultProperties.size() != 2u ||
        resultProperties[0].symbolKey != WellKnownSymbol::None ||
        resultProperties[0].key != "value" ||
        resultProperties[1].symbolKey != WellKnownSymbol::None ||
        resultProperties[1].key != "done") {
        error = RuntimeErrorCode::InvalidAstState;
        rollback();
        return false;
    }
    if (!updateExistingProperty(object, "value", value, error)) {
        rollback();
        return false;
    }
    result = Value::object(object);
    GXOS_JS84_DIAGNOSTIC(++diagnostics_.iteratorResultObjectsPublished);
    return true;
}

bool RuntimeContext::createArrayIteratorEntryResult(std::size_t index,
    const Value& value, Value& result, RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    result = Value::undefined();
    const std::size_t objectCountBefore = objectStore_.objectCount();

    const auto rollback = [this, objectCountBefore]() {
        GXOS_JS84_DIAGNOSTIC(++diagnostics_.arrayIteratorEntryRollbacks);
        RuntimeErrorCode ignored = RuntimeErrorCode::None;
        (void)rollbackObjects(objectCountBefore, ignored);
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

    const std::size_t objectCountBefore = objectStore_.objectCount();
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

    const auto rollback = [this, objectCountBefore, hostObjectCountBefore,
        liveHostObjectCountBefore, reusableHostObjectSlot,
        reusableHostObjectBefore, elementAlreadyRegistered]() {
        GXOS_JS84_DIAGNOSTIC(++diagnostics_.collectionEntryRollbacks);
        RuntimeErrorCode ignored = RuntimeErrorCode::None;
        (void)rollbackObjects(objectCountBefore, ignored);
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
        PortablePropertyEntry entry;
        bool found = false;
        if (!getOwnPropertyEntry(current, key, entry, found, error)) {
            if (error == RuntimeErrorCode::InvalidAstState)
                error = RuntimeErrorCode::CannotReadProperty;
            return false;
        }
        if (found) {
            value = entry.value;
            return true;
        }
        RuntimeObjectId prototype = kInvalidRuntimeObjectId;
        PortableObjectError objectError;
        if (!objectStore_.prototypeOf(current, prototype, objectError)) {
            error = mapPortableObjectError(objectError.code);
            if (error == RuntimeErrorCode::InvalidAstState)
                error = RuntimeErrorCode::CannotReadProperty;
            return false;
        }
        if (prototype == kInvalidRuntimeObjectId) {
            value = Value::undefined();
            return true;
        }
        current = prototype;
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
    if (objectAt(objectId) == nullptr) {
        error = RuntimeErrorCode::CannotWriteProperty;
        return false;
    }
    if (!key.isSymbol() && key.string.size() > limits_.maxPropertyNameLength) {
        error = RuntimeErrorCode::PropertyNameTooLong;
        return false;
    }
    bool isArray = false;
    if (!isArrayObject(objectId, isArray)) {
        error = RuntimeErrorCode::CannotWriteProperty;
        return false;
    }
    if (!key.isSymbol() && isArray && key.string == "length") {
        return setArrayLength(objectId, value, error);
    }
    std::uint32_t standardIndex = 0u;
    const bool isArrayIndex = !key.isSymbol() && isArray &&
        parseEcmaArrayIndex(key.string, standardIndex);
    if (isArrayIndex) {
        if (attributes.bits != PropertyAttributes::ordinaryData().bits) {
            error = RuntimeErrorCode::InvalidAstState;
            return false;
        }
        const std::size_t index = static_cast<std::size_t>(standardIndex);
        if (index > limits_.maxArrayIndex) {
            error = RuntimeErrorCode::ArrayIndexOutOfRange;
            return false;
        }
        if (index >= maximumSupportedArrayLength(limits_)) {
            error = RuntimeErrorCode::ArrayLimitExceeded;
            return false;
        }
    }
    PortablePropertyEntry existingEntry;
    bool alreadyPresent = false;
    if (!getOwnPropertyEntry(objectId, key, existingEntry, alreadyPresent,
            error)) return false;
    if (isArrayIndex && !alreadyPresent) {
        const std::size_t countBefore = objectStore_.arrayElementCount();
        (void)countBefore;
#if defined(GXOS_JAVASCRIPT_TEST_FAILURE_INJECTION)
        if (failNextArraySlotInsertionForTesting_) {
            failNextArraySlotInsertionForTesting_ = false;
            GXOS_JS84_DIAGNOSTIC(++diagnostics_.failedArraySlotAllocations);
            error = RuntimeErrorCode::AllocationFailure;
            return false;
        }
        if (failArraySlotInsertionAfterCountForTesting_) {
            if (arraySlotInsertionsBeforeFailureForTesting_ == 0u) {
                failArraySlotInsertionAfterCountForTesting_ = false;
                GXOS_JS84_DIAGNOSTIC(++diagnostics_.failedArraySlotAllocations);
                error = RuntimeErrorCode::AllocationFailure;
                return false;
            }
            --arraySlotInsertionsBeforeFailureForTesting_;
        }
#endif
        PortableObjectError objectError;
        if (!objectStore_.setOwnProperty(objectId,
                SourceView(key.string.data(), key.string.size()), value,
                objectError, attributes.bits)) {
            GXOS_JS84_DIAGNOSTIC({
                if (objectError.code ==
                    PortableObjectErrorCode::ArrayCapacityExceeded)
                    ++diagnostics_.failedArraySlotAllocations;
            });
            error = mapPortableObjectError(objectError.code);
            return false;
        }
        GXOS_JS84_DIAGNOSTIC({
            if (objectStore_.arrayElementCount() > countBefore) {
                ++diagnostics_.arraySlotsAllocated;
                if (diagnostics_.arraySlotHighWater <
                    objectStore_.arrayElementCount())
                    diagnostics_.arraySlotHighWater =
                        objectStore_.arrayElementCount();
            }
        });
        return true;
    }
    const std::size_t propertiesBefore = objectStore_.propertyCount();
    (void)propertiesBefore;
#if defined(GXOS_JAVASCRIPT_TEST_FAILURE_INJECTION)
    if (!alreadyPresent && !isArrayIndex) {
        if (failNextPropertyInsertionForTesting_) {
            failNextPropertyInsertionForTesting_ = false;
            GXOS_JS84_DIAGNOSTIC(++diagnostics_.failedPropertyAllocations);
            error = RuntimeErrorCode::AllocationFailure;
            return false;
        }
        if (failPropertyInsertionAfterCountForTesting_) {
            if (propertyInsertionsBeforeFailureForTesting_ == 0u) {
                failPropertyInsertionAfterCountForTesting_ = false;
                GXOS_JS84_DIAGNOSTIC(++diagnostics_.failedPropertyAllocations);
                error = RuntimeErrorCode::AllocationFailure;
                return false;
            }
            --propertyInsertionsBeforeFailureForTesting_;
        }
    }
#endif
    PortableObjectError objectError;
    const bool stored = key.isSymbol()
        ? objectStore_.setOwnProperty(objectId, key.symbol, value,
            objectError, attributes.bits)
        : objectStore_.setOwnProperty(objectId,
            SourceView(key.string.data(), key.string.size()), value,
            objectError, attributes.bits);
    if (!stored) {
        GXOS_JS84_DIAGNOSTIC({
            if (objectError.code ==
                PortableObjectErrorCode::PropertyCapacityExceeded ||
                objectError.code ==
                PortableObjectErrorCode::NameStorageExhausted)
                ++diagnostics_.failedPropertyAllocations;
        });
        error = mapPortableObjectError(objectError.code);
        return false;
    }
    GXOS_JS84_DIAGNOSTIC({
        if (objectStore_.propertyCount() > propertiesBefore) {
            ++diagnostics_.runtimePropertiesAllocated;
            if (diagnostics_.propertyPoolHighWater <
                objectStore_.propertyCount())
                diagnostics_.propertyPoolHighWater =
                    objectStore_.propertyCount();
        }
    });
    return true;
}



bool RuntimeContext::deleteOwnProperty(RuntimeObjectId objectId,
    const std::string& key, bool& deleted, RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    deleted = false;
    if (objectAt(objectId) == nullptr) {
        error = RuntimeErrorCode::CannotWriteProperty;
        return false;
    }
    PropertyKey propertyKey;
    propertyKey.string = key;
    bool isArray = false;
    if (!isArrayObject(objectId, isArray)) {
        error = RuntimeErrorCode::CannotWriteProperty;
        return false;
    }
    if (isArray && key == "length") return true;
    PortablePropertyEntry entry;
    bool found = false;
    if (!getOwnPropertyEntry(objectId, propertyKey, entry, found, error))
        return false;
    if (!found) return true;
    PropertyAttributes attributes;
    attributes.bits = entry.attributes;
    if (!attributes.isValid() || !attributes.hasConfigurable()) {
        error = RuntimeErrorCode::InvalidAstState;
        return false;
    }
    if (!attributes.configurable()) return true;
    PortableObjectError objectError;
    if (!objectStore_.deleteOwnProperty(objectId,
            SourceView(key.data(), key.size()), deleted, objectError)) {
        error = mapPortableObjectError(objectError.code);
        return false;
    }
    return true;
}

bool RuntimeContext::updateExistingProperty(RuntimeObjectId objectId,
    const std::string& key, Value value, RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    if (objectAt(objectId) == nullptr) {
        error = RuntimeErrorCode::CannotWriteProperty;
        return false;
    }
    PropertyKey propertyKey;
    propertyKey.string = key;
    PortablePropertyEntry entry;
    bool found = false;
    if (!getOwnPropertyEntry(objectId, propertyKey, entry, found, error))
        return false;
    if (!found) {
        error = RuntimeErrorCode::CannotWriteProperty;
        return false;
    }
    PortableObjectError objectError;
    if (!objectStore_.updateOwnProperty(objectId,
            SourceView(key.data(), key.size()), value, entry.attributes,
            objectError)) {
        error = mapPortableObjectError(objectError.code);
        return false;
    }
    return true;
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
    case HostResultCode::HostObjectLimitExceeded:
        return RuntimeErrorCode::HostObjectLimitExceeded;
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
        case EnvironmentErrorCode::CapacityExceeded:
            error = RuntimeErrorCode::BindingLimitExceeded;
            break;
        case EnvironmentErrorCode::BindingNameTooLong:
            error = RuntimeErrorCode::BindingNameTooLong;
            break;
        case EnvironmentErrorCode::AllocationFailure:
            error = RuntimeErrorCode::AllocationFailure;
            break;
        case EnvironmentErrorCode::DuplicateLexicalBinding:
            error = RuntimeErrorCode::InvalidAstState;
            break;
        case EnvironmentErrorCode::MissingBinding:
            error = RuntimeErrorCode::UnknownIdentifier;
            break;
        case EnvironmentErrorCode::UninitializedBinding:
            error = RuntimeErrorCode::UninitializedBinding;
            break;
        case EnvironmentErrorCode::ImmutableAssignment:
            error = RuntimeErrorCode::TypeError;
            break;
        case EnvironmentErrorCode::BindingAlreadyInitialized:
            error = RuntimeErrorCode::InvalidAstState;
            break;
        case EnvironmentErrorCode::None:
        case EnvironmentErrorCode::InvalidStorage:
        case EnvironmentErrorCode::InvalidAlignment:
        case EnvironmentErrorCode::InvalidInput:
        case EnvironmentErrorCode::InvalidReference:
        case EnvironmentErrorCode::InvalidEnvironment:
        case EnvironmentErrorCode::StaleGeneration:
        case EnvironmentErrorCode::SizeOverflow:
        case EnvironmentErrorCode::ParentCycle:
        case EnvironmentErrorCode::InternalInvariantFailure:
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
        SourceView text;
        RuntimeErrorCode stringError = RuntimeErrorCode::None;
        if (!stringView(value, text, stringError)) {
            error = RuntimeErrorCode::HostCallFailed;
            return false;
        }
        result = HostValue::string(text);
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
    case ValueType::JsonNamespace:
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

bool RuntimeContext::stringView(const Value& value, SourceView& view,
    RuntimeErrorCode& error) const
{
    view = SourceView();
    error = RuntimeErrorCode::None;
    if (!value.isString()) {
        error = RuntimeErrorCode::InvalidAstState;
        return false;
    }
    RuntimeStringError stringError;
    if (!stringStore_.view(value.stringId(), view, stringError)) {
        error = RuntimeErrorCode::InvalidAstState;
        return false;
    }
    return true;
}

std::string RuntimeContext::stringValue(const Value& value) const
{
    SourceView view;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    if (!stringView(value, view, error)) return std::string();
    try {
        return std::string(view.data == nullptr ? "" : view.data,
            view.length);
    } catch (const std::bad_alloc&) {
        return std::string();
    }
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

        if (PortableExecutionCore::supports(ast_, ast_.root(),
                limits_.parser.maxCallArguments,
                limits_.parser.maxExpressionNesting)) {
            RuntimeErrorCode storageError = RuntimeErrorCode::None;
            if (configurePortableExecutionStorage(storageError)) {
                const PortableExecutionStorageView executionStorage{
                    portableCallFrameStorage_.data(),
                    portableCallFrameStorage_.size(),
                    portableArgumentStorage_.data(),
                    portableArgumentStorage_.size()};
                const PortableExecutionLimits executionLimits{
                    limits_.maxCallDepth,
                    limits_.parser.maxCallArguments,
                    limits_.maxFunctions > userFunctionCount_
                        ? limits_.maxFunctions - userFunctionCount_ : 0u,
                    limits_.maxExecutionSteps,
                    limits_.maxFunctionEnvironmentBindings,
                    limits_.maxBindingNameLength,
                    limits_.maxRuntimeStringLength,
                    limits_.parser.maxExpressionNesting};
                PortableExecutionCore portable(ast_, stringStore_,
                    environmentStore_, portableFunctions_,
                    executionStorage, executionLimits, executionSteps_);
                PortableExecutionResult portableResult;
                const bool succeeded = portable.run(ast_.root(),
                    globalEnvironmentId_, finalValue_, portableResult);
                userFunctionCount_ += portableResult.functionsCreated;
                totalStringBytes_ = stringStore_.bytesUsed();
                result_.executionSteps = portableResult.executionSteps;
                if (!succeeded) {
                    setRuntimeError(mapPortableExecutionError(
                        portableResult.error.code),
                        portableResult.error.location);
                    return result_;
                }
                result_.status = ScriptStatus::Success;
                return result_;
            }
            // The portable executor is an optional backend for this bounded
            // syntax slice. If its scratch cannot be provisioned, preserve
            // the established hosted path and its accepted functionality.
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
        // Preserve the source backing any AST installed by execute() before
        // sourceStorage_ is replaced with this same-realm script.  Existing
        // function records and closures continue to refer to those AST nodes,
        // so their identifier spans must remain addressable in the cumulative
        // realm source buffer.
        if (realmSourceStorage_.empty() && ast_.nodeCount() != 0u) {
            realmSourceStorage_ = sourceStorage_;
            realmSourceLineCount_ = static_cast<std::size_t>(std::count(
                realmSourceStorage_.begin(), realmSourceStorage_.end(), '\n'));
            ast_.setSource(SourceView(realmSourceStorage_.data(),
                realmSourceStorage_.size()));
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
    case RuntimeErrorCode::UninitializedBinding:
        return "UninitializedBinding";
    case RuntimeErrorCode::DuplicateBinding: return "DuplicateBinding";
    case RuntimeErrorCode::SyntaxError: return "SyntaxError";
    case RuntimeErrorCode::JsonNestingLimitExceeded:
        return "JsonNestingLimitExceeded";
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
    case RuntimeErrorCode::TooManyArguments:
        return "TooManyArguments";
    case RuntimeErrorCode::PortableCapacityExceeded:
        return "PortableCapacityExceeded";
    case RuntimeErrorCode::StaleObjectReference:
        return "StaleObjectReference";
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
