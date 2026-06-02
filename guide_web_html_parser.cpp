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
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <utility>

namespace gxos {
namespace web {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

	constexpr size_t kCssLiteMaxStyleBytes = 16u * 1024u;

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

static int parsePositiveIntAttr(const std::string& tagBody, const std::string& attr)
{
	std::string value = trim(extractAttr(tagBody, attr));
	if (value.empty()) return 0;
	int result = 0;
	for (char c : value) {
		if (c < '0' || c > '9') break;
		result = result * 10 + (c - '0');
		if (result > 4096) return 4096;
	}
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
	return false;
}

static int parseCssPixelValue(const std::string& rawValue, bool& ok)
{
	std::string value = toLower(trim(rawValue));
	ok = false;
	if (value.empty()) return 0;
	if (value.size() > 2 && value.substr(value.size() - 2) == "px") {
		value = trim(value.substr(0, value.size() - 2));
	}
	if (value.empty()) return 0;
	int sign = 1;
	size_t pos = 0;
	if (value[pos] == '-') { sign = -1; ++pos; }
	else if (value[pos] == '+') { ++pos; }
	if (pos >= value.size() || !std::isdigit(static_cast<unsigned char>(value[pos]))) return 0;
	int result = 0;
	while (pos < value.size() && std::isdigit(static_cast<unsigned char>(value[pos]))) {
		result = result * 10 + (value[pos] - '0');
		if (result > 4096) result = 4096;
		++pos;
	}
	if (pos != value.size()) return 0;
	ok = true;
	result *= sign;
	if (result < 0) result = 0;
	return result;
}

static bool isSupportedSelectorName(const std::string& selector)
{
	return selector == "body" || selector == "h1" || selector == "h2" || selector == "h3" ||
		selector == "p" || selector == "a" || selector == "li" || selector == "pre" ||
		selector == "code" || selector == "img";
}

static bool parseCssSelector(const std::string& rawSelector, WebStyleRule& outRule)
{
	std::string selector = toLower(trim(rawSelector));
	if (selector.empty()) return false;
	if (selector[0] == '.') {
		selector = trim(selector.substr(1));
		if (selector.empty()) return false;
		outRule.selectorType = StyleSelectorType::Class;
		outRule.selector = selector;
		return true;
	}
	if (selector[0] == '#') {
		selector = trim(selector.substr(1));
		if (selector.empty()) return false;
		outRule.selectorType = StyleSelectorType::Id;
		outRule.selector = selector;
		return true;
	}
	if (!isSupportedSelectorName(selector)) return false;
	outRule.selectorType = StyleSelectorType::Element;
	outRule.selector = selector;
	return true;
}

static void applyStyleDeclaration(WebStyle& style,
	const std::string& property,
	const std::string& value,
	CssDiagnostics& diag)
{
	std::string prop = toLower(trim(property));
	std::string val = trim(value);
	if (prop.empty() || val.empty()) return;

	uint32_t color = 0;
	bool parsedInt = false;
	if (prop == "color") {
		if (parseCssColor(val, color)) {
			style.hasColor = true;
			style.color = color;
		} else {
			++diag.unsupportedDeclarationCount;
		}
		return;
	}
	if (prop == "background-color") {
		if (parseCssColor(val, color)) {
			style.hasBackgroundColor = true;
			style.backgroundColor = color;
		} else {
			++diag.unsupportedDeclarationCount;
		}
		return;
	}
	if (prop == "font-weight") {
		style.bold = (toLower(val) == "bold");
		if (!style.bold && toLower(val) != "normal") ++diag.unsupportedDeclarationCount;
		return;
	}
	if (prop == "text-decoration") {
		std::string lower = toLower(val);
		style.underline = (lower.find("underline") != std::string::npos);
		if (!style.underline && lower != "none") ++diag.unsupportedDeclarationCount;
		return;
	}
	if (prop == "margin") {
		int px = parseCssPixelValue(val, parsedInt);
		if (parsedInt) {
			style.marginTop = px;
			style.marginBottom = px;
			style.marginLeft = px;
		} else {
			++diag.unsupportedDeclarationCount;
		}
		return;
	}
	if (prop == "margin-top") {
		style.marginTop = parseCssPixelValue(val, parsedInt);
		if (!parsedInt) ++diag.unsupportedDeclarationCount;
		return;
	}
	if (prop == "margin-bottom") {
		style.marginBottom = parseCssPixelValue(val, parsedInt);
		if (!parsedInt) ++diag.unsupportedDeclarationCount;
		return;
	}
	if (prop == "margin-left") {
		style.marginLeft = parseCssPixelValue(val, parsedInt);
		if (!parsedInt) ++diag.unsupportedDeclarationCount;
		return;
	}
	if (prop == "padding") {
		style.padding = parseCssPixelValue(val, parsedInt);
		if (!parsedInt) ++diag.unsupportedDeclarationCount;
		return;
	}
	if (prop == "font-size") {
		style.fontScaleOrSize = parseCssPixelValue(val, parsedInt);
		if (!parsedInt) ++diag.unsupportedDeclarationCount;
		return;
	}
	++diag.unsupportedDeclarationCount;
}

static void parseCssDeclarations(const std::string& body, WebStyle& style, CssDiagnostics& diag)
{
	size_t cursor = 0;
	while (cursor < body.size()) {
		size_t semi = body.find(';', cursor);
		std::string decl = body.substr(cursor, semi == std::string::npos ? std::string::npos : semi - cursor);
		size_t colon = decl.find(':');
		if (colon != std::string::npos) {
			applyStyleDeclaration(style, decl.substr(0, colon), decl.substr(colon + 1), diag);
		}
		cursor = semi == std::string::npos ? body.size() : semi + 1;
	}
}

static std::string stripCssComments(const std::string& css)
{
	std::string out;
	out.reserve(css.size());
	for (size_t i = 0; i < css.size(); ++i) {
		if (i + 1 < css.size() && css[i] == '/' && css[i + 1] == '*') {
			i += 2;
			while (i + 1 < css.size() && !(css[i] == '*' && css[i + 1] == '/')) ++i;
			if (i + 1 < css.size()) ++i;
			continue;
		}
		out += css[i];
	}
	return out;
}

static void parseEmbeddedCss(WebDocument& doc, const std::string& cssText)
{
	if (cssText.empty()) return;
	doc.cssDiagnostics.cssDetected = true;
	std::string css = cssText;
	if (css.size() > kCssLiteMaxStyleBytes) {
		css.resize(kCssLiteMaxStyleBytes);
		doc.cssDiagnostics.styleBlockCapped = true;
	}
	doc.cssDiagnostics.styleBytesProcessed += css.size();
	css = stripCssComments(css);

	size_t cursor = 0;
	while (cursor < css.size()) {
		size_t brace = css.find('{', cursor);
		if (brace == std::string::npos) break;
		size_t endBrace = css.find('}', brace + 1);
		if (endBrace == std::string::npos) break;
		std::string selectorText = trim(css.substr(cursor, brace - cursor));
		std::string bodyText = css.substr(brace + 1, endBrace - brace - 1);
		cursor = endBrace + 1;

		std::stringstream selectors(selectorText);
		std::string selector;
		while (std::getline(selectors, selector, ',')) {
			WebStyleRule rule;
			if (!parseCssSelector(selector, rule)) {
				++doc.cssDiagnostics.unsupportedDeclarationCount;
				continue;
			}
			parseCssDeclarations(bodyText, rule.style, doc.cssDiagnostics);
			if (rule.selectorType == StyleSelectorType::Element && rule.selector == "body") {
				doc.bodyStyle = rule.style;
			}
			doc.styleRules.push_back(rule);
			++doc.cssDiagnostics.styleRuleCount;
		}
	}
}

static int mergeStyleInt(int baseValue, int overrideValue)
{
	return overrideValue >= 0 ? overrideValue : baseValue;
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
	merged.underline = overrideStyle.underline ? true : merged.underline;
	merged.marginTop = mergeStyleInt(merged.marginTop, overrideStyle.marginTop);
	merged.marginBottom = mergeStyleInt(merged.marginBottom, overrideStyle.marginBottom);
	merged.marginLeft = mergeStyleInt(merged.marginLeft, overrideStyle.marginLeft);
	merged.padding = mergeStyleInt(merged.padding, overrideStyle.padding);
	merged.fontScaleOrSize = mergeStyleInt(merged.fontScaleOrSize, overrideStyle.fontScaleOrSize);
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
		style.marginTop = 10;
		style.marginBottom = 10;
		style.fontScaleOrSize = 24;
		return style;
	}
	if (tagName == "h2") {
		style.bold = true;
		style.marginTop = 8;
		style.marginBottom = 8;
		style.fontScaleOrSize = 20;
		return style;
	}
	if (tagName == "h3") {
		style.bold = true;
		style.marginTop = 6;
		style.marginBottom = 6;
		style.fontScaleOrSize = 18;
		return style;
	}
	if (tagName == "p") {
		style.marginTop = 4;
		style.marginBottom = 8;
		return style;
	}
	if (tagName == "a") {
		style.hasColor = true;
		style.color = 0xFF1E5CB8u;
		style.underline = true;
		style.marginTop = 4;
		style.marginBottom = 6;
		return style;
	}
	if (tagName == "li") {
		style.marginTop = 2;
		style.marginBottom = 4;
		style.marginLeft = 12;
		return style;
	}
	if (tagName == "pre" || tagName == "code") {
		style.hasBackgroundColor = true;
		style.backgroundColor = 0xFFE6E8EEu;
		style.marginTop = 6;
		style.marginBottom = 8;
		style.padding = 4;
		return style;
	}
	if (tagName == "img") {
		style.marginTop = 6;
		style.marginBottom = 6;
		return style;
	}
	return style;
}

static bool blockMatchesRule(const DocBlock& block, const WebStyleRule& rule)
{
	if (rule.selectorType == StyleSelectorType::Element) {
		return toLower(block.tagName) == rule.selector;
	}
	if (rule.selectorType == StyleSelectorType::Class) {
		std::stringstream classes(toLower(block.className));
		std::string className;
		while (classes >> className) {
			if (className == rule.selector) return true;
		}
		return false;
	}
	if (rule.selectorType == StyleSelectorType::Id) {
		return toLower(block.id) == rule.selector;
	}
	return false;
}

static void applyDocumentStyles(WebDocument& doc)
{
	for (DocBlock& block : doc.blocks) {
		WebStyle style = doc.bodyStyle;
		style = mergeStyles(style, defaultStyleForTag(block.tagName));
		for (const WebStyleRule& rule : doc.styleRules) {
			if (rule.selectorType == StyleSelectorType::Element && rule.selector == "body") continue;
			if (blockMatchesRule(block, rule)) {
				style = mergeStyles(style, rule.style);
			}
		}
		block.style = style;
	}
}

static DocBlock makeTextBlock(BlockType type,
	const std::string& tagName,
	const std::string& text,
	const std::string& url,
	const std::string& className,
	const std::string& id)
{
	DocBlock block;
	block.type = type;
	block.tagName = tagName;
	block.className = className;
	block.id = id;
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
	Title,
	Pre,   // <pre> block — whitespace preserved
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
};

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

	switch (st.open) {
	case OpenTag::H1:
	case OpenTag::H2:
	case OpenTag::H3:
		st.doc.blocks.push_back(makeTextBlock(BlockType::Heading,
			st.open == OpenTag::H1 ? "h1" : (st.open == OpenTag::H2 ? "h2" : "h3"),
			t,
			"",
			st.classBuf,
			st.idBuf));
		break;
	case OpenTag::P:
		st.doc.blocks.push_back(makeTextBlock(BlockType::Paragraph, "p", t, "", st.classBuf, st.idBuf));
		break;
	case OpenTag::Li:
		st.doc.blocks.push_back(makeTextBlock(BlockType::ListItem, "li", t, "", st.classBuf, st.idBuf));
		break;
	case OpenTag::Pre:
		st.doc.blocks.push_back(makeTextBlock(BlockType::Preformatted, "pre", t, "", st.classBuf, st.idBuf));
		break;
	case OpenTag::A:
		if (!st.hrefBuf.empty())
			st.doc.blocks.push_back(makeTextBlock(BlockType::Link, "a", t, st.hrefBuf, st.classBuf, st.idBuf));
		else
			st.doc.blocks.push_back(makeTextBlock(BlockType::Paragraph, "p", t, "", st.classBuf, st.idBuf));
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
			st.doc.blocks.push_back(makeTextBlock(BlockType::Paragraph, "p", t, "", "", ""));
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

	// Handle skip-content blocks first.
	if (name == "script") { flushText(st); st.inScript = true; return; }
	if (name == "style")  { flushText(st); st.inStyle  = true; return; }
	if (name == "link") {
		std::string rel = toLower(trim(extractAttr(tagBody, "rel")));
		if (rel == "stylesheet") ++st.doc.cssDiagnostics.unsupportedExternalStylesheetCount;
		return;
	}

	if (st.inScript) return;
	if (st.inStyle) return;

	// Mark body reached.
	if (name == "body") { st.bodyReached = true; return; }

	// Void / structural tags with no direct content effect.
	if (name == "html" || name == "head" || name == "ul" || name == "ol" ||
		name == "div" || name == "span" || name == "section" || name == "article" ||
		name == "header" || name == "footer" || name == "nav" || name == "main" ||
		name == "table" || name == "tr" || name == "td" || name == "th")
		return;

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
		st.open = OpenTag::None;
		return;
	}

	if (name == "option" && st.inSelect) {
		flushText(st);
		st.open = OpenTag::Option;
		st.currentOptionValue = decodeEntities(extractAttr(tagBody, "value"));
		st.currentOptionSelected = hasAttr(tagBody, "selected");
		st.textBuf.clear();
		return;
	}

	// <br> – inside <pre> append a newline to the buffer; outside flush as a
	// line break only if there is pending text (avoids empty Paragraph blocks).
	if (name == "br") {
		if (st.inPre) {
			st.textBuf += '\n';
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
		block.width = parsePositiveIntAttr(tagBody, "width");
		block.height = parsePositiveIntAttr(tagBody, "height");
		st.doc.blocks.push_back(std::move(block));
		st.open = OpenTag::None;
		st.hrefBuf.clear();
		st.classBuf.clear();
		st.idBuf.clear();
		return;
	}

	// Block-level tags: flush any pending text, then open new context.
	flushText(st);

	st.classBuf = extractAttr(tagBody, "class");
	st.idBuf = extractAttr(tagBody, "id");

	if (name == "h1")    { st.open = OpenTag::H1;    return; }
	if (name == "h2")    { st.open = OpenTag::H2;    return; }
	if (name == "h3")    { st.open = OpenTag::H3;    return; }
	if (name == "p")     { st.open = OpenTag::P;     return; }
	if (name == "li")    { st.open = OpenTag::Li;    return; }
	if (name == "title") { st.open = OpenTag::Title; return; }

	if (name == "pre") {
		st.open  = OpenTag::Pre;
		st.inPre = true;
		return;
	}
	// <code>: if a <pre> is already open, stay in it; otherwise treat as plain text.
	if (name == "code") {
		if (!st.inPre) { /* leave current context; code text flows through */ }
		return;
	}

	if (name == "a") {
		std::string href = extractAttr(tagBody, "href");
		if (!href.empty()) {
			// Resolve relative URL against the document base.
			st.hrefBuf = resolveRelativeUrl(st.doc.url, href);
		} else {
			st.hrefBuf.clear();
		}
		st.open = OpenTag::A;
		return;
	}

	if (name == "button") {
		std::string type = toLower(trim(extractAttr(tagBody, "type")));
		if (type.empty() || type == "submit") {
			st.open = OpenTag::ButtonSubmit;
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
		name == "p"  || name == "li" || name == "a"  || name == "title" ||
		name == "button" || name == "textarea" || name == "option") {
		flushText(st);
		st.open    = OpenTag::None;
		st.hrefBuf.clear();
		st.classBuf.clear();
		st.idBuf.clear();
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
		st.doc.blocks.push_back(std::move(block));
		++st.doc.formsDiagnostics.selectCount;
		st.inSelect = false;
		st.currentSelectName.clear();
		st.currentSelectClass.clear();
		st.currentSelectId.clear();
		st.currentSelectOptions.clear();
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
	}
	if (name == "pre") {
		flushText(st);
		st.open  = OpenTag::None;
		st.inPre = false;
		st.classBuf.clear();
		st.idBuf.clear();
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
