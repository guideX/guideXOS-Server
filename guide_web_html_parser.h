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
//   <form method="GET|POST" action="...">
//                    – starts a simple GET form scope
//   <fieldset><legend>...</legend>...</fieldset>
//                    – bounded bordered form group
//   <label for="...">...</label>
//                    – bounded label text and association metadata
//   <input type="text|password|search|email|url|number" ...>
//                    – static text-like control block
//   <input type="checkbox|radio" name="..." value="..." checked>
//                    - FormCheckbox/FormRadio block
//   <input type="button|submit|reset" value="...">
//                    – visual button block; no submission is added by this phase
//   <input type="hidden" ...>
//                    – metadata only, no visible block
//   <textarea name="...">Text</textarea>
//                    - FormTextarea block
//   <select name="..."><option value="..." selected>Text</option></select>
//                    - FormSelect block
//   <input type="submit" value="...">
//                    – FormSubmit block
//   <button type="submit">Text</button>
//                    – bounded visual button block
//
// Ignored with content stripped:
//   <script>
//
// Parsed for CSS-lite rules:
//   <style>          – embedded stylesheet; supported selectors/properties only
// CSS-lite selectors use bounded group recovery, CSS comments as token
// separators, and reject escaped identifiers rather than normalizing them.
// The bounded :empty interpretation matches only complete logical-element
// metadata with no element-like child, non-whitespace text, image/media,
// visible break, replaced content, or other renderable content.  Whitespace
// outside whitespace-preserving blocks is discarded before this test.
//
// All other tags: tag token skipped, inner text preserved.
//
// No exceptions are thrown; malformed HTML/CSS is handled gracefully.
// No external dependencies beyond the C++ standard library.

#include "guide_web_document.h"   // for gxos::web::WebDocument / DocBlock / BlockType

#include <string>
#include <unordered_set>

namespace gxos {
namespace web {

// ---------------------------------------------------------------------------
// resolveRelativeUrl
//
// Given a base URL and an href string, return an absolute URL.
// Supports both file:// and http:// bases. Root-relative http links keep the
// http origin; root-relative non-http links keep the historic file:// behavior.
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
WebDocument parseHtml(const std::string& pageUrl,
	const std::string& htmlText,
	const std::unordered_set<std::string>& visitedUrls = {});

// Recompute bounded document styles after Navigator changes its session-local
// form state table.  Parsed author metadata is not rewritten.
void recomputeDocumentStyles(WebDocument& document);

} // namespace web
} // namespace gxos
