#include "lexer.h"

#include <algorithm>
#include <cstddef>
#include <limits>

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

bool isIdentifierStart(char value)
{
    return (value >= 'A' && value <= 'Z') ||
        (value >= 'a' && value <= 'z') ||
        value == '_' || value == '$';
}

bool isIdentifierPart(char value)
{
    return isIdentifierStart(value) || isAsciiDigit(value);
}

bool isLineTerminator(char value)
{
    return value == '\r' || value == '\n';
}

bool isSupportedStringEscape(char value)
{
    return value == '\\' || value == '\'' || value == '"' ||
        value == 'n' || value == 'r' || value == 't' ||
        value == 'b' || value == 'f';
}

TokenType keywordType(SourceView source, std::size_t offset, std::size_t length)
{
    struct Keyword {
        const char* spelling;
        std::size_t length;
        TokenType type;
    };

    static const Keyword keywords[] = {
        {"var", 3, TokenType::KeywordVar},
        {"function", 8, TokenType::KeywordFunction},
        {"return", 6, TokenType::KeywordReturn},
        {"if", 2, TokenType::KeywordIf},
        {"else", 4, TokenType::KeywordElse},
        {"while", 5, TokenType::KeywordWhile},
        {"for", 3, TokenType::KeywordFor},
        {"break", 5, TokenType::KeywordBreak},
        {"continue", 8, TokenType::KeywordContinue},
        {"true", 4, TokenType::KeywordTrue},
        {"false", 5, TokenType::KeywordFalse},
        {"null", 4, TokenType::KeywordNull},
        {"new", 3, TokenType::KeywordNew},
        {"this", 4, TokenType::KeywordThis},
    };

    for (const Keyword& keyword : keywords) {
        if (keyword.length != length) continue;
        bool matches = true;
        for (std::size_t i = 0; i < length; ++i) {
            if (source.data[offset + i] != keyword.spelling[i]) {
                matches = false;
                break;
            }
        }
        if (matches) return keyword.type;
    }
    return TokenType::Identifier;
}

struct Scanner {
    SourceView source;
    LexerLimits limits;
    LexResult result;
    std::size_t offset = 0;
    std::size_t line = 1;
    std::size_t column = 1;

    SourceLocation currentLocation() const
    {
        SourceLocation location;
        location.offset = offset;
        location.line = line;
        location.column = column;
        return location;
    }

    SourceLocation startLocation() const
    {
        return currentLocation();
    }

    bool atEnd() const
    {
        return offset >= source.length;
    }

    char peek(std::size_t lookahead = 0) const
    {
        if (lookahead > source.length - offset) return '\0';
        const std::size_t index = offset + lookahead;
        if (index >= source.length) return '\0';
        return source.data[index];
    }

    void fail(LexerErrorCode code, SourceLocation location,
        bool hasOffendingByte = false, char offendingByte = '\0')
    {
        if (result.error.code != LexerErrorCode::None) return;
        result.tokens.clear();
        result.error.code = code;
        result.error.location = location;
        result.error.hasOffendingByte = hasOffendingByte;
        result.error.offendingByte = static_cast<std::uint8_t>(offendingByte);
    }

    bool checkTokenLength(const SourceLocation& start)
    {
        if (offset - start.offset <= limits.maxTokenBytes) return true;
        fail(LexerErrorCode::TokenTooLong, start);
        return false;
    }

    bool canEmit(const SourceLocation& start, std::size_t tokenLength)
    {
        if (tokenLength > limits.maxTokenBytes) {
            fail(LexerErrorCode::TokenTooLong, start);
            return false;
        }
        if (result.tokens.size() >= limits.maxTokenCount) {
            fail(LexerErrorCode::TooManyTokens, start);
            return false;
        }
        return true;
    }

    bool emit(TokenType type, const SourceLocation& start)
    {
        const std::size_t tokenLength = offset - start.offset;
        if (!canEmit(start, tokenLength)) return false;

        SourceLocation location = start;
        location.length = tokenLength;
        const char* tokenData = source.data == nullptr
            ? nullptr : source.data + start.offset;
        result.tokens.push_back(Token{type, location, SourceView(tokenData, tokenLength)});
        return true;
    }

    void advance()
    {
        if (atEnd()) return;
        const char value = source.data[offset++];
        if (value == '\r') {
            if (offset < source.length && source.data[offset] == '\n') ++offset;
            ++line;
            column = 1;
        } else if (value == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
    }

    bool matches(const char* spelling, std::size_t length) const
    {
        if (length > source.length - offset) return false;
        for (std::size_t i = 0; i < length; ++i) {
            if (source.data[offset + i] != spelling[i]) return false;
        }
        return true;
    }

    bool consume(std::size_t count, const SourceLocation& start)
    {
        for (std::size_t i = 0; i < count; ++i) advance();
        return checkTokenLength(start);
    }

    bool skipWhitespaceAndComments()
    {
        while (!atEnd()) {
            const char value = peek();
            if (value == ' ' || value == '\t' || value == '\v' || value == '\f' ||
                value == '\r' || value == '\n') {
                advance();
                continue;
            }

            if (matches("//", 2)) {
                advance();
                advance();
                while (!atEnd() && !isLineTerminator(peek())) advance();
                continue;
            }

            if (matches("/*", 2)) {
                const SourceLocation commentStart = startLocation();
                advance();
                advance();
                bool closed = false;
                while (!atEnd()) {
                    if (matches("*/", 2)) {
                        advance();
                        advance();
                        closed = true;
                        break;
                    }
                    advance();
                }
                if (!closed) {
                    fail(LexerErrorCode::UnterminatedBlockComment, commentStart);
                    return false;
                }
                continue;
            }
            break;
        }
        return true;
    }

    bool scanIdentifier()
    {
        const SourceLocation start = startLocation();
        advance();
        if (!checkTokenLength(start)) return false;
        while (!atEnd() && isIdentifierPart(peek())) {
            advance();
            if (!checkTokenLength(start)) return false;
        }
        const TokenType type = keywordType(source, start.offset, offset - start.offset);
        return emit(type, start);
    }

    bool scanNumber()
    {
        const SourceLocation start = startLocation();
        const bool startsWithDot = peek() == '.';
        if (startsWithDot) {
            advance();
            if (!checkTokenLength(start)) return false;
            while (!atEnd() && isAsciiDigit(peek())) {
                advance();
                if (!checkTokenLength(start)) return false;
            }
        } else {
            while (!atEnd() && isAsciiDigit(peek())) {
                advance();
                if (!checkTokenLength(start)) return false;
            }

            if (!atEnd() && (peek() == 'x' || peek() == 'X') &&
                source.data[start.offset] == '0') {
                advance();
                if (!checkTokenLength(start)) return false;
                const std::size_t hexStart = offset;
                while (!atEnd() && isAsciiHexDigit(peek())) {
                    advance();
                    if (!checkTokenLength(start)) return false;
                }
                if (offset == hexStart) {
                    fail(LexerErrorCode::MalformedNumericLiteral, start);
                    return false;
                }
                if (!atEnd() && (isIdentifierPart(peek()) || peek() == '.')) {
                    fail(LexerErrorCode::MalformedNumericLiteral, start);
                    return false;
                }
                return emit(TokenType::NumericLiteral, start);
            }

            if (!atEnd() && peek() == '.') {
                advance();
                if (!checkTokenLength(start)) return false;
                while (!atEnd() && isAsciiDigit(peek())) {
                    advance();
                    if (!checkTokenLength(start)) return false;
                }
            }
        }

        if (!atEnd() && (peek() == 'e' || peek() == 'E')) {
            advance();
            if (!checkTokenLength(start)) return false;
            if (!atEnd() && (peek() == '+' || peek() == '-')) {
                advance();
                if (!checkTokenLength(start)) return false;
            }
            const std::size_t exponentStart = offset;
            while (!atEnd() && isAsciiDigit(peek())) {
                advance();
                if (!checkTokenLength(start)) return false;
            }
            if (offset == exponentStart) {
                fail(LexerErrorCode::MalformedNumericLiteral, start);
                return false;
            }
        }

        if (!atEnd() && (isIdentifierPart(peek()) || peek() == '.')) {
            fail(LexerErrorCode::MalformedNumericLiteral, start);
            return false;
        }
        return emit(TokenType::NumericLiteral, start);
    }

    bool scanString()
    {
        const SourceLocation start = startLocation();
        const char quote = peek();
        advance();
        if (!checkTokenLength(start)) return false;

        while (!atEnd()) {
            const char value = peek();
            if (value == quote) {
                advance();
                return emit(TokenType::StringLiteral, start);
            }
            if (isLineTerminator(value)) {
                fail(LexerErrorCode::UnterminatedString, start);
                return false;
            }
            if (value == '\\') {
                const SourceLocation escapeLocation = currentLocation();
                advance();
                if (atEnd() || isLineTerminator(peek())) {
                    fail(LexerErrorCode::UnterminatedString, start);
                    return false;
                }
                if (!isSupportedStringEscape(peek())) {
                    fail(LexerErrorCode::UnsupportedEscape, escapeLocation, true, peek());
                    return false;
                }
                advance();
            } else {
                advance();
            }
            if (!checkTokenLength(start)) return false;
        }

        fail(LexerErrorCode::UnterminatedString, start);
        return false;
    }

    bool scanOperatorOrPunctuation()
    {
        const SourceLocation start = startLocation();
        const char value = peek();

        if (value == '(') { advance(); return emit(TokenType::LeftParen, start); }
        if (value == ')') { advance(); return emit(TokenType::RightParen, start); }
        if (value == '{') { advance(); return emit(TokenType::LeftBrace, start); }
        if (value == '}') { advance(); return emit(TokenType::RightBrace, start); }
        if (value == '[') { advance(); return emit(TokenType::LeftBracket, start); }
        if (value == ']') { advance(); return emit(TokenType::RightBracket, start); }
        if (value == '.') { advance(); return emit(TokenType::Dot, start); }
        if (value == ',') { advance(); return emit(TokenType::Comma, start); }
        if (value == ';') { advance(); return emit(TokenType::Semicolon, start); }
        if (value == ':') { advance(); return emit(TokenType::Colon, start); }
        if (value == '?') { advance(); return emit(TokenType::Question, start); }

        if (value == '=') {
            if (matches("===", 3)) { consume(3, start); return emit(TokenType::StrictEqual, start); }
            if (matches("==", 2)) { consume(2, start); return emit(TokenType::Equal, start); }
            advance(); return emit(TokenType::Assign, start);
        }
        if (value == '!') {
            if (matches("!==", 3)) { consume(3, start); return emit(TokenType::StrictNotEqual, start); }
            if (matches("!=", 2)) { consume(2, start); return emit(TokenType::NotEqual, start); }
            advance(); return emit(TokenType::Not, start);
        }
        if (value == '<') {
            if (matches("<=", 2)) { consume(2, start); return emit(TokenType::LessEqual, start); }
            advance(); return emit(TokenType::Less, start);
        }
        if (value == '>') {
            if (matches(">=", 2)) { consume(2, start); return emit(TokenType::GreaterEqual, start); }
            advance(); return emit(TokenType::Greater, start);
        }
        if (value == '+') {
            if (matches("++", 2)) { consume(2, start); return emit(TokenType::Increment, start); }
            if (matches("+=", 2)) { consume(2, start); return emit(TokenType::PlusAssign, start); }
            advance(); return emit(TokenType::Plus, start);
        }
        if (value == '-') {
            if (matches("--", 2)) { consume(2, start); return emit(TokenType::Decrement, start); }
            if (matches("-=", 2)) { consume(2, start); return emit(TokenType::MinusAssign, start); }
            advance(); return emit(TokenType::Minus, start);
        }
        if (value == '*') {
            if (matches("*=", 2)) { consume(2, start); return emit(TokenType::StarAssign, start); }
            advance(); return emit(TokenType::Star, start);
        }
        if (value == '/') {
            if (matches("/=", 2)) { consume(2, start); return emit(TokenType::SlashAssign, start); }
            advance(); return emit(TokenType::Slash, start);
        }
        if (value == '%') {
            if (matches("%=", 2)) { consume(2, start); return emit(TokenType::PercentAssign, start); }
            advance(); return emit(TokenType::Percent, start);
        }
        if (value == '&' && matches("&&", 2)) {
            consume(2, start); return emit(TokenType::LogicalAnd, start);
        }
        if (value == '|' && matches("||", 2)) {
            consume(2, start); return emit(TokenType::LogicalOr, start);
        }

        const SourceLocation bad = currentLocation();
        advance();
        fail(LexerErrorCode::UnexpectedCharacter, bad, true, value);
        return false;
    }

    LexResult run()
    {
        if (source.length > limits.maxSourceBytes) {
            fail(LexerErrorCode::SourceTooLarge, currentLocation());
            return result;
        }
        if (source.length != 0 && source.data == nullptr) {
            fail(LexerErrorCode::InvalidSource, currentLocation());
            return result;
        }

        const std::size_t maxReservableTokens = source.length ==
            std::numeric_limits<std::size_t>::max()
            ? source.length : source.length + 1;
        result.tokens.reserve(std::min(limits.maxTokenCount, maxReservableTokens));

        while (true) {
            if (!skipWhitespaceAndComments()) return result;
            if (atEnd()) {
                const SourceLocation end = currentLocation();
                emit(TokenType::EndOfInput, end);
                return result;
            }

            const char value = peek();
            if (isIdentifierStart(value)) {
                if (!scanIdentifier()) return result;
                continue;
            }
            if (isAsciiDigit(value) || (value == '.' && isAsciiDigit(peek(1)))) {
                if (!scanNumber()) return result;
                continue;
            }
            if (value == '\'' || value == '"') {
                if (!scanString()) return result;
                continue;
            }
            if (!scanOperatorOrPunctuation()) return result;
        }
    }
};

} // namespace

LexResult Lexer::tokenize(SourceView source) const
{
    Scanner scanner;
    scanner.source = source;
    scanner.limits = limits_;
    return scanner.run();
}

const char* tokenTypeName(TokenType type)
{
    switch (type) {
    case TokenType::EndOfInput: return "EndOfInput";
    case TokenType::Identifier: return "Identifier";
    case TokenType::NumericLiteral: return "NumericLiteral";
    case TokenType::StringLiteral: return "StringLiteral";
    case TokenType::KeywordVar: return "KeywordVar";
    case TokenType::KeywordFunction: return "KeywordFunction";
    case TokenType::KeywordReturn: return "KeywordReturn";
    case TokenType::KeywordIf: return "KeywordIf";
    case TokenType::KeywordElse: return "KeywordElse";
    case TokenType::KeywordWhile: return "KeywordWhile";
    case TokenType::KeywordFor: return "KeywordFor";
    case TokenType::KeywordBreak: return "KeywordBreak";
    case TokenType::KeywordContinue: return "KeywordContinue";
    case TokenType::KeywordTrue: return "KeywordTrue";
    case TokenType::KeywordFalse: return "KeywordFalse";
    case TokenType::KeywordNull: return "KeywordNull";
    case TokenType::KeywordNew: return "KeywordNew";
    case TokenType::KeywordThis: return "KeywordThis";
    case TokenType::LeftParen: return "LeftParen";
    case TokenType::RightParen: return "RightParen";
    case TokenType::LeftBrace: return "LeftBrace";
    case TokenType::RightBrace: return "RightBrace";
    case TokenType::LeftBracket: return "LeftBracket";
    case TokenType::RightBracket: return "RightBracket";
    case TokenType::Dot: return "Dot";
    case TokenType::Comma: return "Comma";
    case TokenType::Semicolon: return "Semicolon";
    case TokenType::Colon: return "Colon";
    case TokenType::Question: return "Question";
    case TokenType::Plus: return "Plus";
    case TokenType::Minus: return "Minus";
    case TokenType::Star: return "Star";
    case TokenType::Slash: return "Slash";
    case TokenType::Percent: return "Percent";
    case TokenType::Assign: return "Assign";
    case TokenType::Equal: return "Equal";
    case TokenType::StrictEqual: return "StrictEqual";
    case TokenType::NotEqual: return "NotEqual";
    case TokenType::StrictNotEqual: return "StrictNotEqual";
    case TokenType::Less: return "Less";
    case TokenType::LessEqual: return "LessEqual";
    case TokenType::Greater: return "Greater";
    case TokenType::GreaterEqual: return "GreaterEqual";
    case TokenType::Not: return "Not";
    case TokenType::LogicalAnd: return "LogicalAnd";
    case TokenType::LogicalOr: return "LogicalOr";
    case TokenType::Increment: return "Increment";
    case TokenType::Decrement: return "Decrement";
    case TokenType::PlusAssign: return "PlusAssign";
    case TokenType::MinusAssign: return "MinusAssign";
    case TokenType::StarAssign: return "StarAssign";
    case TokenType::SlashAssign: return "SlashAssign";
    case TokenType::PercentAssign: return "PercentAssign";
    }
    return "UnknownToken";
}

const char* lexerErrorCodeName(LexerErrorCode code)
{
    switch (code) {
    case LexerErrorCode::None: return "None";
    case LexerErrorCode::InvalidSource: return "InvalidSource";
    case LexerErrorCode::SourceTooLarge: return "SourceTooLarge";
    case LexerErrorCode::TooManyTokens: return "TooManyTokens";
    case LexerErrorCode::TokenTooLong: return "TokenTooLong";
    case LexerErrorCode::UnexpectedCharacter: return "UnexpectedCharacter";
    case LexerErrorCode::UnsupportedEscape: return "UnsupportedEscape";
    case LexerErrorCode::UnterminatedString: return "UnterminatedString";
    case LexerErrorCode::UnterminatedBlockComment: return "UnterminatedBlockComment";
    case LexerErrorCode::MalformedNumericLiteral: return "MalformedNumericLiteral";
    }
    return "UnknownLexerError";
}

} // namespace javascript
} // namespace gxos
