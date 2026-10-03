#include "navigator_script_host.h"

#include "guide_web_html_parser.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cctype>
#include <limits>
#include <utility>
#include <vector>

namespace gxos {
namespace javascript {

namespace {

bool textEquals(SourceView text, const char* expected)
{
    const std::size_t length = std::char_traits<char>::length(expected);
    return text.data != nullptr && text.length == length &&
        std::string(text.data, text.length) == expected;
}

bool validAttributeName(SourceView name)
{
    if (name.data == nullptr || name.length == 0u ||
        name.length > kNavigatorScriptMaxAttributeNameLength) return false;
    const auto isAsciiLetter = [](unsigned char character) {
        return (character >= static_cast<unsigned char>('a') &&
                character <= static_cast<unsigned char>('z')) ||
            (character >= static_cast<unsigned char>('A') &&
                character <= static_cast<unsigned char>('Z'));
    };
    if (!isAsciiLetter(static_cast<unsigned char>(name.data[0]))) return false;
    for (std::size_t index = 1u; index < name.length; ++index) {
        const unsigned char character = static_cast<unsigned char>(name.data[index]);
        if (!isAsciiLetter(character) &&
            !(character >= static_cast<unsigned char>('0') &&
                character <= static_cast<unsigned char>('9')) &&
            character != static_cast<unsigned char>('-') &&
            character != static_cast<unsigned char>('_') &&
            character != static_cast<unsigned char>(':')) return false;
    }
    return true;
}

bool attributeNameEquals(SourceView name, const char* expected)
{
    const std::size_t length = std::char_traits<char>::length(expected);
    if (name.data == nullptr || name.length != length) return false;
    for (std::size_t index = 0u; index < length; ++index) {
        unsigned char character = static_cast<unsigned char>(name.data[index]);
        if (character >= static_cast<unsigned char>('A') &&
            character <= static_cast<unsigned char>('Z')) {
            character = static_cast<unsigned char>(character - 'A' + 'a');
        }
        if (character != static_cast<unsigned char>(expected[index])) return false;
    }
    return true;
}

bool hasAttributePresence(const gxos::web::HtmlElementRef& element,
    std::uint16_t attribute)
{
    return (element.attributePresence & attribute) != 0u;
}

struct RetainedAttributeLocation {
    std::size_t offset = 0u;
    std::size_t valueLength = 0u;
    std::size_t recordSize = 0u;
    std::size_t spanEnd = 0u;
    bool found = false;
};

unsigned char lowerAscii(unsigned char character)
{
    if (character >= static_cast<unsigned char>('A') &&
        character <= static_cast<unsigned char>('Z'))
        return static_cast<unsigned char>(character - 'A' + 'a');
    return character;
}

bool locateRetainedAttribute(const gxos::web::WebDocument& document,
    const gxos::web::HtmlElementRef& element, SourceView name,
    RetainedAttributeLocation& location)
{
    location = {};
    const std::size_t storageSize = document.retainedAttributeStorage.size();
    std::size_t offset = element.retainedAttributeOffset;
    if (element.retainedAttributeCount >
        gxos::web::kHtmlMaxRetainedAttributesPerElement || offset > storageSize)
        return false;
    for (std::size_t index = 0u; index < element.retainedAttributeCount; ++index) {
        if (storageSize - offset < 3u) return false;
        const std::size_t nameLength =
            document.retainedAttributeStorage[offset];
        const std::size_t valueLength =
            static_cast<std::size_t>(
                document.retainedAttributeStorage[offset + 1u]) |
            (static_cast<std::size_t>(
                document.retainedAttributeStorage[offset + 2u]) << 8u);
        if (nameLength == 0u ||
            nameLength > gxos::web::kHtmlMaxRetainedAttributeNameBytes ||
            valueLength > gxos::web::kHtmlMaxRetainedAttributeValueBytes)
            return false;
        const std::size_t recordSize = 3u + nameLength + valueLength;
        if (recordSize > storageSize - offset) return false;
        bool matches = name.data != nullptr && name.length == nameLength;
        for (std::size_t nameIndex = 0u; matches && nameIndex < nameLength;
            ++nameIndex) {
            matches = document.retainedAttributeStorage[
                offset + 3u + nameIndex] == lowerAscii(
                    static_cast<unsigned char>(name.data[nameIndex]));
        }
        if (matches) {
            location.offset = offset;
            location.valueLength = valueLength;
            location.recordSize = recordSize;
            location.found = true;
        }
        offset += recordSize;
    }
    location.spanEnd = offset;
    return true;
}

bool attributeMutationIsDeferred(const gxos::web::HtmlElementRef& element,
    SourceView name)
{
    const std::string& tag = element.tagName;
    if (attributeNameEquals(name, "style")) return true;
    if (attributeNameEquals(name, "name"))
        return tag == "input" || tag == "button" || tag == "textarea" ||
            tag == "select" || tag == "form";
    if (attributeNameEquals(name, "type"))
        return tag == "input" || tag == "button";
    if (attributeNameEquals(name, "value"))
        return tag == "input" || tag == "button" || tag == "option";
    if (attributeNameEquals(name, "checked"))
        return tag == "input" &&
            (element.formControl.inputType == "checkbox" ||
                element.formControl.inputType == "radio");
    if (attributeNameEquals(name, "selected")) return tag == "option";
    if (attributeNameEquals(name, "disabled"))
        return tag == "input" || tag == "button" || tag == "textarea" ||
            tag == "select" || tag == "option" || tag == "fieldset";
    return false;
}

void writeRetainedAttributeRecord(std::vector<std::uint8_t>& storage,
    std::size_t offset, SourceView name, SourceView value)
{
    storage[offset] = static_cast<std::uint8_t>(name.length);
    storage[offset + 1u] = static_cast<std::uint8_t>(value.length & 0xFFu);
    storage[offset + 2u] = static_cast<std::uint8_t>(
        (value.length >> 8u) & 0xFFu);
    for (std::size_t index = 0u; index < name.length; ++index)
        storage[offset + 3u + index] = lowerAscii(
            static_cast<unsigned char>(name.data[index]));
    for (std::size_t index = 0u; index < value.length; ++index)
        storage[offset + 3u + name.length + index] =
            static_cast<std::uint8_t>(value.data[index]);
}

void shiftLaterElementAttributeOffsets(gxos::web::WebDocument& document,
    const gxos::web::HtmlElementRef& changed, std::size_t boundary,
    int delta)
{
    if (delta == 0) return;
    for (gxos::web::HtmlElementRef& candidate : document.structuralElements) {
        if (&candidate == &changed || candidate.retainedAttributeCount == 0u ||
            candidate.retainedAttributeOffset < boundary) continue;
        const int shifted = static_cast<int>(candidate.retainedAttributeOffset) +
            delta;
        if (shifted >= 0 && shifted <= 65535)
            candidate.retainedAttributeOffset =
                static_cast<std::uint16_t>(shifted);
    }
}

void updateElementCopies(gxos::web::WebDocument& document,
    std::uint64_t serial, bool idAttribute, const std::string& value,
    bool present)
{
    const auto update = [&](gxos::web::HtmlElementRef& element) {
        if (element.serial != serial) return;
        std::uint16_t& bits = element.attributePresence;
        const std::uint16_t bit = idAttribute
            ? gxos::web::HtmlAttributeIdPresent
            : gxos::web::HtmlAttributeClassPresent;
        if (present) bits = static_cast<std::uint16_t>(bits | bit);
        else bits = static_cast<std::uint16_t>(bits & ~bit);
        std::string& projection = idAttribute ? element.id : element.className;
        if (present) projection = value;
        else projection.clear();
    };
    for (gxos::web::HtmlElementRef& element : document.structuralElements)
        update(element);
    if (document.hasDocumentElement) update(document.documentElement);
    if (document.hasBodyElement) update(document.bodyElement);
    for (gxos::web::DocBlock& block : document.blocks) {
        if (block.elementMetadata.serial == serial) {
            update(block.elementMetadata);
            if (idAttribute) block.id = present ? value : std::string();
            else block.className = present ? value : std::string();
        }
        for (gxos::web::HtmlElementRef& ancestor : block.ancestors)
            update(ancestor);
    }
}

bool parseCanonicalIndex(SourceView text, std::size_t& index)
{
    index = 0;
    if (text.data == nullptr || text.length == 0) return false;
    if (text.length > 1u && text.data[0] == '0') return false;
    for (std::size_t position = 0; position < text.length; ++position) {
        const unsigned char character = static_cast<unsigned char>(
            text.data[position]);
        if (character < static_cast<unsigned char>('0') ||
            character > static_cast<unsigned char>('9')) return false;
    }
    const auto parsed = std::from_chars(text.data, text.data + text.length,
        index);
    return parsed.ec == std::errc() && parsed.ptr == text.data + text.length;
}

bool booleanFromHostValue(const HostValue& value)
{
    switch (value.type) {
    case HostValueType::Boolean:
        return value.booleanValue;
    case HostValueType::Number:
        return value.numberValue != 0.0 && !std::isnan(value.numberValue);
    case HostValueType::String:
        return value.stringValue.length != 0;
    case HostValueType::Null:
    case HostValueType::Undefined:
        return false;
    default:
        return true;
    }
}

const gxos::web::HtmlElementRef* findElementInDocument(
    const gxos::web::WebDocument& document, HostInstanceId serial,
    std::size_t nodeLimit)
{
    const std::size_t count = std::min(nodeLimit,
        document.structuralElements.size());
    for (std::size_t index = 0; index < count; ++index) {
        const gxos::web::HtmlElementRef& element =
            document.structuralElements[index];
        if (element.serial == serial) return &element;
    }
    return nullptr;
}

bool isDescendantInDocument(const gxos::web::WebDocument& document,
    std::uint64_t serial, std::uint64_t ancestorSerial,
    std::size_t nodeLimit)
{
    if (serial == 0 || ancestorSerial == 0) return false;
    std::uint64_t current = serial;
    for (std::size_t steps = 0; steps < nodeLimit && current != 0; ++steps) {
        if (current == ancestorSerial) return true;
        const gxos::web::HtmlElementRef* element = findElementInDocument(
            document, current, nodeLimit);
        if (element == nullptr) return false;
        current = element->parentSerial;
    }
    return false;
}

bool appendBounded(std::string& target, const std::string& text,
    std::size_t& operations, std::size_t maxOperations, std::size_t maxBytes)
{
    if (operations >= maxOperations) return false;
    ++operations;
    if (text.size() > maxBytes || target.size() > maxBytes - text.size())
        return false;
    target += text;
    return true;
}

char harnessTextInputCharacter(int keyCode, bool shiftPressed)
{
    if (keyCode >= 65 && keyCode <= 90 && !shiftPressed)
        return static_cast<char>(keyCode + 32);
    return static_cast<char>(keyCode);
}

std::string canonicalTagName(const std::string& tagName)
{
    std::string result = tagName;
    for (char& character : result) {
        character = static_cast<char>(std::toupper(
            static_cast<unsigned char>(character)));
    }
    return result;
}

bool isSelectorAsciiWhitespace(char character)
{
    return character == ' ' || character == '\t' || character == '\r' ||
        character == '\n' || character == '\f';
}

bool isSelectorIdentifierCharacter(char character)
{
    const unsigned char value = static_cast<unsigned char>(character);
    return (value >= static_cast<unsigned char>('a') &&
            value <= static_cast<unsigned char>('z')) ||
        (value >= static_cast<unsigned char>('A') &&
            value <= static_cast<unsigned char>('Z')) ||
        (value >= static_cast<unsigned char>('0') &&
            value <= static_cast<unsigned char>('9')) ||
        character == '-' || character == '_';
}

bool isSelectorTagName(SourceView source, std::size_t begin,
    std::size_t end)
{
    if (begin >= end) return false;
    const unsigned char first = static_cast<unsigned char>(source.data[begin]);
    const bool startsWithLetter =
        (first >= static_cast<unsigned char>('a') &&
            first <= static_cast<unsigned char>('z')) ||
        (first >= static_cast<unsigned char>('A') &&
            first <= static_cast<unsigned char>('Z'));
    if (!startsWithLetter) return false;
    for (std::size_t index = begin + 1u; index < end; ++index) {
        if (!isSelectorIdentifierCharacter(source.data[index])) return false;
    }
    return true;
}

bool isSelectorIdentifier(SourceView source, std::size_t begin,
    std::size_t end)
{
    if (begin >= end) return false;
    for (std::size_t index = begin; index < end; ++index) {
        if (!isSelectorIdentifierCharacter(source.data[index])) return false;
    }
    return true;
}

template <typename Length>
bool copySelectorPart(NavigatorScriptSelectorDescriptor& selector,
    SourceView source, std::size_t begin, std::size_t end,
    std::uint8_t& offset, Length& length)
{
    if (begin > end || end - begin >
            kNavigatorScriptMaxSelectorLength - selector.textLength ||
        selector.textLength > std::numeric_limits<std::uint8_t>::max() ||
        end - begin > std::numeric_limits<Length>::max())
        return false;
    offset = static_cast<std::uint8_t>(selector.textLength);
    length = static_cast<Length>(end - begin);
    for (std::size_t index = begin; index < end; ++index)
        selector.text[selector.textLength++] = source.data[index];
    return true;
}

struct SelectorSourceRange {
    std::size_t offset = 0u;
    std::size_t length = 0u;
};

bool storeSelectorClassTokens(SourceView source,
    std::array<SelectorSourceRange,
        kNavigatorScriptMaxClassQueryTokens>& tokens,
    std::size_t tokenCount, NavigatorScriptSelectorDescriptor& storage,
    NavigatorScriptSimpleSelectorDescriptor& selector)
{
    if (tokenCount == 0u ||
        tokenCount > kNavigatorScriptMaxClassQueryTokens) return false;
    const auto tokenLess = [&source](const SelectorSourceRange& left,
        const SelectorSourceRange& right) {
        const SourceView leftText(source.data + left.offset, left.length);
        const SourceView rightText(source.data + right.offset, right.length);
        return std::lexicographical_compare(leftText.data,
            leftText.data + leftText.length, rightText.data,
            rightText.data + rightText.length);
    };
    std::sort(tokens.begin(), tokens.begin() + tokenCount, tokenLess);

    std::size_t uniqueCount = 0u;
    for (std::size_t index = 0u; index < tokenCount; ++index) {
        if (uniqueCount != 0u) {
            const SelectorSourceRange& previous = tokens[uniqueCount - 1u];
            const SelectorSourceRange& current = tokens[index];
            if (previous.length == current.length &&
                std::char_traits<char>::compare(source.data + previous.offset,
                    source.data + current.offset, current.length) == 0)
                continue;
        }
        tokens[uniqueCount++] = tokens[index];
    }
    selector.classTokenCount = static_cast<std::uint8_t>(uniqueCount);
    for (std::size_t index = 0u; index < uniqueCount; ++index) {
        NavigatorScriptSimpleSelectorDescriptor::ClassTokenRange& stored =
            selector.classTokens[index];
        if (!copySelectorPart(storage, source, tokens[index].offset,
                tokens[index].offset + tokens[index].length,
                stored.offset, stored.length)) return false;
    }
    return true;
}

bool parseCompoundClassTokens(SourceView source, std::size_t begin,
    std::size_t end, NavigatorScriptSelectorDescriptor& storage,
    NavigatorScriptSimpleSelectorDescriptor& selector)
{
    std::array<SelectorSourceRange,
        kNavigatorScriptMaxClassQueryTokens> tokens{};
    std::size_t tokenCount = 0u;
    std::size_t position = begin;
    while (position < end) {
        const std::size_t tokenBegin = position;
        while (position < end && source.data[position] != '.') ++position;
        if (tokenCount >= kNavigatorScriptMaxClassQueryTokens ||
            !isSelectorIdentifier(source, tokenBegin, position)) return false;
        tokens[tokenCount++] = {tokenBegin, position - tokenBegin};
        if (position < end) {
            ++position;
            if (position == end) return false;
        }
    }
    return storeSelectorClassTokens(source, tokens, tokenCount, storage,
        selector);
}

bool isSelectorAttributeValueCharacter(char character)
{
    const unsigned char value = static_cast<unsigned char>(character);
    return (value >= static_cast<unsigned char>('a') &&
            value <= static_cast<unsigned char>('z')) ||
        (value >= static_cast<unsigned char>('A') &&
            value <= static_cast<unsigned char>('Z')) ||
        (value >= static_cast<unsigned char>('0') &&
            value <= static_cast<unsigned char>('9')) ||
        character == '-' || character == '_' || character == ':' ||
        character == '.' || character == '/';
}

bool parseAttributePredicate(SourceView source, std::size_t open,
    std::size_t end, NavigatorScriptSelectorDescriptor& storage,
    NavigatorScriptSimpleSelectorDescriptor& selector,
    std::size_t& afterPredicate)
{
    if (open >= end || source.data[open] != '[' ||
        selector.hasAttributePredicate) return false;

    char quote = '\0';
    std::size_t close = end;
    for (std::size_t index = open + 1u; index < end; ++index) {
        const char character = source.data[index];
        if (quote != '\0') {
            if (character == quote) quote = '\0';
            continue;
        }
        if (character == '"' || character == '\'') {
            quote = character;
            continue;
        }
        if (character == '[') return false;
        if (character == ']') {
            close = index;
            break;
        }
    }
    if (quote != '\0' || close == end) return false;

    std::size_t equals = close;
    char valueDelimiter = '\0';
    for (std::size_t index = open + 1u; index < close; ++index) {
        const char character = source.data[index];
        if (valueDelimiter != '\0') {
            if (character == valueDelimiter) valueDelimiter = '\0';
            continue;
        }
        if (character == '"' || character == '\'') {
            valueDelimiter = character;
            continue;
        }
        if (character != '=') continue;
        if (equals != close) return false;
        equals = index;
    }
    const std::size_t nameEnd = equals;
    if (nameEnd - (open + 1u) >
            kNavigatorScriptMaxSelectorAttributeNameLength ||
        !validAttributeName(SourceView(source.data + open + 1u,
            nameEnd - (open + 1u))) ||
        !copySelectorPart(storage, source, open + 1u, nameEnd,
            selector.attributeNameOffset, selector.attributeNameLength))
        return false;

    selector.hasAttributePredicate = true;
    afterPredicate = close + 1u;
    if (equals == close) return true;

    std::size_t valueBegin = equals + 1u;
    std::size_t valueEnd = close;
    if (valueBegin == valueEnd) return false;
    if (source.data[valueBegin] == '"' || source.data[valueBegin] == '\'') {
        const char valueQuote = source.data[valueBegin++];
        if (valueEnd <= valueBegin || source.data[valueEnd - 1u] != valueQuote)
            return false;
        --valueEnd;
        if (valueEnd - valueBegin >
            kNavigatorScriptMaxSelectorAttributeValueLength) return false;
        for (std::size_t index = valueBegin; index < valueEnd; ++index) {
            const unsigned char character =
                static_cast<unsigned char>(source.data[index]);
            if (source.data[index] == valueQuote || character < 0x20u ||
                character == 0x7fu || character == static_cast<unsigned char>('\\'))
                return false;
        }
    } else {
        if (valueEnd - valueBegin >
            kNavigatorScriptMaxSelectorAttributeValueLength) return false;
        for (std::size_t index = valueBegin; index < valueEnd; ++index) {
            if (!isSelectorAttributeValueCharacter(source.data[index]))
                return false;
        }
    }
    if (!copySelectorPart(storage, source, valueBegin, valueEnd,
            selector.attributeValueOffset, selector.attributeValueLength))
        return false;
    selector.attributeValuePresent = true;
    afterPredicate = close + 1u;
    return true;
}

bool parseNthInteger(SourceView source, std::size_t begin, std::size_t end,
    bool allowSign, std::int16_t& value)
{
    if (source.data == nullptr || begin >= end) return false;
    std::size_t position = begin;
    bool negative = false;
    if (source.data[position] == '+' || source.data[position] == '-') {
        if (!allowSign) return false;
        negative = source.data[position] == '-';
        ++position;
    }
    if (position == end) return false;

    std::uint32_t magnitude = 0u;
    constexpr std::uint32_t maximum =
        static_cast<std::uint32_t>(kNavigatorScriptMaxNthMagnitude);
    for (; position < end; ++position) {
        const unsigned char character =
            static_cast<unsigned char>(source.data[position]);
        if (character < static_cast<unsigned char>('0') ||
            character > static_cast<unsigned char>('9')) return false;
        const std::uint32_t digit = static_cast<std::uint32_t>(
            character - static_cast<unsigned char>('0'));
        if (magnitude > (maximum - digit) / 10u) return false;
        magnitude = magnitude * 10u + digit;
    }
    const std::int32_t signedValue = negative
        ? -static_cast<std::int32_t>(magnitude)
        : static_cast<std::int32_t>(magnitude);
    value = static_cast<std::int16_t>(signedValue);
    return true;
}

bool parseNthExpression(SourceView argument, std::int16_t& a,
    std::int16_t& b)
{
    if (argument.data == nullptr || argument.length == 0u ||
        argument.length > kNavigatorScriptMaxNthArgumentLength) return false;

    std::size_t begin = 0u;
    std::size_t end = argument.length;
    while (begin < end && isSelectorAsciiWhitespace(argument.data[begin]))
        ++begin;
    while (end > begin && isSelectorAsciiWhitespace(argument.data[end - 1u]))
        --end;
    if (begin == end) return false;

    const auto equalsKeyword = [&argument, begin, end](const char* keyword) {
        const std::size_t keywordLength =
            std::char_traits<char>::length(keyword);
        if (end - begin != keywordLength) return false;
        for (std::size_t index = 0u; index < keywordLength; ++index) {
            if (lowerAscii(static_cast<unsigned char>(
                    argument.data[begin + index])) !=
                static_cast<unsigned char>(keyword[index])) return false;
        }
        return true;
    };
    if (equalsKeyword("odd")) {
        a = 2;
        b = 1;
        return true;
    }
    if (equalsKeyword("even")) {
        a = 2;
        b = 0;
        return true;
    }

    std::size_t nPosition = end;
    for (std::size_t index = begin; index < end; ++index) {
        const unsigned char character = static_cast<unsigned char>(
            argument.data[index]);
        if (character != static_cast<unsigned char>('n') &&
            character != static_cast<unsigned char>('N')) continue;
        if (nPosition != end) return false;
        nPosition = index;
    }
    if (nPosition == end) {
        a = 0;
        return parseNthInteger(argument, begin, end, true, b);
    }

    if (nPosition == begin) {
        a = 1;
    } else if (nPosition == begin + 1u &&
        (argument.data[begin] == '+' || argument.data[begin] == '-')) {
        a = argument.data[begin] == '-' ? -1 : 1;
    } else if (!parseNthInteger(argument, begin, nPosition, true, a)) {
        return false;
    }

    std::size_t offsetBegin = nPosition + 1u;
    while (offsetBegin < end &&
        isSelectorAsciiWhitespace(argument.data[offsetBegin])) ++offsetBegin;
    if (offsetBegin == end) {
        b = 0;
        return true;
    }
    const char sign = argument.data[offsetBegin];
    if (sign != '+' && sign != '-') return false;
    std::size_t digitsBegin = offsetBegin + 1u;
    while (digitsBegin < end &&
        isSelectorAsciiWhitespace(argument.data[digitsBegin])) ++digitsBegin;
    if (digitsBegin == end) return false;

    std::int16_t magnitude = 0;
    if (!parseNthInteger(argument, digitsBegin, end, false, magnitude))
        return false;
    b = sign == '-'
        ? static_cast<std::int16_t>(-static_cast<std::int32_t>(magnitude))
        : magnitude;
    return true;
}

bool parseStatePseudo(SourceView source, std::size_t colon,
    std::size_t end, NavigatorScriptSimpleSelectorDescriptor& selector)
{
    if (source.data == nullptr || colon >= end || source.data[colon] != ':' ||
        colon + 1u >= end) return false;

    std::size_t open = end;
    for (std::size_t index = colon + 1u; index < end; ++index) {
        if (source.data[index] == '(') {
            open = index;
            break;
        }
    }
    const std::size_t nameEnd = open;
    const std::size_t nameLength = nameEnd - colon - 1u;
    const char* name = source.data + colon + 1u;
    const auto equalsAsciiCaseInsensitive = [name, nameLength](
            const char* expected) {
        const std::size_t expectedLength =
            std::char_traits<char>::length(expected);
        if (nameLength != expectedLength) return false;
        for (std::size_t index = 0u; index < nameLength; ++index) {
            if (lowerAscii(static_cast<unsigned char>(name[index])) !=
                static_cast<unsigned char>(expected[index])) return false;
        }
        return true;
    };

    if (open == end) {
        if (equalsAsciiCaseInsensitive("checked"))
            selector.statePseudo = NavigatorScriptStatePseudo::Checked;
        else if (equalsAsciiCaseInsensitive("disabled"))
            selector.statePseudo = NavigatorScriptStatePseudo::Disabled;
        else if (equalsAsciiCaseInsensitive("focus"))
            selector.statePseudo = NavigatorScriptStatePseudo::Focus;
        else if (equalsAsciiCaseInsensitive("first-child"))
            selector.statePseudo = NavigatorScriptStatePseudo::FirstChild;
        else if (equalsAsciiCaseInsensitive("last-child"))
            selector.statePseudo = NavigatorScriptStatePseudo::LastChild;
        else if (equalsAsciiCaseInsensitive("only-child"))
            selector.statePseudo = NavigatorScriptStatePseudo::OnlyChild;
        else if (equalsAsciiCaseInsensitive("first-of-type"))
            selector.statePseudo = NavigatorScriptStatePseudo::FirstOfType;
        else if (equalsAsciiCaseInsensitive("last-of-type"))
            selector.statePseudo = NavigatorScriptStatePseudo::LastOfType;
        else if (equalsAsciiCaseInsensitive("only-of-type"))
            selector.statePseudo = NavigatorScriptStatePseudo::OnlyOfType;
        else if (equalsAsciiCaseInsensitive("root"))
            selector.statePseudo = NavigatorScriptStatePseudo::Root;
        else
            return false;
        return true;
    }

    if (end <= open + 1u || source.data[end - 1u] != ')') return false;
    for (std::size_t index = open + 1u; index + 1u < end; ++index) {
        if (source.data[index] == '(' || source.data[index] == ')')
            return false;
    }

    if (equalsAsciiCaseInsensitive("nth-child"))
        selector.statePseudo = NavigatorScriptStatePseudo::NthChild;
    else if (equalsAsciiCaseInsensitive("nth-last-child"))
        selector.statePseudo = NavigatorScriptStatePseudo::NthLastChild;
    else if (equalsAsciiCaseInsensitive("nth-of-type"))
        selector.statePseudo = NavigatorScriptStatePseudo::NthOfType;
    else if (equalsAsciiCaseInsensitive("nth-last-of-type"))
        selector.statePseudo = NavigatorScriptStatePseudo::NthLastOfType;
    else
        return false;

    return parseNthExpression(SourceView(source.data + open + 1u,
        end - open - 2u), selector.nthA, selector.nthB);
}

bool parseSimpleSelector(SourceView source, std::size_t begin,
    std::size_t end, NavigatorScriptSelectorDescriptor& storage,
    NavigatorScriptSimpleSelectorDescriptor& selector)
{
    if (begin == end) return false;

    std::size_t position = begin;
    const char first = source.data[begin];
    if (first == '*') {
        selector.universal = true;
        ++position;
        // Universal selectors remain standalone except for one trailing
        // state pseudo. Universal-plus-attribute was unsupported in JS50.
        if (position < end && source.data[position] != ':') return false;
    } else if (first != '#' && first != '.' && first != '[' && first != ':') {
        while (position < end && source.data[position] != '#' &&
            source.data[position] != '.' && source.data[position] != '[' &&
            source.data[position] != ':')
            ++position;
        if (!isSelectorTagName(source, begin, position) ||
            !copySelectorPart(storage, source, begin, position,
                selector.tagOffset, selector.tagLength)) return false;
    }

    if (position < end && source.data[position] == '#') {
        const std::size_t idBegin = position + 1u;
        std::size_t idEnd = idBegin;
        while (idEnd < end && source.data[idEnd] != '#' &&
            source.data[idEnd] != '.' && source.data[idEnd] != '[' &&
            source.data[idEnd] != ':') ++idEnd;
        if (!isSelectorIdentifier(source, idBegin, idEnd) ||
            !copySelectorPart(storage, source, idBegin, idEnd,
                selector.idOffset, selector.idLength)) return false;
        position = idEnd;
    }

    if (position < end && source.data[position] == '.') {
        std::size_t classEnd = position + 1u;
        while (classEnd < end && source.data[classEnd] != '[' &&
            source.data[classEnd] != ':') ++classEnd;
        if (!parseCompoundClassTokens(source, position + 1u, classEnd, storage,
                selector)) return false;
        position = classEnd;
    }

    if (position < end && source.data[position] == '[') {
        std::size_t afterPredicate = position;
        if (!parseAttributePredicate(source, position, end, storage, selector,
                afterPredicate))
            return false;
        position = afterPredicate;
    }

    if (position < end && source.data[position] == ':') {
        if (!parseStatePseudo(source, position, end, selector))
            return false;
        position = end;
    }

    if (position != end || (selector.tagLength == 0u &&
            selector.idLength == 0u && selector.classTokenCount == 0u &&
            !selector.hasAttributePredicate && !selector.universal &&
            selector.statePseudo == NavigatorScriptStatePseudo::None))
        return false;
    selector.valid = true;
    return true;
}

bool parseBoundedSelectorMember(SourceView source,
    NavigatorScriptSelectorDescriptor& storage,
    NavigatorScriptSelectorMemberDescriptor& selector)
{
    if (source.data == nullptr || source.length == 0u ||
        source.length > kNavigatorScriptMaxSelectorLength) return false;

    std::size_t begin = 0u;
    std::size_t end = source.length;
    while (begin < end && isSelectorAsciiWhitespace(source.data[begin]))
        ++begin;
    while (end > begin && isSelectorAsciiWhitespace(source.data[end - 1u]))
        --end;
    if (begin == end) return false;

    std::size_t relationPosition = end;
    std::size_t whitespacePosition = end;
    char relationCharacter = '\0';
    std::size_t relationCount = 0u;
    bool insideAttribute = false;
    char quote = '\0';
    std::size_t functionalDepth = 0u;
    for (std::size_t index = begin; index < end; ++index) {
        const char character = source.data[index];
        if (insideAttribute) {
            if (quote != '\0') {
                if (character == quote) quote = '\0';
            } else if (character == '"' || character == '\'') {
                quote = character;
            } else if (character == ']') {
                insideAttribute = false;
            }
            continue;
        }
        if (character == '(') {
            ++functionalDepth;
            continue;
        }
        if (character == ')' && functionalDepth != 0u) {
            --functionalDepth;
            continue;
        }
        if (functionalDepth != 0u) continue;
        if (character == '[') {
            insideAttribute = true;
            continue;
        }
        if (character == '>' || character == '+' || character == '~') {
            relationPosition = index;
            relationCharacter = character;
            ++relationCount;
        } else if (whitespacePosition == end &&
            isSelectorAsciiWhitespace(character)) {
            whitespacePosition = index;
        }
    }

    if (relationCount > 1u) return false;
    if (relationCount == 1u) {
        selector.relation = relationCharacter == '>'
            ? NavigatorScriptSelectorRelation::Child
            : relationCharacter == '+'
                ? NavigatorScriptSelectorRelation::AdjacentSibling
                : NavigatorScriptSelectorRelation::GeneralSibling;
        std::size_t leftEnd = relationPosition;
        while (leftEnd > begin &&
            isSelectorAsciiWhitespace(source.data[leftEnd - 1u])) --leftEnd;
        std::size_t rightBegin = relationPosition + 1u;
        while (rightBegin < end &&
            isSelectorAsciiWhitespace(source.data[rightBegin])) ++rightBegin;
        return parseSimpleSelector(source, begin, leftEnd, storage,
                selector.leftSimple) &&
            parseSimpleSelector(source, rightBegin, end, storage,
                selector.rightSimple);
    }

    if (whitespacePosition == end) {
        return parseSimpleSelector(source, begin, end, storage,
            selector.rightSimple);
    }

    selector.relation = NavigatorScriptSelectorRelation::Descendant;
    std::size_t rightBegin = whitespacePosition;
    while (rightBegin < end &&
        isSelectorAsciiWhitespace(source.data[rightBegin])) ++rightBegin;
    return parseSimpleSelector(source, begin, whitespacePosition, storage,
            selector.leftSimple) &&
        parseSimpleSelector(source, rightBegin, end, storage,
            selector.rightSimple);
}

bool parseBoundedSelector(SourceView source,
    NavigatorScriptSelectorDescriptor& selector)
{
    selector = NavigatorScriptSelectorDescriptor();
    if (source.data == nullptr || source.length == 0u ||
        source.length > kNavigatorScriptMaxSelectorLength) return false;

    std::size_t memberBegin = 0u;
    bool insideAttribute = false;
    char quote = '\0';
    std::size_t functionalDepth = 0u;
    for (std::size_t index = 0u; index < source.length; ++index) {
        const char character = source.data[index];
        if (insideAttribute) {
            if (quote != '\0') {
                if (character == quote) quote = '\0';
            } else if (character == '"' || character == '\'') {
                quote = character;
            } else if (character == ']') {
                insideAttribute = false;
            }
            continue;
        }
        if (character == '(') {
            ++functionalDepth;
            continue;
        }
        if (character == ')' && functionalDepth != 0u) {
            --functionalDepth;
            continue;
        }
        if (functionalDepth != 0u) continue;
        if (character == '[') {
            insideAttribute = true;
            continue;
        }
        if (character != ',') continue;
        if (selector.memberCount >=
                kNavigatorScriptMaxSelectorListMembers ||
            !parseBoundedSelectorMember(
                SourceView(source.data + memberBegin, index - memberBegin),
                selector, selector.members[selector.memberCount]))
            return false;
        ++selector.memberCount;
        memberBegin = index + 1u;
    }
    if (selector.memberCount >= kNavigatorScriptMaxSelectorListMembers ||
        !parseBoundedSelectorMember(
            SourceView(source.data + memberBegin, source.length - memberBegin),
            selector, selector.members[selector.memberCount]))
        return false;
    ++selector.memberCount;
    return true;
}

bool makeRetrievalSelector(SourceView argument, bool classSelector,
    NavigatorScriptSelectorDescriptor& selector)
{
    selector = NavigatorScriptSelectorDescriptor();
    if (argument.data == nullptr || argument.length == 0u ||
        argument.length > kNavigatorScriptMaxSelectorLength) return false;
    NavigatorScriptSelectorMemberDescriptor& member = selector.members[0];

    if (!classSelector) {
        if (argument.length == 1u && argument.data[0] == '*') {
            member.rightSimple.valid = true;
            member.rightSimple.universal = true;
            selector.memberCount = 1u;
            return true;
        }
        if (!isSelectorTagName(argument, 0u, argument.length)) return false;
        if (!copySelectorPart(selector, argument, 0u, argument.length,
            member.rightSimple.tagOffset,
            member.rightSimple.tagLength)) return false;
        member.rightSimple.valid = true;
        selector.memberCount = 1u;
        return true;
    }

    // Class retrieval accepts a bounded whitespace-separated token query.
    // Each token retains the existing simple-selector identifier bound, and
    // duplicate tokens are removed before the canonical descriptor is stored.
    if (argument.length > kNavigatorScriptMaxSelectorLength) return false;
    std::array<SelectorSourceRange,
        kNavigatorScriptMaxClassQueryTokens> tokens{};
    std::size_t tokenCount = 0u;
    std::size_t parsedTokenCount = 0u;
    std::size_t position = 0u;
    while (position < argument.length) {
        while (position < argument.length &&
            isSelectorAsciiWhitespace(argument.data[position])) ++position;
        if (position == argument.length) break;
        const std::size_t tokenBegin = position;
        while (position < argument.length &&
            !isSelectorAsciiWhitespace(argument.data[position])) ++position;
        const std::size_t tokenLength = position - tokenBegin;
        if (tokenLength + 1u > kNavigatorScriptMaxSelectorLength ||
            !isSelectorIdentifier(argument, tokenBegin, position) ||
            parsedTokenCount >= kNavigatorScriptMaxClassQueryTokens)
            return false;
        ++parsedTokenCount;

        tokens[tokenCount++] = {tokenBegin, tokenLength};
    }
    if (tokenCount == 0u) return false;
    if (!storeSelectorClassTokens(argument, tokens, tokenCount, selector,
            member.rightSimple)) return false;
    member.rightSimple.valid = true;
    selector.memberCount = 1u;
    return true;
}

SourceView selectorPart(const NavigatorScriptSelectorDescriptor& storage,
    std::uint16_t offset, std::uint16_t length)
{
    return SourceView(storage.text.data() + offset, length);
}

bool selectorTextEquals(SourceView left, SourceView right)
{
    return left.length == right.length &&
        (left.length == 0u || std::char_traits<char>::compare(left.data,
            right.data, left.length) == 0);
}

bool selectorTagEquals(const std::string& tagName, SourceView selector)
{
    if (tagName.size() != selector.length) return false;
    for (std::size_t index = 0; index < tagName.size(); ++index) {
        const unsigned char left = static_cast<unsigned char>(tagName[index]);
        const unsigned char right = static_cast<unsigned char>(selector.data[index]);
        if (std::toupper(left) != std::toupper(right)) return false;
    }
    return true;
}

bool classTokenMatches(const std::string& className, SourceView selector)
{
    std::size_t position = 0;
    while (position < className.size()) {
        while (position < className.size() &&
            isSelectorAsciiWhitespace(className[position])) ++position;
        const std::size_t tokenBegin = position;
        while (position < className.size() &&
            !isSelectorAsciiWhitespace(className[position])) ++position;
        if (position > tokenBegin && selectorTextEquals(
                SourceView(className.data() + tokenBegin,
                    position - tokenBegin), selector)) return true;
    }
    return false;
}

bool classTokenSetMatches(const std::string& className,
    const NavigatorScriptSimpleSelectorDescriptor& selector,
    const NavigatorScriptSelectorDescriptor& storage)
{
    if (selector.classTokenCount == 0u ||
        selector.classTokenCount > kNavigatorScriptMaxClassQueryTokens)
        return false;
    for (std::size_t index = 0; index < selector.classTokenCount; ++index) {
        const NavigatorScriptSimpleSelectorDescriptor::ClassTokenRange& token =
            selector.classTokens[index];
        if (!classTokenMatches(className,
                selectorPart(storage, token.offset, token.length))) return false;
    }
    return true;
}

} // namespace

NavigatorScriptHostAdapter::NavigatorScriptHostAdapter(
    HostGenerationId generation, NavigatorScriptHostLimits limits)
    : generation_(generation), limits_(limits)
{
}

NavigatorScriptHostAdapter::NavigatorScriptHostAdapter(
    gxos::web::WebDocument& document, HostGenerationId generation,
    NavigatorScriptHostLimits limits)
    : document_(&document), generation_(generation), limits_(limits)
{
}

void NavigatorScriptHostAdapter::attachDocument(
    gxos::web::WebDocument& document, HostGenerationId generation)
{
    document_ = &document;
    generation_ = generation;
    clearClickHandlers();
    selectorCollections_ = {};
    returnBuffer_.clear();
}

void NavigatorScriptHostAdapter::detachDocument()
{
    document_ = nullptr;
    clearClickHandlers();
    selectorCollections_ = {};
    returnBuffer_.clear();
}

void NavigatorScriptHostAdapter::setGeneration(HostGenerationId generation)
{
    if (generation_ != generation) {
        clearClickHandlers();
        selectorCollections_ = {};
    }
    generation_ = generation;
}

void NavigatorScriptHostAdapter::setFocusRequestCallback(
    FocusRequestCallback callback, void* context)
{
    focusRequestCallback_ = callback;
    focusRequestContext_ = context;
}

void NavigatorScriptHostAdapter::setDispatchCompleteCallback(
    DispatchCompleteCallback callback, void* context)
{
    dispatchCompleteCallback_ = callback;
    dispatchCompleteContext_ = context;
}

void NavigatorScriptHostAdapter::setActivationDefaultActionCallback(
    ActivationDefaultActionCallback callback, void* context)
{
    activationDefaultActionCallback_ = callback;
    activationDefaultActionContext_ = context;
}

bool NavigatorScriptHostAdapter::allowsReentrantCall(
    std::uint32_t methodId) const
{
    return methodId == kNavigatorFocusMethod ||
        methodId == kNavigatorBlurMethod ||
        methodId == kNavigatorClickMethod ||
        methodId == kNavigatorResetMethod ||
        methodId == kNavigatorHasFocusMethod ||
        methodId == kNavigatorQuerySelectorMethod ||
        methodId == kNavigatorQuerySelectorAllMethod ||
        methodId == kNavigatorMatchesMethod ||
        methodId == kNavigatorClosestMethod ||
        methodId == kNavigatorContainsMethod ||
        methodId == kNavigatorGetElementsByTagNameMethod ||
        methodId == kNavigatorGetElementsByClassNameMethod ||
        methodId == kNavigatorGetAttributeMethod ||
        methodId == kNavigatorHasAttributeMethod ||
        methodId == kNavigatorSetAttributeMethod ||
        methodId == kNavigatorRemoveAttributeMethod;
}

bool NavigatorScriptHostAdapter::allowsStaleHostProperty(
    const HostObjectReference& object, SourceView property) const
{
    return object.kind == kNavigatorElementHostKind &&
        (textEquals(property, "matches") ||
            textEquals(property, "closest") ||
            textEquals(property, "contains") ||
            textEquals(property, "getElementsByTagName") ||
            textEquals(property, "getElementsByClassName") ||
            textEquals(property, "getAttribute") ||
            textEquals(property, "hasAttribute") ||
            textEquals(property, "setAttribute") ||
            textEquals(property, "removeAttribute") ||
            textEquals(property, "parentElement") ||
            textEquals(property, "children") ||
            textEquals(property, "childElementCount") ||
            textEquals(property, "firstElementChild") ||
            textEquals(property, "lastElementChild") ||
            textEquals(property, "nextElementSibling") ||
            textEquals(property, "previousElementSibling"));
}

bool NavigatorScriptHostAdapter::allowsStaleHostMethod(
    std::uint32_t methodId) const
{
    return methodId == kNavigatorMatchesMethod ||
        methodId == kNavigatorClosestMethod ||
        methodId == kNavigatorContainsMethod ||
        methodId == kNavigatorGetElementsByTagNameMethod ||
        methodId == kNavigatorGetElementsByClassNameMethod ||
        methodId == kNavigatorGetAttributeMethod ||
        methodId == kNavigatorHasAttributeMethod ||
        methodId == kNavigatorSetAttributeMethod ||
        methodId == kNavigatorRemoveAttributeMethod;
}

bool NavigatorScriptHostAdapter::allowsStaleHostMethodArgument(
    std::uint32_t methodId) const
{
    return methodId == kNavigatorContainsMethod ||
        methodId == kNavigatorGetElementsByTagNameMethod ||
        methodId == kNavigatorGetElementsByClassNameMethod ||
        methodId == kNavigatorGetAttributeMethod ||
        methodId == kNavigatorHasAttributeMethod ||
        methodId == kNavigatorSetAttributeMethod ||
        methodId == kNavigatorRemoveAttributeMethod;
}

std::size_t NavigatorScriptHostAdapter::callbackLimit() const
{
    return std::min(limits_.maxClickHandlers, clickHandlers_.size());
}

std::size_t NavigatorScriptHostAdapter::listenerLimit() const
{
    return std::min(limits_.maxClickListeners, clickListeners_.size());
}

NavigatorScriptHostAdapter::ClickHandlerRecord*
NavigatorScriptHostAdapter::clickHandlerFor(HostInstanceId serial)
{
    for (std::size_t index = 0; index < clickOnclickRecordCount_; ++index) {
        if (clickHandlers_[index].serial == serial) return &clickHandlers_[index];
    }
    return nullptr;
}

const NavigatorScriptHostAdapter::ClickHandlerRecord*
NavigatorScriptHostAdapter::clickHandlerFor(HostInstanceId serial) const
{
    for (std::size_t index = 0; index < clickOnclickRecordCount_; ++index) {
        if (clickHandlers_[index].serial == serial) return &clickHandlers_[index];
    }
    return nullptr;
}

void NavigatorScriptHostAdapter::clearClickHandlers()
{
    for (ClickHandlerRecord& record : clickHandlers_) record = ClickHandlerRecord();
    for (ClickListenerRecord& record : clickListeners_)
        record = ClickListenerRecord();
    clickHandlerCount_ = 0;
    clickOnclickRecordCount_ = 0;
    clickListenerCount_ = 0;
    nextListenerRegistrationSequence_ = 1u;
    clickDispatchActive_ = false;
}

void NavigatorScriptHostAdapter::removeEmptyClickHandler(HostInstanceId serial)
{
    for (std::size_t index = 0; index < clickOnclickRecordCount_; ++index) {
        ClickHandlerRecord& record = clickHandlers_[index];
        if (record.serial != serial ||
            record.onclickFunction != kInvalidRuntimeFunctionId) continue;
        for (std::size_t move = index + 1; move < clickOnclickRecordCount_; ++move)
            clickHandlers_[move - 1] = clickHandlers_[move];
        clickHandlers_[clickOnclickRecordCount_ - 1] = ClickHandlerRecord();
        --clickOnclickRecordCount_;
        return;
    }
}

NavigatorScriptHostAdapter::ClickListenerRecord*
NavigatorScriptHostAdapter::clickListenerFor(HostObjectKind ownerKind,
    HostInstanceId serial, NavigatorScriptEventType eventType,
    RuntimeFunctionId function, bool capture)
{
    for (ClickListenerRecord& record : clickListeners_) {
        if (record.ownerKind == ownerKind && record.serial == serial &&
            record.eventType == eventType &&
            record.listenerFunction == function &&
            ((record.flags & kNavigatorClickListenerCaptureFlag) != 0u) ==
                capture)
            return &record;
    }
    return nullptr;
}

const NavigatorScriptHostAdapter::ClickListenerRecord*
NavigatorScriptHostAdapter::clickListenerFor(HostObjectKind ownerKind,
    HostInstanceId serial, NavigatorScriptEventType eventType,
    RuntimeFunctionId function, bool capture) const
{
    for (const ClickListenerRecord& record : clickListeners_) {
        if (record.ownerKind == ownerKind && record.serial == serial &&
            record.eventType == eventType &&
            record.listenerFunction == function &&
            ((record.flags & kNavigatorClickListenerCaptureFlag) != 0u) ==
                capture)
            return &record;
    }
    return nullptr;
}

const NavigatorScriptHostAdapter::ClickListenerRecord*
NavigatorScriptHostAdapter::clickListenerForSequence(
    HostObjectKind ownerKind, HostInstanceId serial,
    std::uint64_t registrationSequence) const
{
    for (const ClickListenerRecord& record : clickListeners_) {
        if (record.ownerKind == ownerKind && record.serial == serial &&
            record.registrationSequence == registrationSequence)
            return &record;
    }
    return nullptr;
}

NavigatorScriptHostAdapter::ClickListenerRecord*
NavigatorScriptHostAdapter::clickListenerForSequence(
    HostObjectKind ownerKind, HostInstanceId serial,
    std::uint64_t registrationSequence)
{
    for (ClickListenerRecord& record : clickListeners_) {
        if (record.ownerKind == ownerKind && record.serial == serial &&
            record.registrationSequence == registrationSequence)
            return &record;
    }
    return nullptr;
}

bool NavigatorScriptHostAdapter::hasClickListener(
    HostObjectKind ownerKind, HostInstanceId serial) const
{
    for (const ClickListenerRecord& record : clickListeners_) {
        if (record.ownerKind == ownerKind && record.serial == serial &&
            record.listenerFunction != kInvalidRuntimeFunctionId)
            return true;
    }
    return false;
}

bool NavigatorScriptHostAdapter::hasAnyEventHandler(
    HostObjectKind ownerKind, HostInstanceId serial) const
{
    const ClickHandlerRecord* onclick = ownerKind == kNavigatorElementHostKind
        ? clickHandlerFor(serial) : nullptr;
    return (onclick != nullptr &&
        onclick->onclickFunction != kInvalidRuntimeFunctionId) ||
        hasClickListener(ownerKind, serial);
}

bool NavigatorScriptHostAdapter::hasClickHandler(HostInstanceId serial) const
{
    return hasAnyEventHandler(kNavigatorElementHostKind, serial);
}

void NavigatorScriptHostAdapter::removeClickListener(
    ClickListenerRecord& record)
{
    const HostInstanceId serial = record.serial;
    const HostObjectKind ownerKind = record.ownerKind;
    record = ClickListenerRecord();
    if (clickListenerCount_ > 0) --clickListenerCount_;
    if (!hasAnyEventHandler(ownerKind, serial) && clickHandlerCount_ > 0)
        --clickHandlerCount_;
}

void NavigatorScriptHostAdapter::resequenceListeners()
{
    std::array<std::size_t, kNavigatorScriptMaxClickHandlers> slots{};
    std::size_t count = 0;
    for (std::size_t index = 0; index < clickListeners_.size(); ++index) {
        if (clickListeners_[index].serial == 0 ||
            clickListeners_[index].listenerFunction == kInvalidRuntimeFunctionId)
            continue;
        slots[count++] = index;
    }

    // Insertion sort is intentional: the table is fixed at 64 entries and
    // this keeps the resequencing path allocation-free and deterministic.
    for (std::size_t index = 1; index < count; ++index) {
        const std::size_t slot = slots[index];
        std::size_t position = index;
        while (position > 0 &&
            clickListeners_[slots[position - 1]].registrationSequence >
                clickListeners_[slot].registrationSequence) {
            slots[position] = slots[position - 1];
            --position;
        }
        slots[position] = slot;
    }
    for (std::size_t index = 0; index < count; ++index)
        clickListeners_[slots[index]].registrationSequence =
            static_cast<std::uint64_t>(index + 1u);
    nextListenerRegistrationSequence_ =
        static_cast<std::uint64_t>(count + 1u);
}

bool NavigatorScriptHostAdapter::allocateListenerSequence(
    std::uint64_t& sequence)
{
    // Sequence zero is reserved for an unused record. A 64-bit sequence is
    // practically non-wrapping, but the boundary remains deterministic: if
    // it is reached outside dispatch, active records are compacted in logical
    // order; during dispatch, refusing the new registration protects any
    // already-captured snapshot from resequencing.
    if (nextListenerRegistrationSequence_ == 0u ||
        nextListenerRegistrationSequence_ ==
            std::numeric_limits<std::uint64_t>::max()) {
        if (clickDispatchActive_) return false;
        resequenceListeners();
    }
    sequence = nextListenerRegistrationSequence_++;
    return sequence != 0u;
}

bool NavigatorScriptHostAdapter::collectListenerSnapshot(
    HostObjectKind ownerKind, HostInstanceId serial,
    NavigatorScriptEventType eventType,
    std::array<ClickListenerSnapshotEntry,
        kNavigatorScriptMaxClickHandlers>& snapshot, std::size_t& count,
    bool capture) const
{
    count = 0;
    for (const ClickListenerRecord& record : clickListeners_) {
        if (record.ownerKind != ownerKind || record.serial != serial ||
            record.eventType != eventType ||
            record.listenerFunction == kInvalidRuntimeFunctionId ||
            (((record.flags & kNavigatorClickListenerCaptureFlag) != 0u) !=
                capture)) continue;
        if (count >= snapshot.size()) return false;
        ClickListenerSnapshotEntry entry{
            record.registrationSequence, record.listenerFunction};
        std::size_t position = count++;
        while (position > 0 &&
            snapshot[position - 1].registrationSequence >
                entry.registrationSequence) {
            snapshot[position] = snapshot[position - 1];
            --position;
        }
        snapshot[position] = entry;
    }
    return true;
}

bool NavigatorScriptHostAdapter::dispatchClick(RuntimeContext& runtime,
    HostInstanceId serial, RuntimeErrorCode& error, bool* defaultPrevented)
{
    const HostObjectReference target{
        serial, generation_, kNavigatorElementHostKind};
    return dispatchEvent(runtime, SourceView("click", 5u),
        NavigatorScriptEventType::Click, target, 0u, SourceView(), SourceView(),
        true, error, defaultPrevented);
}

bool NavigatorScriptHostAdapter::requestElementActivation(
    RuntimeContext& runtime, HostInstanceId serial,
    NavigatorScriptActivationProvenance provenance, RuntimeErrorCode& error,
    bool* defaultPrevented)
{
    error = RuntimeErrorCode::None;
    if (defaultPrevented != nullptr) *defaultPrevented = false;
    if (document_ == nullptr || serial == 0 || !isKnownElementSerial(serial)) {
        error = RuntimeErrorCode::StaleHostObject;
        return false;
    }
    if (activationDepth_ >= kNavigatorScriptMaxActivationDepth) {
        error = RuntimeErrorCode::HostReentryUnsupported;
        return false;
    }

    const HostGenerationId activationGeneration = generation_;
    ++activationDepth_;
    bool clickDefaultPrevented = false;
    const bool dispatched = dispatchClick(runtime, serial, error,
        &clickDefaultPrevented);
    if (defaultPrevented != nullptr) *defaultPrevented = clickDefaultPrevented;
    // A listener may perform a nested activation that navigates away.  The
    // outer physical/programmatic trigger has then been consumed by that
    // navigation; do not report the old target becoming stale as a second
    // activation error to its caller.
    if (!dispatched && generation_ != activationGeneration) {
        error = RuntimeErrorCode::None;
        --activationDepth_;
        return true;
    }
    if (dispatched && !clickDefaultPrevented &&
        generation_ == activationGeneration && document_ != nullptr &&
        isKnownElementSerial(serial) && activationDefaultActionCallback_ != nullptr) {
        if (!activationDefaultActionCallback_(activationDefaultActionContext_,
                serial, provenance)) {
            // A valid ordinary element may simply have no default action. The
            // callback returns false only for a stale or otherwise invalid
            // target, which should fail closed without dereferencing it.
            if (generation_ == activationGeneration &&
                isKnownElementSerial(serial)) {
                error = RuntimeErrorCode::None;
            } else {
                error = RuntimeErrorCode::StaleHostObject;
            }
        }
    }
    --activationDepth_;
    return dispatched && error == RuntimeErrorCode::None;
}

bool NavigatorScriptHostAdapter::dispatchKeyboardEvent(
    RuntimeContext& runtime, HostInstanceId targetSerial, int keyCode,
    bool down, bool shiftPressed, RuntimeErrorCode& error,
    bool* defaultPrevented)
{
    std::string key;
    std::string code;
    if (!eventNameForKey(keyCode, shiftPressed, key, code)) {
        error = RuntimeErrorCode::InvalidHostReturn;
        if (defaultPrevented != nullptr) *defaultPrevented = false;
        return false;
    }
    const HostObjectReference target{
        targetSerial == 0 ? kNavigatorDocumentHostInstance : targetSerial,
        generation_, targetSerial == 0
            ? kNavigatorDocumentHostKind : kNavigatorElementHostKind};
    const char* typeText = down ? "keydown" : "keyup";
    return dispatchEvent(runtime, SourceView(typeText, down ? 7u : 5u),
        down ? NavigatorScriptEventType::Keydown :
            NavigatorScriptEventType::Keyup, target,
        0u, SourceView(key.data(), key.size()),
        SourceView(code.data(), code.size()), false, error, defaultPrevented);
}

bool NavigatorScriptHostAdapter::dispatchFocusEvent(
    RuntimeContext& runtime, HostInstanceId targetSerial, bool gained,
    bool bubblingVariant, HostInstanceId relatedTargetSerial,
    RuntimeErrorCode& error, bool* defaultPrevented)
{
    if (targetSerial == 0) {
        error = RuntimeErrorCode::StaleHostObject;
        if (defaultPrevented != nullptr) *defaultPrevented = false;
        return false;
    }
    const char* typeText = nullptr;
    std::size_t typeLength = 0;
    NavigatorScriptEventType eventType = NavigatorScriptEventType::Focus;
    if (gained) {
        if (bubblingVariant) {
            typeText = "focusin";
            typeLength = 7u;
            eventType = NavigatorScriptEventType::Focusin;
        } else {
            typeText = "focus";
            typeLength = 5u;
            eventType = NavigatorScriptEventType::Focus;
        }
    } else if (bubblingVariant) {
        typeText = "focusout";
        typeLength = 8u;
        eventType = NavigatorScriptEventType::Focusout;
    } else {
        typeText = "blur";
        typeLength = 4u;
        eventType = NavigatorScriptEventType::Blur;
    }
    const HostObjectReference target{
        targetSerial, generation_, kNavigatorElementHostKind};
    return dispatchEvent(runtime, SourceView(typeText, typeLength), eventType,
        target, relatedTargetSerial, SourceView(), SourceView(), false, error,
        defaultPrevented);
}

bool NavigatorScriptHostAdapter::dispatchInputEvent(
    RuntimeContext& runtime, HostInstanceId targetSerial,
    RuntimeErrorCode& error, bool* defaultPrevented)
{
    if (!isTextEditableFormElement(targetSerial) &&
        !isDiscreteFormElement(targetSerial)) {
        error = RuntimeErrorCode::StaleHostObject;
        if (defaultPrevented != nullptr) *defaultPrevented = false;
        return false;
    }
    const HostObjectReference target{
        targetSerial, generation_, kNavigatorElementHostKind};
    return dispatchEvent(runtime, SourceView("input", 5u),
        NavigatorScriptEventType::Input, target, 0u, SourceView(), SourceView(),
        false, error, defaultPrevented);
}

bool NavigatorScriptHostAdapter::dispatchChangeEvent(
    RuntimeContext& runtime, HostInstanceId targetSerial,
    RuntimeErrorCode& error, bool* defaultPrevented)
{
    if (!isTextEditableFormElement(targetSerial) &&
        !isDiscreteFormElement(targetSerial)) {
        error = RuntimeErrorCode::StaleHostObject;
        if (defaultPrevented != nullptr) *defaultPrevented = false;
        return false;
    }
    const HostObjectReference target{
        targetSerial, generation_, kNavigatorElementHostKind};
    return dispatchEvent(runtime, SourceView("change", 6u),
        NavigatorScriptEventType::Change, target, 0u, SourceView(), SourceView(),
        false, error, defaultPrevented);
}

bool NavigatorScriptHostAdapter::dispatchSubmitEvent(
    RuntimeContext& runtime, HostInstanceId formSerial,
    RuntimeErrorCode& error, bool* defaultPrevented)
{
    if (!isFormElement(formSerial)) {
        error = RuntimeErrorCode::StaleHostObject;
        if (defaultPrevented != nullptr) *defaultPrevented = false;
        return false;
    }
    const HostObjectReference target{
        formSerial, generation_, kNavigatorElementHostKind};
    return dispatchEvent(runtime, SourceView("submit", 6u),
        NavigatorScriptEventType::Submit, target, 0u, SourceView(), SourceView(),
        false, error, defaultPrevented);
}

bool NavigatorScriptHostAdapter::requestFormReset(
    RuntimeContext& runtime, HostInstanceId formSerial,
    RuntimeErrorCode& error, bool* defaultPrevented)
{
    error = RuntimeErrorCode::None;
    if (defaultPrevented != nullptr) *defaultPrevented = false;
    if (!isFormElement(formSerial)) {
        error = RuntimeErrorCode::StaleHostObject;
        return false;
    }
    if (resetDepth_ >= kNavigatorScriptMaxActivationDepth) {
        error = RuntimeErrorCode::HostReentryUnsupported;
        return false;
    }

    const HostGenerationId resetGeneration = generation_;
    ++resetDepth_;
    bool resetDefaultPrevented = false;
    const bool dispatched = dispatchEvent(runtime, SourceView("reset", 5u),
        NavigatorScriptEventType::Reset,
        HostObjectReference{formSerial, generation_, kNavigatorElementHostKind},
        0u, SourceView(), SourceView(), false, error, &resetDefaultPrevented);
    if (defaultPrevented != nullptr) *defaultPrevented = resetDefaultPrevented;

    // A reset listener may replace the document. Never apply the old
    // document's defaults after that lifecycle boundary.
    if (dispatched && resetGeneration == generation_ && document_ != nullptr &&
        isFormElement(formSerial) && !resetDefaultPrevented) {
        if (!restoreFormDefaults(formSerial)) {
            error = RuntimeErrorCode::StaleHostObject;
        }
    }
    --resetDepth_;
    return dispatched && error == RuntimeErrorCode::None;
}

bool NavigatorScriptHostAdapter::beginFormEditSession(HostInstanceId serial)
{
    gxos::web::DocBlock* block = formControlBlock(serial);
    gxos::web::FormRuntimeControlState* state = formRuntimeState(serial);
    if (block == nullptr || state == nullptr) return false;
    state->editBaselineValue = block->inputValue;
    state->editBaselineValid = true;
    return true;
}

bool NavigatorScriptHostAdapter::commitFormEditSession(
    HostInstanceId serial, bool& changed)
{
    changed = false;
    const gxos::web::DocBlock* block = formControlBlock(serial);
    gxos::web::FormRuntimeControlState* state = formRuntimeState(serial);
    if (block == nullptr || state == nullptr || !state->editBaselineValid)
        return false;
    changed = block->inputValue != state->editBaselineValue;
    // Keep the state tied to this document's latest committed value while
    // disabling comparison until the next authentic focus gain.
    state->editBaselineValue = block->inputValue;
    state->editBaselineValid = false;
    return true;
}

bool NavigatorScriptHostAdapter::setFormValueFromUser(
    HostInstanceId serial, const std::string& value)
{
    return setElementValue(serial, value, false).succeeded();
}

bool NavigatorScriptHostAdapter::setFormControlFromUser(
    HostInstanceId serial, bool& changed)
{
    return updateDiscreteFormControlFromUser(serial, changed);
}

bool NavigatorScriptHostAdapter::dispatchEvent(RuntimeContext& runtime,
    SourceView type, NavigatorScriptEventType eventType,
    const HostObjectReference& target, HostInstanceId relatedTargetSerial,
    SourceView key, SourceView code,
    bool includeOnclick, RuntimeErrorCode& error, bool* defaultPrevented)
{
    error = RuntimeErrorCode::None;
    if (defaultPrevented != nullptr) *defaultPrevented = false;
    if (document_ == nullptr || !target.valid() ||
        target.generation != generation_ ||
        (target.kind == kNavigatorElementHostKind &&
            findElement(target.instanceId) == nullptr) ||
        (target.kind == kNavigatorDocumentHostKind &&
            target.instanceId != kNavigatorDocumentHostInstance) ||
        (target.kind != kNavigatorElementHostKind &&
            target.kind != kNavigatorDocumentHostKind)) {
        error = RuntimeErrorCode::StaleHostObject;
        return false;
    }
    // Snapshot the DOM ownership chain before entering user code. The serial
    // array is fixed-size and contains no native pointers; every subsequent
    // entry is revalidated against the same document and generation before it
    // can be used. Target is path[0]; ancestors follow toward the root.
    struct EventPathEntry {
        HostObjectKind kind = 0;
        HostInstanceId serial = 0;
    };
    std::array<EventPathEntry, kNavigatorScriptMaxClickPropagationDepth>
        propagationPath{};
    std::size_t propagationLength = 0;
    if (target.kind == kNavigatorElementHostKind) {
        HostInstanceId currentSerial = target.instanceId;
        while (currentSerial != 0) {
            if (propagationLength >= propagationPath.size()) {
                error = RuntimeErrorCode::PropagationPathLimitExceeded;
                return false;
            }
            const gxos::web::HtmlElementRef* element = findElement(currentSerial);
            if (element == nullptr || element->serial == 0) {
                error = RuntimeErrorCode::StaleHostObject;
                return false;
            }
            propagationPath[propagationLength++] = EventPathEntry{
                kNavigatorElementHostKind, element->serial};
            const HostInstanceId parentSerial = element->parentSerial;
            if (parentSerial == currentSerial) {
                error = RuntimeErrorCode::StaleHostObject;
                return false;
            }
            currentSerial = parentSerial;
        }
    }
    // Clicks participate in the same bounded document propagation path as the
    // other event types. The document entry is what lets an ancestor/document
    // listener cancel a control's default action after target dispatch.
    if (propagationLength >= propagationPath.size()) {
        error = RuntimeErrorCode::PropagationPathLimitExceeded;
        return false;
    }
    propagationPath[propagationLength++] = EventPathEntry{
        kNavigatorDocumentHostKind, kNavigatorDocumentHostInstance};

    bool hasDispatchableHandler = false;
    for (std::size_t index = 0; index < propagationLength; ++index) {
        if (hasAnyEventHandler(propagationPath[index].kind,
                propagationPath[index].serial)) {
            hasDispatchableHandler = true;
            break;
        }
    }
    if (!hasDispatchableHandler) return true;

    const HostGenerationId dispatchGeneration = generation_;
    HostObjectReference relatedTargetReference;
    const HostObjectReference* relatedTarget = nullptr;
    // The transition seam supplies a logical serial, not a native pointer.
    // Resolve it against the current document for each dispatch and fail
    // closed to the existing null sentinel if the opposite side has already
    // become stale.
    if (relatedTargetSerial != 0 && findElement(relatedTargetSerial) != nullptr) {
        relatedTargetReference = HostObjectReference{
            relatedTargetSerial, dispatchGeneration, kNavigatorElementHostKind};
        relatedTarget = &relatedTargetReference;
    }
    const bool previousClickDispatchActive = clickDispatchActive_;
    clickDispatchActive_ = true;
    if (!runtime.beginEventDispatch()) {
        clickDispatchActive_ = previousClickDispatchActive;
        error = RuntimeErrorCode::HostReentryUnsupported;
        return false;
    }
    Value event;
    const bool bubbles = eventType != NavigatorScriptEventType::Focus &&
        eventType != NavigatorScriptEventType::Blur;
    const bool cancelable = eventType == NavigatorScriptEventType::Click ||
        eventType == NavigatorScriptEventType::Keydown ||
        eventType == NavigatorScriptEventType::Keyup ||
        eventType == NavigatorScriptEventType::Submit ||
        eventType == NavigatorScriptEventType::Reset;
    if (!runtime.createOrUpdateEventObject(type, target,
            HostObjectReference{propagationPath[0].serial,
                dispatchGeneration, propagationPath[0].kind}, key, code,
            bubbles, cancelable, relatedTarget, event, error)) {
        runtime.endEventDispatch();
        clickDispatchActive_ = previousClickDispatchActive;
        return false;
    }
    std::vector<Value> arguments;
    try {
        arguments.reserve(1u);
        arguments.push_back(event);
    } catch (const std::bad_alloc&) {
        runtime.endEventDispatch();
        clickDispatchActive_ = previousClickDispatchActive;
        error = RuntimeErrorCode::AllocationFailure;
        return false;
    }
    Value ignored;
    bool succeeded = true;
    bool dispatchAborted = false;
    RuntimeErrorCode firstError = RuntimeErrorCode::None;
    const auto invoke = [&](RuntimeFunctionId function) {
        if (function == kInvalidRuntimeFunctionId) return;
        RuntimeErrorCode callbackError = RuntimeErrorCode::None;
        if (!runtime.invokeFunctionInSameRealm(Value::function(function),
            arguments, ignored, callbackError)) {
            succeeded = false;
            if (firstError == RuntimeErrorCode::None) firstError = callbackError;
        }
    };
    // The phase comes from the propagation stage, never from the listener's
    // capture flag. Target capture listeners are therefore AT_TARGET.
    runtime.setEventPhase(kEventPhaseCapturing);
    // One fixed snapshot is reused for every node and phase. Capture and
    // bubble are intentionally collected at different times: mutations made
    // during capture can affect a later bubble snapshot, but never the active
    // snapshot for the current node and phase.
    std::array<ClickListenerSnapshotEntry,
        kNavigatorScriptMaxClickHandlers> listenerSnapshot{};
    std::size_t listenerSnapshotCount = 0;
    const auto collectListeners = [&](const EventPathEntry& current,
        bool capture) {
        if (!collectListenerSnapshot(current.kind, current.serial, eventType,
                listenerSnapshot, listenerSnapshotCount, capture)) {
            if (firstError == RuntimeErrorCode::None)
                firstError = RuntimeErrorCode::HostCallbackLimitExceeded;
            succeeded = false;
            dispatchAborted = true;
            return false;
        }
        return true;
    };
    const auto invokeCollectedListeners = [&](const EventPathEntry& current,
        bool capture) {
        if (listenerSnapshotCount == 0u) return true;
        const HostObjectReference currentTarget{
            current.serial, dispatchGeneration, current.kind};
        RuntimeErrorCode eventError = RuntimeErrorCode::None;
        if (!runtime.createOrUpdateEventObject(type, target, currentTarget,
                key, code, bubbles, cancelable, relatedTarget, event,
                eventError)) {
            if (firstError == RuntimeErrorCode::None) firstError = eventError;
            succeeded = false;
            dispatchAborted = true;
            return false;
        }
        arguments[0] = event;
        for (std::size_t listenerIndex = 0;
             listenerIndex < listenerSnapshotCount; ++listenerIndex) {
            if (runtime.eventImmediatePropagationStopped()) break;
            const ClickListenerSnapshotEntry& captured =
                listenerSnapshot[listenerIndex];
            ClickListenerRecord* active = clickListenerForSequence(
                current.kind, current.serial, captured.registrationSequence);
            if (active == nullptr ||
                active->listenerFunction != captured.listenerFunction ||
                active->eventType != eventType ||
                (((active->flags & kNavigatorClickListenerCaptureFlag) != 0u) !=
                    capture)) continue;
            const RuntimeFunctionId listenerFunction =
                active->listenerFunction;
            if ((active->flags & kNavigatorClickListenerOnceFlag) != 0u)
                removeClickListener(*active);
            invoke(listenerFunction);
        }
        return true;
    };
    const auto invokeListeners = [&](const EventPathEntry& current,
        bool capture) {
        return collectListeners(current, capture) &&
            invokeCollectedListeners(current, capture);
    };
    const auto updateCurrentTarget = [&](const EventPathEntry& current) {
        const HostObjectReference currentTarget{
            current.serial, dispatchGeneration, current.kind};
        RuntimeErrorCode eventError = RuntimeErrorCode::None;
        if (!runtime.createOrUpdateEventObject(type, target, currentTarget,
                key, code, bubbles, cancelable, relatedTarget, event,
                eventError)) {
            if (firstError == RuntimeErrorCode::None) firstError = eventError;
            succeeded = false;
            dispatchAborted = true;
            return false;
        }
        arguments[0] = event;
        return true;
    };
    const auto validatePathEntry = [&](const EventPathEntry& current) {
        const HostObjectReference reference{
            current.serial, dispatchGeneration, current.kind};
        if (generation_ == dispatchGeneration && document_ != nullptr &&
            validate(reference).succeeded()) return true;
        if (firstError == RuntimeErrorCode::None)
            firstError = RuntimeErrorCode::StaleHostObject;
        succeeded = false;
        dispatchAborted = true;
        return false;
    };

    // Capture walks the existing target-to-root path in reverse. Ancestor
    // onclick handlers are not part of this phase.
    for (std::size_t reverse = propagationLength; reverse > 1u; --reverse) {
        const EventPathEntry& current = propagationPath[reverse - 1u];
        if (!validatePathEntry(current) || !invokeListeners(current, true)) break;
        if (runtime.eventPropagationStopped()) break;
    }

    // The target has one dispatch stage. Its capture listeners, onclick, and
    // non-capture listeners share the target currentTarget. stopPropagation
    // still permits all later target handlers; immediate stop does not.
    if (!dispatchAborted && !runtime.eventPropagationStopped() &&
        validatePathEntry(propagationPath[0])) {
        runtime.setEventPhase(kEventPhaseAtTarget);
        if (invokeListeners(propagationPath[0], true) &&
            !runtime.eventImmediatePropagationStopped()) {
            // Preserve JS17's target mutation rule: the target bubble
            // snapshot is fixed before target onclick runs, while capture
            // mutations can still affect this later target-phase snapshot.
            if (collectListeners(propagationPath[0], false)) {
                const ClickHandlerRecord* record =
                    propagationPath[0].kind == kNavigatorElementHostKind
                    ? clickHandlerFor(propagationPath[0].serial) : nullptr;
                const RuntimeFunctionId onclickFunction = includeOnclick &&
                    record != nullptr ? record->onclickFunction :
                    kInvalidRuntimeFunctionId;
                if (onclickFunction != kInvalidRuntimeFunctionId) {
                    if (updateCurrentTarget(propagationPath[0]))
                        invoke(onclickFunction);
                }
                if (!dispatchAborted &&
                    !runtime.eventImmediatePropagationStopped()) {
                    invokeCollectedListeners(propagationPath[0], false);
                }
            }
        }
    }

    // Bubble uses the original forward path, starting with the target's
    // parent. Each ancestor gets a fresh non-capture snapshot at this point.
    // Focus and blur intentionally stop after target delivery; their capture
    // phase remains observable through the same ancestor path.
    if (bubbles && !dispatchAborted && !runtime.eventPropagationStopped() &&
        !runtime.eventImmediatePropagationStopped()) {
        runtime.setEventPhase(kEventPhaseBubbling);
        for (std::size_t index = 1u; index < propagationLength; ++index) {
            const EventPathEntry& current = propagationPath[index];
            if (!validatePathEntry(current)) break;
            const ClickHandlerRecord* record =
                current.kind == kNavigatorElementHostKind
                ? clickHandlerFor(current.serial) : nullptr;
            const RuntimeFunctionId onclickFunction = includeOnclick &&
                record != nullptr ? record->onclickFunction :
                kInvalidRuntimeFunctionId;
            if (onclickFunction != kInvalidRuntimeFunctionId) {
                if (!updateCurrentTarget(current)) break;
                invoke(onclickFunction);
            }
            if (runtime.eventImmediatePropagationStopped()) break;
            if (!invokeListeners(current, false)) break;
            if (runtime.eventPropagationStopped()) break;
        }
    }
    const bool dispatchDefaultPrevented = runtime.eventDefaultPrevented();
    if (defaultPrevented != nullptr)
        *defaultPrevented = dispatchDefaultPrevented;
    runtime.endEventDispatch();
    clickDispatchActive_ = previousClickDispatchActive;
    error = firstError;
    if (dispatchCompleteCallback_ != nullptr)
        dispatchCompleteCallback_(dispatchCompleteContext_);
    return succeeded;
}

bool NavigatorScriptHostAdapter::eventTypeFor(SourceView type,
    NavigatorScriptEventType& eventType) const
{
    if (textEquals(type, "click")) {
        eventType = NavigatorScriptEventType::Click;
        return true;
    }
    if (textEquals(type, "keydown")) {
        eventType = NavigatorScriptEventType::Keydown;
        return true;
    }
    if (textEquals(type, "keyup")) {
        eventType = NavigatorScriptEventType::Keyup;
        return true;
    }
    if (textEquals(type, "focus")) {
        eventType = NavigatorScriptEventType::Focus;
        return true;
    }
    if (textEquals(type, "blur")) {
        eventType = NavigatorScriptEventType::Blur;
        return true;
    }
    if (textEquals(type, "focusin")) {
        eventType = NavigatorScriptEventType::Focusin;
        return true;
    }
    if (textEquals(type, "focusout")) {
        eventType = NavigatorScriptEventType::Focusout;
        return true;
    }
    if (textEquals(type, "input")) {
        eventType = NavigatorScriptEventType::Input;
        return true;
    }
    if (textEquals(type, "change")) {
        eventType = NavigatorScriptEventType::Change;
        return true;
    }
    if (textEquals(type, "submit")) {
        eventType = NavigatorScriptEventType::Submit;
        return true;
    }
    if (textEquals(type, "reset")) {
        eventType = NavigatorScriptEventType::Reset;
        return true;
    }
    return false;
}

bool NavigatorScriptHostAdapter::isFormElement(HostInstanceId serial) const
{
    const gxos::web::HtmlElementRef* element = findElement(serial);
    if (element == nullptr) return false;
    return element->tagName == "form" || element->tagName == "FORM";
}

bool NavigatorScriptHostAdapter::isTextEditableFormElement(
    HostInstanceId serial) const
{
    const gxos::web::DocBlock* block = formControlBlock(serial);
    if (block == nullptr || !block->formControl.metadataComplete ||
        !block->formControl.supported) return false;
    return block->type == gxos::web::BlockType::FormTextInput ||
        block->type == gxos::web::BlockType::FormTextarea;
}

bool NavigatorScriptHostAdapter::isCheckableFormElement(
    HostInstanceId serial) const
{
    const gxos::web::DocBlock* block = formControlBlock(serial);
    if (block == nullptr || !block->formControl.metadataComplete ||
        !block->formControl.supported) return false;
    return block->type == gxos::web::BlockType::FormCheckbox ||
        block->type == gxos::web::BlockType::FormRadio;
}

bool NavigatorScriptHostAdapter::isSelectFormElement(
    HostInstanceId serial) const
{
    const gxos::web::DocBlock* block = formControlBlock(serial);
    return block != nullptr && block->formControl.metadataComplete &&
        block->formControl.supported &&
        block->type == gxos::web::BlockType::FormSelect;
}

bool NavigatorScriptHostAdapter::isOptionElement(HostInstanceId serial) const
{
    const gxos::web::HtmlElementRef* element = findElement(serial);
    return element != nullptr &&
        element->formControl.supported &&
        element->formControl.type == gxos::web::FormControlType::Option;
}

bool NavigatorScriptHostAdapter::documentFormAt(std::size_t index,
    HostInstanceId& formSerial) const
{
    formSerial = 0;
    if (document_ == nullptr) return false;
    std::size_t matched = 0;
    const std::size_t count = std::min(limits_.maxDocumentNodes,
        document_->structuralElements.size());
    for (std::size_t position = 0; position < count; ++position) {
        const gxos::web::HtmlElementRef& element =
            document_->structuralElements[position];
        if (element.serial == 0 || element.tagName != "form") continue;
        if (matched == index) {
            formSerial = element.serial;
            return true;
        }
        ++matched;
    }
    return false;
}

std::size_t NavigatorScriptHostAdapter::documentFormCount() const
{
    std::size_t count = 0;
    HostInstanceId ignored = 0;
    while (documentFormAt(count, ignored)) ++count;
    return count;
}

bool NavigatorScriptHostAdapter::documentFormNamed(SourceView property,
    HostInstanceId& formSerial) const
{
    formSerial = 0;
    if (document_ == nullptr || property.data == nullptr ||
        property.length == 0 || property.length > limits_.maxDocumentIdLength)
        return false;
    const std::string key(property.data, property.length);
    const std::size_t count = std::min(limits_.maxDocumentNodes,
        document_->structuralElements.size());

    // Match ids first, in document order. This is the same canonical element
    // metadata later used by getElementById(), without creating a form map.
    for (std::size_t position = 0; position < count; ++position) {
        const gxos::web::HtmlElementRef& element =
            document_->structuralElements[position];
        if (element.serial != 0 && element.tagName == "form" &&
            !element.id.empty() && element.id == key) {
            formSerial = element.serial;
            return true;
        }
    }

    // Form names are parser-owned container metadata. Empty names are not
    // useful named properties, and duplicate names intentionally resolve to
    // the first form encountered in document order.
    for (std::size_t position = 0; position < count; ++position) {
        const gxos::web::HtmlElementRef& element =
            document_->structuralElements[position];
        if (element.serial == 0 || element.tagName != "form") continue;
        for (const gxos::web::FormContainerMetadata& container :
                 document_->formContainers) {
            if (container.serial == element.serial && !container.name.empty() &&
                container.name == key) {
                formSerial = element.serial;
                return true;
            }
        }
    }
    return false;
}

bool NavigatorScriptHostAdapter::optionIndexFor(HostInstanceId optionSerial,
    HostInstanceId& selectSerial, std::size_t& optionIndex) const
{
    selectSerial = 0;
    optionIndex = 0;
    const gxos::web::HtmlElementRef* option = findElement(optionSerial);
    if (option == nullptr || !isOptionElement(optionSerial) ||
        option->parentSerial == 0) return false;
    const gxos::web::DocBlock* select = formControlBlock(option->parentSerial);
    if (select == nullptr || select->type != gxos::web::BlockType::FormSelect)
        return false;

    std::size_t index = 0;
    const std::size_t count = std::min(limits_.maxDocumentNodes,
        document_->structuralElements.size());
    for (std::size_t position = 0; position < count; ++position) {
        const gxos::web::HtmlElementRef& candidate =
            document_->structuralElements[position];
        if (candidate.formControl.type != gxos::web::FormControlType::Option ||
            candidate.parentSerial != option->parentSerial) continue;
        if (candidate.serial == optionSerial) {
            if (index >= select->options.size()) return false;
            selectSerial = option->parentSerial;
            optionIndex = index;
            return true;
        }
        ++index;
    }
    return false;
}

bool NavigatorScriptHostAdapter::formElementAt(HostInstanceId formSerial,
    std::size_t index, HostInstanceId& elementSerial) const
{
    elementSerial = 0;
    if (!isFormElement(formSerial)) return false;
    std::size_t matched = 0;
    const std::size_t count = std::min(limits_.maxDocumentNodes,
        document_->structuralElements.size());
    for (std::size_t position = 0; position < count; ++position) {
        const gxos::web::HtmlElementRef& element =
            document_->structuralElements[position];
        const gxos::web::FormControlMetadata& metadata = element.formControl;
        if (element.serial == 0 || !metadata.metadataComplete ||
            !metadata.supported || metadata.parentFormSerial != formSerial ||
            metadata.type == gxos::web::FormControlType::None ||
            metadata.type == gxos::web::FormControlType::Option ||
            metadata.type == gxos::web::FormControlType::Unsupported) continue;
        if (matched == index) {
            elementSerial = element.serial;
            return true;
        }
        ++matched;
    }
    return false;
}

std::size_t NavigatorScriptHostAdapter::formElementCount(
    HostInstanceId formSerial) const
{
    if (!isFormElement(formSerial)) return 0;
    std::size_t count = 0;
    HostInstanceId ignored = 0;
    while (formElementAt(formSerial, count, ignored)) ++count;
    return count;
}

bool NavigatorScriptHostAdapter::formElementNamed(HostInstanceId formSerial,
    SourceView property, HostInstanceId& elementSerial) const
{
    elementSerial = 0;
    if (!isFormElement(formSerial) || property.data == nullptr ||
        property.length == 0 || property.length > limits_.maxDocumentIdLength)
        return false;
    const std::string key(property.data, property.length);
    const std::size_t count = std::min(limits_.maxDocumentNodes,
        document_->structuralElements.size());
    const auto isOwnedSupportedControl = [&](
        const gxos::web::HtmlElementRef& element) {
        const gxos::web::FormControlMetadata& metadata = element.formControl;
        return element.serial != 0 && metadata.metadataComplete &&
            metadata.supported && metadata.parentFormSerial == formSerial &&
            metadata.type != gxos::web::FormControlType::None &&
            metadata.type != gxos::web::FormControlType::Option &&
            metadata.type != gxos::web::FormControlType::Unsupported;
    };

    // Both named collection views use exact id-first matching. The scan order
    // gives duplicate names the bounded first-in-document-order behavior.
    for (std::size_t position = 0; position < count; ++position) {
        const gxos::web::HtmlElementRef& element =
            document_->structuralElements[position];
        if (isOwnedSupportedControl(element) && !element.id.empty() &&
            element.id == key) {
            elementSerial = element.serial;
            return true;
        }
    }
    for (std::size_t position = 0; position < count; ++position) {
        const gxos::web::HtmlElementRef& element =
            document_->structuralElements[position];
        if (isOwnedSupportedControl(element) && !element.formControl.name.empty() &&
            element.formControl.name == key) {
            elementSerial = element.serial;
            return true;
        }
    }
    return false;
}

bool NavigatorScriptHostAdapter::selectOptionAt(HostInstanceId selectSerial,
    std::size_t index, HostInstanceId& optionSerial) const
{
    optionSerial = 0;
    const gxos::web::DocBlock* select = formControlBlock(selectSerial);
    if (select == nullptr || select->type != gxos::web::BlockType::FormSelect ||
        index >= select->options.size()) return false;

    std::size_t matched = 0;
    const std::size_t count = std::min(limits_.maxDocumentNodes,
        document_->structuralElements.size());
    for (std::size_t position = 0; position < count; ++position) {
        const gxos::web::HtmlElementRef& element =
            document_->structuralElements[position];
        if (element.formControl.type != gxos::web::FormControlType::Option ||
            element.parentSerial != selectSerial) continue;
        if (matched == index) {
            optionSerial = element.serial;
            return optionSerial != 0;
        }
        ++matched;
    }
    return false;
}

HostResult NavigatorScriptHostAdapter::setOptionSelected(
    HostInstanceId optionSerial, bool selected)
{
    HostInstanceId selectSerial = 0;
    std::size_t optionIndex = 0;
    if (!optionIndexFor(optionSerial, selectSerial, optionIndex))
        return HostResult{HostResultCode::PropertyWriteFailed};
    const gxos::web::DocBlock* select = formControlBlock(selectSerial);
    if (select == nullptr || select->formControl.multiple)
        return HostResult{HostResultCode::PropertyWriteFailed};
    if (!selected && select->selectedOption != static_cast<int>(optionIndex))
        return HostResult();
    return setSelectIndex(selectSerial, selected
        ? static_cast<int>(optionIndex) : -1, true);
}

HostResult NavigatorScriptHostAdapter::setOptionDefaultSelected(
    HostInstanceId optionSerial, bool selected)
{
    HostInstanceId selectSerial = 0;
    std::size_t optionIndex = 0;
    if (!optionIndexFor(optionSerial, selectSerial, optionIndex))
        return HostResult{HostResultCode::PropertyWriteFailed};
    const gxos::web::DocBlock* select = formControlBlock(selectSerial);
    gxos::web::FormRuntimeControlState* state = formRuntimeState(selectSerial);
    if (select == nullptr || state == nullptr || select->formControl.multiple)
        return HostResult{HostResultCode::PropertyWriteFailed};
    if (document_->scriptMutationCount >= limits_.maxDocumentMutations)
        return HostResult{HostResultCode::DocumentMutationLimitExceeded};
    state->defaultSelectedOption = selected
        ? static_cast<int>(optionIndex) :
        (state->defaultSelectedOption == static_cast<int>(optionIndex)
            ? -1 : state->defaultSelectedOption);
    ++document_->scriptMutationCount;
    return HostResult();
}

bool NavigatorScriptHostAdapter::isDiscreteFormElement(
    HostInstanceId serial) const
{
    return isCheckableFormElement(serial) || isSelectFormElement(serial);
}

gxos::web::DocBlock* NavigatorScriptHostAdapter::formControlBlock(
    HostInstanceId serial)
{
    if (document_ == nullptr || serial == 0) return nullptr;
    for (gxos::web::DocBlock& block : document_->blocks) {
        if ((block.type == gxos::web::BlockType::FormTextInput ||
                block.type == gxos::web::BlockType::FormTextarea ||
                block.type == gxos::web::BlockType::FormCheckbox ||
                block.type == gxos::web::BlockType::FormRadio ||
                block.type == gxos::web::BlockType::FormSelect) &&
            block.formControl.logicalSerial == serial)
            return &block;
    }
    return nullptr;
}

const gxos::web::DocBlock* NavigatorScriptHostAdapter::formControlBlock(
    HostInstanceId serial) const
{
    if (document_ == nullptr || serial == 0) return nullptr;
    for (const gxos::web::DocBlock& block : document_->blocks) {
        if ((block.type == gxos::web::BlockType::FormTextInput ||
                block.type == gxos::web::BlockType::FormTextarea ||
                block.type == gxos::web::BlockType::FormCheckbox ||
                block.type == gxos::web::BlockType::FormRadio ||
                block.type == gxos::web::BlockType::FormSelect) &&
            block.formControl.logicalSerial == serial)
            return &block;
    }
    return nullptr;
}

gxos::web::FormRuntimeControlState*
NavigatorScriptHostAdapter::formRuntimeState(HostInstanceId serial)
{
    if (document_ == nullptr || serial == 0) return nullptr;
    const std::size_t count = std::min(
        document_->formRuntimeState.count,
        gxos::web::kFormRuntimeControlCap);
    for (std::size_t index = 0; index < count; ++index) {
        gxos::web::FormRuntimeControlState& state =
            document_->formRuntimeState.controls[index];
        if (state.logicalSerial == serial && state.metadataValid)
            return &state;
    }
    return nullptr;
}

const gxos::web::FormRuntimeControlState*
NavigatorScriptHostAdapter::formRuntimeState(HostInstanceId serial) const
{
    if (document_ == nullptr || serial == 0) return nullptr;
    const std::size_t count = std::min(
        document_->formRuntimeState.count,
        gxos::web::kFormRuntimeControlCap);
    for (std::size_t index = 0; index < count; ++index) {
        const gxos::web::FormRuntimeControlState& state =
            document_->formRuntimeState.controls[index];
        if (state.logicalSerial == serial && state.metadataValid)
            return &state;
    }
    return nullptr;
}

HostResult NavigatorScriptHostAdapter::setElementValue(
    HostInstanceId serial, const std::string& value, bool scriptMutation)
{
    if (document_ == nullptr || !isTextEditableFormElement(serial))
        return HostResult{HostResultCode::PropertyWriteFailed};
    if (value.size() > kNavigatorScriptMaxFormValueBytes)
        return HostResult{HostResultCode::DocumentTextLimitExceeded};
    if (scriptMutation && document_->scriptMutationCount >=
        limits_.maxDocumentMutations)
        return HostResult{HostResultCode::DocumentMutationLimitExceeded};

    gxos::web::DocBlock* block = formControlBlock(serial);
    if (block == nullptr) return HostResult{HostResultCode::PropertyWriteFailed};
    block->inputValue = value;
    block->text = value;
    block->formControl.value = value;
    for (gxos::web::HtmlElementRef& element : document_->structuralElements) {
        if (element.serial == serial) element.formControl.value = value;
    }
    document_->layoutDirty = true;
    if (scriptMutation) ++document_->scriptMutationCount;
    return HostResult();
}

void NavigatorScriptHostAdapter::syncCheckableState(
    HostInstanceId serial, bool checked)
{
    if (document_ == nullptr) return;
    for (gxos::web::DocBlock& block : document_->blocks) {
        if (block.formControl.logicalSerial != serial) continue;
        block.checked = checked;
        block.formControl.checked = checked;
    }
    for (gxos::web::HtmlElementRef& element : document_->structuralElements) {
        if (element.serial != serial) continue;
        element.formControl.checked = checked;
    }
}

void NavigatorScriptHostAdapter::syncSelectState(gxos::web::DocBlock& block)
{
    block.formControl.selectedOptionIndex = block.selectedOption;
    block.formControl.value = block.inputValue;
    for (std::size_t index = 0; index < block.options.size(); ++index)
        block.options[index].selected =
            static_cast<int>(index) == block.selectedOption;
    if (document_ == nullptr) return;
    for (gxos::web::HtmlElementRef& element : document_->structuralElements) {
        if (element.serial != block.formControl.logicalSerial) continue;
        element.formControl.selectedOptionIndex = block.selectedOption;
        element.formControl.value = block.inputValue;
        break;
    }
}

bool NavigatorScriptHostAdapter::restoreFormDefaults(HostInstanceId formSerial)
{
    if (document_ == nullptr || !isFormElement(formSerial)) return false;

    // The reset pass has no script callbacks. First establish every current
    // state, including a radio group-wide all-off phase, then mirror it to
    // the compact document projections. This keeps the operation atomic from
    // JavaScript's point of view and avoids input/change dispatch entirely.
    for (gxos::web::DocBlock& block : document_->blocks) {
        if (block.formControl.parentFormSerial != formSerial) continue;
        gxos::web::FormRuntimeControlState* state =
            formRuntimeState(block.formControl.logicalSerial);
        if (state == nullptr) continue;
        if (block.type == gxos::web::BlockType::FormRadio) {
            state->checked = false;
        } else if (block.type == gxos::web::BlockType::FormCheckbox) {
            state->checked = state->defaultChecked;
        }
    }

    // Preserve the parser/runtime's bounded interpretation for malformed
    // radio markup: the first initially checked member in document order wins.
    for (std::size_t index = 0; index < document_->blocks.size(); ++index) {
        gxos::web::DocBlock& block = document_->blocks[index];
        if (block.type != gxos::web::BlockType::FormRadio ||
            block.formControl.parentFormSerial != formSerial) continue;
        gxos::web::FormRuntimeControlState* state =
            formRuntimeState(block.formControl.logicalSerial);
        if (state == nullptr || !state->defaultChecked) continue;
        bool earlierSelected = false;
        for (std::size_t prior = 0; prior < index; ++prior) {
            const gxos::web::DocBlock& candidate = document_->blocks[prior];
            if (candidate.type != gxos::web::BlockType::FormRadio ||
                candidate.formControl.parentFormSerial != formSerial ||
                !radioGroupMatches(candidate, block)) continue;
            const gxos::web::FormRuntimeControlState* candidateState =
                formRuntimeState(candidate.formControl.logicalSerial);
            if (candidateState != nullptr && candidateState->checked) {
                earlierSelected = true;
                break;
            }
        }
        if (!earlierSelected) state->checked = true;
    }

    for (gxos::web::DocBlock& block : document_->blocks) {
        if (block.formControl.parentFormSerial != formSerial) continue;
        gxos::web::FormRuntimeControlState* state =
            formRuntimeState(block.formControl.logicalSerial);
        if (state == nullptr) continue;
        if (block.type == gxos::web::BlockType::FormTextInput ||
            block.type == gxos::web::BlockType::FormTextarea) {
            block.inputValue = state->defaultValue;
            block.text = block.type == gxos::web::BlockType::FormTextInput &&
                block.inputValue.empty() && !block.placeholder.empty()
                ? block.placeholder : block.inputValue;
            block.formControl.value = state->defaultValue;
            for (gxos::web::HtmlElementRef& element : document_->structuralElements) {
                if (element.serial == block.formControl.logicalSerial)
                    element.formControl.value = state->defaultValue;
            }
        } else if (block.type == gxos::web::BlockType::FormSelect) {
            if (!block.formControl.multiple) {
                block.selectedOption = state->defaultSelectedOption;
                if (block.selectedOption >= 0 &&
                    block.selectedOption < static_cast<int>(block.options.size())) {
                    block.inputValue = block.options[static_cast<std::size_t>(
                        block.selectedOption)].value;
                    block.text = block.options[static_cast<std::size_t>(
                        block.selectedOption)].text;
                } else {
                    block.inputValue.clear();
                    block.text.clear();
                }
                syncSelectState(block);
            }
        } else if (block.type == gxos::web::BlockType::FormCheckbox ||
                   block.type == gxos::web::BlockType::FormRadio) {
            syncCheckableState(block.formControl.logicalSerial, state->checked);
        }
    }

    const HostInstanceId focusedSerial =
        document_->formRuntimeState.focusValid
            ? document_->formRuntimeState.focusedLogicalSerial : 0;
    const bool focusGenerationValid = document_->formRuntimeState.focusValid &&
        document_->formRuntimeState.documentGeneration != 0 &&
        document_->formRuntimeState.focusedDocumentGeneration ==
            document_->formRuntimeState.documentGeneration;
    const std::size_t stateCount = std::min(
        document_->formRuntimeState.count,
        gxos::web::kFormRuntimeControlCap);
    for (std::size_t index = 0; index < stateCount; ++index) {
        gxos::web::FormRuntimeControlState& state =
            document_->formRuntimeState.controls[index];
        if (!state.metadataValid || state.parentFormSerial != formSerial)
            continue;
        state.editBaselineValid = false;
        if (focusGenerationValid && state.logicalSerial == focusedSerial &&
            (state.type == gxos::web::FormControlType::Text ||
             state.type == gxos::web::FormControlType::Password ||
             state.type == gxos::web::FormControlType::Search ||
             state.type == gxos::web::FormControlType::Email ||
             state.type == gxos::web::FormControlType::Url ||
             state.type == gxos::web::FormControlType::Number ||
             state.type == gxos::web::FormControlType::Textarea)) {
            state.editBaselineValue = state.defaultValue;
            state.editBaselineValid = true;
        }
    }
    document_->layoutDirty = true;
    return true;
}

bool NavigatorScriptHostAdapter::radioGroupMatches(
    const gxos::web::DocBlock& left, const gxos::web::DocBlock& right) const
{
    if (left.type != gxos::web::BlockType::FormRadio ||
        right.type != gxos::web::BlockType::FormRadio) return false;
    if (left.formControl.name.empty() || right.formControl.name.empty())
        return left.formControl.logicalSerial == right.formControl.logicalSerial;
    if (left.formIndex >= 0 || right.formIndex >= 0)
        return left.formIndex >= 0 && right.formIndex == left.formIndex &&
            left.formControl.name == right.formControl.name;
    if (left.formControl.parentFormSerial != 0 ||
        right.formControl.parentFormSerial != 0)
        return left.formControl.parentFormSerial != 0 &&
            right.formControl.parentFormSerial == left.formControl.parentFormSerial &&
            left.formControl.name == right.formControl.name;
    if (left.formControl.parentFieldsetSerial != 0 ||
        right.formControl.parentFieldsetSerial != 0)
        return left.formControl.parentFieldsetSerial != 0 &&
            right.formControl.parentFieldsetSerial == left.formControl.parentFieldsetSerial &&
            left.formControl.name == right.formControl.name;
    return left.formControl.name == right.formControl.name;
}

HostResult NavigatorScriptHostAdapter::setElementChecked(
    HostInstanceId serial, bool checked, bool scriptMutation)
{
    if (document_ == nullptr || !isCheckableFormElement(serial))
        return HostResult{HostResultCode::PropertyWriteFailed};
    if (scriptMutation && document_->scriptMutationCount >=
        limits_.maxDocumentMutations)
        return HostResult{HostResultCode::DocumentMutationLimitExceeded};

    gxos::web::DocBlock* block = formControlBlock(serial);
    gxos::web::FormRuntimeControlState* state = formRuntimeState(serial);
    if (block == nullptr || state == nullptr)
        return HostResult{HostResultCode::PropertyWriteFailed};

    if (block->type == gxos::web::BlockType::FormRadio && checked) {
        for (gxos::web::DocBlock& candidate : document_->blocks) {
            if (&candidate == block || !radioGroupMatches(candidate, *block)) continue;
            gxos::web::FormRuntimeControlState* candidateState =
                formRuntimeState(candidate.formControl.logicalSerial);
            if (candidateState != nullptr) candidateState->checked = false;
            syncCheckableState(candidate.formControl.logicalSerial, false);
        }
    }
    state->checked = checked;
    syncCheckableState(serial, checked);
    document_->layoutDirty = true;
    if (scriptMutation) ++document_->scriptMutationCount;
    return HostResult();
}

HostResult NavigatorScriptHostAdapter::setElementDefaultValue(
    HostInstanceId serial, const std::string& value)
{
    if (document_ == nullptr || !isTextEditableFormElement(serial))
        return HostResult{HostResultCode::PropertyWriteFailed};
    if (value.size() > kNavigatorScriptMaxFormValueBytes)
        return HostResult{HostResultCode::DocumentTextLimitExceeded};
    if (document_->scriptMutationCount >= limits_.maxDocumentMutations)
        return HostResult{HostResultCode::DocumentMutationLimitExceeded};
    gxos::web::FormRuntimeControlState* state = formRuntimeState(serial);
    if (state == nullptr) return HostResult{HostResultCode::PropertyWriteFailed};
    state->defaultValue = value;
    ++document_->scriptMutationCount;
    return HostResult();
}

HostResult NavigatorScriptHostAdapter::setElementDefaultChecked(
    HostInstanceId serial, bool checked)
{
    if (document_ == nullptr || !isCheckableFormElement(serial))
        return HostResult{HostResultCode::PropertyWriteFailed};
    if (document_->scriptMutationCount >= limits_.maxDocumentMutations)
        return HostResult{HostResultCode::DocumentMutationLimitExceeded};
    gxos::web::DocBlock* block = formControlBlock(serial);
    gxos::web::FormRuntimeControlState* state = formRuntimeState(serial);
    if (block == nullptr || state == nullptr)
        return HostResult{HostResultCode::PropertyWriteFailed};

    // Default radio state is a separate group from current checked state.
    // After a write, normalize the complete group with the same first-
    // document-order-wins rule used for parser defaults and reset. This never
    // touches any current checked projection.
    state->defaultChecked = checked;
    if (block->type == gxos::web::BlockType::FormRadio && checked) {
        bool winnerFound = false;
        for (gxos::web::DocBlock& candidate : document_->blocks) {
            if (!radioGroupMatches(candidate, *block)) continue;
            gxos::web::FormRuntimeControlState* candidateState =
                formRuntimeState(candidate.formControl.logicalSerial);
            if (candidateState == nullptr || !candidateState->defaultChecked)
                continue;
            if (winnerFound) candidateState->defaultChecked = false;
            else winnerFound = true;
        }
    }
    ++document_->scriptMutationCount;
    return HostResult();
}

HostResult NavigatorScriptHostAdapter::setSelectIndex(
    HostInstanceId serial, int index, bool scriptMutation)
{
    if (document_ == nullptr || !isSelectFormElement(serial))
        return HostResult{HostResultCode::PropertyWriteFailed};
    if (scriptMutation && document_->scriptMutationCount >=
        limits_.maxDocumentMutations)
        return HostResult{HostResultCode::DocumentMutationLimitExceeded};
    gxos::web::DocBlock* block = formControlBlock(serial);
    if (block == nullptr || block->formControl.multiple)
        return HostResult{HostResultCode::PropertyWriteFailed};

    // Single-select has a bounded no-selection sentinel. Other out-of-range
    // indexes are safe no-ops without touching the current selection.
    if (index < -1 || index >= static_cast<int>(block->options.size()))
        return HostResult();
    if (index != block->selectedOption) {
        block->selectedOption = index;
        if (index < 0) {
            block->inputValue.clear();
            block->text.clear();
        } else {
            block->inputValue = block->options[static_cast<std::size_t>(index)].value;
            block->text = block->options[static_cast<std::size_t>(index)].text;
        }
        syncSelectState(*block);
        document_->layoutDirty = true;
    }
    if (scriptMutation) ++document_->scriptMutationCount;
    return HostResult();
}

HostResult NavigatorScriptHostAdapter::setSelectValue(
    HostInstanceId serial, const std::string& value, bool scriptMutation)
{
    if (document_ == nullptr || !isSelectFormElement(serial))
        return HostResult{HostResultCode::PropertyWriteFailed};
    if (value.size() > kNavigatorScriptMaxFormValueBytes)
        return HostResult{HostResultCode::DocumentTextLimitExceeded};
    if (scriptMutation && document_->scriptMutationCount >=
        limits_.maxDocumentMutations)
        return HostResult{HostResultCode::DocumentMutationLimitExceeded};

    gxos::web::DocBlock* block = formControlBlock(serial);
    if (block == nullptr) return HostResult{HostResultCode::PropertyWriteFailed};
    if (block->formControl.multiple) {
        // JS27 is deliberately single-select only. A multiple select keeps
        // its existing native state and safely ignores this bounded setter.
        if (scriptMutation) ++document_->scriptMutationCount;
        return HostResult();
    }
    int match = -1;
    for (int index = 0; index < static_cast<int>(block->options.size()); ++index) {
        if (block->options[static_cast<std::size_t>(index)].value == value) {
            match = index;
            break;
        }
    }
    if (match >= 0 && match != block->selectedOption) {
        block->selectedOption = match;
        block->inputValue = value;
        block->text = block->options[static_cast<std::size_t>(match)].text;
        syncSelectState(*block);
        document_->layoutDirty = true;
    }
    if (scriptMutation) ++document_->scriptMutationCount;
    return HostResult();
}

bool NavigatorScriptHostAdapter::updateDiscreteFormControlFromUser(
    HostInstanceId serial, bool& changed)
{
    changed = false;
    if (document_ == nullptr || !isDiscreteFormElement(serial)) return false;
    gxos::web::DocBlock* block = formControlBlock(serial);
    gxos::web::FormRuntimeControlState* state = formRuntimeState(serial);
    if (block == nullptr || state == nullptr || state->disabled ||
        block->formControl.disabled) return false;

    if (block->type == gxos::web::BlockType::FormCheckbox) {
        state->checked = !state->checked;
        syncCheckableState(serial, state->checked);
        changed = true;
    } else if (block->type == gxos::web::BlockType::FormRadio) {
        if (state->checked) return true;
        for (gxos::web::DocBlock& candidate : document_->blocks) {
            if (&candidate == block || !radioGroupMatches(candidate, *block)) continue;
            if (gxos::web::FormRuntimeControlState* candidateState =
                    formRuntimeState(candidate.formControl.logicalSerial)) {
                candidateState->checked = false;
                syncCheckableState(candidate.formControl.logicalSerial, false);
            }
        }
        state->checked = true;
        syncCheckableState(serial, true);
        changed = true;
    } else if (block->type == gxos::web::BlockType::FormSelect) {
        if (block->formControl.multiple || block->options.empty()) return true;
        const int optionCount = static_cast<int>(block->options.size());
        const int current = block->selectedOption;
        int next = current < 0 ? 0 : (current + 1) % optionCount;
        bool foundEnabled = false;
        for (int checked = 0; checked < optionCount; ++checked) {
            if (!block->options[static_cast<std::size_t>(next)].disabled) {
                foundEnabled = true;
                break;
            }
            next = (next + 1) % optionCount;
        }
        if (!foundEnabled || next == current) return true;
        block->selectedOption = next;
        block->inputValue = block->options[static_cast<std::size_t>(next)].value;
        block->text = block->options[static_cast<std::size_t>(next)].text;
        syncSelectState(*block);
        document_->layoutDirty = true;
        changed = true;
    }
    return true;
}

bool NavigatorScriptHostAdapter::eventNameForKey(int keyCode,
    bool shiftPressed, std::string& key, std::string& code) const
{
    key.clear();
    code.clear();
    if (keyCode >= 65 && keyCode <= 90) {
        key.assign(1, static_cast<char>(shiftPressed ? keyCode : keyCode + 32));
        code = "Key";
        code.push_back(static_cast<char>(keyCode));
        return true;
    }
    if (keyCode >= 48 && keyCode <= 57) {
        key.assign(1, static_cast<char>(keyCode));
        code = "Digit";
        code.push_back(static_cast<char>(keyCode));
        return true;
    }
    struct NamedKey { int code; const char* key; const char* name; };
    static const NamedKey named[] = {
        {13, "Enter", "Enter"}, {27, "Escape", "Escape"},
        {8, "Backspace", "Backspace"}, {9, "Tab", "Tab"},
        {32, " ", "Space"}, {37, "ArrowLeft", "ArrowLeft"},
        {38, "ArrowUp", "ArrowUp"}, {39, "ArrowRight", "ArrowRight"},
        {40, "ArrowDown", "ArrowDown"}, {16, "Shift", "Shift"},
        {17, "Control", "Control"}, {18, "Alt", "Alt"},
        {46, "Delete", "Delete"}, {36, "Home", "Home"},
        {35, "End", "End"}, {33, "PageUp", "PageUp"},
        {34, "PageDown", "PageDown"},
    };
    for (const NamedKey& namedKey : named) {
        if (namedKey.code != keyCode) continue;
        key = namedKey.key;
        code = namedKey.name;
        return true;
    }
    // The shared input bridge currently supplies virtual-key values only.
    // Unknown keys still dispatch through the normal path with the bounded
    // empty property values rather than failing the Navigator input loop.
    return true;
}

gxos::web::HtmlElementRef* NavigatorScriptHostAdapter::findElement(
    HostInstanceId serial)
{
    if (document_ == nullptr) return nullptr;
    const std::size_t count = std::min(limits_.maxDocumentNodes,
        document_->structuralElements.size());
    for (std::size_t index = 0; index < count; ++index) {
        if (document_->structuralElements[index].serial == serial)
            return &document_->structuralElements[index];
    }
    return nullptr;
}

const gxos::web::HtmlElementRef* NavigatorScriptHostAdapter::findElement(
    HostInstanceId serial) const
{
    if (document_ == nullptr) return nullptr;
    const std::size_t count = std::min(limits_.maxDocumentNodes,
        document_->structuralElements.size());
    for (std::size_t index = 0; index < count; ++index) {
        if (document_->structuralElements[index].serial == serial)
            return &document_->structuralElements[index];
    }
    return nullptr;
}

bool NavigatorScriptHostAdapter::resolveElementAttribute(
    HostInstanceId serial, SourceView name, SourceView& value) const
{
    value = SourceView();
    if (document_ == nullptr || !validAttributeName(name)) return false;
    const gxos::web::HtmlElementRef* element = findElement(serial);
    if (element == nullptr) return false;

    const std::string& tag = element->tagName;
    const auto has = [&](std::uint16_t bit) {
        return hasAttributePresence(*element, bit);
    };
    const auto setValue = [&](const std::string& stored) {
        value = SourceView(stored.data(), stored.size());
    };
    const auto isInputOrButton = [&]() {
        return tag == "input" || tag == "button";
    };
    const bool usesSpecializedProjection =
        (attributeNameEquals(name, "value") && isInputOrButton()) ||
        attributeNameEquals(name, "id") ||
        attributeNameEquals(name, "class") ||
        attributeNameEquals(name, "style");
    gxos::web::HtmlRetainedAttributeView retained;
    if (!usesSpecializedProjection &&
        gxos::web::findRetainedHtmlAttribute(*document_, *element,
            name.data, name.length, retained)) {
        value = SourceView(retained.value, retained.valueLength);
        return true;
    }

    if (attributeNameEquals(name, "id")) {
        if (has(gxos::web::HtmlAttributeIdPresent)) {
            setValue(element->id);
            return true;
        }
    }
    if (attributeNameEquals(name, "class")) {
        if (has(gxos::web::HtmlAttributeClassPresent)) {
            setValue(element->className);
            return true;
        }
    }
    if (attributeNameEquals(name, "style")) {
        if (has(gxos::web::HtmlAttributeStylePresent)) {
            setValue(element->inlineStyle);
            return true;
        }
    }
    if (attributeNameEquals(name, "name")) {
        if (has(gxos::web::HtmlAttributeNamePresent) &&
            (isInputOrButton() || tag == "textarea" || tag == "select")) {
            setValue(element->formControl.name);
            return true;
        }
        if (has(gxos::web::HtmlAttributeNamePresent) && tag == "form") {
            const std::size_t count = std::min(limits_.maxDocumentNodes,
                document_->formContainers.size());
            for (std::size_t index = 0u; index < count; ++index) {
                const gxos::web::FormContainerMetadata& container =
                    document_->formContainers[index];
                if (container.serial != serial || container.tagName != "form")
                    continue;
                setValue(container.name);
                return true;
            }
        }
    }
    if (attributeNameEquals(name, "type")) {
        if (isInputOrButton() &&
            has(gxos::web::HtmlAttributeTypePresent)) {
            setValue(element->formControl.inputType);
            return true;
        }
    }
    if (attributeNameEquals(name, "value")) {
        if (has(gxos::web::HtmlAttributeValuePresent) && tag == "option") {
            setValue(element->formControl.value);
            return true;
        }
        if (has(gxos::web::HtmlAttributeValuePresent) && isInputOrButton()) {
            const gxos::web::FormRuntimeControlState* state =
                formRuntimeState(serial);
            setValue(state == nullptr ? element->formControl.value :
                state->defaultValue);
            return true;
        }
    }
    if (attributeNameEquals(name, "disabled")) {
        const bool supportedTag = tag == "input" || tag == "button" ||
            tag == "textarea" || tag == "select" || tag == "option" ||
            tag == "fieldset";
        if (supportedTag &&
            has(gxos::web::HtmlAttributeDisabledPresent)) {
            value = SourceView("", 0u);
            return true;
        }
    }
    if (attributeNameEquals(name, "checked")) {
        if (tag == "input" &&
            (element->formControl.inputType != "checkbox" &&
                element->formControl.inputType != "radio")) {
            // This branch is deliberately left to the generic store for
            // unsupported input types.
        } else if (tag == "input" &&
            has(gxos::web::HtmlAttributeCheckedPresent)) {
            value = SourceView("", 0u);
            return true;
        }
    }
    if (attributeNameEquals(name, "selected")) {
        if (tag == "option" &&
            has(gxos::web::HtmlAttributeSelectedPresent)) {
            value = SourceView("", 0u);
            return true;
        }
    }
    if (!usesSpecializedProjection &&
        gxos::web::findRetainedHtmlAttribute(*document_, *element,
            name.data, name.length, retained)) {
        value = SourceView(retained.value, retained.valueLength);
        return true;
    }
    return false;
}

bool NavigatorScriptHostAdapter::mutateElementAttribute(
    HostInstanceId serial, SourceView name, SourceView value, bool remove)
{
    if (document_ == nullptr || !validAttributeName(name)) return false;
    gxos::web::HtmlElementRef* element = findElement(serial);
    if (element == nullptr || attributeMutationIsDeferred(*element, name))
        return false;
    if (!remove && (value.length >
            gxos::web::kHtmlMaxRetainedAttributeValueBytes ||
            (value.length != 0u && value.data == nullptr))) return false;

    RetainedAttributeLocation location;
    if (!locateRetainedAttribute(*document_, *element, name, location))
        return false;

    const bool isId = attributeNameEquals(name, "id");
    const bool isClass = attributeNameEquals(name, "class");
    const std::uint16_t projectionBit = isId
        ? gxos::web::HtmlAttributeIdPresent
        : gxos::web::HtmlAttributeClassPresent;
    const bool hasProjection = (isId || isClass) &&
        hasAttributePresence(*element, projectionBit);

    // The parser can keep its specialized id/class projection even when the
    // generic record is omitted by an earlier parser cap. Preserve that
    // existing logical attribute without manufacturing a seventeenth record.
    if (!remove && !location.found && hasProjection) {
        const std::string projection(value.data == nullptr ? "" : value.data,
            value.length);
        updateElementCopies(*document_, serial, isId, projection, true);
        return true;
    }
    if (remove && !location.found && !hasProjection) return true;

    std::string projection;
    if (!remove && (isId || isClass))
        projection.assign(value.data == nullptr ? "" : value.data,
            value.length);

    std::vector<std::uint8_t>& storage =
        document_->retainedAttributeStorage;
    if (remove) {
        if (location.found) {
            const std::size_t oldEnd = location.offset + location.recordSize;
            storage.erase(storage.begin() + static_cast<std::ptrdiff_t>(
                location.offset), storage.begin() + static_cast<std::ptrdiff_t>(
                    oldEnd));
            shiftLaterElementAttributeOffsets(*document_, *element, oldEnd,
                -static_cast<int>(location.recordSize));
            if (element->retainedAttributeCount != 0u)
                --element->retainedAttributeCount;
            if (document_->retainedAttributeRecordCount != 0u)
                --document_->retainedAttributeRecordCount;
            if (element->retainedAttributeCount == 0u)
                element->retainedAttributeOffset = 0u;
        }
        if (isId || isClass)
            updateElementCopies(*document_, serial, isId, std::string(), false);
        return true;
    }

    const std::size_t newRecordSize = 3u + name.length + value.length;
    std::size_t finalStorageSize = storage.size();
    if (location.found) {
        if (location.recordSize > finalStorageSize) return false;
        finalStorageSize -= location.recordSize;
    } else {
        if (element->retainedAttributeCount >=
                gxos::web::kHtmlMaxRetainedAttributesPerElement ||
            document_->retainedAttributeRecordCount >=
                gxos::web::kHtmlMaxRetainedAttributeRecordsPerDocument)
            return false;
    }
    if (finalStorageSize >
            gxos::web::kHtmlMaxRetainedAttributeStorageBytesPerDocument ||
        newRecordSize >
            gxos::web::kHtmlMaxRetainedAttributeStorageBytesPerDocument -
                finalStorageSize)
        return false;
    finalStorageSize += newRecordSize;

    // Parser-retained documents reserve the complete fixed arena on their
    // first record. Empty documents acquire that reservation before any
    // logical state changes; subsequent byte shifts stay within this cap.
    if (storage.capacity() < finalStorageSize)
        storage.reserve(
            gxos::web::kHtmlMaxRetainedAttributeStorageBytesPerDocument);

    if (location.found) {
        const std::size_t oldEnd = location.offset + location.recordSize;
        const int delta = static_cast<int>(newRecordSize) -
            static_cast<int>(location.recordSize);
        if (delta > 0)
            storage.insert(storage.begin() + static_cast<std::ptrdiff_t>(oldEnd),
                static_cast<std::size_t>(delta), 0u);
        writeRetainedAttributeRecord(storage, location.offset, name, value);
        if (delta < 0) {
            storage.erase(storage.begin() + static_cast<std::ptrdiff_t>(
                    location.offset + newRecordSize),
                storage.begin() + static_cast<std::ptrdiff_t>(oldEnd));
        }
        shiftLaterElementAttributeOffsets(*document_, *element, oldEnd, delta);
    } else {
        const std::size_t insertionOffset = element->retainedAttributeCount == 0u
            ? storage.size() : location.spanEnd;
        std::array<std::uint8_t,
            3u + gxos::web::kHtmlMaxRetainedAttributeNameBytes +
                gxos::web::kHtmlMaxRetainedAttributeValueBytes> record{};
        record[0] = static_cast<std::uint8_t>(name.length);
        record[1] = static_cast<std::uint8_t>(value.length & 0xFFu);
        record[2] = static_cast<std::uint8_t>((value.length >> 8u) & 0xFFu);
        for (std::size_t index = 0u; index < name.length; ++index)
            record[3u + index] = lowerAscii(
                static_cast<unsigned char>(name.data[index]));
        for (std::size_t index = 0u; index < value.length; ++index)
            record[3u + name.length + index] =
                static_cast<std::uint8_t>(value.data[index]);
        storage.insert(storage.begin() + static_cast<std::ptrdiff_t>(
                insertionOffset), record.begin(),
            record.begin() + static_cast<std::ptrdiff_t>(newRecordSize));
        shiftLaterElementAttributeOffsets(*document_, *element,
            insertionOffset, static_cast<int>(newRecordSize));
        if (element->retainedAttributeCount == 0u)
            element->retainedAttributeOffset =
                static_cast<std::uint16_t>(insertionOffset);
        ++element->retainedAttributeCount;
        ++document_->retainedAttributeRecordCount;
    }

    if (isId || isClass)
        updateElementCopies(*document_, serial, isId, projection, true);
    return true;
}

bool NavigatorScriptHostAdapter::isKnownElementSerial(
    HostInstanceId serial) const
{
    return findElement(serial) != nullptr;
}

bool NavigatorScriptHostAdapter::resolveStructuralParentSerial(
    HostInstanceId serial, HostInstanceId& parentSerial) const
{
    parentSerial = 0u;
    const gxos::web::HtmlElementRef* element = findElement(serial);
    if (element == nullptr || element->serial == 0u) return false;
    if (element->parentSerial == element->serial) return false;
    if (element->parentSerial != 0u &&
        findElement(element->parentSerial) == nullptr) return false;
    parentSerial = element->parentSerial;
    return true;
}

bool NavigatorScriptHostAdapter::elementChildAt(
    HostInstanceId parentSerial, std::size_t index,
    HostInstanceId& childSerial) const
{
    childSerial = 0u;
    if (document_ == nullptr || parentSerial == 0u ||
        findElement(parentSerial) == nullptr) return false;
    const std::size_t count = std::min(limits_.maxDocumentNodes,
        document_->structuralElements.size());
    std::size_t matched = 0u;
    for (std::size_t position = 0u; position < count; ++position) {
        const gxos::web::HtmlElementRef& candidate =
            document_->structuralElements[position];
        if (candidate.serial == 0u || candidate.parentSerial != parentSerial)
            continue;
        if (matched == index) {
            childSerial = candidate.serial;
            return true;
        }
        ++matched;
    }
    return false;
}

std::size_t NavigatorScriptHostAdapter::elementChildCount(
    HostInstanceId parentSerial) const
{
    if (document_ == nullptr || parentSerial == 0u ||
        findElement(parentSerial) == nullptr) return 0u;
    const std::size_t count = std::min(limits_.maxDocumentNodes,
        document_->structuralElements.size());
    std::size_t children = 0u;
    for (std::size_t position = 0u; position < count; ++position) {
        const gxos::web::HtmlElementRef& candidate =
            document_->structuralElements[position];
        if (candidate.serial != 0u && candidate.parentSerial == parentSerial)
            ++children;
    }
    return children;
}

bool NavigatorScriptHostAdapter::elementSiblingAt(
    HostInstanceId serial, bool next, HostInstanceId& siblingSerial) const
{
    siblingSerial = 0u;
    HostInstanceId parentSerial = 0u;
    if (!resolveStructuralParentSerial(serial, parentSerial)) return false;
    if (document_ == nullptr) return false;
    const std::size_t count = std::min(limits_.maxDocumentNodes,
        document_->structuralElements.size());
    std::size_t receiverPosition = count;
    for (std::size_t position = 0u; position < count; ++position) {
        if (document_->structuralElements[position].serial == serial) {
            receiverPosition = position;
            break;
        }
    }
    if (receiverPosition >= count) return false;

    if (next) {
        for (std::size_t position = receiverPosition + 1u;
             position < count; ++position) {
            const gxos::web::HtmlElementRef& candidate =
                document_->structuralElements[position];
            if (candidate.serial != 0u && candidate.serial != serial &&
                candidate.parentSerial == parentSerial) {
                siblingSerial = candidate.serial;
                return true;
            }
        }
    } else {
        std::size_t position = receiverPosition;
        while (position > 0u) {
            --position;
            const gxos::web::HtmlElementRef& candidate =
                document_->structuralElements[position];
            if (candidate.serial != 0u && candidate.serial != serial &&
                candidate.parentSerial == parentSerial) {
                siblingSerial = candidate.serial;
                return true;
            }
        }
    }
    return false;
}

HostInstanceId NavigatorScriptHostAdapter::activeElementSerial() const
{
    if (document_ == nullptr) return 0;
    const gxos::web::FormRuntimeStateTable& runtime =
        document_->formRuntimeState;
    if (!runtime.initialized || !runtime.focusValid ||
        runtime.documentGeneration == 0 ||
        runtime.focusedDocumentGeneration != runtime.documentGeneration ||
        runtime.focusedLogicalSerial == 0) return 0;

    // The focused serial is authoritative, but the projection still validates
    // its current document membership and bounded form metadata before
    // creating the ordinary Element host value. A stale or malformed handle
    // therefore fails closed without creating a second focus state.
    if (findElement(runtime.focusedLogicalSerial) == nullptr) return 0;
    bool hasFocusableControl = false;
    if (document_ != nullptr) {
        for (const gxos::web::DocBlock& block : document_->blocks) {
            if (block.formControl.logicalSerial != runtime.focusedLogicalSerial ||
                !block.formControl.metadataComplete ||
                !block.formControl.supported || block.formUnsupported ||
                block.formControl.hidden || block.formControl.disabled) continue;
            if (block.type == gxos::web::BlockType::FormTextInput ||
                block.type == gxos::web::BlockType::FormTextarea ||
                block.type == gxos::web::BlockType::FormCheckbox ||
                block.type == gxos::web::BlockType::FormRadio ||
                block.type == gxos::web::BlockType::FormSelect ||
                block.type == gxos::web::BlockType::FormSubmit) {
                hasFocusableControl = true;
                break;
            }
        }
    }
    if (!hasFocusableControl) return 0;
    return runtime.focusedLogicalSerial;
}

bool NavigatorScriptHostAdapter::isDescendantOrSelf(
    std::uint64_t serial, std::uint64_t ancestorSerial) const
{
    if (document_ == nullptr || serial == 0u || ancestorSerial == 0u ||
        findElement(serial) == nullptr || findElement(ancestorSerial) == nullptr)
        return false;
    if (serial == ancestorSerial) return true;

    // The structural node capacity is the maximum number of parent links
    // examined. resolveStructuralParentSerial rejects missing and self-parent
    // metadata; a longer cycle is bounded by this same document limit.
    const std::size_t depthLimit = std::min(limits_.maxDocumentNodes,
        document_->structuralElements.size());
    HostInstanceId currentSerial = serial;
    for (std::size_t depth = 0u; depth < depthLimit; ++depth) {
        HostInstanceId parentSerial = 0u;
        if (!resolveStructuralParentSerial(currentSerial, parentSerial) ||
            parentSerial == 0u) return false;
        if (parentSerial == ancestorSerial) return true;
        currentSerial = parentSerial;
    }
    return false;
}

const NavigatorScriptHostAdapter::SelectorCollectionRecord*
NavigatorScriptHostAdapter::selectorCollectionFor(HostInstanceId token) const
{
    if (token == 0u) return nullptr;
    const std::size_t index = static_cast<std::size_t>(token - 1u);
    const std::size_t limit = std::min(limits_.maxSelectorCollections,
        selectorCollections_.size());
    if (index >= limit || !selectorCollections_[index].active) return nullptr;
    return &selectorCollections_[index];
}

NavigatorScriptHostAdapter::SelectorCollectionRecord*
NavigatorScriptHostAdapter::selectorCollectionFor(HostInstanceId token)
{
    if (token == 0u) return nullptr;
    const std::size_t index = static_cast<std::size_t>(token - 1u);
    const std::size_t limit = std::min(limits_.maxSelectorCollections,
        selectorCollections_.size());
    if (index >= limit || !selectorCollections_[index].active) return nullptr;
    return &selectorCollections_[index];
}

bool NavigatorScriptHostAdapter::selectorDescriptorEquals(
    const NavigatorScriptSelectorDescriptor& left,
    const NavigatorScriptSelectorDescriptor& right) const
{
    if (left.memberCount != right.memberCount ||
        left.memberCount > kNavigatorScriptMaxSelectorListMembers)
        return false;
    const auto simpleEqual = [](const NavigatorScriptSelectorDescriptor& left,
        const NavigatorScriptSimpleSelectorDescriptor& leftSimple,
        const NavigatorScriptSelectorDescriptor& right,
        const NavigatorScriptSimpleSelectorDescriptor& rightSimple) {
        if (leftSimple.valid != rightSimple.valid ||
            leftSimple.universal != rightSimple.universal ||
            leftSimple.tagLength != rightSimple.tagLength ||
            leftSimple.idLength != rightSimple.idLength ||
            leftSimple.classTokenCount != rightSimple.classTokenCount ||
            leftSimple.hasAttributePredicate !=
                rightSimple.hasAttributePredicate ||
            leftSimple.statePseudo != rightSimple.statePseudo ||
            leftSimple.nthA != rightSimple.nthA ||
            leftSimple.nthB != rightSimple.nthB ||
            leftSimple.attributeValuePresent !=
                rightSimple.attributeValuePresent ||
            leftSimple.classTokenCount > kNavigatorScriptMaxClassQueryTokens ||
            !selectorTextEquals(selectorPart(left, leftSimple.tagOffset,
                leftSimple.tagLength),
                selectorPart(right, rightSimple.tagOffset,
                    rightSimple.tagLength)) ||
            !selectorTextEquals(selectorPart(left, leftSimple.idOffset,
                leftSimple.idLength),
                selectorPart(right, rightSimple.idOffset,
                    rightSimple.idLength))) return false;
        for (std::size_t index = 0; index < leftSimple.classTokenCount;
                ++index) {
            const NavigatorScriptSimpleSelectorDescriptor::ClassTokenRange&
                leftToken = leftSimple.classTokens[index];
            const NavigatorScriptSimpleSelectorDescriptor::ClassTokenRange&
                rightToken = rightSimple.classTokens[index];
            if (leftToken.length != rightToken.length ||
                !selectorTextEquals(selectorPart(left, leftToken.offset,
                    leftToken.length), selectorPart(right, rightToken.offset,
                    rightToken.length))) return false;
        }
        if (leftSimple.hasAttributePredicate &&
            (leftSimple.attributeNameLength !=
                    rightSimple.attributeNameLength ||
                !selectorTextEquals(selectorPart(left,
                    leftSimple.attributeNameOffset,
                    leftSimple.attributeNameLength),
                    selectorPart(right, rightSimple.attributeNameOffset,
                        rightSimple.attributeNameLength)))) return false;
        if (leftSimple.attributeValuePresent &&
            (leftSimple.attributeValueLength !=
                    rightSimple.attributeValueLength ||
                !selectorTextEquals(selectorPart(left,
                    leftSimple.attributeValueOffset,
                    leftSimple.attributeValueLength),
                    selectorPart(right, rightSimple.attributeValueOffset,
                        rightSimple.attributeValueLength)))) return false;
        return true;
    };
    for (std::size_t index = 0u; index < left.memberCount; ++index) {
        const NavigatorScriptSelectorMemberDescriptor& leftMember =
            left.members[index];
        const NavigatorScriptSelectorMemberDescriptor& rightMember =
            right.members[index];
        if (leftMember.relation != rightMember.relation ||
            !simpleEqual(left, leftMember.leftSimple, right,
                rightMember.leftSimple) ||
            !simpleEqual(left, leftMember.rightSimple, right,
                rightMember.rightSimple)) return false;
    }
    return true;
}

bool NavigatorScriptHostAdapter::selectorSimpleElementMatches(
    const gxos::web::HtmlElementRef& element,
    const NavigatorScriptSimpleSelectorDescriptor& selector,
    const NavigatorScriptSelectorDescriptor& storage) const
{
    if (!selector.valid || element.serial == 0u || element.tagName.empty())
        return false;
    if (document_ == nullptr || findElement(element.serial) != &element)
        return false;

    const SourceView tag = selectorPart(storage, selector.tagOffset,
        selector.tagLength);
    const SourceView id = selectorPart(storage, selector.idOffset,
        selector.idLength);
    if (selector.tagLength != 0u &&
        !selectorTagEquals(element.tagName, tag)) return false;
    if (selector.idLength != 0u &&
        !selectorTextEquals(SourceView(element.id.data(), element.id.size()),
            id)) return false;
    if (selector.classTokenCount != 0u &&
        !classTokenSetMatches(element.className, selector, storage))
        return false;
    if (selector.hasAttributePredicate) {
        if (selector.attributeNameLength == 0u ||
            selector.attributeNameLength >
                kNavigatorScriptMaxSelectorAttributeNameLength ||
            (selector.attributeValuePresent &&
                selector.attributeValueLength >
                    kNavigatorScriptMaxSelectorAttributeValueLength))
            return false;
        SourceView retainedValue;
        if (!resolveElementAttribute(element.serial,
                selectorPart(storage, selector.attributeNameOffset,
                    selector.attributeNameLength), retainedValue)) return false;
        if (selector.attributeValuePresent &&
            !selectorTextEquals(retainedValue,
                selectorPart(storage, selector.attributeValueOffset,
                    selector.attributeValueLength))) return false;
    }
    if (selector.statePseudo != NavigatorScriptStatePseudo::None &&
        !selectorStatePseudoMatches(element, selector))
        return false;
    return selector.tagLength != 0u || selector.idLength != 0u ||
        selector.classTokenCount != 0u || selector.hasAttributePredicate ||
        selector.universal ||
        selector.statePseudo != NavigatorScriptStatePseudo::None;
}

bool NavigatorScriptHostAdapter::selectorStatePseudoMatches(
    const gxos::web::HtmlElementRef& element,
    const NavigatorScriptSimpleSelectorDescriptor& selector) const
{
    const NavigatorScriptStatePseudo pseudo = selector.statePseudo;
    if (document_ == nullptr || element.serial == 0u ||
        findElement(element.serial) != &element) return false;

    switch (pseudo) {
    case NavigatorScriptStatePseudo::None:
        return true;
    case NavigatorScriptStatePseudo::Checked: {
        if (element.tagName == "input" &&
            isCheckableFormElement(element.serial)) {
            const gxos::web::FormRuntimeControlState* state =
                formRuntimeState(element.serial);
            return state != nullptr && state->checked;
        }
        if (element.tagName == "option") {
            HostInstanceId selectSerial = 0u;
            std::size_t optionIndex = 0u;
            if (!optionIndexFor(element.serial, selectSerial, optionIndex))
                return false;
            const gxos::web::DocBlock* select =
                formControlBlock(selectSerial);
            return select != nullptr && select->selectedOption >= 0 &&
                static_cast<std::size_t>(select->selectedOption) == optionIndex;
        }
        return false;
    }
    case NavigatorScriptStatePseudo::Disabled: {
        const gxos::web::FormControlMetadata& metadata = element.formControl;
        if (element.tagName == "option") {
            HostInstanceId selectSerial = 0u;
            std::size_t optionIndex = 0u;
            if (!optionIndexFor(element.serial, selectSerial, optionIndex))
                return false;
            const gxos::web::DocBlock* select =
                formControlBlock(selectSerial);
            return select != nullptr && optionIndex < select->options.size() &&
                select->options[optionIndex].disabled;
        }
        if (!metadata.metadataComplete) return false;
        if (element.tagName == "fieldset") return metadata.disabled;
        const bool supportedType = element.tagName == "input" ||
            element.tagName == "button" || element.tagName == "textarea" ||
            element.tagName == "select";
        if (!supportedType || !metadata.supported) return false;
        const gxos::web::FormRuntimeControlState* state =
            formRuntimeState(element.serial);
        return state != nullptr && state->disabled;
    }
    case NavigatorScriptStatePseudo::Focus:
        // activeElementSerial() is the adapter's canonical projection of the
        // generation-checked focused serial and supported focused control.
        return activeElementSerial() == element.serial;
    case NavigatorScriptStatePseudo::Root:
        // WebDocument owns the parser-designated document Element. Its serial
        // is local to this document generation; a parentless Element is not
        // sufficient evidence that it is the document root.
        return document_->hasDocumentElement &&
            document_->documentElement.serial != 0u &&
            element.serial == document_->documentElement.serial;
    case NavigatorScriptStatePseudo::FirstChild: {
        HostInstanceId parentSerial = 0u;
        HostInstanceId previousSerial = 0u;
        HostInstanceId firstChildSerial = 0u;
        return resolveStructuralParentSerial(element.serial, parentSerial) &&
            parentSerial != 0u &&
            elementChildAt(parentSerial, 0u, firstChildSerial) &&
            firstChildSerial == element.serial &&
            !elementSiblingAt(element.serial, false, previousSerial) &&
            previousSerial == 0u;
    }
    case NavigatorScriptStatePseudo::LastChild: {
        HostInstanceId parentSerial = 0u;
        HostInstanceId nextSerial = 0u;
        HostInstanceId lastChildSerial = 0u;
        if (!resolveStructuralParentSerial(element.serial, parentSerial) ||
            parentSerial == 0u) return false;
        const std::size_t childCount = elementChildCount(parentSerial);
        return childCount != 0u &&
            elementChildAt(parentSerial, childCount - 1u, lastChildSerial) &&
            lastChildSerial == element.serial &&
            !elementSiblingAt(element.serial, true, nextSerial) &&
            nextSerial == 0u;
    }
    case NavigatorScriptStatePseudo::OnlyChild: {
        HostInstanceId parentSerial = 0u;
        HostInstanceId previousSerial = 0u;
        HostInstanceId nextSerial = 0u;
        HostInstanceId firstChildSerial = 0u;
        HostInstanceId lastChildSerial = 0u;
        if (!resolveStructuralParentSerial(element.serial, parentSerial) ||
            parentSerial == 0u || elementChildCount(parentSerial) != 1u)
            return false;
        return elementChildAt(parentSerial, 0u, firstChildSerial) &&
            firstChildSerial == element.serial &&
            elementChildAt(parentSerial, 0u, lastChildSerial) &&
            lastChildSerial == element.serial &&
            !elementSiblingAt(element.serial, false, previousSerial) &&
            previousSerial == 0u &&
            !elementSiblingAt(element.serial, true, nextSerial) &&
            nextSerial == 0u;
    }
    case NavigatorScriptStatePseudo::FirstOfType:
    case NavigatorScriptStatePseudo::LastOfType:
    case NavigatorScriptStatePseudo::OnlyOfType: {
        HostInstanceId parentSerial = 0u;
        if (!resolveStructuralParentSerial(element.serial, parentSerial) ||
            parentSerial == 0u) return false;

        const std::size_t count = std::min(limits_.maxDocumentNodes,
            document_->structuralElements.size());
        const gxos::web::HtmlElementRef* parent = findElement(parentSerial);
        if (parent == nullptr) return false;
        std::size_t currentPosition = count;
        std::size_t siblingCount = 0u;
        HostInstanceId previousSiblingSerial = 0u;
        for (std::size_t position = 0u; position < count; ++position) {
            const gxos::web::HtmlElementRef& candidate =
                document_->structuralElements[position];
            if (candidate.parentSerial != parentSerial) continue;
            if (candidate.serial == 0u ||
                siblingCount >= std::numeric_limits<std::uint16_t>::max())
                return false;
            ++siblingCount;
            if (candidate.childIndex != siblingCount ||
                candidate.siblingCount != element.siblingCount ||
                candidate.previousSiblingSerial != previousSiblingSerial)
                return false;
            if (&candidate == &element) currentPosition = position;
            previousSiblingSerial = candidate.serial;
        }
        if (currentPosition >= count || siblingCount != element.siblingCount ||
            parent->childCount != siblingCount) return false;

        const SourceView currentTag(element.tagName.data(),
            element.tagName.size());
        bool hasEarlierSameType = false;
        bool hasLaterSameType = false;
        for (std::size_t position = 0u; position < count; ++position) {
            if (position == currentPosition) continue;
            const gxos::web::HtmlElementRef& candidate =
                document_->structuralElements[position];
            if (candidate.serial == 0u ||
                candidate.parentSerial != parentSerial ||
                !selectorTagEquals(candidate.tagName, currentTag)) continue;
            if (position < currentPosition) hasEarlierSameType = true;
            else hasLaterSameType = true;
        }

        switch (pseudo) {
        case NavigatorScriptStatePseudo::FirstOfType:
            return !hasEarlierSameType;
        case NavigatorScriptStatePseudo::LastOfType:
            return !hasLaterSameType;
        case NavigatorScriptStatePseudo::OnlyOfType:
            return !hasEarlierSameType && !hasLaterSameType;
        default:
            return false;
        }
    }
    case NavigatorScriptStatePseudo::NthChild:
    case NavigatorScriptStatePseudo::NthLastChild:
    case NavigatorScriptStatePseudo::NthOfType:
    case NavigatorScriptStatePseudo::NthLastOfType: {
        std::size_t index = 0u;
        const bool sameType = pseudo == NavigatorScriptStatePseudo::NthOfType ||
            pseudo == NavigatorScriptStatePseudo::NthLastOfType;
        const bool fromEnd = pseudo ==
                NavigatorScriptStatePseudo::NthLastChild ||
            pseudo == NavigatorScriptStatePseudo::NthLastOfType;
        if (!resolveStructuralIndex(element, sameType, fromEnd, index))
            return false;

        const std::int32_t a = selector.nthA;
        const std::int32_t b = selector.nthB;
        if (index == 0u) return false;
        if (a == 0) return static_cast<std::int32_t>(index) == b;
        if (a > 0) {
            const std::int32_t delta = static_cast<std::int32_t>(index) - b;
            return delta >= 0 && delta % a == 0;
        }
        const std::int32_t delta = b - static_cast<std::int32_t>(index);
        const std::int32_t step = -a;
        return delta >= 0 && delta % step == 0;
    }
    }
    return false;
}

bool NavigatorScriptHostAdapter::resolveStructuralIndex(
    const gxos::web::HtmlElementRef& element, bool sameType, bool fromEnd,
    std::size_t& index) const
{
    index = 0u;
    HostInstanceId parentSerial = 0u;
    if (document_ == nullptr || element.serial == 0u ||
        element.tagName.empty() || findElement(element.serial) != &element ||
        !resolveStructuralParentSerial(element.serial, parentSerial) ||
        parentSerial == 0u) return false;

    const gxos::web::HtmlElementRef* parent = findElement(parentSerial);
    if (parent == nullptr) return false;
    const std::size_t bound = std::min(limits_.maxDocumentNodes,
        document_->structuralElements.size());
    const SourceView candidateTag(element.tagName.data(),
        element.tagName.size());
    std::size_t childCount = 0u;
    std::size_t forwardChildIndex = 0u;
    std::size_t sameTypeCount = 0u;
    std::size_t forwardTypeIndex = 0u;
    std::size_t sameTypeAfter = 0u;
    bool candidateFound = false;
    HostInstanceId previousSiblingSerial = 0u;

    for (std::size_t position = 0u; position < bound; ++position) {
        const gxos::web::HtmlElementRef& sibling =
            document_->structuralElements[position];
        if (sibling.parentSerial != parentSerial) continue;
        if (sibling.serial == 0u ||
            childCount >= std::numeric_limits<std::uint16_t>::max())
            return false;
        ++childCount;
        if (sibling.childIndex != childCount ||
            sibling.siblingCount != element.siblingCount ||
            sibling.previousSiblingSerial != previousSiblingSerial)
            return false;

        const bool isCandidate = &sibling == &element;
        if (isCandidate) {
            if (candidateFound || sibling.serial != element.serial) return false;
            candidateFound = true;
            forwardChildIndex = childCount;
        }
        if (!sameType || selectorTagEquals(sibling.tagName, candidateTag)) {
            ++sameTypeCount;
            if (isCandidate) forwardTypeIndex = sameTypeCount;
            else if (candidateFound) ++sameTypeAfter;
        }
        previousSiblingSerial = sibling.serial;
    }

    if (!candidateFound || childCount == 0u ||
        parent->childCount != childCount ||
        element.siblingCount != childCount ||
        element.childIndex != forwardChildIndex) return false;

    if (!sameType) {
        index = fromEnd ? childCount - forwardChildIndex + 1u :
            forwardChildIndex;
    } else {
        if (forwardTypeIndex == 0u || sameTypeCount < forwardTypeIndex)
            return false;
        index = fromEnd ? sameTypeAfter + 1u : forwardTypeIndex;
    }
    return index != 0u && index <= bound;
}

bool NavigatorScriptHostAdapter::selectorMemberElementMatches(
    const gxos::web::HtmlElementRef& element,
    const NavigatorScriptSelectorMemberDescriptor& selector,
    const NavigatorScriptSelectorDescriptor& storage) const
{
    if (document_ == nullptr || !selector.rightSimple.valid ||
        !selectorSimpleElementMatches(element, selector.rightSimple, storage))
        return false;
    if (selector.relation == NavigatorScriptSelectorRelation::None)
        return true;

    HostInstanceId parentSerial = 0u;
    if (!resolveStructuralParentSerial(element.serial, parentSerial) ||
        parentSerial == 0u) return false;
    if (selector.relation == NavigatorScriptSelectorRelation::Child) {
        const gxos::web::HtmlElementRef* parent = findElement(parentSerial);
        return parent != nullptr && selectorSimpleElementMatches(*parent,
            selector.leftSimple, storage);
    }

    if (selector.relation == NavigatorScriptSelectorRelation::AdjacentSibling) {
        HostInstanceId previousSerial = 0u;
        if (!elementSiblingAt(element.serial, false, previousSerial) ||
            previousSerial == 0u || previousSerial == element.serial)
            return false;
        const gxos::web::HtmlElementRef* previous = findElement(previousSerial);
        return previous != nullptr && selectorSimpleElementMatches(*previous,
            selector.leftSimple, storage);
    }

    if (selector.relation == NavigatorScriptSelectorRelation::GeneralSibling) {
        const std::size_t siblingLimit = std::min(limits_.maxDocumentNodes,
            document_->structuralElements.size());
        HostInstanceId currentSerial = element.serial;
        for (std::size_t step = 0u; step < siblingLimit; ++step) {
            HostInstanceId previousSerial = 0u;
            if (!elementSiblingAt(currentSerial, false, previousSerial) ||
                previousSerial == 0u || previousSerial == currentSerial ||
                previousSerial == element.serial)
                return false;
            const gxos::web::HtmlElementRef* previous =
                findElement(previousSerial);
            if (previous == nullptr) return false;
            if (selectorSimpleElementMatches(*previous, selector.leftSimple,
                    storage)) return true;
            currentSerial = previousSerial;
        }
        return false;
    }

    // Match the left side against the real structural ancestry. This uses the
    // same parentSerial resolver and bounded depth as closest(), so malformed
    // links and cycles fail closed without recursive selector evaluation.
    const std::size_t depthLimit = std::min(limits_.maxDocumentNodes,
        document_->structuralElements.size());
    HostInstanceId ancestorSerial = parentSerial;
    for (std::size_t depth = 0u; depth < depthLimit && ancestorSerial != 0u;
            ++depth) {
        const gxos::web::HtmlElementRef* ancestor = findElement(ancestorSerial);
        if (ancestor == nullptr) return false;
        if (selectorSimpleElementMatches(*ancestor, selector.leftSimple,
                storage)) return true;
        HostInstanceId nextParentSerial = 0u;
        if (!resolveStructuralParentSerial(ancestorSerial,
                nextParentSerial)) break;
        ancestorSerial = nextParentSerial;
    }
    return false;
}

bool NavigatorScriptHostAdapter::selectorElementMatches(
    const gxos::web::HtmlElementRef& element,
    const NavigatorScriptSelectorDescriptor& selector) const
{
    if (selector.memberCount == 0u ||
        selector.memberCount > kNavigatorScriptMaxSelectorListMembers)
        return false;
    for (std::size_t index = 0u; index < selector.memberCount; ++index) {
        if (selectorMemberElementMatches(element, selector.members[index],
                selector)) return true;
    }
    return false;
}

bool NavigatorScriptHostAdapter::selectorClosestMatch(
    HostInstanceId receiverSerial,
    const NavigatorScriptSelectorDescriptor& selector,
    HostInstanceId& matchSerial) const
{
    matchSerial = 0u;
    if (document_ == nullptr || receiverSerial == 0u ||
        selector.memberCount == 0u ||
        selector.memberCount > kNavigatorScriptMaxSelectorListMembers)
        return false;

    // The structural document-node capacity is also the maximum ancestry
    // walk. A malformed parent cycle therefore terminates without recursion
    // or unbounded native work.
    const std::size_t depthLimit = std::min(limits_.maxDocumentNodes,
        document_->structuralElements.size());
    HostInstanceId candidateSerial = receiverSerial;
    for (std::size_t depth = 0; depth < depthLimit && candidateSerial != 0u;
            ++depth) {
        const gxos::web::HtmlElementRef* candidate =
            findElement(candidateSerial);
        if (candidate == nullptr) return false;
        if (selectorElementMatches(*candidate, selector)) {
            matchSerial = candidate->serial;
            return true;
        }
        HostInstanceId parentSerial = 0u;
        if (!resolveStructuralParentSerial(candidateSerial, parentSerial)) break;
        candidateSerial = parentSerial;
    }
    return false;
}

bool NavigatorScriptHostAdapter::selectorScopeMatches(
    const gxos::web::HtmlElementRef& element, HostInstanceId scopeSerial) const
{
    if (scopeSerial == 0u) return true;
    if (element.serial == scopeSerial) return false;
    return isDescendantOrSelf(element.serial, scopeSerial);
}

std::size_t NavigatorScriptHostAdapter::selectorMatchCount(
    const SelectorCollectionRecord& record) const
{
    if (document_ == nullptr || record.selector.memberCount == 0u ||
        record.selector.memberCount > kNavigatorScriptMaxSelectorListMembers)
        return 0u;
    const std::size_t count = std::min(limits_.maxDocumentNodes,
        document_->structuralElements.size());
    std::size_t matches = 0u;
    for (std::size_t position = 0; position < count; ++position) {
        const gxos::web::HtmlElementRef& element =
            document_->structuralElements[position];
        if (selectorScopeMatches(element, record.scopeSerial) &&
            selectorElementMatches(element, record.selector)) ++matches;
    }
    return matches;
}

bool NavigatorScriptHostAdapter::selectorMatchAt(
    const SelectorCollectionRecord& record, std::size_t index,
    HostInstanceId& serial) const
{
    serial = 0u;
    if (document_ == nullptr || record.selector.memberCount == 0u ||
        record.selector.memberCount > kNavigatorScriptMaxSelectorListMembers)
        return false;
    const std::size_t count = std::min(limits_.maxDocumentNodes,
        document_->structuralElements.size());
    std::size_t matched = 0u;
    for (std::size_t position = 0; position < count; ++position) {
        const gxos::web::HtmlElementRef& element =
            document_->structuralElements[position];
        if (!selectorScopeMatches(element, record.scopeSerial) ||
            !selectorElementMatches(element, record.selector)) continue;
        if (matched == index) {
            serial = element.serial;
            return true;
        }
        ++matched;
    }
    return false;
}

bool NavigatorScriptHostAdapter::getOrCreateSelectorCollection(
    HostInstanceId scopeSerial,
    const NavigatorScriptSelectorDescriptor& selector, HostInstanceId& token)
{
    token = 0u;
    const std::size_t limit = std::min(limits_.maxSelectorCollections,
        selectorCollections_.size());
    for (std::size_t index = 0; index < limit; ++index) {
        const SelectorCollectionRecord& record = selectorCollections_[index];
        if (record.active && record.scopeSerial == scopeSerial &&
            selectorDescriptorEquals(record.selector, selector)) {
            token = static_cast<HostInstanceId>(index + 1u);
            return true;
        }
    }
    for (std::size_t index = 0; index < limit; ++index) {
        SelectorCollectionRecord& record = selectorCollections_[index];
        if (record.active) continue;
        record.active = true;
        record.scopeSerial = scopeSerial;
        record.selector = selector;
        token = static_cast<HostInstanceId>(index + 1u);
        return true;
    }
    return false;
}

HostResult NavigatorScriptHostAdapter::querySelector(HostInstanceId scopeSerial,
    const HostValue* arguments, std::size_t argumentCount, HostValue& result)
{
    NavigatorScriptSelectorDescriptor selector;
    const bool parsed = arguments != nullptr && argumentCount == 1u &&
        arguments[0].type == HostValueType::String &&
        parseBoundedSelector(arguments[0].stringValue, selector);
    if (!parsed || document_ == nullptr) {
        result = HostValue::nullValue();
        return HostResult();
    }
    if (document_->structuralElements.size() > limits_.maxDocumentNodes)
        return HostResult{HostResultCode::DocumentLookupLimitExceeded};
    const std::size_t count = document_->structuralElements.size();
    for (std::size_t position = 0; position < count; ++position) {
        const gxos::web::HtmlElementRef& element =
            document_->structuralElements[position];
        if (!selectorScopeMatches(element, scopeSerial) ||
            !selectorElementMatches(element, selector)) continue;
        result = HostValue::fromHostObject(HostObjectReference{
            element.serial, generation_, kNavigatorElementHostKind});
        return HostResult();
    }
    result = HostValue::nullValue();
    return HostResult();
}

HostResult NavigatorScriptHostAdapter::querySelectorAll(HostInstanceId scopeSerial,
    const HostValue* arguments, std::size_t argumentCount, HostValue& result)
{
    NavigatorScriptSelectorDescriptor selector;
    if (arguments == nullptr || argumentCount != 1u ||
        arguments[0].type != HostValueType::String ||
        !parseBoundedSelector(arguments[0].stringValue, selector)) {
        selector = NavigatorScriptSelectorDescriptor();
    } else if (document_ != nullptr && document_->structuralElements.size() >
            limits_.maxDocumentNodes) {
        return HostResult{HostResultCode::DocumentLookupLimitExceeded};
    }
    HostInstanceId token = 0u;
    if (!getOrCreateSelectorCollection(scopeSerial, selector, token))
        return HostResult{HostResultCode::DocumentLookupLimitExceeded};
    result = HostValue::fromHostObject(HostObjectReference{
        token, generation_, kNavigatorSelectorCollectionHostKind});
    return HostResult();
}

HostResult NavigatorScriptHostAdapter::emptySelectorCollection(
    HostValue& result)
{
    const NavigatorScriptSelectorDescriptor selector;
    HostInstanceId token = 0u;
    if (!getOrCreateSelectorCollection(0u, selector, token))
        return HostResult{HostResultCode::DocumentLookupLimitExceeded};
    result = HostValue::fromHostObject(HostObjectReference{
        token, generation_, kNavigatorSelectorCollectionHostKind});
    return HostResult();
}

HostResult NavigatorScriptHostAdapter::getElementsBySimpleSelector(
    HostInstanceId scopeSerial, const HostValue* arguments,
    std::size_t argumentCount, bool classSelector, HostValue& result)
{
    NavigatorScriptSelectorDescriptor selector;
    const bool parsed = arguments != nullptr && argumentCount == 1u &&
        arguments[0].type == HostValueType::String &&
        makeRetrievalSelector(arguments[0].stringValue, classSelector, selector);
    if (!parsed) selector = NavigatorScriptSelectorDescriptor();
    else if (document_ != nullptr && document_->structuralElements.size() >
            limits_.maxDocumentNodes) {
        return HostResult{HostResultCode::DocumentLookupLimitExceeded};
    }

    HostInstanceId token = 0u;
    if (!getOrCreateSelectorCollection(scopeSerial, selector, token))
        return HostResult{HostResultCode::DocumentLookupLimitExceeded};
    result = HostValue::fromHostObject(HostObjectReference{
        token, generation_, kNavigatorSelectorCollectionHostKind});
    return HostResult();
}

HostResult NavigatorScriptHostAdapter::validate(
    const HostObjectReference& object)
{
    if (!object.valid()) return HostResult{HostResultCode::InvalidObject};
    if (object.generation != generation_)
        return HostResult{HostResultCode::StaleObject};
    if (document_ == nullptr) return HostResult{HostResultCode::InvalidObject};
    if (object.kind == kNavigatorDocumentHostKind &&
        object.instanceId == kNavigatorDocumentHostInstance) {
        return HostResult();
    }
    if (object.kind == kNavigatorElementHostKind &&
        isKnownElementSerial(object.instanceId)) {
        return HostResult();
    }
    if (object.kind == kNavigatorDocumentFormsCollectionHostKind &&
        object.instanceId == kNavigatorDocumentHostInstance) {
        return HostResult();
    }
    if (object.kind == kNavigatorFormCollectionHostKind &&
        isFormElement(object.instanceId)) {
        return HostResult();
    }
    if (object.kind == kNavigatorOptionsCollectionHostKind &&
        isSelectFormElement(object.instanceId)) {
        return HostResult();
    }
    if (object.kind == kNavigatorElementChildrenCollectionHostKind &&
        isKnownElementSerial(object.instanceId)) {
        return HostResult();
    }
    if (object.kind == kNavigatorSelectorCollectionHostKind &&
        selectorCollectionFor(object.instanceId) != nullptr) {
        return HostResult();
    }
    return HostResult{HostResultCode::InvalidObject};
}

HostResult NavigatorScriptHostAdapter::getProperty(
    const HostObjectReference& object, SourceView property, HostValue& result)
{
    // Keep the pure element predicates fail-closed for an already-held
    // Element handle after a navigation-generation change. The method value
    // itself was obtained in the old realm, so exposing the same bounded
    // method here lets callInternal return false/null without dereferencing
    // stale structural metadata.
    if (object.kind == kNavigatorElementHostKind &&
        object.generation != generation_ &&
        (textEquals(property, "matches") ||
            textEquals(property, "closest") ||
            textEquals(property, "contains") ||
            textEquals(property, "getElementsByTagName") ||
            textEquals(property, "getElementsByClassName") ||
            textEquals(property, "getAttribute") ||
            textEquals(property, "hasAttribute") ||
            textEquals(property, "setAttribute") ||
            textEquals(property, "removeAttribute"))) {
        const std::uint32_t methodId = textEquals(property, "matches")
            ? kNavigatorMatchesMethod : textEquals(property, "closest")
                ? kNavigatorClosestMethod : textEquals(property, "contains")
                    ? kNavigatorContainsMethod
                    : textEquals(property, "getElementsByTagName")
                        ? kNavigatorGetElementsByTagNameMethod
                        : textEquals(property, "getElementsByClassName")
                            ? kNavigatorGetElementsByClassNameMethod
                            : textEquals(property, "getAttribute")
                                ? kNavigatorGetAttributeMethod
                                : textEquals(property, "hasAttribute")
                                    ? kNavigatorHasAttributeMethod
                                    : textEquals(property, "setAttribute")
                                        ? kNavigatorSetAttributeMethod
                                        : kNavigatorRemoveAttributeMethod;
        result = HostValue::method(methodId, true, true);
        return HostResult();
    }
    if (object.kind == kNavigatorElementHostKind &&
        object.generation != generation_ &&
        (textEquals(property, "parentElement") ||
            textEquals(property, "children") ||
            textEquals(property, "childElementCount") ||
            textEquals(property, "firstElementChild") ||
            textEquals(property, "lastElementChild") ||
            textEquals(property, "nextElementSibling") ||
            textEquals(property, "previousElementSibling"))) {
        if (textEquals(property, "childElementCount"))
            result = HostValue::number(0.0);
        else if (textEquals(property, "children"))
            result = HostValue::undefined();
        else
            result = HostValue::nullValue();
        return HostResult();
    }
    const HostResult validation = validate(object);
    if (!validation.succeeded()) return validation;

    if (object.kind == kNavigatorDocumentHostKind) {
        if (textEquals(property, "getElementById")) {
            result = HostValue::method(kNavigatorGetElementByIdMethod, true);
            return HostResult();
        }
        if (textEquals(property, "querySelector")) {
            result = HostValue::method(kNavigatorQuerySelectorMethod, true);
            return HostResult();
        }
        if (textEquals(property, "querySelectorAll")) {
            result = HostValue::method(kNavigatorQuerySelectorAllMethod, true);
            return HostResult();
        }
        if (textEquals(property, "getElementsByTagName")) {
            result = HostValue::method(kNavigatorGetElementsByTagNameMethod,
                true);
            return HostResult();
        }
        if (textEquals(property, "getElementsByClassName")) {
            result = HostValue::method(kNavigatorGetElementsByClassNameMethod,
                true);
            return HostResult();
        }
        if (textEquals(property, "activeElement")) {
            const HostInstanceId serial = activeElementSerial();
            if (serial == 0) {
                result = HostValue::nullValue();
            } else {
                result = HostValue::fromHostObject(HostObjectReference{
                    serial, generation_, kNavigatorElementHostKind});
            }
            return HostResult();
        }
        if (textEquals(property, "hasFocus")) {
            result = HostValue::method(kNavigatorHasFocusMethod, true);
            return HostResult();
        }
        if (textEquals(property, "forms")) {
            result = HostValue::fromHostObject(HostObjectReference{
                kNavigatorDocumentHostInstance, generation_,
                kNavigatorDocumentFormsCollectionHostKind});
            return HostResult();
        }
        if (textEquals(property, "addEventListener")) {
            result = HostValue::method(kNavigatorAddEventListenerMethod, true,
                true);
            return HostResult();
        }
        if (textEquals(property, "removeEventListener")) {
            result = HostValue::method(kNavigatorRemoveEventListenerMethod,
                true, true);
            return HostResult();
        }
        return HostResult{HostResultCode::PropertyNotFound};
    }

    if (object.kind == kNavigatorDocumentFormsCollectionHostKind) {
        if (textEquals(property, "length")) {
            result = HostValue::number(static_cast<double>(
                documentFormCount()));
            return HostResult();
        }
        std::size_t index = 0;
        if (parseCanonicalIndex(property, index)) {
            HostInstanceId formSerial = 0;
            if (!documentFormAt(index, formSerial)) {
                result = HostValue::undefined();
                return HostResult();
            }
            result = HostValue::fromHostObject(HostObjectReference{
                formSerial, generation_, kNavigatorElementHostKind});
            return HostResult();
        }
        HostInstanceId formSerial = 0;
        if (!documentFormNamed(property, formSerial)) {
            result = HostValue::undefined();
            return HostResult();
        }
        result = HostValue::fromHostObject(HostObjectReference{
            formSerial, generation_, kNavigatorElementHostKind});
        return HostResult();
    }

    if (object.kind == kNavigatorFormCollectionHostKind) {
        if (textEquals(property, "length")) {
            result = HostValue::number(static_cast<double>(
                formElementCount(object.instanceId)));
            return HostResult();
        }
        std::size_t index = 0;
        if (parseCanonicalIndex(property, index)) {
            HostInstanceId elementSerial = 0;
            if (!formElementAt(object.instanceId, index, elementSerial)) {
                result = HostValue::undefined();
                return HostResult();
            }
            result = HostValue::fromHostObject(HostObjectReference{
                elementSerial, generation_, kNavigatorElementHostKind});
            return HostResult();
        }
        HostInstanceId elementSerial = 0;
        if (!formElementNamed(object.instanceId, property, elementSerial)) {
            result = HostValue::undefined();
            return HostResult();
        }
        result = HostValue::fromHostObject(HostObjectReference{
            elementSerial, generation_, kNavigatorElementHostKind});
        return HostResult();
    }

    if (object.kind == kNavigatorOptionsCollectionHostKind) {
        const gxos::web::DocBlock* select =
            formControlBlock(object.instanceId);
        if (select == nullptr || select->type != gxos::web::BlockType::FormSelect)
            return HostResult{HostResultCode::InvalidObject};
        if (textEquals(property, "length")) {
            result = HostValue::number(static_cast<double>(
                select->options.size()));
            return HostResult();
        }
        std::size_t index = 0;
        if (!parseCanonicalIndex(property, index))
            return HostResult{HostResultCode::PropertyNotFound};
        HostInstanceId optionSerial = 0;
        if (!selectOptionAt(object.instanceId, index, optionSerial)) {
            result = HostValue::undefined();
            return HostResult();
        }
        result = HostValue::fromHostObject(HostObjectReference{
            optionSerial, generation_, kNavigatorElementHostKind});
        return HostResult();
    }

    if (object.kind == kNavigatorElementChildrenCollectionHostKind) {
        if (textEquals(property, "length")) {
            result = HostValue::number(static_cast<double>(
                elementChildCount(object.instanceId)));
            return HostResult();
        }
        std::size_t index = 0u;
        if (!parseCanonicalIndex(property, index))
            return HostResult{HostResultCode::PropertyNotFound};
        HostInstanceId childSerial = 0u;
        if (!elementChildAt(object.instanceId, index, childSerial)) {
            result = HostValue::undefined();
            return HostResult();
        }
        result = HostValue::fromHostObject(HostObjectReference{
            childSerial, generation_, kNavigatorElementHostKind});
        return HostResult();
    }

    if (object.kind == kNavigatorSelectorCollectionHostKind) {
        const SelectorCollectionRecord* record =
            selectorCollectionFor(object.instanceId);
        if (record == nullptr) return HostResult{HostResultCode::InvalidObject};
        if (textEquals(property, "length")) {
            result = HostValue::number(static_cast<double>(
                selectorMatchCount(*record)));
            return HostResult();
        }
        std::size_t index = 0;
        if (!parseCanonicalIndex(property, index))
            return HostResult{HostResultCode::PropertyNotFound};
        HostInstanceId serial = 0;
        if (!selectorMatchAt(*record, index, serial)) {
            result = HostValue::undefined();
            return HostResult();
        }
        result = HostValue::fromHostObject(HostObjectReference{
            serial, generation_, kNavigatorElementHostKind});
        return HostResult();
    }

    const gxos::web::HtmlElementRef* element = findElement(object.instanceId);
    if (element == nullptr) return HostResult{HostResultCode::InvalidObject};
    if (textEquals(property, "parentElement")) {
        HostInstanceId parentSerial = 0u;
        if (!resolveStructuralParentSerial(element->serial, parentSerial) ||
            parentSerial == 0u) {
            result = HostValue::nullValue();
        } else {
            result = HostValue::fromHostObject(HostObjectReference{
                parentSerial, generation_, kNavigatorElementHostKind});
        }
        return HostResult();
    }
    if (textEquals(property, "children")) {
        result = HostValue::fromHostObject(HostObjectReference{
            element->serial, generation_,
            kNavigatorElementChildrenCollectionHostKind});
        return HostResult();
    }
    if (textEquals(property, "childElementCount")) {
        result = HostValue::number(static_cast<double>(
            elementChildCount(element->serial)));
        return HostResult();
    }
    if (textEquals(property, "firstElementChild") ||
        textEquals(property, "lastElementChild")) {
        const std::size_t childCount = elementChildCount(element->serial);
        HostInstanceId childSerial = 0u;
        const bool hasChild = childCount != 0u && elementChildAt(element->serial,
            textEquals(property, "firstElementChild") ? 0u : childCount - 1u,
            childSerial);
        if (!hasChild) {
            result = HostValue::nullValue();
        } else {
            result = HostValue::fromHostObject(HostObjectReference{
                childSerial, generation_, kNavigatorElementHostKind});
        }
        return HostResult();
    }
    if (textEquals(property, "nextElementSibling") ||
        textEquals(property, "previousElementSibling")) {
        HostInstanceId siblingSerial = 0u;
        const bool hasSibling = elementSiblingAt(element->serial,
            textEquals(property, "nextElementSibling"), siblingSerial);
        if (!hasSibling) {
            result = HostValue::nullValue();
        } else {
            result = HostValue::fromHostObject(HostObjectReference{
                siblingSerial, generation_, kNavigatorElementHostKind});
        }
        return HostResult();
    }
    if (textEquals(property, "elements")) {
        if (!isFormElement(element->serial))
            return HostResult{HostResultCode::PropertyNotFound};
        result = HostValue::fromHostObject(HostObjectReference{
            element->serial, generation_, kNavigatorFormCollectionHostKind});
        return HostResult();
    }
    if (textEquals(property, "querySelector")) {
        result = HostValue::method(kNavigatorQuerySelectorMethod, true, true);
        return HostResult();
    }
    if (textEquals(property, "querySelectorAll")) {
        result = HostValue::method(kNavigatorQuerySelectorAllMethod, true, true);
        return HostResult();
    }
    if (textEquals(property, "getElementsByTagName")) {
        result = HostValue::method(kNavigatorGetElementsByTagNameMethod,
            true, true);
        return HostResult();
    }
    if (textEquals(property, "getElementsByClassName")) {
        result = HostValue::method(kNavigatorGetElementsByClassNameMethod,
            true, true);
        return HostResult();
    }
    if (textEquals(property, "matches")) {
        result = HostValue::method(kNavigatorMatchesMethod, true, true);
        return HostResult();
    }
    if (textEquals(property, "getAttribute")) {
        result = HostValue::method(kNavigatorGetAttributeMethod, true, true);
        return HostResult();
    }
    if (textEquals(property, "hasAttribute")) {
        result = HostValue::method(kNavigatorHasAttributeMethod, true, true);
        return HostResult();
    }
    if (textEquals(property, "setAttribute")) {
        result = HostValue::method(kNavigatorSetAttributeMethod, true, true);
        return HostResult();
    }
    if (textEquals(property, "removeAttribute")) {
        result = HostValue::method(kNavigatorRemoveAttributeMethod, true, true);
        return HostResult();
    }
    if (textEquals(property, "closest")) {
        result = HostValue::method(kNavigatorClosestMethod, true, true);
        return HostResult();
    }
    if (textEquals(property, "contains")) {
        result = HostValue::method(kNavigatorContainsMethod, true, true);
        return HostResult();
    }
    if (textEquals(property, "options")) {
        if (!isSelectFormElement(element->serial))
            return HostResult{HostResultCode::PropertyNotFound};
        result = HostValue::fromHostObject(HostObjectReference{
            element->serial, generation_, kNavigatorOptionsCollectionHostKind});
        return HostResult();
    }
    if (textEquals(property, "length")) {
        if (isFormElement(element->serial)) {
            result = HostValue::number(static_cast<double>(
                formElementCount(element->serial)));
            return HostResult();
        }
        if (isSelectFormElement(element->serial)) {
            const gxos::web::DocBlock* block =
                formControlBlock(element->serial);
            result = HostValue::number(static_cast<double>(
                block == nullptr ? 0u : block->options.size()));
            return HostResult();
        }
        return HostResult{HostResultCode::PropertyNotFound};
    }
    if (textEquals(property, "id")) {
        result = HostValue::string(SourceView(element->id.data(),
            element->id.size()));
        return HostResult();
    }
    if (textEquals(property, "tagName")) {
        returnBuffer_ = canonicalTagName(element->tagName);
        result = HostValue::string(SourceView(returnBuffer_.data(),
            returnBuffer_.size()));
        return HostResult();
    }
    if (textEquals(property, "textContent")) {
        std::string text;
        const HostResult content = textContentForElement(element->serial, text);
        if (!content.succeeded()) return content;
        returnBuffer_ = std::move(text);
        result = HostValue::string(SourceView(returnBuffer_.data(),
            returnBuffer_.size()));
        return HostResult();
    }
    if (textEquals(property, "value")) {
        HostInstanceId selectSerial = 0;
        std::size_t optionIndex = 0;
        if (optionIndexFor(element->serial, selectSerial, optionIndex)) {
            const gxos::web::DocBlock* block = formControlBlock(selectSerial);
            if (block == nullptr || optionIndex >= block->options.size())
                return HostResult{HostResultCode::InvalidObject};
            result = HostValue::string(SourceView(
                block->options[optionIndex].value.data(),
                block->options[optionIndex].value.size()));
            return HostResult();
        }
        const gxos::web::DocBlock* block = formControlBlock(element->serial);
        if (block == nullptr || (!isTextEditableFormElement(element->serial) &&
                !isSelectFormElement(element->serial)))
            return HostResult{HostResultCode::PropertyNotFound};
        result = HostValue::string(SourceView(block->inputValue.data(),
            block->inputValue.size()));
        return HostResult();
    }
    if (textEquals(property, "defaultValue")) {
        if (!isTextEditableFormElement(element->serial))
            return HostResult{HostResultCode::PropertyNotFound};
        const gxos::web::FormRuntimeControlState* state =
            formRuntimeState(element->serial);
        if (state == nullptr) return HostResult{HostResultCode::StaleObject};
        result = HostValue::string(SourceView(state->defaultValue.data(),
            state->defaultValue.size()));
        return HostResult();
    }
    if (textEquals(property, "checked")) {
        if (!isCheckableFormElement(element->serial))
            return HostResult{HostResultCode::PropertyNotFound};
        const gxos::web::FormRuntimeControlState* state =
            formRuntimeState(element->serial);
        if (state == nullptr) return HostResult{HostResultCode::StaleObject};
        result = HostValue::boolean(state->checked);
        return HostResult();
    }
    if (textEquals(property, "defaultChecked")) {
        if (!isCheckableFormElement(element->serial))
            return HostResult{HostResultCode::PropertyNotFound};
        const gxos::web::FormRuntimeControlState* state =
            formRuntimeState(element->serial);
        if (state == nullptr) return HostResult{HostResultCode::StaleObject};
        result = HostValue::boolean(state->defaultChecked);
        return HostResult();
    }
    if (textEquals(property, "selected")) {
        HostInstanceId selectSerial = 0;
        std::size_t optionIndex = 0;
        if (!optionIndexFor(element->serial, selectSerial, optionIndex))
            return HostResult{HostResultCode::PropertyNotFound};
        const gxos::web::DocBlock* block = formControlBlock(selectSerial);
        if (block == nullptr || optionIndex >= block->options.size())
            return HostResult{HostResultCode::InvalidObject};
        result = HostValue::boolean(block->selectedOption ==
            static_cast<int>(optionIndex));
        return HostResult();
    }
    if (textEquals(property, "defaultSelected")) {
        HostInstanceId selectSerial = 0;
        std::size_t optionIndex = 0;
        if (!optionIndexFor(element->serial, selectSerial, optionIndex))
            return HostResult{HostResultCode::PropertyNotFound};
        const gxos::web::FormRuntimeControlState* state =
            formRuntimeState(selectSerial);
        if (state == nullptr)
            return HostResult{HostResultCode::StaleObject};
        result = HostValue::boolean(state->defaultSelectedOption ==
            static_cast<int>(optionIndex));
        return HostResult();
    }
    if (textEquals(property, "selectedIndex")) {
        if (!isSelectFormElement(element->serial))
            return HostResult{HostResultCode::PropertyNotFound};
        const gxos::web::DocBlock* block = formControlBlock(element->serial);
        if (block == nullptr || block->formControl.multiple)
            return HostResult{HostResultCode::PropertyNotFound};
        result = HostValue::number(static_cast<double>(block->selectedOption));
        return HostResult();
    }
    if (textEquals(property, "onclick")) {
        const ClickHandlerRecord* record = clickHandlerFor(element->serial);
        result = record == nullptr ||
            record->onclickFunction == kInvalidRuntimeFunctionId
            ? HostValue::nullValue() : HostValue::function(record->onclickFunction);
        return HostResult();
    }
    if (textEquals(property, "addEventListener")) {
        result = HostValue::method(kNavigatorAddEventListenerMethod, true, true);
        return HostResult();
    }
    if (textEquals(property, "removeEventListener")) {
        result = HostValue::method(kNavigatorRemoveEventListenerMethod, true, true);
        return HostResult();
    }
    if (textEquals(property, "focus")) {
        result = HostValue::method(kNavigatorFocusMethod, true, true);
        return HostResult();
    }
    if (textEquals(property, "blur")) {
        result = HostValue::method(kNavigatorBlurMethod, true, true);
        return HostResult();
    }
    if (textEquals(property, "click")) {
        result = HostValue::method(kNavigatorClickMethod, true, true);
        return HostResult();
    }
    if (textEquals(property, "reset")) {
        if (!isFormElement(element->serial))
            return HostResult{HostResultCode::PropertyNotFound};
        result = HostValue::method(kNavigatorResetMethod, true, true);
        return HostResult();
    }
    return HostResult{HostResultCode::PropertyNotFound};
}

HostResult NavigatorScriptHostAdapter::convertTextValue(
    const HostValue& value, std::string& result) const
{
    switch (value.type) {
    case HostValueType::String:
        if (value.stringValue.data == nullptr && value.stringValue.length != 0)
            return HostResult{HostResultCode::InvalidValue};
        result.assign(value.stringValue.data == nullptr ? "" :
            value.stringValue.data, value.stringValue.length);
        return HostResult();
    case HostValueType::Number: {
        if (std::isnan(value.numberValue)) {
            result = "NaN";
            return HostResult();
        }
        if (std::isinf(value.numberValue)) {
            result = std::signbit(value.numberValue) ? "-Infinity" :
                "Infinity";
            return HostResult();
        }
        if (value.numberValue == 0.0) {
            result = "0";
            return HostResult();
        }
        std::array<char, 128> buffer{};
        const auto conversion = std::to_chars(buffer.data(),
            buffer.data() + buffer.size(), value.numberValue);
        if (conversion.ec != std::errc())
            return HostResult{HostResultCode::InvalidValue};
        result.assign(buffer.data(), conversion.ptr);
        return HostResult();
    }
    case HostValueType::Boolean:
        result = value.booleanValue ? "true" : "false";
        return HostResult();
    case HostValueType::Null:
        result = "null";
        return HostResult();
    case HostValueType::Undefined:
        result = "undefined";
        return HostResult();
    case HostValueType::Object:
    case HostValueType::HostObject:
    case HostValueType::Method:
    case HostValueType::Function:
        return HostResult{HostResultCode::InvalidValue};
    }
    return HostResult{HostResultCode::InvalidValue};
}

HostResult NavigatorScriptHostAdapter::appendBoundedText(
    std::string& target, const std::string& text, std::size_t& operations) const
{
    if (!appendBounded(target, text, operations,
        limits_.maxTextAggregationOperations,
        limits_.maxTextContentAssignment)) {
        return HostResult{HostResultCode::DocumentTextLimitExceeded};
    }
    return HostResult();
}

HostResult NavigatorScriptHostAdapter::textContentForElement(
    std::uint64_t serial, std::string& result) const
{
    if (document_ == nullptr || !isKnownElementSerial(serial))
        return HostResult{HostResultCode::InvalidObject};
    result.clear();
    const gxos::web::DocBlock* directBlock = nullptr;
    std::size_t directBlockCount = 0;
    for (const gxos::web::DocBlock& block : document_->blocks) {
        if (block.elementMetadata.serial != serial) continue;
        directBlock = &block;
        ++directBlockCount;
    }
    if (directBlockCount == 1u && directBlock != nullptr) {
        if (directBlock->text.size() > limits_.maxTextContentAssignment)
            return HostResult{HostResultCode::DocumentTextLimitExceeded};
        result = directBlock->text;
        return HostResult();
    }
    std::size_t operations = 0;
    bool matchedRun = false;
    for (const gxos::web::WebInlineItem& item : document_->inlineItems) {
        if (operations >= limits_.maxTextAggregationOperations)
            return HostResult{HostResultCode::DocumentTextLimitExceeded};
        ++operations;
        if (item.kind != gxos::web::InlineItemKind::TextRun &&
            item.kind != gxos::web::InlineItemKind::ForcedBreak) continue;
        if (!isDescendantOrSelf(item.ownerSerial, serial)) continue;
        matchedRun = true;
        const std::string text = item.kind == gxos::web::InlineItemKind::ForcedBreak
            ? "\n" : item.text;
        if (text.size() > limits_.maxTextContentAssignment ||
            result.size() > limits_.maxTextContentAssignment - text.size()) {
            return HostResult{HostResultCode::DocumentTextLimitExceeded};
        }
        result += text;
    }
    if (matchedRun) return HostResult();

    // A few existing compact blocks (notably table cells and controls) do not
    // emit a text-run item. Keep the host view authoritative for those blocks
    // without pretending that they are a general DOM tree.
    for (const gxos::web::DocBlock& block : document_->blocks) {
        bool matches = block.elementMetadata.serial == serial;
        if (!matches) {
            for (const gxos::web::HtmlElementRef& ancestor : block.ancestors) {
                if (ancestor.serial == serial) {
                    matches = true;
                    break;
                }
            }
        }
        if (!matches) continue;
        const HostResult appended = appendBoundedText(result, block.text,
            operations);
        if (!appended.succeeded()) return appended;
    }
    return HostResult();
}

HostResult NavigatorScriptHostAdapter::setElementTextContent(
    std::uint64_t serial, const std::string& text)
{
    if (document_ == nullptr || !isKnownElementSerial(serial))
        return HostResult{HostResultCode::InvalidObject};
    if (text.size() > limits_.maxTextContentAssignment)
        return HostResult{HostResultCode::DocumentTextLimitExceeded};
    if (document_->scriptMutationCount >= limits_.maxDocumentMutations)
        return HostResult{HostResultCode::DocumentMutationLimitExceeded};

    std::vector<std::size_t> matchingItems;
    matchingItems.reserve(4u);
    std::size_t itemOperations = 0;
    for (std::size_t index = 0; index < document_->inlineItems.size(); ++index) {
        if (itemOperations >= limits_.maxTextAggregationOperations)
            return HostResult{HostResultCode::DocumentTextLimitExceeded};
        ++itemOperations;
        const gxos::web::WebInlineItem& item = document_->inlineItems[index];
        if ((item.kind == gxos::web::InlineItemKind::TextRun ||
                item.kind == gxos::web::InlineItemKind::ForcedBreak) &&
            isDescendantOrSelf(item.ownerSerial, serial)) {
            matchingItems.push_back(index);
        }
    }

    std::vector<std::size_t> matchingBlocks;
    matchingBlocks.reserve(4u);
    for (std::size_t index = 0; index < document_->blocks.size(); ++index) {
        const gxos::web::DocBlock& block = document_->blocks[index];
        bool matches = block.elementMetadata.serial != 0 &&
            isDescendantOrSelf(block.elementMetadata.serial, serial);
        if (!matches) {
            for (const gxos::web::HtmlElementRef& ancestor : block.ancestors) {
                if (ancestor.serial == serial) {
                    matches = true;
                    break;
                }
            }
        }
        if (matches) matchingBlocks.push_back(index);
    }

    // Check vector capacity before changing anything. A successful mutation
    // is intentionally all-or-nothing for the host's bounded representation.
    if (matchingItems.empty() && !text.empty() &&
        document_->inlineItems.size() >= 2048u) {
        return HostResult{HostResultCode::DocumentMutationLimitExceeded};
    }

    if (!matchingItems.empty()) {
        const std::size_t first = matchingItems.front();
        gxos::web::WebInlineItem& item = document_->inlineItems[first];
        item.kind = gxos::web::InlineItemKind::TextRun;
        item.text = text;
        item.ownerSerial = serial;
        const gxos::web::HtmlElementRef* element = findElement(serial);
        item.parentSerial = element == nullptr ? 0 : element->parentSerial;
        for (std::size_t position = 1; position < matchingItems.size(); ++position)
            document_->inlineItems[matchingItems[position]].text.clear();
    } else if (!text.empty()) {
        gxos::web::WebInlineItem item;
        item.kind = gxos::web::InlineItemKind::TextRun;
        item.ownerSerial = serial;
        item.flowSerial = serial;
        item.text = text;
        document_->inlineItems.push_back(std::move(item));
    }

    if (!matchingBlocks.empty()) {
        document_->blocks[matchingBlocks.front()].text = text;
        if (document_->blocks[matchingBlocks.front()].type == gxos::web::BlockType::FormSubmit) {
            document_->blocks[matchingBlocks.front()].submitLabel = text;
        }
        for (std::size_t position = 1; position < matchingBlocks.size(); ++position)
            document_->blocks[matchingBlocks[position]].text.clear();
    }

    ++document_->scriptMutationCount;
    document_->layoutDirty = true;
    return HostResult();
}

HostResult NavigatorScriptHostAdapter::setProperty(
    const HostObjectReference& object, SourceView property,
    const HostValue& value)
{
    const HostResult validation = validate(object);
    if (!validation.succeeded()) return validation;
    if (object.kind == kNavigatorDocumentHostKind &&
        textEquals(property, "activeElement"))
        return HostResult{HostResultCode::PropertyReadOnly};
    if (object.kind == kNavigatorDocumentHostKind &&
        textEquals(property, "forms"))
        return HostResult{HostResultCode::PropertyReadOnly};
    if (object.kind == kNavigatorDocumentFormsCollectionHostKind ||
        object.kind == kNavigatorFormCollectionHostKind ||
        object.kind == kNavigatorOptionsCollectionHostKind ||
        object.kind == kNavigatorElementChildrenCollectionHostKind ||
        object.kind == kNavigatorSelectorCollectionHostKind)
        return HostResult{HostResultCode::PropertyReadOnly};
    if (object.kind != kNavigatorElementHostKind)
        return HostResult{HostResultCode::PropertyWriteFailed};
    if (textEquals(property, "parentElement") ||
        textEquals(property, "children") ||
        textEquals(property, "childElementCount") ||
        textEquals(property, "firstElementChild") ||
        textEquals(property, "lastElementChild") ||
        textEquals(property, "nextElementSibling") ||
        textEquals(property, "previousElementSibling"))
        return HostResult{HostResultCode::PropertyReadOnly};
    if ((isFormElement(object.instanceId) &&
            (textEquals(property, "elements") ||
             textEquals(property, "length"))) ||
        (isSelectFormElement(object.instanceId) &&
            (textEquals(property, "options") ||
             textEquals(property, "length"))))
        return HostResult{HostResultCode::PropertyReadOnly};
    if (textEquals(property, "id") || textEquals(property, "tagName"))
        return HostResult{HostResultCode::PropertyReadOnly};
    if (textEquals(property, "value") && isOptionElement(object.instanceId))
        return HostResult{HostResultCode::PropertyReadOnly};
    if (textEquals(property, "value")) {
        std::string text;
        const HostResult conversion = convertTextValue(value, text);
        if (!conversion.succeeded()) return conversion;
        if (isSelectFormElement(object.instanceId))
            return setSelectValue(object.instanceId, text, true);
        return setElementValue(object.instanceId, text, true);
    }
    if (textEquals(property, "defaultValue")) {
        std::string text;
        const HostResult conversion = convertTextValue(value, text);
        if (!conversion.succeeded()) return conversion;
        return setElementDefaultValue(object.instanceId, text);
    }
    if (textEquals(property, "checked")) {
        if (!isCheckableFormElement(object.instanceId))
            return HostResult{HostResultCode::PropertyWriteFailed};
        bool checked = false;
        switch (value.type) {
        case HostValueType::Boolean:
            checked = value.booleanValue;
            break;
        case HostValueType::Number:
            checked = value.numberValue != 0.0 && !std::isnan(value.numberValue);
            break;
        case HostValueType::String:
            checked = value.stringValue.length != 0;
            break;
        case HostValueType::Null:
        case HostValueType::Undefined:
            checked = false;
            break;
        default:
            checked = true;
            break;
        }
        return setElementChecked(object.instanceId, checked, true);
    }
    if (textEquals(property, "defaultChecked")) {
        if (!isCheckableFormElement(object.instanceId))
            return HostResult{HostResultCode::PropertyWriteFailed};
        bool checked = false;
        switch (value.type) {
        case HostValueType::Boolean:
            checked = value.booleanValue;
            break;
        case HostValueType::Number:
            checked = value.numberValue != 0.0 && !std::isnan(value.numberValue);
            break;
        case HostValueType::String:
            checked = value.stringValue.length != 0;
            break;
        case HostValueType::Null:
        case HostValueType::Undefined:
            checked = false;
            break;
        default:
            checked = true;
            break;
        }
        return setElementDefaultChecked(object.instanceId, checked);
    }
    if (textEquals(property, "selected")) {
        if (!isOptionElement(object.instanceId))
            return HostResult{HostResultCode::PropertyWriteFailed};
        return setOptionSelected(object.instanceId,
            booleanFromHostValue(value));
    }
    if (textEquals(property, "defaultSelected")) {
        if (!isOptionElement(object.instanceId))
            return HostResult{HostResultCode::PropertyWriteFailed};
        return setOptionDefaultSelected(object.instanceId,
            booleanFromHostValue(value));
    }
    if (textEquals(property, "selectedIndex")) {
        if (!isSelectFormElement(object.instanceId) ||
            value.type != HostValueType::Number ||
            std::isnan(value.numberValue) || std::isinf(value.numberValue) ||
            value.numberValue != std::trunc(value.numberValue))
            return HostResult{HostResultCode::PropertyWriteFailed};
        if (value.numberValue < static_cast<double>(std::numeric_limits<int>::min()) ||
            value.numberValue > static_cast<double>(std::numeric_limits<int>::max()))
            return HostResult{HostResultCode::PropertyWriteFailed};
        return setSelectIndex(object.instanceId,
            static_cast<int>(value.numberValue), true);
    }
    if (textEquals(property, "onclick")) {
        const bool hadAnyHandler = hasAnyEventHandler(
            kNavigatorElementHostKind, object.instanceId);
        if (value.type == HostValueType::Null) {
            if (ClickHandlerRecord* record = clickHandlerFor(object.instanceId))
                record->onclickFunction = kInvalidRuntimeFunctionId;
            removeEmptyClickHandler(object.instanceId);
            if (hadAnyHandler && !hasAnyEventHandler(
                    kNavigatorElementHostKind, object.instanceId) &&
                clickHandlerCount_ > 0)
                --clickHandlerCount_;
            return HostResult();
        }
        if (value.type != HostValueType::Function ||
            value.functionId == kInvalidRuntimeFunctionId) {
            return HostResult{HostResultCode::InvalidValue};
        }
        if (ClickHandlerRecord* record = clickHandlerFor(object.instanceId)) {
            record->onclickFunction = value.functionId;
            return HostResult();
        }
        if (clickOnclickRecordCount_ >= callbackLimit())
            return HostResult{HostResultCode::CallbackLimitExceeded};
        clickHandlers_[clickOnclickRecordCount_++] = ClickHandlerRecord{
            object.instanceId, value.functionId};
        if (!hadAnyHandler) ++clickHandlerCount_;
        return HostResult();
    }
    if (!textEquals(property, "textContent"))
        return HostResult{HostResultCode::PropertyWriteFailed};

    std::string text;
    const HostResult conversion = convertTextValue(value, text);
    if (!conversion.succeeded()) return conversion;
    return setElementTextContent(object.instanceId, text);
}

HostResult NavigatorScriptHostAdapter::validateDocumentReceiver(
    const HostObjectReference* receiver)
{
    if (receiver == nullptr || receiver->kind != kNavigatorDocumentHostKind ||
        receiver->instanceId != kNavigatorDocumentHostInstance)
        return HostResult{HostResultCode::InvalidObject};
    return validate(*receiver);
}

HostResult NavigatorScriptHostAdapter::call(
    const HostObjectReference* receiver, std::uint32_t methodId,
    const HostValue* arguments, std::size_t argumentCount, HostValue& result)
{
    if ((methodId == kNavigatorAddEventListenerMethod ||
            methodId == kNavigatorRemoveEventListenerMethod) &&
        argumentCount == 3u) {
        if (arguments == nullptr) return HostResult{HostResultCode::InvalidValue};
        // Boolean options are a host-call compatibility shorthand. The
        // runtime-aware path below handles ordinary options objects; keeping
        // this small direct-call path in sync makes the adapter contract
        // deterministic for embedders that do not have a RuntimeContext.
        if (arguments[2].type == HostValueType::Boolean) {
            return callInternal(receiver, methodId, arguments, argumentCount,
                result, false, arguments[2].booleanValue, true, nullptr);
        }
        if (arguments[2].type == HostValueType::Undefined) {
            return callInternal(receiver, methodId, arguments, argumentCount,
                result, false, false, true, nullptr);
        }
    }
    return callInternal(receiver, methodId, arguments, argumentCount, result,
        false, false, false, nullptr);
}

HostResult NavigatorScriptHostAdapter::callWithRuntime(
    RuntimeContext& runtime, const HostObjectReference* receiver,
    std::uint32_t methodId, const HostValue* arguments,
    std::size_t argumentCount, HostValue& result)
{
    bool optionsSupplied = false;
    bool once = false;
    bool capture = false;
    if ((methodId == kNavigatorAddEventListenerMethod ||
            methodId == kNavigatorRemoveEventListenerMethod) &&
        argumentCount == 3u) {
        if (arguments == nullptr)
            return HostResult{HostResultCode::InvalidValue};
        if (arguments[2].type == HostValueType::Boolean) {
            // The Boolean third argument represents capture only. In
            // particular, it never enables once.
            capture = arguments[2].booleanValue;
            optionsSupplied = true;
        } else if (arguments[2].type == HostValueType::Undefined) {
            // Explicit undefined is the same normalized options state as an
            // omitted third argument. Null and all other primitives remain
            // invalid, preserving the JS18-21 host contract.
            optionsSupplied = true;
        } else if (arguments[2].type == HostValueType::Object) {
            Value onceValue;
            RuntimeErrorCode optionError = RuntimeErrorCode::None;
            if (!runtime.readObjectPropertyForHost(arguments[2].objectId,
                    "once", onceValue, optionError)) {
                return HostResult{HostResultCode::InvalidValue};
            }
            if (onceValue.isUndefined()) {
                once = false;
            } else if (onceValue.isBoolean()) {
                once = onceValue.booleanValue();
            } else {
                return HostResult{HostResultCode::InvalidValue};
            }
            Value captureValue;
            optionError = RuntimeErrorCode::None;
            if (!runtime.readObjectPropertyForHost(arguments[2].objectId,
                    "capture", captureValue, optionError)) {
                return HostResult{HostResultCode::InvalidValue};
            }
            if (captureValue.isUndefined()) {
                capture = false;
            } else if (captureValue.isBoolean()) {
                capture = captureValue.booleanValue();
            } else {
                return HostResult{HostResultCode::InvalidValue};
            }
            optionsSupplied = true;
        } else {
            return HostResult{HostResultCode::InvalidValue};
        }
    }
    return callInternal(receiver, methodId, arguments, argumentCount, result,
        once, capture, optionsSupplied, &runtime);
}

HostResult NavigatorScriptHostAdapter::callInternal(
    const HostObjectReference* receiver,
    std::uint32_t methodId, const HostValue* arguments,
    std::size_t argumentCount, HostValue& result, bool once,
    bool capture, bool optionsSupplied, RuntimeContext* runtime)
{
    if (receiver == nullptr) return HostResult{HostResultCode::InvalidObject};
    if ((methodId == kNavigatorMatchesMethod ||
            methodId == kNavigatorClosestMethod ||
            methodId == kNavigatorContainsMethod ||
            methodId == kNavigatorGetElementsByTagNameMethod ||
            methodId == kNavigatorGetElementsByClassNameMethod ||
            methodId == kNavigatorGetAttributeMethod ||
            methodId == kNavigatorHasAttributeMethod ||
            methodId == kNavigatorSetAttributeMethod ||
            methodId == kNavigatorRemoveAttributeMethod) &&
        receiver->kind == kNavigatorElementHostKind &&
        (receiver->generation != generation_ ||
            findElement(receiver->instanceId) == nullptr)) {
        if (methodId == kNavigatorGetElementsByTagNameMethod ||
            methodId == kNavigatorGetElementsByClassNameMethod)
            return emptySelectorCollection(result);
        if (methodId == kNavigatorSetAttributeMethod ||
            methodId == kNavigatorRemoveAttributeMethod) {
            result = HostValue::undefined();
            return HostResult();
        }
        result = methodId == kNavigatorClosestMethod
            || methodId == kNavigatorGetAttributeMethod
            ? HostValue::nullValue() : HostValue::boolean(false);
        return HostResult();
    }
    const HostResult receiverResult = validate(*receiver);
    if (!receiverResult.succeeded()) return receiverResult;
    if (methodId == kNavigatorGetAttributeMethod ||
        methodId == kNavigatorHasAttributeMethod) {
        if (receiver->kind != kNavigatorElementHostKind)
            return HostResult{HostResultCode::InvalidValue};
        const bool validArgument = arguments != nullptr &&
            argumentCount == 1u && arguments[0].type == HostValueType::String;
        SourceView attributeValue;
        const bool present = validArgument && resolveElementAttribute(
            receiver->instanceId, arguments[0].stringValue, attributeValue);
        if (methodId == kNavigatorHasAttributeMethod) {
            result = HostValue::boolean(present);
        } else if (!present || attributeValue.data == nullptr ||
            attributeValue.length > kNavigatorScriptMaxAttributeValueLength) {
            result = HostValue::nullValue();
        } else {
            // The document-owned string stays live through this synchronous
            // call. RuntimeContext copies the borrowed span into its own
            // bounded string store before returning to JavaScript.
            result = HostValue::string(attributeValue);
        }
        return HostResult();
    }
    if (methodId == kNavigatorSetAttributeMethod ||
        methodId == kNavigatorRemoveAttributeMethod) {
        if (receiver->kind != kNavigatorElementHostKind)
            return HostResult{HostResultCode::InvalidValue};
        if (methodId == kNavigatorSetAttributeMethod) {
            if (arguments != nullptr && argumentCount == 2u &&
                arguments[0].type == HostValueType::String &&
                arguments[1].type == HostValueType::String)
                mutateElementAttribute(receiver->instanceId,
                    arguments[0].stringValue, arguments[1].stringValue, false);
        } else if (arguments != nullptr && argumentCount == 1u &&
            arguments[0].type == HostValueType::String) {
            mutateElementAttribute(receiver->instanceId,
                arguments[0].stringValue, SourceView(), true);
        }
        // This bounded host API deliberately fails closed for invalid names,
        // non-string arguments, capacity failures, and deferred projections.
        result = HostValue::undefined();
        return HostResult();
    }
    if (methodId == kNavigatorQuerySelectorMethod ||
        methodId == kNavigatorQuerySelectorAllMethod) {
        if (receiver->kind != kNavigatorDocumentHostKind &&
            receiver->kind != kNavigatorElementHostKind)
            return HostResult{HostResultCode::InvalidValue};
        const HostInstanceId scopeSerial = receiver->kind ==
            kNavigatorDocumentHostKind ? 0u : receiver->instanceId;
        return methodId == kNavigatorQuerySelectorMethod
            ? querySelector(scopeSerial, arguments, argumentCount, result)
            : querySelectorAll(scopeSerial, arguments, argumentCount, result);
    }
    if (methodId == kNavigatorGetElementsByTagNameMethod ||
        methodId == kNavigatorGetElementsByClassNameMethod) {
        if (receiver->kind != kNavigatorDocumentHostKind &&
            receiver->kind != kNavigatorElementHostKind)
            return HostResult{HostResultCode::InvalidValue};
        const HostInstanceId scopeSerial = receiver->kind ==
            kNavigatorDocumentHostKind ? 0u : receiver->instanceId;
        return getElementsBySimpleSelector(scopeSerial, arguments,
            argumentCount,
            methodId == kNavigatorGetElementsByClassNameMethod, result);
    }
    if (methodId == kNavigatorContainsMethod) {
        if (receiver->kind != kNavigatorElementHostKind)
            return HostResult{HostResultCode::InvalidValue};

        // Only a live Element from this document generation can participate.
        // Both identities are generation + canonical structural serial; the
        // candidate is walked upward through the shared bounded parent model.
        bool contained = false;
        if (arguments != nullptr && argumentCount == 1u &&
            arguments[0].type == HostValueType::HostObject) {
            const HostObjectReference& candidate = arguments[0].hostObject;
            if (candidate.valid() &&
                candidate.kind == kNavigatorElementHostKind &&
                candidate.generation == generation_ &&
                findElement(candidate.instanceId) != nullptr) {
                contained = candidate.instanceId == receiver->instanceId ||
                    isDescendantOrSelf(candidate.instanceId,
                        receiver->instanceId);
            }
        }
        result = HostValue::boolean(contained);
        return HostResult();
    }
    if (methodId == kNavigatorMatchesMethod ||
        methodId == kNavigatorClosestMethod) {
        if (receiver->kind != kNavigatorElementHostKind) {
            return HostResult{HostResultCode::InvalidValue};
        }

        NavigatorScriptSelectorDescriptor selector;
        const bool parsed = arguments != nullptr && argumentCount == 1u &&
            arguments[0].type == HostValueType::String &&
            parseBoundedSelector(arguments[0].stringValue, selector);
        if (methodId == kNavigatorMatchesMethod) {
            // Selector failures follow querySelector's bounded, fail-closed
            // policy and do not allocate or retain a selector collection.
            const gxos::web::HtmlElementRef* element =
                findElement(receiver->instanceId);
            result = HostValue::boolean(parsed && element != nullptr &&
                selectorElementMatches(*element, selector));
            return HostResult();
        }

        HostInstanceId matchSerial = 0u;
        if (!parsed || !selectorClosestMatch(receiver->instanceId, selector,
                matchSerial)) {
            result = HostValue::nullValue();
            return HostResult();
        }
        result = HostValue::fromHostObject(HostObjectReference{
            matchSerial, generation_, kNavigatorElementHostKind});
        return HostResult();
    }
    if (methodId == kNavigatorClickMethod) {
        if (receiver->kind != kNavigatorElementHostKind || argumentCount != 0u ||
            runtime == nullptr)
            return HostResult{HostResultCode::InvalidValue};
        RuntimeErrorCode error = RuntimeErrorCode::None;
        bool defaultPrevented = false;
        if (!requestElementActivation(*runtime, receiver->instanceId,
                NavigatorScriptActivationProvenance::Programmatic, error,
                &defaultPrevented)) {
            return error == RuntimeErrorCode::StaleHostObject
                ? HostResult{HostResultCode::StaleObject}
                : error == RuntimeErrorCode::HostReentryUnsupported
                    ? HostResult{HostResultCode::ReentryUnsupported}
                    : HostResult{HostResultCode::CallFailed};
        }
        result = HostValue::undefined();
        return HostResult();
    }
    if (methodId == kNavigatorResetMethod) {
        if (receiver->kind != kNavigatorElementHostKind ||
            argumentCount != 0u || runtime == nullptr ||
            !isFormElement(receiver->instanceId))
            return HostResult{HostResultCode::InvalidValue};
        RuntimeErrorCode error = RuntimeErrorCode::None;
        bool defaultPrevented = false;
        if (!requestFormReset(*runtime, receiver->instanceId, error,
                &defaultPrevented)) {
            return error == RuntimeErrorCode::StaleHostObject
                ? HostResult{HostResultCode::StaleObject}
                : error == RuntimeErrorCode::HostReentryUnsupported
                    ? HostResult{HostResultCode::ReentryUnsupported}
                    : HostResult{HostResultCode::CallFailed};
        }
        result = HostValue::undefined();
        return HostResult();
    }
    if (methodId == kNavigatorFocusMethod ||
        methodId == kNavigatorBlurMethod) {
        if (receiver->kind != kNavigatorElementHostKind ||
            argumentCount != 0u || focusRequestCallback_ == nullptr)
            return HostResult{HostResultCode::InvalidValue};
        if (!focusRequestCallback_(focusRequestContext_, receiver->instanceId,
                methodId == kNavigatorFocusMethod))
            return HostResult{HostResultCode::CallFailed};
        result = HostValue::undefined();
        return HostResult();
    }
    if (methodId == kNavigatorHasFocusMethod) {
        if (receiver->kind != kNavigatorDocumentHostKind ||
            argumentCount != 0u) return HostResult{HostResultCode::InvalidValue};
        // Navigator has no separate OS/window focus owner. The narrow,
        // authoritative meaning is therefore a valid current-document form
        // focus owner, exactly the same projection used by activeElement.
        result = HostValue::boolean(activeElementSerial() != 0);
        return HostResult();
    }
    if (methodId == kNavigatorAddEventListenerMethod) {
        if ((receiver->kind != kNavigatorElementHostKind &&
                receiver->kind != kNavigatorDocumentHostKind) ||
            (argumentCount != 2u &&
                (!optionsSupplied || argumentCount != 3u)) ||
            arguments == nullptr ||
            arguments[0].type != HostValueType::String ||
            arguments[1].type != HostValueType::Function ||
            arguments[1].functionId == kInvalidRuntimeFunctionId) {
            return HostResult{HostResultCode::InvalidValue};
        }
        if (arguments[0].stringValue.data == nullptr &&
            arguments[0].stringValue.length != 0) {
            return HostResult{HostResultCode::InvalidValue};
        }
        NavigatorScriptEventType eventType = NavigatorScriptEventType::Click;
        if (!eventTypeFor(arguments[0].stringValue, eventType))
            return HostResult{HostResultCode::InvalidValue};
        // The exact (owner, event type, Function ID, capture) tuple is a
        // duplicate no-op. once is deliberately excluded from identity, so a
        // second call cannot change the first registration's once behavior.
        if (clickListenerFor(receiver->kind, receiver->instanceId, eventType,
                arguments[1].functionId, capture) != nullptr) {
            result = HostValue::undefined();
            return HostResult();
        }
        if (clickListenerCount_ >= listenerLimit())
            return HostResult{HostResultCode::CallbackLimitExceeded};

        ClickListenerRecord* freeRecord = nullptr;
        for (ClickListenerRecord& candidate : clickListeners_) {
            if (candidate.serial == 0 ||
                candidate.listenerFunction == kInvalidRuntimeFunctionId) {
                freeRecord = &candidate;
                break;
            }
        }
        if (freeRecord == nullptr)
            return HostResult{HostResultCode::CallbackLimitExceeded};

        std::uint64_t sequence = 0;
        if (!allocateListenerSequence(sequence))
            return HostResult{HostResultCode::CallbackLimitExceeded};
        const bool hadAnyHandler = hasAnyEventHandler(
            receiver->kind, receiver->instanceId);
        freeRecord->serial = receiver->instanceId;
        freeRecord->listenerFunction = arguments[1].functionId;
        freeRecord->flags = (once ? kNavigatorClickListenerOnceFlag : 0u) |
            (capture ? kNavigatorClickListenerCaptureFlag : 0u);
        freeRecord->registrationSequence = sequence;
        freeRecord->ownerKind = receiver->kind;
        freeRecord->eventType = eventType;
        ++clickListenerCount_;
        if (!hadAnyHandler) ++clickHandlerCount_;
        result = HostValue::undefined();
        return HostResult();
    }
    if (methodId == kNavigatorRemoveEventListenerMethod) {
        if ((receiver->kind != kNavigatorElementHostKind &&
                receiver->kind != kNavigatorDocumentHostKind) ||
            (argumentCount != 2u &&
                (!optionsSupplied || argumentCount != 3u)) ||
            arguments == nullptr ||
            arguments[0].type != HostValueType::String ||
            arguments[1].type != HostValueType::Function ||
            arguments[1].functionId == kInvalidRuntimeFunctionId) {
            return HostResult{HostResultCode::InvalidValue};
        }
        if (arguments[0].stringValue.data == nullptr &&
            arguments[0].stringValue.length != 0) {
            return HostResult{HostResultCode::InvalidValue};
        }
        NavigatorScriptEventType eventType = NavigatorScriptEventType::Click;
        if (!eventTypeFor(arguments[0].stringValue, eventType))
            return HostResult{HostResultCode::InvalidValue};

        // Removal is deliberately a lookup only. It never creates a record,
        // and function IDs provide JavaScript function identity within this
        // same realm; source text or function shape is never compared.
        ClickListenerRecord* record = clickListenerFor(receiver->kind,
            receiver->instanceId, eventType, arguments[1].functionId,
            capture);
        if (record != nullptr) {
            removeClickListener(*record);
        }
        result = HostValue::undefined();
        return HostResult();
    }
    if (methodId != kNavigatorGetElementByIdMethod ||
        receiver->kind != kNavigatorDocumentHostKind || argumentCount != 1u ||
        arguments == nullptr || arguments[0].type != HostValueType::String) {
        return HostResult{HostResultCode::CallFailed};
    }
    if (arguments[0].stringValue.data == nullptr &&
        arguments[0].stringValue.length != 0) {
        return HostResult{HostResultCode::InvalidValue};
    }
    if (arguments[0].stringValue.length > limits_.maxDocumentIdLength)
        return HostResult{HostResultCode::DocumentLookupLimitExceeded};
    const std::string id(arguments[0].stringValue.data == nullptr ? "" :
        arguments[0].stringValue.data, arguments[0].stringValue.length);

    const std::size_t count = std::min(limits_.maxDocumentNodes,
        document_->structuralElements.size());
    for (std::size_t index = 0; index < count; ++index) {
        const gxos::web::HtmlElementRef& element =
            document_->structuralElements[index];
        if (element.serial != 0 && element.id == id) {
            result = HostValue::fromHostObject(HostObjectReference{
                element.serial, generation_, kNavigatorElementHostKind});
            return HostResult();
        }
    }
    if (document_->structuralElements.size() > limits_.maxDocumentNodes)
        return HostResult{HostResultCode::DocumentLookupLimitExceeded};
    // DOM-like getElementById uses Null for not found; Undefined is reserved
    // for an unknown host property in the generic JS7 contract.
    result = HostValue::nullValue();
    return HostResult();
}

bool navigatorScriptElementTextContent(
    const gxos::web::WebDocument& document, std::uint64_t serial,
    std::string& result, std::size_t maxOperations, std::size_t maxBytes)
{
    if (findElementInDocument(document, serial, document.structuralElements.size()) == nullptr)
        return false;
    result.clear();
    const gxos::web::DocBlock* directBlock = nullptr;
    std::size_t directBlockCount = 0;
    for (const gxos::web::DocBlock& block : document.blocks) {
        if (block.elementMetadata.serial != serial) continue;
        directBlock = &block;
        ++directBlockCount;
    }
    if (directBlockCount == 1u && directBlock != nullptr) {
        if (directBlock->text.size() > maxBytes) return false;
        result = directBlock->text;
        return true;
    }
    std::size_t operations = 0;
    bool matched = false;
    for (const gxos::web::WebInlineItem& item : document.inlineItems) {
        if (operations >= maxOperations) return false;
        ++operations;
        if (item.kind != gxos::web::InlineItemKind::TextRun &&
            item.kind != gxos::web::InlineItemKind::ForcedBreak) continue;
        if (!isDescendantInDocument(document, item.ownerSerial, serial,
            document.structuralElements.size())) continue;
        matched = true;
        const std::string text = item.kind == gxos::web::InlineItemKind::ForcedBreak
            ? "\n" : item.text;
        if (text.size() > maxBytes || result.size() > maxBytes - text.size())
            return false;
        result += text;
    }
    return matched || result.empty();
}

NavigatorScriptExecutionHarness::NavigatorScriptExecutionHarness(
    RuntimeLimits runtimeLimits, NavigatorScriptHostLimits hostLimits)
    : runtime_(runtimeLimits), adapter_(1u, hostLimits)
{
    adapter_.setFocusRequestCallback(
        &NavigatorScriptExecutionHarness::focusRequestCallback, this);
    adapter_.setDispatchCompleteCallback(
        &NavigatorScriptExecutionHarness::dispatchCompleteCallback, this);
    adapter_.setActivationDefaultActionCallback(
        &NavigatorScriptExecutionHarness::activationDefaultActionCallback, this);
    runtime_.setHostAdapter(&adapter_);
}

bool NavigatorScriptExecutionHarness::installDocumentGlobal(
    RuntimeErrorCode& error)
{
    return runtime_.installHostGlobal("document",
        kNavigatorDocumentHostInstance, kNavigatorDocumentHostKind, error);
}

bool NavigatorScriptExecutionHarness::loadParsedDocument(
    gxos::web::WebDocument document, bool resetRealm, RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    if (resetRealm) {
        runtime_.reset();
        if (!runtime_.lastResult().succeeded()) {
            error = runtime_.lastResult().runtimeError.code;
            return false;
        }
    }
    document_ = std::move(document);
    adapter_.attachDocument(document_, runtime_.hostGeneration());
    document_.formRuntimeState = gxos::web::FormRuntimeStateTable{};
    document_.formRuntimeState.initialized = true;
    document_.formRuntimeState.documentGeneration = 1u;
    for (const gxos::web::HtmlElementRef& element :
        document_.structuralElements) {
        const gxos::web::FormControlMetadata& metadata = element.formControl;
        if (element.serial == 0 || !metadata.metadataComplete ||
            !metadata.supported) continue;
        if (document_.formRuntimeState.count >=
            gxos::web::kFormRuntimeControlCap) break;
        gxos::web::FormRuntimeControlState& state =
            document_.formRuntimeState.controls[
                document_.formRuntimeState.count++];
        state.logicalSerial = element.serial;
        state.type = metadata.type;
        state.parentFormSerial = metadata.parentFormSerial;
        state.parentFieldsetSerial = metadata.parentFieldsetSerial;
        state.checked = metadata.checked;
        state.defaultChecked = metadata.checked;
        state.defaultValue = metadata.value;
        state.defaultSelectedOption = metadata.selectedOptionIndex;
        state.disabled = metadata.disabled;
        state.metadataValid = true;
        for (const gxos::web::DocBlock& block : document_.blocks) {
            if (block.formControl.logicalSerial != element.serial) continue;
            if (block.type == gxos::web::BlockType::FormTextInput ||
                block.type == gxos::web::BlockType::FormTextarea) {
                state.defaultValue = block.inputValue;
            } else if (block.type == gxos::web::BlockType::FormSelect) {
                state.defaultValue = block.inputValue;
                state.defaultSelectedOption = block.selectedOption;
            }
            break;
        }
    }
    // Keep malformed parser markup and scripted defaultChecked writes on one
    // deterministic policy: the first checked radio default in document order
    // owns its group. This touches only reset defaults, never current state.
    const auto sameRadioGroup = [](const gxos::web::DocBlock& left,
        const gxos::web::DocBlock& right) {
        if (left.type != gxos::web::BlockType::FormRadio ||
            right.type != gxos::web::BlockType::FormRadio) return false;
        if (left.formControl.name.empty() || right.formControl.name.empty())
            return left.formControl.logicalSerial == right.formControl.logicalSerial;
        if (left.formIndex >= 0 || right.formIndex >= 0)
            return left.formIndex >= 0 && right.formIndex == left.formIndex &&
                left.formControl.name == right.formControl.name;
        if (left.formControl.parentFormSerial != 0 ||
            right.formControl.parentFormSerial != 0)
            return left.formControl.parentFormSerial != 0 &&
                right.formControl.parentFormSerial == left.formControl.parentFormSerial &&
                left.formControl.name == right.formControl.name;
        if (left.formControl.parentFieldsetSerial != 0 ||
            right.formControl.parentFieldsetSerial != 0)
            return left.formControl.parentFieldsetSerial != 0 &&
                right.formControl.parentFieldsetSerial == left.formControl.parentFieldsetSerial &&
                left.formControl.name == right.formControl.name;
        return left.formControl.name == right.formControl.name;
    };
    for (std::size_t index = 0; index < document_.blocks.size(); ++index) {
        const gxos::web::DocBlock& block = document_.blocks[index];
        if (block.type != gxos::web::BlockType::FormRadio) continue;
        gxos::web::FormRuntimeControlState* state = nullptr;
        for (std::size_t stateIndex = 0;
            stateIndex < document_.formRuntimeState.count; ++stateIndex) {
            gxos::web::FormRuntimeControlState& candidateState =
                document_.formRuntimeState.controls[stateIndex];
            if (candidateState.logicalSerial == block.formControl.logicalSerial) {
                state = &candidateState;
                break;
            }
        }
        if (state == nullptr || !state->defaultChecked) continue;
        for (std::size_t prior = 0; prior < index; ++prior) {
            const gxos::web::DocBlock& candidate = document_.blocks[prior];
            if (candidate.type != gxos::web::BlockType::FormRadio ||
                !sameRadioGroup(candidate, block)) continue;
            for (std::size_t stateIndex = 0;
                stateIndex < document_.formRuntimeState.count; ++stateIndex) {
                const gxos::web::FormRuntimeControlState& candidateState =
                    document_.formRuntimeState.controls[stateIndex];
                if (candidateState.logicalSerial == candidate.formControl.logicalSerial &&
                    candidateState.defaultChecked) {
                    state->defaultChecked = false;
                    break;
                }
            }
            if (!state->defaultChecked) break;
        }
    }
    focusedElementSerial_ = 0;
    focusedInputCaret_ = 0;
    focusTransitionActive_ = false;
    pendingFocusRequest_ = false;
    pendingFocusSerial_ = 0;
    pendingFocusGain_ = false;
    if (!installDocumentGlobal(error)) return false;
    loaded_ = true;
    return true;
}

bool NavigatorScriptExecutionHarness::loadHtml(const std::string& url,
    const std::string& html, RuntimeErrorCode& error)
{
    const gxos::web::WebDocument document = gxos::web::parseHtml(url, html);
    return loadParsedDocument(document, loaded_, error);
}

bool NavigatorScriptExecutionHarness::replaceHtml(const std::string& url,
    const std::string& html, RuntimeErrorCode& error)
{
    const gxos::web::WebDocument document = gxos::web::parseHtml(url, html);
    return loadParsedDocument(document, true, error);
}

bool NavigatorScriptExecutionHarness::invalidateDocumentGeneration(
    RuntimeErrorCode& error)
{
    if (!runtime_.invalidateHostGeneration(error)) return false;
    adapter_.setGeneration(runtime_.hostGeneration());
    return true;
}

ScriptResult NavigatorScriptExecutionHarness::execute(SourceView source)
{
    return runtime_.executeInSameRealm(source);
}

ScriptResult NavigatorScriptExecutionHarness::execute(const std::string& source)
{
    return execute(SourceView(source.data(), source.size()));
}

bool NavigatorScriptExecutionHarness::dispatchClick(std::uint64_t serial,
    RuntimeErrorCode& error, bool* defaultPrevented)
{
    return adapter_.dispatchClick(runtime_, serial, error, defaultPrevented);
}

bool NavigatorScriptExecutionHarness::dispatchSubmit(
    std::uint64_t formSerial, RuntimeErrorCode& error, bool* defaultPrevented)
{
    return adapter_.dispatchSubmitEvent(runtime_, formSerial, error,
        defaultPrevented);
}

bool NavigatorScriptExecutionHarness::focusRequestCallback(
    void* context, HostInstanceId serial, bool focus)
{
    if (context == nullptr) return false;
    return static_cast<NavigatorScriptExecutionHarness*>(context)->requestFocus(
        serial, focus);
}

void NavigatorScriptExecutionHarness::dispatchCompleteCallback(void* context)
{
    if (context == nullptr) return;
    NavigatorScriptExecutionHarness* harness =
        static_cast<NavigatorScriptExecutionHarness*>(context);
    if (harness->focusTransitionActive_) return;
    RuntimeErrorCode error = RuntimeErrorCode::None;
    (void)harness->drainPendingFocusRequests(error);
}

bool NavigatorScriptExecutionHarness::activationDefaultActionCallback(
    void* context, HostInstanceId serial,
    NavigatorScriptActivationProvenance provenance)
{
    if (context == nullptr) return false;
    return static_cast<NavigatorScriptExecutionHarness*>(context)
        ->performElementDefaultAction(serial, provenance);
}

bool NavigatorScriptExecutionHarness::performElementDefaultAction(
    HostInstanceId serial, NavigatorScriptActivationProvenance)
{
    if (!loaded_ || serial == 0 || adapter_.document() != &document_)
        return false;
    const gxos::web::DocBlock* block = nullptr;
    for (const gxos::web::DocBlock& candidate : document_.blocks) {
        if (candidate.elementMetadata.serial == serial ||
            candidate.formControl.logicalSerial == serial) {
            block = &candidate;
            break;
        }
    }
    if (block == nullptr) return true;
    if (block->formControl.disabled) return true;

    if (block->type == gxos::web::BlockType::FormCheckbox ||
        block->type == gxos::web::BlockType::FormRadio ||
        block->type == gxos::web::BlockType::FormSelect) {
        bool changed = false;
        if (!adapter_.setFormControlFromUser(serial, changed)) return true;
        if (changed) {
            RuntimeErrorCode error = RuntimeErrorCode::None;
            if (!adapter_.dispatchInputEvent(runtime_, serial, error) ||
                !adapter_.dispatchChangeEvent(runtime_, serial, error))
                return false;
        }
        return true;
    }

    if (block->type == gxos::web::BlockType::FormSubmit &&
        block->formControl.type == gxos::web::FormControlType::Submit &&
        block->formControl.parentFormSerial != 0) {
        RuntimeErrorCode error = RuntimeErrorCode::None;
        bool defaultPrevented = false;
        if (!adapter_.dispatchSubmitEvent(runtime_,
                block->formControl.parentFormSerial, error,
                &defaultPrevented)) return false;
    } else if (block->type == gxos::web::BlockType::FormSubmit &&
        block->formControl.type == gxos::web::FormControlType::Reset &&
        block->formControl.parentFormSerial != 0) {
        RuntimeErrorCode error = RuntimeErrorCode::None;
        bool defaultPrevented = false;
        if (!adapter_.requestFormReset(runtime_,
                block->formControl.parentFormSerial, error,
                &defaultPrevented)) return false;
    }
    return true;
}

bool NavigatorScriptExecutionHarness::requestFocus(HostInstanceId serial,
    bool focus)
{
    const gxos::web::HtmlElementRef* element = findElementInDocument(document_,
        serial, adapter_.limits().maxDocumentNodes);
    const gxos::web::DocBlock* focusBlock = nullptr;
    for (const gxos::web::DocBlock& candidate : document_.blocks) {
        if (candidate.formControl.logicalSerial == serial) {
            focusBlock = &candidate;
            break;
        }
    }
    const bool focusable = loaded_ && element != nullptr &&
        focusBlock != nullptr && serial != 0 &&
        focusBlock->formControl.metadataComplete &&
        focusBlock->formControl.supported && !focusBlock->formUnsupported &&
        !focusBlock->formControl.hidden && !focusBlock->formControl.disabled;
    const bool ownsFocus = focusedElementSerial_ == serial &&
        document_.formRuntimeState.focusValid;
    if (focus) {
        // Calling focus() on a non-focusable element is a bounded no-op. The
        // receiver was still validated by the ordinary host method path.
        if (!focusable || ownsFocus) return true;
    } else if (!ownsFocus) {
        // blur() never clears a different element's authoritative owner.
        return true;
    }
    if (focusTransitionActive_ || adapter_.eventDispatchActive()) {
        pendingFocusRequest_ = true;
        pendingFocusSerial_ = serial;
        pendingFocusGain_ = focus;
        return true;
    }
    RuntimeErrorCode error = RuntimeErrorCode::None;
    const bool succeeded = focus ? focusElement(serial, error) :
        clearFocus(error);
    return succeeded && error == RuntimeErrorCode::None;
}

bool NavigatorScriptExecutionHarness::focusElementInternal(
    std::uint64_t serial, RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    const gxos::web::HtmlElementRef* element = findElementInDocument(document_,
        serial, adapter_.limits().maxDocumentNodes);
    const gxos::web::DocBlock* focusBlock = nullptr;
    for (const gxos::web::DocBlock& candidate : document_.blocks) {
        if (candidate.formControl.logicalSerial == serial) {
            focusBlock = &candidate;
            break;
        }
    }
    if (!loaded_ || element == nullptr || focusBlock == nullptr ||
        serial == 0 || !focusBlock->formControl.metadataComplete ||
        !focusBlock->formControl.supported || focusBlock->formUnsupported ||
        focusBlock->formControl.hidden || focusBlock->formControl.disabled) {
        error = RuntimeErrorCode::StaleHostObject;
        return false;
    }
    if (focusedElementSerial_ == serial) return true;
    const std::uint64_t previousSerial = focusedElementSerial_;
    if (previousSerial != 0) {
        if (!adapter_.dispatchFocusEvent(runtime_, previousSerial, false, false,
                serial, error) || !adapter_.dispatchFocusEvent(runtime_,
                previousSerial, false, true, serial, error)) return false;
        bool changed = false;
        if (!adapter_.commitFormEditSession(previousSerial, changed))
            changed = false;
        if (changed && !adapter_.dispatchChangeEvent(runtime_, previousSerial,
                error)) return false;
    }
    document_.formRuntimeState.initialized = true;
    document_.formRuntimeState.documentGeneration = 1u;
    document_.formRuntimeState.focusedLogicalSerial = serial;
    document_.formRuntimeState.focusedDocumentGeneration = 1u;
    document_.formRuntimeState.focusValid = true;
    focusedElementSerial_ = serial;
    adapter_.beginFormEditSession(serial);
    focusedInputCaret_ = 0;
    for (const gxos::web::DocBlock& block : document_.blocks) {
        if (block.formControl.logicalSerial == serial &&
            (block.type == gxos::web::BlockType::FormTextInput ||
                block.type == gxos::web::BlockType::FormTextarea)) {
            focusedInputCaret_ = static_cast<int>(block.inputValue.size());
            break;
        }
    }
    if (!adapter_.dispatchFocusEvent(runtime_, serial, true, false,
            previousSerial, error) ||
        !adapter_.dispatchFocusEvent(runtime_, serial, true, true,
            previousSerial, error))
        return false;
    return true;
}

bool NavigatorScriptExecutionHarness::clearFocusInternal(
    RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    if (focusedElementSerial_ == 0) return true;
    const std::uint64_t previousSerial = focusedElementSerial_;
    if (!adapter_.dispatchFocusEvent(runtime_, previousSerial, false, false,
            0, error) || !adapter_.dispatchFocusEvent(runtime_, previousSerial,
            false, true, 0, error)) return false;
    bool changed = false;
    if (!adapter_.commitFormEditSession(previousSerial, changed))
        changed = false;
    if (changed && !adapter_.dispatchChangeEvent(runtime_, previousSerial,
            error)) return false;
    document_.formRuntimeState.focusedLogicalSerial = 0;
    document_.formRuntimeState.focusedDocumentGeneration = 0;
    document_.formRuntimeState.focusValid = false;
    focusedElementSerial_ = 0;
    focusedInputCaret_ = 0;
    return true;
}

bool NavigatorScriptExecutionHarness::drainPendingFocusRequests(
    RuntimeErrorCode& error)
{
    // A single bounded pending request is enough to make callback redirects
    // deterministic without introducing an asynchronous focus queue. The
    // cap also makes mutually recursive focus listeners fail closed.
    constexpr std::size_t kMaxRedirects = 16u;
    for (std::size_t redirect = 0; redirect < kMaxRedirects &&
            pendingFocusRequest_; ++redirect) {
        const std::uint64_t serial = pendingFocusSerial_;
        const bool focus = pendingFocusGain_;
        pendingFocusRequest_ = false;
        if (focus) {
            if (!focusElementInternal(serial, error)) return false;
        } else if (focusedElementSerial_ == serial) {
            if (!clearFocusInternal(error)) return false;
        }
    }
    pendingFocusRequest_ = false;
    return true;
}

bool NavigatorScriptExecutionHarness::focusElement(std::uint64_t serial,
    RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    if (focusTransitionActive_) {
        pendingFocusRequest_ = true;
        pendingFocusSerial_ = serial;
        pendingFocusGain_ = true;
        return true;
    }
    focusTransitionActive_ = true;
    bool succeeded = focusElementInternal(serial, error);
    if (succeeded) succeeded = drainPendingFocusRequests(error);
    focusTransitionActive_ = false;
    return succeeded;
}

bool NavigatorScriptExecutionHarness::clearFocus(RuntimeErrorCode& error)
{
    error = RuntimeErrorCode::None;
    if (focusTransitionActive_) {
        pendingFocusRequest_ = true;
        pendingFocusSerial_ = focusedElementSerial_;
        pendingFocusGain_ = false;
        return true;
    }
    focusTransitionActive_ = true;
    bool succeeded = clearFocusInternal(error);
    if (succeeded) succeeded = drainPendingFocusRequests(error);
    focusTransitionActive_ = false;
    return succeeded;
}

bool NavigatorScriptExecutionHarness::dispatchFocusedKeyboardEvent(
    int keyCode, bool down, bool shiftPressed, RuntimeErrorCode& error,
    bool* defaultPrevented)
{
    return adapter_.dispatchKeyboardEvent(runtime_, focusedElementSerial_,
        keyCode, down, shiftPressed, error, defaultPrevented);
}

bool NavigatorScriptExecutionHarness::dispatchFocusedUserEdit(
    int keyCode, bool shiftPressed, RuntimeErrorCode& error,
    bool* defaultPrevented)
{
    error = RuntimeErrorCode::None;
    if (!loaded_ || focusedElementSerial_ == 0) {
        error = RuntimeErrorCode::StaleHostObject;
        if (defaultPrevented != nullptr) *defaultPrevented = false;
        return false;
    }

    bool keydownDefaultPrevented = false;
    if (!adapter_.dispatchKeyboardEvent(runtime_, focusedElementSerial_,
            keyCode, true, shiftPressed, error, &keydownDefaultPrevented)) {
        return false;
    }
    if (defaultPrevented != nullptr) *defaultPrevented = keydownDefaultPrevented;

    // Keydown listeners can request a bounded focus redirect. The production
    // path targets the resulting authoritative owner for its default action.
    const std::uint64_t serial = focusedElementSerial_;
    gxos::web::DocBlock* block = nullptr;
    for (gxos::web::DocBlock& candidate : document_.blocks) {
        if (candidate.formControl.logicalSerial == serial &&
            candidate.formControl.metadataComplete &&
            candidate.formControl.supported &&
            (candidate.type == gxos::web::BlockType::FormTextInput ||
                candidate.type == gxos::web::BlockType::FormTextarea)) {
            block = &candidate;
            break;
        }
    }

    if (!keydownDefaultPrevented && block != nullptr &&
        keyCode != 13 && keyCode != 32) {
        const int caret = std::max(0, std::min(focusedInputCaret_,
            static_cast<int>(block->inputValue.size())));
        std::string editedValue = block->inputValue;
        int nextCaret = caret;
        if (keyCode == 8) {
            if (caret > 0) {
                editedValue.erase(static_cast<std::size_t>(caret - 1), 1);
                nextCaret = caret - 1;
            }
        } else if (keyCode == 46) {
            if (caret < static_cast<int>(editedValue.size()))
                editedValue.erase(static_cast<std::size_t>(caret), 1);
        } else if (keyCode == 37) {
            nextCaret = std::max(0, caret - 1);
        } else if (keyCode == 39) {
            nextCaret = std::min(static_cast<int>(editedValue.size()),
                caret + 1);
        } else if (keyCode == 36) {
            nextCaret = 0;
        } else if (keyCode == 35) {
            nextCaret = static_cast<int>(editedValue.size());
        } else if (keyCode >= 32 && keyCode <= 126) {
            editedValue.insert(static_cast<std::size_t>(caret), 1,
                harnessTextInputCharacter(keyCode, shiftPressed));
            nextCaret = caret + 1;
        }
        if (editedValue != block->inputValue &&
            adapter_.setFormValueFromUser(serial, editedValue)) {
            focusedInputCaret_ = nextCaret;
            if (!adapter_.dispatchInputEvent(runtime_, serial, error)) {
                return false;
            }
        }
        if (editedValue == block->inputValue &&
            (keyCode == 37 || keyCode == 39 || keyCode == 36 || keyCode == 35)) {
            focusedInputCaret_ = nextCaret;
        }
    }

    const std::uint64_t keyupTarget = focusedElementSerial_;
    return adapter_.dispatchKeyboardEvent(runtime_, keyupTarget, keyCode,
        false, shiftPressed, error, nullptr);
}

bool NavigatorScriptExecutionHarness::dispatchFocusedUserFormControl(
    RuntimeErrorCode& error, bool* defaultPrevented)
{
    error = RuntimeErrorCode::None;
    if (!loaded_ || focusedElementSerial_ == 0) {
        error = RuntimeErrorCode::StaleHostObject;
        if (defaultPrevented != nullptr) *defaultPrevented = false;
        return false;
    }
    if (defaultPrevented != nullptr) *defaultPrevented = false;
    bool changed = false;
    if (!adapter_.setFormControlFromUser(focusedElementSerial_, changed)) {
        error = RuntimeErrorCode::StaleHostObject;
        return false;
    }
    if (!changed) return true;
    if (!adapter_.dispatchInputEvent(runtime_, focusedElementSerial_, error))
        return false;
    if (!adapter_.dispatchChangeEvent(runtime_, focusedElementSerial_, error))
        return false;
    return true;
}

bool NavigatorScriptExecutionHarness::relayout()
{
    if (!loaded_) return false;
    if (!document_.layoutDirty && document_.layoutRevision != 0) return true;
    gxos::web::recomputeDocumentStyles(document_);
    std::size_t extent = 0;
    for (const gxos::web::DocBlock& block : document_.blocks)
        extent = std::max(extent, block.text.size());
    document_.layoutTextExtent = std::min<std::size_t>(8192u, extent);
    document_.layoutDirty = false;
    ++document_.layoutRevision;
    return true;
}

} // namespace javascript
} // namespace gxos
