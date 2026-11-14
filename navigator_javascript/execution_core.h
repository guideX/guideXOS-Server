#pragma once

#include "ast.h"
#include "environment.h"
#include "function_storage.h"
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
    EnvironmentCapacityExhausted,
    BindingCapacityExhausted,
    FunctionCapacityExhausted,
    StringCapacityExhausted,
    ArgumentCapacityExhausted,
    StepBudgetExhausted,
    CallDepthExhausted,
    AllocationFailure,
    IllegalReturn,
    InternalInvariantFailure,
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
};

struct PortableExecutionLimits {
    std::size_t maxCallDepth = 64u;
    std::size_t maxCallArguments = 64u;
    std::size_t maxFunctions = 4096u;
    std::size_t maxExecutionSteps = 100000u;
    std::size_t maxFunctionEnvironmentBindings = 256u;
    std::size_t maxBindingNameLength = 256u;
    std::size_t maxRuntimeStringLength = 64u * 1024u;
    std::size_t maxAstDepth = 256u;
};

struct PortableCallFrame {
    RuntimeFunctionId function = kInvalidRuntimeFunctionId;
    EnvironmentId environment = kInvalidEnvironmentId;
    EnvironmentId previousEnvironment = kInvalidEnvironmentId;
    Value previousThis = Value::undefined();
    SourceLocation callSite;
    std::size_t argumentMark = 0u;
};

struct PortableExecutionStorageView {
    PortableCallFrame* frames = nullptr;
    std::size_t frameCapacity = 0u;
    Value* argumentValues = nullptr;
    std::size_t argumentCapacity = 0u;
};

// Shared evaluator for the initial portable JavaScript execution slice. It
// consumes the same AST, Value, RuntimeStringStore and EnvironmentStore as
// RuntimeContext. Persistent storage and invocation scratch are caller-owned;
// this class never allocates and does not depend on the hosted runtime.
class PortableExecutionCore {
public:
    PortableExecutionCore(Ast& ast, RuntimeStringStore& strings,
        EnvironmentStore& environments, FunctionStore& functions,
        PortableExecutionStorageView storage,
        PortableExecutionLimits limits, std::size_t& sharedExecutionSteps)
        : ast_(ast), strings_(strings), environments_(environments),
          functions_(functions), storage_(storage), limits_(limits),
          sharedExecutionSteps_(sharedExecutionSteps) {}

    static bool supports(const Ast& ast, AstNodeId root,
        std::size_t maxCallArguments, std::size_t maxAstDepth = 256u)
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
            true, false);
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
            (storage_.frameCapacity != 0u && storage_.frames == nullptr) ||
            (storage_.argumentCapacity != 0u &&
                storage_.argumentValues == nullptr)) {
            fail(PortableExecutionErrorCode::InternalInvariantFailure,
                SourceLocation());
            return finish(result, finalValue);
        }
        if (!supports(ast_, root, limits_.maxCallArguments,
                limits_.maxAstDepth)) {
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
        currentThis_ = Value::undefined();
        finalValue_ = Value::undefined();
        callDepth_ = 0u;
        argumentTop_ = 0u;
        result_ = PortableExecutionResult();

        if (!preflightGlobalBindings(root) ||
            !instantiateFunctions(root, globalEnvironment) ||
            !instantiateLexicalBindings(root, globalEnvironment)) {
            return finish(result, finalValue);
        }
        Control control;
        if (!executeStatement(root, false, control))
            return finish(result, finalValue);
        if (control.kind != ControlKind::Normal) {
            fail(PortableExecutionErrorCode::IllegalReturn,
                ast_.node(root).location);
            return finish(result, finalValue);
        }
        result_.completed = true;
        result_.finalValue = finalValue_;
        result_.executionSteps = sharedExecutionSteps_;
        result = result_;
        finalValue = finalValue_;
        return true;
    }

private:
    enum class ControlKind : std::uint8_t { Normal = 0, Return };
    struct Control {
        ControlKind kind = ControlKind::Normal;
        Value value = Value::undefined();
    };

    class EnvironmentRestore {
    public:
        EnvironmentRestore(PortableExecutionCore& core, EnvironmentId previous)
            : core_(core), previous_(previous) {}
        ~EnvironmentRestore() { core_.currentEnvironment_ = previous_; }
    private:
        PortableExecutionCore& core_;
        EnvironmentId previous_;
    };

    class CallRestore {
    public:
        CallRestore(PortableExecutionCore& core, EnvironmentId previousEnvironment,
            Value previousThis, std::size_t previousArgumentTop)
            : core_(core), previousEnvironment_(previousEnvironment),
              previousThis_(previousThis), previousArgumentTop_(previousArgumentTop) {}
        ~CallRestore()
        {
            core_.currentEnvironment_ = previousEnvironment_;
            core_.currentThis_ = previousThis_;
            core_.argumentTop_ = previousArgumentTop_;
            if (core_.callDepth_ != 0u) --core_.callDepth_;
        }
    private:
        PortableExecutionCore& core_;
        EnvironmentId previousEnvironment_;
        Value previousThis_;
        std::size_t previousArgumentTop_;
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
        std::size_t maxArgs, std::size_t maxDepth, std::size_t depth)
    {
        if (depth > maxDepth || !validNode(ast, id) ||
            !simpleParameters(ast, id)) return false;
        const AstNode& function = ast.node(id);
        if (function.kind == AstNodeKind::ArrowFunctionExpression &&
            function.body == kInvalidAstNodeId) {
            return function.expression != kInvalidAstNodeId &&
                supportsExpression(ast, function.expression, maxArgs, maxDepth,
                    depth + 1u);
        }
        if (function.body == kInvalidAstNodeId ||
            !validNode(ast, function.body) ||
            ast.node(function.body).kind != AstNodeKind::BlockStatement)
            return false;
        return supportsStatementList(ast, function.body, maxArgs, maxDepth,
            depth + 1u, true, true);
    }

    static bool supportsExpression(const Ast& ast, AstNodeId id,
        std::size_t maxArgs, std::size_t maxDepth, std::size_t depth)
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
                    depth + 1u);
        case AstNodeKind::BinaryExpression:
            return node.binaryOperator != AstBinaryOperator::Remainder &&
                validNode(ast, node.left) && validNode(ast, node.right) &&
                supportsExpression(ast, node.left, maxArgs, maxDepth,
                    depth + 1u) &&
                supportsExpression(ast, node.right, maxArgs, maxDepth,
                    depth + 1u);
        case AstNodeKind::LogicalExpression:
            return validNode(ast, node.left) && validNode(ast, node.right) &&
                supportsExpression(ast, node.left, maxArgs, maxDepth,
                    depth + 1u) &&
                supportsExpression(ast, node.right, maxArgs, maxDepth,
                    depth + 1u);
        case AstNodeKind::AssignmentExpression:
            return node.assignmentOperator != AstAssignmentOperator::Remainder &&
                validNode(ast, node.left) &&
                ast.node(node.left).kind == AstNodeKind::Identifier &&
                validNode(ast, node.right) &&
                supportsExpression(ast, node.right, maxArgs, maxDepth,
                    depth + 1u);
        case AstNodeKind::CallExpression: {
            if (!validNode(ast, node.callee) ||
                ast.node(node.callee).kind != AstNodeKind::Identifier ||
                node.childCount > maxArgs) return false;
            SourceView name;
            if (!isIdentifierName(ast, node.callee, name) ||
                !declaredFunctionLike(ast, name)) return false;
            for (std::size_t index = 0u; index < node.childCount; ++index) {
                if (!supportsExpression(ast, ast.childAt(id, index), maxArgs,
                        maxDepth, depth + 1u)) return false;
            }
            return true;
        }
        case AstNodeKind::FunctionExpression:
        case AstNodeKind::ArrowFunctionExpression:
            return supportsFunction(ast, id, maxArgs, maxDepth, depth + 1u);
        default:
            return false;
        }
    }

    static bool supportsStatementList(const Ast& ast, AstNodeId id,
        std::size_t maxArgs, std::size_t maxDepth, std::size_t depth,
        bool allowFunctionDeclarations, bool inFunction)
    {
        if (!validNode(ast, id) || depth > maxDepth) return false;
        const AstNode& list = ast.node(id);
        for (std::size_t index = 0u; index < list.childCount; ++index) {
            const AstNodeId child = ast.childAt(id, index);
            if (!supportsStatement(ast, child, maxArgs, maxDepth, depth + 1u,
                    allowFunctionDeclarations, inFunction)) return false;
        }
        return true;
    }

    static bool supportsStatement(const Ast& ast, AstNodeId id,
        std::size_t maxArgs, std::size_t maxDepth, std::size_t depth,
        bool allowFunctionDeclarations, bool inFunction)
    {
        if (!validNode(ast, id) || depth > maxDepth) return false;
        const AstNode& node = ast.node(id);
        switch (node.kind) {
        case AstNodeKind::Program:
            return supportsStatementList(ast, id, maxArgs, maxDepth, depth + 1u,
                true, false);
        case AstNodeKind::BlockStatement:
            return supportsStatementList(ast, id, maxArgs, maxDepth, depth + 1u,
                false, inFunction);
        case AstNodeKind::EmptyStatement:
            return true;
        case AstNodeKind::ExpressionStatement:
            return node.expression != kInvalidAstNodeId &&
                supportsExpression(ast, node.expression, maxArgs, maxDepth,
                    depth + 1u);
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
                        depth + 1u)) return false;
            }
            return true;
        case AstNodeKind::ReturnStatement:
            return node.expression == kInvalidAstNodeId ||
                supportsExpression(ast, node.expression, maxArgs, maxDepth,
                    depth + 1u);
        case AstNodeKind::FunctionDeclaration:
            return allowFunctionDeclarations &&
                supportsFunction(ast, id, maxArgs, maxDepth, depth + 1u);
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
        if (node.childCount > limits_.maxCallArguments) {
            fail(PortableExecutionErrorCode::InvalidInvocationTarget,
                node.location);
            return false;
        }
        const std::size_t mark = argumentTop_;
        Value callee;
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
        std::size_t argumentCount, SourceLocation callSite, Value& result)
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
                    : PortableExecutionErrorCode::InvalidFunctionReference,
                callSite);
            return false;
        }
        const FunctionRecord function = *stored;
        if (function.kind != FunctionRecord::Kind::User ||
            !validNode(ast_, function.declaration)) {
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
        const bool arrow = declaration.kind == AstNodeKind::ArrowFunctionExpression;
        if ((declaration.kind != AstNodeKind::FunctionDeclaration &&
                declaration.kind != AstNodeKind::FunctionExpression && !arrow) ||
            function.arrowFunction != arrow ||
            !simpleParameters(ast_, function.declaration)) {
            fail(PortableExecutionErrorCode::InvalidFunctionReference,
                callSite);
            return false;
        }
        EnvironmentError envError;
        if (!environments_.valid(function.closureEnvironment, envError)) {
            fail(mapEnvironmentError(envError.code), callSite);
            return false;
        }

        const EnvironmentId previousEnvironment = currentEnvironment_;
        const Value previousThis = currentThis_;
        const std::size_t previousArgumentTop = argumentTop_;
        PortableCallFrame& frame = storage_.frames[callDepth_];
        frame.function = id;
        frame.previousEnvironment = previousEnvironment;
        frame.previousThis = previousThis;
        frame.callSite = callSite;
        frame.argumentMark = previousArgumentTop;
        ++callDepth_;
        if (callDepth_ > result_.callDepthHighWater)
            result_.callDepthHighWater = callDepth_;
        CallRestore restore(*this, previousEnvironment, previousThis,
            previousArgumentTop);

        EnvironmentId callEnvironment = kInvalidEnvironmentId;
        if (!environments_.create(function.closureEnvironment,
                EnvironmentKind::Function, environmentLimits(), callEnvironment,
                envError)) {
            fail(mapEnvironmentError(envError.code), callSite);
            return false;
        }
        ++result_.environmentsCreated;
        frame.environment = callEnvironment;
        currentEnvironment_ = callEnvironment;
        currentThis_ = arrow ? function.lexicalThis : Value::undefined();
        for (std::size_t index = 0u; index < declaration.childCount; ++index) {
            const AstNodeId parameter = ast_.childAt(function.declaration, index);
            SourceView name;
            if (!nodeName(parameter, name, ast_.node(parameter).location))
                return false;
            EnvironmentError parameterError;
            const Value value = index < argumentCount
                ? arguments[index] : Value::undefined();
            if (!environments_.declare(callEnvironment, name, value,
                    parameterError)) {
                fail(mapBindingError(parameterError.code),
                    ast_.node(parameter).location);
                return false;
            }
        }
        if (declaration.body != kInvalidAstNodeId) {
            if (!instantiateFunctions(declaration.body, callEnvironment) ||
                !instantiateLexicalBindings(declaration.body, callEnvironment))
                return false;
            if (!consumeStep(ast_.node(declaration.body).location)) return false;
            Control control;
            if (!executeStatementList(declaration.body, true, control))
                return false;
            if (control.kind == ControlKind::Return) result = control.value;
            else result = Value::undefined();
            return true;
        }
        Control control;
        if (!evalExpression(declaration.expression, control.value)) return false;
        result = control.value;
        return true;
    }

    Ast& ast_;
    RuntimeStringStore& strings_;
    EnvironmentStore& environments_;
    FunctionStore& functions_;
    PortableExecutionStorageView storage_;
    PortableExecutionLimits limits_;
    std::size_t& sharedExecutionSteps_;
    PortableExecutionResult result_;
    EnvironmentId currentEnvironment_ = kInvalidEnvironmentId;
    Value currentThis_ = Value::undefined();
    Value finalValue_ = Value::undefined();
    std::size_t callDepth_ = 0u;
    std::size_t argumentTop_ = 0u;
};

} // namespace javascript
} // namespace gxos
