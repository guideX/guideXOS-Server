#include "navigator_javascript/parser.h"

#include <cstddef>
#include <functional>
#include <iostream>
#include <string>

using gxos::javascript::Ast;
using gxos::javascript::AstAssignmentOperator;
using gxos::javascript::AstBinaryOperator;
using gxos::javascript::AstLogicalOperator;
using gxos::javascript::AstNodeId;
using gxos::javascript::AstNodeKind;
using gxos::javascript::AstUpdateOperator;
using gxos::javascript::astNodeKindName;
using gxos::javascript::Lexer;
using gxos::javascript::LexerLimits;
using gxos::javascript::Parser;
using gxos::javascript::ParserErrorCode;
using gxos::javascript::ParserLimits;
using gxos::javascript::ParseResult;
using gxos::javascript::SourceView;
using gxos::javascript::TokenType;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << "\n";
    }
}

std::string text(const Ast& ast, AstNodeId id)
{
    const SourceView slice = ast.nodeText(id);
    return slice.data == nullptr ? std::string() :
        std::string(slice.data, slice.length);
}

void expectKind(const Ast& ast, AstNodeId id, AstNodeKind kind,
    const std::string& label)
{
    expect(id != gxos::javascript::kInvalidAstNodeId,
        label + ": node exists");
    if (id != gxos::javascript::kInvalidAstNodeId) {
        expect(ast.node(id).kind == kind, label + ": kind matches (got " +
            astNodeKindName(ast.node(id).kind) + ")");
    }
}

ParseResult parse(const std::string& source, ParserLimits limits = ParserLimits())
{
    const Lexer lexer;
    const auto lexed = lexer.tokenize(SourceView(source.data(), source.size()));
    expect(lexed.succeeded(), "lexer succeeds before parser test");
    if (!lexed.succeeded()) return ParseResult();
    const Parser parser(limits);
    return parser.parse(SourceView(source.data(), source.size()), lexed);
}

void expectSuccess(const std::string& source, const std::string& label,
    const std::function<void(const Ast&)>& check)
{
    ParseResult result = parse(source);
    expect(result.succeeded(), label + ": parse succeeds");
    if (!result.succeeded()) return;
    expect(result.ast.root() != gxos::javascript::kInvalidAstNodeId,
        label + ": root exists");
    expectKind(result.ast, result.ast.root(), AstNodeKind::Program,
        label + ": root is Program");
    check(result.ast);
}

void expectError(const std::string& source, ParserErrorCode code,
    std::size_t offset, const std::string& label,
    ParserLimits limits = ParserLimits())
{
    ParseResult first = parse(source, limits);
    expect(!first.succeeded(), label + ": parse fails");
    expect(first.error.code == code, label + ": error code matches");
    expect(first.error.location.offset == offset, label + ": offset matches");
    expect(first.ast.nodeCount() == 0, label + ": failed AST is empty");
    expect(first.ast.root() == gxos::javascript::kInvalidAstNodeId,
        label + ": failed AST has no root");

    ParseResult second = parse(source, limits);
    expect(second.error.code == first.error.code &&
        second.error.location.offset == first.error.location.offset &&
        second.error.location.line == first.error.location.line &&
        second.error.location.column == first.error.location.column,
        label + ": repeated failure is deterministic");
}

void testVariableDeclaration()
{
    const std::string source = "var x = 10;";
    expectSuccess(source, "variable declaration", [](const Ast& ast) {
        const AstNodeId program = ast.root();
        expect(ast.childCount(program) == 1, "variable declaration: one statement");
        const AstNodeId declaration = ast.childAt(program, 0);
        expectKind(ast, declaration, AstNodeKind::VariableDeclaration,
            "variable declaration");
        expect(ast.node(declaration).location.offset == 0 &&
            ast.node(declaration).location.length == 11,
            "variable declaration: location covers source");
        expect(ast.childCount(declaration) == 1,
            "variable declaration: one declarator");
        const AstNodeId declarator = ast.childAt(declaration, 0);
        expectKind(ast, declarator, AstNodeKind::VariableDeclarator,
            "variable declarator");
        expect(text(ast, ast.node(declarator).name) == "x",
            "variable declarator: name");
        expectKind(ast, ast.node(declarator).name, AstNodeKind::Identifier,
            "variable declarator: identifier");
        expect(text(ast, ast.node(declarator).initializer) == "10",
            "variable declarator: initializer source");
        expectKind(ast, ast.node(declarator).initializer,
            AstNodeKind::NumericLiteral, "variable declarator: numeric literal");
        expect(ast.node(ast.node(declarator).name).location.offset == 4 &&
            ast.node(ast.node(declarator).initializer).location.offset == 8,
            "variable declaration: child locations propagate");
    });
}

void testFunctionDeclaration()
{
    const std::string source = "function add(a, b) {\n    return a + b;\n}";
    expectSuccess(source, "function declaration", [&source](const Ast& ast) {
        const AstNodeId function = ast.childAt(ast.root(), 0);
        expectKind(ast, function, AstNodeKind::FunctionDeclaration,
            "function declaration");
        expect(text(ast, ast.node(function).name) == "add",
            "function declaration: name");
        expect(ast.childCount(function) == 2,
            "function declaration: parameter count");
        expect(text(ast, ast.childAt(function, 0)) == "a" &&
            text(ast, ast.childAt(function, 1)) == "b",
            "function declaration: ordered parameters");
        const AstNodeId body = ast.node(function).body;
        expectKind(ast, body, AstNodeKind::BlockStatement,
            "function declaration: body");
        const AstNodeId returned = ast.childAt(body, 0);
        expectKind(ast, returned, AstNodeKind::ReturnStatement,
            "function declaration: return");
        const AstNodeId expression = ast.node(returned).expression;
        expectKind(ast, expression, AstNodeKind::BinaryExpression,
            "function declaration: return binary expression");
        expect(ast.node(expression).binaryOperator == AstBinaryOperator::Add,
            "function declaration: return operator");
        expect(text(ast, ast.node(expression).left) == "a" &&
            text(ast, ast.node(expression).right) == "b",
            "function declaration: return operands");
        expect(ast.node(function).location.offset == 0 &&
            ast.node(function).location.length == source.size(),
            "function declaration: authoritative location");
    });
}

void testCallAndAssignment()
{
    const std::string source = "x = add(x, 5);";
    expectSuccess(source, "call assignment", [](const Ast& ast) {
        const AstNodeId statement = ast.childAt(ast.root(), 0);
        const AstNodeId assignment = ast.node(statement).expression;
        expectKind(ast, assignment, AstNodeKind::AssignmentExpression,
            "call assignment: assignment");
        expect(ast.node(assignment).assignmentOperator ==
            AstAssignmentOperator::Assign,
            "call assignment: assignment operator");
        expectKind(ast, ast.node(assignment).left, AstNodeKind::Identifier,
            "call assignment: left target");
        const AstNodeId call = ast.node(assignment).right;
        expectKind(ast, call, AstNodeKind::CallExpression,
            "call assignment: call");
        expect(text(ast, ast.node(call).callee) == "add",
            "call assignment: callee");
        expect(ast.childCount(call) == 2, "call assignment: argument count");
        expect(text(ast, ast.childAt(call, 0)) == "x" &&
            text(ast, ast.childAt(call, 1)) == "5",
            "call assignment: ordered arguments");
    });
}

void testControlFlow()
{
    const std::string ifSource = "if (x >= 10) { x++; } else { x -= 2; }";
    expectSuccess(ifSource, "if statement", [](const Ast& ast) {
        const AstNodeId statement = ast.childAt(ast.root(), 0);
        expectKind(ast, statement, AstNodeKind::IfStatement, "if statement");
        const AstNodeId test = ast.node(statement).test;
        expectKind(ast, test, AstNodeKind::BinaryExpression, "if test");
        expect(ast.node(test).binaryOperator == AstBinaryOperator::GreaterEqual,
            "if test operator");
        const AstNodeId consequent = ast.node(statement).consequent;
        const AstNodeId alternate = ast.node(statement).alternate;
        expectKind(ast, consequent, AstNodeKind::BlockStatement, "if consequent");
        expectKind(ast, alternate, AstNodeKind::BlockStatement, "if alternate");
        const AstNodeId increment = ast.childAt(consequent, 0);
        const AstNodeId decrement = ast.childAt(alternate, 0);
        expectKind(ast, ast.node(increment).expression,
            AstNodeKind::UpdateExpression, "if increment");
        expect(!ast.node(ast.node(increment).expression).prefix,
            "if increment: postfix");
        expect(ast.node(ast.node(increment).expression).updateOperator ==
            AstUpdateOperator::Increment, "if increment: operator");
        const AstNodeId subtraction = ast.node(ast.node(decrement).expression).right;
        expectKind(ast, ast.node(decrement).expression,
            AstNodeKind::AssignmentExpression, "if assignment");
        expect(ast.node(ast.node(decrement).expression).assignmentOperator ==
            AstAssignmentOperator::Subtract, "if assignment: operator");
        expectKind(ast, subtraction, AstNodeKind::NumericLiteral,
            "if assignment: numeric right operand");
    });

    const std::string whileSource = "while (x < 10) { x++; }";
    expectSuccess(whileSource, "while statement", [](const Ast& ast) {
        const AstNodeId statement = ast.childAt(ast.root(), 0);
        expectKind(ast, statement, AstNodeKind::WhileStatement, "while statement");
        expect(ast.node(ast.node(statement).test).binaryOperator ==
            AstBinaryOperator::Less, "while test operator");
        expectKind(ast, ast.node(statement).body, AstNodeKind::BlockStatement,
            "while body");
    });

    const std::string forSource = "for (var i = 0; i < 10; i++) { x += i; }";
    expectSuccess(forSource, "for statement", [](const Ast& ast) {
        const AstNodeId statement = ast.childAt(ast.root(), 0);
        expectKind(ast, statement, AstNodeKind::ForStatement, "for statement");
        expectKind(ast, ast.node(statement).init, AstNodeKind::VariableDeclaration,
            "for initializer");
        expectKind(ast, ast.node(statement).test, AstNodeKind::BinaryExpression,
            "for test");
        expectKind(ast, ast.node(statement).update, AstNodeKind::UpdateExpression,
            "for update");
        expectKind(ast, ast.node(statement).body, AstNodeKind::BlockStatement,
            "for body");
        const AstNodeId bodyStatement = ast.childAt(ast.node(statement).body, 0);
        const AstNodeId bodyExpression = ast.node(bodyStatement).expression;
        expectKind(ast, bodyExpression, AstNodeKind::AssignmentExpression,
            "for body assignment");
        expect(ast.node(bodyExpression).assignmentOperator ==
            AstAssignmentOperator::Add, "for body assignment: operator");
    });
}

void testMemberCall()
{
    const std::string source = "document.getElementById(\"status\");";
    expectSuccess(source, "member call", [](const Ast& ast) {
        const AstNodeId statement = ast.childAt(ast.root(), 0);
        const AstNodeId call = ast.node(statement).expression;
        expectKind(ast, call, AstNodeKind::CallExpression, "member call: call");
        const AstNodeId method = ast.node(call).callee;
        expectKind(ast, method, AstNodeKind::MemberExpression,
            "member call: method member");
        expect(!ast.node(method).computed, "member call: dot member");
        const AstNodeId object = ast.node(method).object;
        expectKind(ast, object, AstNodeKind::Identifier,
            "member call: object identifier");
        expect(text(ast, object) == "document",
            "member call: object root");
        expect(text(ast, ast.node(method).property) == "getElementById",
            "member call: method name");
        expect(ast.childCount(call) == 1 &&
            text(ast, ast.childAt(call, 0)) == "\"status\"",
            "member call: string argument");
    });

    const std::string computedSource = "foo[\"bar\"].value();";
    expectSuccess(computedSource, "computed member", [](const Ast& ast) {
        const AstNodeId call = ast.node(ast.childAt(ast.root(), 0)).expression;
        const AstNodeId member = ast.node(call).callee;
        expectKind(ast, member, AstNodeKind::MemberExpression,
            "computed member: outer member");
        expect(!ast.node(member).computed, "computed member: outer dot");
        const AstNodeId computed = ast.node(member).object;
        expectKind(ast, computed, AstNodeKind::MemberExpression,
            "computed member: computed node");
        expect(ast.node(computed).computed, "computed member: bracket flag");
        expect(text(ast, ast.node(computed).property) == "\"bar\"",
            "computed member: property expression");
    });
}

void testPrecedence()
{
    const std::string additive = "a + b * c;";
    expectSuccess(additive, "additive precedence", [](const Ast& ast) {
        const AstNodeId expression = ast.node(ast.childAt(ast.root(), 0)).expression;
        expectKind(ast, expression, AstNodeKind::BinaryExpression,
            "additive precedence: root");
        expect(ast.node(expression).binaryOperator == AstBinaryOperator::Add,
            "additive precedence: root operator");
        expectKind(ast, ast.node(expression).right, AstNodeKind::BinaryExpression,
            "additive precedence: multiplicative child");
        expect(ast.node(ast.node(expression).right).binaryOperator ==
            AstBinaryOperator::Multiply, "additive precedence: child operator");
    });

    const std::string parenthesized = "(a + b) * c;";
    expectSuccess(parenthesized, "parenthesized precedence", [](const Ast& ast) {
        const AstNodeId expression = ast.node(ast.childAt(ast.root(), 0)).expression;
        expect(ast.node(expression).binaryOperator == AstBinaryOperator::Multiply,
            "parenthesized precedence: root operator");
        expect(ast.node(ast.node(expression).left).binaryOperator ==
            AstBinaryOperator::Add, "parenthesized precedence: left operator");
    });

    const std::string logical = "a || b && c;";
    expectSuccess(logical, "logical precedence", [](const Ast& ast) {
        const AstNodeId expression = ast.node(ast.childAt(ast.root(), 0)).expression;
        expectKind(ast, expression, AstNodeKind::LogicalExpression,
            "logical precedence: root");
        expect(ast.node(expression).logicalOperator == AstLogicalOperator::Or,
            "logical precedence: root OR");
        expectKind(ast, ast.node(expression).right, AstNodeKind::LogicalExpression,
            "logical precedence: AND child");
        expect(ast.node(ast.node(expression).right).logicalOperator ==
            AstLogicalOperator::And, "logical precedence: child AND");
    });

    const std::string assignment = "a = b = 5;";
    expectSuccess(assignment, "assignment associativity", [](const Ast& ast) {
        const AstNodeId expression = ast.node(ast.childAt(ast.root(), 0)).expression;
        expect(ast.node(expression).assignmentOperator ==
            AstAssignmentOperator::Assign, "assignment associativity: outer");
        expectKind(ast, ast.node(expression).right,
            AstNodeKind::AssignmentExpression, "assignment associativity: right child");
        expect(text(ast, ast.node(expression).left) == "a" &&
            text(ast, ast.node(ast.node(expression).right).left) == "b",
            "assignment associativity: targets");
    });

    const std::string call = "foo.bar(a + b * c);";
    expectSuccess(call, "call precedence", [](const Ast& ast) {
        const AstNodeId expression = ast.node(ast.childAt(ast.root(), 0)).expression;
        const AstNodeId argument = ast.childAt(expression, 0);
        expectKind(ast, argument, AstNodeKind::BinaryExpression,
            "call precedence: argument root");
        expectKind(ast, ast.node(argument).right, AstNodeKind::BinaryExpression,
            "call precedence: argument multiplicative child");
    });
}

void testAdditionalExpressions()
{
    const std::string source =
        "; var x; var y = 1, z = 2; !x; +x; -x; ++x; x--; "
        "new Foo(a, b); this; true; false; null; foo[\"bar\"];";
    expectSuccess(source, "additional grammar", [](const Ast& ast) {
        expect(ast.childCount(ast.root()) == 14,
            "additional grammar: statement count");
        expectKind(ast, ast.childAt(ast.root(), 0), AstNodeKind::EmptyStatement,
            "additional grammar: empty statement");
        expectKind(ast, ast.childAt(ast.root(), 1), AstNodeKind::VariableDeclaration,
            "additional grammar: uninitialized var");
        expectKind(ast, ast.childAt(ast.root(), 2), AstNodeKind::VariableDeclaration,
            "additional grammar: multi var");
        const AstNodeId unary = ast.node(ast.childAt(ast.root(), 3)).expression;
        expectKind(ast, unary, AstNodeKind::UnaryExpression,
            "additional grammar: not");
        expectKind(ast, ast.node(ast.childAt(ast.root(), 6)).expression,
            AstNodeKind::UpdateExpression, "additional grammar: prefix update");
        const AstNodeId newExpression = ast.node(ast.childAt(ast.root(), 8)).expression;
        expectKind(ast, newExpression, AstNodeKind::NewExpression,
            "additional grammar: new expression");
        expect(ast.childCount(newExpression) == 2,
            "additional grammar: new arguments");
        expectKind(ast, ast.node(ast.childAt(ast.root(), 9)).expression,
            AstNodeKind::ThisExpression, "additional grammar: this");
        expectKind(ast, ast.node(ast.childAt(ast.root(), 10)).expression,
            AstNodeKind::BooleanLiteral, "additional grammar: true");
        expectKind(ast, ast.node(ast.childAt(ast.root(), 11)).expression,
            AstNodeKind::BooleanLiteral, "additional grammar: false");
        expectKind(ast, ast.node(ast.childAt(ast.root(), 12)).expression,
            AstNodeKind::NullLiteral, "additional grammar: null");
        const AstNodeId member = ast.node(ast.childAt(ast.root(), 13)).expression;
        expectKind(ast, member, AstNodeKind::MemberExpression,
            "additional grammar: computed member");
        expect(ast.node(member).computed, "additional grammar: computed flag");
    });
}

void testObjectAndArrayLiterals()
{
    const std::string source =
        "var obj = { x: 1, \"hello\": [2, 3] };"
        "var values = [{ a: 4 }, [5, 6]];";
    expectSuccess(source, "object and array literals", [](const Ast& ast) {
        const AstNodeId objectDeclaration = ast.childAt(ast.root(), 0);
        const AstNodeId objectDeclarator = ast.childAt(objectDeclaration, 0);
        const AstNodeId object = ast.node(objectDeclarator).initializer;
        expectKind(ast, object, AstNodeKind::ObjectLiteral,
            "object literal: root");
        expect(ast.childCount(object) == 2, "object literal: property count");
        const AstNodeId firstProperty = ast.childAt(object, 0);
        expectKind(ast, firstProperty, AstNodeKind::ObjectProperty,
            "object literal: first property");
        expect(text(ast, ast.node(firstProperty).key) == "x",
            "object literal: identifier key");
        expectKind(ast, ast.node(firstProperty).initializer,
            AstNodeKind::NumericLiteral, "object literal: first value");
        const AstNodeId secondProperty = ast.childAt(object, 1);
        expect(text(ast, ast.node(secondProperty).key) == "\"hello\"",
            "object literal: string key");
        expectKind(ast, ast.node(secondProperty).initializer,
            AstNodeKind::ArrayLiteral, "object literal: nested array");
        expect(ast.childCount(ast.node(secondProperty).initializer) == 2,
            "array literal: nested element count");

        const AstNodeId valuesDeclaration = ast.childAt(ast.root(), 1);
        const AstNodeId values = ast.node(ast.childAt(valuesDeclaration, 0)).initializer;
        expectKind(ast, values, AstNodeKind::ArrayLiteral,
            "array literal: root");
        expect(ast.childCount(values) == 2, "array literal: element count");
        expectKind(ast, ast.childAt(values, 0), AstNodeKind::ObjectLiteral,
            "array literal: nested object");
        expectKind(ast, ast.childAt(values, 1), AstNodeKind::ArrayLiteral,
            "array literal: nested array");
    });

    expectError("var x = { a: 1;", ParserErrorCode::ExpectedToken, 14,
        "malformed object literal");
    expectError("var x = [1, 2;", ParserErrorCode::ExpectedToken, 13,
        "malformed array literal");
}

void testNegativeSyntax()
{
    expectError("var = 1;", ParserErrorCode::ExpectedToken, 4,
        "missing variable name");
    expectError("if (", ParserErrorCode::UnexpectedEndOfInput, 4,
        "truncated if");
    expectError("function foo(a, {", ParserErrorCode::ExpectedToken, 16,
        "malformed parameter list");
    expectError("1 = x;", ParserErrorCode::InvalidAssignmentTarget, 0,
        "invalid assignment target");
    expectError("foo(;", ParserErrorCode::InvalidExpression, 4,
        "malformed call");
    expectError("for (;; {", ParserErrorCode::InvalidExpression, 8,
        "malformed for header");
}

void testLimits()
{
    ParserLimits nodes;
    nodes.maxAstNodes = 3;
    expectError("a + b;", ParserErrorCode::AstNodeLimitExceeded, 0,
        "AST node limit", nodes);

    ParserLimits parserDepth;
    parserDepth.maxExpressionNesting = 3;
    expectError("((((1))));", ParserErrorCode::NestingLimitExceeded, 3,
        "expression nesting limit", parserDepth);

    ParserLimits recursiveDepth;
    recursiveDepth.maxParserDepth = 4;
    const ParseResult recursiveDepthResult = parse("a = b = c;", recursiveDepth);
    expect(recursiveDepthResult.error.code == ParserErrorCode::NestingLimitExceeded,
        "parser recursion depth limit");

    ParserLimits blocks;
    blocks.maxBlockNesting = 2;
    expectError("{{{;}}}", ParserErrorCode::NestingLimitExceeded, 2,
        "block nesting limit", blocks);

    ParserLimits statements;
    statements.maxStatements = 2;
    expectError(";;;", ParserErrorCode::TooManyStatements, 2,
        "statement limit", statements);

    ParserLimits parameters;
    parameters.maxFunctionParameters = 2;
    expectError("function f(a, b, c) {}", ParserErrorCode::TooManyParameters, 17,
        "parameter limit", parameters);

    ParserLimits arguments;
    arguments.maxCallArguments = 2;
    expectError("foo(a, b, c);", ParserErrorCode::TooManyArguments, 10,
        "argument limit", arguments);

    ParserLimits properties;
    properties.maxObjectLiteralProperties = 1;
    expectError("var x = { a: 1, b: 2 };",
        ParserErrorCode::TooManyObjectProperties, 16,
        "object literal property limit", properties);

    ParserLimits elements;
    elements.maxArrayLiteralElements = 1;
    expectError("var x = [1, 2];", ParserErrorCode::TooManyArrayElements, 12,
        "array literal element limit", elements);
}

void testLocationsAndLexerPropagation()
{
    const std::string source = "\n  x = 1;";
    expectSuccess(source, "source locations", [](const Ast& ast) {
        const AstNodeId statement = ast.childAt(ast.root(), 0);
        const AstNodeId assignment = ast.node(statement).expression;
        expect(ast.node(assignment).location.line == 2 &&
            ast.node(assignment).location.column == 3,
            "source locations: assignment start");
        expect(ast.node(ast.node(assignment).left).location.line == 2 &&
            ast.node(ast.node(assignment).left).location.column == 3,
            "source locations: identifier token location");
    });

    const std::string invalidSource = "@";
    const Lexer lexer;
    const auto lexed = lexer.tokenize(SourceView(invalidSource.data(), invalidSource.size()));
    const Parser parser;
    const ParseResult result = parser.parse(
        SourceView(invalidSource.data(), invalidSource.size()), lexed);
    expect(result.error.code == ParserErrorCode::LexerFailure,
        "lexer failure propagates to parser result");
    expect(result.error.location.offset == 0,
        "lexer failure location propagates to parser result");
}

} // namespace

int main()
{
    testVariableDeclaration();
    testFunctionDeclaration();
    testCallAndAssignment();
    testControlFlow();
    testMemberCall();
    testPrecedence();
    testAdditionalExpressions();
    testObjectAndArrayLiterals();
    testNegativeSyntax();
    testLimits();
    testLocationsAndLexerPropagation();

    if (failures != 0) {
        std::cerr << "Navigator JavaScript parser tests FAIL (" << failures << ")\n";
        return 1;
    }
    std::cout << "Navigator JavaScript parser tests PASS\n";
    return 0;
}
