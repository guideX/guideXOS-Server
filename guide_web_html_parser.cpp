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
#include <sstream>
#include <utility>

namespace gxos {
namespace web {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

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

// Block-tag context: what block is currently open.
enum class OpenTag : uint8_t {
	None  = 0,
	H1, H2, H3,
	P,
	A,
	Li,
	Title,
	Pre,   // <pre> block — whitespace preserved
};

struct ParserState {
	WebDocument  doc;
	std::string  textBuf;   // accumulated character data for current block
	std::string  hrefBuf;   // href of the open <a> tag
	OpenTag      open    = OpenTag::None;
	bool         inScript = false;
	bool         inStyle  = false;
	bool         inPre    = false; // true inside <pre>: preserve whitespace
	bool         bodyReached = false; // ignore content before <body> except <title>
};

// Flush textBuf into a DocBlock, if non-empty.
static void flushText(ParserState& st)
{
	std::string t;
	if (st.inPre) {
		// Inside <pre>: decode entities but preserve whitespace/newlines.
		t = decodeEntities(st.textBuf);
		// Strip a single leading newline that immediately follows <pre>
		if (!t.empty() && t[0] == '\n') t = t.substr(1);
		// Strip a single trailing newline before </pre>
		if (!t.empty() && t.back() == '\n') t.pop_back();
	} else {
		t = trim(collapseWs(decodeEntities(st.textBuf)));
	}
	st.textBuf.clear();
	if (t.empty()) return;

	switch (st.open) {
	case OpenTag::H1:
	case OpenTag::H2:
	case OpenTag::H3:
		st.doc.blocks.push_back({BlockType::Heading, t, ""});
		break;
	case OpenTag::P:
		st.doc.blocks.push_back({BlockType::Paragraph, t, ""});
		break;
	case OpenTag::Li:
		st.doc.blocks.push_back({BlockType::ListItem, t, ""});
		break;
	case OpenTag::Pre:
		st.doc.blocks.push_back({BlockType::Preformatted, t, ""});
		break;
	case OpenTag::A:
		if (!st.hrefBuf.empty())
			st.doc.blocks.push_back({BlockType::Link, t, st.hrefBuf});
		else
			st.doc.blocks.push_back({BlockType::Paragraph, t, ""});
		break;
	case OpenTag::Title:
		st.doc.title = t;
		break;
	default:
		// Text outside a known block: emit as paragraph if body is active.
		if (st.bodyReached)
			st.doc.blocks.push_back({BlockType::Paragraph, t, ""});
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

	if (st.inScript || st.inStyle) return;

	// Mark body reached.
	if (name == "body") { st.bodyReached = true; return; }

	// Void / structural tags with no direct content effect.
	if (name == "html" || name == "head" || name == "ul" || name == "ol" ||
		name == "div" || name == "span" || name == "section" || name == "article" ||
		name == "header" || name == "footer" || name == "nav" || name == "main" ||
		name == "table" || name == "tr" || name == "td" || name == "th")
		return;

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
		block.width = parsePositiveIntAttr(tagBody, "width");
		block.height = parsePositiveIntAttr(tagBody, "height");
		st.doc.blocks.push_back(std::move(block));
		st.open = OpenTag::None;
		st.hrefBuf.clear();
		return;
	}

	// Block-level tags: flush any pending text, then open new context.
	flushText(st);

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

	// Unknown open tag: leave current open context unchanged so text inside
	// unknown tags flows into the current block.
}

// Handle a closing tag.
static void handleCloseTag(ParserState& st, const std::string& tagName)
{
	std::string name = toLower(tagName);

	if (name == "script") { st.inScript = false; st.textBuf.clear(); return; }
	if (name == "style")  { st.inStyle  = false; st.textBuf.clear(); return; }

	// Close block-level contexts.
	if (name == "h1" || name == "h2" || name == "h3" ||
		name == "p"  || name == "li" || name == "a"  || name == "title") {
		flushText(st);
		st.open    = OpenTag::None;
		st.hrefBuf.clear();
	}
	if (name == "pre") {
		flushText(st);
		st.open  = OpenTag::None;
		st.inPre = false;
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
	if (href.find("://") != std::string::npos) return href;

	// Root-relative: file:// + href
	if (href[0] == '/') return "file://" + href;

	// Fragment-only ("#...") – stay on the same page.
	if (href[0] == '#') return base;

	// Relative: strip the last path segment from base, append href.
	// base looks like "file:///docs/index.html"
	size_t lastSlash = base.rfind('/');
	if (lastSlash == std::string::npos) return "file:///" + href;
	return base.substr(0, lastSlash + 1) + href;
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
		if (!st.inScript && !st.inStyle) {
			// Inside <pre>, preserve all characters including newlines/spaces.
			// Outside <pre>, collapse the character stream normally; flushText()
			// will call collapseWs() later.
			st.textBuf += c;
		}
		++i;
	}

	// Flush any trailing text not closed by a tag.
	flushText(st);

	// If no title was parsed, use the filename from the URL.
	if (st.doc.title.empty()) {
		size_t slash = pageUrl.rfind('/');
		st.doc.title = (slash != std::string::npos) ? pageUrl.substr(slash + 1) : pageUrl;
	}

	return st.doc;
}

} // namespace web
} // namespace gxos
