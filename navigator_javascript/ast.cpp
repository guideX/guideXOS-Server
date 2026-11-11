#include "ast.h"

#if !defined(GXOS_NAVIGATOR_FRONTEND_FREESTANDING)
#include <utility>
#endif

namespace gxos {
namespace javascript {

Ast::Ast(SourceView source) : source_(source)
{
#if !defined(GXOS_NAVIGATOR_FRONTEND_FREESTANDING)
    ownsStorage_ = true;
    syncOwnedStorage();
#endif
}

Ast::Ast(SourceView source, AstStorageView storage)
    : source_(source), nodes_(storage.nodes), nodeCapacity_(storage.nodeCapacity),
      children_(storage.children), childCapacity_(storage.childCapacity)
{
}

#if !defined(GXOS_NAVIGATOR_FRONTEND_FREESTANDING)
void Ast::syncOwnedStorage()
{
    if (!ownsStorage_) return;
    nodes_ = ownedNodes_.data();
    nodeCount_ = ownedNodes_.size();
    nodeCapacity_ = ownedNodes_.capacity();
    children_ = ownedChildren_.data();
    childCount_ = ownedChildren_.size();
    childCapacity_ = ownedChildren_.capacity();
}

Ast::Ast(const Ast& other)
    : source_(other.source_), root_(other.root_), ownsStorage_(true)
{
    if (other.nodeCount_ != 0)
        ownedNodes_.assign(other.nodes_, other.nodes_ + other.nodeCount_);
    if (other.childCount_ != 0)
        ownedChildren_.assign(other.children_,
            other.children_ + other.childCount_);
    syncOwnedStorage();
}

Ast& Ast::operator=(const Ast& other)
{
    if (this == &other) return *this;
    Ast copy(other);
    *this = std::move(copy);
    return *this;
}
#endif

Ast::Ast(Ast&& other) noexcept
    : source_(other.source_), root_(other.root_), nodes_(other.nodes_),
      nodeCount_(other.nodeCount_), nodeCapacity_(other.nodeCapacity_),
      children_(other.children_), childCount_(other.childCount_),
      childCapacity_(other.childCapacity_), ownsStorage_(other.ownsStorage_)
#if !defined(GXOS_NAVIGATOR_FRONTEND_FREESTANDING)
      , ownedNodes_(std::move(other.ownedNodes_)),
      ownedChildren_(std::move(other.ownedChildren_))
#endif
{
#if !defined(GXOS_NAVIGATOR_FRONTEND_FREESTANDING)
    syncOwnedStorage();
#endif
    other.source_ = SourceView();
    other.root_ = kInvalidAstNodeId;
    other.nodes_ = nullptr;
    other.nodeCount_ = 0;
    other.nodeCapacity_ = 0;
    other.children_ = nullptr;
    other.childCount_ = 0;
    other.childCapacity_ = 0;
#if !defined(GXOS_NAVIGATOR_FRONTEND_FREESTANDING)
    other.ownsStorage_ = true;
    other.ownedNodes_.clear();
    other.ownedChildren_.clear();
    other.syncOwnedStorage();
#endif
}

Ast& Ast::operator=(Ast&& other) noexcept
{
    if (this == &other) return *this;
    source_ = other.source_;
    root_ = other.root_;
    nodes_ = other.nodes_;
    nodeCount_ = other.nodeCount_;
    nodeCapacity_ = other.nodeCapacity_;
    children_ = other.children_;
    childCount_ = other.childCount_;
    childCapacity_ = other.childCapacity_;
    ownsStorage_ = other.ownsStorage_;
#if !defined(GXOS_NAVIGATOR_FRONTEND_FREESTANDING)
    ownedNodes_ = std::move(other.ownedNodes_);
    ownedChildren_ = std::move(other.ownedChildren_);
    syncOwnedStorage();
#endif
    other.source_ = SourceView();
    other.root_ = kInvalidAstNodeId;
    other.nodes_ = nullptr;
    other.nodeCount_ = 0;
    other.nodeCapacity_ = 0;
    other.children_ = nullptr;
    other.childCount_ = 0;
    other.childCapacity_ = 0;
#if !defined(GXOS_NAVIGATOR_FRONTEND_FREESTANDING)
    other.ownsStorage_ = true;
    other.ownedNodes_.clear();
    other.ownedChildren_.clear();
    other.syncOwnedStorage();
#endif
    return *this;
}

void Ast::reset(SourceView source)
{
    source_ = source;
    root_ = kInvalidAstNodeId;
#if !defined(GXOS_NAVIGATOR_FRONTEND_FREESTANDING)
    if (ownsStorage_) {
        ownedNodes_.clear();
        ownedChildren_.clear();
        syncOwnedStorage();
        return;
    }
#endif
    nodeCount_ = 0;
    childCount_ = 0;
}

bool Ast::appendFrom(const Ast& source, std::size_t sourceOffset,
    std::size_t lineOffset, AstNodeId& appendedRoot)
{
    appendedRoot = kInvalidAstNodeId;
    const std::size_t sourceNodeCount = source.nodeCount_;
    const std::size_t sourceChildCount = source.childCount_;
    const AstNodeId sourceRoot = source.root_;
    const std::size_t idLimit = static_cast<std::size_t>(kInvalidAstNodeId);
    const std::size_t childLimit =
        static_cast<std::size_t>(static_cast<std::uint32_t>(-1));
    if (sourceRoot == kInvalidAstNodeId || sourceRoot >= sourceNodeCount ||
        nodeCount_ > idLimit || sourceNodeCount > idLimit - nodeCount_ ||
        childCount_ > childLimit || sourceChildCount > childLimit - childCount_) {
        return false;
    }
    if (source.nodes_ == nullptr || sourceNodeCount > source.nodeCapacity_ ||
        sourceChildCount > source.childCapacity_ ||
        (sourceChildCount != 0 && source.children_ == nullptr)) return false;

    const auto validReference = [sourceNodeCount](AstNodeId id) {
        return id == kInvalidAstNodeId || id < sourceNodeCount;
    };

    const std::size_t sizeMax = static_cast<std::size_t>(-1);
    for (std::size_t index = 0; index < sourceNodeCount; ++index) {
        const AstNode& sourceNode = source.nodes_[index];
        if (sourceOffset > sizeMax - sourceNode.location.offset ||
            lineOffset > sizeMax - sourceNode.location.line ||
            !validReference(sourceNode.name) ||
            !validReference(sourceNode.initializer) ||
            !validReference(sourceNode.expression) ||
            !validReference(sourceNode.argument) ||
            !validReference(sourceNode.left) ||
            !validReference(sourceNode.right) ||
            !validReference(sourceNode.test) ||
            !validReference(sourceNode.consequent) ||
            !validReference(sourceNode.alternate) ||
            !validReference(sourceNode.init) ||
            !validReference(sourceNode.update) ||
            !validReference(sourceNode.body) ||
            !validReference(sourceNode.callee) ||
            !validReference(sourceNode.object) ||
            !validReference(sourceNode.property) ||
            !validReference(sourceNode.key) ||
            sourceNode.childOffset > sourceChildCount ||
            sourceNode.childCount > sourceChildCount - sourceNode.childOffset) {
            return false;
        }
    }
    for (std::size_t index = 0; index < sourceChildCount; ++index) {
        if (!validReference(source.children_[index])) return false;
    }

    const std::size_t requiredNodes = nodeCount_ + sourceNodeCount;
    const std::size_t requiredChildren = childCount_ + sourceChildCount;
#if !defined(GXOS_NAVIGATOR_FRONTEND_FREESTANDING)
    if (ownsStorage_) {
        try {
            ownedNodes_.reserve(requiredNodes);
            ownedChildren_.reserve(requiredChildren);
        } catch (...) {
            syncOwnedStorage();
            return false;
        }
        syncOwnedStorage();
    } else
#endif
    if (requiredNodes > nodeCapacity_ || requiredChildren > childCapacity_ ||
        (requiredNodes != 0 && nodes_ == nullptr) ||
        (requiredChildren != 0 && children_ == nullptr)) {
        return false;
    }

    const AstNodeId nodeBase = static_cast<AstNodeId>(nodeCount_);
    const std::uint32_t childBase = static_cast<std::uint32_t>(childCount_);
    const AstNode* sourceNodes = source.nodes_;
    const AstNodeId* sourceChildren = source.children_;
    const auto remap = [nodeBase](AstNodeId id) {
        return id == kInvalidAstNodeId ? id :
            static_cast<AstNodeId>(nodeBase + id);
    };

    for (std::size_t index = 0; index < sourceNodeCount; ++index) {
        AstNode nodeValue = sourceNodes[index];
        nodeValue.location.offset += sourceOffset;
        nodeValue.location.line += lineOffset;
        nodeValue.name = remap(nodeValue.name);
        nodeValue.initializer = remap(nodeValue.initializer);
        nodeValue.expression = remap(nodeValue.expression);
        nodeValue.argument = remap(nodeValue.argument);
        nodeValue.left = remap(nodeValue.left);
        nodeValue.right = remap(nodeValue.right);
        nodeValue.test = remap(nodeValue.test);
        nodeValue.consequent = remap(nodeValue.consequent);
        nodeValue.alternate = remap(nodeValue.alternate);
        nodeValue.init = remap(nodeValue.init);
        nodeValue.update = remap(nodeValue.update);
        nodeValue.body = remap(nodeValue.body);
        nodeValue.callee = remap(nodeValue.callee);
        nodeValue.object = remap(nodeValue.object);
        nodeValue.property = remap(nodeValue.property);
        nodeValue.key = remap(nodeValue.key);
        if (nodeValue.childCount != 0)
            nodeValue.childOffset += childBase;
#if !defined(GXOS_NAVIGATOR_FRONTEND_FREESTANDING)
        if (ownsStorage_) ownedNodes_.push_back(nodeValue);
        else
#endif
            nodes_[nodeCount_] = nodeValue;
        ++nodeCount_;
    }
    for (std::size_t index = 0; index < sourceChildCount; ++index) {
        const AstNodeId child = remap(sourceChildren[index]);
#if !defined(GXOS_NAVIGATOR_FRONTEND_FREESTANDING)
        if (ownsStorage_) ownedChildren_.push_back(child);
        else
#endif
            children_[childCount_] = child;
        ++childCount_;
    }
#if !defined(GXOS_NAVIGATOR_FRONTEND_FREESTANDING)
    syncOwnedStorage();
#endif
    appendedRoot = remap(sourceRoot);
    return true;
}

AstNodeId Ast::addNode(const AstNode& nodeValue)
{
    const std::size_t idLimit = static_cast<std::size_t>(kInvalidAstNodeId);
    if (nodeCount_ >= idLimit) return kInvalidAstNodeId;

#if !defined(GXOS_NAVIGATOR_FRONTEND_FREESTANDING)
    if (ownsStorage_) {
        try {
            ownedNodes_.push_back(nodeValue);
        } catch (...) {
            syncOwnedStorage();
            return kInvalidAstNodeId;
        }
        const AstNodeId id = static_cast<AstNodeId>(nodeCount_);
        syncOwnedStorage();
        return id;
    }
#endif

    if (nodes_ == nullptr || nodeCount_ >= nodeCapacity_)
        return kInvalidAstNodeId;
    const AstNodeId id = static_cast<AstNodeId>(nodeCount_);
    nodes_[nodeCount_++] = nodeValue;
    return id;
}

bool Ast::setRoot(AstNodeId root)
{
    if (root == kInvalidAstNodeId || root >= nodeCount_) return false;
    root_ = root;
    return true;
}

bool Ast::setChildren(AstNodeId parent, const AstNodeId* children,
    std::size_t count)
{
    if (parent == kInvalidAstNodeId || parent >= nodeCount_) return false;
    if (count != 0 && children == nullptr) return false;
    const std::size_t childLimit =
        static_cast<std::size_t>(static_cast<std::uint32_t>(-1));
    if (childCount_ > childLimit || count > childLimit - childCount_ ||
        count > childLimit) return false;

    for (std::size_t i = 0; i < count; ++i) {
        if (children[i] == kInvalidAstNodeId || children[i] >= nodeCount_)
            return false;
    }

    const std::uint32_t offset = static_cast<std::uint32_t>(childCount_);
    const std::size_t requiredChildren = childCount_ + count;
#if !defined(GXOS_NAVIGATOR_FRONTEND_FREESTANDING)
    if (ownsStorage_) {
        if (count != 0) {
            try {
                ownedChildren_.insert(ownedChildren_.end(), children,
                    children + count);
            } catch (...) {
                syncOwnedStorage();
                return false;
            }
        }
        syncOwnedStorage();
    } else
#endif
    {
        if (requiredChildren > childCapacity_ ||
            (count != 0 && children_ == nullptr)) return false;
        for (std::size_t i = 0; i < count; ++i)
            children_[childCount_ + i] = children[i];
        childCount_ = requiredChildren;
    }
    nodes_[parent].childOffset = offset;
    nodes_[parent].childCount = static_cast<std::uint32_t>(count);
    return true;
}

const AstNode& Ast::node(AstNodeId id) const
{
    static const AstNode invalidNode;
    if (id == kInvalidAstNodeId || id >= nodeCount_ || nodes_ == nullptr)
        return invalidNode;
    return nodes_[id];
}

AstNode& Ast::node(AstNodeId id)
{
    static AstNode invalidNode;
    if (id == kInvalidAstNodeId || id >= nodeCount_ || nodes_ == nullptr)
        return invalidNode;
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
    const std::size_t offset = parentNode.childOffset;
    if (index > static_cast<std::size_t>(-1) - offset)
        return kInvalidAstNodeId;
    const std::size_t childIndex = offset + index;
    if (childIndex >= childCount_ || children_ == nullptr)
        return kInvalidAstNodeId;
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
    case AstNodeKind::ForOfStatement: return "ForOfStatement";
    case AstNodeKind::BreakStatement: return "BreakStatement";
    case AstNodeKind::ContinueStatement: return "ContinueStatement";
    case AstNodeKind::FunctionDeclaration: return "FunctionDeclaration";
    case AstNodeKind::FunctionExpression: return "FunctionExpression";
    case AstNodeKind::ArrowFunctionExpression: return "ArrowFunctionExpression";
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
    case AstNodeKind::ArrayElision: return "ArrayElision";
    }
    return "Invalid";
}

} // namespace javascript
} // namespace gxos
