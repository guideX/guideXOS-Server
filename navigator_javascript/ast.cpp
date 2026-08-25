#include "ast.h"

#include <limits>

namespace gxos {
namespace javascript {

void Ast::reset(SourceView source)
{
    source_ = source;
    root_ = kInvalidAstNodeId;
    nodes_.clear();
    children_.clear();
}

bool Ast::appendFrom(const Ast& source, std::size_t sourceOffset,
    std::size_t lineOffset, AstNodeId& appendedRoot)
{
    appendedRoot = kInvalidAstNodeId;
    if (source.root_ == kInvalidAstNodeId ||
        source.root_ >= source.nodes_.size() ||
        nodes_.size() > static_cast<std::size_t>(kInvalidAstNodeId) -
            source.nodes_.size() ||
        children_.size() > static_cast<std::size_t>(kInvalidAstNodeId) -
            source.children_.size()) return false;

    const AstNodeId nodeBase = static_cast<AstNodeId>(nodes_.size());
    const std::uint32_t childBase = static_cast<std::uint32_t>(children_.size());
    const auto remap = [nodeBase](AstNodeId id) {
        return id == kInvalidAstNodeId ? id :
            static_cast<AstNodeId>(nodeBase + id);
    };
    try {
        nodes_.reserve(nodes_.size() + source.nodes_.size());
        children_.reserve(children_.size() + source.children_.size());
        for (const AstNode& sourceNode : source.nodes_) {
            AstNode node = sourceNode;
            node.location.offset += sourceOffset;
            node.location.line += lineOffset;
            node.name = remap(node.name);
            node.initializer = remap(node.initializer);
            node.expression = remap(node.expression);
            node.argument = remap(node.argument);
            node.left = remap(node.left);
            node.right = remap(node.right);
            node.test = remap(node.test);
            node.consequent = remap(node.consequent);
            node.alternate = remap(node.alternate);
            node.init = remap(node.init);
            node.update = remap(node.update);
            node.body = remap(node.body);
            node.callee = remap(node.callee);
            node.object = remap(node.object);
            node.property = remap(node.property);
            node.key = remap(node.key);
            if (node.childCount != 0)
                node.childOffset += childBase;
            nodes_.push_back(node);
        }
        for (AstNodeId child : source.children_) children_.push_back(remap(child));
    } catch (...) {
        return false;
    }
    appendedRoot = remap(source.root_);
    return true;
}

AstNodeId Ast::addNode(const AstNode& nodeValue)
{
    if (nodes_.size() >= static_cast<std::size_t>(kInvalidAstNodeId)) {
        return kInvalidAstNodeId;
    }

    const AstNodeId id = static_cast<AstNodeId>(nodes_.size());
    nodes_.push_back(nodeValue);
    return id;
}

bool Ast::setRoot(AstNodeId root)
{
    if (root == kInvalidAstNodeId || root >= nodes_.size()) return false;
    root_ = root;
    return true;
}

bool Ast::setChildren(AstNodeId parent, const AstNodeId* children,
    std::size_t count)
{
    if (parent == kInvalidAstNodeId || parent >= nodes_.size()) return false;
    if (count != 0 && children == nullptr) return false;
    if (children_.size() > static_cast<std::size_t>(kInvalidAstNodeId) ||
        count > static_cast<std::size_t>(kInvalidAstNodeId) - children_.size()) {
        return false;
    }

    for (std::size_t i = 0; i < count; ++i) {
        if (children[i] == kInvalidAstNodeId || children[i] >= nodes_.size()) {
            return false;
        }
    }

    const std::uint32_t offset = static_cast<std::uint32_t>(children_.size());
    if (count != 0) children_.insert(children_.end(), children, children + count);
    nodes_[parent].childOffset = offset;
    nodes_[parent].childCount = static_cast<std::uint32_t>(count);
    return true;
}

const AstNode& Ast::node(AstNodeId id) const
{
    static const AstNode invalidNode;
    if (id == kInvalidAstNodeId || id >= nodes_.size()) return invalidNode;
    return nodes_[id];
}

AstNode& Ast::node(AstNodeId id)
{
    static AstNode invalidNode;
    if (id == kInvalidAstNodeId || id >= nodes_.size()) return invalidNode;
    return nodes_[id];
}

std::size_t Ast::childCount(AstNodeId parent) const
{
    const AstNode& parentNode = node(parent);
    return parentNode.childCount;
}

AstNodeId Ast::childAt(AstNodeId parent, std::size_t index) const
{
    const AstNode& parentNode = node(parent);
    if (index >= parentNode.childCount) return kInvalidAstNodeId;
    const std::size_t childIndex =
        static_cast<std::size_t>(parentNode.childOffset) + index;
    if (childIndex >= children_.size()) return kInvalidAstNodeId;
    return children_[childIndex];
}

SourceView Ast::sourceSlice(SourceLocation location) const
{
    if (location.length == 0) return SourceView();
    if (source_.data == nullptr || location.offset > source_.length ||
        location.length > source_.length - location.offset) {
        return SourceView();
    }
    return SourceView(source_.data + location.offset, location.length);
}

SourceView Ast::nodeText(AstNodeId id) const
{
    return sourceSlice(node(id).location);
}

const char* astNodeKindName(AstNodeKind kind)
{
    switch (kind) {
    case AstNodeKind::Invalid: return "Invalid";
    case AstNodeKind::Program: return "Program";
    case AstNodeKind::EmptyStatement: return "EmptyStatement";
    case AstNodeKind::VariableDeclaration: return "VariableDeclaration";
    case AstNodeKind::VariableDeclarator: return "VariableDeclarator";
    case AstNodeKind::ExpressionStatement: return "ExpressionStatement";
    case AstNodeKind::BlockStatement: return "BlockStatement";
    case AstNodeKind::ReturnStatement: return "ReturnStatement";
    case AstNodeKind::IfStatement: return "IfStatement";
    case AstNodeKind::WhileStatement: return "WhileStatement";
    case AstNodeKind::ForStatement: return "ForStatement";
    case AstNodeKind::BreakStatement: return "BreakStatement";
    case AstNodeKind::ContinueStatement: return "ContinueStatement";
    case AstNodeKind::FunctionDeclaration: return "FunctionDeclaration";
    case AstNodeKind::FunctionExpression: return "FunctionExpression";
    case AstNodeKind::Identifier: return "Identifier";
    case AstNodeKind::NumericLiteral: return "NumericLiteral";
    case AstNodeKind::StringLiteral: return "StringLiteral";
    case AstNodeKind::BooleanLiteral: return "BooleanLiteral";
    case AstNodeKind::NullLiteral: return "NullLiteral";
    case AstNodeKind::ThisExpression: return "ThisExpression";
    case AstNodeKind::UnaryExpression: return "UnaryExpression";
    case AstNodeKind::BinaryExpression: return "BinaryExpression";
    case AstNodeKind::LogicalExpression: return "LogicalExpression";
    case AstNodeKind::AssignmentExpression: return "AssignmentExpression";
    case AstNodeKind::UpdateExpression: return "UpdateExpression";
    case AstNodeKind::CallExpression: return "CallExpression";
    case AstNodeKind::MemberExpression: return "MemberExpression";
    case AstNodeKind::NewExpression: return "NewExpression";
    case AstNodeKind::ObjectLiteral: return "ObjectLiteral";
    case AstNodeKind::ObjectProperty: return "ObjectProperty";
    case AstNodeKind::ArrayLiteral: return "ArrayLiteral";
    }
    return "Invalid";
}

} // namespace javascript
} // namespace gxos
