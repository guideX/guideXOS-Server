#pragma once

#include "lexer.h"

#include <cstddef>
#include <cstdint>
#if !defined(GXOS_NAVIGATOR_FRONTEND_FREESTANDING)
#include <vector>
#endif

namespace gxos {
namespace javascript {

using AstNodeId = std::uint32_t;

constexpr AstNodeId kInvalidAstNodeId = 0xffffffffu;
constexpr std::size_t kDefaultMaxJavaScriptAstNodes = 16384u;

enum class AstNodeKind : std::uint8_t {
    Invalid = 0,
    Program,
    EmptyStatement,
    VariableDeclaration,
    VariableDeclarator,
    ExpressionStatement,
    BlockStatement,
    ReturnStatement,
    IfStatement,
    WhileStatement,
    ForStatement,
    ForOfStatement,
    BreakStatement,
    ContinueStatement,
    FunctionDeclaration,
    FunctionExpression,
    ArrowFunctionExpression,

    Identifier,
    NumericLiteral,
    StringLiteral,
    BooleanLiteral,
    NullLiteral,
    ThisExpression,
    UnaryExpression,
    BinaryExpression,
    LogicalExpression,
    AssignmentExpression,
    UpdateExpression,
    CallExpression,
    MemberExpression,
    NewExpression,
    ObjectLiteral,
    ObjectProperty,
    ArrayLiteral,
    ArrayElision,
};

enum class AstUnaryOperator : std::uint8_t {
    LogicalNot = 0,
    Plus,
    Minus,
};

enum class AstBinaryOperator : std::uint8_t {
    Add = 0,
    Subtract,
    Multiply,
    Divide,
    Remainder,
    Equal,
    StrictEqual,
    NotEqual,
    StrictNotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
};

enum class AstLogicalOperator : std::uint8_t {
    And = 0,
    Or,
};

enum class AstAssignmentOperator : std::uint8_t {
    Assign = 0,
    Add,
    Subtract,
    Multiply,
    Divide,
    Remainder,
};

enum class AstUpdateOperator : std::uint8_t {
    Increment = 0,
    Decrement,
};

enum class AstDeclarationKind : std::uint8_t {
    Var = 0,
    Let,
    Const,
};

// Relationships are represented by stable indices into Ast::nodes(), never
// by pointers into parser-owned storage.  childOffset/childCount describe a
// contiguous range in Ast's child index storage for Program, BlockStatement,
// VariableDeclaration, FunctionDeclaration, FunctionExpression, and
// CallExpression nodes. Formal parameter nodes remain Identifier records:
// initializer stores an optional default expression and prefix marks a rest
// parameter. Those fields are otherwise unused by Identifier nodes, avoiding
// any increase to AstNode size.
struct AstNode {
    AstNodeKind kind = AstNodeKind::Invalid;
    SourceLocation location;

    std::uint32_t childOffset = 0;
    std::uint32_t childCount = 0;

    AstNodeId name = kInvalidAstNodeId;
    AstNodeId initializer = kInvalidAstNodeId;
    AstNodeId expression = kInvalidAstNodeId;
    AstNodeId argument = kInvalidAstNodeId;
    AstNodeId left = kInvalidAstNodeId;
    AstNodeId right = kInvalidAstNodeId;
    AstNodeId test = kInvalidAstNodeId;
    AstNodeId consequent = kInvalidAstNodeId;
    AstNodeId alternate = kInvalidAstNodeId;
    AstNodeId init = kInvalidAstNodeId;
    AstNodeId update = kInvalidAstNodeId;
    AstNodeId body = kInvalidAstNodeId;
    AstNodeId callee = kInvalidAstNodeId;
    AstNodeId object = kInvalidAstNodeId;
    AstNodeId property = kInvalidAstNodeId;
    AstNodeId key = kInvalidAstNodeId;

    AstUnaryOperator unaryOperator = AstUnaryOperator::LogicalNot;
    AstBinaryOperator binaryOperator = AstBinaryOperator::Add;
    AstLogicalOperator logicalOperator = AstLogicalOperator::And;
    AstAssignmentOperator assignmentOperator = AstAssignmentOperator::Assign;
    AstUpdateOperator updateOperator = AstUpdateOperator::Increment;
    AstDeclarationKind declarationKind = AstDeclarationKind::Var;
    bool computed = false;
    bool prefix = false;
    bool immutableBinding = false;
};

struct AstStorageView {
    AstNode* nodes = nullptr;
    std::size_t nodeCapacity = 0;
    AstNodeId* children = nullptr;
    std::size_t childCapacity = 0;
};

class Ast {
public:
    explicit Ast(SourceView source = SourceView());
    Ast(SourceView source, AstStorageView storage);
#if !defined(GXOS_NAVIGATOR_FRONTEND_FREESTANDING)
    Ast(const Ast& other);
    Ast& operator=(const Ast& other);
#else
    Ast(const Ast&) = delete;
    Ast& operator=(const Ast&) = delete;
#endif
    Ast(Ast&& other) noexcept;
    Ast& operator=(Ast&& other) noexcept;

    void reset(SourceView source = SourceView());
    void setSource(SourceView source) { source_ = source; }

    // Append a parsed program while preserving the stable IDs of existing
    // nodes. sourceOffset/lineOffset rebase diagnostics onto a cumulative
    // realm source buffer; the returned root is suitable for setRoot().
    bool appendFrom(const Ast& source, std::size_t sourceOffset,
        std::size_t lineOffset, AstNodeId& appendedRoot);

    AstNodeId addNode(const AstNode& node);
    bool setChildren(AstNodeId parent, const AstNodeId* children,
        std::size_t count);
    bool setRoot(AstNodeId root);
    AstNodeId root() const { return root_; }

    std::size_t nodeCount() const { return nodeCount_; }
    std::size_t nodeCapacity() const { return nodeCapacity_; }
    std::size_t childStorageUsed() const { return childCount_; }
    std::size_t childStorageCapacity() const { return childCapacity_; }
    const AstNode& node(AstNodeId id) const;
    AstNode& node(AstNodeId id);

    std::size_t childCount(AstNodeId parent) const;
    AstNodeId childAt(AstNodeId parent, std::size_t index) const;

    SourceView source() const { return source_; }
    SourceView sourceSlice(SourceLocation location) const;
    SourceView nodeText(AstNodeId id) const;

private:
#if !defined(GXOS_NAVIGATOR_FRONTEND_FREESTANDING)
    void syncOwnedStorage();
#endif

    SourceView source_;
    AstNodeId root_ = kInvalidAstNodeId;
    AstNode* nodes_ = nullptr;
    std::size_t nodeCount_ = 0;
    std::size_t nodeCapacity_ = 0;
    AstNodeId* children_ = nullptr;
    std::size_t childCount_ = 0;
    std::size_t childCapacity_ = 0;
    bool ownsStorage_ = false;
#if !defined(GXOS_NAVIGATOR_FRONTEND_FREESTANDING)
    std::vector<AstNode> ownedNodes_;
    std::vector<AstNodeId> ownedChildren_;
#endif
};

const char* astNodeKindName(AstNodeKind kind);

} // namespace javascript
} // namespace gxos
