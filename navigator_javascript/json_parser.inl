#include "json_parser.h"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace gxos {
namespace javascript {
namespace {

bool jsonIsAsciiDigit(char value)
{
    return value >= '0' && value <= '9';
}

bool jsonIsAsciiHexDigit(char value)
{
    return jsonIsAsciiDigit(value) || (value >= 'a' && value <= 'f') ||
        (value >= 'A' && value <= 'F');
}

unsigned jsonHexValue(char value)
{
    if (jsonIsAsciiDigit(value)) return static_cast<unsigned>(value - '0');
    if (value >= 'a' && value <= 'f')
        return static_cast<unsigned>(value - 'a') + 10u;
    return static_cast<unsigned>(value - 'A') + 10u;
}

bool isJsonWhitespace(char value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

void appendUtf8Scalar(char* output, std::size_t& outputLength,
    std::uint32_t scalar)
{
    if (scalar <= 0x7fu) {
        output[outputLength++] = static_cast<char>(scalar);
    } else if (scalar <= 0x7ffu) {
        output[outputLength++] = static_cast<char>(0xc0u | (scalar >> 6u));
        output[outputLength++] = static_cast<char>(0x80u | (scalar & 0x3fu));
    } else if (scalar <= 0xffffu) {
        output[outputLength++] = static_cast<char>(0xe0u | (scalar >> 12u));
        output[outputLength++] = static_cast<char>(0x80u |
            ((scalar >> 6u) & 0x3fu));
        output[outputLength++] = static_cast<char>(0x80u | (scalar & 0x3fu));
    } else {
        output[outputLength++] = static_cast<char>(0xf0u | (scalar >> 18u));
        output[outputLength++] = static_cast<char>(0x80u |
            ((scalar >> 12u) & 0x3fu));
        output[outputLength++] = static_cast<char>(0x80u |
            ((scalar >> 6u) & 0x3fu));
        output[outputLength++] = static_cast<char>(0x80u | (scalar & 0x3fu));
    }
}

class PortableJsonParserImpl {
public:
    PortableJsonParserImpl(SourceView input, PortableObjectStore& objects,
        RuntimeStringStore& strings, PortableJsonParseLimits limits,
        char* decodedScratch, std::size_t decodedScratchCapacity,
        char* keyScratch, std::size_t keyScratchCapacity,
        PortableJsonStepConsumer stepConsumer, void* stepContext,
        PortableJsonParseError& error, PortableJsonParseStats& stats)
        : input_(input), objects_(objects), strings_(strings), limits_(limits),
          decodedScratch_(decodedScratch),
          decodedScratchCapacity_(decodedScratchCapacity),
          keyScratch_(keyScratch), keyScratchCapacity_(keyScratchCapacity),
          stepConsumer_(stepConsumer), stepContext_(stepContext),
          error_(error), stats_(stats)
    {
        error_ = PortableJsonParseError();
        stats_ = PortableJsonParseStats();
    }

    bool run(Value& result)
    {
        result = Value::undefined();
        if ((input_.data == nullptr && input_.length != 0u) ||
            !objects_.configured()) {
            setError(PortableJsonParseErrorCode::InvalidInput);
            return false;
        }
        if (input_.length > limits_.maxInputBytes) {
            setError(PortableJsonParseErrorCode::InputLimitExceeded);
            return false;
        }
        if (objects_.limits().maxPropertyNameLength != 0u &&
            limits_.maxNestingDepth >
                (std::numeric_limits<std::size_t>::max)() /
                    objects_.limits().maxPropertyNameLength) {
            setError(PortableJsonParseErrorCode::InternalInvariantFailure);
            return false;
        }
        const std::size_t requiredKeyBytes = limits_.maxNestingDepth *
            objects_.limits().maxPropertyNameLength;
        if (decodedScratchCapacity_ < limits_.maxStringLength ||
            (limits_.maxStringLength != 0u && decodedScratch_ == nullptr) ||
            keyScratchCapacity_ < requiredKeyBytes ||
            (requiredKeyBytes != 0u && keyScratch_ == nullptr)) {
            setError(PortableJsonParseErrorCode::InvalidInput);
            return false;
        }

        objectMark_ = objects_.objectCount();
        stringMark_ = strings_.mark();
        if (!parseValue(0u, result) || !skipWhitespace() ||
            cursor_ != input_.length) {
            if (error_.code == PortableJsonParseErrorCode::None)
                setError(PortableJsonParseErrorCode::SyntaxError);
            result = Value::undefined();
            rollback();
            return false;
        }
        return true;
    }

private:
    void setError(PortableJsonParseErrorCode code)
    {
        if (error_.code == PortableJsonParseErrorCode::None)
            error_.code = code;
    }

    bool consumeStep()
    {
        if (stepConsumer_ != nullptr) {
            if (!stepConsumer_(stepContext_)) {
                setError(PortableJsonParseErrorCode::ExecutionBudgetExceeded);
                return false;
            }
        } else {
            if (stats_.stepsConsumed >= limits_.maxSteps) {
                setError(PortableJsonParseErrorCode::ExecutionBudgetExceeded);
                return false;
            }
        }
        ++stats_.stepsConsumed;
        return true;
    }

    bool takeByte(char& result)
    {
        if (cursor_ >= input_.length) return false;
        if (!consumeStep()) return false;
        result = input_.data[cursor_++];
        return true;
    }

    bool skipWhitespace()
    {
        while (cursor_ < input_.length &&
            isJsonWhitespace(input_.data[cursor_])) {
            char ignored = 0;
            if (!takeByte(ignored)) return false;
        }
        return true;
    }

    bool readHexQuad(std::uint16_t& result)
    {
        result = 0u;
        for (std::size_t index = 0u; index < 4u; ++index) {
            char character = 0;
            if (!takeByte(character)) {
                if (error_.code == PortableJsonParseErrorCode::None)
                    setError(PortableJsonParseErrorCode::SyntaxError);
                return false;
            }
            if (!jsonIsAsciiHexDigit(character)) {
                setError(PortableJsonParseErrorCode::SyntaxError);
                return false;
            }
            result = static_cast<std::uint16_t>((result << 4u) |
                static_cast<std::uint16_t>(jsonHexValue(character)));
        }
        return true;
    }

    bool appendScalar(char* output, std::size_t capacity,
        std::size_t& outputLength, std::uint32_t scalar, bool propertyKey)
    {
        const std::size_t byteCount = scalar <= 0x7fu ? 1u
            : scalar <= 0x7ffu ? 2u : scalar <= 0xffffu ? 3u : 4u;
        if (byteCount > capacity || outputLength > capacity - byteCount) {
            if (propertyKey) {
                error_.objectError = PortableObjectErrorCode::NameTooLong;
                setError(PortableJsonParseErrorCode::ObjectStoreFailure);
            } else {
                setError(PortableJsonParseErrorCode::StringLimitExceeded);
            }
            return false;
        }
        appendUtf8Scalar(output, outputLength, scalar);
        return true;
    }

    bool readJsonString(char* output, std::size_t capacity,
        bool propertyKey, SourceView& result)
    {
        result = SourceView();
        std::size_t outputLength = 0u;
        char quote = 0;
        if (!takeByte(quote) || quote != '"') {
            if (error_.code == PortableJsonParseErrorCode::None)
                setError(PortableJsonParseErrorCode::SyntaxError);
            return false;
        }
        while (cursor_ < input_.length) {
            const std::size_t characterStart = cursor_;
            const unsigned char next = static_cast<unsigned char>(
                input_.data[cursor_]);
            if (next >= 0x80u) {
                std::size_t decodedEnd = cursor_;
                std::uint32_t scalar = 0u;
                if (!decodeJsonUtf8Scalar(input_.data, input_.length,
                        decodedEnd, scalar)) {
                    setError(PortableJsonParseErrorCode::InvalidUnicode);
                    return false;
                }
                const std::size_t byteCount = decodedEnd - cursor_;
                for (std::size_t index = 0u; index < byteCount; ++index) {
                    if (!consumeStep()) return false;
                }
                if (byteCount > capacity || outputLength > capacity - byteCount) {
                    if (propertyKey) {
                        error_.objectError = PortableObjectErrorCode::NameTooLong;
                        setError(PortableJsonParseErrorCode::ObjectStoreFailure);
                    } else {
                        setError(PortableJsonParseErrorCode::StringLimitExceeded);
                    }
                    return false;
                }
                for (std::size_t index = 0u; index < byteCount; ++index)
                    output[outputLength++] = input_.data[characterStart + index];
                cursor_ = decodedEnd;
                continue;
            }

            char character = 0;
            if (!takeByte(character)) return false;
            if (character == '"') {
                result = SourceView(output, outputLength);
                return true;
            }
            if (static_cast<unsigned char>(character) < 0x20u) {
                setError(PortableJsonParseErrorCode::SyntaxError);
                return false;
            }
            if (character != '\\') {
                if (outputLength >= capacity) {
                    if (propertyKey) {
                        error_.objectError = PortableObjectErrorCode::NameTooLong;
                        setError(PortableJsonParseErrorCode::ObjectStoreFailure);
                    } else {
                        setError(PortableJsonParseErrorCode::StringLimitExceeded);
                    }
                    return false;
                }
                output[outputLength++] = character;
                continue;
            }

            char escape = 0;
            if (!takeByte(escape)) {
                if (error_.code == PortableJsonParseErrorCode::None)
                    setError(PortableJsonParseErrorCode::SyntaxError);
                return false;
            }
            switch (escape) {
            case '"': character = '"'; break;
            case '\\': character = '\\'; break;
            case '/': character = '/'; break;
            case 'b': character = '\b'; break;
            case 'f': character = '\f'; break;
            case 'n': character = '\n'; break;
            case 'r': character = '\r'; break;
            case 't': character = '\t'; break;
            case 'u': {
                std::uint16_t firstUnit = 0u;
                if (!readHexQuad(firstUnit)) return false;
                std::uint32_t scalar = firstUnit;
                if (firstUnit >= 0xd800u && firstUnit <= 0xdbffu) {
                    char slash = 0;
                    char marker = 0;
                    if (!takeByte(slash) || slash != '\\' ||
                        !takeByte(marker) || marker != 'u') {
                        if (error_.code == PortableJsonParseErrorCode::None)
                            setError(PortableJsonParseErrorCode::InvalidUnicode);
                        return false;
                    }
                    std::uint16_t secondUnit = 0u;
                    if (!readHexQuad(secondUnit)) return false;
                    if (secondUnit < 0xdc00u || secondUnit > 0xdfffu) {
                        setError(PortableJsonParseErrorCode::InvalidUnicode);
                        return false;
                    }
                    scalar = 0x10000u +
                        ((static_cast<std::uint32_t>(firstUnit) - 0xd800u)
                            << 10u) +
                        (static_cast<std::uint32_t>(secondUnit) - 0xdc00u);
                } else if (firstUnit >= 0xdc00u && firstUnit <= 0xdfffu) {
                    setError(PortableJsonParseErrorCode::InvalidUnicode);
                    return false;
                }
                if (!appendScalar(output, capacity, outputLength, scalar,
                        propertyKey)) return false;
                continue;
            }
            default:
                setError(PortableJsonParseErrorCode::SyntaxError);
                return false;
            }
            if (outputLength >= capacity) {
                if (propertyKey) {
                    error_.objectError = PortableObjectErrorCode::NameTooLong;
                    setError(PortableJsonParseErrorCode::ObjectStoreFailure);
                } else {
                    setError(PortableJsonParseErrorCode::StringLimitExceeded);
                }
                return false;
            }
            output[outputLength++] = character;
        }
        setError(PortableJsonParseErrorCode::SyntaxError);
        return false;
    }

    bool parseLiteral(const char* expected, std::size_t length)
    {
        for (std::size_t index = 0u; index < length; ++index) {
            char character = 0;
            if (!takeByte(character) || character != expected[index]) {
                if (error_.code == PortableJsonParseErrorCode::None)
                    setError(PortableJsonParseErrorCode::SyntaxError);
                return false;
            }
        }
        return true;
    }

    bool parseNumber(double& result)
    {
        const std::size_t begin = cursor_;
        if (cursor_ < input_.length && input_.data[cursor_] == '-') {
            char ignored = 0;
            if (!takeByte(ignored)) return false;
        }
        if (cursor_ >= input_.length) {
            setError(PortableJsonParseErrorCode::SyntaxError);
            return false;
        }
        if (input_.data[cursor_] == '0') {
            char ignored = 0;
            if (!takeByte(ignored)) return false;
            if (cursor_ < input_.length && jsonIsAsciiDigit(input_.data[cursor_])) {
                setError(PortableJsonParseErrorCode::SyntaxError);
                return false;
            }
        } else if (input_.data[cursor_] >= '1' &&
            input_.data[cursor_] <= '9') {
            do {
                char ignored = 0;
                if (!takeByte(ignored)) return false;
            } while (cursor_ < input_.length &&
                jsonIsAsciiDigit(input_.data[cursor_]));
        } else {
            setError(PortableJsonParseErrorCode::SyntaxError);
            return false;
        }
        if (cursor_ < input_.length && input_.data[cursor_] == '.') {
            char ignored = 0;
            if (!takeByte(ignored)) return false;
            if (cursor_ >= input_.length || !jsonIsAsciiDigit(input_.data[cursor_])) {
                setError(PortableJsonParseErrorCode::SyntaxError);
                return false;
            }
            do {
                char ignored = 0;
                if (!takeByte(ignored)) return false;
            } while (cursor_ < input_.length &&
                jsonIsAsciiDigit(input_.data[cursor_]));
        }
        if (cursor_ < input_.length && (input_.data[cursor_] == 'e' ||
            input_.data[cursor_] == 'E')) {
            char ignored = 0;
            if (!takeByte(ignored)) return false;
            if (cursor_ < input_.length && (input_.data[cursor_] == '+' ||
                input_.data[cursor_] == '-')) {
                if (!takeByte(ignored)) return false;
            }
            if (cursor_ >= input_.length || !jsonIsAsciiDigit(input_.data[cursor_])) {
                setError(PortableJsonParseErrorCode::SyntaxError);
                return false;
            }
            do {
                char ignored = 0;
                if (!takeByte(ignored)) return false;
            } while (cursor_ < input_.length &&
                jsonIsAsciiDigit(input_.data[cursor_]));
        }
        const SourceView token(input_.data + begin, cursor_ - begin);
        const bool converted = limits_.numberConverter != nullptr
            ? limits_.numberConverter(token, result)
            : parsePortableJsonBinary64(token, result);
        if (!converted) {
            setError(PortableJsonParseErrorCode::InvalidNumber);
            return false;
        }
        return true;
    }

    bool parseValue(std::size_t parentDepth, Value& result)
    {
        if (!skipWhitespace()) return false;
        if (cursor_ >= input_.length) {
            setError(PortableJsonParseErrorCode::SyntaxError);
            return false;
        }
        const char marker = input_.data[cursor_];
        if (marker == '{' || marker == '[') {
            if (parentDepth >= limits_.maxNestingDepth) {
                setError(PortableJsonParseErrorCode::NestingLimitExceeded);
                return false;
            }
            const std::size_t containerDepth = parentDepth + 1u;
            char opening = 0;
            if (!takeByte(opening)) return false;
            RuntimeObjectId object = kInvalidRuntimeObjectId;
            PortableObjectError objectError;
            if (opening == '{') {
                if (!objects_.createObject(limits_.objectPrototype, object,
                        objectError)) {
                    error_.objectError = objectError.code;
                    setError(PortableJsonParseErrorCode::ObjectStoreFailure);
                    return false;
                }
                ++stats_.objectsCreated;
                result = Value::object(object);
                if (!skipWhitespace()) return false;
                if (cursor_ < input_.length && input_.data[cursor_] == '}') {
                    char closing = 0;
                    return takeByte(closing);
                }
                while (true) {
                    const std::size_t keyLimit =
                        objects_.limits().maxPropertyNameLength;
                    const std::size_t keyOffset =
                        (containerDepth - 1u) * keyLimit;
                    SourceView key;
                    char* keyBuffer = keyLimit == 0u ? nullptr
                        : keyScratch_ + keyOffset;
                    if (!readJsonString(keyBuffer, keyLimit, true, key))
                        return false;
                    if (!skipWhitespace()) return false;
                    char colon = 0;
                    if (!takeByte(colon) || colon != ':') {
                        if (error_.code == PortableJsonParseErrorCode::None)
                            setError(PortableJsonParseErrorCode::SyntaxError);
                        return false;
                    }
                    Value member = Value::undefined();
                    if (!parseValue(containerDepth, member)) return false;
                    if (!objects_.setOwnProperty(object, key, member,
                            objectError, limits_.propertyAttributes)) {
                        error_.objectError = objectError.code;
                        setError(PortableJsonParseErrorCode::ObjectStoreFailure);
                        return false;
                    }
                    if (!skipWhitespace()) return false;
                    char delimiter = 0;
                    if (!takeByte(delimiter)) {
                        if (error_.code == PortableJsonParseErrorCode::None)
                            setError(PortableJsonParseErrorCode::SyntaxError);
                        return false;
                    }
                    if (delimiter == '}') return true;
                    if (delimiter != ',') {
                        setError(PortableJsonParseErrorCode::SyntaxError);
                        return false;
                    }
                    if (!skipWhitespace()) return false;
                    if (cursor_ >= input_.length || input_.data[cursor_] != '"') {
                        setError(PortableJsonParseErrorCode::SyntaxError);
                        return false;
                    }
                }
            }

            if (!objects_.createArray(0u, limits_.arrayPrototype, object,
                    objectError)) {
                error_.objectError = objectError.code;
                setError(PortableJsonParseErrorCode::ObjectStoreFailure);
                return false;
            }
            ++stats_.objectsCreated;
            result = Value::object(object);
            if (!skipWhitespace()) return false;
            if (cursor_ < input_.length && input_.data[cursor_] == ']') {
                char closing = 0;
                return takeByte(closing);
            }
            std::size_t elementIndex = 0u;
            while (true) {
                const std::size_t arrayLimit = limits_.maxArrayLength <
                    limits_.maxArrayElements ? limits_.maxArrayLength
                    : limits_.maxArrayElements;
                if (elementIndex >= arrayLimit ||
                    objects_.arrayElementCount() >=
                        limits_.maxTotalArrayElements) {
                    setError(PortableJsonParseErrorCode::ArrayLimitExceeded);
                    return false;
                }
                Value element = Value::undefined();
                if (!parseValue(containerDepth, element)) return false;
                if (!objects_.setArrayElement(object, elementIndex, element,
                        objectError, limits_.propertyAttributes)) {
                    error_.objectError = objectError.code;
                    setError(PortableJsonParseErrorCode::ObjectStoreFailure);
                    return false;
                }
                ++stats_.arrayElementsCreated;
                ++elementIndex;
                if (!skipWhitespace()) return false;
                char delimiter = 0;
                if (!takeByte(delimiter)) {
                    if (error_.code == PortableJsonParseErrorCode::None)
                        setError(PortableJsonParseErrorCode::SyntaxError);
                    return false;
                }
                if (delimiter == ']') return true;
                if (delimiter != ',') {
                    setError(PortableJsonParseErrorCode::SyntaxError);
                    return false;
                }
                if (!skipWhitespace()) return false;
                if (cursor_ >= input_.length || input_.data[cursor_] == ']') {
                    setError(PortableJsonParseErrorCode::SyntaxError);
                    return false;
                }
            }
        }
        if (marker == '"') {
            SourceView decoded;
            if (!readJsonString(decodedScratch_, decodedScratchCapacity_,
                    false, decoded)) return false;
            if (strings_.count() >= limits_.maxStringValues ||
                decoded.length > limits_.maxStringBytes ||
                strings_.bytesUsed() > limits_.maxStringBytes - decoded.length) {
                setError(PortableJsonParseErrorCode::StringLimitExceeded);
                return false;
            }
            RuntimeStringId id = kInvalidRuntimeStringId;
            RuntimeStringError stringError;
            if (!strings_.create(decoded, limits_.maxStringLength, id,
                    stringError)) {
                error_.stringError = stringError.code;
                setError(stringError.code == RuntimeStringErrorCode::CapacityExceeded
                    ? PortableJsonParseErrorCode::StringLimitExceeded
                    : PortableJsonParseErrorCode::StringStoreFailure);
                return false;
            }
            ++stats_.stringsCreated;
            stats_.stringBytesCreated += decoded.length;
            result = Value::string(id);
            return true;
        }
        if (marker == 't') {
            if (!parseLiteral("true", 4u)) return false;
            result = Value::boolean(true);
            return true;
        }
        if (marker == 'f') {
            if (!parseLiteral("false", 5u)) return false;
            result = Value::boolean(false);
            return true;
        }
        if (marker == 'n') {
            if (!parseLiteral("null", 4u)) return false;
            result = Value::nullValue();
            return true;
        }
        if (marker == '-' || jsonIsAsciiDigit(marker)) {
            double number = 0.0;
            if (!parseNumber(number)) return false;
            result = Value::number(number);
            return true;
        }
        setError(PortableJsonParseErrorCode::SyntaxError);
        return false;
    }

    void rollback()
    {
        PortableObjectError objectError;
        RuntimeStringError stringError;
        const bool objectsRestored = objects_.rollbackObjects(objectMark_,
            objectError);
        const bool stringsRestored = strings_.restore(stringMark_, stringError);
        if (!objectsRestored || !stringsRestored) {
            error_.objectError = objectError.code;
            error_.stringError = stringError.code;
            error_.code = PortableJsonParseErrorCode::InternalInvariantFailure;
        }
    }

    SourceView input_;
    PortableObjectStore& objects_;
    RuntimeStringStore& strings_;
    PortableJsonParseLimits limits_;
    char* decodedScratch_;
    std::size_t decodedScratchCapacity_;
    char* keyScratch_;
    std::size_t keyScratchCapacity_;
    PortableJsonStepConsumer stepConsumer_;
    void* stepContext_;
    PortableJsonParseError& error_;
    PortableJsonParseStats& stats_;
    std::size_t cursor_ = 0u;
    std::size_t objectMark_ = 0u;
    RuntimeStringMark stringMark_;
};

} // namespace

inline bool decodeJsonUtf8Scalar(const char* bytes, std::size_t length,
    std::size_t& offset, std::uint32_t& scalar)
{
    if (bytes == nullptr || offset >= length) return false;
    const unsigned char first = static_cast<unsigned char>(bytes[offset]);
    if (first <= 0x7fu) {
        scalar = first;
        ++offset;
        return true;
    }

    std::size_t count = 0u;
    std::uint32_t value = 0u;
    if (first >= 0xc2u && first <= 0xdfu) {
        count = 2u;
        value = first & 0x1fu;
    } else if (first >= 0xe0u && first <= 0xefu) {
        count = 3u;
        value = first & 0x0fu;
    } else if (first >= 0xf0u && first <= 0xf4u) {
        count = 4u;
        value = first & 0x07u;
    } else {
        return false;
    }
    if (count > length - offset) return false;
    for (std::size_t index = 1u; index < count; ++index) {
        const unsigned char continuation =
            static_cast<unsigned char>(bytes[offset + index]);
        if ((continuation & 0xc0u) != 0x80u) return false;
        if (index == 1u &&
            ((first == 0xe0u && continuation < 0xa0u) ||
                (first == 0xedu && continuation >= 0xa0u) ||
                (first == 0xf0u && continuation < 0x90u) ||
                (first == 0xf4u && continuation >= 0x90u))) return false;
        value = (value << 6u) | (continuation & 0x3fu);
    }
    if (value > 0x10ffffu || (value >= 0xd800u && value <= 0xdfffu))
        return false;
    offset += count;
    scalar = value;
    return true;
}

inline bool parsePortableJson(SourceView input, PortableObjectStore& objects,
    RuntimeStringStore& strings, PortableJsonParseLimits limits,
    char* decodedScratch, std::size_t decodedScratchCapacity,
    char* keyScratch, std::size_t keyScratchCapacity,
    PortableJsonStepConsumer stepConsumer, void* stepContext,
    Value& result, PortableJsonParseError& error,
    PortableJsonParseStats* stats)
{
    PortableJsonParseStats localStats;
    PortableJsonParserImpl parser(input, objects, strings, limits, decodedScratch,
        decodedScratchCapacity, keyScratch, keyScratchCapacity,
        stepConsumer, stepContext, error, localStats);
    const bool succeeded = parser.run(result);
    if (stats != nullptr) *stats = localStats;
    return succeeded;
}

inline const char* portableJsonParseErrorCodeName(
    PortableJsonParseErrorCode code)
{
    switch (code) {
    case PortableJsonParseErrorCode::None: return "None";
    case PortableJsonParseErrorCode::InvalidInput: return "InvalidInput";
    case PortableJsonParseErrorCode::InputLimitExceeded:
        return "InputLimitExceeded";
    case PortableJsonParseErrorCode::SyntaxError: return "SyntaxError";
    case PortableJsonParseErrorCode::InvalidUnicode: return "InvalidUnicode";
    case PortableJsonParseErrorCode::InvalidNumber: return "InvalidNumber";
    case PortableJsonParseErrorCode::NumericConversionUnavailable:
        return "NumericConversionUnavailable";
    case PortableJsonParseErrorCode::NestingLimitExceeded:
        return "NestingLimitExceeded";
    case PortableJsonParseErrorCode::StringLimitExceeded:
        return "StringLimitExceeded";
    case PortableJsonParseErrorCode::ObjectStoreFailure:
        return "ObjectStoreFailure";
    case PortableJsonParseErrorCode::StringStoreFailure:
        return "StringStoreFailure";
    case PortableJsonParseErrorCode::ArrayLimitExceeded:
        return "ArrayLimitExceeded";
    case PortableJsonParseErrorCode::ExecutionBudgetExceeded:
        return "ExecutionBudgetExceeded";
    case PortableJsonParseErrorCode::InternalInvariantFailure:
        return "InternalInvariantFailure";
    }
    return "Unknown";
}

} // namespace javascript
} // namespace gxos
