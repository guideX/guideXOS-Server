#pragma once

#include "lexer.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gxos {
namespace javascript {

using AstNodeId = std::uint32_t;

constexpr AstNodeId kInvalidAstNodeId = 0xffffffffu;

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
    BreakStatement,
    ContinueStatement,
    FunctionDeclaration,
    FunctionExpression,

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

// Relationships are represented by stable indices into Ast::nodes(), never
// by pointers into parser-owned storage.  childOffset/childCount describe a
// contiguous range in Ast's child index storage for Program, BlockStatement,
// VariableDeclaration, FunctionDeclaration, FunctionExpression, and
// CallExpression nodes.
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
    bool computed = false;
    bool prefix = false;
};

class Ast {
public:
    explicit Ast(SourceView source = SourceView()) : source_(source) {}

    Ast(const Ast&) = default;
    Ast(Ast&&) noexcept = default;
    Ast& operator=(const Ast&) = default;
    Ast& operator=(Ast&&) noexcept = default;

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

    std::size_t nodeCount() const { return nodes_.size(); }
    const AstNode& node(AstNodeId id) const;
    AstNode& node(AstNodeId id);

    std::size_t childCount(AstNodeId parent) const;
    AstNodeId childAt(AstNodeId parent, std::size_t index) const;

    SourceView source() const { return source_; }
    SourceView sourceSlice(SourceLocation location) const;
    SourceView nodeText(AstNodeId id) const;

private:
    SourceView source_;
    AstNodeId root_ = kInvalidAstNodeId;
    std::vector<AstNode> nodes_;
    std::vector<AstNodeId> children_;
};

const char* astNodeKindName(AstNodeKind kind);

} // namespace javascript
} // namespace gxos
