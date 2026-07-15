// guide_web_html_parser.cpp
//
// Minimal, forgiving HTML-subset parser for guideXOS.
// See guide_web_html_parser.h for the public API and tag support table.
//
// Implementation strategy
// -----------------------
// Single-pass character scanner.  State machine tracks:
//   - whether we are inside a tag token
//   - which "block tag" is currently open  (h1/h2/h3/p/a/li/title)
//   - whether we are inside a skip-content block  (script/style)
// Text accumulates in a string buffer; a flush() call converts the buffer
// to a DocBlock of the appropriate type and clears it.
//
// No dynamic_cast, no exceptions, no new library dependencies.

#include "guide_web_html_parser.h"
#if !defined(GXOS_BARE_METAL)
#include "guide_web_http.h"
#endif
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace gxos {
namespace web {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

	constexpr size_t kCssLiteMaxStyleBytes = 16u * 1024u;
	constexpr int kCssLiteMaxSpacingPx = 256;
	constexpr int kCssLiteMaxFontSizePx = 72;
	constexpr int kCssLiteMaxLineHeightPx = 96;
	constexpr int kCssLiteMaxWidthPx = 2048;
	constexpr int kCssLiteMaxBorderWidthPx = 12;
	constexpr size_t kCssLiteMaxStyleBlocks = 32;
	constexpr size_t kCssLiteMaxRules = 256;
	constexpr size_t kCssLiteMaxSelectorGroups = 16;
	constexpr size_t kCssLiteMaxSelectorLength = 256;
	constexpr size_t kCssLiteMaxSelectorComponents = 8;
	constexpr size_t kCssLiteMaxSelectorClasses = 8;
	constexpr size_t kCssLiteMaxCombinatorDepth = 6;
	constexpr size_t kCssLiteMaxDeclarationsPerRule = 64;
	constexpr size_t kCssLiteMaxTotalDeclarations = 2048;
	constexpr size_t kCssLiteMaxCascadeApplicationsPerNode = 512;
	constexpr size_t kCssLiteMaxTotalStyleBytes = 64u * 1024u;
	constexpr size_t kCssLiteMaxInheritanceDepth = 12;
	constexpr size_t kCssLiteMaxEvidenceEntries = 16;

	enum class CssProperty : uint8_t {
		Color = 0,
		Background,
		Bold,
		Italic,
		TextDecoration,
		Display,
		ListStyle,
		BorderCollapse,
		BorderSpacingHorizontal,
		BorderSpacingVertical,
		GenericFontFamily,
		TextAlign,
		LineHeight,
		MarginTop,
		MarginRight,
		MarginBottom,
		MarginLeft,
		PaddingTop,
		PaddingRight,
		PaddingBottom,
		PaddingLeft,
		FontSize,
		Width,
		Height,
		MaxWidth,
		MaxHeight,
		WhiteSpace,
		OverflowWrap,
		WordBreak,
		BorderTopWidth,
		BorderTopStyle,
		BorderTopColor,
		BorderRightWidth,
		BorderRightStyle,
		BorderRightColor,
		BorderBottomWidth,
		BorderBottomStyle,
		BorderBottomColor,
		BorderLeftWidth,
		BorderLeftStyle,
		BorderLeftColor,
		Count,
	};

	constexpr uint64_t cssPropertyBit(CssProperty property)
	{
		return uint64_t(1) << static_cast<unsigned>(property);
	}

	constexpr uint64_t cssPropertyMask(CssProperty first, CssProperty last)
	{
		uint64_t mask = 0;
		for (unsigned i = static_cast<unsigned>(first); i <= static_cast<unsigned>(last); ++i)
			mask |= uint64_t(1) << i;
		return mask;
	}

	static bool cssAccepted(WebStyle& style, uint64_t properties, bool important)
	{
		style.specifiedProperties |= properties;
		if (important) style.importantProperties |= properties;
		return true;
	}

	static void saturatingIncrement(int& value, int amount = 1)
	{
		if (amount <= 0 || value >= std::numeric_limits<int>::max() - amount) {
			value = std::numeric_limits<int>::max();
			return;
		}
		value += amount;
	}

// ASCII lower-case without locale dependency.
static std::string toLower(const std::string& s)
{
	std::string out(s.size(), '\0');
	for (size_t i = 0; i < s.size(); ++i)
		out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
	return out;
}

// Trim leading/trailing ASCII whitespace (space, tab, CR, LF).
static std::string trim(const std::string& s)
{
	size_t a = 0;
	while (a < s.size() && static_cast<unsigned char>(s[a]) <= 32) ++a;
	size_t b = s.size();
	while (b > a && static_cast<unsigned char>(s[b - 1]) <= 32) --b;
	return s.substr(a, b - a);
}

// Collapse runs of whitespace (space/tab/cr/lf) to a single space.
static std::string collapseWs(const std::string& s)
{
	std::string out;
	out.reserve(s.size());
	bool inWs = true; // leading ws suppressed
	for (unsigned char c : s) {
		if (c <= 32) {
			if (!inWs) { out += ' '; inWs = true; }
		} else {
			out += static_cast<char>(c);
			inWs = false;
		}
	}
	// trim trailing space added above
	if (!out.empty() && out.back() == ' ') out.pop_back();
	return out;
}

// Decode minimal HTML entities: &amp; &lt; &gt; &quot; &apos; &#nn;
static std::string decodeEntities(const std::string& s)
{
	std::string out;
	out.reserve(s.size());
	size_t i = 0;
	while (i < s.size()) {
		if (s[i] != '&') { out += s[i++]; continue; }
		size_t semi = s.find(';', i + 1);
		if (semi == std::string::npos || semi - i > 10) { out += s[i++]; continue; }
		std::string ent = s.substr(i + 1, semi - i - 1);
		std::string entLo = toLower(ent);
		if      (entLo == "amp")  out += '&';
		else if (entLo == "lt")   out += '<';
		else if (entLo == "gt")   out += '>';
		else if (entLo == "quot") out += '"';
		else if (entLo == "apos") out += '\'';
		else if (entLo == "nbsp") out += ' ';
		else if (entLo == "ndash" || entLo == "mdash") out += '-';
		else if (entLo == "hellip") { out += '.'; out += '.'; out += '.'; }
		else if (entLo == "laquo") { out += '<'; out += '<'; }
		else if (entLo == "raquo") { out += '>'; out += '>'; }
		else if (entLo == "copy") out += '('; // (C)
		else if (entLo == "reg")  out += '('; // (R)
		else if (!ent.empty() && ent[0] == '#') {
			try {
				int cp = (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X'))
					? std::stoi(ent.substr(2), nullptr, 16)
					: std::stoi(ent.substr(1));
				if (cp > 0 && cp < 128) out += static_cast<char>(cp);
			} catch (...) { out += '&'; out += ent; out += ';'; }
		} else { out += '&'; out += ent; out += ';'; }
		i = semi + 1;
	}
	return out;
}

// Extract the value of an attribute from a raw tag body string.
// E.g.  extractAttr("a href=\"foo.html\" id=\"x\"", "href")  ->  "foo.html"
static std::string extractAttr(const std::string& tagBody, const std::string& attr)
{
	std::string body = toLower(tagBody);
	std::string key  = toLower(attr);
	size_t pos = body.find(key + "=");
	if (pos == std::string::npos) return "";
	pos += key.size() + 1; // skip "attr="
	if (pos >= body.size()) return "";

	// Use the raw tagBody (original case) for the value substring
	// but find the position in the original string.
	size_t rawPos = pos; // positions match because we only lowercased
	char delim = tagBody[rawPos];
	if (delim == '"' || delim == '\'') {
		size_t end = tagBody.find(delim, rawPos + 1);
		if (end == std::string::npos) return tagBody.substr(rawPos + 1);
		return tagBody.substr(rawPos + 1, end - rawPos - 1);
	}
	// unquoted value: read until whitespace or '>'
	size_t end = rawPos;
	while (end < tagBody.size() && tagBody[end] != ' ' && tagBody[end] != '>' && tagBody[end] != '\t')
		++end;
	return tagBody.substr(rawPos, end - rawPos);
}

static bool hasAttr(const std::string& tagBody, const std::string& attr)
{
	std::string body = toLower(tagBody);
	std::string key = toLower(attr);
	size_t pos = 0;
	while ((pos = body.find(key, pos)) != std::string::npos) {
		bool leftOk = (pos == 0) || std::isspace(static_cast<unsigned char>(body[pos - 1])) || body[pos - 1] == '<';
		size_t end = pos + key.size();
		bool rightOk = (end >= body.size()) ||
			std::isspace(static_cast<unsigned char>(body[end])) ||
			body[end] == '=' || body[end] == '/' || body[end] == '>';
		if (leftOk && rightOk) return true;
		pos = end;
	}
	return false;
}

static bool supportedFormMethod(const std::string& method)
{
	return method == "get" || method == "post";
}

static bool supportedFormEncoding(const std::string& encoding)
{
	return encoding.empty() || encoding == "application/x-www-form-urlencoded";
}

static int parsePositiveIntAttr(const std::string& tagBody, const std::string& attr, bool* wasClamped = nullptr)
{
	std::string value = trim(extractAttr(tagBody, attr));
	if (wasClamped) *wasClamped = false;
	if (value.empty()) return 0;
	int result = 0;
	bool sawDigit = false;
	for (char c : value) {
		if (c < '0' || c > '9') break;
		sawDigit = true;
		const int digit = c - '0';
		if (result > 409) {
			if (wasClamped) *wasClamped = true;
			return 4096;
		}
		result = result * 10 + digit;
		if (result > 4096) {
			if (wasClamped) *wasClamped = true;
			return 4096;
		}
	}
	if (!sawDigit) return 0;
	return result > 0 ? result : 0;
}

static bool isHexDigit(char c)
{
	return (c >= '0' && c <= '9') ||
		(c >= 'a' && c <= 'f') ||
		(c >= 'A' && c <= 'F');
}

static int hexDigitValue(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
	if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
	return 0;
}

static bool parseCssColor(const std::string& rawValue, uint32_t& outColor)
{
	std::string value = toLower(trim(rawValue));
	if (value.empty()) return false;
	if (value == "inherit" || value == "initial") return false;
	if (value == "transparent") {
		outColor = 0;
		return true;
	}
	if (value.size() == 7 && value[0] == '#') {
		for (size_t i = 1; i < value.size(); ++i) {
			if (!isHexDigit(value[i])) return false;
		}
		int r = hexDigitValue(value[1]) * 16 + hexDigitValue(value[2]);
		int g = hexDigitValue(value[3]) * 16 + hexDigitValue(value[4]);
		int b = hexDigitValue(value[5]) * 16 + hexDigitValue(value[6]);
		outColor = 0xFF000000u | (static_cast<uint32_t>(r) << 16) |
			(static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
		return true;
	}
	if (value.size() == 4 && value[0] == '#') {
		for (size_t i = 1; i < value.size(); ++i) {
			if (!isHexDigit(value[i])) return false;
		}
		int r = hexDigitValue(value[1]); r = r * 16 + r;
		int g = hexDigitValue(value[2]); g = g * 16 + g;
		int b = hexDigitValue(value[3]); b = b * 16 + b;
		outColor = 0xFF000000u | (static_cast<uint32_t>(r) << 16) |
			(static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
		return true;
	}
	if (value == "black") { outColor = 0xFF000000u; return true; }
	if (value == "white") { outColor = 0xFFFFFFFFu; return true; }
	if (value == "red")   { outColor = 0xFFFF0000u; return true; }
	if (value == "green") { outColor = 0xFF008000u; return true; }
	if (value == "blue")  { outColor = 0xFF0000FFu; return true; }
	if (value == "gray" || value == "grey") { outColor = 0xFF808080u; return true; }
	if (value.rfind("rgb(", 0) == 0 && value.back() == ')') {
		std::string inner = value.substr(4, value.size() - 5);
		std::stringstream ss(inner);
		std::string part;
		int channels[3] = { 0, 0, 0 };
		int index = 0;
		while (std::getline(ss, part, ',') && index < 3) {
			try {
				channels[index] = std::max(0, std::min(255, std::stoi(trim(part))));
			} catch (...) {
				return false;
			}
			++index;
		}
		if (index == 3) {
			outColor = 0xFF000000u | (static_cast<uint32_t>(channels[0]) << 16) |
				(static_cast<uint32_t>(channels[1]) << 8) | static_cast<uint32_t>(channels[2]);
			return true;
		}
	}
	return false;
}

static void parseCssDeclarations(const std::string& body, WebStyle& style, CssDiagnostics& diag);
static uint32_t allocateCssSourceOrder(CssDiagnostics& diag);

static bool parseCssNumber(const std::string& rawValue, double& out)
{
	std::string value = toLower(trim(rawValue));
	if (value.empty()) return false;
	try {
		size_t consumed = 0;
		out = std::stod(value, &consumed);
		return consumed == value.size();
	} catch (...) {
		return false;
	}
}

static int roundCssNumber(double value)
{
	return value <= 0.0 ? 0 : static_cast<int>(value + 0.5);
}

static int clampCssValue(CssDiagnostics& diag, int value, int minValue, int maxValue)
{
	const int clamped = std::max(minValue, std::min(maxValue, value));
	if (clamped != value) {
		++diag.clampedValueCount;
	}
	return clamped;
}

static int clampBorderWidthPx(CssDiagnostics& diag, int value)
{
	const int clamped = clampCssValue(diag, std::max(0, value), 0, kCssLiteMaxBorderWidthPx);
	if (clamped != std::max(0, value)) {
		++diag.borderWidthClampCount;
	}
	return clamped;
}

static int clampBorderSpacingPx(CssDiagnostics& diag, int value)
{
	const int clamped = clampCssValue(diag, std::max(0, value), 0, kCssLiteMaxSpacingPx);
	if (clamped != std::max(0, value)) {
		++diag.borderSpacingClampCount;
	}
	return clamped;
}

static bool parseCssLengthValue(const std::string& rawValue,
	int basePx,
	int& outPx,
	bool& outAuto,
	bool allowPercent = false)
{
	std::string value = toLower(trim(rawValue));
	outAuto = false;
	outPx = 0;
	if (value.empty()) return false;
	if (value == "auto") {
		outAuto = true;
		return true;
	}
	double numeric = 0.0;
	double scale = 1.0;
	if (value.size() >= 2 && value.substr(value.size() - 2) == "px") {
		value = trim(value.substr(0, value.size() - 2));
		scale = 1.0;
	} else if (value.size() >= 2 && value.substr(value.size() - 2) == "em") {
		value = trim(value.substr(0, value.size() - 2));
		scale = 16.0;
	} else if (value.size() >= 2 && value.substr(value.size() - 2) == "pt") {
		value = trim(value.substr(0, value.size() - 2));
		scale = 96.0 / 72.0;
	} else if (value.size() >= 1 && value.back() == '%') {
		if (!allowPercent) return false;
		value.pop_back();
		scale = static_cast<double>(basePx) / 100.0;
	} else if (value == "thin") {
		outPx = 1;
		return true;
	} else if (value == "medium") {
		outPx = 2;
		return true;
	} else if (value == "thick") {
		outPx = 3;
		return true;
	}
	if (!parseCssNumber(value, numeric)) return false;
	outPx = roundCssNumber(numeric * scale);
	return true;
}

static bool parseCssSimpleSelectorPart(const std::string& rawPart,
	CssSelectorPart& part,
	CssSpecificity& specificity,
	CssDiagnostics& diag)
{
	std::string selector = toLower(trim(rawPart));
	part = {};
	specificity = {};
	if (selector.empty() || selector.find_first_of("+~[]:") != std::string::npos) return false;

	size_t pos = 0;
	auto isIdentChar = [](char c) {
		return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_';
	};
	if (selector[pos] != '.' && selector[pos] != '#') {
		size_t start = pos;
		while (pos < selector.size() && selector[pos] != '.' && selector[pos] != '#') {
			if (selector[pos] == '*') {
				if (pos != start || selector.size() != 1) return false;
				++pos;
				break;
			}
			if (!isIdentChar(selector[pos])) return false;
			++pos;
		}
		part.tagName = selector.substr(start, pos - start);
		if (part.tagName != "*" && !part.tagName.empty()) ++specificity.elementCount;
	}
	while (pos < selector.size()) {
		char prefix = selector[pos];
		if (prefix != '.' && prefix != '#') return false;
		++pos;
		size_t start = pos;
		while (pos < selector.size() && selector[pos] != '.' && selector[pos] != '#') {
			if (!isIdentChar(selector[pos])) return false;
			++pos;
		}
		std::string token = selector.substr(start, pos - start);
		if (token.empty()) return false;
		if (prefix == '.') {
			if (part.classNames.size() >= kCssLiteMaxSelectorClasses) {
				++diag.selectorDepthClamps;
				return false;
			}
			part.classNames.push_back(token);
			++specificity.classCount;
		} else {
			if (!part.id.empty()) return false;
			part.id = token;
			++specificity.idCount;
		}
	}
	if (part.tagName.empty() && part.classNames.empty() && part.id.empty()) return false;
	return true;
}

static bool parseCssSelector(const std::string& rawSelector, WebStyleRule& outRule, CssDiagnostics& diag)
{
	std::string selectorText = trim(rawSelector);
	if (selectorText.empty() || selectorText.size() > kCssLiteMaxSelectorLength) return false;
	outRule.selectorParts.clear();
	outRule.combinators.clear();
	outRule.specificity = 0;
	outRule.specificityTuple = {};
	size_t cursor = 0;
	bool needCompound = true;
	while (cursor < selectorText.size()) {
		while (cursor < selectorText.size() && std::isspace(static_cast<unsigned char>(selectorText[cursor]))) ++cursor;
		if (cursor >= selectorText.size()) break;
		if (selectorText[cursor] == '+' || selectorText[cursor] == '~' || selectorText[cursor] == '[' || selectorText[cursor] == ':') return false;
		size_t start = cursor;
		while (cursor < selectorText.size() &&
			!std::isspace(static_cast<unsigned char>(selectorText[cursor])) &&
			selectorText[cursor] != '>') ++cursor;
		if (start == cursor) return false;
		if (outRule.selectorParts.size() >= kCssLiteMaxSelectorComponents) {
			++diag.selectorDepthClamps;
			return false;
		}
		CssSelectorPart part;
		CssSpecificity partSpecificity;
		if (!parseCssSimpleSelectorPart(selectorText.substr(start, cursor - start), part, partSpecificity, diag)) return false;
		outRule.selectorParts.push_back(std::move(part));
		outRule.specificityTuple.idCount = static_cast<uint16_t>(std::min<uint32_t>(
			std::numeric_limits<uint16_t>::max(),
			static_cast<uint32_t>(outRule.specificityTuple.idCount) + partSpecificity.idCount));
		outRule.specificityTuple.classCount = static_cast<uint16_t>(std::min<uint32_t>(
			std::numeric_limits<uint16_t>::max(),
			static_cast<uint32_t>(outRule.specificityTuple.classCount) + partSpecificity.classCount));
		outRule.specificityTuple.elementCount = static_cast<uint16_t>(std::min<uint32_t>(
			std::numeric_limits<uint16_t>::max(),
			static_cast<uint32_t>(outRule.specificityTuple.elementCount) + partSpecificity.elementCount));
		needCompound = false;
		bool hadWhitespace = false;
		while (cursor < selectorText.size() && std::isspace(static_cast<unsigned char>(selectorText[cursor]))) {
			hadWhitespace = true;
			++cursor;
		}
		if (cursor >= selectorText.size()) break;
		if (selectorText[cursor] == '>') {
			++cursor;
			while (cursor < selectorText.size() && std::isspace(static_cast<unsigned char>(selectorText[cursor]))) ++cursor;
			if (cursor >= selectorText.size() || selectorText[cursor] == '>') return false;
			outRule.combinators.push_back(CssCombinator::Child);
			saturatingIncrement(diag.childCombinatorCount);
		} else if (hadWhitespace) {
			outRule.combinators.push_back(CssCombinator::Descendant);
			saturatingIncrement(diag.descendantCombinatorCount);
		} else {
			return false;
		}
		if (outRule.combinators.size() > kCssLiteMaxCombinatorDepth) {
			++diag.selectorDepthClamps;
			return false;
		}
	}
	if (outRule.selectorParts.empty() || needCompound ||
		outRule.combinators.size() + 1 != outRule.selectorParts.size()) return false;
	outRule.specificity = static_cast<int>(std::min<uint32_t>(
		std::numeric_limits<int>::max(),
		static_cast<uint32_t>(outRule.specificityTuple.idCount) * 100u +
		static_cast<uint32_t>(outRule.specificityTuple.classCount) * 10u +
		static_cast<uint32_t>(outRule.specificityTuple.elementCount)));
	outRule.selector = toLower(selectorText);
	const CssSelectorPart& last = outRule.selectorParts.back();
	if (!last.id.empty()) outRule.selectorType = StyleSelectorType::Id;
	else if (!last.classNames.empty() || !last.tagName.empty()) {
		outRule.selectorType = last.classNames.empty() ? StyleSelectorType::Element : StyleSelectorType::Class;
	}
	return true;
}

static std::vector<std::string> splitCssTokens(const std::string& text)
{
	std::vector<std::string> tokens;
	std::stringstream ss(text);
	std::string token;
	while (ss >> token) tokens.push_back(token);
	return tokens;
}

static bool applyLengthList(WebStyle& style,
	const std::vector<std::string>& values,
	const std::string& property,
	CssDiagnostics& diag)
{
	if (values.empty()) return false;
	int parsed[4] = { -1, -1, -1, -1 };
	bool autos[4] = { false, false, false, false };
	for (size_t i = 0; i < values.size() && i < 4; ++i) {
		const bool allowAuto = property == "margin";
		if (!parseCssLengthValue(values[i], 320, parsed[i], autos[i], allowAuto)) {
			++diag.unsupportedDeclarationCount;
			return false;
		}
		if (autos[i]) {
			parsed[i] = -2;
		} else {
			parsed[i] = clampCssValue(diag, parsed[i], 0, kCssLiteMaxSpacingPx);
		}
	}
	if (property == "margin") {
		if (values.size() == 1) {
			style.marginTop = parsed[0];
			style.marginRight = parsed[0];
			style.marginBottom = parsed[0];
			style.marginLeft = parsed[0];
		} else if (values.size() == 2) {
			style.marginTop = parsed[0];
			style.marginBottom = parsed[0];
			style.marginLeft = parsed[1];
			style.marginRight = parsed[1];
		} else if (values.size() == 3) {
			style.marginTop = parsed[0];
			style.marginLeft = parsed[1];
			style.marginRight = parsed[1];
			style.marginBottom = parsed[2];
		} else {
			style.marginTop = parsed[0];
			style.marginRight = parsed[1];
			style.marginBottom = parsed[2];
			style.marginLeft = parsed[3];
		}
		return true;
	}
	if (property == "padding") {
		if (values.size() == 1) {
			style.padding = parsed[0];
			style.paddingTop = parsed[0];
			style.paddingRight = parsed[0];
			style.paddingBottom = parsed[0];
			style.paddingLeft = parsed[0];
		} else if (values.size() == 2) {
			style.paddingTop = parsed[0];
			style.paddingBottom = parsed[0];
			style.paddingLeft = parsed[1];
			style.paddingRight = parsed[1];
		} else if (values.size() == 3) {
			style.paddingTop = parsed[0];
			style.paddingLeft = parsed[1];
			style.paddingRight = parsed[1];
			style.paddingBottom = parsed[2];
		} else {
			style.paddingTop = parsed[0];
			style.paddingRight = parsed[1];
			style.paddingBottom = parsed[2];
			style.paddingLeft = parsed[3];
		}
		return true;
	}
	return false;
}

enum class BorderSideIndex : uint8_t {
	Top = 0,
	Right = 1,
	Bottom = 2,
	Left = 3,
};

struct BorderSideValue {
	bool hasWidth = false;
	int width = 0;
	bool hasColor = false;
	uint32_t color = 0;
	bool hasStyle = false;
	BorderLineStyle style = BorderLineStyle::Inherit;
	bool sawRecognized = false;
};

static bool parseBorderStyleToken(const std::string& token, BorderLineStyle& style)
{
	const std::string lower = toLower(trim(token));
	if (lower == "none") {
		style = BorderLineStyle::None;
		return true;
	}
	if (lower == "hidden") {
		style = BorderLineStyle::Hidden;
		return true;
	}
	if (lower == "solid") {
		style = BorderLineStyle::Solid;
		return true;
	}
	if (lower == "dashed") {
		style = BorderLineStyle::Dashed;
		return true;
	}
	if (lower == "dotted") {
		style = BorderLineStyle::Dotted;
		return true;
	}
	return false;
}

static std::vector<std::string> splitCssCommaTokens(const std::string& text)
{
	std::vector<std::string> tokens;
	std::string current;
	bool inSingle = false;
	bool inDouble = false;
	for (char c : text) {
		if (c == '\'' && !inDouble) {
			inSingle = !inSingle;
			current += c;
			continue;
		}
		if (c == '"' && !inSingle) {
			inDouble = !inDouble;
			current += c;
			continue;
		}
		if (c == ',' && !inSingle && !inDouble) {
			tokens.push_back(trim(current));
			current.clear();
			continue;
		}
		current += c;
	}
	if (!current.empty() || !tokens.empty()) {
		tokens.push_back(trim(current));
	}
	return tokens;
}

static std::string stripCssQuotes(const std::string& text)
{
	std::string value = trim(text);
	if (value.size() >= 2) {
		const char first = value.front();
		const char last = value.back();
		if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
			return value.substr(1, value.size() - 2);
		}
	}
	return value;
}

static bool parseGenericFontFamily(const std::string& rawValue, GenericFontFamily& outFamily)
{
	const std::string value = toLower(trim(stripCssQuotes(rawValue)));
	if (value == "sans-serif") {
		outFamily = GenericFontFamily::SansSerif;
		return true;
	}
	if (value == "serif") {
		outFamily = GenericFontFamily::Serif;
		return true;
	}
	if (value == "monospace") {
		outFamily = GenericFontFamily::Monospace;
		return true;
	}
	return false;
}

static bool parseBorderSideValue(const std::string& rawValue, BorderSideValue& out, CssDiagnostics& diag)
{
	out = {};
	out.width = 1;
	out.color = 0xFF000000u;
	std::vector<std::string> tokens = splitCssTokens(rawValue);
	if (tokens.empty()) return false;
	for (const std::string& token : tokens) {
		const std::string lower = toLower(token);
		BorderLineStyle style = BorderLineStyle::Inherit;
		bool tokenRecognized = false;
		if (parseBorderStyleToken(lower, style)) {
			out.hasStyle = true;
			out.style = style;
			tokenRecognized = true;
			if (style == BorderLineStyle::None || style == BorderLineStyle::Hidden) {
				if (!out.hasWidth) {
					out.width = 0;
				}
			}
		} else {
			bool autoValue = false;
			int px = 0;
			if (parseCssLengthValue(token, 16, px, autoValue, false) && !autoValue) {
				out.hasWidth = true;
				out.width = clampBorderWidthPx(diag, px);
				tokenRecognized = true;
			} else {
				uint32_t color = 0;
				if (parseCssColor(token, color)) {
					out.hasColor = true;
					out.color = color;
					tokenRecognized = true;
				}
			}
		}
		if (tokenRecognized) {
			out.sawRecognized = true;
			continue;
		}
		++diag.unsupportedDeclarationCount;
	}
	if (!out.sawRecognized) return false;
	return true;
}

static void applyBorderSideValue(WebStyle& style, BorderSideIndex side, const BorderSideValue& value)
{
	switch (side) {
	case BorderSideIndex::Top:
		style.hasBorderTop = true;
		if (value.hasWidth) style.borderTopWidth = value.width;
		if (value.hasColor) style.borderTopColor = value.color;
		if (value.hasStyle) style.borderTopStyle = value.style;
		break;
	case BorderSideIndex::Right:
		style.hasBorderRight = true;
		if (value.hasWidth) style.borderRightWidth = value.width;
		if (value.hasColor) style.borderRightColor = value.color;
		if (value.hasStyle) style.borderRightStyle = value.style;
		break;
	case BorderSideIndex::Bottom:
		style.hasBorderBottom = true;
		if (value.hasWidth) style.borderBottomWidth = value.width;
		if (value.hasColor) style.borderBottomColor = value.color;
		if (value.hasStyle) style.borderBottomStyle = value.style;
		break;
	case BorderSideIndex::Left:
		style.hasBorderLeft = true;
		if (value.hasWidth) style.borderLeftWidth = value.width;
		if (value.hasColor) style.borderLeftColor = value.color;
		if (value.hasStyle) style.borderLeftStyle = value.style;
		break;
	}
}

static BorderSideValue borderSideValueFromStyle(const WebStyle& style, BorderSideIndex side)
{
	BorderSideValue value;
	switch (side) {
	case BorderSideIndex::Top:
		value.hasWidth = style.hasBorderTop;
		value.width = style.borderTopWidth;
		value.hasColor = style.hasBorderTop;
		value.color = style.borderTopColor;
		value.hasStyle = style.hasBorderTop;
		value.style = style.borderTopStyle;
		break;
	case BorderSideIndex::Right:
		value.hasWidth = style.hasBorderRight;
		value.width = style.borderRightWidth;
		value.hasColor = style.hasBorderRight;
		value.color = style.borderRightColor;
		value.hasStyle = style.hasBorderRight;
		value.style = style.borderRightStyle;
		break;
	case BorderSideIndex::Bottom:
		value.hasWidth = style.hasBorderBottom;
		value.width = style.borderBottomWidth;
		value.hasColor = style.hasBorderBottom;
		value.color = style.borderBottomColor;
		value.hasStyle = style.hasBorderBottom;
		value.style = style.borderBottomStyle;
		break;
	case BorderSideIndex::Left:
		value.hasWidth = style.hasBorderLeft;
		value.width = style.borderLeftWidth;
		value.hasColor = style.hasBorderLeft;
		value.color = style.borderLeftColor;
		value.hasStyle = style.hasBorderLeft;
		value.style = style.borderLeftStyle;
		break;
	}
	value.sawRecognized = value.hasWidth || value.hasColor || value.hasStyle;
	return value;
}

static bool applyBorderWidthListToSides(WebStyle& style, const std::vector<std::string>& values, CssDiagnostics& diag)
{
	if (values.empty() || values.size() > 4) return false;
	int widths[4] = { 0, 0, 0, 0 };
	for (size_t i = 0; i < values.size(); ++i) {
		bool autoValue = false;
		int px = 0;
		if (!parseCssLengthValue(values[i], 16, px, autoValue, false) || autoValue) {
			++diag.unsupportedDeclarationCount;
			return false;
		}
		widths[i] = clampBorderWidthPx(diag, px);
	}
	if (values.size() == 1) {
		widths[1] = widths[2] = widths[3] = widths[0];
	} else if (values.size() == 2) {
		widths[2] = widths[0];
		widths[3] = widths[1];
	} else if (values.size() == 3) {
		widths[3] = widths[1];
	}
	style.hasBorderTop = style.hasBorderRight = style.hasBorderBottom = style.hasBorderLeft = true;
	style.borderTopWidth = widths[0];
	style.borderRightWidth = widths[1];
	style.borderBottomWidth = widths[2];
	style.borderLeftWidth = widths[3];
	return true;
}

static bool applyBorderStyleListToSides(WebStyle& style, const std::vector<std::string>& values, CssDiagnostics& diag)
{
	if (values.empty() || values.size() > 4) return false;
	BorderLineStyle styles[4] = {
		BorderLineStyle::Inherit,
		BorderLineStyle::Inherit,
		BorderLineStyle::Inherit,
		BorderLineStyle::Inherit,
	};
	for (size_t i = 0; i < values.size(); ++i) {
		BorderLineStyle borderStyle = BorderLineStyle::Inherit;
		if (!parseBorderStyleToken(values[i], borderStyle)) {
			++diag.unsupportedDeclarationCount;
			return false;
		}
		styles[i] = borderStyle;
	}
	if (values.size() == 1) {
		styles[1] = styles[2] = styles[3] = styles[0];
	} else if (values.size() == 2) {
		styles[2] = styles[0];
		styles[3] = styles[1];
	} else if (values.size() == 3) {
		styles[3] = styles[1];
	}
	style.hasBorderTop = style.hasBorderRight = style.hasBorderBottom = style.hasBorderLeft = true;
	style.borderTopStyle = styles[0];
	style.borderRightStyle = styles[1];
	style.borderBottomStyle = styles[2];
	style.borderLeftStyle = styles[3];
	return true;
}

static bool applyBorderColorListToSides(WebStyle& style, const std::vector<std::string>& values, CssDiagnostics& diag)
{
	if (values.empty() || values.size() > 4) return false;
	uint32_t colors[4] = { 0xFF000000u, 0xFF000000u, 0xFF000000u, 0xFF000000u };
	for (size_t i = 0; i < values.size(); ++i) {
		uint32_t color = 0;
		if (!parseCssColor(values[i], color)) {
			++diag.unsupportedDeclarationCount;
			return false;
		}
		colors[i] = color;
	}
	if (values.size() == 1) {
		colors[1] = colors[2] = colors[3] = colors[0];
	} else if (values.size() == 2) {
		colors[2] = colors[0];
		colors[3] = colors[1];
	} else if (values.size() == 3) {
		colors[3] = colors[1];
	}
	style.hasBorderTop = style.hasBorderRight = style.hasBorderBottom = style.hasBorderLeft = true;
	style.borderTopColor = colors[0];
	style.borderRightColor = colors[1];
	style.borderBottomColor = colors[2];
	style.borderLeftColor = colors[3];
	return true;
}

static bool applyBorderSpacingList(WebStyle& style, const std::vector<std::string>& values, CssDiagnostics& diag)
{
	if (values.empty() || values.size() > 2) return false;
	int spacing[2] = { 0, 0 };
	for (size_t i = 0; i < values.size(); ++i) {
		bool autoValue = false;
		int px = 0;
		if (!parseCssLengthValue(values[i], 16, px, autoValue, false) || autoValue) {
			++diag.unsupportedDeclarationCount;
			return false;
		}
		spacing[i] = clampBorderSpacingPx(diag, px);
	}
	if (values.size() == 1) {
		spacing[1] = spacing[0];
	}
	style.borderSpacingHorizontal = spacing[0];
	style.borderSpacingVertical = spacing[1];
	return true;
}

static BorderLineStyle defaultVisibleBorderStyle(const WebStyle& style, BorderSideIndex side)
{
	BorderSideValue value = borderSideValueFromStyle(style, side);
	if (!value.hasStyle || value.style == BorderLineStyle::Inherit) {
		return value.hasWidth && value.width > 0 ? BorderLineStyle::Solid : BorderLineStyle::None;
	}
	return value.style;
}

static void parseEmbeddedCss(WebDocument& doc, const std::string& cssText)
{
	if (cssText.empty()) return;
	doc.cssDiagnostics.cssDetected = true;
	if (doc.cssDiagnostics.styleBlockCount >= static_cast<int>(kCssLiteMaxStyleBlocks)) {
		doc.cssDiagnostics.styleBlockCapped = true;
		saturatingIncrement(doc.cssDiagnostics.ruleCapCount);
		return;
	}
	++doc.cssDiagnostics.styleBlockCount;
	std::string css = cssText;
	const size_t totalRemaining = doc.cssDiagnostics.styleBytesProcessed >= kCssLiteMaxTotalStyleBytes
		? 0 : kCssLiteMaxTotalStyleBytes - doc.cssDiagnostics.styleBytesProcessed;
	const size_t byteLimit = std::min(kCssLiteMaxStyleBytes, totalRemaining);
	if (css.size() > byteLimit) {
		css.resize(byteLimit);
		doc.cssDiagnostics.styleBlockCapped = true;
	}
	doc.cssDiagnostics.styleBytesProcessed += css.size();
	if (css.empty()) return;
	auto stripCssComments = [](const std::string& input) {
		std::string out;
		out.reserve(input.size());
		for (size_t i = 0; i < input.size(); ++i) {
			if (i + 1 < input.size() && input[i] == '/' && input[i + 1] == '*') {
				i += 2;
				while (i + 1 < input.size() && !(input[i] == '*' && input[i + 1] == '/')) ++i;
				if (i + 1 < input.size()) ++i;
				continue;
			}
			out += input[i];
		}
		return out;
	};
	css = stripCssComments(css);
	auto stripAtRules = [](const std::string& input) {
		std::string out;
		out.reserve(input.size());
		size_t i = 0;
		while (i < input.size()) {
			if (input[i] == '@') {
				size_t semi = input.find(';', i);
				size_t brace = input.find('{', i);
				if (semi != std::string::npos && (brace == std::string::npos || semi < brace)) {
					i = semi + 1;
					continue;
				}
				if (brace != std::string::npos) {
					int depth = 1;
					i = brace + 1;
					while (i < input.size() && depth > 0) {
						if (input[i] == '{') ++depth;
						else if (input[i] == '}') --depth;
						++i;
					}
					continue;
				}
			}
			out += input[i++];
		}
		return out;
	};
	css = stripAtRules(css);

	size_t cursor = 0;
	while (cursor < css.size()) {
		size_t brace = css.find('{', cursor);
		if (brace == std::string::npos) {
			if (trim(css.substr(cursor)).size() > 0) ++doc.cssDiagnostics.parseErrorCount;
			break;
		}
		size_t endBrace = css.find('}', brace + 1);
		if (endBrace == std::string::npos) {
			++doc.cssDiagnostics.parseErrorCount;
			break;
		}
		std::string selectorText = trim(css.substr(cursor, brace - cursor));
		std::string bodyText = css.substr(brace + 1, endBrace - brace - 1);
		cursor = endBrace + 1;
		if (selectorText.empty()) continue;

		std::stringstream selectors(selectorText);
		std::string selector;
		size_t groupCount = 0;
		while (std::getline(selectors, selector, ',')) {
			if (groupCount >= kCssLiteMaxSelectorGroups) {
				saturatingIncrement(doc.cssDiagnostics.selectorGroupClamps);
				break;
			}
			++groupCount;
			saturatingIncrement(doc.cssDiagnostics.selectorGroupsParsed);
			selector = trim(selector);
			if (selector.empty()) continue;
			if (doc.styleRules.size() >= kCssLiteMaxRules) {
				saturatingIncrement(doc.cssDiagnostics.ruleCapCount);
				continue;
			}
			WebStyleRule rule;
			if (!parseCssSelector(selector, rule, doc.cssDiagnostics)) {
				saturatingIncrement(doc.cssDiagnostics.unsupportedRuleCount);
				saturatingIncrement(doc.cssDiagnostics.unsupportedSelectorCount);
				continue;
			}
			parseCssDeclarations(bodyText, rule.style, doc.cssDiagnostics);
			rule.sourceOrder = allocateCssSourceOrder(doc.cssDiagnostics);
			doc.styleRules.push_back(rule);
			saturatingIncrement(doc.cssDiagnostics.styleRuleCount);
			saturatingIncrement(doc.cssDiagnostics.compoundSelectorsParsed, static_cast<int>(rule.selectorParts.size()));
		}
	}
}

static bool selectorPartMatchesElement(const HtmlElementRef& element, const CssSelectorPart& part)
{
	std::string tag = toLower(element.tagName);
	std::string id = toLower(element.id);
	if (!part.tagName.empty() && part.tagName != "*" && part.tagName != tag) return false;
	if (!part.id.empty() && part.id != id) return false;
	if (!part.classNames.empty()) {
		std::stringstream classes(toLower(element.className));
		std::string className;
		std::vector<std::string> classList;
		while (classes >> className) classList.push_back(className);
		for (const std::string& required : part.classNames) {
			bool found = false;
			for (const std::string& actual : classList) {
				if (actual == required) {
					found = true;
					break;
				}
			}
			if (!found) return false;
		}
	}
	return true;
}

static bool selectorMatchesPathAt(const std::vector<HtmlElementRef>& path,
	const WebStyleRule& rule,
	int partIndex,
	int pathIndex)
{
	if (partIndex < 0 || pathIndex < 0 ||
		partIndex >= static_cast<int>(rule.selectorParts.size()) ||
		pathIndex >= static_cast<int>(path.size())) return false;
	if (!selectorPartMatchesElement(path[static_cast<size_t>(pathIndex)],
		rule.selectorParts[static_cast<size_t>(partIndex)])) return false;
	if (partIndex == 0) return true;
	const CssCombinator combinator = rule.combinators[static_cast<size_t>(partIndex - 1)];
	if (combinator == CssCombinator::Child) {
		return selectorMatchesPathAt(path, rule, partIndex - 1, pathIndex - 1);
	}
	for (int ancestorIndex = pathIndex - 1; ancestorIndex >= 0; --ancestorIndex) {
		if (selectorMatchesPathAt(path, rule, partIndex - 1, ancestorIndex)) return true;
	}
	return false;
}

static bool selectorMatchesPath(const std::vector<HtmlElementRef>& path, const WebStyleRule& rule)
{
	if (rule.selectorParts.empty() || path.empty() ||
		rule.selectorParts.size() != rule.combinators.size() + 1 ||
		rule.combinators.size() > kCssLiteMaxCombinatorDepth) return false;
	return selectorMatchesPathAt(path, rule,
		static_cast<int>(rule.selectorParts.size()) - 1,
		static_cast<int>(path.size()) - 1);
}

static bool parseInlineStyleDeclaration(WebStyle& style,
	const std::string& property,
	const std::string& value,
	CssDiagnostics& diag)
{
	std::string prop = toLower(trim(property));
	std::string val = trim(value);
	if (prop.empty() || val.empty()) return false;
	bool important = false;
	if (val.size() >= 10 && toLower(val.substr(val.size() - 10)) == "!important" &&
		(val.size() == 10 || std::isspace(static_cast<unsigned char>(val[val.size() - 11])))) {
		important = true;
		val = trim(val.substr(0, val.size() - 10));
	}
	if (val.empty()) return false;
	auto accept = [&](CssProperty propertyId) {
		return cssAccepted(style, cssPropertyBit(propertyId), important);
	};
	auto acceptMask = [&](uint64_t properties) {
		return cssAccepted(style, properties, important);
	};
	if (prop == "color" || prop == "background-color" || prop == "background") {
		const std::string lower = toLower(val);
		if (lower == "transparent" || lower == "inherit" || lower == "initial" || lower == "unset" || lower == "none" || lower == "0 0") {
			if (prop != "color") {
				style.hasBackgroundColor = false;
				return accept(CssProperty::Background);
			}
			return lower == "inherit" || lower == "initial" || lower == "unset"
				? true : accept(CssProperty::Color);
		}
		uint32_t color = 0;
		if (parseCssColor(val, color)) {
			if (prop == "color") {
				style.hasColor = true;
				style.color = color;
			} else {
				style.hasBackgroundColor = true;
				style.backgroundColor = color;
			}
			return accept(prop == "color" ? CssProperty::Color : CssProperty::Background);
		}
		++diag.unsupportedDeclarationCount;
		return false;
	}
	if (prop == "font-weight") {
		std::string lower = toLower(val);
		if (lower == "bold" || lower == "700" || lower == "800" || lower == "900") {
			style.bold = true;
			return accept(CssProperty::Bold);
		}
		if (lower == "normal" || lower == "400") {
			style.bold = false;
			return accept(CssProperty::Bold);
		}
		++diag.unsupportedDeclarationCount;
		return false;
	}
	if (prop == "font-style") {
		std::string lower = toLower(val);
		if (lower == "italic" || lower == "oblique") {
			style.italic = true;
			return accept(CssProperty::Italic);
		}
		if (lower == "normal") {
			style.italic = false;
			return accept(CssProperty::Italic);
		}
		++diag.unsupportedDeclarationCount;
		return false;
	}
	if (prop == "text-decoration") {
		std::string lower = toLower(val);
		if (lower == "inherit" || lower == "initial" || lower == "unset") {
			return true;
		}
		if (lower == "none") {
			style.hasTextDecoration = true;
			style.underline = false;
			style.lineThrough = false;
			return accept(CssProperty::TextDecoration);
		}
		std::vector<std::string> tokens = splitCssTokens(lower);
		if (tokens.empty()) {
			++diag.unsupportedDeclarationCount;
			return false;
		}
		bool sawDecoration = false;
		bool underline = false;
		bool lineThrough = false;
		for (const std::string& token : tokens) {
			if (token == "underline") {
				underline = true;
				sawDecoration = true;
				continue;
			}
			if (token == "line-through") {
				lineThrough = true;
				sawDecoration = true;
				continue;
			}
			++diag.unsupportedDeclarationCount;
		}
		if (!sawDecoration) return false;
		style.hasTextDecoration = true;
		style.underline = underline;
		style.lineThrough = lineThrough;
		return accept(CssProperty::TextDecoration);
	}
	if (prop == "text-decoration-line") {
		std::string lower = toLower(val);
		if (lower == "inherit" || lower == "initial" || lower == "unset") {
			return true;
		}
		if (lower == "none") {
			style.hasTextDecoration = true;
			style.underline = false;
			style.lineThrough = false;
			return accept(CssProperty::TextDecoration);
		}
		std::vector<std::string> tokens = splitCssTokens(lower);
		if (tokens.empty()) {
			++diag.unsupportedDeclarationCount;
			return false;
		}
		bool sawDecoration = false;
		bool underline = false;
		bool lineThrough = false;
		for (const std::string& token : tokens) {
			if (token == "underline") {
				underline = true;
				sawDecoration = true;
				continue;
			}
			if (token == "line-through") {
				lineThrough = true;
				sawDecoration = true;
				continue;
			}
			++diag.unsupportedDeclarationCount;
		}
		if (!sawDecoration) return false;
		style.hasTextDecoration = true;
		style.underline = underline;
		style.lineThrough = lineThrough;
		return accept(CssProperty::TextDecoration);
	}
	if (prop == "text-align") {
		std::string lower = toLower(val);
		if (lower == "center") style.textAlign = TextAlign::Center;
		else if (lower == "left") style.textAlign = TextAlign::Left;
		else if (lower == "right") style.textAlign = TextAlign::Right;
		else if (lower == "inherit") style.textAlign = TextAlign::Inherit;
		else ++diag.unsupportedDeclarationCount;
		return lower == "inherit" ? true : accept(CssProperty::TextAlign);
	}
	if (prop == "display") {
		std::string lower = toLower(val);
		if (lower == "none") {
			style.displayNone = true;
			return accept(CssProperty::Display);
		}
		if (lower == "block" || lower == "inline" || lower == "inline-block") {
			style.displayNone = false;
			return accept(CssProperty::Display);
		}
		++diag.unsupportedDeclarationCount;
		return false;
	}
	if (prop == "margin-top" || prop == "margin-right" || prop == "margin-bottom" || prop == "margin-left" ||
		prop == "padding-top" || prop == "padding-right" || prop == "padding-bottom" || prop == "padding-left") {
		bool autoValue = false;
		int px = 0;
		if (!parseCssLengthValue(val, 320, px, autoValue, true)) {
			++diag.unsupportedDeclarationCount;
			return false;
		}
		if (autoValue) {
			px = -2;
		}
		if (prop == "margin-top") style.marginTop = px;
		else if (prop == "margin-right") style.marginRight = px;
		else if (prop == "margin-bottom") style.marginBottom = px;
		else if (prop == "margin-left") style.marginLeft = px;
		else if (prop == "padding-top") style.paddingTop = px;
		else if (prop == "padding-right") style.paddingRight = px;
		else if (prop == "padding-bottom") style.paddingBottom = px;
		else if (prop == "padding-left") style.paddingLeft = px;
		if (prop == "margin-top") return accept(CssProperty::MarginTop);
		if (prop == "margin-right") return accept(CssProperty::MarginRight);
		if (prop == "margin-bottom") return accept(CssProperty::MarginBottom);
		if (prop == "margin-left") return accept(CssProperty::MarginLeft);
		if (prop == "padding-top") return accept(CssProperty::PaddingTop);
		if (prop == "padding-right") return accept(CssProperty::PaddingRight);
		if (prop == "padding-bottom") return accept(CssProperty::PaddingBottom);
		return accept(CssProperty::PaddingLeft);
	}
	if (prop == "margin" || prop == "padding") {
		std::vector<std::string> values = splitCssTokens(val);
		if (values.empty() || values.size() > 4) {
			++diag.unsupportedDeclarationCount;
			return false;
		}
		if (!applyLengthList(style, values, prop, diag)) {
			return false;
		}
		return acceptMask(cssPropertyMask(prop == "margin" ? CssProperty::MarginTop : CssProperty::PaddingTop,
			prop == "margin" ? CssProperty::MarginLeft : CssProperty::PaddingLeft));
	}
	if (prop == "font-size") {
		bool autoValue = false;
		int px = 0;
		if (!parseCssLengthValue(val, 16, px, autoValue, true)) {
			++diag.unsupportedDeclarationCount;
			return false;
		}
		if (autoValue) {
			++diag.unsupportedDeclarationCount;
			return false;
		}
		style.fontScaleOrSize = clampCssValue(diag, px, 8, kCssLiteMaxFontSizePx);
		return accept(CssProperty::FontSize);
	}
	if (prop == "line-height") {
		std::string lower = toLower(val);
		if (lower == "normal") {
			style.lineHeightNormal = true;
			style.lineHeight = -1;
			return accept(CssProperty::LineHeight);
		}
		bool autoValue = false;
		int px = 0;
		const int lineHeightBase = style.fontScaleOrSize > 0 ? style.fontScaleOrSize : 16;
		double numeric = 0.0;
		// A unitless line-height is a multiplier, unlike a unitless length.
		if (parseCssNumber(val, numeric)) {
			px = roundCssNumber(numeric * static_cast<double>(lineHeightBase));
		} else if (!parseCssLengthValue(val, lineHeightBase, px, autoValue, true)) {
			++diag.unsupportedDeclarationCount;
			return false;
		}
		if (autoValue) {
			++diag.unsupportedDeclarationCount;
			return false;
		}
		style.lineHeightNormal = false;
		style.lineHeight = clampCssValue(diag, px, 8, kCssLiteMaxLineHeightPx);
		return accept(CssProperty::LineHeight);
	}
	if (prop == "width" || prop == "max-width" || prop == "height" || prop == "max-height") {
		std::string lower = toLower(val);
		bool autoValue = false;
		int px = 0;
		if (lower.size() > 1 && lower.back() == '%') {
			double numeric = 0.0;
			if (!parseCssNumber(lower.substr(0, lower.size() - 1), numeric)) {
				++diag.unsupportedDeclarationCount;
				return false;
			}
			int percent = roundCssNumber(numeric);
			percent = clampCssValue(diag, percent, 1, 100);
			if (prop == "width") style.widthPercent = percent;
			else if (prop == "max-width") style.maxWidthPercent = percent;
			else if (prop == "height") style.heightPercent = percent;
			else style.maxHeightPercent = percent;
			return accept(prop == "width" ? CssProperty::Width :
				prop == "height" ? CssProperty::Height :
				prop == "max-width" ? CssProperty::MaxWidth : CssProperty::MaxHeight);
		}
		if (!parseCssLengthValue(val, 320, px, autoValue, false)) {
			++diag.unsupportedDeclarationCount;
			return false;
		}
		if (autoValue) {
			if (prop == "width") {
				style.widthPercent = -1;
				style.width = 0;
			} else if (prop == "max-width") {
				style.maxWidthPercent = -1;
				style.maxWidth = 0;
			} else if (prop == "height") {
				style.heightPercent = -1;
				style.height = 0;
			} else {
				style.maxHeightPercent = -1;
				style.maxHeight = 0;
			}
			return accept(prop == "width" ? CssProperty::Width :
				prop == "height" ? CssProperty::Height :
				prop == "max-width" ? CssProperty::MaxWidth : CssProperty::MaxHeight);
		}
		px = clampCssValue(diag, px, 1, kCssLiteMaxWidthPx);
		if (prop == "width") style.width = px;
		else if (prop == "max-width") style.maxWidth = px;
		else if (prop == "height") style.height = px;
		else style.maxHeight = px;
		return accept(prop == "width" ? CssProperty::Width :
			prop == "height" ? CssProperty::Height :
			prop == "max-width" ? CssProperty::MaxWidth : CssProperty::MaxHeight);
	}
	if (prop == "white-space") {
		std::string lower = toLower(val);
		if (lower == "inherit") {
			style.whiteSpace = WhiteSpaceMode::Inherit;
			return true;
		}
		if (lower == "normal") {
			style.whiteSpace = WhiteSpaceMode::Normal;
			return accept(CssProperty::WhiteSpace);
		}
		if (lower == "pre") {
			style.whiteSpace = WhiteSpaceMode::Pre;
			return accept(CssProperty::WhiteSpace);
		}
		if (lower == "pre-wrap") {
			style.whiteSpace = WhiteSpaceMode::PreWrap;
			return accept(CssProperty::WhiteSpace);
		}
		++diag.unsupportedDeclarationCount;
		return false;
	}
	if (prop == "overflow-wrap" || prop == "word-wrap") {
		std::string lower = toLower(val);
		if (lower == "inherit") {
			style.overflowWrap = OverflowWrapMode::Inherit;
			return true;
		}
		if (lower == "normal") {
			style.overflowWrap = OverflowWrapMode::Normal;
			return accept(CssProperty::OverflowWrap);
		}
		if (lower == "break-word") {
			style.overflowWrap = OverflowWrapMode::BreakWord;
			return accept(CssProperty::OverflowWrap);
		}
		++diag.unsupportedDeclarationCount;
		return false;
	}
	if (prop == "word-break") {
		std::string lower = toLower(val);
		if (lower == "inherit") {
			style.wordBreak = WordBreakMode::Inherit;
			return true;
		}
		if (lower == "normal") {
			style.wordBreak = WordBreakMode::Normal;
			return accept(CssProperty::WordBreak);
		}
		if (lower == "break-all") {
			style.wordBreak = WordBreakMode::BreakAll;
			return accept(CssProperty::WordBreak);
		}
		++diag.unsupportedDeclarationCount;
		return false;
	}
	if (prop == "font-family") {
		std::vector<std::string> families = splitCssCommaTokens(val);
		bool sawGeneric = false;
		for (const std::string& family : families) {
			GenericFontFamily genericFamily = GenericFontFamily::Inherit;
			if (parseGenericFontFamily(family, genericFamily)) {
				style.genericFontFamily = genericFamily;
				sawGeneric = true;
				return accept(CssProperty::GenericFontFamily);
			}
		}
		if (!sawGeneric) {
			++diag.unsupportedDeclarationCount;
		}
		return sawGeneric;
	}
	if (prop == "list-style" || prop == "list-style-type") {
		std::vector<std::string> tokens = splitCssTokens(val);
		if (tokens.empty()) {
			++diag.unsupportedDeclarationCount;
			return false;
		}
		bool sawSupported = false;
		for (const std::string& token : tokens) {
			const std::string lower = toLower(token);
			if (lower == "none") {
				style.listStyleNone = true;
				style.listStyleType = ListStyleType::None;
				sawSupported = true;
				continue;
			}
			if (lower == "disc") {
				style.listStyleNone = false;
				style.listStyleType = ListStyleType::Disc;
				sawSupported = true;
				continue;
			}
			if (lower == "circle") {
				style.listStyleNone = false;
				style.listStyleType = ListStyleType::Circle;
				sawSupported = true;
				continue;
			}
			if (lower == "square") {
				style.listStyleNone = false;
				style.listStyleType = ListStyleType::Square;
				sawSupported = true;
				continue;
			}
			if (lower == "decimal" || lower == "decimal-leading-zero") {
				style.listStyleNone = false;
				style.listStyleType = ListStyleType::Decimal;
				sawSupported = true;
				continue;
			}
			if (lower == "lower-alpha" || lower == "lower-latin") {
				style.listStyleNone = false;
				style.listStyleType = ListStyleType::LowerAlpha;
				sawSupported = true;
				continue;
			}
			if (lower == "upper-alpha" || lower == "upper-latin") {
				style.listStyleNone = false;
				style.listStyleType = ListStyleType::UpperAlpha;
				sawSupported = true;
				continue;
			}
			if (lower == "lower-roman") {
				style.listStyleNone = false;
				style.listStyleType = ListStyleType::LowerRoman;
				sawSupported = true;
				continue;
			}
			if (lower == "upper-roman") {
				style.listStyleNone = false;
				style.listStyleType = ListStyleType::UpperRoman;
				sawSupported = true;
				continue;
			}
			if (lower == "inherit") {
				sawSupported = true;
				continue;
			}
			++diag.unsupportedDeclarationCount;
		}
		return sawSupported ? accept(CssProperty::ListStyle) : false;
	}
	if (prop == "border-collapse") {
		std::string lower = toLower(val);
		if (lower == "inherit" || lower == "initial" || lower == "unset") {
			style.borderCollapse = TableBorderCollapseMode::Inherit;
			return accept(CssProperty::BorderCollapse);
		}
		if (lower == "collapse") {
			style.borderCollapse = TableBorderCollapseMode::Collapse;
			return accept(CssProperty::BorderCollapse);
		}
		if (lower == "separate") {
			style.borderCollapse = TableBorderCollapseMode::Separate;
			return accept(CssProperty::BorderCollapse);
		}
		++diag.unsupportedDeclarationCount;
		return false;
	}
	if (prop == "border-spacing") {
		std::vector<std::string> values = splitCssTokens(val);
		if (!applyBorderSpacingList(style, values, diag)) {
			++diag.unsupportedDeclarationCount;
			return false;
		}
		return acceptMask(cssPropertyBit(CssProperty::BorderSpacingHorizontal) |
			cssPropertyBit(CssProperty::BorderSpacingVertical));
	}
	if (prop == "border-width") {
		std::vector<std::string> values = splitCssTokens(val);
		if (!applyBorderWidthListToSides(style, values, diag)) {
			++diag.unsupportedDeclarationCount;
			return false;
		}
		return acceptMask(cssPropertyBit(CssProperty::BorderTopWidth) |
			cssPropertyBit(CssProperty::BorderRightWidth) |
			cssPropertyBit(CssProperty::BorderBottomWidth) |
			cssPropertyBit(CssProperty::BorderLeftWidth));
	}
	if (prop == "border-style") {
		std::vector<std::string> values = splitCssTokens(val);
		if (!applyBorderStyleListToSides(style, values, diag)) {
			++diag.unsupportedDeclarationCount;
			return false;
		}
		return acceptMask(cssPropertyBit(CssProperty::BorderTopStyle) |
			cssPropertyBit(CssProperty::BorderRightStyle) |
			cssPropertyBit(CssProperty::BorderBottomStyle) |
			cssPropertyBit(CssProperty::BorderLeftStyle));
	}
	if (prop == "border-color") {
		std::vector<std::string> values = splitCssTokens(val);
		if (!applyBorderColorListToSides(style, values, diag)) {
			++diag.unsupportedDeclarationCount;
			return false;
		}
		return acceptMask(cssPropertyBit(CssProperty::BorderTopColor) |
			cssPropertyBit(CssProperty::BorderRightColor) |
			cssPropertyBit(CssProperty::BorderBottomColor) |
			cssPropertyBit(CssProperty::BorderLeftColor));
	}
	if (prop == "border" || prop == "border-top" || prop == "border-right" || prop == "border-bottom" || prop == "border-left") {
		BorderSideValue borderValue;
		if (!parseBorderSideValue(val, borderValue, diag)) {
			++diag.unsupportedDeclarationCount;
			return false;
		}
		if (prop == "border") {
			applyBorderSideValue(style, BorderSideIndex::Top, borderValue);
			applyBorderSideValue(style, BorderSideIndex::Right, borderValue);
			applyBorderSideValue(style, BorderSideIndex::Bottom, borderValue);
			applyBorderSideValue(style, BorderSideIndex::Left, borderValue);
			return acceptMask(
				cssPropertyBit(CssProperty::BorderTopWidth) | cssPropertyBit(CssProperty::BorderTopStyle) | cssPropertyBit(CssProperty::BorderTopColor) |
				cssPropertyBit(CssProperty::BorderRightWidth) | cssPropertyBit(CssProperty::BorderRightStyle) | cssPropertyBit(CssProperty::BorderRightColor) |
				cssPropertyBit(CssProperty::BorderBottomWidth) | cssPropertyBit(CssProperty::BorderBottomStyle) | cssPropertyBit(CssProperty::BorderBottomColor) |
				cssPropertyBit(CssProperty::BorderLeftWidth) | cssPropertyBit(CssProperty::BorderLeftStyle) | cssPropertyBit(CssProperty::BorderLeftColor));
		}
		if (prop == "border-top") {
			applyBorderSideValue(style, BorderSideIndex::Top, borderValue);
			return acceptMask(cssPropertyBit(CssProperty::BorderTopWidth) |
				cssPropertyBit(CssProperty::BorderTopStyle) | cssPropertyBit(CssProperty::BorderTopColor));
		}
		if (prop == "border-right") {
			applyBorderSideValue(style, BorderSideIndex::Right, borderValue);
			return acceptMask(cssPropertyBit(CssProperty::BorderRightWidth) |
				cssPropertyBit(CssProperty::BorderRightStyle) | cssPropertyBit(CssProperty::BorderRightColor));
		}
		if (prop == "border-bottom") {
			applyBorderSideValue(style, BorderSideIndex::Bottom, borderValue);
			return acceptMask(cssPropertyBit(CssProperty::BorderBottomWidth) |
				cssPropertyBit(CssProperty::BorderBottomStyle) | cssPropertyBit(CssProperty::BorderBottomColor));
		}
		applyBorderSideValue(style, BorderSideIndex::Left, borderValue);
		return acceptMask(cssPropertyBit(CssProperty::BorderLeftWidth) |
			cssPropertyBit(CssProperty::BorderLeftStyle) | cssPropertyBit(CssProperty::BorderLeftColor));
	}
	if (prop == "border-top-width" || prop == "border-right-width" || prop == "border-bottom-width" || prop == "border-left-width" ||
		prop == "border-top-style" || prop == "border-right-style" || prop == "border-bottom-style" || prop == "border-left-style" ||
		prop == "border-top-color" || prop == "border-right-color" || prop == "border-bottom-color" || prop == "border-left-color") {
		BorderSideIndex side = BorderSideIndex::Top;
		if (prop.find("right") != std::string::npos) side = BorderSideIndex::Right;
		else if (prop.find("bottom") != std::string::npos) side = BorderSideIndex::Bottom;
		else if (prop.find("left") != std::string::npos) side = BorderSideIndex::Left;
		BorderSideValue borderValue = borderSideValueFromStyle(style, side);
		borderValue.sawRecognized = true;
		if (prop.find("-width") != std::string::npos) {
			bool autoValue = false;
			int px = 0;
			if (!parseCssLengthValue(val, 16, px, autoValue, false) || autoValue) {
				++diag.unsupportedDeclarationCount;
				return false;
			}
			borderValue.hasWidth = true;
			borderValue.width = clampBorderWidthPx(diag, px);
		} else if (prop.find("-style") != std::string::npos) {
			BorderLineStyle borderStyle = BorderLineStyle::Inherit;
			if (!parseBorderStyleToken(val, borderStyle)) {
				++diag.unsupportedDeclarationCount;
				return false;
			}
			borderValue.hasStyle = true;
			borderValue.style = borderStyle;
			if ((borderStyle == BorderLineStyle::Solid || borderStyle == BorderLineStyle::Dashed || borderStyle == BorderLineStyle::Dotted) &&
				(!borderValue.hasWidth || borderValue.width <= 0)) {
				borderValue.hasWidth = true;
				borderValue.width = 1;
			}
			if ((borderStyle == BorderLineStyle::None || borderStyle == BorderLineStyle::Hidden) && !borderValue.hasWidth) {
				borderValue.hasWidth = true;
				borderValue.width = 0;
			}
		} else {
			uint32_t color = 0;
			if (!parseCssColor(val, color)) {
				++diag.unsupportedDeclarationCount;
				return false;
			}
			borderValue.hasColor = true;
			borderValue.color = color;
		}
		applyBorderSideValue(style, side, borderValue);
		CssProperty propertyId = CssProperty::BorderTopWidth;
		if (side == BorderSideIndex::Right) propertyId = CssProperty::BorderRightWidth;
		else if (side == BorderSideIndex::Bottom) propertyId = CssProperty::BorderBottomWidth;
		else if (side == BorderSideIndex::Left) propertyId = CssProperty::BorderLeftWidth;
		if (prop.find("-style") != std::string::npos) {
			propertyId = propertyId == CssProperty::BorderTopWidth ? CssProperty::BorderTopStyle :
				propertyId == CssProperty::BorderRightWidth ? CssProperty::BorderRightStyle :
				propertyId == CssProperty::BorderBottomWidth ? CssProperty::BorderBottomStyle : CssProperty::BorderLeftStyle;
		} else if (prop.find("-color") != std::string::npos) {
			propertyId = propertyId == CssProperty::BorderTopWidth ? CssProperty::BorderTopColor :
				propertyId == CssProperty::BorderRightWidth ? CssProperty::BorderRightColor :
				propertyId == CssProperty::BorderBottomWidth ? CssProperty::BorderBottomColor : CssProperty::BorderLeftColor;
		}
		return accept(propertyId);
	}
	++diag.unsupportedDeclarationCount;
	return false;
}

static void applyStyleProperty(WebStyle& destination, const WebStyle& source, CssProperty property)
{
	switch (property) {
	case CssProperty::Color: destination.hasColor = source.hasColor; destination.color = source.color; break;
	case CssProperty::Background: destination.hasBackgroundColor = source.hasBackgroundColor; destination.backgroundColor = source.backgroundColor; break;
	case CssProperty::Bold: destination.bold = source.bold; break;
	case CssProperty::Italic: destination.italic = source.italic; break;
	case CssProperty::TextDecoration:
		destination.hasTextDecoration = source.hasTextDecoration;
		destination.underline = source.underline;
		destination.lineThrough = source.lineThrough;
		break;
	case CssProperty::Display: destination.displayNone = source.displayNone; break;
	case CssProperty::ListStyle:
		destination.listStyleNone = source.listStyleNone;
		destination.listStyleType = source.listStyleType;
		break;
	case CssProperty::BorderCollapse: destination.borderCollapse = source.borderCollapse; break;
	case CssProperty::BorderSpacingHorizontal: destination.borderSpacingHorizontal = source.borderSpacingHorizontal; break;
	case CssProperty::BorderSpacingVertical: destination.borderSpacingVertical = source.borderSpacingVertical; break;
	case CssProperty::GenericFontFamily: destination.genericFontFamily = source.genericFontFamily; break;
	case CssProperty::TextAlign: destination.textAlign = source.textAlign; break;
	case CssProperty::LineHeight:
		destination.lineHeightNormal = source.lineHeightNormal;
		destination.lineHeight = source.lineHeight;
		break;
	case CssProperty::MarginTop: destination.marginTop = source.marginTop; break;
	case CssProperty::MarginRight: destination.marginRight = source.marginRight; break;
	case CssProperty::MarginBottom: destination.marginBottom = source.marginBottom; break;
	case CssProperty::MarginLeft: destination.marginLeft = source.marginLeft; break;
	case CssProperty::PaddingTop: destination.paddingTop = source.paddingTop; break;
	case CssProperty::PaddingRight: destination.paddingRight = source.paddingRight; break;
	case CssProperty::PaddingBottom: destination.paddingBottom = source.paddingBottom; break;
	case CssProperty::PaddingLeft: destination.paddingLeft = source.paddingLeft; break;
	case CssProperty::FontSize: destination.fontScaleOrSize = source.fontScaleOrSize; break;
	case CssProperty::Width: destination.width = source.width; destination.widthPercent = source.widthPercent; break;
	case CssProperty::Height: destination.height = source.height; destination.heightPercent = source.heightPercent; break;
	case CssProperty::MaxWidth: destination.maxWidth = source.maxWidth; destination.maxWidthPercent = source.maxWidthPercent; break;
	case CssProperty::MaxHeight: destination.maxHeight = source.maxHeight; destination.maxHeightPercent = source.maxHeightPercent; break;
	case CssProperty::WhiteSpace: destination.whiteSpace = source.whiteSpace; break;
	case CssProperty::OverflowWrap: destination.overflowWrap = source.overflowWrap; break;
	case CssProperty::WordBreak: destination.wordBreak = source.wordBreak; break;
	case CssProperty::BorderTopWidth: destination.hasBorderTop = source.hasBorderTop; destination.borderTopWidth = source.borderTopWidth; break;
	case CssProperty::BorderTopStyle: destination.hasBorderTop = source.hasBorderTop; destination.borderTopStyle = source.borderTopStyle; break;
	case CssProperty::BorderTopColor: destination.hasBorderTop = source.hasBorderTop; destination.borderTopColor = source.borderTopColor; break;
	case CssProperty::BorderRightWidth: destination.hasBorderRight = source.hasBorderRight; destination.borderRightWidth = source.borderRightWidth; break;
	case CssProperty::BorderRightStyle: destination.hasBorderRight = source.hasBorderRight; destination.borderRightStyle = source.borderRightStyle; break;
	case CssProperty::BorderRightColor: destination.hasBorderRight = source.hasBorderRight; destination.borderRightColor = source.borderRightColor; break;
	case CssProperty::BorderBottomWidth: destination.hasBorderBottom = source.hasBorderBottom; destination.borderBottomWidth = source.borderBottomWidth; break;
	case CssProperty::BorderBottomStyle: destination.hasBorderBottom = source.hasBorderBottom; destination.borderBottomStyle = source.borderBottomStyle; break;
	case CssProperty::BorderBottomColor: destination.hasBorderBottom = source.hasBorderBottom; destination.borderBottomColor = source.borderBottomColor; break;
	case CssProperty::BorderLeftWidth: destination.hasBorderLeft = source.hasBorderLeft; destination.borderLeftWidth = source.borderLeftWidth; break;
	case CssProperty::BorderLeftStyle: destination.hasBorderLeft = source.hasBorderLeft; destination.borderLeftStyle = source.borderLeftStyle; break;
	case CssProperty::BorderLeftColor: destination.hasBorderLeft = source.hasBorderLeft; destination.borderLeftColor = source.borderLeftColor; break;
	case CssProperty::Count: break;
	}
}

static void mergeParsedDeclaration(WebStyle& destination, const WebStyle& source)
{
	for (unsigned i = 0; i < static_cast<unsigned>(CssProperty::Count); ++i) {
		const CssProperty property = static_cast<CssProperty>(i);
		const uint64_t bit = cssPropertyBit(property);
		if ((source.specifiedProperties & bit) == 0) continue;
		const bool sourceImportant = (source.importantProperties & bit) != 0;
		const bool destinationImportant = (destination.importantProperties & bit) != 0;
		if ((destination.specifiedProperties & bit) != 0 && destinationImportant && !sourceImportant) continue;
		applyStyleProperty(destination, source, property);
		destination.specifiedProperties |= bit;
		if (sourceImportant) destination.importantProperties |= bit;
		else destination.importantProperties &= ~bit;
	}
}

static void parseCssDeclarations(const std::string& body, WebStyle& style, CssDiagnostics& diag)
{
	size_t cursor = 0;
	size_t declarationCount = 0;
	while (cursor < body.size()) {
		if (declarationCount >= kCssLiteMaxDeclarationsPerRule ||
			diag.declarationsProcessed >= static_cast<int>(kCssLiteMaxTotalDeclarations)) {
			saturatingIncrement(diag.declarationCapCount);
			break;
		}
		size_t semi = body.find(';', cursor);
		std::string decl = body.substr(cursor, semi == std::string::npos ? std::string::npos : semi - cursor);
		size_t colon = decl.find(':');
		if (colon != std::string::npos) {
			++declarationCount;
			saturatingIncrement(diag.declarationsProcessed);
			WebStyle parsed;
			// Unitless line-height is relative to the font size established by
			// an earlier declaration in this same bounded declaration list.
			parsed.fontScaleOrSize = style.fontScaleOrSize;
			parseInlineStyleDeclaration(parsed, decl.substr(0, colon), decl.substr(colon + 1), diag);
			mergeParsedDeclaration(style, parsed);
		}
		cursor = semi == std::string::npos ? body.size() : semi + 1;
	}
}

static WebStyle mergeStyles(const WebStyle& baseStyle, const WebStyle& overrideStyle)
{
	WebStyle merged = baseStyle;
	if (overrideStyle.hasColor) {
		merged.hasColor = true;
		merged.color = overrideStyle.color;
	}
	if (overrideStyle.hasBackgroundColor) {
		merged.hasBackgroundColor = true;
		merged.backgroundColor = overrideStyle.backgroundColor;
	}
	merged.bold = overrideStyle.bold ? true : merged.bold;
	merged.italic = overrideStyle.italic ? true : merged.italic;
	if (overrideStyle.hasTextDecoration) {
		merged.hasTextDecoration = true;
		merged.underline = overrideStyle.underline;
		merged.lineThrough = overrideStyle.lineThrough;
	}
	merged.displayNone = overrideStyle.displayNone ? true : merged.displayNone;
	merged.listStyleNone = overrideStyle.listStyleNone ? true : merged.listStyleNone;
	if (overrideStyle.listStyleType != ListStyleType::Inherit) {
		merged.listStyleType = overrideStyle.listStyleType;
		if (overrideStyle.listStyleType != ListStyleType::None) {
			merged.listStyleNone = false;
		}
	}
	if (overrideStyle.borderCollapse != TableBorderCollapseMode::Inherit) {
		merged.borderCollapse = overrideStyle.borderCollapse;
	}
	merged.borderSpacingHorizontal = overrideStyle.borderSpacingHorizontal != -1 ?
		overrideStyle.borderSpacingHorizontal : merged.borderSpacingHorizontal;
	merged.borderSpacingVertical = overrideStyle.borderSpacingVertical != -1 ?
		overrideStyle.borderSpacingVertical : merged.borderSpacingVertical;
	if (overrideStyle.genericFontFamily != GenericFontFamily::Inherit) {
		merged.genericFontFamily = overrideStyle.genericFontFamily;
	}
	if (overrideStyle.textAlign != TextAlign::Inherit) {
		merged.textAlign = overrideStyle.textAlign;
	}
	if (overrideStyle.lineHeightNormal) {
		merged.lineHeightNormal = true;
		merged.lineHeight = -1;
	} else if (overrideStyle.lineHeight > 0) {
		merged.lineHeightNormal = false;
		merged.lineHeight = overrideStyle.lineHeight;
	}
	merged.marginTop = overrideStyle.marginTop != -1 ? overrideStyle.marginTop : merged.marginTop;
	merged.marginRight = overrideStyle.marginRight != -1 ? overrideStyle.marginRight : merged.marginRight;
	merged.marginBottom = overrideStyle.marginBottom != -1 ? overrideStyle.marginBottom : merged.marginBottom;
	merged.marginLeft = overrideStyle.marginLeft != -1 ? overrideStyle.marginLeft : merged.marginLeft;
	merged.padding = overrideStyle.padding != -1 ? overrideStyle.padding : merged.padding;
	merged.paddingTop = overrideStyle.paddingTop != -1 ? overrideStyle.paddingTop : merged.paddingTop;
	merged.paddingRight = overrideStyle.paddingRight != -1 ? overrideStyle.paddingRight : merged.paddingRight;
	merged.paddingBottom = overrideStyle.paddingBottom != -1 ? overrideStyle.paddingBottom : merged.paddingBottom;
	merged.paddingLeft = overrideStyle.paddingLeft != -1 ? overrideStyle.paddingLeft : merged.paddingLeft;
	merged.fontScaleOrSize = overrideStyle.fontScaleOrSize > 0 ? overrideStyle.fontScaleOrSize : merged.fontScaleOrSize;
	merged.width = overrideStyle.width != -1 ? overrideStyle.width : merged.width;
	merged.widthPercent = overrideStyle.widthPercent != -1 ? overrideStyle.widthPercent : merged.widthPercent;
	merged.height = overrideStyle.height != -1 ? overrideStyle.height : merged.height;
	merged.heightPercent = overrideStyle.heightPercent != -1 ? overrideStyle.heightPercent : merged.heightPercent;
	merged.maxWidth = overrideStyle.maxWidth != -1 ? overrideStyle.maxWidth : merged.maxWidth;
	merged.maxWidthPercent = overrideStyle.maxWidthPercent != -1 ? overrideStyle.maxWidthPercent : merged.maxWidthPercent;
	merged.maxHeight = overrideStyle.maxHeight != -1 ? overrideStyle.maxHeight : merged.maxHeight;
	merged.maxHeightPercent = overrideStyle.maxHeightPercent != -1 ? overrideStyle.maxHeightPercent : merged.maxHeightPercent;
	if (overrideStyle.whiteSpace != WhiteSpaceMode::Inherit) {
		merged.whiteSpace = overrideStyle.whiteSpace;
	}
	if (overrideStyle.overflowWrap != OverflowWrapMode::Inherit) {
		merged.overflowWrap = overrideStyle.overflowWrap;
	}
	if (overrideStyle.wordBreak != WordBreakMode::Inherit) {
		merged.wordBreak = overrideStyle.wordBreak;
	}
	merged.hasBorderTop = overrideStyle.hasBorderTop ? true : merged.hasBorderTop;
	if (overrideStyle.hasBorderTop) {
		merged.borderTopWidth = overrideStyle.borderTopWidth;
		merged.borderTopColor = overrideStyle.borderTopColor;
		merged.borderTopStyle = overrideStyle.borderTopStyle;
	}
	merged.hasBorderRight = overrideStyle.hasBorderRight ? true : merged.hasBorderRight;
	if (overrideStyle.hasBorderRight) {
		merged.borderRightWidth = overrideStyle.borderRightWidth;
		merged.borderRightColor = overrideStyle.borderRightColor;
		merged.borderRightStyle = overrideStyle.borderRightStyle;
	}
	merged.hasBorderBottom = overrideStyle.hasBorderBottom ? true : merged.hasBorderBottom;
	if (overrideStyle.hasBorderBottom) {
		merged.borderBottomWidth = overrideStyle.borderBottomWidth;
		merged.borderBottomColor = overrideStyle.borderBottomColor;
		merged.borderBottomStyle = overrideStyle.borderBottomStyle;
	}
	merged.hasBorderLeft = overrideStyle.hasBorderLeft ? true : merged.hasBorderLeft;
	if (overrideStyle.hasBorderLeft) {
		merged.borderLeftWidth = overrideStyle.borderLeftWidth;
		merged.borderLeftColor = overrideStyle.borderLeftColor;
		merged.borderLeftStyle = overrideStyle.borderLeftStyle;
	}
	return merged;
}

static WebStyle defaultStyleForTag(const std::string& tagName)
{
	WebStyle style;
	if (tagName == "body") {
		style.hasColor = true;
		style.color = 0xFF303846u;
		style.hasBackgroundColor = true;
		style.backgroundColor = 0xFFF5F7FAu;
		style.marginTop = 0;
		style.marginBottom = 0;
		style.marginLeft = 0;
		style.padding = 0;
		return style;
	}
	if (tagName == "h1") {
		style.bold = true;
		style.marginTop = 14;
		style.marginBottom = 10;
		style.fontScaleOrSize = 24;
		return style;
	}
	if (tagName == "h2") {
		style.bold = true;
		style.marginTop = 12;
		style.marginBottom = 8;
		style.fontScaleOrSize = 20;
		return style;
	}
	if (tagName == "h3") {
		style.bold = true;
		style.marginTop = 10;
		style.marginBottom = 6;
		style.fontScaleOrSize = 18;
		return style;
	}
	if (tagName == "p") {
		style.marginTop = 8;
		style.marginBottom = 10;
		return style;
	}
	if (tagName == "blockquote") {
		style.marginTop = 8;
		style.marginBottom = 10;
		style.marginLeft = 18;
		style.paddingLeft = 12;
		style.paddingRight = 8;
		style.paddingTop = 4;
		style.paddingBottom = 4;
		style.hasBackgroundColor = true;
		style.backgroundColor = 0xFFF1F5F9u;
		return style;
	}
	if (tagName == "figure") {
		style.marginTop = 10;
		style.marginBottom = 10;
		return style;
	}
	if (tagName == "figcaption") {
		style.textAlign = TextAlign::Center;
		style.hasColor = true;
		style.color = 0xFF5B6472u;
		style.italic = true;
		style.fontScaleOrSize = 13;
		style.marginTop = 4;
		style.marginBottom = 6;
		return style;
	}
	if (tagName == "dl") {
		style.marginTop = 8;
		style.marginBottom = 10;
		return style;
	}
	if (tagName == "dt") {
		style.bold = true;
		style.marginTop = 6;
		style.marginBottom = 2;
		return style;
	}
	if (tagName == "dd") {
		style.marginTop = 2;
		style.marginBottom = 6;
		style.marginLeft = 18;
		return style;
	}
	if (tagName == "caption") {
		style.textAlign = TextAlign::Center;
		style.hasColor = true;
		style.color = 0xFF5B6472u;
		style.italic = true;
		style.fontScaleOrSize = 13;
		style.marginTop = 4;
		style.marginBottom = 6;
		return style;
	}
	if (tagName == "table") {
		style.marginTop = 8;
		style.marginBottom = 10;
		style.padding = 0;
		style.borderCollapse = TableBorderCollapseMode::Separate;
		return style;
	}
	if (tagName == "td" || tagName == "th") {
		style.marginTop = 0;
		style.marginBottom = 0;
		style.paddingTop = 4;
		style.paddingRight = 6;
		style.paddingBottom = 4;
		style.paddingLeft = 6;
		if (tagName == "th") {
			style.bold = true;
			style.textAlign = TextAlign::Center;
		}
		return style;
	}
	if (tagName == "hr") {
		style.hasBorderTop = true;
		style.borderTopWidth = 1;
		style.borderTopColor = 0xFFB8C0CCu;
		style.marginTop = 10;
		style.marginBottom = 10;
		return style;
	}
	if (tagName == "strong" || tagName == "b") {
		style.bold = true;
		return style;
	}
	if (tagName == "em" || tagName == "i") {
		style.italic = true;
		return style;
	}
	if (tagName == "small") {
		style.hasColor = true;
		style.color = 0xFF5B6472u;
		style.fontScaleOrSize = 13;
		return style;
	}
	if (tagName == "cite") {
		style.italic = true;
		style.hasColor = true;
		style.color = 0xFF5B6472u;
		return style;
	}
	if (tagName == "q") {
		style.italic = true;
		return style;
	}
	if (tagName == "code" || tagName == "kbd" || tagName == "samp") {
		style.hasBackgroundColor = true;
		style.backgroundColor = 0xFFE6E8EEu;
		style.marginTop = 4;
		style.marginBottom = 4;
		style.paddingTop = 2;
		style.paddingRight = 4;
		style.paddingBottom = 2;
		style.paddingLeft = 4;
		style.fontScaleOrSize = 14;
		return style;
	}
	if (tagName == "a") {
		style.hasColor = true;
		style.color = 0xFF1E5CB8u;
		style.hasTextDecoration = true;
		style.underline = true;
		style.marginTop = 4;
		style.marginBottom = 4;
		return style;
	}
	if (tagName == "li") {
		style.marginTop = 4;
		style.marginBottom = 4;
		style.marginLeft = 8;
		return style;
	}
	if (tagName == "ul" || tagName == "ol") {
		style.marginTop = 6;
		style.marginBottom = 8;
		style.paddingLeft = 18;
		style.listStyleType = tagName == "ol" ? ListStyleType::Decimal : ListStyleType::Disc;
		return style;
	}
	if (tagName == "pre" || tagName == "code") {
		style.hasBackgroundColor = true;
		style.backgroundColor = 0xFFE6E8EEu;
		style.marginTop = 8;
		style.marginBottom = 10;
		style.padding = 8;
		style.whiteSpace = WhiteSpaceMode::Pre;
		return style;
	}
	if (tagName == "img") {
		style.marginTop = 8;
		style.marginBottom = 8;
		return style;
	}
	return style;
}

static void markDefaultStyleProperties(WebStyle& style)
{
	if (style.hasColor) style.specifiedProperties |= cssPropertyBit(CssProperty::Color);
	if (style.hasBackgroundColor) style.specifiedProperties |= cssPropertyBit(CssProperty::Background);
	if (style.bold) style.specifiedProperties |= cssPropertyBit(CssProperty::Bold);
	if (style.italic) style.specifiedProperties |= cssPropertyBit(CssProperty::Italic);
	if (style.hasTextDecoration) style.specifiedProperties |= cssPropertyBit(CssProperty::TextDecoration);
	if (style.displayNone) style.specifiedProperties |= cssPropertyBit(CssProperty::Display);
	if (style.listStyleNone || style.listStyleType != ListStyleType::Inherit) style.specifiedProperties |= cssPropertyBit(CssProperty::ListStyle);
	if (style.borderCollapse != TableBorderCollapseMode::Inherit) style.specifiedProperties |= cssPropertyBit(CssProperty::BorderCollapse);
	if (style.borderSpacingHorizontal != -1) style.specifiedProperties |= cssPropertyBit(CssProperty::BorderSpacingHorizontal);
	if (style.borderSpacingVertical != -1) style.specifiedProperties |= cssPropertyBit(CssProperty::BorderSpacingVertical);
	if (style.genericFontFamily != GenericFontFamily::Inherit) style.specifiedProperties |= cssPropertyBit(CssProperty::GenericFontFamily);
	if (style.textAlign != TextAlign::Inherit) style.specifiedProperties |= cssPropertyBit(CssProperty::TextAlign);
	if (style.lineHeightNormal || style.lineHeight > 0) style.specifiedProperties |= cssPropertyBit(CssProperty::LineHeight);
	if (style.marginTop != -1) style.specifiedProperties |= cssPropertyBit(CssProperty::MarginTop);
	if (style.marginRight != -1) style.specifiedProperties |= cssPropertyBit(CssProperty::MarginRight);
	if (style.marginBottom != -1) style.specifiedProperties |= cssPropertyBit(CssProperty::MarginBottom);
	if (style.marginLeft != -1) style.specifiedProperties |= cssPropertyBit(CssProperty::MarginLeft);
	if (style.padding != -1) style.specifiedProperties |= cssPropertyMask(CssProperty::PaddingTop, CssProperty::PaddingLeft);
	if (style.paddingTop != -1) style.specifiedProperties |= cssPropertyBit(CssProperty::PaddingTop);
	if (style.paddingRight != -1) style.specifiedProperties |= cssPropertyBit(CssProperty::PaddingRight);
	if (style.paddingBottom != -1) style.specifiedProperties |= cssPropertyBit(CssProperty::PaddingBottom);
	if (style.paddingLeft != -1) style.specifiedProperties |= cssPropertyBit(CssProperty::PaddingLeft);
	if (style.fontScaleOrSize > 0) style.specifiedProperties |= cssPropertyBit(CssProperty::FontSize);
	if (style.width != -1 || style.widthPercent != -1) style.specifiedProperties |= cssPropertyBit(CssProperty::Width);
	if (style.height != -1 || style.heightPercent != -1) style.specifiedProperties |= cssPropertyBit(CssProperty::Height);
	if (style.maxWidth != -1 || style.maxWidthPercent != -1) style.specifiedProperties |= cssPropertyBit(CssProperty::MaxWidth);
	if (style.maxHeight != -1 || style.maxHeightPercent != -1) style.specifiedProperties |= cssPropertyBit(CssProperty::MaxHeight);
	if (style.whiteSpace != WhiteSpaceMode::Inherit) style.specifiedProperties |= cssPropertyBit(CssProperty::WhiteSpace);
	if (style.overflowWrap != OverflowWrapMode::Inherit) style.specifiedProperties |= cssPropertyBit(CssProperty::OverflowWrap);
	if (style.wordBreak != WordBreakMode::Inherit) style.specifiedProperties |= cssPropertyBit(CssProperty::WordBreak);
	if (style.hasBorderTop) style.specifiedProperties |= cssPropertyBit(CssProperty::BorderTopWidth) | cssPropertyBit(CssProperty::BorderTopStyle) | cssPropertyBit(CssProperty::BorderTopColor);
	if (style.hasBorderRight) style.specifiedProperties |= cssPropertyBit(CssProperty::BorderRightWidth) | cssPropertyBit(CssProperty::BorderRightStyle) | cssPropertyBit(CssProperty::BorderRightColor);
	if (style.hasBorderBottom) style.specifiedProperties |= cssPropertyBit(CssProperty::BorderBottomWidth) | cssPropertyBit(CssProperty::BorderBottomStyle) | cssPropertyBit(CssProperty::BorderBottomColor);
	if (style.hasBorderLeft) style.specifiedProperties |= cssPropertyBit(CssProperty::BorderLeftWidth) | cssPropertyBit(CssProperty::BorderLeftStyle) | cssPropertyBit(CssProperty::BorderLeftColor);
}

static std::string pathSignature(const std::vector<HtmlElementRef>& path)
{
	std::ostringstream oss;
	for (const HtmlElementRef& element : path) {
		oss << "|" << toLower(element.tagName) << "#" << toLower(element.id)
			<< "." << toLower(element.className) << "{" << element.inlineStyle << "}";
	}
	return oss.str();
}

struct CssCascadeWinner {
	bool valid = false;
	bool important = false;
	bool inlineDeclaration = false;
	bool inherited = false;
	CssSpecificity specificity;
	uint32_t sourceOrder = 0;
};

static bool specificityGreater(const CssSpecificity& left, const CssSpecificity& right)
{
	if (left.idCount != right.idCount) return left.idCount > right.idCount;
	if (left.classCount != right.classCount) return left.classCount > right.classCount;
	return left.elementCount > right.elementCount;
}

static bool specificityEqual(const CssSpecificity& left, const CssSpecificity& right)
{
	return left.idCount == right.idCount && left.classCount == right.classCount &&
		left.elementCount == right.elementCount;
}

static bool cascadeCandidateWins(const CssCascadeWinner& candidate, const CssCascadeWinner& current)
{
	if (!current.valid) return true;
	if (candidate.important != current.important) return candidate.important;
	if (candidate.inlineDeclaration != current.inlineDeclaration) return candidate.inlineDeclaration;
	if (specificityGreater(candidate.specificity, current.specificity)) return true;
	if (specificityEqual(candidate.specificity, current.specificity)) return candidate.sourceOrder >= current.sourceOrder;
	return false;
}

static uint32_t allocateCssSourceOrder(CssDiagnostics& diag)
{
	if (diag.nextSourceOrder == std::numeric_limits<uint32_t>::max()) return diag.nextSourceOrder;
	return diag.nextSourceOrder++;
}

static bool styleHasEffectiveProperty(const WebStyle& style, CssProperty property)
{
	const uint64_t bit = cssPropertyBit(property);
	return (style.specifiedProperties & bit) != 0 || (style.inheritedProperties & bit) != 0;
}

static void applyInheritedProperties(WebStyle& style, const WebStyle& parent, CssDiagnostics& diag)
{
	const CssProperty inherited[] = {
		CssProperty::Color, CssProperty::Bold, CssProperty::Italic,
		CssProperty::GenericFontFamily, CssProperty::FontSize, CssProperty::LineHeight,
		CssProperty::TextAlign, CssProperty::WhiteSpace, CssProperty::OverflowWrap,
		CssProperty::WordBreak,
	};
	for (CssProperty property : inherited) {
		const uint64_t bit = cssPropertyBit(property);
		if (styleHasEffectiveProperty(style, property) ||
			(!styleHasEffectiveProperty(parent, property))) continue;
		applyStyleProperty(style, parent, property);
		style.inheritedProperties |= bit;
		saturatingIncrement(diag.inheritedPropertiesApplied);
	}
}

static std::string cssColorEvidence(bool present, uint32_t color)
{
	if (!present) return "none";
	std::ostringstream oss;
	oss << "#" << std::hex << std::setw(6) << std::setfill('0') << (color & 0xFFFFFFu);
	return oss.str();
}

static void appendComputedStyleEvidence(WebDocument& doc,
	const HtmlElementRef& element,
	const WebStyle& style,
	const std::array<CssCascadeWinner, static_cast<size_t>(CssProperty::Count)>& winners)
{
	const std::string id = toLower(element.id);
	if (id.rfind("phase2a-", 0) != 0 && id.rfind("css2a-", 0) != 0) return;
	if (doc.cssDiagnostics.computedStyleEvidence.size() >= 8192 ||
		std::count(doc.cssDiagnostics.computedStyleEvidence.begin(),
			doc.cssDiagnostics.computedStyleEvidence.end(), ';') >= static_cast<int>(kCssLiteMaxEvidenceEntries - 1)) return;
	const CssCascadeWinner& colorWinner = winners[static_cast<size_t>(CssProperty::Color)];
	const CssCascadeWinner& paddingWinner = winners[static_cast<size_t>(CssProperty::PaddingTop)];
	const CssCascadeWinner& fontWinner = winners[static_cast<size_t>(CssProperty::FontSize)];
	const CssCascadeWinner& borderWinner = winners[static_cast<size_t>(CssProperty::BorderTopWidth)];
	std::ostringstream oss;
	oss << "id=" << element.id << ",tag=" << toLower(element.tagName)
		<< ",classes=" << collapseWs(element.className)
		<< ",color=" << cssColorEvidence(style.hasColor, style.color)
		<< ",background=" << cssColorEvidence(style.hasBackgroundColor, style.backgroundColor)
		<< ",font-size=" << style.fontScaleOrSize
		<< ",line-height=" << (style.lineHeightNormal ? "normal" : std::to_string(style.lineHeight))
		<< ",padding-top=" << style.paddingTop
		<< ",border-top-width=" << style.borderTopWidth
		<< ",color-specificity=" << colorWinner.specificity.idCount << "." << colorWinner.specificity.classCount << "." << colorWinner.specificity.elementCount
		<< ",color-source-order=" << colorWinner.sourceOrder
		<< ",color-inherited=" << (colorWinner.inherited ? "yes" : "no")
		<< ",padding-source-order=" << paddingWinner.sourceOrder
		<< ",font-size-source-order=" << fontWinner.sourceOrder
		<< ",border-source-order=" << borderWinner.sourceOrder << ";";
	if (doc.cssDiagnostics.computedStyleEvidence.size() + oss.str().size() <= 8192)
		doc.cssDiagnostics.computedStyleEvidence += oss.str();
}

static WebStyle computePathStyle(WebDocument& doc,
	const std::vector<HtmlElementRef>& path,
	std::unordered_map<std::string, WebStyle>& cache)
{
	if (path.empty()) return WebStyle();
	const std::string key = pathSignature(path);
	auto it = cache.find(key);
	if (it != cache.end()) return it->second;

	WebStyle style = defaultStyleForTag(path.back().tagName);
	markDefaultStyleProperties(style);
	std::array<CssCascadeWinner, static_cast<size_t>(CssProperty::Count)> winners{};
	WebStyle parent;
	bool hasParent = path.size() > 1 && path.size() <= kCssLiteMaxInheritanceDepth + 1;
	size_t cascadeApplications = 0;
	bool cascadeApplicationsCapped = false;
	if (path.size() > kCssLiteMaxInheritanceDepth + 1) saturatingIncrement(doc.cssDiagnostics.inheritanceDepthClamps);
	if (hasParent) {
		std::vector<HtmlElementRef> parentPath(path.begin(), path.end() - 1);
		parent = computePathStyle(doc, parentPath, cache);
		applyInheritedProperties(style, parent, doc.cssDiagnostics);
	}

	for (const WebStyleRule& rule : doc.styleRules) {
		if (!selectorMatchesPath(path, rule)) continue;
		saturatingIncrement(doc.cssDiagnostics.selectorMatches);
		for (unsigned i = 0; i < static_cast<unsigned>(CssProperty::Count); ++i) {
			const CssProperty property = static_cast<CssProperty>(i);
			const uint64_t bit = cssPropertyBit(property);
			if ((rule.style.specifiedProperties & bit) == 0) continue;
			if (cascadeApplications >= kCssLiteMaxCascadeApplicationsPerNode) {
				saturatingIncrement(doc.cssDiagnostics.ruleCapCount);
				cascadeApplicationsCapped = true;
				break;
			}
			++cascadeApplications;
			CssCascadeWinner candidate;
			candidate.valid = true;
			candidate.important = (rule.style.importantProperties & bit) != 0;
			candidate.specificity = rule.specificityTuple;
			candidate.sourceOrder = rule.sourceOrder;
			if (!cascadeCandidateWins(candidate, winners[i])) continue;
			if (winners[i].valid) {
				if (specificityGreater(candidate.specificity, winners[i].specificity)) saturatingIncrement(doc.cssDiagnostics.specificityOverrides);
				else if (specificityEqual(candidate.specificity, winners[i].specificity) && candidate.sourceOrder > winners[i].sourceOrder) saturatingIncrement(doc.cssDiagnostics.sourceOrderOverrides);
			}
			applyStyleProperty(style, rule.style, property);
			style.specifiedProperties |= bit;
			style.inheritedProperties &= ~bit;
			style.importantProperties = (style.importantProperties & ~bit) | (candidate.important ? bit : 0);
			winners[i] = candidate;
			saturatingIncrement(doc.cssDiagnostics.cascadePropertyResolutions);
			if (candidate.important) saturatingIncrement(doc.cssDiagnostics.importantDeclarationsApplied);
		}
		if (cascadeApplicationsCapped) break;
	}

	if (!path.back().inlineStyle.empty()) {
		WebStyle inlineStyle;
		parseCssDeclarations(path.back().inlineStyle, inlineStyle, doc.cssDiagnostics);
		const uint32_t inlineOrder = allocateCssSourceOrder(doc.cssDiagnostics);
		for (unsigned i = 0; i < static_cast<unsigned>(CssProperty::Count); ++i) {
			const CssProperty property = static_cast<CssProperty>(i);
			const uint64_t bit = cssPropertyBit(property);
			if ((inlineStyle.specifiedProperties & bit) == 0) continue;
			if (cascadeApplications >= kCssLiteMaxCascadeApplicationsPerNode) {
				saturatingIncrement(doc.cssDiagnostics.ruleCapCount);
				break;
			}
			++cascadeApplications;
			CssCascadeWinner candidate;
			candidate.valid = true;
			candidate.important = (inlineStyle.importantProperties & bit) != 0;
			candidate.inlineDeclaration = true;
			candidate.specificity = {};
			candidate.sourceOrder = inlineOrder;
			if (!cascadeCandidateWins(candidate, winners[i])) continue;
			if (winners[i].valid) {
				if (candidate.inlineDeclaration) saturatingIncrement(doc.cssDiagnostics.inlineOverrides);
				else if (specificityGreater(candidate.specificity, winners[i].specificity)) saturatingIncrement(doc.cssDiagnostics.specificityOverrides);
			}
			applyStyleProperty(style, inlineStyle, property);
			style.specifiedProperties |= bit;
			style.inheritedProperties &= ~bit;
			style.importantProperties = (style.importantProperties & ~bit) | (candidate.important ? bit : 0);
			winners[i] = candidate;
			saturatingIncrement(doc.cssDiagnostics.cascadePropertyResolutions);
			if (candidate.important) saturatingIncrement(doc.cssDiagnostics.importantDeclarationsApplied);
		}
	}
	if (hasParent && parent.displayNone) style.displayNone = true;
	appendComputedStyleEvidence(doc, path.back(), style, winners);

	cache.emplace(key, style);
	return style;
}

static void applyDocumentStyles(WebDocument& doc)
{
	doc.cssDiagnostics.cssEnabled = true;
	std::unordered_map<std::string, WebStyle> cache;

	if (doc.hasBodyElement) {
		std::vector<HtmlElementRef> bodyPath;
		bodyPath.push_back(doc.bodyElement);
		doc.bodyStyle = computePathStyle(doc, bodyPath, cache);
	} else {
		doc.bodyStyle = defaultStyleForTag("body");
		markDefaultStyleProperties(doc.bodyStyle);
	}

	for (DocBlock& block : doc.blocks) {
		std::vector<HtmlElementRef> path;
		if (doc.hasBodyElement) {
			path.push_back(doc.bodyElement);
		}

		size_t startIndex = 0;
		if (!block.ancestors.empty() && doc.hasBodyElement &&
			toLower(block.ancestors.front().tagName) == "body") {
			startIndex = 1;
		}

		for (size_t i = startIndex; i < block.ancestors.size(); ++i) path.push_back(block.ancestors[i]);

		HtmlElementRef selfRef;
		selfRef.tagName = block.tagName;
		selfRef.className = block.className;
		selfRef.id = block.id;
	selfRef.inlineStyle = block.inlineStyle;
	path.push_back(selfRef);
		block.style = computePathStyle(doc, path, cache);
		// Table geometry and wrapper borders are renderer metadata rather than
		// inherited CSS.  Resolve them from bounded computed prefixes only when
		// the block itself has no value, preserving explicit child declarations.
		for (size_t prefixLength = path.size(); prefixLength > 1; --prefixLength) {
			std::vector<HtmlElementRef> prefix(path.begin(), path.begin() + prefixLength - 1);
			const WebStyle ancestorStyle = computePathStyle(doc, prefix, cache);
			const std::string ancestorTag = toLower(prefix.back().tagName);
			if (ancestorTag == "table" && block.style.borderCollapse == TableBorderCollapseMode::Inherit) {
				block.style.borderCollapse = ancestorStyle.borderCollapse;
				block.style.borderSpacingHorizontal = ancestorStyle.borderSpacingHorizontal;
				block.style.borderSpacingVertical = ancestorStyle.borderSpacingVertical;
			}
			if (ancestorTag == "table") {
				// The compact renderer represents a table through its cell blocks.
				// Project only table sizing/placement metadata needed by that
				// representation; backgrounds and text properties remain non-inherited.
				if (block.style.width == -1 && block.style.widthPercent == -1) {
					block.style.width = ancestorStyle.width;
					block.style.widthPercent = ancestorStyle.widthPercent;
				}
				if (block.style.maxWidth == -1 && block.style.maxWidthPercent == -1) {
					block.style.maxWidth = ancestorStyle.maxWidth;
					block.style.maxWidthPercent = ancestorStyle.maxWidthPercent;
				}
				if (block.style.marginLeft == -1) block.style.marginLeft = ancestorStyle.marginLeft;
				if (block.style.marginRight == -1) block.style.marginRight = ancestorStyle.marginRight;
			}
			if ((ancestorTag == "ul" || ancestorTag == "ol") && block.style.listStyleType == ListStyleType::Inherit) {
				block.style.listStyleType = ancestorStyle.listStyleType;
				block.style.listStyleNone = ancestorStyle.listStyleNone;
			}
			if (!block.style.hasBorderTop && ancestorStyle.hasBorderTop) {
				block.style.hasBorderTop = true;
				block.style.borderTopWidth = ancestorStyle.borderTopWidth;
				block.style.borderTopStyle = ancestorStyle.borderTopStyle;
				block.style.borderTopColor = ancestorStyle.borderTopColor;
			}
			if (!block.style.hasBorderRight && ancestorStyle.hasBorderRight) {
				block.style.hasBorderRight = true;
				block.style.borderRightWidth = ancestorStyle.borderRightWidth;
				block.style.borderRightStyle = ancestorStyle.borderRightStyle;
				block.style.borderRightColor = ancestorStyle.borderRightColor;
			}
			if (!block.style.hasBorderBottom && ancestorStyle.hasBorderBottom) {
				block.style.hasBorderBottom = true;
				block.style.borderBottomWidth = ancestorStyle.borderBottomWidth;
				block.style.borderBottomStyle = ancestorStyle.borderBottomStyle;
				block.style.borderBottomColor = ancestorStyle.borderBottomColor;
			}
			if (!block.style.hasBorderLeft && ancestorStyle.hasBorderLeft) {
				block.style.hasBorderLeft = true;
				block.style.borderLeftWidth = ancestorStyle.borderLeftWidth;
				block.style.borderLeftStyle = ancestorStyle.borderLeftStyle;
				block.style.borderLeftColor = ancestorStyle.borderLeftColor;
			}
		}
	}
}

static DocBlock makeTextBlock(BlockType type,
	const std::string& tagName,
	const std::string& text,
	const std::string& url,
	const std::string& className,
	const std::string& id,
	const std::vector<HtmlElementRef>& ancestors = {},
	const std::string& inlineStyle = {})
{
	DocBlock block;
	block.type = type;
	block.tagName = tagName;
	block.className = className;
	block.id = id;
	block.inlineStyle = inlineStyle;
	block.ancestors = ancestors;
	block.text = text;
	block.url = url;
	return block;
}

// Block-tag context: what block is currently open.
enum class OpenTag : uint8_t {
	None  = 0,
	H1, H2, H3,
	P,
	A,
	Li,
	Dt,
	Dd,
	Figcaption,
	Title,
	Pre,   // <pre> block — whitespace preserved
	Caption,
	TableCell,
	ButtonSubmit,
	Textarea,
	Option,
};

struct ParserState {
	WebDocument  doc;
	std::string  textBuf;   // accumulated character data for current block
	std::string  hrefBuf;   // href of the open <a> tag
	std::string  classBuf;
	std::string  idBuf;
	std::string  styleBuf;
	std::vector<HtmlElementRef> openElements;
	uint64_t     nextElementSerial = 1;
	OpenTag      open    = OpenTag::None;
	bool         inScript = false;
	bool         inStyle  = false;
	bool         inPre    = false; // true inside <pre>: preserve whitespace
	bool         bodyReached = false; // ignore content before <body> except <title>
	bool         inForm = false;
	int          currentFormIndex = -1;
	std::string  currentFormAction;
	std::string  currentFormMethod;
	std::string  currentFormEncoding;
	bool         currentFormUnsupported = false;
	std::string  currentTextareaName;
	std::string  currentTextareaClass;
	std::string  currentTextareaId;
	int          currentTextareaRows = 0;
	int          currentTextareaCols = 0;
	bool         inSelect = false;
	std::string  currentSelectName;
	std::string  currentSelectClass;
	std::string  currentSelectId;
	std::vector<FormOption> currentSelectOptions;
	std::string  currentOptionValue;
	bool         currentOptionSelected = false;
	std::string  currentTableCellText;
	std::string  currentTableCaptionText;
	bool         currentTableCellHeader = false;
	std::string  currentTableCellHref;
};

static HtmlElementRef elementRefFromTagBody(const std::string& tagName, const std::string& tagBody)
{
	HtmlElementRef element;
	element.tagName = toLower(tagName);
	element.className = extractAttr(tagBody, "class");
	element.id = extractAttr(tagBody, "id");
	element.inlineStyle = extractAttr(tagBody, "style");
	return element;
}

static std::vector<HtmlElementRef> captureBlockAncestors(const ParserState& st)
{
	std::vector<HtmlElementRef> ancestors = st.openElements;
	switch (st.open) {
	case OpenTag::H1:
	case OpenTag::H2:
	case OpenTag::H3:
	case OpenTag::P:
	case OpenTag::A:
	case OpenTag::Li:
	case OpenTag::Dt:
	case OpenTag::Dd:
	case OpenTag::Figcaption:
	case OpenTag::Title:
	case OpenTag::Pre:
	case OpenTag::Caption:
	case OpenTag::TableCell:
	case OpenTag::ButtonSubmit:
	case OpenTag::Textarea:
	case OpenTag::Option:
		if (!ancestors.empty()) ancestors.pop_back();
		break;
	default:
		break;
	}
	return ancestors;
}

static std::string openTagName(OpenTag tag)
{
	switch (tag) {
	case OpenTag::H1: return "h1";
	case OpenTag::H2: return "h2";
	case OpenTag::H3: return "h3";
	case OpenTag::P: return "p";
	case OpenTag::A: return "a";
	case OpenTag::Li: return "li";
	case OpenTag::Dt: return "dt";
	case OpenTag::Dd: return "dd";
	case OpenTag::Figcaption: return "figcaption";
	case OpenTag::Title: return "title";
	case OpenTag::Pre: return "pre";
	case OpenTag::Caption: return "caption";
	case OpenTag::TableCell: return "td";
	case OpenTag::ButtonSubmit: return "button";
	case OpenTag::Textarea: return "textarea";
	case OpenTag::Option: return "option";
	default: return "";
	}
}

static void loadStylesheetForDocument(ParserState& st, const std::string& href)
{
	if (href.empty()) {
		++st.doc.cssDiagnostics.unsupportedExternalStylesheetCount;
		return;
	}
	if (st.doc.cssDiagnostics.externalStylesheetLoadedCount >= 8) {
		++st.doc.cssDiagnostics.unsupportedExternalStylesheetCount;
		st.doc.cssDiagnostics.styleBlockCapped = true;
		return;
	}
	const std::string url = resolveRelativeUrl(st.doc.url, href);
	bool loaded = false;
#if !defined(GXOS_BARE_METAL)
	if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) {
		HttpResponse response = fetchHttpUrl(url);
		const std::string contentType = toLower(trim(response.contentType));
		const bool contentTypeLooksCss = contentType.empty() ||
			contentType.find("text/css") != std::string::npos ||
			contentType.find("application/css") != std::string::npos;
		if (response.ok() && !response.body.empty() && contentTypeLooksCss) {
			parseEmbeddedCss(st.doc, response.body);
			loaded = true;
		}
	}
#endif
	if (loaded) {
		++st.doc.cssDiagnostics.externalStylesheetLoadedCount;
	} else {
		++st.doc.cssDiagnostics.unsupportedExternalStylesheetCount;
	}
}

static void pushElement(ParserState& st, const HtmlElementRef& element)
{
	if (!element.inlineStyle.empty()) {
		++st.doc.cssDiagnostics.inlineStyleCount;
		st.doc.cssDiagnostics.cssDetected = true;
	}
	HtmlElementRef pushed = element;
	pushed.serial = st.nextElementSerial++;
	st.openElements.push_back(std::move(pushed));
}

static void popElementByName(ParserState& st, const std::string& tagName)
{
	std::string target = toLower(tagName);
	for (int i = static_cast<int>(st.openElements.size()) - 1; i >= 0; --i) {
		if (toLower(st.openElements[static_cast<size_t>(i)].tagName) == target) {
			st.openElements.resize(static_cast<size_t>(i));
			return;
		}
	}
}

// Flush textBuf into a DocBlock, if non-empty.
static void flushText(ParserState& st)
{
	std::string t;
	if (st.inPre || st.open == OpenTag::Textarea) {
		// Inside <pre>: decode entities but preserve whitespace/newlines.
		t = decodeEntities(st.textBuf);
		if (st.inPre) {
			// Strip a single leading newline that immediately follows <pre>
			if (!t.empty() && t[0] == '\n') t = t.substr(1);
			// Strip a single trailing newline before </pre>
			if (!t.empty() && t.back() == '\n') t.pop_back();
		}
	} else {
		t = trim(collapseWs(decodeEntities(st.textBuf)));
	}
	st.textBuf.clear();
	if (t.empty()) return;
	if (st.open == OpenTag::TableCell) {
		if (!st.currentTableCellText.empty()) st.currentTableCellText += ' ';
		st.currentTableCellText += t;
		return;
	}
	if (st.open == OpenTag::Caption) {
		if (!st.currentTableCaptionText.empty()) st.currentTableCaptionText += ' ';
		st.currentTableCaptionText += t;
		return;
	}
	std::vector<HtmlElementRef> ancestors = captureBlockAncestors(st);

	switch (st.open) {
	case OpenTag::H1:
	case OpenTag::H2:
	case OpenTag::H3:
		st.doc.blocks.push_back(makeTextBlock(BlockType::Heading,
			st.open == OpenTag::H1 ? "h1" : (st.open == OpenTag::H2 ? "h2" : "h3"),
			t,
			"",
			st.classBuf,
			st.idBuf,
			ancestors,
			st.styleBuf));
		break;
	case OpenTag::P:
		st.doc.blocks.push_back(makeTextBlock(BlockType::Paragraph, "p", t, "", st.classBuf, st.idBuf, ancestors, st.styleBuf));
		break;
	case OpenTag::Li:
		st.doc.blocks.push_back(makeTextBlock(BlockType::ListItem, "li", t, "", st.classBuf, st.idBuf, ancestors, st.styleBuf));
		break;
	case OpenTag::Dt:
		st.doc.blocks.push_back(makeTextBlock(BlockType::Paragraph, "dt", t, "", st.classBuf, st.idBuf, ancestors, st.styleBuf));
		break;
	case OpenTag::Dd:
		st.doc.blocks.push_back(makeTextBlock(BlockType::Paragraph, "dd", t, "", st.classBuf, st.idBuf, ancestors, st.styleBuf));
		break;
	case OpenTag::Figcaption:
		st.doc.blocks.push_back(makeTextBlock(BlockType::Paragraph, "figcaption", t, "", st.classBuf, st.idBuf, ancestors, st.styleBuf));
		break;
	case OpenTag::Pre:
		st.doc.blocks.push_back(makeTextBlock(BlockType::Preformatted, "pre", t, "", st.classBuf, st.idBuf, ancestors, st.styleBuf));
		break;
	case OpenTag::A:
		if (!st.hrefBuf.empty())
			st.doc.blocks.push_back(makeTextBlock(BlockType::Link, "a", t, st.hrefBuf, st.classBuf, st.idBuf, ancestors, st.styleBuf));
		else
			st.doc.blocks.push_back(makeTextBlock(BlockType::Paragraph, "p", t, "", st.classBuf, st.idBuf, ancestors, st.styleBuf));
		break;
	case OpenTag::Title:
		st.doc.title = t;
		break;
	case OpenTag::ButtonSubmit: {
		DocBlock block;
		block.type = BlockType::FormSubmit;
		block.tagName = "button";
		block.text = t.empty() ? "Submit" : t;
		block.submitLabel = block.text;
		block.formIndex = st.currentFormIndex;
		block.formAction = st.currentFormAction.empty() ? st.doc.url : st.currentFormAction;
		block.formMethod = st.currentFormMethod.empty() ? "get" : st.currentFormMethod;
		block.formEncoding = st.currentFormEncoding.empty() ? "application/x-www-form-urlencoded" : st.currentFormEncoding;
		block.formUnsupported = st.currentFormUnsupported;
		block.ancestors = ancestors;
		block.inlineStyle = st.styleBuf;
		st.doc.blocks.push_back(std::move(block));
		++st.doc.formsDiagnostics.submitCount;
		break;
	}
	case OpenTag::Textarea: {
		DocBlock block;
		block.type = BlockType::FormTextarea;
		block.tagName = "textarea";
		block.className = st.currentTextareaClass;
		block.id = st.currentTextareaId;
		block.text = t;
		block.inputValue = t;
		block.inputName = st.currentTextareaName;
		block.inputType = "textarea";
		block.formIndex = st.currentFormIndex;
		block.formAction = st.currentFormAction.empty() ? st.doc.url : st.currentFormAction;
		block.formMethod = st.currentFormMethod.empty() ? "get" : st.currentFormMethod;
		block.formEncoding = st.currentFormEncoding.empty() ? "application/x-www-form-urlencoded" : st.currentFormEncoding;
		block.visibleRows = st.currentTextareaRows > 0 ? st.currentTextareaRows : 4;
		block.visibleCols = st.currentTextareaCols > 0 ? st.currentTextareaCols : 40;
		block.formUnsupported = st.currentFormUnsupported;
		block.ancestors = ancestors;
		block.inlineStyle = st.styleBuf;
		st.doc.blocks.push_back(std::move(block));
		++st.doc.formsDiagnostics.textareaCount;
		break;
	}
	case OpenTag::Option: {
		FormOption option;
		option.text = t;
		option.value = st.currentOptionValue.empty() ? t : st.currentOptionValue;
		option.selected = st.currentOptionSelected;
		st.currentSelectOptions.push_back(std::move(option));
		break;
	}
	default:
		// Text outside a known block: emit as paragraph if body is active.
		if (st.bodyReached)
			st.doc.blocks.push_back(makeTextBlock(BlockType::Paragraph, "p", t, "", "", "", ancestors, st.styleBuf));
		break;
	}
}

// Handle an opening tag.  tagBody is everything inside <...>, e.g. "a href=\"x\""
static void handleOpenTag(ParserState& st, const std::string& tagBody)
{
	// Extract the tag name (first token before whitespace or /).
	std::string name;
	for (char c : tagBody) {
		if (c == ' ' || c == '\t' || c == '/' || c == '>') break;
		name += c;
	}
	name = toLower(name);
	HtmlElementRef elementRef = elementRefFromTagBody(name, tagBody);

	// Handle skip-content blocks first.
	if (name == "script") { flushText(st); st.inScript = true; return; }
	if (name == "style")  { flushText(st); st.inStyle  = true; return; }
	if (name == "link") {
		std::string rel = toLower(trim(extractAttr(tagBody, "rel")));
		if (rel == "stylesheet") loadStylesheetForDocument(st, trim(decodeEntities(extractAttr(tagBody, "href"))));
		return;
	}

	if (st.inScript) return;
	if (st.inStyle) return;

	// Mark body reached.
	if (name == "body") {
		st.bodyReached = true;
		st.doc.hasBodyElement = true;
		st.doc.bodyElement = elementRef;
		pushElement(st, elementRef);
		return;
	}

	if (name == "blockquote" || name == "figure" || name == "dl") {
		flushText(st);
		if (!st.bodyReached) return;
		pushElement(st, elementRef);
		return;
	}

	if (name == "dt" || name == "dd" || name == "figcaption") {
		flushText(st);
		if (!st.bodyReached) return;
		st.classBuf = extractAttr(tagBody, "class");
		st.idBuf = extractAttr(tagBody, "id");
		st.styleBuf = extractAttr(tagBody, "style");
		st.open = name == "dt" ? OpenTag::Dt : (name == "dd" ? OpenTag::Dd : OpenTag::Figcaption);
		pushElement(st, elementRef);
		return;
	}

	if (name == "hr") {
		flushText(st);
		if (!st.bodyReached) return;
		DocBlock block;
		block.type = BlockType::Paragraph;
		block.tagName = "hr";
		block.className = extractAttr(tagBody, "class");
		block.id = extractAttr(tagBody, "id");
		block.inlineStyle = extractAttr(tagBody, "style");
		block.ancestors = captureBlockAncestors(st);
		st.doc.blocks.push_back(std::move(block));
		return;
	}

	// Void / structural tags with no direct content effect but meaningful CSS scope.
	if (name == "html" || name == "head" || name == "ul" || name == "ol" ||
		name == "div" || name == "span" || name == "strong" || name == "b" ||
		name == "em" || name == "i" || name == "small" || name == "code" ||
		name == "cite" || name == "q" ||
		name == "kbd" || name == "samp" || name == "section" ||
		name == "article" || name == "header" || name == "footer" || name == "nav" ||
		name == "main" || name == "table" || name == "thead" || name == "tbody" ||
		name == "tfoot" || name == "tr" || name == "noscript" || name == "form") {
		if (name == "form") {
			flushText(st);
			st.inForm = true;
			st.currentFormIndex = st.doc.formsDiagnostics.formCount++;
			st.currentFormAction = trim(decodeEntities(extractAttr(tagBody, "action")));
			if (st.currentFormAction.empty()) st.currentFormAction = st.doc.url;
			st.currentFormAction = resolveRelativeUrl(st.doc.url, st.currentFormAction);
			st.currentFormMethod = toLower(trim(extractAttr(tagBody, "method")));
			if (st.currentFormMethod.empty()) st.currentFormMethod = "get";
			st.currentFormEncoding = toLower(trim(decodeEntities(extractAttr(tagBody, "enctype"))));
			if (st.currentFormEncoding.empty()) st.currentFormEncoding = "application/x-www-form-urlencoded";
			const bool unsupportedMethod = !supportedFormMethod(st.currentFormMethod);
			const bool unsupportedEncoding = !supportedFormEncoding(st.currentFormEncoding);
			st.currentFormUnsupported = unsupportedMethod || unsupportedEncoding;
			if (unsupportedMethod) st.doc.formsDiagnostics.hasUnsupportedMethod = true;
			if (unsupportedEncoding) st.doc.formsDiagnostics.hasUnsupportedEncoding = true;
		}
		pushElement(st, elementRef);
		return;
	}

	if (name == "caption") {
		flushText(st);
		st.open = OpenTag::Caption;
		st.currentTableCaptionText.clear();
		st.classBuf = extractAttr(tagBody, "class");
		st.idBuf = extractAttr(tagBody, "id");
		st.styleBuf = extractAttr(tagBody, "style");
		pushElement(st, elementRef);
		return;
	}

	if (name == "td" || name == "th") {
		flushText(st);
		st.open = OpenTag::TableCell;
		st.currentTableCellText.clear();
		st.currentTableCellHeader = (name == "th");
		st.currentTableCellHref.clear();
		st.classBuf = extractAttr(tagBody, "class");
		st.idBuf = extractAttr(tagBody, "id");
		st.styleBuf = extractAttr(tagBody, "style");
		pushElement(st, elementRef);
		return;
	}

	if (name == "div" || name == "span" || name == "section" || name == "article" ||
		name == "header" || name == "footer" || name == "nav" || name == "main" ||
		name == "table" || name == "thead" || name == "tbody" || name == "tfoot" ||
		name == "tr" || name == "ul" || name == "ol" || name == "noscript" ||
		name == "html" || name == "head") {
		pushElement(st, elementRef);
		return;
	}

	if (name == "input") {
		flushText(st);
		std::string type = toLower(trim(extractAttr(tagBody, "type")));
		if (type.empty()) type = "text";
		if (type == "text" || type == "search") {
			DocBlock block;
			block.type = BlockType::FormTextInput;
			block.tagName = "input";
			block.className = extractAttr(tagBody, "class");
			block.id = extractAttr(tagBody, "id");
			block.formIndex = st.currentFormIndex;
			block.formAction = st.currentFormAction.empty() ? st.doc.url : st.currentFormAction;
			block.formMethod = st.currentFormMethod.empty() ? "get" : st.currentFormMethod;
			block.formEncoding = st.currentFormEncoding.empty() ? "application/x-www-form-urlencoded" : st.currentFormEncoding;
			block.inputName = decodeEntities(extractAttr(tagBody, "name"));
			block.inputValue = decodeEntities(extractAttr(tagBody, "value"));
			block.inputType = type;
			block.placeholder = decodeEntities(extractAttr(tagBody, "placeholder"));
			block.formUnsupported = st.currentFormUnsupported;
			block.text = block.inputValue;
			block.ancestors = captureBlockAncestors(st);
			block.inlineStyle = extractAttr(tagBody, "style");
			if (!block.inlineStyle.empty()) {
				++st.doc.cssDiagnostics.inlineStyleCount;
				st.doc.cssDiagnostics.cssDetected = true;
			}
			st.doc.blocks.push_back(std::move(block));
			++st.doc.formsDiagnostics.textInputCount;
		} else if (type == "checkbox" || type == "radio") {
			DocBlock block;
			block.type = type == "checkbox" ? BlockType::FormCheckbox : BlockType::FormRadio;
			block.tagName = "input";
			block.className = extractAttr(tagBody, "class");
			block.id = extractAttr(tagBody, "id");
			block.formIndex = st.currentFormIndex;
			block.formAction = st.currentFormAction.empty() ? st.doc.url : st.currentFormAction;
			block.formMethod = st.currentFormMethod.empty() ? "get" : st.currentFormMethod;
			block.formEncoding = st.currentFormEncoding.empty() ? "application/x-www-form-urlencoded" : st.currentFormEncoding;
			block.inputName = decodeEntities(extractAttr(tagBody, "name"));
			block.inputValue = decodeEntities(extractAttr(tagBody, "value"));
			if (block.inputValue.empty()) block.inputValue = "on";
			block.inputType = type;
			block.checked = hasAttr(tagBody, "checked");
			block.text = block.inputName.empty() ? block.inputValue : block.inputName;
			block.formUnsupported = st.currentFormUnsupported;
			block.ancestors = captureBlockAncestors(st);
			block.inlineStyle = extractAttr(tagBody, "style");
			if (!block.inlineStyle.empty()) {
				++st.doc.cssDiagnostics.inlineStyleCount;
				st.doc.cssDiagnostics.cssDetected = true;
			}
			st.doc.blocks.push_back(std::move(block));
			if (type == "checkbox") ++st.doc.formsDiagnostics.checkboxCount;
			else ++st.doc.formsDiagnostics.radioCount;
		} else if (type == "submit") {
			DocBlock block;
			block.type = BlockType::FormSubmit;
			block.tagName = "input";
			block.className = extractAttr(tagBody, "class");
			block.id = extractAttr(tagBody, "id");
			block.formIndex = st.currentFormIndex;
			block.formAction = st.currentFormAction.empty() ? st.doc.url : st.currentFormAction;
			block.formMethod = st.currentFormMethod.empty() ? "get" : st.currentFormMethod;
			block.formEncoding = st.currentFormEncoding.empty() ? "application/x-www-form-urlencoded" : st.currentFormEncoding;
			block.submitLabel = decodeEntities(extractAttr(tagBody, "value"));
			if (block.submitLabel.empty()) block.submitLabel = "Submit";
			block.text = block.submitLabel;
			block.formUnsupported = st.currentFormUnsupported;
			block.ancestors = captureBlockAncestors(st);
			block.inlineStyle = extractAttr(tagBody, "style");
			if (!block.inlineStyle.empty()) {
				++st.doc.cssDiagnostics.inlineStyleCount;
				st.doc.cssDiagnostics.cssDetected = true;
			}
			st.doc.blocks.push_back(std::move(block));
			++st.doc.formsDiagnostics.submitCount;
		} else {
			++st.doc.formsDiagnostics.unsupportedControlCount;
		}
		return;
	}

	if (name == "textarea") {
		flushText(st);
		st.open = OpenTag::Textarea;
		st.currentTextareaName = decodeEntities(extractAttr(tagBody, "name"));
		st.currentTextareaClass = extractAttr(tagBody, "class");
		st.currentTextareaId = extractAttr(tagBody, "id");
		st.currentTextareaRows = parsePositiveIntAttr(tagBody, "rows");
		st.currentTextareaCols = parsePositiveIntAttr(tagBody, "cols");
		st.styleBuf = extractAttr(tagBody, "style");
		pushElement(st, elementRef);
		st.textBuf.clear();
		return;
	}

	if (name == "select") {
		flushText(st);
		st.inSelect = true;
		st.currentSelectName = decodeEntities(extractAttr(tagBody, "name"));
		st.currentSelectClass = extractAttr(tagBody, "class");
		st.currentSelectId = extractAttr(tagBody, "id");
		st.currentSelectOptions.clear();
		st.styleBuf = extractAttr(tagBody, "style");
		pushElement(st, elementRef);
		st.open = OpenTag::None;
		return;
	}

	if (name == "option" && st.inSelect) {
		flushText(st);
		st.open = OpenTag::Option;
		st.currentOptionValue = decodeEntities(extractAttr(tagBody, "value"));
		st.currentOptionSelected = hasAttr(tagBody, "selected");
		st.styleBuf = extractAttr(tagBody, "style");
		pushElement(st, elementRef);
		st.textBuf.clear();
		return;
	}

	// <br> – inside <pre> append a newline to the buffer; outside flush as a
	// line break only if there is pending text (avoids empty Paragraph blocks).
	if (name == "br") {
		++st.doc.cssDiagnostics.lineBreakCount;
		if (st.inPre) {
			st.textBuf += '\n';
		} else if (st.open == OpenTag::TableCell) {
			flushText(st);
			st.currentTableCellText += ' ';
		} else if (!trim(collapseWs(st.textBuf)).empty()) {
			flushText(st);
		}
		return;
	}

	if (name == "img") {
		flushText(st);
		std::string src = trim(decodeEntities(extractAttr(tagBody, "src")));
		if (src.empty()) return;
		std::string alt = decodeEntities(extractAttr(tagBody, "alt"));
		DocBlock block;
		block.type = BlockType::Image;
		block.text = alt;
		block.url = resolveRelativeUrl(st.doc.url, src);
		block.src = src;
		block.alt = alt;
		block.tagName = "img";
		block.className = extractAttr(tagBody, "class");
		block.id = extractAttr(tagBody, "id");
		block.inlineStyle = extractAttr(tagBody, "style");
		if (!block.inlineStyle.empty()) {
			++st.doc.cssDiagnostics.inlineStyleCount;
			st.doc.cssDiagnostics.cssDetected = true;
		}
		bool widthAttrClamped = false;
		bool heightAttrClamped = false;
		block.width = parsePositiveIntAttr(tagBody, "width", &widthAttrClamped);
		block.height = parsePositiveIntAttr(tagBody, "height", &heightAttrClamped);
		block.imageSizeAttrClamped = widthAttrClamped || heightAttrClamped;
		block.ancestors = captureBlockAncestors(st);
		st.doc.blocks.push_back(std::move(block));
		st.open = OpenTag::None;
		st.hrefBuf.clear();
		st.classBuf.clear();
		st.idBuf.clear();
		st.styleBuf.clear();
		return;
	}

	// Block-level tags: flush any pending text, then open new context.
	flushText(st);

	st.classBuf = extractAttr(tagBody, "class");
	st.idBuf = extractAttr(tagBody, "id");
	st.styleBuf = extractAttr(tagBody, "style");

	if (name == "h1")    { st.open = OpenTag::H1;    pushElement(st, elementRef); return; }
	if (name == "h2")    { st.open = OpenTag::H2;    pushElement(st, elementRef); return; }
	if (name == "h3")    { st.open = OpenTag::H3;    pushElement(st, elementRef); return; }
	if (name == "p")     { st.open = OpenTag::P;     pushElement(st, elementRef); return; }
	if (name == "li")    { st.open = OpenTag::Li;    pushElement(st, elementRef); return; }
	if (name == "title") { st.open = OpenTag::Title; pushElement(st, elementRef); return; }

	if (name == "pre") {
		st.open  = OpenTag::Pre;
		st.inPre = true;
		pushElement(st, elementRef);
		return;
	}
	// <code>: if a <pre> is already open, stay in it; otherwise treat as plain text.
	if (name == "code") {
		pushElement(st, elementRef);
		return;
	}

	if (name == "a") {
		std::string href = extractAttr(tagBody, "href");
		if (!href.empty()) {
			// Resolve relative URL against the document base.
			st.hrefBuf = resolveRelativeUrl(st.doc.url, href);
			if (st.open == OpenTag::TableCell) {
				st.currentTableCellHref = st.hrefBuf;
			}
		} else {
			st.hrefBuf.clear();
		}
		if (st.open != OpenTag::TableCell) {
			st.open = OpenTag::A;
		}
		pushElement(st, elementRef);
		return;
	}

	if (name == "button") {
		std::string type = toLower(trim(extractAttr(tagBody, "type")));
		if (type.empty() || type == "submit") {
			st.open = OpenTag::ButtonSubmit;
			pushElement(st, elementRef);
		} else {
			++st.doc.formsDiagnostics.unsupportedControlCount;
		}
		return;
	}

	// Unknown open tag: leave current open context unchanged so text inside
	// unknown tags flows into the current block.
}

// Handle a closing tag.
static void handleCloseTag(ParserState& st, const std::string& tagName)
{
	std::string name = toLower(tagName);

	if (name == "script") { st.inScript = false; st.textBuf.clear(); return; }
	if (name == "style")  {
		parseEmbeddedCss(st.doc, st.textBuf);
		st.inStyle  = false;
		st.textBuf.clear();
		return;
	}

	// Close block-level contexts.
	if (name == "h1" || name == "h2" || name == "h3" ||
		name == "p"  || name == "li" || name == "dt" || name == "dd" || name == "figcaption" || name == "title" ||
		name == "button" || name == "textarea" || name == "option") {
		flushText(st);
		popElementByName(st, name);
		st.open    = OpenTag::None;
		st.hrefBuf.clear();
		st.classBuf.clear();
		st.idBuf.clear();
		st.styleBuf.clear();
		if (name == "textarea") {
			st.currentTextareaName.clear();
			st.currentTextareaClass.clear();
			st.currentTextareaId.clear();
		}
		if (name == "option") {
			st.currentOptionValue.clear();
			st.currentOptionSelected = false;
		}
	}
	if (name == "a") {
		if (st.open != OpenTag::TableCell) {
			flushText(st);
			popElementByName(st, name);
			st.open = OpenTag::None;
			st.hrefBuf.clear();
			st.classBuf.clear();
			st.idBuf.clear();
			st.styleBuf.clear();
		} else {
			popElementByName(st, name);
			st.currentTableCellHref = st.currentTableCellHref.empty() ? st.hrefBuf : st.currentTableCellHref;
			st.hrefBuf.clear();
		}
	}
	if (name == "blockquote" || name == "figure" || name == "dl") {
		flushText(st);
		popElementByName(st, name);
	}
	if (name == "caption") {
		flushText(st);
		if (!st.currentTableCaptionText.empty()) {
			DocBlock block = makeTextBlock(BlockType::Paragraph, "caption",
				st.currentTableCaptionText, "", st.classBuf, st.idBuf, captureBlockAncestors(st), st.styleBuf);
			st.doc.blocks.push_back(std::move(block));
		}
		st.currentTableCaptionText.clear();
		st.open = OpenTag::None;
		popElementByName(st, name);
		st.classBuf.clear();
		st.idBuf.clear();
		st.styleBuf.clear();
	}
	if (name == "td" || name == "th") {
		flushText(st);
		DocBlock block = makeTextBlock(BlockType::Paragraph, name, st.currentTableCellText, "",
			st.classBuf, st.idBuf, captureBlockAncestors(st), st.styleBuf);
		if (!st.currentTableCellHref.empty()) {
			block.url = st.currentTableCellHref;
		}
		st.doc.blocks.push_back(std::move(block));
		st.currentTableCellText.clear();
		st.currentTableCellHeader = false;
		st.currentTableCellHref.clear();
		st.open = OpenTag::None;
		popElementByName(st, name);
		st.classBuf.clear();
		st.idBuf.clear();
		st.styleBuf.clear();
	}
	if (name == "select") {
		flushText(st);
		DocBlock block;
		block.type = BlockType::FormSelect;
		block.tagName = "select";
		block.className = st.currentSelectClass;
		block.id = st.currentSelectId;
		block.formIndex = st.currentFormIndex;
		block.formAction = st.currentFormAction.empty() ? st.doc.url : st.currentFormAction;
		block.formMethod = st.currentFormMethod.empty() ? "get" : st.currentFormMethod;
		block.formEncoding = st.currentFormEncoding.empty() ? "application/x-www-form-urlencoded" : st.currentFormEncoding;
		block.inputName = st.currentSelectName;
		block.inputType = "select";
		block.options = st.currentSelectOptions;
		block.formUnsupported = st.currentFormUnsupported;
		for (int i = 0; i < static_cast<int>(block.options.size()); ++i) {
			if (block.options[i].selected) {
				block.selectedOption = i;
				break;
			}
		}
		if (block.selectedOption < 0 && !block.options.empty()) block.selectedOption = 0;
		if (block.selectedOption >= 0 && block.selectedOption < static_cast<int>(block.options.size())) {
			block.inputValue = block.options[static_cast<size_t>(block.selectedOption)].value;
			block.text = block.options[static_cast<size_t>(block.selectedOption)].text;
		}
		block.ancestors = st.openElements;
		if (!block.ancestors.empty()) block.ancestors.pop_back();
		block.inlineStyle = st.styleBuf;
		st.doc.blocks.push_back(std::move(block));
		++st.doc.formsDiagnostics.selectCount;
		st.inSelect = false;
		st.currentSelectName.clear();
		st.currentSelectClass.clear();
		st.currentSelectId.clear();
		st.currentSelectOptions.clear();
		st.styleBuf.clear();
		popElementByName(st, name);
		st.open = OpenTag::None;
	}
	if (name == "form") {
		flushText(st);
		st.inForm = false;
		st.currentFormIndex = -1;
		st.currentFormAction.clear();
		st.currentFormMethod.clear();
		st.currentFormEncoding.clear();
		st.currentFormUnsupported = false;
		popElementByName(st, name);
	}
	if (name == "pre") {
		flushText(st);
		st.open  = OpenTag::None;
		st.inPre = false;
		st.classBuf.clear();
		st.idBuf.clear();
		st.styleBuf.clear();
		popElementByName(st, name);
	}
	if (name == "body") {
		popElementByName(st, name);
	}
	if (name == "div" || name == "section" || name == "article" ||
		name == "header" || name == "footer" || name == "nav" || name == "main" ||
		name == "figure" || name == "blockquote" || name == "dl") {
		flushText(st);
		popElementByName(st, name);
		return;
	}
	if (name == "strong" || name == "b" || name == "em" || name == "i" || name == "code" ||
		name == "small" || name == "kbd" || name == "samp" ||
		name == "cite" || name == "q" ||
		name == "span" ||
		name == "table" || name == "thead" || name == "tbody" || name == "tfoot" ||
		name == "tr" || name == "ul" || name == "ol" || name == "noscript" || name == "html" || name == "head") {
		popElementByName(st, name);
	}
	// </code> inside <pre>: stay in pre context.
	// </code> outside: nothing to do.
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// resolveRelativeUrl
// ---------------------------------------------------------------------------
std::string resolveRelativeUrl(const std::string& base, const std::string& href)
{
	if (href.empty()) return base;

	// Already absolute?
	size_t colon = href.find(':');
	if (colon != std::string::npos) {
		bool scheme = colon > 0;
		for (size_t i = 0; i < colon && scheme; ++i) {
			unsigned char ch = static_cast<unsigned char>(href[i]);
			scheme = std::isalnum(ch) || href[i] == '+' || href[i] == '-' || href[i] == '.';
		}
		if (scheme) return href;
	}

	// Root-relative: preserve the HTTP(S) origin, or use the existing file:// rule.
	if (href[0] == '/') {
		if (base.rfind("http://", 0) == 0 || base.rfind("https://", 0) == 0) {
			size_t authorityStart = base.find("://") + 3;
			size_t authorityEnd = base.find('/', authorityStart);
			if (authorityEnd == std::string::npos) return base + href;
			return base.substr(0, authorityEnd) + href;
		}
		return "file://" + href;
	}

	// Fragment-only ("#...") – stay on the same page.
	if (href[0] == '#') return base;

	// Relative: strip the last path segment from base, append href.
	// base looks like "file:///docs/index.html" or "http://host/docs/index.html"
	size_t end = base.find_first_of("?#");
	std::string baseNoQuery = end == std::string::npos ? base : base.substr(0, end);
	size_t lastSlash = baseNoQuery.rfind('/');
	if (lastSlash == std::string::npos) return "file:///" + href;
	return baseNoQuery.substr(0, lastSlash + 1) + href;
}

// ---------------------------------------------------------------------------
// parseHtml
// ---------------------------------------------------------------------------
WebDocument parseHtml(const std::string& pageUrl, const std::string& htmlText)
{
	ParserState st;
	st.doc.url = pageUrl;

	const size_t len = htmlText.size();
	size_t i = 0;

	while (i < len) {
		char c = htmlText[i];

		// ----------------------------------------------------------------
		// Tag token
		// ----------------------------------------------------------------
		if (c == '<') {
			size_t tagStart = i + 1;
			// Find closing '>',  respecting attribute strings.
			size_t j = tagStart;
			bool inStr = false;
			char strDelim = 0;
			while (j < len) {
				char ch = htmlText[j];
				if (inStr) {
					if (ch == strDelim) inStr = false;
				} else {
					if (ch == '"' || ch == '\'') { inStr = true; strDelim = ch; }
					else if (ch == '>') break;
				}
				++j;
			}
			// j now points to '>' or len if malformed; tag body is [tagStart, j)
			std::string tagBody = htmlText.substr(tagStart, j - tagStart);
			i = (j < len) ? j + 1 : len; // skip past '>'

			if (tagBody.empty()) continue;

			// HTML comment: <!-- ... -->
			if (tagBody.size() >= 2 && tagBody[0] == '!' && tagBody[1] == '-') continue;
			// DOCTYPE
			if (!tagBody.empty() && tagBody[0] == '!') continue;

			bool isClose = (tagBody[0] == '/');
			if (isClose) {
				std::string closeName = tagBody.substr(1);
				// trim trailing whitespace/slash
				while (!closeName.empty() && (closeName.back() == ' ' || closeName.back() == '/'))
					closeName.pop_back();
				handleCloseTag(st, closeName);
			} else {
				handleOpenTag(st, tagBody);
			}
			continue;
		}

		// ----------------------------------------------------------------
		// Character data
		// ----------------------------------------------------------------
		if (!st.inScript) {
			// Inside <pre>, preserve all characters including newlines/spaces.
			// Inside <style>, preserve the raw stylesheet so CSS-lite can parse
			// it when </style> closes. Outside <pre>, flushText() will collapse
			// ordinary document text later.
			st.textBuf += c;
		}
		++i;
	}

	// Flush any trailing text not closed by a tag.
	flushText(st);
	applyDocumentStyles(st.doc);

	// If no title was parsed, use the filename from the URL.
	if (st.doc.title.empty()) {
		size_t slash = pageUrl.rfind('/');
		st.doc.title = (slash != std::string::npos) ? pageUrl.substr(slash + 1) : pageUrl;
	}

	return st.doc;
}

} // namespace web
} // namespace gxos
