#include "navigator_javascript/lexer.h"

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace {

using gxos::javascript::LexResult;
using gxos::javascript::Lexer;
using gxos::javascript::LexerErrorCode;
using gxos::javascript::LexerLimits;
using gxos::javascript::SourceView;
using gxos::javascript::Token;
using gxos::javascript::TokenType;

int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

struct ExpectedToken {
    TokenType type;
    const char* lexeme;
    std::size_t offset;
    std::size_t line;
    std::size_t column;
};

std::string tokenText(const Token& token)
{
    if (token.lexeme.data == nullptr) return std::string();
    return std::string(token.lexeme.data, token.lexeme.length);
}

void expectExactStream(const std::string& source,
    const std::vector<ExpectedToken>& expected, const std::string& label)
{
    const Lexer lexer;
    const LexResult result = lexer.tokenize(SourceView(source.data(), source.size()));
    expect(result.succeeded(), label + ": tokenization succeeded");
    if (!result.succeeded()) return;
    expect(result.tokens.size() == expected.size(), label + ": token count matches");
    const std::size_t count = result.tokens.size() < expected.size()
        ? result.tokens.size() : expected.size();
    for (std::size_t i = 0; i < count; ++i) {
        const Token& actual = result.tokens[i];
        const ExpectedToken& wanted = expected[i];
        const std::string prefix = label + ": token " + std::to_string(i);
        expect(actual.type == wanted.type, prefix + ": type matches");
        expect(tokenText(actual) == wanted.lexeme, prefix + ": lexeme matches");
        expect(actual.location.offset == wanted.offset, prefix + ": offset matches");
        expect(actual.location.length == std::string(wanted.lexeme).size(),
            prefix + ": length matches");
        expect(actual.location.line == wanted.line, prefix + ": line matches");
        expect(actual.location.column == wanted.column, prefix + ": column matches");
    }
}

void expectTypesAndLexemes(const std::string& source,
    const std::vector<TokenType>& types, const std::vector<std::string>& lexemes,
    const std::string& label)
{
    const Lexer lexer;
    const LexResult result = lexer.tokenize(SourceView(source.data(), source.size()));
    expect(result.succeeded(), label + ": tokenization succeeded");
    if (!result.succeeded()) return;
    expect(result.tokens.size() == types.size(), label + ": type count matches");
    expect(result.tokens.size() == lexemes.size(), label + ": lexeme count matches");
    const std::size_t count = result.tokens.size() < types.size()
        ? result.tokens.size() : types.size();
    for (std::size_t i = 0; i < count; ++i) {
        expect(result.tokens[i].type == types[i],
            label + ": type " + std::to_string(i) + " matches");
        expect(tokenText(result.tokens[i]) == lexemes[i],
            label + ": lexeme " + std::to_string(i) + " matches");
    }
}

void expectError(const std::string& source, LexerErrorCode code,
    std::size_t offset, std::size_t line, std::size_t column,
    const std::string& label, LexerLimits limits = LexerLimits())
{
    const Lexer lexer(limits);
    const LexResult result = lexer.tokenize(SourceView(source.data(), source.size()));
    expect(!result.succeeded(), label + ": tokenization fails");
    expect(result.error.code == code, label + ": error code matches");
    expect(result.error.location.offset == offset, label + ": error offset matches");
    expect(result.error.location.line == line, label + ": error line matches");
    expect(result.error.location.column == column, label + ": error column matches");
    expect(result.tokens.empty(), label + ": failed result has no partial token stream");
}

void testBasicAndFunctionStreams()
{
    expectExactStream("var x = 10;", {
        {TokenType::KeywordVar, "var", 0, 1, 1},
        {TokenType::Identifier, "x", 4, 1, 5},
        {TokenType::Assign, "=", 6, 1, 7},
        {TokenType::NumericLiteral, "10", 8, 1, 9},
        {TokenType::Semicolon, ";", 10, 1, 11},
        {TokenType::EndOfInput, "", 11, 1, 12},
    }, "basic source");

    expectExactStream("function add(a, b) {\n    return a + b;\n}", {
        {TokenType::KeywordFunction, "function", 0, 1, 1},
        {TokenType::Identifier, "add", 9, 1, 10},
        {TokenType::LeftParen, "(", 12, 1, 13},
        {TokenType::Identifier, "a", 13, 1, 14},
        {TokenType::Comma, ",", 14, 1, 15},
        {TokenType::Identifier, "b", 16, 1, 17},
        {TokenType::RightParen, ")", 17, 1, 18},
        {TokenType::LeftBrace, "{", 19, 1, 20},
        {TokenType::KeywordReturn, "return", 25, 2, 5},
        {TokenType::Identifier, "a", 32, 2, 12},
        {TokenType::Plus, "+", 34, 2, 14},
        {TokenType::Identifier, "b", 36, 2, 16},
        {TokenType::Semicolon, ";", 37, 2, 17},
        {TokenType::RightBrace, "}", 39, 3, 1},
        {TokenType::EndOfInput, "", 40, 3, 2},
    }, "function source");
}

void testControlFlowAndComments()
{
    expectTypesAndLexemes("if (x >= 10) { x++; } else { x -= 2; }", {
        TokenType::KeywordIf, TokenType::LeftParen, TokenType::Identifier,
        TokenType::GreaterEqual, TokenType::NumericLiteral, TokenType::RightParen,
        TokenType::LeftBrace, TokenType::Identifier, TokenType::Increment,
        TokenType::Semicolon, TokenType::RightBrace, TokenType::KeywordElse,
        TokenType::LeftBrace, TokenType::Identifier, TokenType::MinusAssign,
        TokenType::NumericLiteral, TokenType::Semicolon, TokenType::RightBrace,
        TokenType::EndOfInput,
    }, {
        "if", "(", "x", ">=", "10", ")", "{", "x", "++", ";", "}",
        "else", "{", "x", "-=", "2", ";", "}", "",
    }, "control flow");

    const std::string source = "// hello\nvar x = 1;\n\n/* world */\nx = x + 1;";
    expectTypesAndLexemes(source, {
        TokenType::KeywordVar, TokenType::Identifier, TokenType::Assign,
        TokenType::NumericLiteral, TokenType::Semicolon, TokenType::Identifier,
        TokenType::Assign, TokenType::Identifier, TokenType::Plus,
        TokenType::NumericLiteral, TokenType::Semicolon, TokenType::EndOfInput,
    }, {
        "var", "x", "=", "1", ";", "x", "=", "x", "+", "1", ";", "",
    }, "comments");

    const Lexer lexer;
    const LexResult result = lexer.tokenize(SourceView(source.data(), source.size()));
    expect(result.succeeded(), "comments: location tokenization succeeded");
    if (result.succeeded()) {
        expect(result.tokens[0].location.line == 2 && result.tokens[0].location.column == 1,
            "comments: token after line comment starts on line 2");
        expect(result.tokens[5].location.line == 5 && result.tokens[5].location.column == 1,
            "comments: token after block comment starts on line 5");
    }
}

void testStringsNumbersKeywordsAndPunctuation()
{
    expectTypesAndLexemes("var a = \"hello\"; var b = 'world\\n';", {
        TokenType::KeywordVar, TokenType::Identifier, TokenType::Assign,
        TokenType::StringLiteral, TokenType::Semicolon, TokenType::KeywordVar,
        TokenType::Identifier, TokenType::Assign, TokenType::StringLiteral,
        TokenType::Semicolon, TokenType::EndOfInput,
    }, {
        "var", "a", "=", "\"hello\"", ";", "var", "b", "=",
        "'world\\n'", ";", "",
    }, "strings");

    const std::string numbers = "0 123 12.5 .5 5. 1e3 1E-3 0x10 0XFF";
    const std::vector<std::string> numberLexemes = {
        "0", "123", "12.5", ".5", "5.", "1e3", "1E-3", "0x10", "0XFF", "",
    };
    std::vector<TokenType> numberTypes(numberLexemes.size(), TokenType::NumericLiteral);
    numberTypes.back() = TokenType::EndOfInput;
    expectTypesAndLexemes(numbers, numberTypes, numberLexemes, "numbers");

    expectTypesAndLexemes(
        "var function return if else while for break continue true false null new this undefined",
        {
            TokenType::KeywordVar, TokenType::KeywordFunction, TokenType::KeywordReturn,
            TokenType::KeywordIf, TokenType::KeywordElse, TokenType::KeywordWhile,
            TokenType::KeywordFor, TokenType::KeywordBreak, TokenType::KeywordContinue,
            TokenType::KeywordTrue, TokenType::KeywordFalse, TokenType::KeywordNull,
            TokenType::KeywordNew, TokenType::KeywordThis, TokenType::Identifier,
            TokenType::EndOfInput,
        }, {
            "var", "function", "return", "if", "else", "while", "for", "break",
            "continue", "true", "false", "null", "new", "this", "undefined", "",
        }, "keywords");

    expectTypesAndLexemes("( ) { } [ ] . , ; : ?", {
        TokenType::LeftParen, TokenType::RightParen, TokenType::LeftBrace,
        TokenType::RightBrace, TokenType::LeftBracket, TokenType::RightBracket,
        TokenType::Dot, TokenType::Comma, TokenType::Semicolon, TokenType::Colon,
        TokenType::Question, TokenType::EndOfInput,
    }, {"(", ")", "{", "}", "[", "]", ".", ",", ";", ":", "?", ""},
        "punctuation");
}

void testLongestMatchOperators()
{
    expectTypesAndLexemes("= == === ! != !== < <= > >= + ++ += - -- -= && ||", {
        TokenType::Assign, TokenType::Equal, TokenType::StrictEqual,
        TokenType::Not, TokenType::NotEqual, TokenType::StrictNotEqual,
        TokenType::Less, TokenType::LessEqual, TokenType::Greater,
        TokenType::GreaterEqual, TokenType::Plus, TokenType::Increment,
        TokenType::PlusAssign, TokenType::Minus, TokenType::Decrement,
        TokenType::MinusAssign, TokenType::LogicalAnd, TokenType::LogicalOr,
        TokenType::EndOfInput,
    }, {
        "=", "==", "===", "!", "!=", "!==", "<", "<=", ">", ">=", "+", "++",
        "+=", "-", "--", "-=", "&&", "||", "",
    }, "longest-match operators");

    expectTypesAndLexemes("* / % *= /= %=", {
        TokenType::Star, TokenType::Slash, TokenType::Percent,
        TokenType::StarAssign, TokenType::SlashAssign, TokenType::PercentAssign,
        TokenType::EndOfInput,
    }, {"*", "/", "%", "*=", "/=", "%=", ""}, "arithmetic operators");
}

void testLocationsAcrossLineTerminators()
{
    const std::string source = "a\r\n  b\r c\n\t d";
    expectExactStream(source, {
        {TokenType::Identifier, "a", 0, 1, 1},
        {TokenType::Identifier, "b", 5, 2, 3},
        {TokenType::Identifier, "c", 8, 3, 2},
        {TokenType::Identifier, "d", 12, 4, 3},
        {TokenType::EndOfInput, "", 13, 4, 4},
    }, "CR/LF/CRLF locations");
}

void testExplicitSourceExtent()
{
    const char nonTerminatedSource[] = {'v', 'a', 'r', ' ', 'x'};
    const Lexer lexer;
    const LexResult nonTerminated = lexer.tokenize(
        SourceView(nonTerminatedSource, sizeof(nonTerminatedSource)));
    expect(nonTerminated.succeeded(), "explicit source extent: no terminator required");
    if (nonTerminated.succeeded()) {
        expect(nonTerminated.tokens.size() == 3,
            "explicit source extent: exact token count");
        expect(nonTerminated.tokens[0].type == TokenType::KeywordVar,
            "explicit source extent: keyword token");
        expect(nonTerminated.tokens[1].type == TokenType::Identifier &&
            tokenText(nonTerminated.tokens[1]) == "x",
            "explicit source extent: identifier token");
        expect(nonTerminated.tokens[2].type == TokenType::EndOfInput,
            "explicit source extent: EOF token");
    }

    const char boundedPrefix[] = {'a', '@'};
    const LexResult bounded = lexer.tokenize(SourceView(boundedPrefix, 1));
    expect(bounded.succeeded(), "explicit source extent: trailing byte ignored");
    if (bounded.succeeded()) {
        expect(bounded.tokens.size() == 2 && bounded.tokens[0].lexeme.length == 1,
            "explicit source extent: authoritative length is honored");
    }
}

void testErrorsAndLimits()
{
    expectError("\"unterminated", LexerErrorCode::UnterminatedString,
        0, 1, 1, "unterminated string");
    expectError("/* unterminated", LexerErrorCode::UnterminatedBlockComment,
        0, 1, 1, "unterminated block comment");
    expectError("1e+", LexerErrorCode::MalformedNumericLiteral,
        0, 1, 1, "malformed exponent");
    expectError("123abc", LexerErrorCode::MalformedNumericLiteral,
        0, 1, 1, "malformed numeric suffix");
    expectError("@", LexerErrorCode::UnexpectedCharacter,
        0, 1, 1, "unsupported character");
    expectError("'\\x'", LexerErrorCode::UnsupportedEscape,
        1, 1, 2, "unsupported string escape");

    LexerLimits sourceLimits;
    sourceLimits.maxSourceBytes = 3;
    expectError("abcd", LexerErrorCode::SourceTooLarge,
        0, 1, 1, "source limit", sourceLimits);

    LexerLimits tokenLimits;
    tokenLimits.maxTokenCount = 1;
    expectError("a b", LexerErrorCode::TooManyTokens,
        2, 1, 3, "token count limit", tokenLimits);

    LexerLimits lengthLimits;
    lengthLimits.maxTokenBytes = 2;
    expectError("abc", LexerErrorCode::TokenTooLong,
        0, 1, 1, "token length limit", lengthLimits);

    const Lexer lexer;
    const LexResult invalidSource = lexer.tokenize(SourceView(nullptr, 1));
    expect(!invalidSource.succeeded(), "invalid source: tokenization fails");
    expect(invalidSource.error.code == LexerErrorCode::InvalidSource,
        "invalid source: error code matches");
}

void testDeterministicSafetyInputs()
{
    const std::vector<std::string> inputs = {
        "", "/", "/*", "//", "'", "\"", "1e", "0x", "(", "&&", "\r\n",
        "a/*x*/b", "\"\\", "...", "0xG", "'\\x'", "{[()]}", "//x\r\n/*y*/",
    };
    const Lexer lexer;
    for (const std::string& input : inputs) {
        for (int repeat = 0; repeat < 4; ++repeat) {
            const LexResult result = lexer.tokenize(SourceView(input.data(), input.size()));
            if (result.succeeded()) {
                expect(!result.tokens.empty(), "safety input: successful result has EOF");
                if (!result.tokens.empty()) {
                    expect(result.tokens.back().type == TokenType::EndOfInput,
                        "safety input: successful result ends with EOF");
                }
            }
        }
    }
}

} // namespace

int main()
{
    testBasicAndFunctionStreams();
    testControlFlowAndComments();
    testStringsNumbersKeywordsAndPunctuation();
    testLongestMatchOperators();
    testLocationsAcrossLineTerminators();
    testExplicitSourceExtent();
    testErrorsAndLimits();
    testDeterministicSafetyInputs();

    if (failures != 0) {
        std::cerr << "Navigator JavaScript lexer tests FAIL (" << failures << ")\n";
        return 1;
    }
    std::cout << "Navigator JavaScript lexer tests PASS\n";
    return 0;
}
