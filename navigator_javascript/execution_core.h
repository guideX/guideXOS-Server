#pragma once

#include "ast.h"
#include "environment.h"
#include "function_storage.h"
#include "object_storage.h"
#include "runtime_string_storage.h"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace gxos {
namespace javascript {

enum class PortableExecutionErrorCode : std::uint8_t {
    None = 0,
    UnsupportedOperation,
    InvalidAst,
    MissingBinding,
    UninitializedBinding,
    DuplicateBinding,
    ImmutableAssignment,
    InvalidAssignmentTarget,
    TypeError,
    InvalidInvocationTarget,
    InvalidFunctionReference,
    InvalidEnvironmentReference,
    StaleReference,
    RealmMismatch,
    EnvironmentCapacityExhausted,
    BindingCapacityExhausted,
    FunctionCapacityExhausted,
    StringCapacityExhausted,
    ArgumentCapacityExhausted,
    StepBudgetExhausted,
    CallDepthExhausted,
    AllocationFailure,
    IllegalReturn,
    ObjectCapacityExhausted,
    PropertyCapacityExhausted,
    ArrayCapacityExhausted,
    InvalidObjectReference,
    StaleObjectReference,
    InvalidPropertyReference,
    PropertyNameTooLong,
    InvalidArrayIndex,
    InvalidArrayLength,
    PrototypeDepthExceeded,
    InternalInvariantFailure,
    ExecutionFrameCapacityExhausted,
};

struct PortableExecutionError {
    PortableExecutionErrorCode code = PortableExecutionErrorCode::None;
    SourceLocation location;
};

struct PortableExecutionResult {
    bool completed = false;
    PortableExecutionError error;
    Value finalValue = Value::undefined();
    std::size_t executionSteps = 0u;
    std::size_t environmentsCreated = 0u;
    std::size_t functionsCreated = 0u;
    std::size_t argumentValuesHighWater = 0u;
    std::size_t callDepthHighWater = 0u;
    std::size_t tailCallsExecuted = 0u;
    std::size_t executionFrameHighWater = 0u;
};

constexpr std::size_t kDefaultPortableExecutionFrameCapacity = 4096u;

struct PortableExecutionLimits {
    std::size_t maxCallDepth = 64u;
    std::size_t maxCallArguments = 64u;
    std::size_t maxFunctions = 4096u;
    std::size_t maxExecutionSteps = 100000u;
    std::size_t maxFunctionEnvironmentBindings = 256u;
    std::size_t maxBindingNameLength = 256u;
    std::size_t maxRuntimeStringLength = 64u * 1024u;
    std::size_t maxAstDepth = 256u;
    std::size_t maxExecutionFrames = kDefaultPortableExecutionFrameCapacity;
};

struct PortableCallFrame {
    RuntimeFunctionId function = kInvalidRuntimeFunctionId;
    EnvironmentId environment = kInvalidEnvironmentId;
    EnvironmentId previousEnvironment = kInvalidEnvironmentId;
    Value previousThis = Value::undefined();
    SourceLocation callSite;
    std::size_t argumentMark = 0u;
    // For the qualified non-tail self-recursion shape, the child frame owns
    // the caller's pending AST operation until the child returns.
    AstNodeId continuationNode = kInvalidAstNodeId;
};

enum class PortableExecutionFrameKind : std::uint8_t {
    Empty = 0,
    StatementList,
    Statement,
    Expression,
    FunctionBoundary,
};

// One stable, caller-owned record for a suspended statement or expression.
// `cursor`, `mark`, and `callFrameIndex` are offsets/indices; no pointer into
// the arena or a native stack frame is retained across a nested call.
struct PortableExecutionFrame {
    PortableExecutionFrameKind kind = PortableExecutionFrameKind::Empty;
    std::uint8_t stage = 0u;
    std::uint16_t flags = 0u;
    AstNodeId node = kInvalidAstNodeId;
    AstNodeId auxiliaryNode = kInvalidAstNodeId;
    std::size_t cursor = 0u;
    std::size_t mark = 0u;
    std::size_t callFrameIndex = 0u;
    EnvironmentId savedEnvironment = kInvalidEnvironmentId;
    Value a = Value::undefined();
    Value b = Value::undefined();
    Value c = Value::undefined();
    Value childValue = Value::undefined();
};

struct PortableExecutionStorageView {
    PortableCallFrame* frames = nullptr;
    std::size_t frameCapacity = 0u;
    Value* argumentValues = nullptr;
    std::size_t argumentCapacity = 0u;
    PortableExecutionFrame* executionFrames = nullptr;
    std::size_t executionFrameCapacity = 0u;
};

// Shared evaluator for the initial portable JavaScript execution slice. It
// consumes the same AST, Value, RuntimeStringStore and EnvironmentStore as
// RuntimeContext. Persistent storage and invocation scratch are caller-owned;
// this class never allocates and does not depend on the hosted runtime.
template <bool EnableObjectOperations>
class PortableExecutionCoreImpl {
public:
    PortableExecutionCoreImpl(Ast& ast, RuntimeStringStore& strings,
        EnvironmentStore& environments, FunctionStore& functions,
        PortableExecutionStorageView storage,
        PortableExecutionLimits limits, std::size_t& sharedExecutionSteps)
        : ast_(ast), strings_(strings), environments_(environments),
          functions_(functions), storage_(storage), limits_(limits),
          sharedExecutionSteps_(sharedExecutionSteps) {}

    PortableExecutionCoreImpl(Ast& ast, RuntimeStringStore& strings,
        EnvironmentStore& environments, FunctionStore& functions,
        PortableObjectStore& objects, PortableExecutionStorageView storage,
        PortableExecutionLimits limits, std::size_t& sharedExecutionSteps)
        : ast_(ast), strings_(strings), environments_(environments),
          functions_(functions), objects_(&objects), storage_(storage),
          limits_(limits), sharedExecutionSteps_(sharedExecutionSteps) {}

    static bool supports(const Ast& ast, AstNodeId root,
        std::size_t maxCallArguments, std::size_t maxAstDepth = 256u)
    {
        return supports(ast, root, maxCallArguments, maxAstDepth, false);
    }

    static bool supports(const Ast& ast, AstNodeId root,
        std::size_t maxCallArguments, std::size_t maxAstDepth,
        bool allowObjectsAndArrays)
    {
        if (!validNode(ast, root) || ast.node(root).kind != AstNodeKind::Program)
            return false;
        bool hasString = false;
        bool hasAddition = false;
        for (std::size_t index = 0u; index < ast.nodeCount(); ++index) {
            const AstNode& node = ast.node(static_cast<AstNodeId>(index));
            hasString = hasString || node.kind == AstNodeKind::StringLiteral;
            hasAddition = hasAddition ||
                (node.kind == AstNodeKind::BinaryExpression &&
                    node.binaryOperator == AstBinaryOperator::Add) ||
                (node.kind == AstNodeKind::AssignmentExpression &&
                    node.assignmentOperator == AstAssignmentOperator::Add);
        }
        // String values are portable; coercive string concatenation remains
        // on the accepted hosted evaluator until its number formatting path
        // can be shared without changing its observable spelling.
        if (hasString && hasAddition) return false;
        return supportsStatement(ast, root, maxCallArguments, maxAstDepth, 0u,
            true, false, allowObjectsAndArrays);
    }

    bool run(AstNodeId root, EnvironmentId globalEnvironment,
        Value& finalValue, PortableExecutionResult& result)
    {
        result_ = PortableExecutionResult();
        finalValue_ = Value::undefined();
        result = PortableExecutionResult();
        finalValue = Value::undefined();
        if (strings_.generation() == kInvalidRuntimeGeneration ||
            !environments_.configured() ||
            !functions_.configured() ||
            (objects_ != nullptr && !objects_->configured()) ||
            (storage_.frameCapacity != 0u && storage_.frames == nullptr) ||
            (storage_.argumentCapacity != 0u &&
                storage_.argumentValues == nullptr) ||
            (storage_.executionFrameCapacity != 0u &&
                storage_.executionFrames == nullptr)) {
            fail(PortableExecutionErrorCode::InternalInvariantFailure,
                SourceLocation());
            return finish(result, finalValue);
        }
        if (!supports(ast_, root, limits_.maxCallArguments,
                limits_.maxAstDepth,
                EnableObjectOperations && objects_ != nullptr)) {
            fail(PortableExecutionErrorCode::UnsupportedOperation,
                validNode(ast_, root) ? ast_.node(root).location
                    : SourceLocation());
            return finish(result, finalValue);
        }
        EnvironmentError environmentError;
        if (!environments_.valid(globalEnvironment, environmentError)) {
            fail(mapEnvironmentError(environmentError.code),
                ast_.node(root).location);
            return finish(result, finalValue);
        }
        currentEnvironment_ = globalEnvironment;
        globalEnvironment_ = globalEnvironment;
        currentThis_ = Value::undefined();
        finalValue_ = Value::undefined();
        callDepth_ = 0u;
        argumentTop_ = 0u;
        executionFrameTop_ = 0u;
        machineReturning_ = false;
        machineReturnValue_ = Value::undefined();
        result_ = PortableExecutionResult();

        if (!preflightGlobalBindings(root) ||
            !instantiateFunctions(root, globalEnvironment) ||
            !instantiateLexicalBindings(root, globalEnvironment)) {
            return finish(result, finalValue);
        }
        if (storage_.executionFrameCapacity != 0u) {
            if (!runExecutionMachine(root, finalValue_))
                return finish(result, finalValue);
        } else {
            Control control;
            if (!executeStatement(root, false, control))
                return finish(result, finalValue);
            if (control.kind != ControlKind::Normal) {
                fail(PortableExecutionErrorCode::IllegalReturn,
                    ast_.node(root).location);
                return finish(result, finalValue);
            }
        }
        result_.completed = true;
        result_.finalValue = finalValue_;
        result_.executionSteps = sharedExecutionSteps_;
        result = result_;
        finalValue = finalValue_;
        return true;
    }

    // Callable identification for portable callback clients. Native and host
    // records remain callable Values, but this freestanding core reports them
    // as unsupported at invocation until an accepted shared implementation is
    // available.
    bool isCallable(Value value, bool& callable,
        PortableExecutionError& error) const
    {
        callable = false;
        error = PortableExecutionError();
        if (!value.isFunction()) return true;

        FunctionStoreError functionError;
        const FunctionRecord* function = functions_.get(
            value.functionId(), functionError);
        if (function == nullptr) {
            error.code = functionError.code ==
                    FunctionStoreErrorCode::StaleGeneration
                ? PortableExecutionErrorCode::StaleReference
                : functionError.code ==
                    FunctionStoreErrorCode::RealmMismatch
                    ? PortableExecutionErrorCode::RealmMismatch
                : functionError.code ==
                    FunctionStoreErrorCode::InvalidReference
                    ? PortableExecutionErrorCode::InvalidFunctionReference
                    : PortableExecutionErrorCode::InternalInvariantFailure;
            return false;
        }
        switch (function->kind) {
        case FunctionRecord::Kind::User:
        case FunctionRecord::Kind::Native:
        case FunctionRecord::Kind::Host:
            callable = true;
            return true;
        }
        error.code = PortableExecutionErrorCode::InternalInvariantFailure;
        return false;
    }

    // Invoke a retained callable in this execution context. Ordinary
    // functions receive the explicit receiver; arrows retain lexical this.
    // The argument span is borrowed only for this synchronous call and is
    // checked against the same depth, argument, and execution budgets used by
    // script calls.
    bool invokeCallable(Value callable, Value receiver,
        const Value* arguments, std::size_t argumentCount,
        PortableExecutionResult& result,
        SourceLocation callSite = SourceLocation())
    {
        result_ = PortableExecutionResult();
        finalValue_ = Value::undefined();
        result = PortableExecutionResult();
        if (!environments_.configured() || !functions_.configured() ||
            (objects_ != nullptr && !objects_->configured()) ||
            globalEnvironment_ == kInvalidEnvironmentId ||
            (storage_.frameCapacity != 0u && storage_.frames == nullptr)) {
            fail(PortableExecutionErrorCode::InternalInvariantFailure,
                callSite);
            result = result_;
            return false;
        }
        EnvironmentError environmentError;
        if (!environments_.valid(globalEnvironment_, environmentError)) {
            fail(mapEnvironmentError(environmentError.code), callSite);
            result = result_;
            return false;
        }
        currentEnvironment_ = globalEnvironment_;
        currentThis_ = Value::undefined();

        bool callableValue = false;
        PortableExecutionError callableError;
        if (!isCallable(callable, callableValue, callableError)) {
            fail(callableError.code, callSite);
        } else if (!callableValue) {
            fail(PortableExecutionErrorCode::TypeError, callSite);
        } else if (argumentCount > limits_.maxCallArguments) {
            fail(PortableExecutionErrorCode::InvalidInvocationTarget,
                callSite);
        } else if (argumentCount != 0u && arguments == nullptr) {
            fail(PortableExecutionErrorCode::InternalInvariantFailure,
                callSite);
        } else {
            result_.argumentValuesHighWater = argumentCount;
            FunctionStoreError functionError;
            const FunctionRecord* function = functions_.get(
                callable.functionId(), functionError);
            if (function == nullptr) {
                fail(functionError.code ==
                        FunctionStoreErrorCode::StaleGeneration
                    ? PortableExecutionErrorCode::StaleReference
                    : functionError.code ==
                        FunctionStoreErrorCode::RealmMismatch
                        ? PortableExecutionErrorCode::RealmMismatch
                    : PortableExecutionErrorCode::InvalidFunctionReference,
                    callSite);
            } else if (function->kind != FunctionRecord::Kind::User) {
                fail(PortableExecutionErrorCode::UnsupportedOperation,
                    callSite);
            } else {
                Value returned = Value::undefined();
                if (invokeFunction(callable.functionId(), arguments,
                        argumentCount, callSite, returned, receiver)) {
                    finalValue_ = returned;
                    result_.completed = true;
                    result_.finalValue = returned;
                }
            }
        }
        result_.executionSteps = sharedExecutionSteps_;
        result = result_;
        return result_.completed;
    }

private:
    enum class ControlKind : std::uint8_t { Normal = 0, Return, TailCall };
    struct Control {
        ControlKind kind = ControlKind::Normal;
        Value value = Value::undefined();
        RuntimeFunctionId tailFunction = kInvalidRuntimeFunctionId;
        Value tailReceiver = Value::undefined();
        std::size_t tailArgumentMark = 0u;
        std::size_t tailArgumentCount = 0u;
        SourceLocation tailCallSite;
    };

    class EnvironmentRestore {
    public:
        EnvironmentRestore(PortableExecutionCoreImpl& core,
            EnvironmentId previous)
            : core_(core), previous_(previous) {}
        ~EnvironmentRestore() { core_.currentEnvironment_ = previous_; }
    private:
        PortableExecutionCoreImpl& core_;
        EnvironmentId previous_;
    };

    class CallRestore {
    public:
        CallRestore(PortableExecutionCoreImpl& core,
            EnvironmentId previousEnvironment,
            Value previousThis, std::size_t previousArgumentTop,
            std::size_t previousCallDepth)
            : core_(core), previousEnvironment_(previousEnvironment),
              previousThis_(previousThis), previousArgumentTop_(previousArgumentTop),
              previousCallDepth_(previousCallDepth) {}
        ~CallRestore()
        {
            if (core_.storage_.frames != nullptr) {
                const std::size_t end = core_.callDepth_ <
                        core_.storage_.frameCapacity
                    ? core_.callDepth_ : core_.storage_.frameCapacity;
                for (std::size_t index = previousCallDepth_; index < end;
                    ++index) {
                    core_.storage_.frames[index].continuationNode =
                        kInvalidAstNodeId;
                }
            }
            core_.currentEnvironment_ = previousEnvironment_;
            core_.currentThis_ = previousThis_;
            core_.argumentTop_ = previousArgumentTop_;
            core_.callDepth_ = previousCallDepth_;
        }
    private:
        PortableExecutionCoreImpl& core_;
        EnvironmentId previousEnvironment_;
        Value previousThis_;
        std::size_t previousArgumentTop_;
        std::size_t previousCallDepth_;
    };

    static bool validNode(const Ast& ast, AstNodeId id)
    {
        return id != kInvalidAstNodeId && id < ast.nodeCount() &&
            ast.node(id).kind != AstNodeKind::Invalid;
    }

    static bool equalName(SourceView left, SourceView right)
    {
        if (left.data == nullptr || right.data == nullptr ||
            left.length != right.length) return false;
        for (std::size_t index = 0u; index < left.length; ++index)
            if (left.data[index] != right.data[index]) return false;
        return true;
    }

    static bool isIdentifierName(const Ast& ast, AstNodeId id,
        SourceView& name)
    {
        if (!validNode(ast, id) || ast.node(id).kind != AstNodeKind::Identifier)
            return false;
        name = ast.nodeText(id);
        return name.data != nullptr && name.length != 0u;
    }

    static bool functionReturnsFunction(const Ast& ast, AstNodeId id)
    {
        if (!validNode(ast, id)) return false;
        const AstNode& function = ast.node(id);
        if (function.kind == AstNodeKind::ArrowFunctionExpression &&
            function.body == kInvalidAstNodeId) {
            if (!validNode(ast, function.expression)) return false;
            const AstNodeKind result = ast.node(function.expression).kind;
            return result == AstNodeKind::FunctionExpression ||
                result == AstNodeKind::ArrowFunctionExpression;
        }
        if (function.body == kInvalidAstNodeId ||
            !validNode(ast, function.body) ||
            ast.node(function.body).kind != AstNodeKind::BlockStatement)
            return false;
        bool hasFunctionReturn = false;
        const AstNode& body = ast.node(function.body);
        for (std::size_t index = 0u; index < body.childCount; ++index) {
            const AstNodeId statementId = ast.childAt(function.body, index);
            const AstNode& statement = ast.node(statementId);
            if (statement.kind != AstNodeKind::ReturnStatement) continue;
            if (!validNode(ast, statement.expression)) return false;
            const AstNodeKind result = ast.node(statement.expression).kind;
            if (result != AstNodeKind::FunctionExpression &&
                result != AstNodeKind::ArrowFunctionExpression) return false;
            hasFunctionReturn = true;
        }
        return hasFunctionReturn;
    }

    static bool namedFunctionDeclarationReturnsFunction(const Ast& ast,
        SourceView name)
    {
        bool found = false;
        for (std::size_t index = 0u; index < ast.nodeCount(); ++index) {
            const AstNode& node = ast.node(static_cast<AstNodeId>(index));
            if (node.kind != AstNodeKind::FunctionDeclaration) continue;
            SourceView declared;
            if (!isIdentifierName(ast, node.name, declared) ||
                !equalName(name, declared)) continue;
            found = true;
            if (!functionReturnsFunction(ast,
                    static_cast<AstNodeId>(index))) return false;
        }
        return found;
    }

    static bool declaredFunctionLike(const Ast& ast, SourceView name)
    {
        bool found = false;
        bool shadowed = false;
        for (std::size_t index = 0u; index < ast.nodeCount(); ++index) {
            const AstNodeId id = static_cast<AstNodeId>(index);
            const AstNode& node = ast.node(id);
            if (node.kind == AstNodeKind::FunctionDeclaration) {
                SourceView declared;
                if (isIdentifierName(ast, node.name, declared) &&
                    equalName(name, declared)) found = true;
            } else if (node.kind == AstNodeKind::VariableDeclarator) {
                SourceView declared;
                if (!isIdentifierName(ast, node.name, declared) ||
                    !equalName(name, declared)) continue;
                if (validNode(ast, node.initializer)) {
                    const AstNode& initializer = ast.node(node.initializer);
                    if (initializer.kind == AstNodeKind::FunctionExpression ||
                        initializer.kind == AstNodeKind::ArrowFunctionExpression) {
                        found = true;
                    } else if (initializer.kind == AstNodeKind::CallExpression &&
                        validNode(ast, initializer.callee) &&
                        ast.node(initializer.callee).kind ==
                            AstNodeKind::Identifier) {
                        SourceView factory;
                        if (isIdentifierName(ast, initializer.callee, factory) &&
                            namedFunctionDeclarationReturnsFunction(ast,
                                factory)) found = true;
                        else shadowed = true;
                    } else {
                        shadowed = true;
                    }
                } else {
                    shadowed = true;
                }
            }
            if (node.kind == AstNodeKind::FunctionDeclaration ||
                node.kind == AstNodeKind::FunctionExpression ||
                node.kind == AstNodeKind::ArrowFunctionExpression) {
                for (std::size_t parameter = 0u;
                    parameter < node.childCount; ++parameter) {
                    SourceView declared;
                    const AstNodeId parameterId = ast.childAt(
                        id, parameter);
                    if (isIdentifierName(ast, parameterId, declared) &&
                        equalName(name, declared)) shadowed = true;
                }
            }
        }
        return found && !shadowed;
    }

    static bool simpleParameters(const Ast& ast, AstNodeId function)
    {
        if (!validNode(ast, function)) return false;
        const AstNode& node = ast.node(function);
        if (node.childCount > 64u) return false;
        for (std::size_t index = 0u; index < node.childCount; ++index) {
            const AstNodeId parameter = ast.childAt(function, index);
            if (!validNode(ast, parameter) ||
                ast.node(parameter).kind != AstNodeKind::Identifier ||
                ast.node(parameter).initializer != kInvalidAstNodeId ||
                ast.node(parameter).prefix) return false;
        }
        return true;
    }

    static bool supportsFunction(const Ast& ast, AstNodeId id,
        std::size_t maxArgs, std::size_t maxDepth, std::size_t depth,
        bool allowObjects)
    {
        if (depth > maxDepth || !validNode(ast, id) ||
            !simpleParameters(ast, id)) return false;
        const AstNode& function = ast.node(id);
        if (function.kind == AstNodeKind::ArrowFunctionExpression &&
            function.body == kInvalidAstNodeId) {
            return function.expression != kInvalidAstNodeId &&
                supportsExpression(ast, function.expression, maxArgs, maxDepth,
                    depth + 1u, allowObjects);
        }
        if (function.body == kInvalidAstNodeId ||
            !validNode(ast, function.body) ||
            ast.node(function.body).kind != AstNodeKind::BlockStatement)
            return false;
        return supportsStatementList(ast, function.body, maxArgs, maxDepth,
            depth + 1u, true, true, allowObjects);
    }

    static bool supportsExpression(const Ast& ast, AstNodeId id,
        std::size_t maxArgs, std::size_t maxDepth, std::size_t depth,
        bool allowObjects)
    {
        if (depth > maxDepth || !validNode(ast, id)) return false;
        const AstNode& node = ast.node(id);
        switch (node.kind) {
        case AstNodeKind::Identifier:
        case AstNodeKind::NumericLiteral:
        case AstNodeKind::StringLiteral:
        case AstNodeKind::BooleanLiteral:
        case AstNodeKind::NullLiteral:
        case AstNodeKind::ThisExpression:
            return true;
        case AstNodeKind::UnaryExpression:
            return node.argument != kInvalidAstNodeId &&
                supportsExpression(ast, node.argument, maxArgs, maxDepth,
                    depth + 1u, allowObjects);
        case AstNodeKind::BinaryExpression:
            return node.binaryOperator != AstBinaryOperator::Remainder &&
                validNode(ast, node.left) && validNode(ast, node.right) &&
                supportsExpression(ast, node.left, maxArgs, maxDepth,
                    depth + 1u, allowObjects) &&
                supportsExpression(ast, node.right, maxArgs, maxDepth,
                    depth + 1u, allowObjects);
        case AstNodeKind::LogicalExpression:
            return validNode(ast, node.left) && validNode(ast, node.right) &&
                supportsExpression(ast, node.left, maxArgs, maxDepth,
                    depth + 1u, allowObjects) &&
                supportsExpression(ast, node.right, maxArgs, maxDepth,
                    depth + 1u, allowObjects);
        case AstNodeKind::AssignmentExpression:
            return node.assignmentOperator != AstAssignmentOperator::Remainder &&
                validNode(ast, node.left) && validNode(ast, node.right) &&
                ((ast.node(node.left).kind == AstNodeKind::Identifier) ||
                    (allowObjects &&
                        node.assignmentOperator == AstAssignmentOperator::Assign &&
                        ast.node(node.left).kind == AstNodeKind::MemberExpression)) &&
                supportsExpression(ast, node.left, maxArgs, maxDepth,
                    depth + 1u, allowObjects) &&
                supportsExpression(ast, node.right, maxArgs, maxDepth,
                    depth + 1u, allowObjects);
        case AstNodeKind::CallExpression: {
            if (!validNode(ast, node.callee) || node.childCount > maxArgs)
                return false;
            if (ast.node(node.callee).kind == AstNodeKind::Identifier) {
                SourceView name;
                if (!isIdentifierName(ast, node.callee, name) ||
                    !declaredFunctionLike(ast, name)) return false;
            } else if (allowObjects && ast.node(node.callee).kind ==
                    AstNodeKind::MemberExpression) {
                const AstNode& member = ast.node(node.callee);
                if (!validNode(ast, member.object) ||
                    !validNode(ast, member.property) ||
                    !supportsExpression(ast, member.object, maxArgs,
                        maxDepth, depth + 1u, allowObjects) ||
                    (member.computed
                        ? !supportsExpression(ast, member.property, maxArgs,
                            maxDepth, depth + 1u, allowObjects)
                        : ast.node(member.property).kind !=
                            AstNodeKind::Identifier)) return false;
            } else {
                return false;
            }
            for (std::size_t index = 0u; index < node.childCount; ++index) {
                if (!supportsExpression(ast, ast.childAt(id, index), maxArgs,
                        maxDepth, depth + 1u, allowObjects)) return false;
            }
            return true;
        }
        case AstNodeKind::FunctionExpression:
        case AstNodeKind::ArrowFunctionExpression:
            return supportsFunction(ast, id, maxArgs, maxDepth, depth + 1u,
                allowObjects);
        case AstNodeKind::MemberExpression:
            return allowObjects && validNode(ast, node.object) &&
                validNode(ast, node.property) &&
                supportsExpression(ast, node.object, maxArgs, maxDepth,
                    depth + 1u, allowObjects) &&
                (node.computed
                    ? supportsExpression(ast, node.property, maxArgs, maxDepth,
                        depth + 1u, allowObjects)
                    : ast.node(node.property).kind == AstNodeKind::Identifier);
        case AstNodeKind::ObjectLiteral:
            if (!allowObjects) return false;
            for (std::size_t index = 0u; index < node.childCount; ++index) {
                const AstNodeId propertyId = ast.childAt(id, index);
                if (!validNode(ast, propertyId) ||
                    ast.node(propertyId).kind != AstNodeKind::ObjectProperty)
                    return false;
                const AstNode& property = ast.node(propertyId);
                if (!validNode(ast, property.key) ||
                    !validNode(ast, property.initializer) ||
                    (ast.node(property.key).kind != AstNodeKind::Identifier &&
                        ast.node(property.key).kind != AstNodeKind::StringLiteral) ||
                    !supportsExpression(ast, property.initializer, maxArgs,
                        maxDepth, depth + 1u, allowObjects)) return false;
            }
            return true;
        case AstNodeKind::ArrayLiteral:
            if (!allowObjects) return false;
            for (std::size_t index = 0u; index < node.childCount; ++index) {
                const AstNodeId element = ast.childAt(id, index);
                if (!validNode(ast, element)) return false;
                if (ast.node(element).kind != AstNodeKind::ArrayElision &&
                    !supportsExpression(ast, element, maxArgs, maxDepth,
                        depth + 1u, allowObjects)) return false;
            }
            return true;
        default:
            return false;
        }
    }

    static bool supportsStatementList(const Ast& ast, AstNodeId id,
        std::size_t maxArgs, std::size_t maxDepth, std::size_t depth,
        bool allowFunctionDeclarations, bool inFunction, bool allowObjects)
    {
        if (!validNode(ast, id) || depth > maxDepth) return false;
        const AstNode& list = ast.node(id);
        for (std::size_t index = 0u; index < list.childCount; ++index) {
            const AstNodeId child = ast.childAt(id, index);
            if (!supportsStatement(ast, child, maxArgs, maxDepth, depth + 1u,
                    allowFunctionDeclarations, inFunction,
                    allowObjects)) return false;
        }
        return true;
    }

    static bool supportsStatement(const Ast& ast, AstNodeId id,
        std::size_t maxArgs, std::size_t maxDepth, std::size_t depth,
        bool allowFunctionDeclarations, bool inFunction, bool allowObjects)
    {
        if (!validNode(ast, id) || depth > maxDepth) return false;
        const AstNode& node = ast.node(id);
        switch (node.kind) {
        case AstNodeKind::Program:
            return supportsStatementList(ast, id, maxArgs, maxDepth, depth + 1u,
                true, false, allowObjects);
        case AstNodeKind::BlockStatement:
            return supportsStatementList(ast, id, maxArgs, maxDepth, depth + 1u,
                false, inFunction, allowObjects);
        case AstNodeKind::EmptyStatement:
            return true;
        case AstNodeKind::ExpressionStatement:
            return node.expression != kInvalidAstNodeId &&
                supportsExpression(ast, node.expression, maxArgs, maxDepth,
                    depth + 1u, allowObjects);
        case AstNodeKind::VariableDeclaration:
            if (node.declarationKind == AstDeclarationKind::Var) return false;
            for (std::size_t index = 0u; index < node.childCount; ++index) {
                const AstNodeId declaration = ast.childAt(id, index);
                if (!validNode(ast, declaration) ||
                    ast.node(declaration).kind != AstNodeKind::VariableDeclarator ||
                    !validNode(ast, ast.node(declaration).name) ||
                    ast.node(ast.node(declaration).name).kind !=
                        AstNodeKind::Identifier) return false;
                const AstNodeId initializer = ast.node(declaration).initializer;
                if (initializer != kInvalidAstNodeId &&
                    !supportsExpression(ast, initializer, maxArgs, maxDepth,
                        depth + 1u, allowObjects)) return false;
            }
            return true;
        case AstNodeKind::ReturnStatement:
            return node.expression == kInvalidAstNodeId ||
                supportsExpression(ast, node.expression, maxArgs, maxDepth,
                    depth + 1u, allowObjects);
        case AstNodeKind::FunctionDeclaration:
            return allowFunctionDeclarations &&
                supportsFunction(ast, id, maxArgs, maxDepth, depth + 1u,
                    allowObjects);
        default:
            return false;
        }
    }

    void fail(PortableExecutionErrorCode code, SourceLocation location)
    {
        if (result_.error.code != PortableExecutionErrorCode::None) return;
        result_.error.code = code;
        result_.error.location = location;
        result_.executionSteps = sharedExecutionSteps_;
    }

    void failObject(const PortableObjectError& error, SourceLocation location)
    {
        PortableExecutionErrorCode code =
            PortableExecutionErrorCode::InternalInvariantFailure;
        switch (error.code) {
        case PortableObjectErrorCode::None:
            code = PortableExecutionErrorCode::None;
            break;
        case PortableObjectErrorCode::InvalidStorage:
        case PortableObjectErrorCode::InternalInvariantFailure:
            code = PortableExecutionErrorCode::InternalInvariantFailure;
            break;
        case PortableObjectErrorCode::InvalidObject:
        case PortableObjectErrorCode::InvalidPrototype:
        case PortableObjectErrorCode::PrototypeCycle:
            code = PortableExecutionErrorCode::InvalidObjectReference;
            break;
        case PortableObjectErrorCode::StaleGeneration:
            code = PortableExecutionErrorCode::StaleObjectReference;
            break;
        case PortableObjectErrorCode::InvalidArray:
            code = PortableExecutionErrorCode::TypeError;
            break;
        case PortableObjectErrorCode::InvalidProperty:
            code = PortableExecutionErrorCode::InvalidPropertyReference;
            break;
        case PortableObjectErrorCode::InvalidIndex:
            code = PortableExecutionErrorCode::InvalidArrayIndex;
            break;
        case PortableObjectErrorCode::InvalidLength:
            code = PortableExecutionErrorCode::InvalidArrayLength;
            break;
        case PortableObjectErrorCode::PrototypeDepthExceeded:
            code = PortableExecutionErrorCode::PrototypeDepthExceeded;
            break;
        case PortableObjectErrorCode::PropertyCapacityExceeded:
        case PortableObjectErrorCode::NameStorageExhausted:
            code = PortableExecutionErrorCode::PropertyCapacityExhausted;
            break;
        case PortableObjectErrorCode::NameTooLong:
            code = PortableExecutionErrorCode::PropertyNameTooLong;
            break;
        case PortableObjectErrorCode::ObjectCapacityExceeded:
            code = PortableExecutionErrorCode::ObjectCapacityExhausted;
            break;
        case PortableObjectErrorCode::ArrayCapacityExceeded:
            code = PortableExecutionErrorCode::ArrayCapacityExhausted;
            break;
        case PortableObjectErrorCode::ArithmeticOverflow:
            code = PortableExecutionErrorCode::InvalidArrayIndex;
            break;
        }
        if (code != PortableExecutionErrorCode::None) fail(code, location);
    }

    struct PropertyReference {
        RuntimeObjectId object = kInvalidRuntimeObjectId;
        SourceView key;
        char numberKey[24]{};
    };

    bool propertyKeyFromValue(const Value& value, PropertyReference& reference,
        SourceLocation location)
    {
        if (value.isString()) {
            RuntimeStringError error;
            if (!strings_.view(value.stringId(), reference.key, error)) {
                fail(error.code == RuntimeStringErrorCode::StaleGeneration
                        ? PortableExecutionErrorCode::StaleReference
                        : PortableExecutionErrorCode::InternalInvariantFailure,
                    location);
                return false;
            }
            return true;
        }
        if (!value.isNumber()) {
            fail(PortableExecutionErrorCode::UnsupportedOperation, location);
            return false;
        }
        const double number = value.numberValue();
        if (!(number >= 0.0) || number > 4294967294.0) {
            fail(PortableExecutionErrorCode::UnsupportedOperation, location);
            return false;
        }
        std::uint64_t integer = static_cast<std::uint64_t>(number);
        if (static_cast<double>(integer) != number) {
            fail(PortableExecutionErrorCode::UnsupportedOperation, location);
            return false;
        }
        char reversed[24]{};
        std::size_t digits = 0u;
        do {
            reversed[digits++] = static_cast<char>('0' + integer % 10u);
            integer /= 10u;
        } while (integer != 0u && digits < sizeof(reversed));
        for (std::size_t index = 0u; index < digits; ++index)
            reference.numberKey[index] = reversed[digits - index - 1u];
        reference.key = SourceView(reference.numberKey, digits);
        return true;
    }

    bool evalPropertyReference(AstNodeId id, PropertyReference& reference)
    {
        if (objects_ == nullptr || !validNode(ast_, id) ||
            ast_.node(id).kind != AstNodeKind::MemberExpression) {
            fail(PortableExecutionErrorCode::InvalidAssignmentTarget,
                validNode(ast_, id) ? ast_.node(id).location
                    : SourceLocation());
            return false;
        }
        const AstNode& node = ast_.node(id);
        Value receiver;
        if (!evalExpression(node.object, receiver)) return false;
        if (!receiver.isObject()) {
            fail(PortableExecutionErrorCode::TypeError, node.location);
            return false;
        }
        reference.object = receiver.objectId();
        if (node.computed) {
            Value key;
            if (!evalExpression(node.property, key)) return false;
            return propertyKeyFromValue(key, reference, node.location);
        }
        if (!validNode(ast_, node.property) ||
            ast_.node(node.property).kind != AstNodeKind::Identifier) {
            fail(PortableExecutionErrorCode::InvalidAst, node.location);
            return false;
        }
        reference.key = ast_.nodeText(node.property);
        return true;
    }

    bool finish(PortableExecutionResult& output, Value& finalValue)
    {
        result_.completed = false;
        result_.finalValue = finalValue_;
        result_.executionSteps = sharedExecutionSteps_;
        output = result_;
        finalValue = finalValue_;
        return false;
    }

    bool consumeStep(SourceLocation location)
    {
        if (sharedExecutionSteps_ >= limits_.maxExecutionSteps) {
            fail(PortableExecutionErrorCode::StepBudgetExhausted, location);
            return false;
        }
        ++sharedExecutionSteps_;
        result_.executionSteps = sharedExecutionSteps_;
        return true;
    }

    PortableExecutionErrorCode mapEnvironmentError(
        EnvironmentErrorCode code) const
    {
        switch (code) {
        case EnvironmentErrorCode::None: return PortableExecutionErrorCode::None;
        case EnvironmentErrorCode::MissingBinding:
            return PortableExecutionErrorCode::MissingBinding;
        case EnvironmentErrorCode::UninitializedBinding:
        case EnvironmentErrorCode::BindingAlreadyInitialized:
            return PortableExecutionErrorCode::UninitializedBinding;
        case EnvironmentErrorCode::ImmutableAssignment:
            return PortableExecutionErrorCode::ImmutableAssignment;
        case EnvironmentErrorCode::DuplicateLexicalBinding:
            return PortableExecutionErrorCode::DuplicateBinding;
        case EnvironmentErrorCode::BindingLimitExceeded:
        case EnvironmentErrorCode::BindingNameTooLong:
            return PortableExecutionErrorCode::BindingCapacityExhausted;
        case EnvironmentErrorCode::CapacityExceeded:
            return PortableExecutionErrorCode::EnvironmentCapacityExhausted;
        case EnvironmentErrorCode::AllocationFailure:
            return PortableExecutionErrorCode::AllocationFailure;
        case EnvironmentErrorCode::InvalidReference:
        case EnvironmentErrorCode::StaleGeneration:
            return PortableExecutionErrorCode::StaleReference;
        case EnvironmentErrorCode::InvalidEnvironment:
            return PortableExecutionErrorCode::InvalidEnvironmentReference;
        case EnvironmentErrorCode::InternalInvariantFailure:
            return PortableExecutionErrorCode::InternalInvariantFailure;
        default:
            return PortableExecutionErrorCode::InvalidAst;
        }
    }

    PortableExecutionErrorCode mapBindingError(EnvironmentErrorCode code) const
    {
        if (code == EnvironmentErrorCode::BindingLimitExceeded ||
            code == EnvironmentErrorCode::BindingNameTooLong ||
            code == EnvironmentErrorCode::CapacityExceeded)
            return PortableExecutionErrorCode::BindingCapacityExhausted;
        return mapEnvironmentError(code);
    }

    bool nodeName(AstNodeId id, SourceView& name, SourceLocation at)
    {
        if (!isIdentifierName(ast_, id, name)) {
            fail(PortableExecutionErrorCode::InvalidAst, at);
            return false;
        }
        return true;
    }

    bool preflightGlobalBindings(AstNodeId program)
    {
        const AstNode& root = ast_.node(program);
        for (std::size_t index = 0u; index < root.childCount; ++index) {
            const AstNodeId child = ast_.childAt(program, index);
            const AstNode& declaration = ast_.node(child);
            if (declaration.kind == AstNodeKind::VariableDeclaration) {
                if (declaration.declarationKind == AstDeclarationKind::Var)
                    continue;
                for (std::size_t item = 0u; item < declaration.childCount; ++item) {
                    const AstNodeId declaratorId = ast_.childAt(child, item);
                    const AstNode& declarator = ast_.node(declaratorId);
                    SourceView name;
                    if (!nodeName(declarator.name, name, declarator.location))
                        return false;
                    EnvironmentError error;
                    const Binding* existing = environments_.resolveLocal(
                        currentEnvironment_, name, error);
                    if (existing != nullptr) {
                        fail(PortableExecutionErrorCode::DuplicateBinding,
                            declarator.location);
                        return false;
                    }
                    if (error.code != EnvironmentErrorCode::None &&
                        error.code != EnvironmentErrorCode::MissingBinding) {
                        fail(mapEnvironmentError(error.code),
                            declarator.location);
                        return false;
                    }
                }
            } else if (declaration.kind == AstNodeKind::FunctionDeclaration) {
                SourceView name;
                if (!nodeName(declaration.name, name, declaration.location))
                    return false;
                EnvironmentError error;
                const Binding* existing = environments_.resolveLocal(
                    currentEnvironment_, name, error);
                if (existing != nullptr && existing->lexical) {
                    fail(PortableExecutionErrorCode::DuplicateBinding,
                        declaration.location);
                    return false;
                }
                if (existing == nullptr && error.code != EnvironmentErrorCode::None &&
                    error.code != EnvironmentErrorCode::MissingBinding) {
                    fail(mapEnvironmentError(error.code), declaration.location);
                    return false;
                }
            }
        }
        return true;
    }

    bool instantiateFunctions(AstNodeId container, EnvironmentId environment)
    {
        const AstNode& scope = ast_.node(container);
        for (std::size_t index = 0u; index < scope.childCount; ++index) {
            const AstNodeId child = ast_.childAt(container, index);
            const AstNode& declaration = ast_.node(child);
            if (declaration.kind != AstNodeKind::FunctionDeclaration) continue;
            if (hasLaterFunctionDeclaration(container, index,
                    declaration.name)) continue;
            if (result_.functionsCreated >= limits_.maxFunctions) {
                fail(PortableExecutionErrorCode::FunctionCapacityExhausted,
                    declaration.location);
                return false;
            }
            SourceView name;
            if (!nodeName(declaration.name, name, declaration.location))
                return false;
            FunctionRecord record;
            record.kind = FunctionRecord::Kind::User;
            record.declaration = child;
            record.closureEnvironment = environment;
            record.arrowFunction = false;
            RuntimeFunctionId function = kInvalidRuntimeFunctionId;
            FunctionStoreError functionError;
            if (!functions_.append(record, function, functionError)) {
                fail(functionError.code == FunctionStoreErrorCode::CapacityExceeded
                        ? PortableExecutionErrorCode::FunctionCapacityExhausted
                        : functionError.code == FunctionStoreErrorCode::StaleGeneration
                            ? PortableExecutionErrorCode::StaleReference
                            : PortableExecutionErrorCode::InternalInvariantFailure,
                    declaration.location);
                return false;
            }
            EnvironmentError envError;
            Binding* existing = environments_.resolveLocal(environment, name,
                envError);
            if (existing != nullptr) {
                if (existing->lexical) {
                    if (!discardUnpublishedFunction(function,
                            declaration.location)) return false;
                    fail(PortableExecutionErrorCode::DuplicateBinding,
                        declaration.location);
                    return false;
                }
                if (!environments_.assign(environment, name,
                        Value::function(function), envError)) {
                    const PortableExecutionErrorCode mapped =
                        mapBindingError(envError.code);
                    if (!discardUnpublishedFunction(function,
                            declaration.location)) return false;
                    fail(mapped, declaration.location);
                    return false;
                }
            } else if (envError.code != EnvironmentErrorCode::None &&
                envError.code != EnvironmentErrorCode::MissingBinding) {
                const PortableExecutionErrorCode mapped =
                    mapEnvironmentError(envError.code);
                if (!discardUnpublishedFunction(function,
                        declaration.location)) return false;
                fail(mapped, declaration.location);
                return false;
            } else if (!environments_.declare(environment, name,
                    Value::function(function), envError)) {
                const PortableExecutionErrorCode mapped =
                    mapBindingError(envError.code);
                if (!discardUnpublishedFunction(function,
                        declaration.location)) return false;
                fail(mapped, declaration.location);
                return false;
            }
            ++result_.functionsCreated;
        }
        return true;
    }

    bool discardUnpublishedFunction(RuntimeFunctionId function,
        SourceLocation location)
    {
        FunctionStoreError error;
        if (functions_.discardLast(function, error)) return true;
        fail(PortableExecutionErrorCode::InternalInvariantFailure, location);
        return false;
    }

    bool hasLaterFunctionDeclaration(AstNodeId container, std::size_t index,
        AstNodeId name) const
    {
        const AstNode& scope = ast_.node(container);
        const SourceView target = ast_.nodeText(name);
        for (std::size_t later = index + 1u; later < scope.childCount; ++later) {
            const AstNode& candidate = ast_.node(ast_.childAt(container, later));
            if (candidate.kind == AstNodeKind::FunctionDeclaration &&
                equalName(target, ast_.nodeText(candidate.name))) return true;
        }
        return false;
    }

    bool instantiateLexicalBindings(AstNodeId container,
        EnvironmentId environment)
    {
        const AstNode& scope = ast_.node(container);
        for (std::size_t index = 0u; index < scope.childCount; ++index) {
            const AstNodeId child = ast_.childAt(container, index);
            const AstNode& declaration = ast_.node(child);
            if (declaration.kind != AstNodeKind::VariableDeclaration) continue;
            const bool immutable = declaration.declarationKind ==
                AstDeclarationKind::Const;
            for (std::size_t item = 0u; item < declaration.childCount; ++item) {
                const AstNodeId declaratorId = ast_.childAt(child, item);
                const AstNode& declarator = ast_.node(declaratorId);
                SourceView name;
                if (!nodeName(declarator.name, name, declarator.location))
                    return false;
                EnvironmentError error;
                if (!environments_.declareLexical(environment, name, immutable,
                        error, Value::undefined(), false)) {
                    fail(mapBindingError(error.code), declarator.location);
                    return false;
                }
            }
        }
        return true;
    }

    bool hasDirectLexicalBindings(AstNodeId block) const
    {
        const AstNode& node = ast_.node(block);
        for (std::size_t index = 0u; index < node.childCount; ++index) {
            const AstNode& child = ast_.node(ast_.childAt(block, index));
            if (child.kind == AstNodeKind::VariableDeclaration) return true;
        }
        return false;
    }

    enum MachineFrameFlags : std::uint16_t {
        MachineInFunction = 0x0001u,
        MachineRestoreEnvironment = 0x0002u,
        MachineChargeNode = 0x0004u,
        MachineTailPosition = 0x0008u,
        MachineChildReady = 0x0010u,
    };

    bool machinePush(PortableExecutionFrameKind kind, AstNodeId node,
        bool inFunction, bool chargeNode, bool restoreEnvironment = false,
        bool tailPosition = false)
    {
        const std::size_t capacity = storage_.executionFrameCapacity <
                limits_.maxExecutionFrames
            ? storage_.executionFrameCapacity : limits_.maxExecutionFrames;
        if (executionFrameTop_ >= capacity) {
            fail(PortableExecutionErrorCode::ExecutionFrameCapacityExhausted,
                validNode(ast_, node) ? ast_.node(node).location
                    : SourceLocation());
            return false;
        }
        PortableExecutionFrame& frame =
            storage_.executionFrames[executionFrameTop_++];
        frame = PortableExecutionFrame();
        frame.kind = kind;
        frame.node = node;
        if (inFunction) frame.flags |= MachineInFunction;
        if (chargeNode) frame.flags |= MachineChargeNode;
        if (restoreEnvironment) frame.flags |= MachineRestoreEnvironment;
        if (tailPosition) frame.flags |= MachineTailPosition;
        if (executionFrameTop_ > result_.executionFrameHighWater)
            result_.executionFrameHighWater = executionFrameTop_;
        return true;
    }

    bool machinePushExpression(AstNodeId node, bool inFunction,
        bool tailPosition = false)
    {
        if (!validNode(ast_, node)) {
            fail(PortableExecutionErrorCode::InvalidAst, SourceLocation());
            return false;
        }
        return machinePush(PortableExecutionFrameKind::Expression, node,
            inFunction, false, false, tailPosition);
    }

    bool machinePushStatement(AstNodeId node, bool inFunction)
    {
        if (!validNode(ast_, node)) {
            fail(PortableExecutionErrorCode::InvalidAst, SourceLocation());
            return false;
        }
        return machinePush(PortableExecutionFrameKind::Statement, node,
            inFunction, false);
    }

    bool machinePushStatementList(AstNodeId node, bool inFunction,
        bool chargeNode, bool restoreEnvironment = false)
    {
        if (!validNode(ast_, node)) {
            fail(PortableExecutionErrorCode::InvalidAst, SourceLocation());
            return false;
        }
        return machinePush(PortableExecutionFrameKind::StatementList, node,
            inFunction, chargeNode, restoreEnvironment);
    }

    void machinePop(bool hasValue, Value value = Value::undefined())
    {
        if (executionFrameTop_ == 0u) {
            fail(PortableExecutionErrorCode::InternalInvariantFailure,
                SourceLocation());
            return;
        }
        PortableExecutionFrame& top =
            storage_.executionFrames[executionFrameTop_ - 1u];
        if (top.kind == PortableExecutionFrameKind::StatementList &&
            (top.flags & MachineRestoreEnvironment) != 0u)
            currentEnvironment_ = top.savedEnvironment;
        top = PortableExecutionFrame();
        --executionFrameTop_;
        if (executionFrameTop_ != 0u) {
            PortableExecutionFrame& parent =
                storage_.executionFrames[executionFrameTop_ - 1u];
            parent.childValue = hasValue ? value : Value::undefined();
            parent.flags |= MachineChildReady;
        } else if (hasValue) {
            machineRootValue_ = value;
        }
    }

    void machineDiscardTop()
    {
        if (executionFrameTop_ == 0u) return;
        PortableExecutionFrame& frame =
            storage_.executionFrames[executionFrameTop_ - 1u];
        if (frame.kind == PortableExecutionFrameKind::StatementList &&
            (frame.flags & MachineRestoreEnvironment) != 0u)
            currentEnvironment_ = frame.savedEnvironment;
        frame = PortableExecutionFrame();
        --executionFrameTop_;
    }

    bool machinePropertyKey(AstNodeId keyNode, PropertyReference& reference,
        SourceLocation location)
    {
        if (!validNode(ast_, keyNode)) {
            fail(PortableExecutionErrorCode::InvalidAst, location);
            return false;
        }
        const AstNode& key = ast_.node(keyNode);
        if (key.kind == AstNodeKind::Identifier) {
            reference.key = ast_.nodeText(keyNode);
            return true;
        }
        if (key.kind != AstNodeKind::StringLiteral) {
            fail(PortableExecutionErrorCode::InvalidAst, location);
            return false;
        }
        RuntimeStringId decoded = kInvalidRuntimeStringId;
        RuntimeStringError stringError;
        if (!strings_.createDecodedStringLiteral(ast_.nodeText(keyNode),
                limits_.maxRuntimeStringLength, decoded, stringError)) {
            fail(stringError.code == RuntimeStringErrorCode::CapacityExceeded
                    ? PortableExecutionErrorCode::StringCapacityExhausted
                    : stringError.code == RuntimeStringErrorCode::StaleGeneration
                        ? PortableExecutionErrorCode::StaleReference
                        : PortableExecutionErrorCode::InternalInvariantFailure,
                location);
            return false;
        }
        if (!strings_.view(decoded, reference.key, stringError)) {
            fail(PortableExecutionErrorCode::InternalInvariantFailure,
                location);
            return false;
        }
        return true;
    }

    bool machineReadProperty(Value receiver, AstNodeId keyNode,
        bool computed, Value keyValue, SourceLocation location, Value& value)
    {
        if constexpr (!EnableObjectOperations) {
            (void)receiver;
            (void)keyNode;
            (void)computed;
            (void)keyValue;
            (void)value;
            fail(PortableExecutionErrorCode::UnsupportedOperation, location);
            return false;
        } else {
            if (objects_ == nullptr || !receiver.isObject()) {
                fail(receiver.isObject()
                        ? PortableExecutionErrorCode::UnsupportedOperation
                        : PortableExecutionErrorCode::TypeError,
                    location);
                return false;
            }
            PropertyReference reference;
            reference.object = receiver.objectId();
            if (computed) {
                if (!propertyKeyFromValue(keyValue, reference, location))
                    return false;
            } else if (!machinePropertyKey(keyNode, reference, location)) {
                return false;
            }
            bool found = false;
            PortableObjectError error;
            if (!objects_->getProperty(reference.object, reference.key, value,
                    found, error)) {
                failObject(error, location);
                return false;
            }
            return true;
        }
    }

    bool machineWriteProperty(Value receiver, AstNodeId keyNode,
        bool computed, Value keyValue, Value value, SourceLocation location)
    {
        if constexpr (!EnableObjectOperations) {
            (void)receiver;
            (void)keyNode;
            (void)computed;
            (void)keyValue;
            (void)value;
            fail(PortableExecutionErrorCode::UnsupportedOperation, location);
            return false;
        } else {
            if (objects_ == nullptr || !receiver.isObject()) {
                fail(receiver.isObject()
                        ? PortableExecutionErrorCode::UnsupportedOperation
                        : PortableExecutionErrorCode::TypeError,
                    location);
                return false;
            }
            PropertyReference reference;
            reference.object = receiver.objectId();
            if (computed) {
                if (!propertyKeyFromValue(keyValue, reference, location))
                    return false;
            } else if (!machinePropertyKey(keyNode, reference, location)) {
                return false;
            }
            PortableObjectError error;
            if (!objects_->setOwnProperty(reference.object, reference.key,
                    value, error)) {
                failObject(error, location);
                return false;
            }
            return true;
        }
    }

    bool machineApplyUnary(const AstNode& node, Value argument, Value& result)
    {
        switch (node.unaryOperator) {
        case AstUnaryOperator::LogicalNot: {
            bool value = false;
            if (!truthy(argument, value)) return false;
            result = Value::boolean(!value);
            return true;
        }
        case AstUnaryOperator::Plus:
        case AstUnaryOperator::Minus: {
            double number = 0.0;
            if (!toNumber(argument, number, node.location)) return false;
            result = Value::number(node.unaryOperator == AstUnaryOperator::Minus
                ? -number : number);
            return true;
        }
        }
        fail(PortableExecutionErrorCode::InvalidAst, node.location);
        return false;
    }

    bool machineAssignmentValue(const AstNode& node, Value oldValue,
        Value right, Value& result)
    {
        if (node.assignmentOperator == AstAssignmentOperator::Assign) {
            result = right;
            return true;
        }
        AstNode binary;
        binary.kind = AstNodeKind::BinaryExpression;
        binary.location = node.location;
        switch (node.assignmentOperator) {
        case AstAssignmentOperator::Add:
            binary.binaryOperator = AstBinaryOperator::Add;
            break;
        case AstAssignmentOperator::Subtract:
            binary.binaryOperator = AstBinaryOperator::Subtract;
            break;
        case AstAssignmentOperator::Multiply:
            binary.binaryOperator = AstBinaryOperator::Multiply;
            break;
        case AstAssignmentOperator::Divide:
            binary.binaryOperator = AstBinaryOperator::Divide;
            break;
        default:
            fail(PortableExecutionErrorCode::UnsupportedOperation,
                node.location);
            return false;
        }
        return applyBinaryValues(binary, oldValue, right, result);
    }

    bool machineGetCallable(Value value, RuntimeFunctionId& function,
        SourceLocation location)
    {
        if (!value.isFunction()) {
            fail(PortableExecutionErrorCode::InvalidInvocationTarget,
                location);
            return false;
        }
        FunctionStoreError error;
        const FunctionRecord* record = functions_.get(value.functionId(), error);
        if (record == nullptr) {
            fail(error.code == FunctionStoreErrorCode::StaleGeneration
                    ? PortableExecutionErrorCode::StaleReference
                    : error.code == FunctionStoreErrorCode::RealmMismatch
                        ? PortableExecutionErrorCode::RealmMismatch
                        : PortableExecutionErrorCode::InvalidFunctionReference,
                location);
            return false;
        }
        if (record->kind != FunctionRecord::Kind::User) {
            fail(PortableExecutionErrorCode::UnsupportedOperation, location);
            return false;
        }
        function = value.functionId();
        return true;
    }

    void machineFinishFunction(std::size_t boundaryIndex, Value value)
    {
        if (boundaryIndex >= executionFrameTop_ || callDepth_ == 0u) {
            fail(PortableExecutionErrorCode::InternalInvariantFailure,
                SourceLocation());
            return;
        }
        PortableExecutionFrame& boundary =
            storage_.executionFrames[boundaryIndex];
        const std::size_t frameIndex = boundary.callFrameIndex;
        const std::size_t firstFrameIndex = boundary.cursor;
        if (frameIndex + 1u != callDepth_ ||
            firstFrameIndex > frameIndex ||
            frameIndex >= storage_.frameCapacity) {
            fail(PortableExecutionErrorCode::InternalInvariantFailure,
                validNode(ast_, boundary.node) ? ast_.node(boundary.node).location
                    : SourceLocation());
            return;
        }
        const PortableCallFrame call = storage_.frames[firstFrameIndex];
        currentEnvironment_ = call.previousEnvironment;
        currentThis_ = call.previousThis;
        for (std::size_t index = firstFrameIndex; index < callDepth_; ++index)
            storage_.frames[index].continuationNode = kInvalidAstNodeId;
        callDepth_ = firstFrameIndex;
        argumentTop_ = boundary.mark;
        executionFrameTop_ = boundaryIndex + 1u;
        machinePop(true, value);
    }

    bool machineStartCall(PortableExecutionFrame& frame)
    {
        const AstNode& call = ast_.node(frame.node);
        if (call.childCount > limits_.maxCallArguments) {
            fail(PortableExecutionErrorCode::InvalidInvocationTarget,
                call.location);
            return false;
        }
        frame.mark = argumentTop_;
        if (argumentTop_ > storage_.argumentCapacity ||
            call.childCount > storage_.argumentCapacity - argumentTop_) {
            fail(PortableExecutionErrorCode::ArgumentCapacityExhausted,
                call.location);
            return false;
        }
        argumentTop_ += call.childCount;
        if (argumentTop_ > result_.argumentValuesHighWater)
            result_.argumentValuesHighWater = argumentTop_;
        frame.cursor = 0u;
        frame.stage = 4u;
        if (call.childCount == 0u) return machineInvokeCall(frame);
        return machinePushExpression(ast_.childAt(frame.node, 0u),
            (frame.flags & MachineInFunction) != 0u);
    }

    bool machineTailReplace(PortableExecutionFrame& callFrame,
        RuntimeFunctionId target, Value receiver)
    {
        std::size_t boundaryIndex = executionFrameTop_;
        for (std::size_t cursor = executionFrameTop_; cursor != 0u; --cursor) {
            if (storage_.executionFrames[cursor - 1u].kind ==
                    PortableExecutionFrameKind::FunctionBoundary) {
                boundaryIndex = cursor - 1u;
                break;
            }
        }
        if (boundaryIndex == executionFrameTop_ || callDepth_ == 0u) {
            fail(PortableExecutionErrorCode::InternalInvariantFailure,
                ast_.node(callFrame.node).location);
            return false;
        }
        const std::size_t oldIndex = callDepth_ - 1u;
        const PortableCallFrame oldFrame = storage_.frames[oldIndex];
        const std::size_t tailBase = callFrame.mark;
        const std::size_t argumentCount = ast_.node(callFrame.node).childCount;
        const Value* arguments = argumentCount == 0u ? nullptr
            : storage_.argumentValues + tailBase;
        // Reuse the current logical activation slot. The new activation first
        // captures the active environment; its restore destination is then
        // redirected to the original caller held by the replaced frame.
        FunctionRecord function;
        if (!pushFunctionActivation(target, arguments, argumentCount,
                ast_.node(callFrame.node).location, receiver,
                kInvalidAstNodeId, function)) return false;
        const std::size_t newIndex = callDepth_ - 1u;
        storage_.frames[newIndex].previousEnvironment =
            oldFrame.previousEnvironment;
        storage_.frames[newIndex].previousThis = oldFrame.previousThis;
        ++result_.tailCallsExecuted;
        argumentTop_ = tailBase;

        for (std::size_t index = boundaryIndex + 1u;
            index < executionFrameTop_; ++index)
            storage_.executionFrames[index] = PortableExecutionFrame();
        executionFrameTop_ = boundaryIndex + 1u;
        PortableExecutionFrame& boundary =
            storage_.executionFrames[boundaryIndex];
        boundary.stage = 0u;
        boundary.flags &= static_cast<std::uint16_t>(~MachineChildReady);
        boundary.callFrameIndex = newIndex;
        boundary.node = function.declaration;
        return true;
    }

    bool machineInvokeCall(PortableExecutionFrame& frame)
    {
        RuntimeFunctionId function = kInvalidRuntimeFunctionId;
        if (!machineGetCallable(frame.a, function,
                ast_.node(frame.node).location)) return false;
        const std::size_t count = ast_.node(frame.node).childCount;
        const Value* arguments = count == 0u ? nullptr
            : storage_.argumentValues + frame.mark;
        if ((frame.flags & MachineTailPosition) != 0u && callDepth_ != 0u)
            return machineTailReplace(frame, function, frame.b);

        FunctionRecord activated;
        if (!pushFunctionActivation(function, arguments, count,
                ast_.node(frame.node).location, frame.b,
                kInvalidAstNodeId, activated)) return false;
        const std::size_t activation = callDepth_ - 1u;
        argumentTop_ = frame.mark;
        frame.stage = 5u;
        if (!machinePush(PortableExecutionFrameKind::FunctionBoundary,
                activated.declaration, true, false)) return false;
        PortableExecutionFrame& boundary =
            storage_.executionFrames[executionFrameTop_ - 1u];
        boundary.callFrameIndex = activation;
        boundary.cursor = activation;
        boundary.mark = frame.mark;
        return true;
    }

    bool machineRunExpression(PortableExecutionFrame& frame)
    {
        const AstNode& node = ast_.node(frame.node);
        const bool inFunction = (frame.flags & MachineInFunction) != 0u;
        const bool childReady = (frame.flags & MachineChildReady) != 0u;
        Value value = Value::undefined();
        if (frame.stage == 0u) {
            if (!consumeStep(node.location)) return false;
            switch (node.kind) {
            case AstNodeKind::Identifier:
                if (!readIdentifier(frame.node, value)) return false;
                machinePop(true, value);
                return result_.error.code == PortableExecutionErrorCode::None;
            case AstNodeKind::NumericLiteral: {
                double number = 0.0;
                if (!parseNumber(ast_.nodeText(frame.node), number)) {
                    fail(PortableExecutionErrorCode::InvalidAst, node.location);
                    return false;
                }
                machinePop(true, Value::number(number));
                return result_.error.code == PortableExecutionErrorCode::None;
            }
            case AstNodeKind::StringLiteral: {
                RuntimeStringId string = kInvalidRuntimeStringId;
                RuntimeStringError error;
                if (!strings_.createDecodedStringLiteral(ast_.nodeText(frame.node),
                        limits_.maxRuntimeStringLength, string, error)) {
                    fail(error.code == RuntimeStringErrorCode::CapacityExceeded
                            ? PortableExecutionErrorCode::StringCapacityExhausted
                            : error.code == RuntimeStringErrorCode::StaleGeneration
                                ? PortableExecutionErrorCode::StaleReference
                                : PortableExecutionErrorCode::InternalInvariantFailure,
                        node.location);
                    return false;
                }
                machinePop(true, Value::string(string));
                return result_.error.code == PortableExecutionErrorCode::None;
            }
            case AstNodeKind::BooleanLiteral:
                machinePop(true, Value::boolean(
                    nameEquals(ast_.nodeText(frame.node), "true")));
                return true;
            case AstNodeKind::NullLiteral:
                machinePop(true, Value::nullValue());
                return true;
            case AstNodeKind::ThisExpression:
                machinePop(true, currentThis_);
                return true;
            case AstNodeKind::FunctionExpression:
            case AstNodeKind::ArrowFunctionExpression:
                if (!createFunction(frame.node, currentEnvironment_,
                        currentThis_, value)) return false;
                machinePop(true, value);
                return true;
            case AstNodeKind::UnaryExpression:
                frame.stage = 1u;
                return machinePushExpression(node.argument, inFunction);
            case AstNodeKind::BinaryExpression:
            case AstNodeKind::LogicalExpression:
                frame.stage = 1u;
                return machinePushExpression(node.left, inFunction);
            case AstNodeKind::AssignmentExpression:
                if (validNode(ast_, node.left) &&
                    ast_.node(node.left).kind == AstNodeKind::Identifier) {
                    frame.auxiliaryNode = node.left;
                    if (node.assignmentOperator != AstAssignmentOperator::Assign) {
                        if (!readIdentifier(node.left, frame.a)) return false;
                    }
                    frame.stage = 3u;
                    return machinePushExpression(node.right, inFunction);
                }
                if constexpr (!EnableObjectOperations) {
                    fail(PortableExecutionErrorCode::UnsupportedOperation,
                        node.location);
                    return false;
                } else {
                    if (!validNode(ast_, node.left) ||
                        ast_.node(node.left).kind != AstNodeKind::MemberExpression ||
                        node.assignmentOperator != AstAssignmentOperator::Assign) {
                        fail(PortableExecutionErrorCode::UnsupportedOperation,
                            node.location);
                        return false;
                    }
                    frame.stage = 1u;
                    return machinePushExpression(ast_.node(node.left).object,
                        inFunction);
                }
            case AstNodeKind::MemberExpression:
                if constexpr (!EnableObjectOperations) {
                    fail(PortableExecutionErrorCode::UnsupportedOperation,
                        node.location);
                    return false;
                } else {
                    frame.stage = 1u;
                    return machinePushExpression(node.object, inFunction);
                }
            case AstNodeKind::ObjectLiteral: {
                if constexpr (!EnableObjectOperations) {
                    fail(PortableExecutionErrorCode::UnsupportedOperation,
                        node.location);
                    return false;
                } else {
                    RuntimeObjectId object = kInvalidRuntimeObjectId;
                    PortableObjectError error;
                    if (objects_ == nullptr || !objects_->createObject(
                            kInvalidRuntimeObjectId, object, error)) {
                        if (objects_ == nullptr)
                            fail(PortableExecutionErrorCode::UnsupportedOperation,
                                node.location);
                        else failObject(error, node.location);
                        return false;
                    }
                    frame.a = Value::object(object);
                    frame.cursor = 0u;
                    frame.stage = 1u;
                    if (node.childCount == 0u) {
                        machinePop(true, frame.a);
                        return true;
                    }
                    const AstNodeId propertyId = ast_.childAt(frame.node, 0u);
                    if (!validNode(ast_, propertyId) ||
                        ast_.node(propertyId).kind != AstNodeKind::ObjectProperty) {
                        fail(PortableExecutionErrorCode::InvalidAst,
                            node.location);
                        return false;
                    }
                    const AstNode& property = ast_.node(propertyId);
                    if (!consumeStep(property.location)) return false;
                    frame.auxiliaryNode = propertyId;
                    return machinePushExpression(property.initializer,
                        inFunction);
                }
            }
            case AstNodeKind::ArrayLiteral: {
                if constexpr (!EnableObjectOperations) {
                    fail(PortableExecutionErrorCode::UnsupportedOperation,
                        node.location);
                    return false;
                } else {
                    RuntimeObjectId array = kInvalidRuntimeObjectId;
                    PortableObjectError error;
                    if (objects_ == nullptr || !objects_->createArray(
                            node.childCount, kInvalidRuntimeObjectId,
                            array, error)) {
                        if (objects_ == nullptr)
                            fail(PortableExecutionErrorCode::UnsupportedOperation,
                                node.location);
                        else failObject(error, node.location);
                        return false;
                    }
                    frame.a = Value::object(array);
                    frame.cursor = 0u;
                    frame.stage = 1u;
                    while (frame.cursor < node.childCount &&
                        ast_.node(ast_.childAt(frame.node, frame.cursor)).kind ==
                            AstNodeKind::ArrayElision)
                        ++frame.cursor;
                    if (frame.cursor == node.childCount) {
                        machinePop(true, frame.a);
                        return true;
                    }
                    return machinePushExpression(ast_.childAt(frame.node,
                        frame.cursor), inFunction);
                }
            }
            case AstNodeKind::CallExpression: {
                if (node.childCount > limits_.maxCallArguments) {
                    fail(PortableExecutionErrorCode::InvalidInvocationTarget,
                        node.location);
                    return false;
                }
                if (validNode(ast_, node.callee) &&
                    ast_.node(node.callee).kind == AstNodeKind::MemberExpression) {
                    if constexpr (!EnableObjectOperations) {
                        fail(PortableExecutionErrorCode::UnsupportedOperation,
                            node.location);
                        return false;
                    } else {
                        const AstNode& member = ast_.node(node.callee);
                        if (!consumeStep(member.location)) return false;
                        frame.stage = 1u;
                        return machinePushExpression(member.object, inFunction);
                    }
                }
                frame.stage = 10u;
                return machinePushExpression(node.callee, inFunction);
            }
            default:
                fail(PortableExecutionErrorCode::UnsupportedOperation,
                    node.location);
                return false;
            }
        }

        if (node.kind == AstNodeKind::UnaryExpression && frame.stage == 1u) {
            if (!childReady) {
                fail(PortableExecutionErrorCode::InternalInvariantFailure,
                    node.location);
                return false;
            }
            value = frame.childValue;
            frame.flags &= static_cast<std::uint16_t>(~MachineChildReady);
            if (!machineApplyUnary(node, value, value)) return false;
            machinePop(true, value);
            return true;
        }

        if (node.kind == AstNodeKind::BinaryExpression) {
            if (frame.stage == 1u) {
                if (!childReady) {
                    fail(PortableExecutionErrorCode::InternalInvariantFailure,
                        node.location);
                    return false;
                }
                frame.a = frame.childValue;
                frame.flags &= static_cast<std::uint16_t>(~MachineChildReady);
                frame.stage = 2u;
                return machinePushExpression(node.right, inFunction);
            }
            if (frame.stage == 2u && childReady) {
                frame.b = frame.childValue;
                frame.flags &= static_cast<std::uint16_t>(~MachineChildReady);
                if (!applyBinaryValues(node, frame.a, frame.b, value))
                    return false;
                machinePop(true, value);
                return true;
            }
        }

        if (node.kind == AstNodeKind::LogicalExpression) {
            if (frame.stage == 1u) {
                if (!childReady) {
                    fail(PortableExecutionErrorCode::InternalInvariantFailure,
                        node.location);
                    return false;
                }
                frame.a = frame.childValue;
                frame.flags &= static_cast<std::uint16_t>(~MachineChildReady);
                bool truth = false;
                if (!truthy(frame.a, truth)) return false;
                if ((node.logicalOperator == AstLogicalOperator::And && !truth) ||
                    (node.logicalOperator == AstLogicalOperator::Or && truth)) {
                    machinePop(true, frame.a);
                    return true;
                }
                frame.stage = 2u;
                return machinePushExpression(node.right, inFunction);
            }
            if (frame.stage == 2u && childReady) {
                value = frame.childValue;
                frame.flags &= static_cast<std::uint16_t>(~MachineChildReady);
                machinePop(true, value);
                return true;
            }
        }

        if (node.kind == AstNodeKind::AssignmentExpression) {
            if (frame.stage == 1u && childReady) {
                frame.b = frame.childValue;
                frame.flags &= static_cast<std::uint16_t>(~MachineChildReady);
                if (!frame.b.isObject()) {
                    fail(PortableExecutionErrorCode::TypeError,
                        ast_.node(node.left).location);
                    return false;
                }
                const AstNode& target = ast_.node(node.left);
                if (target.computed) {
                    frame.stage = 2u;
                    return machinePushExpression(target.property, inFunction);
                }
                frame.auxiliaryNode = target.property;
                frame.stage = 3u;
                return machinePushExpression(node.right, inFunction);
            }
            if (frame.stage == 2u && childReady) {
                frame.c = frame.childValue;
                frame.flags &= static_cast<std::uint16_t>(~MachineChildReady);
                frame.stage = 3u;
                return machinePushExpression(node.right, inFunction);
            }
            if (frame.stage == 3u && childReady) {
                Value right = frame.childValue;
                frame.flags &= static_cast<std::uint16_t>(~MachineChildReady);
                if (validNode(ast_, node.left) &&
                    ast_.node(node.left).kind == AstNodeKind::Identifier) {
                    SourceView name;
                    if (!nodeName(frame.auxiliaryNode, name,
                            ast_.node(frame.auxiliaryNode).location)) return false;
                    if (!machineAssignmentValue(node, frame.a, right, value))
                        return false;
                    EnvironmentError error;
                    if (!environments_.assignChain(currentEnvironment_, name,
                            value, error)) {
                        fail(mapEnvironmentError(error.code), node.location);
                        return false;
                    }
                } else {
                    value = right;
                    const AstNode& target = ast_.node(node.left);
                    if (!machineWriteProperty(frame.b, frame.auxiliaryNode,
                            target.computed, frame.c, value, node.location))
                        return false;
                }
                machinePop(true, value);
                return true;
            }
        }

        if (node.kind == AstNodeKind::MemberExpression) {
            if (frame.stage == 1u && childReady) {
                frame.a = frame.childValue;
                frame.flags &= static_cast<std::uint16_t>(~MachineChildReady);
                if (!frame.a.isObject()) {
                    fail(PortableExecutionErrorCode::TypeError,
                        node.location);
                    return false;
                }
                if (node.computed) {
                    frame.stage = 2u;
                    return machinePushExpression(node.property, inFunction);
                }
                if (!machineReadProperty(frame.a, node.property, false,
                        Value::undefined(), node.location, value)) return false;
                machinePop(true, value);
                return true;
            }
            if (frame.stage == 2u && childReady) {
                frame.b = frame.childValue;
                frame.flags &= static_cast<std::uint16_t>(~MachineChildReady);
                if (!machineReadProperty(frame.a, node.property, true,
                        frame.b, node.location, value)) return false;
                machinePop(true, value);
                return true;
            }
        }

        if (node.kind == AstNodeKind::ObjectLiteral && frame.stage == 1u) {
            if (!childReady) {
                fail(PortableExecutionErrorCode::InternalInvariantFailure,
                    node.location);
                return false;
            }
            const AstNode& property = ast_.node(frame.auxiliaryNode);
            frame.flags &= static_cast<std::uint16_t>(~MachineChildReady);
            if (!machineWriteProperty(frame.a, property.key, false,
                    Value::undefined(), frame.childValue,
                    property.location)) return false;
            ++frame.cursor;
            if (frame.cursor == node.childCount) {
                machinePop(true, frame.a);
                return true;
            }
            const AstNodeId propertyId = ast_.childAt(frame.node, frame.cursor);
            if (!validNode(ast_, propertyId) ||
                ast_.node(propertyId).kind != AstNodeKind::ObjectProperty) {
                fail(PortableExecutionErrorCode::InvalidAst, node.location);
                return false;
            }
            const AstNode& next = ast_.node(propertyId);
            if (!consumeStep(next.location)) return false;
            frame.auxiliaryNode = propertyId;
            return machinePushExpression(next.initializer, inFunction);
        }

        if (node.kind == AstNodeKind::ArrayLiteral && frame.stage == 1u &&
            childReady) {
            if constexpr (!EnableObjectOperations) {
                fail(PortableExecutionErrorCode::UnsupportedOperation,
                    node.location);
                return false;
            } else {
                const std::size_t index = frame.cursor;
                frame.flags &= static_cast<std::uint16_t>(~MachineChildReady);
                PortableObjectError error;
                if (!objects_->setArrayElement(frame.a.objectId(), index,
                        frame.childValue, error)) {
                    failObject(error,
                        ast_.node(ast_.childAt(frame.node, index)).location);
                    return false;
                }
                ++frame.cursor;
                while (frame.cursor < node.childCount &&
                    ast_.node(ast_.childAt(frame.node, frame.cursor)).kind ==
                        AstNodeKind::ArrayElision)
                    ++frame.cursor;
                if (frame.cursor == node.childCount) {
                    machinePop(true, frame.a);
                    return true;
                }
                return machinePushExpression(ast_.childAt(frame.node,
                    frame.cursor), inFunction);
            }
        }

        if (node.kind == AstNodeKind::CallExpression) {
            if (frame.stage == 1u && childReady) {
                frame.b = frame.childValue;
                frame.flags &= static_cast<std::uint16_t>(~MachineChildReady);
                if (!frame.b.isObject()) {
                    fail(PortableExecutionErrorCode::TypeError,
                        node.location);
                    return false;
                }
                const AstNode& member = ast_.node(node.callee);
                if (member.computed) {
                    frame.stage = 2u;
                    return machinePushExpression(member.property, inFunction);
                }
                frame.auxiliaryNode = member.property;
                if (!machineReadProperty(frame.b, frame.auxiliaryNode, false,
                        Value::undefined(), member.location, frame.a)) return false;
                return machineStartCall(frame);
            }
            if (frame.stage == 2u && childReady) {
                frame.c = frame.childValue;
                frame.flags &= static_cast<std::uint16_t>(~MachineChildReady);
                const AstNode& member = ast_.node(node.callee);
                if (!machineReadProperty(frame.b, member.property, true,
                        frame.c, member.location, frame.a)) return false;
                return machineStartCall(frame);
            }
            if (frame.stage == 10u && childReady) {
                frame.a = frame.childValue;
                frame.b = Value::undefined();
                frame.flags &= static_cast<std::uint16_t>(~MachineChildReady);
                return machineStartCall(frame);
            }
            if (frame.stage == 4u && childReady) {
                storage_.argumentValues[frame.mark + frame.cursor] =
                    frame.childValue;
                frame.flags &= static_cast<std::uint16_t>(~MachineChildReady);
                ++frame.cursor;
                if (frame.cursor < node.childCount)
                    return machinePushExpression(ast_.childAt(frame.node,
                        frame.cursor), inFunction);
                return machineInvokeCall(frame);
            }
            if (frame.stage == 5u && childReady) {
                value = frame.childValue;
                frame.flags &= static_cast<std::uint16_t>(~MachineChildReady);
                argumentTop_ = frame.mark;
                machinePop(true, value);
                return true;
            }
        }

        fail(PortableExecutionErrorCode::InternalInvariantFailure,
            node.location);
        return false;
    }

    bool machineRunStatement(PortableExecutionFrame& frame)
    {
        const AstNode& node = ast_.node(frame.node);
        const bool inFunction = (frame.flags & MachineInFunction) != 0u;
        const bool childReady = (frame.flags & MachineChildReady) != 0u;
        if (frame.stage == 0u) {
            if (!consumeStep(node.location)) return false;
            switch (node.kind) {
            case AstNodeKind::Program:
                frame.stage = 1u;
                return machinePushStatementList(frame.node, inFunction,
                    false);
            case AstNodeKind::BlockStatement: {
                const EnvironmentId previous = currentEnvironment_;
                EnvironmentId block = previous;
                bool restore = false;
                if (hasDirectLexicalBindings(frame.node)) {
                    EnvironmentError error;
                    if (!environments_.create(currentEnvironment_,
                            EnvironmentKind::Block, environmentLimits(),
                            block, error)) {
                        fail(mapEnvironmentError(error.code), node.location);
                        return false;
                    }
                    ++result_.environmentsCreated;
                    currentEnvironment_ = block;
                    restore = true;
                    if (!instantiateLexicalBindings(frame.node, block))
                        return false;
                }
                frame.stage = 1u;
                if (!machinePushStatementList(frame.node, inFunction, false,
                        restore)) return false;
                if (restore)
                    storage_.executionFrames[executionFrameTop_ - 1u]
                        .savedEnvironment = previous;
                return true;
            }
            case AstNodeKind::EmptyStatement:
            case AstNodeKind::FunctionDeclaration:
                machinePop(false);
                return true;
            case AstNodeKind::VariableDeclaration:
                frame.cursor = 0u;
                frame.stage = 2u;
                break;
            case AstNodeKind::ExpressionStatement:
                frame.stage = 1u;
                return machinePushExpression(node.expression, inFunction);
            case AstNodeKind::ReturnStatement:
                if (!inFunction) {
                    fail(PortableExecutionErrorCode::IllegalReturn,
                        node.location);
                    return false;
                }
                if (node.expression == kInvalidAstNodeId) {
                    machineReturning_ = true;
                    machineReturnValue_ = Value::undefined();
                    return true;
                }
                frame.stage = 1u;
                return machinePushExpression(node.expression, true,
                    ast_.node(node.expression).kind ==
                        AstNodeKind::CallExpression);
            default:
                fail(PortableExecutionErrorCode::InvalidAst, node.location);
                return false;
            }
        }
        if (node.kind == AstNodeKind::ExpressionStatement &&
            frame.stage == 1u && childReady) {
            finalValue_ = frame.childValue;
            frame.flags &= static_cast<std::uint16_t>(~MachineChildReady);
            machinePop(false);
            return true;
        }
        if (node.kind == AstNodeKind::ReturnStatement &&
            frame.stage == 1u && childReady) {
            machineReturnValue_ = frame.childValue;
            machineReturning_ = true;
            frame.flags &= static_cast<std::uint16_t>(~MachineChildReady);
            return true;
        }
        if (node.kind == AstNodeKind::VariableDeclaration && frame.stage == 2u) {
            if (childReady) {
                const AstNode& declarator = ast_.node(frame.auxiliaryNode);
                SourceView name;
                if (!nodeName(declarator.name, name, declarator.location))
                    return false;
                EnvironmentError error;
                if (!environments_.initializeChain(currentEnvironment_, name,
                        frame.childValue, error)) {
                    fail(mapEnvironmentError(error.code), declarator.location);
                    return false;
                }
                frame.flags &= static_cast<std::uint16_t>(~MachineChildReady);
                ++frame.cursor;
            }
            const std::size_t count = node.childCount;
            if (frame.cursor >= count) {
                machinePop(false);
                return true;
            }
            const AstNodeId declaratorId = ast_.childAt(frame.node,
                frame.cursor);
            if (!validNode(ast_, declaratorId) ||
                ast_.node(declaratorId).kind != AstNodeKind::VariableDeclarator) {
                fail(PortableExecutionErrorCode::InvalidAst, node.location);
                return false;
            }
            const AstNode& declarator = ast_.node(declaratorId);
            SourceView name;
            if (!nodeName(declarator.name, name, declarator.location))
                return false;
            if (declarator.initializer == kInvalidAstNodeId) {
                EnvironmentError error;
                if (!environments_.initializeChain(currentEnvironment_, name,
                        Value::undefined(), error)) {
                    fail(mapEnvironmentError(error.code), declarator.location);
                    return false;
                }
                ++frame.cursor;
                return true;
            }
            frame.auxiliaryNode = declaratorId;
            return machinePushExpression(declarator.initializer, inFunction);
        }
        if (node.kind == AstNodeKind::Program ||
            node.kind == AstNodeKind::BlockStatement) {
            if (childReady) frame.flags &= static_cast<std::uint16_t>(
                ~MachineChildReady);
            machinePop(false);
            return true;
        }
        fail(PortableExecutionErrorCode::InternalInvariantFailure,
            node.location);
        return false;
    }

    bool machineRunStatementList(PortableExecutionFrame& frame)
    {
        const AstNode& list = ast_.node(frame.node);
        if (frame.stage == 0u) {
            if ((frame.flags & MachineChargeNode) != 0u &&
                !consumeStep(list.location)) return false;
            frame.stage = 1u;
        }
        if (frame.flags & MachineChildReady)
            frame.flags &= static_cast<std::uint16_t>(~MachineChildReady);
        if (frame.cursor >= list.childCount) {
            machinePop(false);
            return true;
        }
        const AstNodeId statement = ast_.childAt(frame.node, frame.cursor++);
        return machinePushStatement(statement,
            (frame.flags & MachineInFunction) != 0u);
    }

    bool machineRunFunctionBoundary(PortableExecutionFrame& frame)
    {
        if (frame.stage == 0u) {
            if (frame.callFrameIndex >= callDepth_ ||
                frame.callFrameIndex >= storage_.frameCapacity) {
                fail(PortableExecutionErrorCode::InternalInvariantFailure,
                    SourceLocation());
                return false;
            }
            const RuntimeFunctionId functionId =
                storage_.frames[frame.callFrameIndex].function;
            FunctionStoreError error;
            const FunctionRecord* stored = functions_.get(functionId, error);
            if (stored == nullptr || !validNode(ast_, stored->declaration)) {
                fail(PortableExecutionErrorCode::InvalidFunctionReference,
                    SourceLocation());
                return false;
            }
            const FunctionRecord function = *stored;
            const AstNode& declaration = ast_.node(function.declaration);
            if (!prepareFunctionBody(function,
                    storage_.frames[frame.callFrameIndex].callSite))
                return false;
            frame.node = function.declaration;
            frame.stage = 1u;
            if (declaration.body != kInvalidAstNodeId)
                return machinePushStatementList(declaration.body, true, true);
            frame.stage = 2u;
            return machinePushExpression(declaration.expression, true,
                validNode(ast_, declaration.expression) &&
                    ast_.node(declaration.expression).kind ==
                        AstNodeKind::CallExpression);
        }
        if ((frame.flags & MachineChildReady) == 0u) {
            fail(PortableExecutionErrorCode::InternalInvariantFailure,
                validNode(ast_, frame.node) ? ast_.node(frame.node).location
                    : SourceLocation());
            return false;
        }
        const Value returned = frame.stage == 2u
            ? frame.childValue : Value::undefined();
        frame.flags &= static_cast<std::uint16_t>(~MachineChildReady);
        const std::size_t index = executionFrameTop_ - 1u;
        machineFinishFunction(index, returned);
        return result_.error.code == PortableExecutionErrorCode::None;
    }

    void machineUnwindReturn()
    {
        while (machineReturning_ && executionFrameTop_ != 0u &&
            result_.error.code == PortableExecutionErrorCode::None) {
            PortableExecutionFrame& frame =
                storage_.executionFrames[executionFrameTop_ - 1u];
            if (frame.kind == PortableExecutionFrameKind::FunctionBoundary) {
                const std::size_t index = executionFrameTop_ - 1u;
                const Value returned = machineReturnValue_;
                machineReturning_ = false;
                machineFinishFunction(index, returned);
                return;
            }
            machineDiscardTop();
        }
        if (machineReturning_ && executionFrameTop_ == 0u) {
            machineReturning_ = false;
            fail(PortableExecutionErrorCode::IllegalReturn,
                SourceLocation());
        }
    }

    void machineAbort(std::size_t oldCallDepth,
        EnvironmentId oldEnvironment, Value oldThis, std::size_t oldArgumentTop)
    {
        while (executionFrameTop_ != 0u) {
            storage_.executionFrames[executionFrameTop_ - 1u] =
                PortableExecutionFrame();
            --executionFrameTop_;
        }
        for (std::size_t index = oldCallDepth;
            index < callDepth_ && index < storage_.frameCapacity; ++index)
            storage_.frames[index].continuationNode = kInvalidAstNodeId;
        callDepth_ = oldCallDepth;
        currentEnvironment_ = oldEnvironment;
        currentThis_ = oldThis;
        argumentTop_ = oldArgumentTop;
        machineReturning_ = false;
    }

    bool runExecutionMachine(AstNodeId root, Value& result)
    {
        const std::size_t oldCallDepth = callDepth_;
        const EnvironmentId oldEnvironment = currentEnvironment_;
        const Value oldThis = currentThis_;
        const std::size_t oldArgumentTop = argumentTop_;
        executionFrameTop_ = 0u;
        machineReturning_ = false;
        machineRootValue_ = Value::undefined();
        if (!machinePushStatementList(root, false, true)) {
            machineAbort(oldCallDepth, oldEnvironment, oldThis,
                oldArgumentTop);
            return false;
        }
        while (executionFrameTop_ != 0u &&
            result_.error.code == PortableExecutionErrorCode::None) {
            if (machineReturning_) {
                machineUnwindReturn();
                continue;
            }
            PortableExecutionFrame& frame =
                storage_.executionFrames[executionFrameTop_ - 1u];
            bool ok = false;
            switch (frame.kind) {
            case PortableExecutionFrameKind::StatementList:
                ok = machineRunStatementList(frame);
                break;
            case PortableExecutionFrameKind::Statement:
                ok = machineRunStatement(frame);
                break;
            case PortableExecutionFrameKind::Expression:
                ok = machineRunExpression(frame);
                break;
            case PortableExecutionFrameKind::FunctionBoundary:
                ok = machineRunFunctionBoundary(frame);
                break;
            case PortableExecutionFrameKind::Empty:
            default:
                fail(PortableExecutionErrorCode::InternalInvariantFailure,
                    SourceLocation());
                ok = false;
                break;
            }
            if (!ok) break;
        }
        if (result_.error.code != PortableExecutionErrorCode::None ||
            executionFrameTop_ != 0u || callDepth_ != oldCallDepth ||
            machineReturning_) {
            if (result_.error.code == PortableExecutionErrorCode::None)
                fail(PortableExecutionErrorCode::InternalInvariantFailure,
                    SourceLocation());
            machineAbort(oldCallDepth, oldEnvironment, oldThis,
                oldArgumentTop);
            return false;
        }
        result = finalValue_;
        currentEnvironment_ = oldEnvironment;
        currentThis_ = oldThis;
        argumentTop_ = oldArgumentTop;
        return true;
    }

    bool executeStatementList(AstNodeId container, bool inFunction,
        Control& control)
    {
        const AstNode& list = ast_.node(container);
        for (std::size_t index = 0u; index < list.childCount; ++index) {
            if (!executeStatement(ast_.childAt(container, index), inFunction,
                    control)) return false;
            if (control.kind != ControlKind::Normal) return true;
        }
        return true;
    }

    bool executeStatement(AstNodeId id, bool inFunction, Control& control)
    {
        if (!validNode(ast_, id)) {
            fail(PortableExecutionErrorCode::InvalidAst, SourceLocation());
            return false;
        }
        const AstNode& node = ast_.node(id);
        if (!consumeStep(node.location)) return false;
        switch (node.kind) {
        case AstNodeKind::Program:
            return executeStatementList(id, inFunction, control);
        case AstNodeKind::BlockStatement: {
            const EnvironmentId previous = currentEnvironment_;
            if (hasDirectLexicalBindings(id)) {
                EnvironmentId block = kInvalidEnvironmentId;
                EnvironmentError error;
                if (!environments_.create(previous, EnvironmentKind::Block,
                        environmentLimits(), block, error)) {
                    fail(mapEnvironmentError(error.code), node.location);
                    return false;
                }
                ++result_.environmentsCreated;
                currentEnvironment_ = block;
                EnvironmentRestore restore(*this, previous);
                if (!instantiateLexicalBindings(id, block)) return false;
                return executeStatementList(id, inFunction, control);
            }
            return executeStatementList(id, inFunction, control);
        }
        case AstNodeKind::EmptyStatement:
            return true;
        case AstNodeKind::VariableDeclaration:
            return executeVariableDeclaration(id, node);
        case AstNodeKind::ExpressionStatement: {
            Value value;
            if (!evalExpression(node.expression, value)) return false;
            finalValue_ = value;
            return true;
        }
        case AstNodeKind::ReturnStatement:
            if (!inFunction) {
                fail(PortableExecutionErrorCode::IllegalReturn, node.location);
                return false;
            }
            if (node.expression != kInvalidAstNodeId &&
                validNode(ast_, node.expression) &&
                ast_.node(node.expression).kind == AstNodeKind::CallExpression)
                return evaluateTailCall(node.expression, control);
            control.value = Value::undefined();
            if (node.expression != kInvalidAstNodeId &&
                !evalExpression(node.expression, control.value)) return false;
            control.kind = ControlKind::Return;
            return true;
        case AstNodeKind::FunctionDeclaration:
            return true;
        default:
            fail(PortableExecutionErrorCode::InvalidAst, node.location);
            return false;
        }
    }

    EnvironmentLimits environmentLimits() const
    {
        return EnvironmentLimits{limits_.maxFunctionEnvironmentBindings,
            limits_.maxBindingNameLength};
    }

    bool executeVariableDeclaration(AstNodeId id, const AstNode& node)
    {
        for (std::size_t index = 0u; index < node.childCount; ++index) {
            const AstNodeId declaratorId = ast_.childAt(id, index);
            const AstNode& declarator = ast_.node(declaratorId);
            SourceView name;
            if (!nodeName(declarator.name, name, declarator.location))
                return false;
            Value value = Value::undefined();
            if (declarator.initializer != kInvalidAstNodeId &&
                !evalExpression(declarator.initializer, value)) return false;
            EnvironmentError error;
            if (!environments_.initializeChain(currentEnvironment_, name, value,
                    error)) {
                fail(mapEnvironmentError(error.code), declarator.location);
                return false;
            }
        }
        return true;
    }

    bool readIdentifier(AstNodeId id, Value& value)
    {
        SourceView name;
        if (!nodeName(id, name, ast_.node(id).location)) return false;
        EnvironmentError error;
        if (environments_.readChain(currentEnvironment_, name, value, error))
            return true;
        if (error.code == EnvironmentErrorCode::MissingBinding) {
            if (nameEquals(name, "undefined")) {
                value = Value::undefined();
                return true;
            }
            if (nameEquals(name, "Symbol")) {
                value = Value::symbolNamespace();
                return true;
            }
            if (nameEquals(name, "Object")) {
                value = Value::objectNamespace();
                return true;
            }
            if (nameEquals(name, "JSON")) {
                value = Value::jsonNamespace();
                return true;
            }
        }
        fail(mapEnvironmentError(error.code), ast_.node(id).location);
        return false;
    }

    static bool nameEquals(SourceView name, const char* text)
    {
        if (text == nullptr || name.data == nullptr) return false;
        std::size_t length = 0u;
        while (text[length] != '\0') ++length;
        if (name.length != length) return false;
        for (std::size_t index = 0u; index < length; ++index)
            if (name.data[index] != text[index]) return false;
        return true;
    }

    bool evalExpression(AstNodeId id, Value& value)
    {
        if (!validNode(ast_, id)) {
            fail(PortableExecutionErrorCode::InvalidAst, SourceLocation());
            return false;
        }
        const AstNode& node = ast_.node(id);
        if (!consumeStep(node.location)) return false;
        switch (node.kind) {
        case AstNodeKind::Identifier:
            return readIdentifier(id, value);
        case AstNodeKind::NumericLiteral: {
            double number = 0.0;
            if (!parseNumber(ast_.nodeText(id), number)) {
                fail(PortableExecutionErrorCode::InvalidAst, node.location);
                return false;
            }
            value = Value::number(number);
            return true;
        }
        case AstNodeKind::StringLiteral: {
            RuntimeStringId string = kInvalidRuntimeStringId;
            RuntimeStringError error;
            if (!strings_.createDecodedStringLiteral(ast_.nodeText(id),
                    limits_.maxRuntimeStringLength, string, error)) {
                fail(error.code == RuntimeStringErrorCode::CapacityExceeded
                        ? PortableExecutionErrorCode::StringCapacityExhausted
                        : error.code == RuntimeStringErrorCode::StaleGeneration
                            ? PortableExecutionErrorCode::StaleReference
                            : PortableExecutionErrorCode::InternalInvariantFailure,
                    node.location);
                return false;
            }
            value = Value::string(string);
            return true;
        }
        case AstNodeKind::BooleanLiteral:
            value = Value::boolean(nameEquals(ast_.nodeText(id), "true"));
            return true;
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
        case AstNodeKind::MemberExpression:
            if constexpr (EnableObjectOperations)
                return evalMemberExpression(id, node, value);
            else {
                fail(PortableExecutionErrorCode::UnsupportedOperation,
                    node.location);
                return false;
            }
        case AstNodeKind::ObjectLiteral:
            if constexpr (EnableObjectOperations)
                return evalObjectLiteral(id, node, value);
            else {
                fail(PortableExecutionErrorCode::UnsupportedOperation,
                    node.location);
                return false;
            }
        case AstNodeKind::ArrayLiteral:
            if constexpr (EnableObjectOperations)
                return evalArrayLiteral(id, node, value);
            else {
                fail(PortableExecutionErrorCode::UnsupportedOperation,
                    node.location);
                return false;
            }
        case AstNodeKind::CallExpression:
            return evalCall(id, node, value);
        case AstNodeKind::FunctionExpression:
        case AstNodeKind::ArrowFunctionExpression:
            return createFunction(id, currentEnvironment_, currentThis_, value);
        default:
            fail(PortableExecutionErrorCode::UnsupportedOperation, node.location);
            return false;
        }
    }

    bool evalMemberExpression(AstNodeId id, const AstNode& node,
        Value& value)
    {
        PropertyReference reference;
        if (!evalPropertyReference(id, reference)) return false;
        bool found = false;
        PortableObjectError error;
        if (!objects_->getProperty(reference.object, reference.key, value,
                found, error)) {
            failObject(error, node.location);
            return false;
        }
        return true;
    }

    bool evalObjectLiteral(AstNodeId id, const AstNode& node, Value& value)
    {
        if (objects_ == nullptr) {
            fail(PortableExecutionErrorCode::UnsupportedOperation,
                node.location);
            return false;
        }
        RuntimeObjectId object = kInvalidRuntimeObjectId;
        PortableObjectError error;
        if (!objects_->createObject(kInvalidRuntimeObjectId, object, error)) {
            failObject(error, node.location);
            return false;
        }
        value = Value::object(object);
        for (std::size_t index = 0u; index < node.childCount; ++index) {
            const AstNodeId propertyId = ast_.childAt(id, index);
            if (!validNode(ast_, propertyId) ||
                ast_.node(propertyId).kind != AstNodeKind::ObjectProperty) {
                fail(PortableExecutionErrorCode::InvalidAst,
                    node.location);
                return false;
            }
            const AstNode& property = ast_.node(propertyId);
            if (!consumeStep(property.location)) return false;
            SourceView key;
            const AstNode& keyNode = ast_.node(property.key);
            if (keyNode.kind == AstNodeKind::Identifier) {
                key = ast_.nodeText(property.key);
            } else if (keyNode.kind == AstNodeKind::StringLiteral) {
                RuntimeStringId decoded = kInvalidRuntimeStringId;
                RuntimeStringError stringError;
                if (!strings_.createDecodedStringLiteral(
                        ast_.nodeText(property.key),
                        limits_.maxRuntimeStringLength, decoded,
                        stringError)) {
                    fail(stringError.code ==
                            RuntimeStringErrorCode::CapacityExceeded
                            ? PortableExecutionErrorCode::StringCapacityExhausted
                            : stringError.code ==
                                RuntimeStringErrorCode::StaleGeneration
                                ? PortableExecutionErrorCode::StaleReference
                                : PortableExecutionErrorCode::InternalInvariantFailure,
                        property.location);
                    return false;
                }
                if (!strings_.view(decoded, key, stringError)) {
                    fail(PortableExecutionErrorCode::InternalInvariantFailure,
                        property.location);
                    return false;
                }
            } else {
                fail(PortableExecutionErrorCode::InvalidAst,
                    property.location);
                return false;
            }
            Value propertyValue;
            if (!evalExpression(property.initializer, propertyValue))
                return false;
            if (!objects_->setOwnProperty(object, key, propertyValue, error)) {
                failObject(error, property.location);
                return false;
            }
        }
        return true;
    }

    bool evalArrayLiteral(AstNodeId id, const AstNode& node, Value& value)
    {
        if (objects_ == nullptr) {
            fail(PortableExecutionErrorCode::UnsupportedOperation,
                node.location);
            return false;
        }
        RuntimeObjectId array = kInvalidRuntimeObjectId;
        PortableObjectError error;
        if (!objects_->createArray(node.childCount,
                kInvalidRuntimeObjectId, array, error)) {
            failObject(error, node.location);
            return false;
        }
        value = Value::object(array);
        for (std::size_t index = 0u; index < node.childCount; ++index) {
            const AstNodeId element = ast_.childAt(id, index);
            if (!validNode(ast_, element)) {
                fail(PortableExecutionErrorCode::InvalidAst,
                    node.location);
                return false;
            }
            if (ast_.node(element).kind == AstNodeKind::ArrayElision)
                continue;
            Value elementValue;
            if (!evalExpression(element, elementValue)) return false;
            if (!objects_->setArrayElement(array, index, elementValue,
                    error)) {
                failObject(error, ast_.node(element).location);
                return false;
            }
        }
        return true;
    }

    static bool parseNumber(SourceView input, double& result)
    {
        if (input.data == nullptr && input.length != 0u) return false;
        std::size_t begin = 0u;
        while (begin < input.length && isWhitespace(input.data[begin])) ++begin;
        std::size_t end = input.length;
        while (end > begin && isWhitespace(input.data[end - 1u])) --end;
        if (begin == end) { result = 0.0; return true; }
        bool negative = false;
        if (input.data[begin] == '+' || input.data[begin] == '-') {
            negative = input.data[begin] == '-';
            ++begin;
            if (begin == end) return false;
        }
        double value = 0.0;
        if (end - begin >= 2u && input.data[begin] == '0' &&
            (input.data[begin + 1u] == 'x' || input.data[begin + 1u] == 'X')) {
            begin += 2u;
            if (begin == end) return false;
            for (std::size_t index = begin; index < end; ++index) {
                const int digit = hexDigit(input.data[index]);
                if (digit < 0) return false;
                value = value * 16.0 + static_cast<double>(digit);
            }
            result = negative ? -value : value;
            return true;
        }
        bool hasDigits = false;
        while (begin < end && isDigit(input.data[begin])) {
            hasDigits = true;
            value = value * 10.0 + static_cast<double>(input.data[begin] - '0');
            ++begin;
        }
        int fractionDigits = 0;
        if (begin < end && input.data[begin] == '.') {
            ++begin;
            while (begin < end && isDigit(input.data[begin])) {
                hasDigits = true;
                value = value * 10.0 + static_cast<double>(input.data[begin] - '0');
                if (fractionDigits < 10000) ++fractionDigits;
                ++begin;
            }
        }
        if (!hasDigits) return false;
        int exponent = 0;
        int exponentSign = 1;
        if (begin < end && (input.data[begin] == 'e' || input.data[begin] == 'E')) {
            ++begin;
            if (begin < end && (input.data[begin] == '+' || input.data[begin] == '-')) {
                exponentSign = input.data[begin] == '-' ? -1 : 1;
                ++begin;
            }
            if (begin == end || !isDigit(input.data[begin])) return false;
            while (begin < end && isDigit(input.data[begin])) {
                if (exponent < 10000)
                    exponent = exponent * 10 + (input.data[begin] - '0');
                if (exponent > 10000) exponent = 10000;
                ++begin;
            }
        }
        if (begin != end) return false;
        int scale = exponentSign * exponent - fractionDigits;
        if (value != 0.0) {
            if (scale > 400) value = std::numeric_limits<double>::infinity();
            else if (scale < -400) value = 0.0;
            else while (scale > 0) { value *= 10.0; --scale; }
            if (scale < 0 && value != 0.0) {
                while (scale < 0) { value /= 10.0; ++scale; }
            }
        }
        result = negative ? -value : value;
        return true;
    }

    static bool isWhitespace(char value)
    {
        return value == ' ' || value == '\t' || value == '\n' ||
            value == '\r' || value == '\v' || value == '\f';
    }
    static bool isDigit(char value) { return value >= '0' && value <= '9'; }
    static int hexDigit(char value)
    {
        if (isDigit(value)) return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    }

    bool truthy(const Value& value, bool& result)
    {
        switch (value.type()) {
        case ValueType::Undefined:
        case ValueType::Null: result = false; return true;
        case ValueType::Boolean: result = value.booleanValue(); return true;
        case ValueType::Number: {
            const double number = value.numberValue();
            result = number != 0.0 && number == number;
            return true;
        }
        case ValueType::String: {
            SourceView text;
            RuntimeStringError error;
            if (!strings_.view(value.stringId(), text, error)) {
                fail(error.code == RuntimeStringErrorCode::StaleGeneration
                        ? PortableExecutionErrorCode::StaleReference
                        : PortableExecutionErrorCode::InvalidFunctionReference,
                    SourceLocation());
                return false;
            }
            result = text.length != 0u;
            return true;
        }
        default:
            result = true;
            return true;
        }
    }

    bool toNumber(const Value& value, double& number, SourceLocation location)
    {
        switch (value.type()) {
        case ValueType::Undefined: number = std::numeric_limits<double>::quiet_NaN(); return true;
        case ValueType::Null: number = 0.0; return true;
        case ValueType::Boolean: number = value.booleanValue() ? 1.0 : 0.0; return true;
        case ValueType::Number: number = value.numberValue(); return true;
        case ValueType::String: {
            SourceView text;
            RuntimeStringError error;
            if (!strings_.view(value.stringId(), text, error)) {
                fail(error.code == RuntimeStringErrorCode::StaleGeneration
                        ? PortableExecutionErrorCode::StaleReference
                        : PortableExecutionErrorCode::InvalidFunctionReference,
                    location);
                return false;
            }
            if (!parseNumber(text, number))
                number = std::numeric_limits<double>::quiet_NaN();
            return true;
        }
        case ValueType::Function:
        case ValueType::Object:
        case ValueType::HostObject:
            number = std::numeric_limits<double>::quiet_NaN();
            return true;
        default:
            fail(PortableExecutionErrorCode::TypeError, location);
            return false;
        }
    }

    bool strictEqual(const Value& left, const Value& right, bool& equal,
        SourceLocation location)
    {
        equal = false;
        if (left.type() != right.type()) return true;
        switch (left.type()) {
        case ValueType::Undefined:
        case ValueType::Null: equal = true; return true;
        case ValueType::Boolean: equal = left.booleanValue() == right.booleanValue(); return true;
        case ValueType::Number: {
            const double a = left.numberValue();
            const double b = right.numberValue();
            equal = a == b && a == a && b == b;
            return true;
        }
        case ValueType::String: {
            SourceView a;
            SourceView b;
            RuntimeStringError error;
            if (!strings_.view(left.stringId(), a, error) ||
                !strings_.view(right.stringId(), b, error)) {
                fail(error.code == RuntimeStringErrorCode::StaleGeneration
                        ? PortableExecutionErrorCode::StaleReference
                        : PortableExecutionErrorCode::InvalidFunctionReference,
                    location);
                return false;
            }
            equal = equalName(a, b);
            return true;
        }
        case ValueType::Function:
            equal = left.functionId() == right.functionId(); return true;
        case ValueType::Object:
            equal = left.objectId() == right.objectId(); return true;
        case ValueType::HostObject:
            equal = left.hostObjectId() == right.hostObjectId(); return true;
        default:
            equal = true;
            return true;
        }
    }

    bool looseEqual(const Value& left, const Value& right, bool& equal,
        SourceLocation location)
    {
        if ((left.isUndefined() || left.isNull()) &&
            (right.isUndefined() || right.isNull())) { equal = true; return true; }
        if (left.type() == right.type())
            return strictEqual(left, right, equal, location);
        if (left.isBoolean() || right.isBoolean() ||
            (left.isNumber() && right.isString()) ||
            (left.isString() && right.isNumber())) {
            double a = 0.0;
            double b = 0.0;
            if (!toNumber(left, a, location) || !toNumber(right, b, location))
                return false;
            equal = a == b && a == a && b == b;
            return true;
        }
        equal = false;
        return true;
    }

    bool evalUnary(const AstNode& node, Value& result)
    {
        Value argument;
        if (!evalExpression(node.argument, argument)) return false;
        switch (node.unaryOperator) {
        case AstUnaryOperator::LogicalNot: {
            bool value = false;
            if (!truthy(argument, value)) return false;
            result = Value::boolean(!value);
            return true;
        }
        case AstUnaryOperator::Plus:
        case AstUnaryOperator::Minus: {
            double number = 0.0;
            if (!toNumber(argument, number, node.location)) return false;
            result = Value::number(node.unaryOperator == AstUnaryOperator::Minus
                ? -number : number);
            return true;
        }
        }
        fail(PortableExecutionErrorCode::InvalidAst, node.location);
        return false;
    }

    bool evalBinary(const AstNode& node, Value& result)
    {
        Value left;
        Value right;
        if (!evalExpression(node.left, left) ||
            !evalExpression(node.right, right)) return false;
        return applyBinaryValues(node, left, right, result);
    }

    bool applyBinaryValues(const AstNode& node, Value left, Value right,
        Value& result)
    {
        if (node.binaryOperator == AstBinaryOperator::Add) {
            if (left.isString() || right.isString()) {
                fail(PortableExecutionErrorCode::UnsupportedOperation,
                    node.location);
                return false;
            }
            double a = 0.0;
            double b = 0.0;
            if (!toNumber(left, a, node.location) ||
                !toNumber(right, b, node.location)) return false;
            result = Value::number(a + b);
            return true;
        }
        if (node.binaryOperator == AstBinaryOperator::StrictEqual ||
            node.binaryOperator == AstBinaryOperator::StrictNotEqual ||
            node.binaryOperator == AstBinaryOperator::Equal ||
            node.binaryOperator == AstBinaryOperator::NotEqual) {
            bool equal = false;
            const bool strict = node.binaryOperator == AstBinaryOperator::StrictEqual ||
                node.binaryOperator == AstBinaryOperator::StrictNotEqual;
            if (!(strict ? strictEqual(left, right, equal, node.location)
                    : looseEqual(left, right, equal, node.location))) return false;
            const bool invert = node.binaryOperator == AstBinaryOperator::StrictNotEqual ||
                node.binaryOperator == AstBinaryOperator::NotEqual;
            result = Value::boolean(invert ? !equal : equal);
            return true;
        }
        double a = 0.0;
        double b = 0.0;
        if (!toNumber(left, a, node.location) ||
            !toNumber(right, b, node.location)) return false;
        switch (node.binaryOperator) {
        case AstBinaryOperator::Subtract: result = Value::number(a - b); return true;
        case AstBinaryOperator::Multiply: result = Value::number(a * b); return true;
        case AstBinaryOperator::Divide:
            if (b == 0.0) {
                if (a == 0.0 || a != a) result = Value::number(std::numeric_limits<double>::quiet_NaN());
                else result = Value::number((a < 0.0) != (b < 0.0)
                    ? -std::numeric_limits<double>::infinity()
                    : std::numeric_limits<double>::infinity());
            } else result = Value::number(a / b);
            return true;
        case AstBinaryOperator::Less:
        case AstBinaryOperator::LessEqual:
        case AstBinaryOperator::Greater:
        case AstBinaryOperator::GreaterEqual: {
            bool comparison = false;
            if (a == a && b == b) {
                if (node.binaryOperator == AstBinaryOperator::Less) comparison = a < b;
                else if (node.binaryOperator == AstBinaryOperator::LessEqual) comparison = a <= b;
                else if (node.binaryOperator == AstBinaryOperator::Greater) comparison = a > b;
                else comparison = a >= b;
            }
            result = Value::boolean(comparison);
            return true;
        }
        default:
            fail(PortableExecutionErrorCode::UnsupportedOperation,
                node.location);
            return false;
        }
    }

    bool evalLogical(const AstNode& node, Value& result)
    {
        Value left;
        if (!evalExpression(node.left, left)) return false;
        bool value = false;
        if (!truthy(left, value)) return false;
        if ((node.logicalOperator == AstLogicalOperator::And && !value) ||
            (node.logicalOperator == AstLogicalOperator::Or && value)) {
            result = left;
            return true;
        }
        return evalExpression(node.right, result);
    }

    bool evalAssignment(const AstNode& node, Value& result)
    {
        if constexpr (EnableObjectOperations) {
            if (validNode(ast_, node.left) &&
                ast_.node(node.left).kind == AstNodeKind::MemberExpression) {
                PropertyReference reference;
                if (!evalPropertyReference(node.left, reference)) return false;
                Value right;
                if (!evalExpression(node.right, right)) return false;
                PortableObjectError objectError;
                if (!objects_->setOwnProperty(reference.object, reference.key,
                        right, objectError)) {
                    failObject(objectError, node.location);
                    return false;
                }
                result = right;
                return true;
            }
        }
        SourceView name;
        if (!nodeName(node.left, name, ast_.node(node.left).location))
            return false;
        Value right;
        if (node.assignmentOperator == AstAssignmentOperator::Assign) {
            if (!evalExpression(node.right, right)) return false;
        } else {
            Value left;
            EnvironmentError envError;
            if (!environments_.readChain(currentEnvironment_, name, left,
                    envError)) {
                fail(mapEnvironmentError(envError.code),
                    ast_.node(node.left).location);
                return false;
            }
            if (!evalExpression(node.right, right)) return false;
            if (node.assignmentOperator == AstAssignmentOperator::Add) {
                if (left.isString() || right.isString()) {
                    fail(PortableExecutionErrorCode::UnsupportedOperation,
                        node.location);
                    return false;
                }
                double a = 0.0;
                double b = 0.0;
                if (!toNumber(left, a, node.location) ||
                    !toNumber(right, b, node.location)) return false;
                right = Value::number(a + b);
            } else {
                double a = 0.0;
                double b = 0.0;
                if (!toNumber(left, a, node.location) ||
                    !toNumber(right, b, node.location)) return false;
                if (node.assignmentOperator == AstAssignmentOperator::Subtract)
                    right = Value::number(a - b);
                else if (node.assignmentOperator == AstAssignmentOperator::Multiply)
                    right = Value::number(a * b);
                else if (node.assignmentOperator == AstAssignmentOperator::Divide)
                    right = Value::number(a / b);
                else {
                    fail(PortableExecutionErrorCode::UnsupportedOperation,
                        node.location);
                    return false;
                }
            }
        }
        EnvironmentError error;
        if (!environments_.assignChain(currentEnvironment_, name, right, error)) {
            fail(mapEnvironmentError(error.code), ast_.node(node.left).location);
            return false;
        }
        result = right;
        return true;
    }

    bool evalCall(AstNodeId callId, const AstNode& node, Value& result)
    {
        AstNodeId continuationNode = kInvalidAstNodeId;
        if (callDepth_ != 0u &&
            matchesSelfArithmeticContinuation(callId,
                continuationNode)) {
            return invokeSelfArithmeticContinuation(callId,
                continuationNode, result);
        }
        Value callee;
        std::size_t mark = 0u;
        if (!evaluateCallOperands(callId, node, callee, mark)) return false;
        if (!callee.isFunction()) {
            argumentTop_ = mark;
            fail(PortableExecutionErrorCode::InvalidInvocationTarget,
                node.location);
            return false;
        }
        const bool invoked = invokeFunction(callee.functionId(),
            storage_.argumentValues == nullptr ? nullptr
                : storage_.argumentValues + mark,
            node.childCount, node.location, result);
        argumentTop_ = mark;
        return invoked;
    }

    static bool sameIdentifier(const Ast& ast, AstNodeId left,
        AstNodeId right)
    {
        if (!validNode(ast, left) || !validNode(ast, right) ||
            ast.node(left).kind != AstNodeKind::Identifier ||
            ast.node(right).kind != AstNodeKind::Identifier) return false;
        const SourceView leftName = ast.nodeText(left);
        const SourceView rightName = ast.nodeText(right);
        return equalName(leftName, rightName);
    }

    AstNodeId functionReturnExpression(const AstNode& declaration) const
    {
        if (declaration.body == kInvalidAstNodeId)
            return declaration.expression;
        if (!validNode(ast_, declaration.body) ||
            ast_.node(declaration.body).kind != AstNodeKind::BlockStatement ||
            ast_.node(declaration.body).childCount != 1u)
            return kInvalidAstNodeId;
        const AstNodeId statement = ast_.childAt(declaration.body, 0u);
        if (!validNode(ast_, statement) ||
            ast_.node(statement).kind != AstNodeKind::ReturnStatement)
            return kInvalidAstNodeId;
        return ast_.node(statement).expression;
    }

    bool matchesSelfArithmeticContinuation(AstNodeId callId,
        AstNodeId& continuationNode) const
    {
        continuationNode = kInvalidAstNodeId;
        if (callDepth_ == 0u || storage_.frames == nullptr ||
            !validNode(ast_, callId) ||
            ast_.node(callId).kind != AstNodeKind::CallExpression)
            return false;
        const PortableCallFrame& active = storage_.frames[callDepth_ - 1u];
        FunctionStoreError functionError;
        const FunctionRecord* function = functions_.get(active.function,
            functionError);
        if (function == nullptr ||
            function->kind != FunctionRecord::Kind::User ||
            !validNode(ast_, function->declaration) ||
            !simpleParameters(ast_, function->declaration)) return false;
        const AstNode& declaration = ast_.node(function->declaration);
        if (declaration.childCount != 1u) return false;
        const AstNodeId parameter = ast_.childAt(function->declaration, 0u);
        if (!validNode(ast_, parameter) ||
            ast_.node(parameter).kind != AstNodeKind::Identifier) return false;
        const AstNodeId expression = functionReturnExpression(declaration);
        if (!validNode(ast_, expression) ||
            ast_.node(expression).kind != AstNodeKind::LogicalExpression)
            return false;
        const AstNode& logical = ast_.node(expression);
        if ((logical.logicalOperator != AstLogicalOperator::And &&
                logical.logicalOperator != AstLogicalOperator::Or) ||
            !sameIdentifier(ast_, logical.left, parameter) ||
            !validNode(ast_, logical.right) ||
            ast_.node(logical.right).kind != AstNodeKind::BinaryExpression)
            return false;
        const AstNodeId binaryId = logical.right;
        const AstNode& binary = ast_.node(binaryId);
        if (binary.left != callId ||
            (binary.binaryOperator != AstBinaryOperator::Add &&
                binary.binaryOperator != AstBinaryOperator::Subtract &&
                binary.binaryOperator != AstBinaryOperator::Multiply &&
                binary.binaryOperator != AstBinaryOperator::Divide) ||
            !validNode(ast_, binary.right) ||
            ast_.node(binary.right).kind != AstNodeKind::NumericLiteral)
            return false;
        const AstNode& call = ast_.node(callId);
        if (!validNode(ast_, call.callee) ||
            ast_.node(call.callee).kind != AstNodeKind::Identifier ||
            call.childCount != 1u) return false;
        const AstNodeId argumentId = ast_.childAt(callId, 0u);
        if (!validNode(ast_, argumentId) ||
            ast_.node(argumentId).kind != AstNodeKind::BinaryExpression)
            return false;
        const AstNode& argument = ast_.node(argumentId);
        if (argument.binaryOperator != AstBinaryOperator::Subtract ||
            !sameIdentifier(ast_, argument.left, parameter) ||
            !validNode(ast_, argument.right) ||
            ast_.node(argument.right).kind != AstNodeKind::NumericLiteral)
            return false;
        continuationNode = binaryId;
        return true;
    }

    bool pushFunctionActivation(RuntimeFunctionId id,
        const Value* arguments, std::size_t argumentCount,
        SourceLocation callSite, Value receiver,
        AstNodeId continuationNode, FunctionRecord& function)
    {
        if (argumentCount != 0u && arguments == nullptr) {
            fail(PortableExecutionErrorCode::InternalInvariantFailure,
                callSite);
            return false;
        }
        if (argumentCount > limits_.maxCallArguments) {
            fail(PortableExecutionErrorCode::InvalidInvocationTarget,
                callSite);
            return false;
        }
        FunctionStoreError functionError;
        const FunctionRecord* stored = functions_.get(id, functionError);
        if (stored == nullptr) {
            fail(functionError.code == FunctionStoreErrorCode::StaleGeneration
                    ? PortableExecutionErrorCode::StaleReference
                    : functionError.code == FunctionStoreErrorCode::RealmMismatch
                        ? PortableExecutionErrorCode::RealmMismatch
                        : PortableExecutionErrorCode::InvalidFunctionReference,
                callSite);
            return false;
        }
        function = *stored;
        if (function.kind != FunctionRecord::Kind::User) {
            fail(function.kind == FunctionRecord::Kind::Native ||
                    function.kind == FunctionRecord::Kind::Host
                    ? PortableExecutionErrorCode::UnsupportedOperation
                    : PortableExecutionErrorCode::InvalidFunctionReference,
                callSite);
            return false;
        }
        if (!validNode(ast_, function.declaration)) {
            fail(PortableExecutionErrorCode::InvalidFunctionReference,
                callSite);
            return false;
        }
        if (callDepth_ >= limits_.maxCallDepth ||
            callDepth_ >= storage_.frameCapacity) {
            fail(PortableExecutionErrorCode::CallDepthExhausted, callSite);
            return false;
        }
        const AstNode& declaration = ast_.node(function.declaration);
        const bool arrow = declaration.kind ==
            AstNodeKind::ArrowFunctionExpression;
        if ((declaration.kind != AstNodeKind::FunctionDeclaration &&
                declaration.kind != AstNodeKind::FunctionExpression && !arrow) ||
            function.arrowFunction != arrow ||
            !simpleParameters(ast_, function.declaration)) {
            fail(PortableExecutionErrorCode::InvalidFunctionReference,
                callSite);
            return false;
        }
        EnvironmentError environmentError;
        if (!environments_.valid(function.closureEnvironment,
                environmentError)) {
            fail(mapEnvironmentError(environmentError.code), callSite);
            return false;
        }

        PortableCallFrame& frame = storage_.frames[callDepth_];
        frame.function = id;
        frame.previousEnvironment = currentEnvironment_;
        frame.previousThis = currentThis_;
        frame.callSite = callSite;
        frame.argumentMark = argumentTop_;
        frame.continuationNode = continuationNode;
        ++callDepth_;
        if (callDepth_ > result_.callDepthHighWater)
            result_.callDepthHighWater = callDepth_;

        EnvironmentId callEnvironment = kInvalidEnvironmentId;
        if (!environments_.create(function.closureEnvironment,
                EnvironmentKind::Function, environmentLimits(),
                callEnvironment, environmentError)) {
            fail(mapEnvironmentError(environmentError.code), callSite);
            return false;
        }
        ++result_.environmentsCreated;
        frame.environment = callEnvironment;
        currentEnvironment_ = callEnvironment;
        currentThis_ = arrow ? function.lexicalThis : receiver;
        for (std::size_t index = 0u; index < declaration.childCount; ++index) {
            const AstNodeId parameter = ast_.childAt(function.declaration,
                index);
            SourceView parameterName;
            if (!nodeName(parameter, parameterName,
                    ast_.node(parameter).location)) return false;
            const Value argument = index < argumentCount
                ? arguments[index] : Value::undefined();
            if (!environments_.declare(callEnvironment, parameterName,
                    argument, environmentError)) {
                fail(mapBindingError(environmentError.code),
                    ast_.node(parameter).location);
                return false;
            }
        }
        return true;
    }

    bool prepareFunctionBody(const FunctionRecord& function,
        SourceLocation callSite)
    {
        if (function.declaration == kInvalidAstNodeId ||
            !validNode(ast_, function.declaration)) {
            fail(PortableExecutionErrorCode::InvalidFunctionReference,
                callSite);
            return false;
        }
        const AstNode& declaration = ast_.node(function.declaration);
        if (declaration.body == kInvalidAstNodeId) return true;
        if (!instantiateFunctions(declaration.body, currentEnvironment_) ||
            !instantiateLexicalBindings(declaration.body, currentEnvironment_))
            return false;
        return true;
    }

    bool invokeSelfArithmeticContinuation(AstNodeId firstCallId,
        AstNodeId firstContinuation, Value& result)
    {
        const std::size_t startDepth = callDepth_;
        const EnvironmentId previousEnvironment = currentEnvironment_;
        const Value previousThis = currentThis_;
        const std::size_t previousArgumentTop = argumentTop_;
        CallRestore restore(*this, previousEnvironment, previousThis,
            previousArgumentTop, startDepth);
        const RuntimeFunctionId recursiveFunction =
            storage_.frames[startDepth - 1u].function;
        FunctionStoreError initialFunctionError;
        const FunctionRecord* initialFunction = functions_.get(
            recursiveFunction, initialFunctionError);
        if (initialFunction == nullptr ||
            !validNode(ast_, initialFunction->declaration)) {
            fail(PortableExecutionErrorCode::InvalidFunctionReference,
                ast_.node(firstCallId).location);
            return false;
        }
        const AstNodeId initialReturn = functionReturnExpression(
            ast_.node(initialFunction->declaration));
        if (!validNode(ast_, initialReturn) ||
            ast_.node(initialReturn).kind != AstNodeKind::LogicalExpression ||
            ast_.node(initialReturn).right != firstContinuation) {
            fail(PortableExecutionErrorCode::InternalInvariantFailure,
                ast_.node(firstCallId).location);
            return false;
        }
        const AstNode& firstCall = ast_.node(firstCallId);
        Value firstCallee;
        std::size_t firstMark = 0u;
        if (!evaluateCallOperands(firstCallId, firstCall, firstCallee,
                firstMark)) return false;
        if (!firstCallee.isFunction()) {
            argumentTop_ = firstMark;
            fail(PortableExecutionErrorCode::InvalidInvocationTarget,
                firstCall.location);
            return false;
        }
        const Value firstArgument = storage_.argumentValues[firstMark];
        argumentTop_ = firstMark;
        if (firstCallee.functionId() != recursiveFunction) {
            return invokeFunction(firstCallee.functionId(), &firstArgument,
                1u, firstCall.location, result);
        }

        RuntimeFunctionId pendingFunction = firstCallee.functionId();
        Value pendingArgument = firstArgument;
        SourceLocation pendingCallSite = firstCall.location;
        AstNodeId incomingContinuation = kInvalidAstNodeId;
        Value returned = Value::undefined();

        while (true) {
            FunctionRecord function;
            if (!pushFunctionActivation(pendingFunction, &pendingArgument, 1u,
                    pendingCallSite, Value::undefined(),
                    incomingContinuation, function) ||
                !prepareFunctionBody(function, pendingCallSite)) return false;
            if (pendingFunction != recursiveFunction) {
                fail(PortableExecutionErrorCode::InternalInvariantFailure,
                    pendingCallSite);
                return false;
            }
            const AstNode& declaration = ast_.node(function.declaration);
            const AstNodeId expression = functionReturnExpression(declaration);
            const AstNode& logical = ast_.node(expression);
            if (declaration.body != kInvalidAstNodeId) {
                if (!consumeStep(ast_.node(declaration.body).location) ||
                    !consumeStep(ast_.node(ast_.childAt(
                        declaration.body, 0u)).location)) return false;
            }
            if (!consumeStep(logical.location)) return false;
            Value test;
            if (!evalExpression(logical.left, test)) return false;
            bool testTruthy = false;
            if (!truthy(test, testTruthy)) return false;
            const bool shouldRecurse = logical.logicalOperator ==
                    AstLogicalOperator::And
                ? testTruthy : !testTruthy;
            if (!shouldRecurse) {
                returned = test;
                break;
            }

            const AstNodeId binaryId = logical.right;
            const AstNode& binary = ast_.node(binaryId);
            const AstNodeId callId = binary.left;
            if (!consumeStep(binary.location) ||
                !consumeStep(ast_.node(callId).location)) return false;
            Value nextCallee;
            std::size_t nextMark = 0u;
            if (!evaluateCallOperands(callId, ast_.node(callId), nextCallee,
                    nextMark)) return false;
            if (!nextCallee.isFunction()) {
                argumentTop_ = nextMark;
                fail(PortableExecutionErrorCode::InvalidInvocationTarget,
                    ast_.node(callId).location);
                return false;
            }
            const Value nextArgument = storage_.argumentValues[nextMark];
            const RuntimeFunctionId nextFunction = nextCallee.functionId();
            argumentTop_ = nextMark;
            if (nextFunction != recursiveFunction) {
                if (!invokeFunction(nextFunction, &nextArgument, 1u,
                        ast_.node(callId).location, returned)) return false;
                Value right;
                if (!evalExpression(binary.right, right) ||
                    !applyBinaryValues(binary, returned, right, returned))
                    return false;
                break;
            }
            pendingFunction = nextFunction;
            pendingArgument = nextArgument;
            pendingCallSite = ast_.node(callId).location;
            incomingContinuation = binaryId;
        }

        while (callDepth_ > startDepth) {
            const std::size_t childIndex = callDepth_ - 1u;
            const AstNodeId pending =
                storage_.frames[childIndex].continuationNode;
            const EnvironmentId parentEnvironment =
                storage_.frames[childIndex].previousEnvironment;
            const Value parentThis = storage_.frames[childIndex].previousThis;
            storage_.frames[childIndex].continuationNode =
                kInvalidAstNodeId;
            --callDepth_;
            currentEnvironment_ = parentEnvironment;
            currentThis_ = parentThis;
            if (pending == kInvalidAstNodeId) {
                if (callDepth_ != startDepth) {
                    fail(PortableExecutionErrorCode::InternalInvariantFailure,
                        ast_.node(firstCallId).location);
                    return false;
                }
                result = returned;
                return true;
            }
            if (!validNode(ast_, pending) ||
                ast_.node(pending).kind != AstNodeKind::BinaryExpression) {
                fail(PortableExecutionErrorCode::InternalInvariantFailure,
                    ast_.node(firstCallId).location);
                return false;
            }
            const AstNode& continuation = ast_.node(pending);
            Value right;
            if (!evalExpression(continuation.right, right) ||
                !applyBinaryValues(continuation, returned, right, returned))
                return false;
        }
        fail(PortableExecutionErrorCode::InternalInvariantFailure,
            ast_.node(firstCallId).location);
        return false;
    }

    bool evaluateCallOperands(AstNodeId callId, const AstNode& node,
        Value& callee, std::size_t& mark)
    {
        if (node.childCount > limits_.maxCallArguments) {
            fail(PortableExecutionErrorCode::InvalidInvocationTarget,
                node.location);
            return false;
        }
        mark = argumentTop_;
        if (!evalExpression(node.callee, callee)) return false;
        if (argumentTop_ > storage_.argumentCapacity ||
            node.childCount > storage_.argumentCapacity - argumentTop_) {
            fail(PortableExecutionErrorCode::ArgumentCapacityExhausted,
                node.location);
            return false;
        }
        argumentTop_ += node.childCount;
        if (argumentTop_ > result_.argumentValuesHighWater)
            result_.argumentValuesHighWater = argumentTop_;
        for (std::size_t index = 0u; index < node.childCount; ++index) {
            Value argument;
            if (!evalExpression(ast_.childAt(callId, index), argument)) {
                argumentTop_ = mark;
                return false;
            }
            storage_.argumentValues[mark + index] = argument;
        }
        return true;
    }

    bool evaluateTailCall(AstNodeId callId, Control& control)
    {
        const AstNode& call = ast_.node(callId);
        if (!consumeStep(call.location)) return false;
        Value callee;
        std::size_t mark = 0u;
        if (!evaluateCallOperands(callId, call, callee, mark)) return false;
        if (!callee.isFunction()) {
            argumentTop_ = mark;
            fail(PortableExecutionErrorCode::InvalidInvocationTarget,
                call.location);
            return false;
        }
        control.kind = ControlKind::TailCall;
        control.tailFunction = callee.functionId();
        control.tailReceiver = Value::undefined();
        control.tailArgumentMark = mark;
        control.tailArgumentCount = call.childCount;
        control.tailCallSite = call.location;
        return true;
    }

    bool createFunction(AstNodeId declaration, EnvironmentId closure,
        Value lexicalThis, Value& value)
    {
        if (result_.functionsCreated >= limits_.maxFunctions) {
            fail(PortableExecutionErrorCode::FunctionCapacityExhausted,
                ast_.node(declaration).location);
            return false;
        }
        const AstNode& node = ast_.node(declaration);
        FunctionRecord record;
        record.kind = FunctionRecord::Kind::User;
        record.declaration = declaration;
        record.closureEnvironment = closure;
        record.arrowFunction = node.kind == AstNodeKind::ArrowFunctionExpression;
        record.lexicalThis = lexicalThis;
        RuntimeFunctionId function = kInvalidRuntimeFunctionId;
        FunctionStoreError error;
        if (!functions_.append(record, function, error)) {
            fail(error.code == FunctionStoreErrorCode::CapacityExceeded
                    ? PortableExecutionErrorCode::FunctionCapacityExhausted
                    : error.code == FunctionStoreErrorCode::StaleGeneration
                        ? PortableExecutionErrorCode::StaleReference
                        : PortableExecutionErrorCode::InternalInvariantFailure,
                node.location);
            return false;
        }
        ++result_.functionsCreated;
        value = Value::function(function);
        return true;
    }

    bool invokeFunction(RuntimeFunctionId id, const Value* arguments,
        std::size_t argumentCount, SourceLocation callSite, Value& result,
        Value receiver = Value::undefined())
    {
        const EnvironmentId previousEnvironment = currentEnvironment_;
        const Value previousThis = currentThis_;
        const std::size_t previousArgumentTop = argumentTop_;
        CallRestore restore(*this, previousEnvironment, previousThis,
            previousArgumentTop, callDepth_);
        const std::size_t tailArgumentBase = previousArgumentTop;
        RuntimeFunctionId currentId = id;
        const Value* currentArguments = arguments;
        std::size_t currentArgumentCount = argumentCount;
        SourceLocation currentCallSite = callSite;
        Value currentReceiver = receiver;
        bool tailTransition = false;

        while (true) {
            FunctionRecord function;
            if (!pushFunctionActivation(currentId, currentArguments,
                    currentArgumentCount, currentCallSite, currentReceiver,
                    kInvalidAstNodeId, function)) return false;
            const AstNode& declaration = ast_.node(function.declaration);
            if (tailTransition) ++result_.tailCallsExecuted;
            // Incoming values have been copied into this activation's
            // environment. Reuse one caller-owned argument window for any
            // subsequent tail transfer instead of growing it with depth.
            argumentTop_ = tailArgumentBase;

            Control control;
            if (declaration.body != kInvalidAstNodeId) {
                if (!prepareFunctionBody(function, currentCallSite))
                    return false;
                if (!consumeStep(ast_.node(declaration.body).location))
                    return false;
                if (!executeStatementList(declaration.body, true, control))
                    return false;
            } else if (validNode(ast_, declaration.expression) &&
                ast_.node(declaration.expression).kind ==
                    AstNodeKind::CallExpression) {
                if (!evaluateTailCall(declaration.expression, control))
                    return false;
            } else if (!evalExpression(declaration.expression, control.value)) {
                return false;
            } else {
                result = control.value;
                return true;
            }

            if (control.kind == ControlKind::Return) {
                result = control.value;
                return true;
            }
            if (control.kind != ControlKind::TailCall) {
                result = Value::undefined();
                return true;
            }
            tailTransition = true;
            currentId = control.tailFunction;
            currentReceiver = control.tailReceiver;
            currentCallSite = control.tailCallSite;
            currentArgumentCount = control.tailArgumentCount;
            currentArguments = currentArgumentCount == 0u
                ? nullptr : storage_.argumentValues +
                    control.tailArgumentMark;
        }
    }

    Ast& ast_;
    RuntimeStringStore& strings_;
    EnvironmentStore& environments_;
    FunctionStore& functions_;
    PortableObjectStore* objects_ = nullptr;
    PortableExecutionStorageView storage_;
    PortableExecutionLimits limits_;
    std::size_t& sharedExecutionSteps_;
    PortableExecutionResult result_;
    EnvironmentId currentEnvironment_ = kInvalidEnvironmentId;
    EnvironmentId globalEnvironment_ = kInvalidEnvironmentId;
    Value currentThis_ = Value::undefined();
    Value finalValue_ = Value::undefined();
    std::size_t callDepth_ = 0u;
    std::size_t argumentTop_ = 0u;
    std::size_t executionFrameTop_ = 0u;
    bool machineReturning_ = false;
    Value machineReturnValue_ = Value::undefined();
    Value machineRootValue_ = Value::undefined();
};

using PortableExecutionCore = PortableExecutionCoreImpl<false>;
using PortableObjectExecutionCore = PortableExecutionCoreImpl<true>;

} // namespace javascript
} // namespace gxos
