#pragma once
// guide_web_html_parser.h
//
// Forgiving, minimal HTML-subset parser for guideXOS.
//
// Supported tags (case-insensitive):
//   <img src="...">  - Image block; src resolved via resolveRelativeUrl()
//   <title>          – sets WebDocument.title
//   <h1> <h2> <h3>   – Heading block
//   <p>              – Paragraph block
//   <br>             – line break (flush pending text; no empty Paragraph if no text)
//   <a href="...">   – Link block; href resolved via resolveRelativeUrl()
//   <li>             – ListItem block (bullet item with dash prefix)
//   <ul> <ol>        – ignored (structure only; <li> carries the content)
//   <pre>            – Preformatted block; whitespace/newlines preserved
//   <code>           – inside <pre>: stays in Preformatted; elsewhere: plain text
//
// Ignored with content stripped:
//   <script>  <style>
//
// All other tags: tag token skipped, inner text preserved.
//
// No exceptions are thrown; malformed HTML is handled gracefully.
// No external dependencies beyond the C++ standard library.

#include "guide_web_document.h"   // for gxos::web::WebDocument / DocBlock / BlockType

#include <string>

namespace gxos {
namespace web {

// ---------------------------------------------------------------------------
// resolveRelativeUrl
//
// Given a base file:// URL and an href string, return an absolute URL.
//
// Rules:
//  - href already starts with a scheme (contains "://") → return as-is
//  - href starts with '/'                               → file:// + href
//  - href starts with '#'                               → return base (same page)
//  - otherwise                                          → strip last path
//                                                         segment of base,
//                                                         append href
//
// Example:
//   base = "file:///docs/index.html"
//   href = "desktop.html"
//   →     "file:///docs/desktop.html"
// ---------------------------------------------------------------------------
std::string resolveRelativeUrl(const std::string& base, const std::string& href);

// ---------------------------------------------------------------------------
// parseHtml
//
// Parse |htmlText| as a minimal HTML document and return a populated
// WebDocument.  |pageUrl| is used for relative-link resolution and stored
// verbatim in WebDocument.url.
//
// Never throws; on any internal error the returned document may have
// fewer blocks than expected but will always be usable by renderDocument().
// ---------------------------------------------------------------------------
WebDocument parseHtml(const std::string& pageUrl, const std::string& htmlText);

} // namespace web
} // namespace gxos
