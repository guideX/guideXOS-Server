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
#include <cmath>
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
	constexpr int kCssLiteMaxPercentage = 1000;
	constexpr int kCssLiteMaxBorderWidthPx = 12;
	constexpr size_t kCssLiteMaxStyleBlocks = 32;
	constexpr size_t kCssLiteMaxRules = 256;
	constexpr size_t kCssLiteMaxSelectorGroups = 16;
	constexpr size_t kCssLiteMaxSelectorLength = 256;
	constexpr size_t kCssLiteMaxSelectorComponents = 8;
	constexpr size_t kCssLiteMaxSelectorClasses = 8;
	constexpr size_t kCssLiteMaxPseudoClassesPerCompound = 4;
	constexpr size_t kCssLiteMaxNotComponents = 8;
	constexpr size_t kCssLiteMaxNthExpressionLength = 32;
	constexpr int kCssLiteMaxNthCoefficient = 1024;
	constexpr int kCssLiteMaxNthOffset = 4096;
	constexpr size_t kCssLiteMaxCombinatorDepth = 6;
	constexpr size_t kCssLiteMaxStructuralMetadata = 1024;
	constexpr size_t kCssLiteMaxSelectorEvaluationSteps = 64;
	constexpr size_t kCssLiteMaxSiblingScanSteps = 64;
	constexpr size_t kCssLiteMaxDeclarationsPerRule = 64;
	constexpr size_t kCssLiteMaxTotalDeclarations = 2048;
	constexpr size_t kCssLiteMaxCascadeApplicationsPerNode = 512;
	constexpr size_t kCssLiteMaxTotalStyleBytes = 64u * 1024u;
	constexpr size_t kCssLiteMaxInheritanceDepth = 12;
	constexpr size_t kCssLiteMaxEvidenceEntries = 32;
	constexpr size_t kCssLiteMaxEvidenceBytes = 32768;
	constexpr size_t kCssLiteMaxCommentBytes = 1024;
	constexpr size_t kCssLiteMaxStringScanBytes = 4096;
	constexpr size_t kCssLiteMaxDelimiterDepth = 4;
	constexpr size_t kCssLiteMaxContentAggregationOperations = 1024;
	constexpr size_t kCssLiteMaxOpenElementDepth = 1024;
	constexpr size_t kCssLiteMaxVisibleTextBytesPerElement = 1024;
	constexpr size_t kCssLiteMaxInlineItems = 2048;
	constexpr size_t kCssLiteMaxInlineTextBytes = 4096;
	constexpr size_t kCssLiteMaxRecoveryAttemptsPerGroup = 16;
	constexpr size_t kCssLiteMaxEvidenceTokenBytes = 64;
	constexpr size_t kFormMaxControls = 128;
	constexpr size_t kFormMaxValueBytes = 256;
	constexpr size_t kFormMaxPlaceholderBytes = 128;
	constexpr size_t kFormMaxLabelBytes = 128;
	constexpr size_t kFormMaxOptions = 64;
	constexpr size_t kFormMaxOptionTextBytes = 128;
	constexpr int kFormMaxRows = 12;
	constexpr int kFormMaxCols = 80;
	constexpr int kFormMaxSize = 64;

	enum class CssProperty : uint8_t {
		Color = 0,
		Background,
		Bold,
		Italic,
		TextDecoration,
		Display,
		BoxSizing,
		MinWidth,
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
		MinHeight,
		OverflowX,
		OverflowY,
		Visibility,
		Opacity,
		VerticalAlign,
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

// Replace CSS comments with one separator while preserving quoted strings.
// The caller may safely parse the prefix already produced when this returns
// false; the affected remainder is deliberately ignored.
static bool normalizeCssComments(const std::string& input,
	std::string& output,
	CssDiagnostics& diag)
{
	output.clear();
	output.reserve(input.size());
	char quote = 0;
	size_t quotedBytes = 0;
	for (size_t i = 0; i < input.size(); ++i) {
		const char c = input[i];
		if (quote != 0) {
			if (quotedBytes++ >= kCssLiteMaxStringScanBytes) {
				saturatingIncrement(diag.unterminatedStringErrors);
				return false;
			}
			output += c;
			if (c == '\\' && i + 1 < input.size()) {
				output += input[++i];
				if (quotedBytes++ >= kCssLiteMaxStringScanBytes) {
					saturatingIncrement(diag.unterminatedStringErrors);
					return false;
				}
				continue;
			}
			if (c == quote) quote = 0;
			continue;
		}
		if (c == '\'' || c == '"') {
			quote = c;
			quotedBytes = 0;
			output += c;
			continue;
		}
		if (c != '/' || i + 1 >= input.size() || input[i + 1] != '*') {
			output += c;
			continue;
		}

		output += ' ';
		i += 2;
		bool closed = false;
		size_t scanned = 0;
		while (i < input.size()) {
			if (scanned++ >= kCssLiteMaxCommentBytes) {
				saturatingIncrement(diag.commentScanClamps);
				saturatingIncrement(diag.unterminatedCommentErrors);
				return false;
			}
			if (input[i] == '*' && i + 1 < input.size() && input[i + 1] == '/') {
				i += 1;
				closed = true;
				break;
			}
			++i;
		}
		if (!closed) {
			saturatingIncrement(diag.unterminatedCommentErrors);
			return false;
		}
	}
	if (quote != 0) {
		saturatingIncrement(diag.unterminatedStringErrors);
		return false;
	}
	return true;
}

static bool scanCssBalancedRange(const std::string& text,
	size_t start,
	size_t end,
	CssDiagnostics& diag,
	bool allowBraces = false)
{
	int parentheses = 0;
	int brackets = 0;
	int braces = 0;
	char quote = 0;
	for (size_t i = start; i < end; ++i) {
		const char c = text[i];
		if (quote != 0) {
			if (c == '\\' && i + 1 < end) { ++i; continue; }
			if (c == quote) quote = 0;
			continue;
		}
		if (c == '\'' || c == '"') { quote = c; continue; }
		if (c == '(') {
			if (++parentheses > static_cast<int>(kCssLiteMaxDelimiterDepth)) {
				saturatingIncrement(diag.unbalancedParenthesisErrors);
				return false;
			}
		} else if (c == ')') {
			if (parentheses == 0) {
				saturatingIncrement(diag.unbalancedParenthesisErrors);
				return false;
			}
			--parentheses;
		} else if (c == '[') {
			if (++brackets > static_cast<int>(kCssLiteMaxDelimiterDepth)) {
				saturatingIncrement(diag.unbalancedBracketErrors);
				return false;
			}
		} else if (c == ']') {
			if (brackets == 0) {
				saturatingIncrement(diag.unbalancedBracketErrors);
				return false;
			}
			--brackets;
		} else if (allowBraces && c == '{') {
			++braces;
		} else if (allowBraces && c == '}') {
			if (braces == 0) return false;
			--braces;
		}
	}
	if (quote != 0) {
		saturatingIncrement(diag.unterminatedStringErrors);
		return false;
	}
	if (parentheses != 0) {
		saturatingIncrement(diag.unbalancedParenthesisErrors);
		return false;
	}
	if (brackets != 0) {
		saturatingIncrement(diag.unbalancedBracketErrors);
		return false;
	}
	return true;
}

static std::vector<std::string> splitCssSelectorGroups(const std::string& text,
	CssDiagnostics& diag)
{
	std::vector<std::string> groups;
	std::string current;
	bool memberCapped = false;
	auto appendBounded = [&](char c) {
		if (current.size() < kCssLiteMaxSelectorLength + 1) {
			current += c;
		}
		if (current.size() > kCssLiteMaxSelectorLength) {
			if (!memberCapped) {
				memberCapped = true;
				saturatingIncrement(diag.selectorDepthClamps);
			}
		}
	};
	auto finishMember = [&]() {
		groups.push_back(current);
		current.clear();
		memberCapped = false;
	};
	int parentheses = 0;
	int brackets = 0;
	char quote = 0;
	for (size_t i = 0; i < text.size(); ++i) {
		const char c = text[i];
		if (quote != 0) {
			appendBounded(c);
			if (c == '\\' && i + 1 < text.size()) appendBounded(text[++i]);
			else if (c == quote) quote = 0;
			continue;
		}
		if (c == '\\') {
			// Escapes are unsupported by the identifier policy, but the escaped
			// byte remains part of this member so an escaped comma cannot leak
			// the suffix into a new, accidentally valid selector.
			appendBounded(c);
			if (i + 1 < text.size()) appendBounded(text[++i]);
			continue;
		}
		if (c == '\'' || c == '"') { quote = c; appendBounded(c); continue; }
		if (c == '(') {
			if (parentheses < static_cast<int>(kCssLiteMaxDelimiterDepth) + 1) ++parentheses;
			if (parentheses > static_cast<int>(kCssLiteMaxDelimiterDepth)) {
				saturatingIncrement(diag.unbalancedParenthesisErrors);
			}
		} else if (c == ')') {
			if (parentheses == 0) {
				saturatingIncrement(diag.unbalancedParenthesisErrors);
			} else {
				--parentheses;
			}
		} else if (c == '[') {
			if (brackets < static_cast<int>(kCssLiteMaxDelimiterDepth) + 1) ++brackets;
			if (brackets > static_cast<int>(kCssLiteMaxDelimiterDepth)) {
				saturatingIncrement(diag.unbalancedBracketErrors);
			}
		} else if (c == ']') {
			if (brackets == 0) {
				saturatingIncrement(diag.unbalancedBracketErrors);
			} else {
				--brackets;
			}
		} else if (c == ',' && parentheses == 0 && brackets == 0) {
			finishMember();
			continue;
		}
		appendBounded(c);
	}
	if (quote != 0) {
		saturatingIncrement(diag.unterminatedStringErrors);
	}
	if (parentheses != 0) {
		saturatingIncrement(diag.unbalancedParenthesisErrors);
	}
	if (brackets != 0) {
		saturatingIncrement(diag.unbalancedBracketErrors);
	}
	finishMember();
	return groups;
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

static std::string boundedDecodedFormText(const std::string& raw,
	size_t cap,
	FormsDiagnostics& diagnostics)
{
	std::string decoded = decodeEntities(raw);
	if (decoded.size() <= cap) return decoded;
	++diagnostics.controlTextTruncations;
	decoded.resize(cap);
	return decoded;
}

static int parseBoundedFormInt(const std::string& tagBody,
	const std::string& attr,
	int maximum,
	FormsDiagnostics& diagnostics)
{
	const std::string raw = trim(extractAttr(tagBody, attr));
	if (raw.empty()) return 0;
	int result = 0;
	bool sawDigit = false;
	for (char c : raw) {
		if (c < '0' || c > '9') break;
		sawDigit = true;
		if (result > maximum / 10) {
			++diagnostics.controlMetadataClamps;
			return maximum;
		}
		result = result * 10 + (c - '0');
		if (result > maximum) {
			++diagnostics.controlMetadataClamps;
			return maximum;
		}
	}
	if (!sawDigit || result <= 0) return 0;
	return result;
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
		return consumed == value.size() && std::isfinite(out);
	} catch (...) {
		return false;
	}
}

static int roundCssNumber(double value)
{
	if (!std::isfinite(value) || value <= 0.0) return 0;
	if (value >= static_cast<double>(std::numeric_limits<int>::max() - 1))
		return std::numeric_limits<int>::max();
	return static_cast<int>(value + 0.5);
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

static bool parseCssBoundedDimension(const std::string& rawValue,
	CssLengthValue& out,
	CssDiagnostics& diag,
	bool allowAuto,
	bool allowNone,
	bool allowPercent)
{
	out = CssLengthValue();
	std::string value = toLower(trim(rawValue));
	if (value.empty()) return false;
	if (value == "auto") {
		if (!allowAuto) return false;
		out.type = CssLengthType::Auto;
		out.valid = true;
		return true;
	}
	if (value == "none") {
		if (!allowNone) return false;
		out.type = CssLengthType::None;
		out.valid = true;
		return true;
	}

	bool percent = false;
	double scale = 1.0;
	if (value.size() >= 2 && value.substr(value.size() - 2) == "px") {
		value = trim(value.substr(0, value.size() - 2));
	} else if (value.size() >= 2 && value.substr(value.size() - 2) == "em") {
		value = trim(value.substr(0, value.size() - 2));
		scale = 16.0;
	} else if (value.size() >= 2 && value.substr(value.size() - 2) == "pt") {
		value = trim(value.substr(0, value.size() - 2));
		scale = 96.0 / 72.0;
	} else if (!value.empty() && value.back() == '%') {
		if (!allowPercent) return false;
		value.pop_back();
		percent = true;
		scale = 1.0;
	} else {
		// Only unitless zero is accepted in this bounded CSS subset.
		if (value != "0") return false;
	}

	double numeric = 0.0;
	if (!parseCssNumber(value, numeric) || numeric < 0.0) return false;
	if (percent) {
		int percentValue = roundCssNumber(numeric);
		if (percentValue > kCssLiteMaxPercentage) {
			percentValue = kCssLiteMaxPercentage;
			out.clamped = true;
			++diag.clampedValueCount;
			++diag.lengthValueClampCount;
		}
		out.type = CssLengthType::Percent;
		out.value = percentValue;
		out.valid = true;
		return true;
	}

	const double scaled = numeric * scale;
	if (!std::isfinite(scaled) || scaled < 0.0) return false;
	int pixels = roundCssNumber(scaled);
	if (pixels > kCssLiteMaxWidthPx) {
		pixels = kCssLiteMaxWidthPx;
		out.clamped = true;
		++diag.clampedValueCount;
		++diag.lengthValueClampCount;
	}
	out.type = pixels == 0 ? CssLengthType::Zero : CssLengthType::Px;
	out.value = pixels;
	out.valid = true;
	return true;
}

static bool parseCssOpacityValue(const std::string& rawValue, int& outPercent, CssDiagnostics& diag)
{
	std::string value = toLower(trim(rawValue));
	bool percent = !value.empty() && value.back() == '%';
	if (percent) value.pop_back();
	double numeric = 0.0;
	if (!parseCssNumber(value, numeric)) return false;
	if (percent) {
		if (numeric < 0.0 || numeric > 100.0) {
			numeric = std::max(0.0, std::min(100.0, numeric));
			++diag.clampedValueCount;
		}
	} else {
		if (numeric < 0.0 || numeric > 1.0) {
			numeric = std::max(0.0, std::min(1.0, numeric));
			++diag.clampedValueCount;
		}
		numeric *= 100.0;
	}
	outPercent = std::max(0, std::min(100, roundCssNumber(numeric)));
	return true;
}

static bool parseCssVerticalAlignValue(const std::string& rawValue,
	VerticalAlignMode& outMode,
	int& outValue,
	bool& outClamped,
	CssDiagnostics& diag)
{
	std::string value = toLower(trim(rawValue));
	outMode = VerticalAlignMode::Baseline;
	outValue = 0;
	outClamped = false;
	if (value == "baseline") { outMode = VerticalAlignMode::Baseline; return true; }
	if (value == "middle") { outMode = VerticalAlignMode::Middle; return true; }
	if (value == "top") { outMode = VerticalAlignMode::Top; return true; }
	if (value == "bottom") { outMode = VerticalAlignMode::Bottom; return true; }
	if (value == "text-top") { outMode = VerticalAlignMode::TextTop; return true; }
	if (value == "text-bottom") { outMode = VerticalAlignMode::TextBottom; return true; }
	if (value == "sub") { outMode = VerticalAlignMode::Sub; return true; }
	if (value == "super") { outMode = VerticalAlignMode::Super; return true; }
	if (value.empty()) return false;
	bool percent = value.back() == '%';
	if (percent) value.pop_back();
	double numeric = 0.0;
	double scale = 1.0;
	if (!percent && value.size() >= 2 && value.substr(value.size() - 2) == "px") {
		value = trim(value.substr(0, value.size() - 2));
	} else if (!percent && value.size() >= 2 && value.substr(value.size() - 2) == "em") {
		value = trim(value.substr(0, value.size() - 2));
		scale = 16.0;
	} else if (!percent && value.size() >= 2 && value.substr(value.size() - 2) == "pt") {
		value = trim(value.substr(0, value.size() - 2));
		scale = 96.0 / 72.0;
	} else if (!percent && value != "0") {
		return false;
	}
	if (!parseCssNumber(value, numeric)) return false;
	const double scaled = numeric * scale;
	if (!std::isfinite(scaled)) return false;
	int bounded = scaled < 0.0
		? -roundCssNumber(-scaled) : roundCssNumber(scaled);
	const int maxValue = percent ? 100 : 128;
	if (bounded < -maxValue || bounded > maxValue) {
		bounded = std::max(-maxValue, std::min(maxValue, bounded));
		outClamped = true;
		++diag.clampedValueCount;
	}
	outValue = bounded;
	outMode = percent ? VerticalAlignMode::Percent : VerticalAlignMode::LengthPx;
	return true;
}

static bool parseCssSignedInteger(const std::string& raw,
	int maxAbs,
	int& out,
	CssDiagnostics& diag)
{
	if (raw.empty()) return false;
	size_t pos = 0;
	int sign = 1;
	if (raw[pos] == '+' || raw[pos] == '-') {
		sign = raw[pos] == '-' ? -1 : 1;
		if (++pos >= raw.size()) return false;
	}
	uint64_t magnitude = 0;
	for (; pos < raw.size(); ++pos) {
		const char c = raw[pos];
		if (c < '0' || c > '9') return false;
		const uint64_t digit = static_cast<uint64_t>(c - '0');
		if (magnitude > (static_cast<uint64_t>(std::numeric_limits<int>::max()) - digit) / 10u)
			return false;
		magnitude = magnitude * 10u + digit;
	}
	if (magnitude > static_cast<uint64_t>(maxAbs)) {
		magnitude = static_cast<uint64_t>(maxAbs);
		saturatingIncrement(diag.pseudoClassClamps);
	}
	out = sign < 0 ? -static_cast<int>(magnitude) : static_cast<int>(magnitude);
	return true;
}

static bool parseCssNthExpression(const std::string& raw,
	CssNthExpression& out,
	CssDiagnostics& diag)
{
	if (raw.size() > kCssLiteMaxNthExpressionLength) {
		saturatingIncrement(diag.nthExpressionParseErrors);
		return false;
	}
	std::string expression;
	expression.reserve(raw.size());
	for (unsigned char c : raw) {
		if (c <= 32) continue;
		expression += static_cast<char>(std::tolower(c));
	}
	if (expression.empty()) {
		saturatingIncrement(diag.nthExpressionParseErrors);
		return false;
	}
	if (expression == "odd") { out = { 2, 1 }; return true; }
	if (expression == "even") { out = { 2, 0 }; return true; }

	const size_t nPos = expression.find('n');
	if (nPos == std::string::npos) {
		int position = 0;
		if (!parseCssSignedInteger(expression, kCssLiteMaxNthOffset, position, diag) || position <= 0) {
			saturatingIncrement(diag.nthExpressionParseErrors);
			return false;
		}
		out = { 0, position };
		return true;
	}
	if (expression.find('n', nPos + 1) != std::string::npos) {
		saturatingIncrement(diag.nthExpressionParseErrors);
		return false;
	}

	const std::string coefficientText = expression.substr(0, nPos);
	const std::string offsetText = expression.substr(nPos + 1);
	int coefficient = 1;
	if (coefficientText == "-") coefficient = -1;
	else if (coefficientText == "+") coefficient = 1;
	else if (!coefficientText.empty()) {
		if (!parseCssSignedInteger(coefficientText, kCssLiteMaxNthCoefficient, coefficient, diag)) {
			saturatingIncrement(diag.nthExpressionParseErrors);
			return false;
		}
	}
	int offset = 0;
	if (!offsetText.empty() && !parseCssSignedInteger(offsetText, kCssLiteMaxNthOffset, offset, diag)) {
		saturatingIncrement(diag.nthExpressionParseErrors);
		return false;
	}
	if (coefficient == 0 && offset <= 0) {
		saturatingIncrement(diag.nthExpressionParseErrors);
		return false;
	}
	out = { coefficient, offset };
	return true;
}

static bool parseCssSimpleSelectorCore(const std::string& rawSelector,
	CssSimpleSelector& out,
	CssSpecificity& specificity,
	CssDiagnostics& diag)
{
	const std::string selector = toLower(trim(rawSelector));
	out = {};
	specificity = {};
	if (selector.empty()) return false;
	auto isIdentChar = [](char c) {
		return static_cast<unsigned char>(c) < 128 &&
			(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_');
	};
	size_t pos = 0;
	if (selector[pos] != '.' && selector[pos] != '#') {
		const size_t start = pos;
		if (selector[pos] == '*') {
			++pos;
			if (pos < selector.size() && selector[pos] != '.' && selector[pos] != '#') return false;
			out.tagName = "*";
		} else {
		while (pos < selector.size() && selector[pos] != '.' && selector[pos] != '#') {
			if (!isIdentChar(selector[pos])) return false;
			++pos;
		}
		out.tagName = selector.substr(start, pos - start);
		if (out.tagName.empty()) {
			return false;
		} else {
			++specificity.elementCount;
		}
		}
	}
	while (pos < selector.size()) {
		const char prefix = selector[pos];
		if (prefix != '.' && prefix != '#') return false;
		++pos;
		const size_t start = pos;
		while (pos < selector.size() && selector[pos] != '.' && selector[pos] != '#') {
			if (!isIdentChar(selector[pos])) return false;
			++pos;
		}
		const std::string token = selector.substr(start, pos - start);
		if (token.empty()) return false;
		if (prefix == '.') {
			if (out.classNames.size() >= kCssLiteMaxSelectorClasses) {
				saturatingIncrement(diag.selectorDepthClamps);
				return false;
			}
			out.classNames.push_back(token);
			++specificity.classCount;
		} else {
			if (!out.id.empty()) return false;
			out.id = token;
			++specificity.idCount;
		}
	}
	return !out.tagName.empty() || !out.classNames.empty() || !out.id.empty();
}

static bool parseCssSimpleSelectorPart(const std::string& rawPart,
	CssSelectorPart& part,
	CssSpecificity& specificity,
	CssDiagnostics& diag,
	bool allowPseudo = true)
{
	const std::string selector = toLower(trim(rawPart));
	part = {};
	specificity = {};
	if (selector.empty()) return false;
	if (selector.find('\\') != std::string::npos) {
		saturatingIncrement(diag.identifierEscapeRejections);
		return false;
	}

	CssSimpleSelector core;
	CssSpecificity coreSpecificity;
	std::string coreText;
	size_t pos = 0;
	int depth = 0;
	while (pos < selector.size()) {
		if (selector[pos] == '(') {
			if (++depth > static_cast<int>(kCssLiteMaxDelimiterDepth)) {
				saturatingIncrement(diag.unbalancedParenthesisErrors);
				return false;
			}
		}
		else if (selector[pos] == ')') {
			if (depth == 0) {
				saturatingIncrement(diag.unbalancedParenthesisErrors);
				return false;
			}
			--depth;
		}
		if (depth == 0 && selector[pos] == ':') break;
		++pos;
	}
	coreText = selector.substr(0, pos);
	if (!coreText.empty()) {
		if (!parseCssSimpleSelectorCore(coreText, core, coreSpecificity, diag)) return false;
		part.tagName = core.tagName;
		part.classNames = core.classNames;
		part.id = core.id;
		specificity = coreSpecificity;
	}
	if (pos == selector.size()) return !part.tagName.empty() || !part.classNames.empty() || !part.id.empty();
	if (!allowPseudo) return false;
	while (pos < selector.size()) {
		if (selector[pos++] != ':') return false;
		if (pos >= selector.size() || selector[pos] == ':') return false;
		const size_t nameStart = pos;
		while (pos < selector.size() &&
			(std::isalnum(static_cast<unsigned char>(selector[pos])) || selector[pos] == '-')) ++pos;
		const std::string name = selector.substr(nameStart, pos - nameStart);
		if (name.empty() || part.pseudoClasses.size() >= kCssLiteMaxPseudoClassesPerCompound) {
			saturatingIncrement(diag.pseudoClassClamps);
			return false;
		}
		std::string argument;
		bool hasArgument = false;
		if (pos < selector.size() && selector[pos] == '(') {
			hasArgument = true;
			const size_t argumentStart = ++pos;
			int parentheses = 1;
			char argumentQuote = 0;
			while (pos < selector.size() && parentheses > 0) {
				if (argumentQuote != 0) {
					if (selector[pos] == '\\' && pos + 1 < selector.size()) { pos += 2; continue; }
					if (selector[pos] == argumentQuote) argumentQuote = 0;
					++pos;
					continue;
				}
				if (selector[pos] == '\'' || selector[pos] == '"') argumentQuote = selector[pos];
				else if (selector[pos] == '(') {
					if (++parentheses > static_cast<int>(kCssLiteMaxDelimiterDepth)) {
						saturatingIncrement(diag.unbalancedParenthesisErrors);
						return false;
					}
				} else if (selector[pos] == ')') --parentheses;
				if (parentheses > 0) ++pos;
			}
			if (argumentQuote != 0) {
				saturatingIncrement(diag.unterminatedStringErrors);
				return false;
			}
			if (parentheses != 0) {
				saturatingIncrement(diag.unbalancedParenthesisErrors);
				return false;
			}
			argument = selector.substr(argumentStart, pos - argumentStart);
			++pos;
		}
		CssPseudoClassSelector pseudo;
		if (name == "first-child" || name == "last-child" || name == "only-child" ||
			name == "first-of-type" || name == "last-of-type" || name == "only-of-type" ||
			name == "root" || name == "link" || name == "visited" || name == "empty" ||
			name == "checked" || name == "disabled" || name == "enabled" || name == "required" ||
			name == "read-only" || name == "read-write" || name == "focus" ||
			name == "focus-visible") {
			if (hasArgument) return false;
			if (name == "first-child") pseudo.type = CssPseudoClass::FirstChild;
			else if (name == "last-child") pseudo.type = CssPseudoClass::LastChild;
			else if (name == "only-child") pseudo.type = CssPseudoClass::OnlyChild;
			else if (name == "first-of-type") pseudo.type = CssPseudoClass::FirstOfType;
			else if (name == "last-of-type") pseudo.type = CssPseudoClass::LastOfType;
			else if (name == "only-of-type") pseudo.type = CssPseudoClass::OnlyOfType;
			else if (name == "root") pseudo.type = CssPseudoClass::Root;
			else if (name == "link") pseudo.type = CssPseudoClass::Link;
			else if (name == "visited") pseudo.type = CssPseudoClass::Visited;
			else if (name == "empty") {
				pseudo.type = CssPseudoClass::Empty;
				saturatingIncrement(diag.emptyPseudoParsed);
			} else if (name == "checked") {
				pseudo.type = CssPseudoClass::Checked;
				saturatingIncrement(diag.checkedPseudoParsed);
			} else if (name == "disabled") {
				pseudo.type = CssPseudoClass::Disabled;
				saturatingIncrement(diag.disabledPseudoParsed);
			} else if (name == "enabled") {
				pseudo.type = CssPseudoClass::Enabled;
				saturatingIncrement(diag.enabledPseudoParsed);
			} else if (name == "required") {
				pseudo.type = CssPseudoClass::Required;
				saturatingIncrement(diag.requiredPseudoParsed);
			} else if (name == "read-only") {
				pseudo.type = CssPseudoClass::ReadOnly;
				saturatingIncrement(diag.readonlyPseudoParsed);
			} else if (name == "read-write") {
				pseudo.type = CssPseudoClass::ReadWrite;
				saturatingIncrement(diag.readwritePseudoParsed);
			} else if (name == "focus") {
				pseudo.type = CssPseudoClass::Focus;
				saturatingIncrement(diag.focusPseudoParsed);
			} else {
				pseudo.type = CssPseudoClass::FocusVisible;
				saturatingIncrement(diag.focusVisiblePseudoParsed);
			}
			++specificity.classCount;
		} else if (name == "nth-child" || name == "nth-of-type") {
			if (!hasArgument || !parseCssNthExpression(argument, pseudo.nth, diag)) return false;
			pseudo.type = name == "nth-child" ? CssPseudoClass::NthChild : CssPseudoClass::NthOfType;
			++specificity.classCount;
		} else if (name == "not") {
			if (!hasArgument || argument.find_first_of(" :+~[]") != std::string::npos) return false;
			CssSpecificity notSpecificity;
			if (!parseCssSimpleSelectorCore(argument, pseudo.notSelector, notSpecificity, diag)) return false;
			const size_t componentCount = pseudo.notSelector.classNames.size() +
				(pseudo.notSelector.id.empty() ? 0u : 1u) +
				(pseudo.notSelector.tagName.empty() || pseudo.notSelector.tagName == "*" ? 0u : 1u);
			if (componentCount > kCssLiteMaxNotComponents) {
				saturatingIncrement(diag.pseudoClassClamps);
				return false;
			}
			pseudo.type = CssPseudoClass::Not;
			specificity.idCount = static_cast<uint16_t>(std::min<uint32_t>(
				std::numeric_limits<uint16_t>::max(), specificity.idCount + notSpecificity.idCount));
			specificity.classCount = static_cast<uint16_t>(std::min<uint32_t>(
				std::numeric_limits<uint16_t>::max(), specificity.classCount + notSpecificity.classCount));
			specificity.elementCount = static_cast<uint16_t>(std::min<uint32_t>(
				std::numeric_limits<uint16_t>::max(), specificity.elementCount + notSpecificity.elementCount));
		} else {
			return false;
		}
		saturatingIncrement(diag.pseudoClassesParsed);
		part.pseudoClasses.push_back(std::move(pseudo));
	}
	return !part.tagName.empty() || !part.classNames.empty() || !part.id.empty() || !part.pseudoClasses.empty();
}

static bool parseCssSelector(const std::string& rawSelector, WebStyleRule& outRule, CssDiagnostics& diag)
{
	std::string selectorText;
	if (!normalizeCssComments(rawSelector, selectorText, diag)) return false;
	selectorText = trim(selectorText);
	if (selectorText.empty() || selectorText.size() > kCssLiteMaxSelectorLength) return false;
	outRule.selectorParts.clear();
	outRule.combinators.clear();
	outRule.hasVisitedPseudo = false;
	outRule.specificity = 0;
	outRule.specificityTuple = {};
	size_t cursor = 0;
	while (true) {
		while (cursor < selectorText.size() && std::isspace(static_cast<unsigned char>(selectorText[cursor]))) ++cursor;
		if (cursor >= selectorText.size()) break;
		if (selectorText[cursor] == '>' || selectorText[cursor] == '+' || selectorText[cursor] == '~') {
			saturatingIncrement(diag.invalidCombinatorSequences);
			return false;
		}
		if (selectorText[cursor] == '[') {
			// Consume the unsupported attribute fragment as part of this
			// member, then reject the member without leaking its contents.
			saturatingIncrement(diag.unsupportedSelectorCount);
			if (selectorText.find(']', cursor) == std::string::npos)
				saturatingIncrement(diag.unbalancedBracketErrors);
			return false;
		}
		size_t start = cursor;
		int parentheses = 0;
		int brackets = 0;
		char quote = 0;
		while (cursor < selectorText.size()) {
			const char c = selectorText[cursor];
			if (quote != 0) {
				if (c == '\\' && cursor + 1 < selectorText.size()) { cursor += 2; continue; }
				if (c == quote) quote = 0;
				++cursor;
				continue;
			}
			if (c == '\'' || c == '"') {
				quote = c;
				++cursor;
				continue;
			}
			if (c == '\\') {
				// Keep escaped punctuation inside this member.  The bounded
				// identifier policy rejects the member later, but skipping the
				// escaped byte here prevents accidental combinator/group recovery.
				if (cursor + 1 < selectorText.size()) cursor += 2;
				else ++cursor;
				continue;
			}
			if (c == '(') {
				if (++parentheses > static_cast<int>(kCssLiteMaxDelimiterDepth)) {
					saturatingIncrement(diag.unbalancedParenthesisErrors);
					return false;
				}
			} else if (c == '[') {
				if (++brackets > static_cast<int>(kCssLiteMaxDelimiterDepth)) {
					saturatingIncrement(diag.unbalancedBracketErrors);
					return false;
				}
			} else if (c == ']') {
				if (brackets == 0) {
					saturatingIncrement(diag.unbalancedBracketErrors);
					return false;
				}
				--brackets;
			} else if (c == ')') {
				if (parentheses == 0) {
					saturatingIncrement(diag.unbalancedParenthesisErrors);
					return false;
				}
				--parentheses;
			}
			if (parentheses == 0 && brackets == 0 &&
				(std::isspace(static_cast<unsigned char>(c)) || c == '>' || c == '+' || c == '~')) break;
			++cursor;
		}
		if (parentheses != 0) {
			saturatingIncrement(diag.unbalancedParenthesisErrors);
			return false;
		}
		if (brackets != 0) {
			saturatingIncrement(diag.unbalancedBracketErrors);
			return false;
		}
		if (quote != 0) {
			saturatingIncrement(diag.unterminatedStringErrors);
			return false;
		}
		if (start == cursor) return false;
		if (outRule.selectorParts.size() >= kCssLiteMaxSelectorComponents) {
			++diag.selectorDepthClamps;
			return false;
		}
		CssSelectorPart part;
		CssSpecificity partSpecificity;
		if (!parseCssSimpleSelectorPart(selectorText.substr(start, cursor - start), part, partSpecificity, diag)) return false;
		for (const CssPseudoClassSelector& pseudo : part.pseudoClasses) {
			if (pseudo.type == CssPseudoClass::Visited) outRule.hasVisitedPseudo = true;
		}
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
		bool hadWhitespace = false;
		while (cursor < selectorText.size() && std::isspace(static_cast<unsigned char>(selectorText[cursor]))) {
			hadWhitespace = true;
			++cursor;
		}
		if (cursor >= selectorText.size()) break;
		CssCombinator combinator = CssCombinator::Descendant;
		const char next = selectorText[cursor];
		if (next == '>' || next == '+' || next == '~') {
			if (next == '>') combinator = CssCombinator::Child;
			else if (next == '+') combinator = CssCombinator::AdjacentSibling;
			else combinator = CssCombinator::GeneralSibling;
			++cursor;
			while (cursor < selectorText.size() && std::isspace(static_cast<unsigned char>(selectorText[cursor]))) ++cursor;
			if (cursor >= selectorText.size() || selectorText[cursor] == '>' ||
				selectorText[cursor] == '+' || selectorText[cursor] == '~') {
				saturatingIncrement(diag.invalidCombinatorSequences);
				return false;
			}
		} else if (!hadWhitespace) {
			return false;
		} else if (outRule.selectorParts.size() == 1 &&
			outRule.selectorParts.front().tagName == "*" &&
			outRule.selectorParts.front().classNames.empty() &&
			outRule.selectorParts.front().id.empty() &&
			outRule.selectorParts.front().pseudoClasses.empty()) {
			// Keep the bounded grammar's explicit rejection of the ambiguous
			// leading-universal descendant form while retaining standalone * and
			// explicit child/sibling universal compounds.
			return false;
		}
		outRule.combinators.push_back(combinator);
		switch (combinator) {
		case CssCombinator::Child: saturatingIncrement(diag.childCombinatorCount); break;
		case CssCombinator::Descendant: saturatingIncrement(diag.descendantCombinatorCount); break;
		case CssCombinator::AdjacentSibling: saturatingIncrement(diag.adjacentSiblingCombinatorCount); break;
		case CssCombinator::GeneralSibling: saturatingIncrement(diag.generalSiblingCombinatorCount); break;
		}
		if (outRule.combinators.size() > kCssLiteMaxCombinatorDepth) {
			saturatingIncrement(diag.selectorDepthClamps);
			return false;
		}
	}
	if (outRule.selectorParts.empty() ||
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

static const HtmlElementContentMetadata* findContentMetadata(const WebDocument& doc,
	uint64_t serial);

static bool contentMetadataProvesEmpty(const HtmlElementContentMetadata& metadata)
{
	return metadata.contentMetadataComplete &&
		!metadata.hasElementChild &&
		!metadata.hasNonWhitespaceText &&
		!metadata.hasImageOrMediaChild &&
		!metadata.hasVisibleBreak &&
		!metadata.hasVisibleReplacedContent &&
		!metadata.hasRenderableContent;
}

static std::string stripCssAtRulesBounded(const std::string& input, CssDiagnostics& diag)
{
	std::string output;
	output.reserve(input.size());
	size_t i = 0;
	int outerBraces = 0;
	char outerQuote = 0;
	while (i < input.size()) {
		const char current = input[i];
		if (outerQuote != 0) {
			output += current;
			if (current == '\\' && i + 1 < input.size()) output += input[++i];
			else if (current == outerQuote) outerQuote = 0;
			++i;
			continue;
		}
		if (current == '\'' || current == '"') {
			outerQuote = current;
			output += current;
			++i;
			continue;
		}
		if (current == '{') {
			if (++outerBraces > static_cast<int>(kCssLiteMaxDelimiterDepth)) {
				saturatingIncrement(diag.parseErrorCount);
				return output;
			}
			output += current;
			++i;
			continue;
		}
		if (current == '}') {
			if (outerBraces > 0) --outerBraces;
			output += current;
			++i;
			continue;
		}
		if (current != '@' || outerBraces != 0) {
			output += input[i++];
			continue;
		}
		size_t j = i + 1;
		int parentheses = 0;
		int brackets = 0;
		int braces = 0;
		char quote = 0;
		bool skipped = false;
		for (; j < input.size(); ++j) {
			const char c = input[j];
			if (quote != 0) {
				if (c == '\\' && j + 1 < input.size()) { ++j; continue; }
				if (c == quote) quote = 0;
				continue;
			}
			if (c == '\'' || c == '"') { quote = c; continue; }
			if (c == '(') ++parentheses;
			else if (c == ')' && parentheses > 0) --parentheses;
			else if (c == '[') ++brackets;
			else if (c == ']' && brackets > 0) --brackets;
			else if (parentheses == 0 && brackets == 0 && c == ';') {
				i = j + 1;
				skipped = true;
				break;
			} else if (parentheses == 0 && brackets == 0 && c == '{') {
				braces = 1;
				++j;
				for (; j < input.size() && braces > 0; ++j) {
					const char nested = input[j];
					if (nested == '\'' || nested == '"') {
						const char nestedQuote = nested;
						++j;
						while (j < input.size()) {
							if (input[j] == '\\' && j + 1 < input.size()) { ++j; continue; }
							if (input[j] == nestedQuote) break;
							++j;
						}
					} else if (nested == '{') {
						if (++braces > static_cast<int>(kCssLiteMaxDelimiterDepth)) {
							saturatingIncrement(diag.selectorDepthClamps);
							return output;
						}
					}
					else if (nested == '}') --braces;
				}
				if (braces != 0) {
					saturatingIncrement(diag.parseErrorCount);
					return output;
				}
				i = j;
				skipped = true;
				break;
			}
		}
		if (quote != 0) saturatingIncrement(diag.unterminatedStringErrors);
		if (!skipped) {
			saturatingIncrement(diag.parseErrorCount);
			break;
		}
	}
	return output;
}

static bool findCssRuleOpenBrace(const std::string& css,
	size_t start,
	size_t& outBrace,
	CssDiagnostics& diag)
{
	int parentheses = 0;
	int brackets = 0;
	char quote = 0;
	for (size_t i = start; i < css.size(); ++i) {
		const char c = css[i];
		if (quote != 0) {
			if (c == '\\' && i + 1 < css.size()) { ++i; continue; }
			if (c == quote) quote = 0;
			continue;
		}
		if (c == '\'' || c == '"') { quote = c; continue; }
		if (c == '\\') {
			if (i + 1 < css.size()) ++i;
			continue;
		}
		if (c == '{') {
			// A brace is the bounded rule boundary even when an unsupported
			// selector fragment left a parenthesis/bracket open.  The affected
			// member will fail in parseCssSelector, while later comma members
			// and following rules remain recoverable.
			outBrace = i;
			return true;
		}
		if (c == '(') {
			if (++parentheses > static_cast<int>(kCssLiteMaxDelimiterDepth)) {
				saturatingIncrement(diag.unbalancedParenthesisErrors);
				return false;
			}
		} else if (c == ')') {
			if (parentheses == 0) {
				saturatingIncrement(diag.unbalancedParenthesisErrors);
				return false;
			}
			--parentheses;
		} else if (c == '[') {
			if (++brackets > static_cast<int>(kCssLiteMaxDelimiterDepth)) {
				saturatingIncrement(diag.unbalancedBracketErrors);
				return false;
			}
		} else if (c == ']') {
			if (brackets == 0) {
				saturatingIncrement(diag.unbalancedBracketErrors);
				return false;
			}
			--brackets;
		} else if (c == '}' && parentheses == 0 && brackets == 0) {
			saturatingIncrement(diag.parseErrorCount);
			return false;
		}
	}
	if (quote != 0) saturatingIncrement(diag.unterminatedStringErrors);
	if (parentheses != 0) saturatingIncrement(diag.unbalancedParenthesisErrors);
	if (brackets != 0) saturatingIncrement(diag.unbalancedBracketErrors);
	return false;
}

static bool findCssRuleCloseBrace(const std::string& css,
	size_t start,
	size_t& outBrace,
	CssDiagnostics& diag)
{
	int parentheses = 0;
	int brackets = 0;
	int braces = 1;
	char quote = 0;
	for (size_t i = start; i < css.size(); ++i) {
		const char c = css[i];
		if (quote != 0) {
			if (c == '\\' && i + 1 < css.size()) { ++i; continue; }
			if (c == quote) quote = 0;
			continue;
		}
		if (c == '\'' || c == '"') { quote = c; continue; }
		if (c == '\\') {
			if (i + 1 < css.size()) ++i;
			continue;
		}
		if (c == '(') {
			if (++parentheses > static_cast<int>(kCssLiteMaxDelimiterDepth)) {
				saturatingIncrement(diag.unbalancedParenthesisErrors);
				return false;
			}
		} else if (c == ')') {
			if (parentheses == 0) {
				saturatingIncrement(diag.unbalancedParenthesisErrors);
				return false;
			}
			--parentheses;
		} else if (c == '[') {
			if (++brackets > static_cast<int>(kCssLiteMaxDelimiterDepth)) {
				saturatingIncrement(diag.unbalancedBracketErrors);
				return false;
			}
		} else if (c == ']') {
			if (brackets == 0) {
				saturatingIncrement(diag.unbalancedBracketErrors);
				return false;
			}
			--brackets;
		} else if (c == '{') {
			if (++braces > static_cast<int>(kCssLiteMaxCombinatorDepth)) {
				saturatingIncrement(diag.selectorDepthClamps);
				return false;
			}
		} else if (c == '}' && parentheses == 0 && brackets == 0) {
			if (--braces == 0) {
				outBrace = i;
				return true;
			}
		}
	}
	if (quote != 0) saturatingIncrement(diag.unterminatedStringErrors);
	if (parentheses != 0) saturatingIncrement(diag.unbalancedParenthesisErrors);
	if (brackets != 0) saturatingIncrement(diag.unbalancedBracketErrors);
	saturatingIncrement(diag.parseErrorCount);
	return false;
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
	std::string normalizedCss;
	const bool commentsComplete = normalizeCssComments(css, normalizedCss, doc.cssDiagnostics);
	css = stripCssAtRulesBounded(normalizedCss, doc.cssDiagnostics);
	if (!commentsComplete && css.empty()) return;

	size_t cursor = 0;
	while (cursor < css.size()) {
		size_t brace = 0;
		if (!findCssRuleOpenBrace(css, cursor, brace, doc.cssDiagnostics)) {
			if (!trim(css.substr(cursor)).empty()) saturatingIncrement(doc.cssDiagnostics.parseErrorCount);
			break;
		}
		size_t endBrace = 0;
		if (!findCssRuleCloseBrace(css, brace + 1, endBrace, doc.cssDiagnostics)) break;
		std::string selectorText = trim(css.substr(cursor, brace - cursor));
		std::string bodyText = css.substr(brace + 1, endBrace - brace - 1);
		cursor = endBrace + 1;
		if (selectorText.empty()) {
			saturatingIncrement(doc.cssDiagnostics.selectorMemberParseFailures);
			continue;
		}

		const std::vector<std::string> selectors = splitCssSelectorGroups(selectorText, doc.cssDiagnostics);
		if (selectors.empty()) {
			saturatingIncrement(doc.cssDiagnostics.selectorMemberParseFailures);
			continue;
		}
		size_t groupCount = 0;
		size_t invalidMembers = 0;
		size_t validMembers = 0;
		size_t recoveryAttempts = 0;
		for (const std::string& rawMember : selectors) {
			if (groupCount >= kCssLiteMaxSelectorGroups) {
				saturatingIncrement(doc.cssDiagnostics.selectorGroupClamps);
				break;
			}
			if (recoveryAttempts++ >= kCssLiteMaxRecoveryAttemptsPerGroup) {
				saturatingIncrement(doc.cssDiagnostics.selectorGroupClamps);
				break;
			}
			++groupCount;
			saturatingIncrement(doc.cssDiagnostics.selectorGroupsParsed);
			const std::string selector = trim(rawMember);
			if (selector.empty()) {
				++invalidMembers;
				saturatingIncrement(doc.cssDiagnostics.selectorMemberParseFailures);
				continue;
			}
			if (doc.styleRules.size() >= kCssLiteMaxRules) {
				saturatingIncrement(doc.cssDiagnostics.ruleCapCount);
				continue;
			}
			WebStyleRule rule;
			if (!parseCssSelector(selector, rule, doc.cssDiagnostics)) {
				++invalidMembers;
				saturatingIncrement(doc.cssDiagnostics.unsupportedRuleCount);
				saturatingIncrement(doc.cssDiagnostics.unsupportedSelectorCount);
				saturatingIncrement(doc.cssDiagnostics.selectorMemberParseFailures);
				continue;
			}
			parseCssDeclarations(bodyText, rule.style, doc.cssDiagnostics);
			rule.sourceOrder = allocateCssSourceOrder(doc.cssDiagnostics);
			rule.evidenceRuleIndex = static_cast<uint16_t>(std::min<size_t>(
			std::numeric_limits<uint16_t>::max(), doc.styleRules.size() + 1));
			rule.evidenceGroupIndex = static_cast<uint8_t>(std::min<size_t>(255, groupCount));
		uint32_t hash = 2166136261u;
		for (unsigned char c : rule.selector) hash = (hash ^ c) * 16777619u;
		rule.evidenceSelectorHash = hash;
		doc.styleRules.push_back(rule);
		saturatingIncrement(doc.cssDiagnostics.styleRuleCount);
			saturatingIncrement(doc.cssDiagnostics.compoundSelectorsParsed, static_cast<int>(rule.selectorParts.size()));
			++validMembers;
		}
		if (invalidMembers > 0 && validMembers > 0) {
		saturatingIncrement(doc.cssDiagnostics.selectorGroupMemberRecoveries,
			static_cast<int>(std::min<size_t>(invalidMembers, std::numeric_limits<int>::max())));
		saturatingIncrement(doc.cssDiagnostics.selectorRecoverySuccesses);
		}
	}
}

static bool simpleSelectorMatchesElement(const HtmlElementRef& element, const CssSimpleSelector& selector)
{
	const std::string tag = toLower(element.tagName);
	const std::string id = toLower(element.id);
	if (!selector.tagName.empty() && selector.tagName != "*" && selector.tagName != tag) return false;
	if (!selector.id.empty() && selector.id != id) return false;
	if (!selector.classNames.empty()) {
		std::stringstream classes(toLower(element.className));
		std::string className;
		std::vector<std::string> classList;
		while (classes >> className) classList.push_back(className);
		for (const std::string& required : selector.classNames) {
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

static bool nthExpressionMatches(const CssNthExpression& expression, int index)
{
	if (index <= 0) return false;
	if (expression.a == 0) return index == expression.b;
	if (expression.a > 0) {
		if (index < expression.b) return false;
		return (index - expression.b) % expression.a == 0;
	}
	if (index > expression.b) return false;
	return (expression.b - index) % (-expression.a) == 0;
}

static void recordPseudoMatch(CssDiagnostics& diag, CssPseudoClass type)
{
	saturatingIncrement(diag.structuralPseudoMatches);
	switch (type) {
	case CssPseudoClass::FirstChild: saturatingIncrement(diag.firstChildMatches); break;
	case CssPseudoClass::LastChild: saturatingIncrement(diag.lastChildMatches); break;
	case CssPseudoClass::NthChild: saturatingIncrement(diag.nthChildMatches); break;
	case CssPseudoClass::FirstOfType:
	case CssPseudoClass::LastOfType:
	case CssPseudoClass::OnlyOfType:
	case CssPseudoClass::NthOfType: saturatingIncrement(diag.ofTypeMatches); break;
	case CssPseudoClass::Not: saturatingIncrement(diag.notMatches); break;
	case CssPseudoClass::Link: saturatingIncrement(diag.linkPseudoMatches); break;
	case CssPseudoClass::Visited: saturatingIncrement(diag.visitedPseudoMatches); break;
	case CssPseudoClass::Empty: saturatingIncrement(diag.emptyPseudoMatches); break;
	case CssPseudoClass::Checked: saturatingIncrement(diag.checkedPseudoMatches); break;
	case CssPseudoClass::Disabled: saturatingIncrement(diag.disabledPseudoMatches); break;
	case CssPseudoClass::Enabled: saturatingIncrement(diag.enabledPseudoMatches); break;
	case CssPseudoClass::Required: saturatingIncrement(diag.requiredPseudoMatches); break;
	case CssPseudoClass::ReadOnly: saturatingIncrement(diag.readonlyPseudoMatches); break;
	case CssPseudoClass::ReadWrite: saturatingIncrement(diag.readwritePseudoMatches); break;
	case CssPseudoClass::Focus: saturatingIncrement(diag.focusPseudoMatches); break;
	case CssPseudoClass::FocusVisible: saturatingIncrement(diag.focusVisiblePseudoMatches); break;
	default: break;
	}
}

static const FormRuntimeControlState* runtimeControlState(const WebDocument& doc,
	uint64_t logicalSerial)
{
	if (!doc.formRuntimeState.initialized || logicalSerial == 0) return nullptr;
	const size_t count = std::min(doc.formRuntimeState.count, kFormRuntimeControlCap);
	for (size_t i = 0; i < count; ++i) {
		const FormRuntimeControlState& state = doc.formRuntimeState.controls[i];
		if (state.logicalSerial == logicalSerial && state.metadataValid) return &state;
	}
	return nullptr;
}

static bool effectiveChecked(const WebDocument& doc, const HtmlElementRef& element)
{
	if (const FormRuntimeControlState* state = runtimeControlState(doc, element.serial))
		return state->checked;
	return element.formControl.type == FormControlType::Option
		? element.formControl.selected : element.formControl.checked;
}

static bool effectiveDisabled(const WebDocument& doc, const HtmlElementRef& element)
{
	if (const FormRuntimeControlState* state = runtimeControlState(doc, element.serial))
		return state->disabled;
	return element.formControl.disabled;
}

static bool effectiveFocused(const WebDocument& doc, const HtmlElementRef& element)
{
	if (!doc.formRuntimeState.initialized || !doc.formRuntimeState.focusValid ||
		element.serial == 0 || !element.formControl.metadataComplete ||
		!element.formControl.supported || element.formControl.hidden ||
		effectiveDisabled(doc, element)) return false;
	return doc.formRuntimeState.documentGeneration != 0 &&
		doc.formRuntimeState.focusedDocumentGeneration == doc.formRuntimeState.documentGeneration &&
		doc.formRuntimeState.focusedLogicalSerial == element.serial;
}

static bool effectiveFocusVisible(const WebDocument& doc, const HtmlElementRef& element)
{
	return effectiveFocused(doc, element) &&
		doc.formRuntimeState.focusOrigin == FormFocusOrigin::Keyboard;
}

static bool selectorPartMatchesElement(const HtmlElementRef& element,
	const CssSelectorPart& part,
	const std::vector<HtmlElementRef>& path,
	size_t pathIndex,
	const WebDocument& doc,
	CssDiagnostics& diag)
{
	CssSimpleSelector core;
	core.tagName = part.tagName;
	core.classNames = part.classNames;
	core.id = part.id;
	if (!simpleSelectorMatchesElement(element, core)) return false;
	for (const CssPseudoClassSelector& pseudo : part.pseudoClasses) {
		bool matched = false;
		switch (pseudo.type) {
		case CssPseudoClass::FirstChild:
			matched = element.childIndex == 1 && element.siblingCount > 0;
			break;
		case CssPseudoClass::LastChild:
			matched = element.childIndex > 0 && element.childIndex == element.siblingCount;
			break;
		case CssPseudoClass::OnlyChild:
			matched = element.childIndex == 1 && element.siblingCount == 1;
			break;
		case CssPseudoClass::NthChild:
			matched = element.siblingCount > 0 && nthExpressionMatches(pseudo.nth, element.childIndex);
			break;
		case CssPseudoClass::FirstOfType:
			matched = element.typeIndex == 1 && element.typeCount > 0;
			break;
		case CssPseudoClass::LastOfType:
			matched = element.typeIndex > 0 && element.typeIndex == element.typeCount;
			break;
		case CssPseudoClass::OnlyOfType:
			matched = element.typeIndex == 1 && element.typeCount == 1;
			break;
		case CssPseudoClass::NthOfType:
			matched = element.typeCount > 0 && nthExpressionMatches(pseudo.nth, element.typeIndex);
			break;
		case CssPseudoClass::Not:
			matched = !simpleSelectorMatchesElement(element, pseudo.notSelector);
			break;
		case CssPseudoClass::Root:
			matched = pathIndex == 0 && (!doc.hasDocumentElement ||
				doc.documentElement.serial == 0 || element.serial == doc.documentElement.serial);
			break;
		case CssPseudoClass::Link:
			matched = element.tagName == "a" && element.hasLinkTarget && !element.visited;
			break;
		case CssPseudoClass::Visited:
			matched = element.tagName == "a" && element.hasLinkTarget && element.visited;
			break;
		case CssPseudoClass::Empty: {
			const HtmlElementContentMetadata* metadata = findContentMetadata(doc, element.serial);
			if (!metadata || !metadata->contentMetadataComplete) {
				saturatingIncrement(diag.emptyMetadataIncomplete);
				matched = false;
				break;
			}
			matched = contentMetadataProvesEmpty(*metadata);
			break;
		}
		case CssPseudoClass::Checked:
			matched = element.formControl.metadataComplete && element.formControl.supported &&
				(element.formControl.type == FormControlType::Checkbox ||
				 element.formControl.type == FormControlType::Radio ||
				 element.formControl.type == FormControlType::Option) &&
				effectiveChecked(doc, element);
			break;
		case CssPseudoClass::Disabled:
			matched = element.formControl.metadataComplete && effectiveDisabled(doc, element);
			break;
		case CssPseudoClass::Enabled:
			matched = element.formControl.metadataComplete && element.formControl.supported &&
				!effectiveDisabled(doc, element);
			break;
		case CssPseudoClass::Required:
			matched = element.formControl.metadataComplete && element.formControl.supported &&
				element.formControl.required;
			break;
		case CssPseudoClass::ReadOnly:
			matched = element.formControl.metadataComplete &&
				(element.formControl.readOnly || !element.formControl.supported ||
				 element.formControl.type == FormControlType::Checkbox ||
				 element.formControl.type == FormControlType::Radio ||
				 element.formControl.type == FormControlType::Button ||
				 element.formControl.type == FormControlType::Submit ||
				 element.formControl.type == FormControlType::Reset ||
				 element.formControl.type == FormControlType::Select ||
				 element.formControl.type == FormControlType::Option);
			break;
		case CssPseudoClass::ReadWrite:
			matched = element.formControl.metadataComplete && element.formControl.supported &&
				!element.formControl.readOnly &&
				(element.formControl.type == FormControlType::Text ||
				 element.formControl.type == FormControlType::Password ||
				 element.formControl.type == FormControlType::Search ||
				 element.formControl.type == FormControlType::Email ||
				 element.formControl.type == FormControlType::Url ||
				 element.formControl.type == FormControlType::Number ||
				 element.formControl.type == FormControlType::Textarea);
			break;
		case CssPseudoClass::Focus:
			matched = effectiveFocused(doc, element);
			break;
		case CssPseudoClass::FocusVisible:
			matched = effectiveFocusVisible(doc, element);
			break;
		}
		if (!matched) return false;
		recordPseudoMatch(diag, pseudo.type);
	}
	return true;
}

struct CssSelectorMatchTrace {
	size_t siblingScanSteps = 0;
};

static bool selectorMatchesPathAt(const std::vector<HtmlElementRef>& path,
	const WebDocument& doc,
	CssDiagnostics& diag,
	const WebStyleRule& rule,
	int partIndex,
	int pathIndex,
	size_t& evaluationSteps,
	CssSelectorMatchTrace& trace);

static const HtmlElementRef* structuralElementBySerial(const WebDocument& doc,
	CssDiagnostics& diag,
	uint64_t serial)
{
	if (serial == 0) {
		saturatingIncrement(diag.siblingMetadataErrors);
		return nullptr;
	}
	for (const HtmlElementRef& element : doc.structuralElements) {
		if (element.serial == serial) return &element;
	}
	saturatingIncrement(diag.siblingMetadataErrors);
	return nullptr;
}

static bool selectorMatchesSiblingRelation(const std::vector<HtmlElementRef>& path,
	const WebDocument& doc,
	CssDiagnostics& diag,
	const WebStyleRule& rule,
	int partIndex,
	int pathIndex,
	CssCombinator combinator,
	size_t& evaluationSteps,
	CssSelectorMatchTrace& trace)
{
	if (pathIndex <= 0 || pathIndex >= static_cast<int>(path.size())) return false;
	const HtmlElementRef& candidate = path[static_cast<size_t>(pathIndex)];
	const HtmlElementRef& parent = path[static_cast<size_t>(pathIndex - 1)];
	// A flattened text artifact has no structural serial. It is readable, but
	// is intentionally excluded from element-sibling semantics.
	if (candidate.serial == 0) return false;
	if (candidate.parentSerial == 0 || parent.serial == 0 ||
		candidate.parentSerial != parent.serial) {
		saturatingIncrement(diag.siblingMetadataErrors);
		return false;
	}

	uint64_t previousSerial = candidate.previousSiblingSerial;
	if (previousSerial == 0) {
		if (candidate.childIndex > 1 || candidate.childIndex == 0)
			saturatingIncrement(diag.siblingMetadataErrors);
		return false;
	}

	const bool adjacent = combinator == CssCombinator::AdjacentSibling;
	size_t scanSteps = 0;
	while (previousSerial != 0) {
		if (++scanSteps > kCssLiteMaxSiblingScanSteps) {
			saturatingIncrement(diag.siblingScanClamps);
			return false;
		}
		saturatingIncrement(diag.siblingScanSteps);
		++trace.siblingScanSteps;
		const HtmlElementRef* previous = structuralElementBySerial(doc, diag, previousSerial);
		if (!previous || previous->parentSerial != candidate.parentSerial ||
			previous->serial >= candidate.serial) {
			saturatingIncrement(diag.siblingMetadataErrors);
			return false;
		}

		std::vector<HtmlElementRef> siblingPath(path.begin(), path.begin() + pathIndex);
		siblingPath.push_back(*previous);
		if (selectorMatchesPathAt(siblingPath, doc, diag, rule, partIndex - 1,
			pathIndex, evaluationSteps, trace)) {
			if (adjacent) saturatingIncrement(diag.adjacentSiblingMatches);
			else saturatingIncrement(diag.generalSiblingMatches);
			return true;
		}
		if (adjacent) return false;
		previousSerial = previous->previousSiblingSerial;
	}
	return false;
}

static bool selectorMatchesPathAt(const std::vector<HtmlElementRef>& path,
	const WebDocument& doc,
	CssDiagnostics& diag,
	const WebStyleRule& rule,
	int partIndex,
	int pathIndex,
	size_t& evaluationSteps,
	CssSelectorMatchTrace& trace)
{
	if (++evaluationSteps > kCssLiteMaxSelectorEvaluationSteps) {
		saturatingIncrement(diag.selectorEvaluationStepClamps);
		return false;
	}
	if (partIndex < 0 || pathIndex < 0 ||
		partIndex >= static_cast<int>(rule.selectorParts.size()) ||
		pathIndex >= static_cast<int>(path.size())) return false;
	if (!selectorPartMatchesElement(path[static_cast<size_t>(pathIndex)],
		rule.selectorParts[static_cast<size_t>(partIndex)], path,
		static_cast<size_t>(pathIndex), doc, diag)) return false;
	if (partIndex == 0) return true;
	const CssCombinator combinator = rule.combinators[static_cast<size_t>(partIndex - 1)];
	if (combinator == CssCombinator::Child) {
		return selectorMatchesPathAt(path, doc, diag, rule, partIndex - 1, pathIndex - 1, evaluationSteps, trace);
	}
	if (combinator == CssCombinator::AdjacentSibling || combinator == CssCombinator::GeneralSibling) {
		return selectorMatchesSiblingRelation(path, doc, diag, rule, partIndex, pathIndex,
			combinator, evaluationSteps, trace);
	}
	for (int ancestorIndex = pathIndex - 1; ancestorIndex >= 0; --ancestorIndex) {
		if (selectorMatchesPathAt(path, doc, diag, rule, partIndex - 1, ancestorIndex, evaluationSteps, trace)) return true;
	}
	return false;
}

static bool selectorMatchesPath(const std::vector<HtmlElementRef>& path,
	const WebDocument& doc,
	CssDiagnostics& diag,
	const WebStyleRule& rule,
	CssSelectorMatchTrace* traceOut = nullptr)
{
	if (rule.selectorParts.empty() || path.empty() ||
		rule.selectorParts.size() != rule.combinators.size() + 1 ||
		rule.combinators.size() > kCssLiteMaxCombinatorDepth) return false;
	size_t evaluationSteps = 0;
	CssSelectorMatchTrace trace;
	const bool matched = selectorMatchesPathAt(path, doc, diag, rule,
		static_cast<int>(rule.selectorParts.size()) - 1,
		static_cast<int>(path.size()) - 1, evaluationSteps, trace);
	if (traceOut) *traceOut = trace;
	return matched;
}

static bool parseInlineStyleDeclaration(WebStyle& style,
	const std::string& property,
	const std::string& value,
	CssDiagnostics& diag)
{
	std::string prop = toLower(trim(property));
	std::string normalizedValue;
	if (!normalizeCssComments(value, normalizedValue, diag)) return false;
	std::string val = trim(normalizedValue);
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
	if (prop == "box-sizing") {
		const std::string lower = toLower(val);
		if (lower == "content-box") {
			style.boxSizing = BoxSizingMode::ContentBox;
			style.boxSizingSpecified = true;
			return accept(CssProperty::BoxSizing);
		}
		if (lower == "border-box") {
			style.boxSizing = BoxSizingMode::BorderBox;
			style.boxSizingSpecified = true;
			return accept(CssProperty::BoxSizing);
		}
		++diag.unsupportedDeclarationCount;
		return false;
	}
	if (prop == "overflow" || prop == "overflow-x" || prop == "overflow-y") {
		const std::vector<std::string> tokens = splitCssTokens(toLower(val));
		if (tokens.empty() || tokens.size() > (prop == "overflow" ? 2u : 1u)) {
			++diag.unsupportedDeclarationCount;
			return false;
		}
		auto parseOverflow = [](const std::string& token, OverflowMode& mode) {
			if (token == "visible") mode = OverflowMode::Visible;
			else if (token == "hidden") mode = OverflowMode::Hidden;
			else if (token == "auto") mode = OverflowMode::Auto;
			else if (token == "scroll") mode = OverflowMode::Scroll;
			else return false;
			return true;
		};
		OverflowMode first = OverflowMode::Visible;
		OverflowMode second = OverflowMode::Visible;
		if (!parseOverflow(tokens[0], first) || (tokens.size() == 2 && !parseOverflow(tokens[1], second))) {
			++diag.unsupportedDeclarationCount;
			return false;
		}
		if (prop == "overflow-x") {
			style.overflowX = first;
			return accept(CssProperty::OverflowX);
		}
		if (prop == "overflow-y") {
			style.overflowY = first;
			return accept(CssProperty::OverflowY);
		}
		style.overflowX = first;
		style.overflowY = tokens.size() == 2 ? second : first;
		return acceptMask(cssPropertyBit(CssProperty::OverflowX) | cssPropertyBit(CssProperty::OverflowY));
	}
	if (prop == "visibility") {
		const std::string lower = toLower(val);
		if (lower == "visible") {
			style.visibility = VisibilityMode::Visible;
			return accept(CssProperty::Visibility);
		}
		if (lower == "hidden") {
			style.visibility = VisibilityMode::Hidden;
			return accept(CssProperty::Visibility);
		}
		// collapse is deliberately unsupported; it is not a hidden alias.
		++diag.unsupportedDeclarationCount;
		return false;
	}
	if (prop == "opacity") {
		int opacity = 100;
		if (!parseCssOpacityValue(val, opacity, diag)) {
			++diag.unsupportedDeclarationCount;
			return false;
		}
		style.opacityPercent = opacity;
		return accept(CssProperty::Opacity);
	}
	if (prop == "vertical-align") {
		if (!parseCssVerticalAlignValue(val, style.verticalAlign, style.verticalAlignValue,
			style.verticalAlignValueClamped, diag)) {
			++diag.unsupportedDeclarationCount;
			return false;
		}
		return accept(CssProperty::VerticalAlign);
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
			style.lineHeightMode = LineHeightMode::Normal;
			style.lineHeightValue = 0;
			style.lineHeight = -1;
			return accept(CssProperty::LineHeight);
		}
		double numeric = 0.0;
		// A unitless line-height is a multiplier, unlike a unitless length.
		if (parseCssNumber(val, numeric)) {
			if (!std::isfinite(numeric) || numeric < 0.0) {
				++diag.unsupportedDeclarationCount;
				return false;
			}
			int multiplier = roundCssNumber(numeric * 1000.0);
			if (multiplier > kCssLiteMaxLineHeightPx * 1000) {
				multiplier = kCssLiteMaxLineHeightPx * 1000;
				++diag.clampedValueCount;
			}
			style.lineHeightNormal = false;
			style.lineHeightMode = LineHeightMode::Unitless;
			style.lineHeightValue = multiplier;
			style.lineHeight = std::max(0, std::min(kCssLiteMaxLineHeightPx,
				roundCssNumber(numeric * static_cast<double>(style.fontScaleOrSize > 0 ? style.fontScaleOrSize : 16))));
			return accept(CssProperty::LineHeight);
		}
		std::string unitValue = lower;
		bool percent = !unitValue.empty() && unitValue.back() == '%';
		if (percent) unitValue.pop_back();
		if (percent) {
			if (!parseCssNumber(unitValue, numeric) || !std::isfinite(numeric) || numeric < 0.0) {
				++diag.unsupportedDeclarationCount;
				return false;
			}
			int percentage = roundCssNumber(numeric);
			if (percentage > 600) {
				percentage = 600;
				++diag.clampedValueCount;
			}
			style.lineHeightNormal = false;
			style.lineHeightMode = LineHeightMode::Percent;
			style.lineHeightValue = percentage;
			style.lineHeight = std::max(0, std::min(kCssLiteMaxLineHeightPx,
				roundCssNumber((style.fontScaleOrSize > 0 ? style.fontScaleOrSize : 16) * numeric / 100.0)));
			return accept(CssProperty::LineHeight);
		}
		bool autoValue = false;
		int px = 0;
		if (!parseCssLengthValue(lower, 16, px, autoValue, false)) {
			++diag.unsupportedDeclarationCount;
			return false;
		}
		if (autoValue || px < 0) {
			++diag.unsupportedDeclarationCount;
			return false;
		}
		style.lineHeightNormal = false;
		style.lineHeightMode = LineHeightMode::Px;
		style.lineHeightValue = clampCssValue(diag, px, 0, kCssLiteMaxLineHeightPx);
		style.lineHeight = clampCssValue(diag, px, 8, kCssLiteMaxLineHeightPx);
		return accept(CssProperty::LineHeight);
	}
	if (prop == "width" || prop == "min-width" || prop == "max-width" ||
		prop == "height" || prop == "min-height" || prop == "max-height") {
		const bool isMax = prop == "max-width" || prop == "max-height";
		CssLengthValue parsed;
		if (!parseCssBoundedDimension(val, parsed, diag, !isMax && prop != "min-width" && prop != "min-height", isMax, true)) {
			++diag.unsupportedDeclarationCount;
			++diag.invalidLengthValueCount;
			return false;
		}
		auto setLegacy = [&](int& px, int& percent, bool* none) {
			px = -1;
			percent = -1;
			if (none) *none = false;
			if (parsed.type == CssLengthType::Auto) {
				px = 0;
			} else if (parsed.type == CssLengthType::None) {
				if (none) *none = true;
			} else if (parsed.type == CssLengthType::Percent) {
				percent = parsed.value;
			} else {
				px = parsed.value;
			}
		};
		if (prop == "width") {
			style.widthValue = parsed;
			setLegacy(style.width, style.widthPercent, nullptr);
			return accept(CssProperty::Width);
		}
		if (prop == "min-width") {
			style.minWidthValue = parsed;
			setLegacy(style.minWidth, style.minWidthPercent, nullptr);
			return accept(CssProperty::MinWidth);
		}
		if (prop == "max-width") {
			style.maxWidthValue = parsed;
			setLegacy(style.maxWidth, style.maxWidthPercent, &style.maxWidthNone);
			return accept(CssProperty::MaxWidth);
		}
		if (prop == "height") {
			style.heightValue = parsed;
			setLegacy(style.height, style.heightPercent, nullptr);
			return accept(CssProperty::Height);
		}
		if (prop == "min-height") {
			style.minHeightValue = parsed;
			setLegacy(style.minHeight, style.minHeightPercent, nullptr);
			return accept(CssProperty::MinHeight);
		}
		style.maxHeightValue = parsed;
		setLegacy(style.maxHeight, style.maxHeightPercent, &style.maxHeightNone);
		return accept(CssProperty::MaxHeight);
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
		if (lower == "nowrap") {
			style.whiteSpace = WhiteSpaceMode::Nowrap;
			return accept(CssProperty::WhiteSpace);
		}
		if (lower == "pre-line") {
			style.whiteSpace = WhiteSpaceMode::PreLine;
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
	case CssProperty::BoxSizing:
		destination.boxSizing = source.boxSizing;
		destination.boxSizingSpecified = source.boxSizingSpecified;
		break;
	case CssProperty::MinWidth:
		destination.minWidthValue = source.minWidthValue;
		destination.minWidth = source.minWidth;
		destination.minWidthPercent = source.minWidthPercent;
		break;
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
		destination.lineHeightMode = source.lineHeightMode;
		destination.lineHeightValue = source.lineHeightValue;
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
	case CssProperty::Width:
		destination.widthValue = source.widthValue;
		destination.width = source.width;
		destination.widthPercent = source.widthPercent;
		break;
	case CssProperty::Height:
		destination.heightValue = source.heightValue;
		destination.height = source.height;
		destination.heightPercent = source.heightPercent;
		break;
	case CssProperty::MaxWidth:
		destination.maxWidthValue = source.maxWidthValue;
		destination.maxWidth = source.maxWidth;
		destination.maxWidthPercent = source.maxWidthPercent;
		destination.maxWidthNone = source.maxWidthNone;
		break;
	case CssProperty::MaxHeight:
		destination.maxHeightValue = source.maxHeightValue;
		destination.maxHeight = source.maxHeight;
		destination.maxHeightPercent = source.maxHeightPercent;
		destination.maxHeightNone = source.maxHeightNone;
		break;
	case CssProperty::MinHeight:
		destination.minHeightValue = source.minHeightValue;
		destination.minHeight = source.minHeight;
		destination.minHeightPercent = source.minHeightPercent;
		break;
	case CssProperty::OverflowX: destination.overflowX = source.overflowX; break;
	case CssProperty::OverflowY: destination.overflowY = source.overflowY; break;
	case CssProperty::Visibility: destination.visibility = source.visibility; break;
	case CssProperty::Opacity:
		destination.opacityPercent = source.opacityPercent;
		break;
	case CssProperty::VerticalAlign:
		destination.verticalAlign = source.verticalAlign;
		destination.verticalAlignValue = source.verticalAlignValue;
		destination.verticalAlignValueClamped = source.verticalAlignValueClamped;
		break;
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

static bool findCssDeclarationEnd(const std::string& text,
	size_t start,
	size_t& outEnd,
	CssDiagnostics& diag)
{
	int parentheses = 0;
	int brackets = 0;
	char quote = 0;
	for (size_t i = start; i < text.size(); ++i) {
		const char c = text[i];
		if (quote != 0) {
			if (c == '\\' && i + 1 < text.size()) { ++i; continue; }
			if (c == quote) quote = 0;
			if (i - start >= kCssLiteMaxStringScanBytes) {
				saturatingIncrement(diag.unterminatedStringErrors);
				return false;
			}
			continue;
		}
		if (c == '\'' || c == '"') { quote = c; continue; }
		if (c == '(') {
			if (++parentheses > static_cast<int>(kCssLiteMaxDelimiterDepth)) {
				saturatingIncrement(diag.unbalancedParenthesisErrors);
				return false;
			}
		} else if (c == ')') {
			if (parentheses == 0) {
				saturatingIncrement(diag.unbalancedParenthesisErrors);
				return false;
			}
			--parentheses;
		} else if (c == '[') {
			if (++brackets > static_cast<int>(kCssLiteMaxDelimiterDepth)) {
				saturatingIncrement(diag.unbalancedBracketErrors);
				return false;
			}
		} else if (c == ']') {
			if (brackets == 0) {
				saturatingIncrement(diag.unbalancedBracketErrors);
				return false;
			}
			--brackets;
		} else if (c == ';' && parentheses == 0 && brackets == 0) {
			outEnd = i;
			return true;
		}
	}
	if (quote != 0) {
		saturatingIncrement(diag.unterminatedStringErrors);
		return false;
	}
	if (parentheses != 0) saturatingIncrement(diag.unbalancedParenthesisErrors);
	if (brackets != 0) saturatingIncrement(diag.unbalancedBracketErrors);
	outEnd = text.size();
	return parentheses == 0 && brackets == 0;
}

static size_t findCssDeclarationColon(const std::string& text)
{
	int parentheses = 0;
	int brackets = 0;
	char quote = 0;
	for (size_t i = 0; i < text.size(); ++i) {
		const char c = text[i];
		if (quote != 0) {
			if (c == '\\' && i + 1 < text.size()) { ++i; continue; }
			if (c == quote) quote = 0;
			continue;
		}
		if (c == '\'' || c == '"') { quote = c; continue; }
		if (c == '(') ++parentheses;
		else if (c == ')' && parentheses > 0) --parentheses;
		else if (c == '[') ++brackets;
		else if (c == ']' && brackets > 0) --brackets;
		else if (c == ':' && parentheses == 0 && brackets == 0) return i;
	}
	return std::string::npos;
}

static void parseCssDeclarations(const std::string& body, WebStyle& style, CssDiagnostics& diag)
{
	std::string normalizedBody;
	normalizeCssComments(body, normalizedBody, diag);
	size_t cursor = 0;
	size_t declarationCount = 0;
	while (cursor < normalizedBody.size()) {
		if (declarationCount >= kCssLiteMaxDeclarationsPerRule ||
			diag.declarationsProcessed >= static_cast<int>(kCssLiteMaxTotalDeclarations)) {
			saturatingIncrement(diag.declarationCapCount);
			break;
		}
		size_t end = normalizedBody.size();
		if (!findCssDeclarationEnd(normalizedBody, cursor, end, diag)) break;
		std::string decl = normalizedBody.substr(cursor, end - cursor);
		size_t colon = findCssDeclarationColon(decl);
		if (colon != std::string::npos) {
			++declarationCount;
			saturatingIncrement(diag.declarationsProcessed);
			WebStyle parsed;
			// Preserve the unitless multiplier so inheritance can resolve it
			// against the inheriting element's font size during inline layout.
			parsed.fontScaleOrSize = style.fontScaleOrSize;
			parseInlineStyleDeclaration(parsed, decl.substr(0, colon), decl.substr(colon + 1), diag);
			mergeParsedDeclaration(style, parsed);
		}
		cursor = end >= normalizedBody.size() ? normalizedBody.size() : end + 1;
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
	if ((overrideStyle.specifiedProperties & cssPropertyBit(CssProperty::BoxSizing)) != 0)
		merged.boxSizing = overrideStyle.boxSizing;
	if ((overrideStyle.specifiedProperties & cssPropertyBit(CssProperty::BoxSizing)) != 0)
		merged.boxSizingSpecified = overrideStyle.boxSizingSpecified;
	if ((overrideStyle.specifiedProperties & cssPropertyBit(CssProperty::OverflowX)) != 0)
		merged.overflowX = overrideStyle.overflowX;
	if ((overrideStyle.specifiedProperties & cssPropertyBit(CssProperty::OverflowY)) != 0)
		merged.overflowY = overrideStyle.overflowY;
	if ((overrideStyle.specifiedProperties & cssPropertyBit(CssProperty::Visibility)) != 0)
		merged.visibility = overrideStyle.visibility;
	if ((overrideStyle.specifiedProperties & cssPropertyBit(CssProperty::Opacity)) != 0)
		merged.opacityPercent = overrideStyle.opacityPercent;
	if ((overrideStyle.specifiedProperties & cssPropertyBit(CssProperty::VerticalAlign)) != 0) {
		merged.verticalAlign = overrideStyle.verticalAlign;
		merged.verticalAlignValue = overrideStyle.verticalAlignValue;
		merged.verticalAlignValueClamped = overrideStyle.verticalAlignValueClamped;
	}
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
	if ((overrideStyle.specifiedProperties & cssPropertyBit(CssProperty::LineHeight)) != 0 &&
		overrideStyle.lineHeightNormal) {
		merged.lineHeightNormal = true;
		merged.lineHeightMode = LineHeightMode::Normal;
		merged.lineHeightValue = 0;
		merged.lineHeight = -1;
	} else if ((overrideStyle.specifiedProperties & cssPropertyBit(CssProperty::LineHeight)) != 0) {
		merged.lineHeightNormal = false;
		merged.lineHeightMode = overrideStyle.lineHeightMode;
		merged.lineHeightValue = overrideStyle.lineHeightValue;
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
	if (overrideStyle.widthValue.valid) merged.widthValue = overrideStyle.widthValue;
	merged.height = overrideStyle.height != -1 ? overrideStyle.height : merged.height;
	merged.heightPercent = overrideStyle.heightPercent != -1 ? overrideStyle.heightPercent : merged.heightPercent;
	if (overrideStyle.heightValue.valid) merged.heightValue = overrideStyle.heightValue;
	merged.minWidth = overrideStyle.minWidth != -1 ? overrideStyle.minWidth : merged.minWidth;
	merged.minWidthPercent = overrideStyle.minWidthPercent != -1 ? overrideStyle.minWidthPercent : merged.minWidthPercent;
	if (overrideStyle.minWidthValue.valid) merged.minWidthValue = overrideStyle.minWidthValue;
	merged.maxWidth = overrideStyle.maxWidth != -1 ? overrideStyle.maxWidth : merged.maxWidth;
	merged.maxWidthPercent = overrideStyle.maxWidthPercent != -1 ? overrideStyle.maxWidthPercent : merged.maxWidthPercent;
	if ((overrideStyle.specifiedProperties & cssPropertyBit(CssProperty::MaxWidth)) != 0) {
		merged.maxWidthNone = overrideStyle.maxWidthNone;
	}
	if (overrideStyle.maxWidthValue.valid) merged.maxWidthValue = overrideStyle.maxWidthValue;
	merged.minHeight = overrideStyle.minHeight != -1 ? overrideStyle.minHeight : merged.minHeight;
	merged.minHeightPercent = overrideStyle.minHeightPercent != -1 ? overrideStyle.minHeightPercent : merged.minHeightPercent;
	if (overrideStyle.minHeightValue.valid) merged.minHeightValue = overrideStyle.minHeightValue;
	merged.maxHeight = overrideStyle.maxHeight != -1 ? overrideStyle.maxHeight : merged.maxHeight;
	merged.maxHeightPercent = overrideStyle.maxHeightPercent != -1 ? overrideStyle.maxHeightPercent : merged.maxHeightPercent;
	if ((overrideStyle.specifiedProperties & cssPropertyBit(CssProperty::MaxHeight)) != 0) {
		merged.maxHeightNone = overrideStyle.maxHeightNone;
	}
	if (overrideStyle.maxHeightValue.valid) merged.maxHeightValue = overrideStyle.maxHeightValue;
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
	if (tagName == "form") {
		style.marginTop = 4;
		style.marginBottom = 8;
		return style;
	}
	if (tagName == "fieldset") {
		style.marginTop = 8;
		style.marginBottom = 10;
		style.padding = 8;
		style.hasBorderTop = style.hasBorderRight = style.hasBorderBottom = style.hasBorderLeft = true;
		style.borderTopWidth = style.borderRightWidth = style.borderBottomWidth = style.borderLeftWidth = 1;
		style.borderTopStyle = style.borderRightStyle = style.borderBottomStyle = style.borderLeftStyle = BorderLineStyle::Solid;
		style.borderTopColor = style.borderRightColor = style.borderBottomColor = style.borderLeftColor = 0xFFB8C0CCu;
		return style;
	}
	if (tagName == "legend") {
		style.bold = true;
		style.marginTop = 2;
		style.marginBottom = 4;
		style.fontScaleOrSize = 14;
		return style;
	}
	if (tagName == "label") {
		style.marginTop = 3;
		style.marginBottom = 3;
		return style;
	}
	if (tagName == "input" || tagName == "textarea" || tagName == "select") {
		style.hasBackgroundColor = true;
		style.backgroundColor = 0xFFFFFFFFu;
		style.hasBorderTop = style.hasBorderRight = style.hasBorderBottom = style.hasBorderLeft = true;
		style.borderTopWidth = style.borderRightWidth = style.borderBottomWidth = style.borderLeftWidth = 1;
		style.borderTopStyle = style.borderRightStyle = style.borderBottomStyle = style.borderLeftStyle = BorderLineStyle::Solid;
		style.borderTopColor = style.borderRightColor = style.borderBottomColor = style.borderLeftColor = 0xFF9AA6B2u;
		style.padding = 4;
		style.marginTop = 3;
		style.marginBottom = 5;
		return style;
	}
	if (tagName == "button") {
		style.hasBackgroundColor = true;
		style.backgroundColor = 0xFFE6E8EEu;
		style.hasBorderTop = style.hasBorderRight = style.hasBorderBottom = style.hasBorderLeft = true;
		style.borderTopWidth = style.borderRightWidth = style.borderBottomWidth = style.borderLeftWidth = 1;
		style.borderTopStyle = style.borderRightStyle = style.borderBottomStyle = style.borderLeftStyle = BorderLineStyle::Solid;
		style.borderTopColor = style.borderRightColor = style.borderBottomColor = style.borderLeftColor = 0xFF8D99A8u;
		style.padding = 4;
		style.marginTop = 3;
		style.marginBottom = 5;
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
	if (style.lineHeightNormal || style.lineHeight > 0 || style.lineHeightValue > 0)
		style.specifiedProperties |= cssPropertyBit(CssProperty::LineHeight);
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
			<< "." << toLower(element.className) << "{" << element.inlineStyle << "}"
			<< "@" << element.serial << ":" << element.parentSerial
			<< ":" << element.childIndex << "/" << element.childCount
			<< ":s" << element.siblingCount
			<< ":prev=" << element.previousSiblingSerial
			<< ":" << element.typeIndex << "/" << element.typeCount
			<< ":form=" << static_cast<unsigned>(element.formControl.type)
			<< ":state=" << (element.formControl.checked ? "c" : "-")
			<< (element.formControl.disabled ? "d" : "-")
			<< (element.formControl.required ? "r" : "-")
			<< (element.formControl.readOnly ? "o" : "-")
			<< (element.formControl.selected ? "s" : "-")
			<< ":" << (element.hasLinkTarget ? (element.visited ? "visited" : "link") : "");
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
	std::string pseudoCategory;
	std::string combinatorCategory;
	std::string selectorCategory;
	uint16_t evidenceRuleIndex = 0;
	uint8_t evidenceGroupIndex = 0;
	uint32_t evidenceSelectorHash = 0;
	size_t siblingScanSteps = 0;
};

static const char* cssPseudoName(CssPseudoClass type)
{
	switch (type) {
	case CssPseudoClass::FirstChild: return "first-child";
	case CssPseudoClass::LastChild: return "last-child";
	case CssPseudoClass::OnlyChild: return "only-child";
	case CssPseudoClass::NthChild: return "nth-child";
	case CssPseudoClass::FirstOfType: return "first-of-type";
	case CssPseudoClass::LastOfType: return "last-of-type";
	case CssPseudoClass::OnlyOfType: return "only-of-type";
	case CssPseudoClass::NthOfType: return "nth-of-type";
	case CssPseudoClass::Not: return "not";
	case CssPseudoClass::Root: return "root";
	case CssPseudoClass::Link: return "link";
	case CssPseudoClass::Visited: return "visited";
	case CssPseudoClass::Empty: return "empty";
	case CssPseudoClass::Checked: return "checked";
	case CssPseudoClass::Disabled: return "disabled";
	case CssPseudoClass::Enabled: return "enabled";
	case CssPseudoClass::Required: return "required";
	case CssPseudoClass::ReadOnly: return "read-only";
	case CssPseudoClass::ReadWrite: return "read-write";
	case CssPseudoClass::Focus: return "focus";
	case CssPseudoClass::FocusVisible: return "focus-visible";
	default: return "unknown";
	}
}

static std::string selectorPseudoSummary(const WebStyleRule& rule)
{
	std::string summary;
	for (const CssSelectorPart& part : rule.selectorParts) {
		for (const CssPseudoClassSelector& pseudo : part.pseudoClasses) {
			if (!summary.empty()) summary += "+";
			summary += cssPseudoName(pseudo.type);
		}
	}
	return summary;
}

static std::string selectorCombinatorSummary(const WebStyleRule& rule)
{
	std::string summary;
	for (CssCombinator combinator : rule.combinators) {
		const char* name = "descendant";
		if (combinator == CssCombinator::Child) name = "child";
		else if (combinator == CssCombinator::AdjacentSibling) name = "adjacent-sibling";
		else if (combinator == CssCombinator::GeneralSibling) name = "general-sibling";
		if (!summary.empty()) summary += "+";
		summary += name;
	}
	return summary.empty() ? "none" : summary;
}

static const char* selectorTypeName(StyleSelectorType type)
{
	switch (type) {
	case StyleSelectorType::Id: return "id";
	case StyleSelectorType::Class: return "class";
	default: return "element";
	}
}

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
		CssProperty::WordBreak, CssProperty::Visibility,
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

static std::string boundedEvidenceToken(const std::string& raw)
{
	std::string token = collapseWs(raw);
	if (token.size() > kCssLiteMaxEvidenceTokenBytes)
		token.resize(kCssLiteMaxEvidenceTokenBytes);
	for (char& c : token) {
		if (c == ';' || c == '\n' || c == '\r') c = '_';
	}
	return token;
}

static uint32_t formRadioGroupEvidenceHash(const HtmlElementRef& element)
{
	if (element.formControl.type != FormControlType::Radio) return 0;
	uint32_t hash = 2166136261u;
	auto mix = [&](uint64_t value) {
		for (unsigned i = 0; i < sizeof(value); ++i) {
			hash ^= static_cast<uint32_t>((value >> (i * 8)) & 0xFFu);
			hash *= 16777619u;
		}
	};
	mix(element.formControl.parentFormSerial);
	mix(element.formControl.parentFieldsetSerial);
	for (unsigned char c : element.formControl.name) {
		hash ^= static_cast<uint32_t>(c);
		hash *= 16777619u;
	}
	return hash;
}

static const char* cssLengthTypeName(CssLengthType type)
{
	switch (type) {
	case CssLengthType::Auto: return "auto";
	case CssLengthType::Px: return "px";
	case CssLengthType::Percent: return "percent";
	case CssLengthType::Zero: return "zero";
	case CssLengthType::None: return "none";
	default: return "unset";
	}
}

static std::string cssLengthEvidence(const CssLengthValue& value)
{
	std::ostringstream oss;
	oss << cssLengthTypeName(value.type);
	if (value.type == CssLengthType::Px || value.type == CssLengthType::Percent ||
		value.type == CssLengthType::Zero) {
		oss << ":" << value.value;
	}
	if (value.clamped) oss << ":clamped";
	return oss.str();
}

static const char* formFocusOriginName(FormFocusOrigin origin)
{
	switch (origin) {
	case FormFocusOrigin::Mouse: return "mouse";
	case FormFocusOrigin::Keyboard: return "keyboard";
	case FormFocusOrigin::ProgrammaticInternalSmoke: return "programmatic/internal-smoke";
	default: return "none";
	}
}

static void appendComputedStyleEvidence(WebDocument& doc,
	const HtmlElementRef& element,
	const WebStyle& style,
	const std::array<CssCascadeWinner, static_cast<size_t>(CssProperty::Count)>& winners)
{
	const std::string id = toLower(element.id);
	if (id.rfind("phase2a-", 0) != 0 && id.rfind("css2a-", 0) != 0 &&
		id.rfind("phase2b-", 0) != 0 && id.rfind("css2b-", 0) != 0 &&
		id.rfind("phase2c-", 0) != 0 && id.rfind("css2c-", 0) != 0 &&
		id.rfind("phase2d-", 0) != 0 && id.rfind("css2d-", 0) != 0 &&
		id.rfind("phase2e-", 0) != 0 && id.rfind("css2e-", 0) != 0 &&
		id.rfind("phase2f-", 0) != 0 && id.rfind("css2f-", 0) != 0 &&
		id.rfind("phase2g-", 0) != 0 && id.rfind("css2g-", 0) != 0 &&
		id.rfind("phase2h-", 0) != 0 && id.rfind("css2h-", 0) != 0 &&
		id.rfind("phase3a-", 0) != 0 && id.rfind("css3a-", 0) != 0) return;
	const bool phase3aEvidence = id.rfind("phase3a-", 0) == 0 || id.rfind("css3a-", 0) == 0;
	const bool phase2gEvidence = id.rfind("phase2g-", 0) == 0 || id.rfind("css2g-", 0) == 0;
	const bool phase2hEvidence = id.rfind("phase2h-", 0) == 0 || id.rfind("css2h-", 0) == 0;
	if (std::find(doc.cssDiagnostics.computedStyleEvidenceSerials.begin(),
		doc.cssDiagnostics.computedStyleEvidenceSerials.end(), element.serial) !=
		doc.cssDiagnostics.computedStyleEvidenceSerials.end()) return;
	if (doc.cssDiagnostics.computedStyleEvidence.size() >= kCssLiteMaxEvidenceBytes ||
		doc.cssDiagnostics.computedStyleEvidenceSerials.size() >= kCssLiteMaxEvidenceEntries) return;
	const CssCascadeWinner& colorWinner = winners[static_cast<size_t>(CssProperty::Color)];
	const CssCascadeWinner& paddingWinner = winners[static_cast<size_t>(CssProperty::PaddingTop)];
	const CssCascadeWinner& fontWinner = winners[static_cast<size_t>(CssProperty::FontSize)];
	const CssCascadeWinner& borderWinner = winners[static_cast<size_t>(CssProperty::BorderTopWidth)];
	std::string previousSiblingTag = "none";
	if (element.previousSiblingSerial > 0 && element.previousSiblingSerial <= doc.structuralElements.size()) {
		const HtmlElementRef& previous = doc.structuralElements[static_cast<size_t>(element.previousSiblingSerial - 1)];
		if (previous.serial == element.previousSiblingSerial) previousSiblingTag = toLower(previous.tagName);
	}
	const HtmlElementContentMetadata* content = findContentMetadata(doc, element.serial);
	const char* computedEmpty = "unknown";
	if (content && content->contentMetadataComplete) {
		computedEmpty = contentMetadataProvesEmpty(*content) ? "yes" : "no";
	}
	const uint64_t colorBit = cssPropertyBit(CssProperty::Color);
	const uint64_t paddingBit = cssPropertyBit(CssProperty::PaddingTop);
	const uint64_t fontBit = cssPropertyBit(CssProperty::FontSize);
	const uint64_t borderBit = cssPropertyBit(CssProperty::BorderTopWidth);
	const FormRuntimeControlState* runtime = runtimeControlState(doc, element.serial);
	const bool checked = effectiveChecked(doc, element);
	const bool disabled = effectiveDisabled(doc, element);
	const bool focused = effectiveFocused(doc, element);
	const bool focusVisible = effectiveFocusVisible(doc, element);
	const bool focusable = element.serial != 0 && element.formControl.metadataComplete &&
		element.formControl.supported && !element.formControl.hidden && !disabled &&
		element.formControl.type != FormControlType::Option &&
		element.formControl.type != FormControlType::Unsupported &&
		element.formControl.type != FormControlType::None;
	std::ostringstream oss;
	oss << "id=" << boundedEvidenceToken(element.id) << ",tag=" << boundedEvidenceToken(toLower(element.tagName))
		<< ",classes=" << boundedEvidenceToken(element.className)
		<< ",color=" << cssColorEvidence(style.hasColor, style.color)
		<< ",background=" << cssColorEvidence(style.hasBackgroundColor, style.backgroundColor)
		<< ",font-size=" << style.fontScaleOrSize
		<< ",line-height=" << (style.lineHeightNormal ? "normal" : std::to_string(style.lineHeight))
		<< ",padding-top=" << style.paddingTop
		<< ",border-top-width=" << style.borderTopWidth
		<< ",color-specificity=" << colorWinner.specificity.idCount << "." << colorWinner.specificity.classCount << "." << colorWinner.specificity.elementCount
		<< ",color-source-order=" << colorWinner.sourceOrder
		<< ",color-inherited=" << ((style.inheritedProperties & colorBit) != 0 ? "yes" : "no")
		<< ",color-explicit=" << ((style.specifiedProperties & colorBit) != 0 ? "yes" : "no")
		<< ",color-important=" << (colorWinner.important ? "yes" : "no")
		<< ",padding-source-order=" << paddingWinner.sourceOrder
		<< ",padding-explicit=" << ((style.specifiedProperties & paddingBit) != 0 ? "yes" : "no")
		<< ",padding-important=" << (paddingWinner.important ? "yes" : "no")
		<< ",font-size-source-order=" << fontWinner.sourceOrder
		<< ",font-size-explicit=" << ((style.specifiedProperties & fontBit) != 0 ? "yes" : "no")
		<< ",font-size-important=" << (fontWinner.important ? "yes" : "no")
		<< ",border-source-order=" << borderWinner.sourceOrder
		<< ",border-explicit=" << ((style.specifiedProperties & borderBit) != 0 ? "yes" : "no")
		<< ",border-important=" << (borderWinner.important ? "yes" : "no")
		<< ",logical-serial=" << element.serial
		<< ",parent-serial=" << element.parentSerial
		<< ",content-metadata=" << (content ? (content->contentMetadataComplete ? "complete" : "incomplete") : "missing")
		<< ",element-child-count=" << (content ? content->elementChildCount : 0)
		<< ",has-nonwhitespace-text=" << (content && content->hasNonWhitespaceText ? "yes" : "no")
		<< ",has-media-replaced=" << (content && (content->hasImageOrMediaChild || content->hasVisibleReplacedContent) ? "yes" : "no")
		<< ",has-visible-break=" << (content && content->hasVisibleBreak ? "yes" : "no")
		<< ",has-renderable-content=" << (content && content->hasRenderableContent ? "yes" : "no")
		<< ",computed-empty=" << computedEmpty
		<< ",previous-sibling-serial=" << element.previousSiblingSerial
		<< ",previous-sibling-tag=" << previousSiblingTag
		<< ",winning-combinator=" << (colorWinner.combinatorCategory.empty() ? "none" : colorWinner.combinatorCategory)
		<< ",color-winning-selector=" << (colorWinner.selectorCategory.empty() ? "none" : colorWinner.selectorCategory)
		<< ",color-winning-rule-index=" << colorWinner.evidenceRuleIndex
		<< ",color-winning-group-index=" << static_cast<unsigned>(colorWinner.evidenceGroupIndex)
		<< ",color-winning-selector-hash=" << (colorWinner.valid ? colorWinner.evidenceSelectorHash : 0)
		<< ",sibling-scan-steps=" << colorWinner.siblingScanSteps
		<< ",element-index=" << element.childIndex
		<< ",element-count=" << element.siblingCount
		<< ",type-index=" << element.typeIndex
		<< ",type-count=" << element.typeCount
		<< ",control-type=" << static_cast<unsigned>(element.formControl.type)
		<< ",control-supported=" << (element.formControl.supported ? "yes" : "no")
		<< ",control-checked=" << (checked ? "yes" : "no")
		<< ",control-disabled=" << (disabled ? "yes" : "no")
		<< ",control-enabled=" << (element.formControl.supported && !disabled ? "yes" : "no")
		<< ",parsed-checked=" << ((element.formControl.checked || element.formControl.selected) ? "yes" : "no")
		<< ",runtime-checked=" << (runtime ? (runtime->checked ? "yes" : "no") : "absent")
		<< ",runtime-activation-count=" << (runtime ? runtime->activationCount : 0)
		<< ",runtime-metadata-valid=" << (runtime ? (runtime->metadataValid ? "yes" : "no") : "no");
	if (phase3aEvidence) {
		oss << ",box-sizing=" << (style.boxSizing == BoxSizingMode::BorderBox ? "border-box" : "content-box")
			<< ",width-specified=" << cssLengthEvidence(style.widthValue)
			<< ",height-specified=" << cssLengthEvidence(style.heightValue)
			<< ",min-width-specified=" << cssLengthEvidence(style.minWidthValue)
			<< ",max-width-specified=" << cssLengthEvidence(style.maxWidthValue)
			<< ",min-height-specified=" << cssLengthEvidence(style.minHeightValue)
			<< ",max-height-specified=" << cssLengthEvidence(style.maxHeightValue)
			<< ",overflow-x=" << static_cast<unsigned>(style.overflowX)
			<< ",overflow-y=" << static_cast<unsigned>(style.overflowY)
			<< ",visibility=" << (style.visibility == VisibilityMode::Hidden ? "hidden" : "visible")
			<< ",opacity-percent=" << style.opacityPercent
			<< ",effective-opacity-percent=" << style.effectiveOpacityPercent
			<< ",vertical-align=" << static_cast<unsigned>(style.verticalAlign)
			<< ",vertical-align-value=" << style.verticalAlignValue;
	}
	if (phase2gEvidence || phase2hEvidence) {
		oss << ",document-generation=" << doc.formRuntimeState.documentGeneration
			<< ",focusable=" << (focusable ? "yes" : "no")
			<< ",focused=" << (focused ? "yes" : "no")
			<< ",focus-origin=" << formFocusOriginName(doc.formRuntimeState.focusOrigin)
			<< ",focus-document-generation=" << doc.formRuntimeState.focusedDocumentGeneration
			<< ",focus-pseudo-match=" << (focused ? "yes" : "no")
			<< ",focus-visible-pseudo-match=" << (focusVisible ? "yes" : "no");
	}
	oss << ",radio-group-hash=" << formRadioGroupEvidenceHash(element)
		<< ",control-required=" << (element.formControl.required ? "yes" : "no")
		<< ",control-readonly=" << (element.formControl.readOnly ? "yes" : "no")
		<< ",color-winning-pseudo=" << (colorWinner.pseudoCategory.empty() ? "none" : colorWinner.pseudoCategory);
	if (phase2gEvidence || phase2hEvidence) {
		oss << ",winning-focus-pseudo=" << ((colorWinner.pseudoCategory.find("focus") != std::string::npos) ? colorWinner.pseudoCategory : "none");
	}
	oss << ";";
	const std::string evidence = oss.str();
	if (doc.cssDiagnostics.computedStyleEvidence.size() + evidence.size() <= kCssLiteMaxEvidenceBytes) {
		doc.cssDiagnostics.computedStyleEvidence += evidence;
		doc.cssDiagnostics.computedStyleEvidenceSerials.push_back(element.serial);
	}
}

static bool buildStructuralPath(const WebDocument& doc,
	uint64_t serial,
	std::vector<HtmlElementRef>& outPath)
{
	outPath.clear();
	if (serial == 0) return false;
	uint64_t current = serial;
	for (size_t depth = 0; depth < kCssLiteMaxInheritanceDepth + 2 && current != 0; ++depth) {
		const HtmlElementRef* found = nullptr;
		for (const HtmlElementRef& candidate : doc.structuralElements) {
			if (candidate.serial == current) {
				found = &candidate;
				break;
			}
		}
		if (!found) return false;
		outPath.push_back(*found);
		current = found->parentSerial;
	}
	if (current != 0) return false;
	std::reverse(outPath.begin(), outPath.end());
	return !outPath.empty();
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
	const int parentOpacity = hasParent ? parent.effectiveOpacityPercent : 100;
	const int localOpacity = std::max(0, std::min(100, style.opacityPercent));
	style.effectiveOpacityPercent = std::max(0, std::min(100,
		(parentOpacity * localOpacity + 50) / 100));

	for (const WebStyleRule& rule : doc.styleRules) {
		CssSelectorMatchTrace matchTrace;
		if (!selectorMatchesPath(path, doc, doc.cssDiagnostics, rule, &matchTrace)) continue;
		saturatingIncrement(doc.cssDiagnostics.selectorMatches);
		for (unsigned i = 0; i < static_cast<unsigned>(CssProperty::Count); ++i) {
			const CssProperty property = static_cast<CssProperty>(i);
			const uint64_t bit = cssPropertyBit(property);
			if ((rule.style.specifiedProperties & bit) == 0) continue;
			if (rule.hasVisitedPseudo && property != CssProperty::Color &&
				property != CssProperty::Bold && property != CssProperty::TextDecoration) continue;
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
			candidate.pseudoCategory = selectorPseudoSummary(rule);
			candidate.combinatorCategory = selectorCombinatorSummary(rule);
			candidate.selectorCategory = selectorTypeName(rule.selectorType);
			candidate.evidenceRuleIndex = rule.evidenceRuleIndex;
			candidate.evidenceGroupIndex = rule.evidenceGroupIndex;
			candidate.evidenceSelectorHash = rule.evidenceSelectorHash;
			candidate.siblingScanSteps = matchTrace.siblingScanSteps;
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
	style.effectiveOpacityPercent = std::max(0, std::min(100,
		(parentOpacity * std::max(0, std::min(100, style.opacityPercent)) + 50) / 100));
	if (hasParent && parent.displayNone) style.displayNone = true;
	appendComputedStyleEvidence(doc, path.back(), style, winners);

	cache.emplace(key, style);
	return style;
}

static void applyDocumentStyles(WebDocument& doc)
{
	doc.cssDiagnostics.cssEnabled = true;
	doc.computedStyles.clear();
	std::unordered_map<std::string, WebStyle> cache;
	for (FormContainerMetadata& container : doc.formContainers) {
		std::vector<HtmlElementRef> path;
		if (buildStructuralPath(doc, container.serial, path)) {
			container.style = computePathStyle(doc, path, cache);
		} else {
			container.metadataComplete = false;
			container.style = defaultStyleForTag(container.tagName);
			markDefaultStyleProperties(container.style);
		}
	}

	if (doc.hasBodyElement) {
		std::vector<HtmlElementRef> bodyPath;
		if (doc.hasDocumentElement) bodyPath.push_back(doc.documentElement);
		if (bodyPath.empty() || bodyPath.back().serial != doc.bodyElement.serial)
			bodyPath.push_back(doc.bodyElement);
		doc.bodyStyle = computePathStyle(doc, bodyPath, cache);
	} else {
		doc.bodyStyle = defaultStyleForTag("body");
		markDefaultStyleProperties(doc.bodyStyle);
	}

	for (DocBlock& block : doc.blocks) {
		std::vector<HtmlElementRef> path;
		if (doc.hasDocumentElement) path.push_back(doc.documentElement);
		if (doc.hasBodyElement && (path.empty() || path.back().serial != doc.bodyElement.serial))
			path.push_back(doc.bodyElement);
		for (const HtmlElementRef& ancestor : block.ancestors) {
			if (ancestor.serial != 0 && std::any_of(path.begin(), path.end(), [&](const HtmlElementRef& existing) {
				return existing.serial == ancestor.serial;
			})) continue;
			path.push_back(ancestor);
		}

		HtmlElementRef selfRef = block.elementMetadata;
		if (selfRef.tagName.empty()) selfRef.tagName = block.tagName;
		selfRef.className = block.className;
		selfRef.id = block.id;
		selfRef.inlineStyle = block.inlineStyle;
		selfRef.formControl = block.formControl;
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
					block.style.widthValue = ancestorStyle.widthValue;
				}
				if (block.style.minWidth == -1 && block.style.minWidthPercent == -1)
					block.style.minWidthValue = ancestorStyle.minWidthValue;
				if (block.style.maxWidth == -1 && block.style.maxWidthPercent == -1) {
					block.style.maxWidth = ancestorStyle.maxWidth;
					block.style.maxWidthPercent = ancestorStyle.maxWidthPercent;
					block.style.maxWidthNone = ancestorStyle.maxWidthNone;
					block.style.maxWidthValue = ancestorStyle.maxWidthValue;
				}
				if (!block.style.boxSizingSpecified) {
					block.style.boxSizing = ancestorStyle.boxSizing;
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
	// Retain one bounded computed-style record per structural serial so the
	// Navigator can resolve descendant percentages against ancestor content
	// boxes without constructing a live DOM tree.
	doc.computedStyles.reserve(std::min<size_t>(doc.structuralElements.size(), 1024));
	for (const HtmlElementRef& element : doc.structuralElements) {
		if (element.serial == 0 || doc.computedStyles.size() >= 1024) break;
		std::vector<HtmlElementRef> path;
		if (!buildStructuralPath(doc, element.serial, path)) continue;
		CssComputedStyleRecord record;
		record.serial = element.serial;
		record.style = computePathStyle(doc, path, cache);
		record.valid = true;
		doc.computedStyles.push_back(std::move(record));
	}
	// Options do not become standalone layout blocks, but they still carry
	// bounded state metadata.  Resolve their paths so option:checked and
	// option:disabled participate in deterministic selector diagnostics without
	// creating any popup or hidden layout surface.
	for (const HtmlElementRef& element : doc.structuralElements) {
		if (element.formControl.type != FormControlType::Option || element.serial == 0)
			continue;
		std::vector<HtmlElementRef> path;
		if (buildStructuralPath(doc, element.serial, path))
			(void)computePathStyle(doc, path, cache);
	}
}

static DocBlock makeTextBlock(BlockType type,
	const std::string& tagName,
	const std::string& text,
	const std::string& url,
	const std::string& className,
	const std::string& id,
	const std::vector<HtmlElementRef>& ancestors = {},
	const std::string& inlineStyle = {},
	const HtmlElementRef& elementMetadata = {})
{
	DocBlock block;
	block.type = type;
	block.tagName = tagName;
	block.className = className;
	block.id = id;
	block.inlineStyle = inlineStyle;
	block.ancestors = ancestors;
	block.elementMetadata = elementMetadata;
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
	Legend,
	Label,
};

struct StructuralChildCounter {
	uint64_t serial = 0;
	uint64_t lastChildSerial = 0;
	uint16_t childCount = 0;
	std::vector<std::pair<std::string, uint16_t>> typeCounts;
};

struct ParserState {
	WebDocument  doc;
	std::string  textBuf;   // accumulated character data for current block
	std::string  hrefBuf;   // href of the open <a> tag
	std::string  classBuf;
	std::string  idBuf;
	std::string  styleBuf;
	std::vector<HtmlElementRef> openElements;
	std::vector<HtmlElementRef> structuralElements;
	std::vector<HtmlElementContentMetadata> contentMetadata;
	std::vector<StructuralChildCounter> structuralCounters;
	uint64_t     nextElementSerial = 1;
	uint64_t     activeBlockSerial = 0;
	size_t       uncapturedOpenElementDepth = 0;
	const std::unordered_set<std::string>* visitedUrls = nullptr;
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
	uint64_t     currentFormSerial = 0;
	std::string  currentTextareaName;
	std::string  currentTextareaClass;
	std::string  currentTextareaId;
	std::string  currentTextareaPlaceholder;
	bool         currentTextareaDisabled = false;
	bool         currentTextareaRequired = false;
	bool         currentTextareaReadOnly = false;
	int          currentTextareaRows = 0;
	int          currentTextareaCols = 0;
	bool         inSelect = false;
	std::string  currentSelectName;
	std::string  currentSelectClass;
	std::string  currentSelectId;
	std::string  currentSelectInputType;
	bool         currentSelectDisabled = false;
	bool         currentSelectRequired = false;
	bool         currentSelectMultiple = false;
	int          currentSelectSize = 0;
	uint64_t     currentSelectSerial = 0;
	std::vector<FormOption> currentSelectOptions;
	std::string  currentOptionValue;
	bool         currentOptionSelected = false;
	bool         currentOptionDisabled = false;
	std::string  currentLabelFor;
	std::string  currentLabelClass;
	std::string  currentLabelId;
	std::string  currentLegendText;
	uint64_t     currentLegendSerial = 0;
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

static HtmlElementRef* findStructuralElement(ParserState& st, uint64_t serial)
{
	if (serial == 0) return nullptr;
	for (HtmlElementRef& element : st.structuralElements) {
		if (element.serial == serial) return &element;
	}
	return nullptr;
}

static StructuralChildCounter* findStructuralCounter(ParserState& st, uint64_t serial)
{
	if (serial == 0) return nullptr;
	for (StructuralChildCounter& counter : st.structuralCounters) {
		if (counter.serial == serial) return &counter;
	}
	return nullptr;
}

static HtmlElementContentMetadata* findContentMetadata(ParserState& st, uint64_t serial)
{
	if (serial == 0) return nullptr;
	for (HtmlElementContentMetadata& metadata : st.contentMetadata) {
		if (metadata.serial == serial) return &metadata;
	}
	return nullptr;
}

static const HtmlElementContentMetadata* findContentMetadata(const WebDocument& doc,
	uint64_t serial)
{
	if (serial == 0) return nullptr;
	for (const HtmlElementContentMetadata& metadata : doc.contentMetadata) {
		if (metadata.serial == serial) return &metadata;
	}
	return nullptr;
}

static bool isNonRenderedMetadataElement(const std::string& tagName)
{
	const std::string tag = toLower(tagName);
	return tag == "head" || tag == "title" || tag == "meta" ||
		tag == "link" || tag == "base" || tag == "style" || tag == "script";
}

static void markContentMetadata(ParserState& st,
	uint64_t serial,
	bool hasText,
	size_t textBytes,
	bool hasImageOrMedia,
	bool hasBreak,
	bool hasReplaced,
	bool hasRenderable,
	bool uncertain = false)
{
	HtmlElementContentMetadata* metadata = findContentMetadata(st, serial);
	if (!metadata) {
		if (serial != 0) saturatingIncrement(st.doc.cssDiagnostics.contentMetadataClamps);
		return;
	}
	metadata->hasNonWhitespaceText = metadata->hasNonWhitespaceText || hasText;
	metadata->hasImageOrMediaChild = metadata->hasImageOrMediaChild || hasImageOrMedia;
	metadata->hasVisibleBreak = metadata->hasVisibleBreak || hasBreak;
	metadata->hasVisibleReplacedContent = metadata->hasVisibleReplacedContent || hasReplaced;
	metadata->hasRenderableContent = metadata->hasRenderableContent || hasRenderable;
	if (textBytes > 0) {
		const size_t current = metadata->visibleTextByteCount;
		const size_t next = std::min(kCssLiteMaxVisibleTextBytesPerElement,
			current + textBytes);
		metadata->visibleTextByteCount = static_cast<uint16_t>(next);
		if (current + textBytes > kCssLiteMaxVisibleTextBytesPerElement)
			saturatingIncrement(st.doc.cssDiagnostics.contentMetadataClamps);
	}
	if (uncertain) metadata->contentMetadataComplete = false;
}

static void markContentForOpenElements(ParserState& st,
	bool hasText,
	size_t textBytes,
	bool hasImageOrMedia,
	bool hasBreak,
	bool hasReplaced,
	bool hasRenderable,
	bool uncertain = false)
{
	const size_t limit = std::min(st.openElements.size(), kCssLiteMaxContentAggregationOperations);
	if (st.openElements.size() > limit)
		saturatingIncrement(st.doc.cssDiagnostics.contentMetadataClamps);
	for (size_t i = 0; i < limit; ++i) {
		const HtmlElementRef& element = st.openElements[i];
		if (isNonRenderedMetadataElement(element.tagName)) continue;
		markContentMetadata(st, element.serial, hasText, textBytes,
			hasImageOrMedia, hasBreak, hasReplaced, hasRenderable, uncertain);
	}
}

static void markUncertainContent(ParserState& st)
{
	markContentForOpenElements(st, false, 0, false, false, false, true, true);
}

static HtmlElementRef registerStructuralElement(ParserState& st, HtmlElementRef element)
{
	element.tagName = toLower(element.tagName);
	if (isNonRenderedMetadataElement(element.tagName)) {
		// Metadata elements remain available to the forgiving HTML state
		// machine, but they are deliberately not logical structural children.
		// A zero serial also prevents head/title/style artifacts from affecting
		// sibling counts or :empty content ownership.
		element.serial = 0;
		element.parentSerial = st.openElements.empty() ? 0 : st.openElements.back().serial;
		return element;
	}
	element.serial = st.nextElementSerial++;
	element.parentSerial = st.openElements.empty() ? 0 : st.openElements.back().serial;
	if (StructuralChildCounter* parent = findStructuralCounter(st, element.parentSerial)) {
		element.previousSiblingSerial = parent->lastChildSerial;
		if (parent->childCount < std::numeric_limits<uint16_t>::max()) {
			element.childIndex = static_cast<uint16_t>(++parent->childCount);
		} else {
			saturatingIncrement(st.doc.cssDiagnostics.structuralMetadataClamps);
			saturatingIncrement(st.doc.cssDiagnostics.siblingMetadataClamps);
		}
		uint16_t* typeCount = nullptr;
		for (auto& type : parent->typeCounts) {
			if (type.first == element.tagName) {
				typeCount = &type.second;
				break;
			}
		}
		if (!typeCount) {
			if (parent->typeCounts.size() >= kCssLiteMaxNotComponents * 2) {
				saturatingIncrement(st.doc.cssDiagnostics.structuralMetadataClamps);
				if (HtmlElementContentMetadata* parentMetadata = findContentMetadata(st, element.parentSerial))
					parentMetadata->contentMetadataComplete = false;
			} else {
				parent->typeCounts.push_back({ element.tagName, 0 });
				typeCount = &parent->typeCounts.back().second;
			}
		}
		if (typeCount && *typeCount < std::numeric_limits<uint16_t>::max())
			element.typeIndex = static_cast<uint16_t>(++(*typeCount));
		parent->lastChildSerial = element.serial;
		if (HtmlElementContentMetadata* parentMetadata = findContentMetadata(st, element.parentSerial)) {
			parentMetadata->hasElementChild = true;
			if (parentMetadata->elementChildCount < std::numeric_limits<uint16_t>::max())
				++parentMetadata->elementChildCount;
			else {
				saturatingIncrement(st.doc.cssDiagnostics.contentMetadataClamps);
				parentMetadata->contentMetadataComplete = false;
			}
		} else {
			saturatingIncrement(st.doc.cssDiagnostics.contentMetadataClamps);
		}
	} else if (element.parentSerial != 0) {
		saturatingIncrement(st.doc.cssDiagnostics.siblingMetadataErrors);
		if (HtmlElementContentMetadata* parentMetadata = findContentMetadata(st, element.parentSerial))
			parentMetadata->contentMetadataComplete = false;
	}
	if (st.structuralElements.size() >= kCssLiteMaxStructuralMetadata) {
		saturatingIncrement(st.doc.cssDiagnostics.structuralMetadataClamps);
		saturatingIncrement(st.doc.cssDiagnostics.siblingMetadataClamps);
		if (HtmlElementContentMetadata* parentMetadata = findContentMetadata(st, element.parentSerial))
			parentMetadata->contentMetadataComplete = false;
		return element;
	}
	st.structuralElements.push_back(element);
	HtmlElementContentMetadata content;
	content.serial = element.serial;
	content.contentMetadataComplete = true;
	st.contentMetadata.push_back(content);
	st.structuralCounters.push_back({ element.serial, 0, {} });
	return element;
}

static HtmlElementRef activeBlockElement(const ParserState& st)
{
	for (const HtmlElementRef& element : st.openElements) {
		if (element.serial == st.activeBlockSerial) return element;
	}
	return {};
}

static std::vector<HtmlElementRef> captureBlockAncestors(const ParserState& st)
{
	std::vector<HtmlElementRef> ancestors;
	if (st.activeBlockSerial != 0) {
		for (const HtmlElementRef& element : st.openElements) {
			if (element.serial == st.activeBlockSerial) break;
			ancestors.push_back(element);
		}
		return ancestors;
	}
	ancestors = st.openElements;
	return ancestors;
}

static std::vector<HtmlElementRef> captureControlAncestors(const ParserState& st)
{
	// <input> is a void element and is registered without being pushed onto
	// openElements.  Preserve every represented wrapper here, including a
	// wrapping <label>, so descendant selectors and label association retain
	// their bounded structural path.
	return st.openElements;
}

static bool isInlineFlowBoundaryTag(const std::string& rawTag)
{
	const std::string tag = toLower(rawTag);
	return tag == "body" || tag == "p" || tag == "h1" || tag == "h2" ||
		tag == "h3" || tag == "li" || tag == "dt" || tag == "dd" ||
		tag == "pre" || tag == "td" || tag == "th" || tag == "caption" ||
		tag == "label";
}

static const HtmlElementRef* inlineOwnerElement(const ParserState& st)
{
	return st.openElements.empty() ? nullptr : &st.openElements.back();
}

static uint64_t inlineFlowSerial(const ParserState& st)
{
	for (auto it = st.openElements.rbegin(); it != st.openElements.rend(); ++it) {
		if (isInlineFlowBoundaryTag(it->tagName)) return it->serial;
	}
	for (auto it = st.openElements.rbegin(); it != st.openElements.rend(); ++it) {
		if (it->serial != 0) return it->serial;
	}
	return 0;
}

static void appendInlineItem(ParserState& st,
	InlineItemKind kind,
	const std::string& text,
	int blockIndex,
	uint64_t flowSerial,
	const HtmlElementRef* owner)
{
	if (st.doc.inlineItems.size() >= kCssLiteMaxInlineItems) {
		saturatingIncrement(st.doc.cssDiagnostics.contentMetadataClamps);
		return;
	}
	WebInlineItem item;
	item.kind = kind;
	item.flowSerial = flowSerial;
	item.ownerSerial = owner ? owner->serial : 0;
	item.parentSerial = owner ? owner->parentSerial : 0;
	item.blockIndex = blockIndex;
	if (text.size() > kCssLiteMaxInlineTextBytes) {
		item.text = text.substr(0, kCssLiteMaxInlineTextBytes);
		saturatingIncrement(st.doc.cssDiagnostics.contentMetadataClamps);
	} else {
		item.text = text;
	}
	st.doc.inlineItems.push_back(std::move(item));
}

static void markInlineFlowOnNewBlocks(ParserState& st,
	size_t firstBlock,
	uint64_t flowSerial)
{
	for (size_t i = firstBlock; i < st.doc.blocks.size(); ++i)
		st.doc.blocks[i].inlineFlowSerial = flowSerial;
}

static int inlineFlowAnchorBlock(ParserState& st, uint64_t flowSerial)
{
	if (flowSerial == 0) return -1;
	for (int i = 0; i < static_cast<int>(st.doc.blocks.size()); ++i) {
		if (st.doc.blocks[static_cast<size_t>(i)].inlineFlowSerial == flowSerial) return i;
	}
	return -1;
}

static uint64_t nearestAncestorSerial(const ParserState& st, const std::string& tagName)
{
	const std::string wanted = toLower(tagName);
	for (auto it = st.openElements.rbegin(); it != st.openElements.rend(); ++it) {
		if (toLower(it->tagName) == wanted && it->serial != 0) return it->serial;
	}
	return 0;
}

static bool disabledByFieldset(const ParserState& st)
{
	for (auto it = st.openElements.rbegin(); it != st.openElements.rend(); ++it) {
		if (toLower(it->tagName) != "fieldset") continue;
		return it->formControl.disabled;
	}
	return false;
}

static FormControlMetadata makeFormControlMetadata(const ParserState& st,
	const HtmlElementRef& element,
	FormControlType type,
	const std::string& inputType,
	bool supported)
{
	FormControlMetadata metadata;
	metadata.type = type;
	metadata.logicalSerial = element.serial;
	metadata.parentFormSerial = nearestAncestorSerial(st, "form");
	metadata.parentFieldsetSerial = nearestAncestorSerial(st, "fieldset");
	metadata.inputType = inputType;
	metadata.supported = supported;
	metadata.metadataComplete = element.serial != 0 && st.uncapturedOpenElementDepth == 0;
	metadata.disabled = disabledByFieldset(st);
	return metadata;
}

static void registerFormContainer(ParserState& st,
	const HtmlElementRef& element,
	const std::string& tagName)
{
	if (st.doc.formContainers.size() >= kFormMaxControls) {
		++st.doc.formsDiagnostics.controlMetadataClamps;
		return;
	}
	FormContainerMetadata container;
	container.tagName = toLower(tagName);
	container.className = element.className;
	container.id = element.id;
	container.inlineStyle = element.inlineStyle;
	container.serial = element.serial;
	container.parentSerial = element.parentSerial;
	container.metadataComplete = element.serial != 0;
	st.doc.formContainers.push_back(std::move(container));
}

static FormContainerMetadata* findFormContainer(ParserState& st, uint64_t serial)
{
	for (FormContainerMetadata& container : st.doc.formContainers) {
		if (container.serial == serial) return &container;
	}
	return nullptr;
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
	case OpenTag::Legend: return "legend";
	case OpenTag::Label: return "label";
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

static bool pushElement(ParserState& st, const HtmlElementRef& element)
{
	if (st.openElements.size() >= kCssLiteMaxOpenElementDepth) {
		markUncertainContent(st);
		if (st.uncapturedOpenElementDepth < kCssLiteMaxOpenElementDepth)
			++st.uncapturedOpenElementDepth;
		saturatingIncrement(st.doc.cssDiagnostics.structuralMetadataClamps);
		return false;
	}
	if (!element.inlineStyle.empty()) {
		++st.doc.cssDiagnostics.inlineStyleCount;
		st.doc.cssDiagnostics.cssDetected = true;
	}
	HtmlElementRef pushed = registerStructuralElement(st, element);
	st.openElements.push_back(std::move(pushed));
	return true;
}

static void activateCurrentBlock(ParserState& st)
{
	st.activeBlockSerial = st.openElements.empty() ? 0 : st.openElements.back().serial;
}

static void popElementByName(ParserState& st, const std::string& tagName)
{
	std::string target = toLower(tagName);
	if (st.uncapturedOpenElementDepth > 0) {
		// The bounded stack cannot retain the omitted tag names.  Consume one
		// matching-depth close conservatively and leave the represented prefix
		// intact so later sibling metadata cannot look valid by accident.
		--st.uncapturedOpenElementDepth;
		return;
	}
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
	const std::string rawDecoded = decodeEntities(st.textBuf);
	const uint64_t flowSerial = inlineFlowSerial(st);
	const HtmlElementRef* owner = inlineOwnerElement(st);
	std::string t;
	if (st.inPre || st.open == OpenTag::Textarea) {
		// Inside <pre>: decode entities but preserve whitespace/newlines.
		t = rawDecoded;
		if (st.inPre) {
			// Strip a single leading newline that immediately follows <pre>
			if (!t.empty() && t[0] == '\n') t = t.substr(1);
			// Strip a single trailing newline before </pre>
			if (!t.empty() && t.back() == '\n') t.pop_back();
		}
	} else {
		t = trim(collapseWs(decodeEntities(st.textBuf)));
	}
	if (st.open == OpenTag::Textarea && t.size() > kFormMaxValueBytes) {
		++st.doc.formsDiagnostics.controlTextTruncations;
		t.resize(kFormMaxValueBytes);
	}
	if (!t.empty()) {
		markContentForOpenElements(st, true,
			std::min(t.size(), kCssLiteMaxVisibleTextBytesPerElement),
			false, false, false, true);
	}
	st.textBuf.clear();
	if (t.empty()) {
		if (!rawDecoded.empty() && st.open != OpenTag::Textarea &&
			st.open != OpenTag::Option && st.open != OpenTag::Title &&
			st.open != OpenTag::Legend && st.open != OpenTag::Caption &&
			st.open != OpenTag::TableCell) {
			appendInlineItem(st, InlineItemKind::TextRun, rawDecoded, -1, flowSerial, owner);
		}
		return;
	}
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
	const HtmlElementRef elementMetadata = activeBlockElement(st);
	const size_t blockStart = st.doc.blocks.size();

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
			st.styleBuf,
			elementMetadata));
		break;
	case OpenTag::P:
		st.doc.blocks.push_back(makeTextBlock(BlockType::Paragraph, "p", t, "", st.classBuf, st.idBuf, ancestors, st.styleBuf, elementMetadata));
		break;
	case OpenTag::Li:
		st.doc.blocks.push_back(makeTextBlock(BlockType::ListItem, "li", t, "", st.classBuf, st.idBuf, ancestors, st.styleBuf, elementMetadata));
		break;
	case OpenTag::Dt:
		st.doc.blocks.push_back(makeTextBlock(BlockType::Paragraph, "dt", t, "", st.classBuf, st.idBuf, ancestors, st.styleBuf, elementMetadata));
		break;
	case OpenTag::Dd:
		st.doc.blocks.push_back(makeTextBlock(BlockType::Paragraph, "dd", t, "", st.classBuf, st.idBuf, ancestors, st.styleBuf, elementMetadata));
		break;
	case OpenTag::Figcaption:
		st.doc.blocks.push_back(makeTextBlock(BlockType::Paragraph, "figcaption", t, "", st.classBuf, st.idBuf, ancestors, st.styleBuf, elementMetadata));
		break;
	case OpenTag::Pre:
		st.doc.blocks.push_back(makeTextBlock(BlockType::Preformatted, "pre", t, "", st.classBuf, st.idBuf, ancestors, st.styleBuf, elementMetadata));
		break;
	case OpenTag::A:
		if (!st.hrefBuf.empty())
			st.doc.blocks.push_back(makeTextBlock(BlockType::Link, "a", t, st.hrefBuf, st.classBuf, st.idBuf, ancestors, st.styleBuf, elementMetadata));
		else
			st.doc.blocks.push_back(makeTextBlock(BlockType::Paragraph, "p", t, "", st.classBuf, st.idBuf, ancestors, st.styleBuf, elementMetadata));
		break;
	case OpenTag::Title:
		st.doc.title = t;
		break;
	case OpenTag::ButtonSubmit: {
		DocBlock block;
		block.type = BlockType::FormSubmit;
		block.tagName = "button";
		block.className = elementMetadata.className;
		block.id = elementMetadata.id;
		std::string buttonType = elementMetadata.formControl.inputType.empty() ? "submit" : elementMetadata.formControl.inputType;
		block.text = t.empty() ? (buttonType == "reset" ? "Reset" : (buttonType == "button" ? "Button" : "Submit")) : t;
		if (block.text.size() > kFormMaxLabelBytes) {
			++st.doc.formsDiagnostics.controlTextTruncations;
			block.text.resize(kFormMaxLabelBytes);
		}
		block.submitLabel = block.text;
		block.inputType = buttonType;
		block.formIndex = st.currentFormIndex;
		block.formAction = st.currentFormAction.empty() ? st.doc.url : st.currentFormAction;
		block.formMethod = st.currentFormMethod.empty() ? "get" : st.currentFormMethod;
		block.formEncoding = st.currentFormEncoding.empty() ? "application/x-www-form-urlencoded" : st.currentFormEncoding;
		block.formUnsupported = st.currentFormUnsupported;
		block.formControl = elementMetadata.formControl;
		block.formControl.type = buttonType == "button" ? FormControlType::Button :
			buttonType == "reset" ? FormControlType::Reset : FormControlType::Submit;
		block.formControl.logicalSerial = elementMetadata.serial;
		block.formControl.label = block.text;
		block.formControl.metadataComplete = elementMetadata.serial != 0 && st.uncapturedOpenElementDepth == 0;
		block.ancestors = ancestors;
		block.elementMetadata = elementMetadata;
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
		block.placeholder = st.currentTextareaPlaceholder;
		block.formUnsupported = st.currentFormUnsupported;
		block.formControl.type = FormControlType::Textarea;
		block.formControl.logicalSerial = elementMetadata.serial;
		block.formControl.parentFormSerial = nearestAncestorSerial(st, "form");
		block.formControl.parentFieldsetSerial = nearestAncestorSerial(st, "fieldset");
		block.formControl.inputType = "textarea";
		block.formControl.name = block.inputName;
		block.formControl.value = block.inputValue;
		block.formControl.placeholder = block.placeholder;
		block.formControl.supported = true;
		block.formControl.metadataComplete = elementMetadata.serial != 0 && st.uncapturedOpenElementDepth == 0;
		block.formControl.disabled = st.currentTextareaDisabled;
		block.formControl.required = st.currentTextareaRequired;
		block.formControl.readOnly = st.currentTextareaReadOnly;
		block.formControl.rows = block.visibleRows;
		block.formControl.cols = block.visibleCols;
		block.ancestors = ancestors;
		block.elementMetadata = elementMetadata;
		block.inlineStyle = st.styleBuf;
		st.doc.blocks.push_back(std::move(block));
		++st.doc.formsDiagnostics.textareaCount;
		break;
	}
	case OpenTag::Option: {
		if (st.currentSelectOptions.size() >= kFormMaxOptions) {
			++st.doc.formsDiagnostics.controlMetadataClamps;
			break;
		}
		if (t.size() > kFormMaxOptionTextBytes) {
			++st.doc.formsDiagnostics.controlTextTruncations;
			t.resize(kFormMaxOptionTextBytes);
		}
		FormOption option;
		option.text = t;
		option.value = st.currentOptionValue.empty() ? t : st.currentOptionValue;
		option.selected = st.currentOptionSelected;
		option.disabled = st.currentOptionDisabled;
		st.currentSelectOptions.push_back(std::move(option));
		break;
	}
	case OpenTag::Legend:
		if (st.currentLegendText.size() + t.size() > kFormMaxLabelBytes) {
			++st.doc.formsDiagnostics.controlTextTruncations;
			const size_t remaining = st.currentLegendText.size() < kFormMaxLabelBytes
				? kFormMaxLabelBytes - st.currentLegendText.size() : 0;
			st.currentLegendText += t.substr(0, remaining);
		} else {
			st.currentLegendText += t;
		}
		break;
	case OpenTag::Label: {
		DocBlock block;
		block.type = BlockType::FormLabel;
		block.tagName = "label";
		block.text = t;
		block.className = st.currentLabelClass;
		block.id = st.currentLabelId;
		block.labelFor = st.currentLabelFor;
		block.formIndex = st.currentFormIndex;
		block.formControl.type = FormControlType::None;
		block.formControl.metadataComplete = elementMetadata.serial != 0 && st.uncapturedOpenElementDepth == 0;
		block.ancestors = ancestors;
		block.elementMetadata = elementMetadata;
		block.inlineStyle = st.styleBuf;
		st.doc.blocks.push_back(std::move(block));
		break;
	}
	default:
		// Text outside a known block: emit as paragraph if body is active.
		if (st.bodyReached)
			st.doc.blocks.push_back(makeTextBlock(BlockType::Paragraph, "p", t, "", "", "", ancestors, st.styleBuf));
		break;
	}
	markInlineFlowOnNewBlocks(st, blockStart, flowSerial);
	if (st.open == OpenTag::ButtonSubmit || st.open == OpenTag::Textarea) {
		if (st.doc.blocks.size() > blockStart) {
			appendInlineItem(st, InlineItemKind::FormControl, {},
				static_cast<int>(blockStart), flowSerial, owner);
		}
	} else if (st.open != OpenTag::Option && st.open != OpenTag::Title &&
		st.open != OpenTag::Legend && st.open != OpenTag::Caption &&
		st.open != OpenTag::TableCell && !rawDecoded.empty()) {
		appendInlineItem(st, InlineItemKind::TextRun, rawDecoded,
			st.doc.blocks.size() > blockStart ? static_cast<int>(blockStart) : -1,
			flowSerial, owner);
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
		st.doc.hasBodyElement = pushElement(st, elementRef);
		if (st.doc.hasBodyElement && !st.openElements.empty())
			st.doc.bodyElement = st.openElements.back();
		return;
	}
	if (name == "html") {
		st.doc.hasDocumentElement = pushElement(st, elementRef);
		if (st.doc.hasDocumentElement && !st.openElements.empty())
			st.doc.documentElement = st.openElements.back();
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
		activateCurrentBlock(st);
		return;
	}

	if (name == "hr") {
		flushText(st);
		if (!st.bodyReached) return;
		const HtmlElementRef hrElement = registerStructuralElement(st, elementRef);
		markContentForOpenElements(st, false, 0, false, false, true, true);
		markContentMetadata(st, hrElement.serial, false, 0, false, false, true, true);
		DocBlock block;
		block.type = BlockType::Paragraph;
		block.tagName = "hr";
		block.className = extractAttr(tagBody, "class");
		block.id = extractAttr(tagBody, "id");
		block.inlineStyle = extractAttr(tagBody, "style");
		block.elementMetadata = hrElement;
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
		name == "tfoot" || name == "tr" || name == "noscript" || name == "form" || name == "fieldset") {
		if (name == "form") {
			flushText(st);
			st.inForm = true;
			st.currentFormIndex = st.doc.formsDiagnostics.formCount++;
			++st.doc.formsDiagnostics.htmlFormsParsed;
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
			const bool pushed = pushElement(st, elementRef);
			st.currentFormSerial = pushed && !st.openElements.empty() ? st.openElements.back().serial : 0;
			return;
		}
		if (name == "fieldset") {
			flushText(st);
			elementRef.formControl.type = FormControlType::None;
			elementRef.formControl.disabled = hasAttr(tagBody, "disabled");
			elementRef.formControl.metadataComplete = true;
			const bool pushed = pushElement(st, elementRef);
			if (pushed && !st.openElements.empty()) {
				const HtmlElementRef& fieldset = st.openElements.back();
				registerFormContainer(st, fieldset, "fieldset");
			}
			++st.doc.formsDiagnostics.htmlFieldsetsParsed;
			return;
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
		activateCurrentBlock(st);
		return;
	}

	if (name == "legend") {
		flushText(st);
		st.open = OpenTag::Legend;
		st.currentLegendText.clear();
		st.classBuf = extractAttr(tagBody, "class");
		st.idBuf = extractAttr(tagBody, "id");
		st.styleBuf = extractAttr(tagBody, "style");
		if (pushElement(st, elementRef) && !st.openElements.empty()) {
			registerFormContainer(st, st.openElements.back(), "legend");
			st.currentLegendSerial = st.openElements.back().serial;
			st.activeBlockSerial = st.currentLegendSerial;
		}
		return;
	}

	if (name == "label") {
		flushText(st);
		st.open = OpenTag::Label;
		st.currentLabelFor = boundedDecodedFormText(extractAttr(tagBody, "for"), kFormMaxLabelBytes, st.doc.formsDiagnostics);
		st.currentLabelClass = extractAttr(tagBody, "class");
		st.currentLabelId = extractAttr(tagBody, "id");
		st.classBuf = st.currentLabelClass;
		st.idBuf = st.currentLabelId;
		st.styleBuf = extractAttr(tagBody, "style");
		if (pushElement(st, elementRef) && !st.openElements.empty())
			st.activeBlockSerial = st.openElements.back().serial;
		++st.doc.formsDiagnostics.htmlLabelsParsed;
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
		activateCurrentBlock(st);
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
		const bool hidden = type == "hidden";
		const bool supported = type == "text" || type == "password" || type == "search" ||
			type == "email" || type == "url" || type == "number" || type == "checkbox" ||
			type == "radio" || type == "button" || type == "submit" || type == "reset";
		FormControlType controlType = FormControlType::Unsupported;
		if (type == "text") controlType = FormControlType::Text;
		else if (type == "password") controlType = FormControlType::Password;
		else if (type == "search") controlType = FormControlType::Search;
		else if (type == "email") controlType = FormControlType::Email;
		else if (type == "url") controlType = FormControlType::Url;
		else if (type == "number") controlType = FormControlType::Number;
		else if (type == "checkbox") controlType = FormControlType::Checkbox;
		else if (type == "radio") controlType = FormControlType::Radio;
		else if (type == "button") controlType = FormControlType::Button;
		else if (type == "submit") controlType = FormControlType::Submit;
		else if (type == "reset") controlType = FormControlType::Reset;

		const int parsedControls = st.doc.formsDiagnostics.textInputCount +
			st.doc.formsDiagnostics.checkboxCount + st.doc.formsDiagnostics.radioCount +
			st.doc.formsDiagnostics.textareaCount + st.doc.formsDiagnostics.selectCount +
			st.doc.formsDiagnostics.submitCount + st.doc.formsDiagnostics.htmlHiddenControls +
			st.doc.formsDiagnostics.unsupportedControlCount;
		if (parsedControls >= static_cast<int>(kFormMaxControls)) {
			++st.doc.formsDiagnostics.controlMetadataClamps;
			return;
		}

		elementRef.formControl.type = controlType;
		elementRef.formControl.inputType = type;
		elementRef.formControl.supported = supported;
		elementRef.formControl.metadataComplete = true;
		elementRef.formControl.disabled = hasAttr(tagBody, "disabled") || disabledByFieldset(st);
		elementRef.formControl.required = hasAttr(tagBody, "required");
		elementRef.formControl.readOnly = hasAttr(tagBody, "readonly");
		elementRef.formControl.hidden = hidden;
		elementRef.formControl.checked = (type == "checkbox" || type == "radio") && hasAttr(tagBody, "checked");
		elementRef.formControl.name = boundedDecodedFormText(extractAttr(tagBody, "name"), kFormMaxLabelBytes, st.doc.formsDiagnostics);
		elementRef.formControl.value = boundedDecodedFormText(extractAttr(tagBody, "value"), kFormMaxValueBytes, st.doc.formsDiagnostics);
		elementRef.formControl.placeholder = boundedDecodedFormText(extractAttr(tagBody, "placeholder"), kFormMaxPlaceholderBytes, st.doc.formsDiagnostics);
		elementRef.formControl.size = parseBoundedFormInt(tagBody, "size", kFormMaxSize, st.doc.formsDiagnostics);
		const HtmlElementRef inputElement = registerStructuralElement(st, elementRef);
		for (HtmlElementRef& stored : st.structuralElements) {
			if (stored.serial == inputElement.serial) stored.formControl.logicalSerial = inputElement.serial;
		}
		if (hidden) {
			markContentForOpenElements(st, false, 0, false, false, false, false);
			markContentMetadata(st, inputElement.serial, false, 0, false, false, false, false);
		} else {
			markContentForOpenElements(st, false, 0, true, false, true, true);
			markContentMetadata(st, inputElement.serial, false, 0, true, false, true, true);
		}
		++st.doc.formsDiagnostics.htmlInputsParsed;
		if (hidden) {
			++st.doc.formsDiagnostics.htmlHiddenControls;
			return;
		}
		if (type == "text" || type == "password" || type == "search" || type == "email" || type == "url" || type == "number" || !supported) {
			DocBlock block;
			block.type = BlockType::FormTextInput;
			block.tagName = "input";
			block.className = extractAttr(tagBody, "class");
			block.id = extractAttr(tagBody, "id");
			block.formIndex = st.currentFormIndex;
			block.formAction = st.currentFormAction.empty() ? st.doc.url : st.currentFormAction;
			block.formMethod = st.currentFormMethod.empty() ? "get" : st.currentFormMethod;
			block.formEncoding = st.currentFormEncoding.empty() ? "application/x-www-form-urlencoded" : st.currentFormEncoding;
			block.inputName = inputElement.formControl.name;
			block.inputValue = supported ? inputElement.formControl.value : std::string();
			block.inputType = type;
			block.visibleCols = inputElement.formControl.size;
			block.placeholder = inputElement.formControl.placeholder;
			block.formUnsupported = st.currentFormUnsupported;
			block.text = block.inputValue.empty() && !block.placeholder.empty() ? block.placeholder : block.inputValue;
			if (!supported) {
				block.inputType = "unsupported";
				block.placeholder = "[unsupported input]";
				++st.doc.formsDiagnostics.unsupportedControlCount;
				++st.doc.formsDiagnostics.formControlsUnsupported;
			}
			block.formControl = inputElement.formControl;
			block.formControl.logicalSerial = inputElement.serial;
			block.elementMetadata = inputElement;
			block.ancestors = captureControlAncestors(st);
			block.inlineStyle = extractAttr(tagBody, "style");
			if (!block.inlineStyle.empty()) {
				++st.doc.cssDiagnostics.inlineStyleCount;
				st.doc.cssDiagnostics.cssDetected = true;
			}
			const uint64_t flowSerial = inlineFlowSerial(st);
			block.inlineFlowSerial = flowSerial;
			const int blockIndex = static_cast<int>(st.doc.blocks.size());
			st.doc.blocks.push_back(std::move(block));
			appendInlineItem(st, InlineItemKind::FormControl, {}, blockIndex, flowSerial,
				findStructuralElement(st, inputElement.serial));
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
			block.inputName = inputElement.formControl.name;
			block.inputValue = inputElement.formControl.value;
			if (block.inputValue.empty()) block.inputValue = "on";
			block.inputType = type;
			block.checked = hasAttr(tagBody, "checked");
			block.text = block.inputName.empty() ? block.inputValue : block.inputName;
			block.formControl = inputElement.formControl;
			block.formControl.logicalSerial = inputElement.serial;
			block.formControl.checked = block.checked;
			block.elementMetadata = inputElement;
			block.formUnsupported = st.currentFormUnsupported;
			block.ancestors = captureControlAncestors(st);
			block.inlineStyle = extractAttr(tagBody, "style");
			if (!block.inlineStyle.empty()) {
				++st.doc.cssDiagnostics.inlineStyleCount;
				st.doc.cssDiagnostics.cssDetected = true;
			}
			const uint64_t flowSerial = inlineFlowSerial(st);
			block.inlineFlowSerial = flowSerial;
			const int blockIndex = static_cast<int>(st.doc.blocks.size());
			st.doc.blocks.push_back(std::move(block));
			appendInlineItem(st, InlineItemKind::FormControl, {}, blockIndex, flowSerial,
				findStructuralElement(st, inputElement.serial));
			if (type == "checkbox") ++st.doc.formsDiagnostics.checkboxCount;
			else ++st.doc.formsDiagnostics.radioCount;
		} else if (type == "button" || type == "submit" || type == "reset") {
			DocBlock block;
			block.type = BlockType::FormSubmit;
			block.tagName = "input";
			block.className = extractAttr(tagBody, "class");
			block.id = extractAttr(tagBody, "id");
			block.formIndex = st.currentFormIndex;
			block.formAction = st.currentFormAction.empty() ? st.doc.url : st.currentFormAction;
			block.formMethod = st.currentFormMethod.empty() ? "get" : st.currentFormMethod;
			block.formEncoding = st.currentFormEncoding.empty() ? "application/x-www-form-urlencoded" : st.currentFormEncoding;
			block.submitLabel = inputElement.formControl.value;
			if (block.submitLabel.empty()) block.submitLabel = type == "reset" ? "Reset" : (type == "button" ? "Button" : "Submit");
			block.text = block.submitLabel;
			block.inputType = type;
			block.formControl = inputElement.formControl;
			block.formControl.logicalSerial = inputElement.serial;
			block.elementMetadata = inputElement;
			block.formUnsupported = st.currentFormUnsupported;
			block.ancestors = captureControlAncestors(st);
			block.inlineStyle = extractAttr(tagBody, "style");
			if (!block.inlineStyle.empty()) {
				++st.doc.cssDiagnostics.inlineStyleCount;
				st.doc.cssDiagnostics.cssDetected = true;
			}
			const uint64_t flowSerial = inlineFlowSerial(st);
			block.inlineFlowSerial = flowSerial;
			const int blockIndex = static_cast<int>(st.doc.blocks.size());
			st.doc.blocks.push_back(std::move(block));
			appendInlineItem(st, InlineItemKind::FormControl, {}, blockIndex, flowSerial,
				findStructuralElement(st, inputElement.serial));
			++st.doc.formsDiagnostics.submitCount;
			++st.doc.formsDiagnostics.htmlButtonsParsed;
		} else {
			++st.doc.formsDiagnostics.unsupportedControlCount;
			++st.doc.formsDiagnostics.formControlsUnsupported;
		}
		return;
	}

	if (name == "textarea") {
		flushText(st);
		const int parsedControls = st.doc.formsDiagnostics.textInputCount +
			st.doc.formsDiagnostics.checkboxCount + st.doc.formsDiagnostics.radioCount +
			st.doc.formsDiagnostics.textareaCount + st.doc.formsDiagnostics.selectCount +
			st.doc.formsDiagnostics.submitCount + st.doc.formsDiagnostics.htmlHiddenControls +
			st.doc.formsDiagnostics.unsupportedControlCount;
		if (parsedControls >= static_cast<int>(kFormMaxControls)) {
			++st.doc.formsDiagnostics.controlMetadataClamps;
			return;
		}
		st.open = OpenTag::Textarea;
		st.currentTextareaName = boundedDecodedFormText(extractAttr(tagBody, "name"), kFormMaxLabelBytes, st.doc.formsDiagnostics);
		st.currentTextareaClass = extractAttr(tagBody, "class");
		st.currentTextareaId = extractAttr(tagBody, "id");
		st.currentTextareaPlaceholder = boundedDecodedFormText(extractAttr(tagBody, "placeholder"), kFormMaxPlaceholderBytes, st.doc.formsDiagnostics);
		st.currentTextareaDisabled = hasAttr(tagBody, "disabled") || disabledByFieldset(st);
		st.currentTextareaRequired = hasAttr(tagBody, "required");
		st.currentTextareaReadOnly = hasAttr(tagBody, "readonly");
		st.currentTextareaRows = parseBoundedFormInt(tagBody, "rows", kFormMaxRows, st.doc.formsDiagnostics);
		st.currentTextareaCols = parseBoundedFormInt(tagBody, "cols", kFormMaxCols, st.doc.formsDiagnostics);
		st.styleBuf = extractAttr(tagBody, "style");
		elementRef.formControl = makeFormControlMetadata(st, elementRef, FormControlType::Textarea, "textarea", true);
		elementRef.formControl.name = st.currentTextareaName;
		elementRef.formControl.placeholder = st.currentTextareaPlaceholder;
		elementRef.formControl.disabled = st.currentTextareaDisabled;
		elementRef.formControl.required = st.currentTextareaRequired;
		elementRef.formControl.readOnly = st.currentTextareaReadOnly;
		elementRef.formControl.rows = st.currentTextareaRows;
		elementRef.formControl.cols = st.currentTextareaCols;
		pushElement(st, elementRef);
		++st.doc.formsDiagnostics.htmlTextareasParsed;
		activateCurrentBlock(st);
		st.textBuf.clear();
		return;
	}

	if (name == "select") {
		flushText(st);
		const int parsedControls = st.doc.formsDiagnostics.textInputCount +
			st.doc.formsDiagnostics.checkboxCount + st.doc.formsDiagnostics.radioCount +
			st.doc.formsDiagnostics.textareaCount + st.doc.formsDiagnostics.selectCount +
			st.doc.formsDiagnostics.submitCount + st.doc.formsDiagnostics.htmlHiddenControls +
			st.doc.formsDiagnostics.unsupportedControlCount;
		if (parsedControls >= static_cast<int>(kFormMaxControls)) {
			++st.doc.formsDiagnostics.controlMetadataClamps;
			return;
		}
		st.inSelect = true;
		st.currentSelectName = boundedDecodedFormText(extractAttr(tagBody, "name"), kFormMaxLabelBytes, st.doc.formsDiagnostics);
		st.currentSelectClass = extractAttr(tagBody, "class");
		st.currentSelectId = extractAttr(tagBody, "id");
		st.currentSelectInputType = "select";
		st.currentSelectDisabled = hasAttr(tagBody, "disabled") || disabledByFieldset(st);
		st.currentSelectRequired = hasAttr(tagBody, "required");
		st.currentSelectMultiple = hasAttr(tagBody, "multiple");
		st.currentSelectSize = parseBoundedFormInt(tagBody, "size", kFormMaxSize, st.doc.formsDiagnostics);
		st.currentSelectOptions.clear();
		st.styleBuf = extractAttr(tagBody, "style");
		elementRef.formControl = makeFormControlMetadata(st, elementRef, FormControlType::Select, "select", true);
		elementRef.formControl.name = st.currentSelectName;
		elementRef.formControl.disabled = st.currentSelectDisabled;
		elementRef.formControl.required = st.currentSelectRequired;
		elementRef.formControl.multiple = st.currentSelectMultiple;
		elementRef.formControl.size = st.currentSelectSize;
		if (pushElement(st, elementRef) && !st.openElements.empty())
			st.currentSelectSerial = st.openElements.back().serial;
		++st.doc.formsDiagnostics.htmlSelectsParsed;
		activateCurrentBlock(st);
		st.open = OpenTag::None;
		return;
	}

	if (name == "option" && st.inSelect) {
		flushText(st);
		st.open = OpenTag::Option;
		st.currentOptionValue = boundedDecodedFormText(extractAttr(tagBody, "value"), kFormMaxOptionTextBytes, st.doc.formsDiagnostics);
		st.currentOptionSelected = hasAttr(tagBody, "selected");
		st.currentOptionDisabled = hasAttr(tagBody, "disabled") || st.currentSelectDisabled;
		st.styleBuf = extractAttr(tagBody, "style");
		elementRef.formControl = makeFormControlMetadata(st, elementRef, FormControlType::Option, "option", true);
		elementRef.formControl.selected = st.currentOptionSelected;
		elementRef.formControl.checked = st.currentOptionSelected;
		elementRef.formControl.disabled = st.currentOptionDisabled;
		pushElement(st, elementRef);
		++st.doc.formsDiagnostics.htmlOptionsParsed;
		activateCurrentBlock(st);
		st.textBuf.clear();
		return;
	}

	// <br> – inside <pre> append a newline to the buffer; outside flush as a
	// line break only if there is pending text (avoids empty Paragraph blocks).
	if (name == "br") {
		++st.doc.cssDiagnostics.lineBreakCount;
		markContentForOpenElements(st, false, 0, false, true, false, true);
		if (st.inPre) {
			st.textBuf += '\n';
		} else if (st.open == OpenTag::TableCell) {
			flushText(st);
			st.currentTableCellText += ' ';
		} else {
			flushText(st);
			const uint64_t flowSerial = inlineFlowSerial(st);
			int anchorBlock = inlineFlowAnchorBlock(st, flowSerial);
			if (anchorBlock < 0 && flowSerial != 0 && st.bodyReached) {
				const size_t blockStart = st.doc.blocks.size();
				st.doc.blocks.push_back(makeTextBlock(BlockType::Paragraph, "p", "", "", st.classBuf,
					st.idBuf, captureBlockAncestors(st), st.styleBuf, activeBlockElement(st)));
				markInlineFlowOnNewBlocks(st, blockStart, flowSerial);
				anchorBlock = static_cast<int>(blockStart);
			}
			appendInlineItem(st, InlineItemKind::ForcedBreak, {}, anchorBlock,
				flowSerial, inlineOwnerElement(st));
		}
		return;
	}

	if (name == "img") {
		flushText(st);
		std::string src = trim(decodeEntities(extractAttr(tagBody, "src")));
		const HtmlElementRef imageElement = registerStructuralElement(st, elementRef);
		markContentForOpenElements(st, false, 0, true, false, true, true);
		markContentMetadata(st, imageElement.serial, false, 0, true, false, true, true);
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
		block.elementMetadata = imageElement;
		block.ancestors = captureBlockAncestors(st);
		const uint64_t flowSerial = inlineFlowSerial(st);
		block.inlineFlowSerial = flowSerial;
		const int blockIndex = static_cast<int>(st.doc.blocks.size());
		st.doc.blocks.push_back(std::move(block));
		appendInlineItem(st, InlineItemKind::ReplacedImage, {}, blockIndex, flowSerial,
			findStructuralElement(st, imageElement.serial));
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

	if (name == "h1")    { st.open = OpenTag::H1;    pushElement(st, elementRef); activateCurrentBlock(st); return; }
	if (name == "h2")    { st.open = OpenTag::H2;    pushElement(st, elementRef); activateCurrentBlock(st); return; }
	if (name == "h3")    { st.open = OpenTag::H3;    pushElement(st, elementRef); activateCurrentBlock(st); return; }
	if (name == "p")     { st.open = OpenTag::P;     pushElement(st, elementRef); activateCurrentBlock(st); return; }
	if (name == "li")    { st.open = OpenTag::Li;    pushElement(st, elementRef); activateCurrentBlock(st); return; }
	if (name == "title") { st.open = OpenTag::Title; pushElement(st, elementRef); activateCurrentBlock(st); return; }

	if (name == "pre") {
		st.open  = OpenTag::Pre;
		st.inPre = true;
		pushElement(st, elementRef);
		activateCurrentBlock(st);
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
			elementRef.hasLinkTarget = true;
			elementRef.visited = st.visitedUrls && st.visitedUrls->find(st.hrefBuf) != st.visitedUrls->end();
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
		if (st.open == OpenTag::A) activateCurrentBlock(st);
		return;
	}

	if (name == "button") {
		std::string type = toLower(trim(extractAttr(tagBody, "type")));
		if (type.empty()) type = "submit";
		if (type == "submit" || type == "button" || type == "reset") {
			elementRef.formControl = makeFormControlMetadata(st, elementRef,
				type == "button" ? FormControlType::Button : (type == "reset" ? FormControlType::Reset : FormControlType::Submit),
				type, true);
			elementRef.formControl.disabled = hasAttr(tagBody, "disabled") || disabledByFieldset(st);
			st.open = OpenTag::ButtonSubmit;
			if (pushElement(st, elementRef)) ++st.doc.formsDiagnostics.htmlButtonsParsed;
			activateCurrentBlock(st);
		} else {
			++st.doc.formsDiagnostics.unsupportedControlCount;
		}
		return;
	}

	// Unknown open tag: leave current open context unchanged so text inside
	// unknown tags flows into the current block.  Its ownership is uncertain,
	// so :empty must fail closed for the enclosing logical elements.
	markUncertainContent(st);
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
		name == "button" || name == "textarea" || name == "option" || name == "legend" || name == "label") {
		const uint64_t closingSerial = st.activeBlockSerial;
		flushText(st);
		if (name == "legend") {
			if (FormContainerMetadata* container = findFormContainer(st, closingSerial)) {
				container->legendText = st.currentLegendText;
				if (FormContainerMetadata* fieldset = findFormContainer(st, container->parentSerial))
					fieldset->legendText = st.currentLegendText;
			}
		}
		popElementByName(st, name);
		st.open    = OpenTag::None;
		st.activeBlockSerial = 0;
		st.hrefBuf.clear();
		st.classBuf.clear();
		st.idBuf.clear();
		st.styleBuf.clear();
		if (name == "textarea") {
			st.currentTextareaName.clear();
			st.currentTextareaClass.clear();
			st.currentTextareaId.clear();
			st.currentTextareaPlaceholder.clear();
			st.currentTextareaDisabled = false;
			st.currentTextareaRequired = false;
			st.currentTextareaReadOnly = false;
		}
		if (name == "option") {
			st.currentOptionValue.clear();
			st.currentOptionSelected = false;
			st.currentOptionDisabled = false;
		}
		if (name == "legend") {
			st.currentLegendText.clear();
			st.currentLegendSerial = 0;
		}
		if (name == "label") {
			st.currentLabelFor.clear();
			st.currentLabelClass.clear();
			st.currentLabelId.clear();
		}
	}
	if (name == "a") {
		if (st.open != OpenTag::TableCell) {
			flushText(st);
			popElementByName(st, name);
			st.open = OpenTag::None;
			st.activeBlockSerial = 0;
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
			block.elementMetadata = activeBlockElement(st);
			st.doc.blocks.push_back(std::move(block));
		}
		st.currentTableCaptionText.clear();
		st.open = OpenTag::None;
		st.activeBlockSerial = 0;
		popElementByName(st, name);
		st.classBuf.clear();
		st.idBuf.clear();
		st.styleBuf.clear();
	}
	if (name == "td" || name == "th") {
		flushText(st);
		DocBlock block = makeTextBlock(BlockType::Paragraph, name, st.currentTableCellText, "",
			st.classBuf, st.idBuf, captureBlockAncestors(st), st.styleBuf);
		block.elementMetadata = activeBlockElement(st);
		if (!st.currentTableCellHref.empty()) {
			block.url = st.currentTableCellHref;
		}
		st.doc.blocks.push_back(std::move(block));
		st.currentTableCellText.clear();
		st.currentTableCellHeader = false;
		st.currentTableCellHref.clear();
		st.open = OpenTag::None;
		st.activeBlockSerial = 0;
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
		block.visibleRows = st.currentSelectSize;
		block.visibleCols = 0;
		block.formUnsupported = st.currentFormUnsupported;
		for (int i = 0; i < static_cast<int>(block.options.size()); ++i) {
			if (block.options[i].selected) {
				block.selectedOption = i;
				break;
			}
		}
		if (block.selectedOption < 0) {
			for (int i = 0; i < static_cast<int>(block.options.size()); ++i) {
				if (!block.options[static_cast<size_t>(i)].disabled) {
					block.selectedOption = i;
					break;
				}
			}
		}
		if (block.selectedOption >= 0 && block.selectedOption < static_cast<int>(block.options.size())) {
			block.inputValue = block.options[static_cast<size_t>(block.selectedOption)].value;
			block.text = block.options[static_cast<size_t>(block.selectedOption)].text;
		}
		block.ancestors = st.openElements;
		if (!block.ancestors.empty()) block.ancestors.pop_back();
		block.elementMetadata = {};
		if (const HtmlElementRef* selectElement = findStructuralElement(st, st.currentSelectSerial))
			block.elementMetadata = *selectElement;
		block.inlineStyle = st.styleBuf;
		block.formControl.type = FormControlType::Select;
		block.formControl.logicalSerial = block.elementMetadata.serial;
		block.formControl.parentFormSerial = nearestAncestorSerial(st, "form");
		block.formControl.parentFieldsetSerial = nearestAncestorSerial(st, "fieldset");
		block.formControl.inputType = "select";
		block.formControl.name = block.inputName;
		block.formControl.supported = true;
		block.formControl.metadataComplete = block.elementMetadata.serial != 0 && st.uncapturedOpenElementDepth == 0;
		block.formControl.disabled = st.currentSelectDisabled;
		block.formControl.required = st.currentSelectRequired;
		block.formControl.multiple = st.currentSelectMultiple;
		block.formControl.size = st.currentSelectSize;
		block.formControl.optionCount = static_cast<int>(block.options.size());
		block.formControl.selectedOptionIndex = block.selectedOption;
		const uint64_t flowSerial = inlineFlowSerial(st);
		block.inlineFlowSerial = flowSerial;
		const int blockIndex = static_cast<int>(st.doc.blocks.size());
		st.doc.blocks.push_back(std::move(block));
		appendInlineItem(st, InlineItemKind::FormControl, {}, blockIndex, flowSerial,
			findStructuralElement(st, st.currentSelectSerial));
		++st.doc.formsDiagnostics.selectCount;
		st.inSelect = false;
		st.currentSelectName.clear();
		st.currentSelectClass.clear();
		st.currentSelectId.clear();
		st.currentSelectInputType.clear();
		st.currentSelectDisabled = false;
		st.currentSelectRequired = false;
		st.currentSelectMultiple = false;
		st.currentSelectSize = 0;
		st.currentSelectSerial = 0;
		st.currentSelectOptions.clear();
		st.styleBuf.clear();
		popElementByName(st, name);
		st.open = OpenTag::None;
		st.activeBlockSerial = 0;
	}
	if (name == "form") {
		flushText(st);
		st.inForm = false;
		st.currentFormIndex = -1;
		st.currentFormAction.clear();
		st.currentFormMethod.clear();
		st.currentFormEncoding.clear();
		st.currentFormUnsupported = false;
		st.currentFormSerial = 0;
		popElementByName(st, name);
	}
	if (name == "fieldset") {
		flushText(st);
		popElementByName(st, name);
	}
	if (name == "pre") {
		flushText(st);
		st.open  = OpenTag::None;
		st.activeBlockSerial = 0;
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

static void finalizeStructuralMetadata(ParserState& st)
{
	// Any parser state still open at EOF has uncertain ownership.  Preserve
	// readable blocks, but make content-sensitive selectors fail closed.
	for (const HtmlElementRef& element : st.openElements) {
		if (HtmlElementContentMetadata* metadata = findContentMetadata(st, element.serial))
			metadata->contentMetadataComplete = false;
	}
	if (st.uncapturedOpenElementDepth > 0) {
		for (const HtmlElementRef& element : st.openElements) {
			if (HtmlElementContentMetadata* metadata = findContentMetadata(st, element.serial))
				metadata->contentMetadataComplete = false;
		}
	}
	for (HtmlElementRef& element : st.structuralElements) {
		if (StructuralChildCounter* counter = findStructuralCounter(st, element.serial)) {
			element.childCount = counter->childCount;
		}
		if (StructuralChildCounter* parent = findStructuralCounter(st, element.parentSerial)) {
			element.siblingCount = parent->childCount;
			for (const auto& type : parent->typeCounts) {
				if (type.first == element.tagName) {
					element.typeCount = type.second;
					break;
				}
			}
		}
	}
	auto updateRef = [&](HtmlElementRef& ref) {
		if (HtmlElementRef* stored = findStructuralElement(st, ref.serial)) ref = *stored;
	};
	updateRef(st.doc.documentElement);
	updateRef(st.doc.bodyElement);
	for (DocBlock& block : st.doc.blocks) {
		updateRef(block.elementMetadata);
		for (HtmlElementRef& ancestor : block.ancestors) updateRef(ancestor);
	}
	st.doc.structuralElements = std::move(st.structuralElements);
	st.doc.contentMetadata = std::move(st.contentMetadata);
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

void recomputeDocumentStyles(WebDocument& document)
{
	// Evidence is a snapshot of the current computed state.  Drop the prior
	// bounded snapshot before recomputing so runtime :checked changes are
	// observable without retaining an unbounded history.
	document.cssDiagnostics.computedStyleEvidence.clear();
	document.cssDiagnostics.computedStyleEvidenceSerials.clear();
	applyDocumentStyles(document);
}

// ---------------------------------------------------------------------------
// parseHtml
// ---------------------------------------------------------------------------
WebDocument parseHtml(const std::string& pageUrl,
	const std::string& htmlText,
	const std::unordered_set<std::string>& visitedUrls)
{
	ParserState st;
	st.doc.url = pageUrl;
	st.visitedUrls = &visitedUrls;

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
	finalizeStructuralMetadata(st);
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
