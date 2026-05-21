#pragma once
// navigator_html_parser.h
//
// Forgiving, minimal HTML-subset parser for guideXOS Navigator.
//
// Supported tags (case-insensitive):
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

#include "guide_web_html_parser.h"   // resolveRelativeUrl, parseHtml (gxos::web)
#include "guide_web_document.h"      // BlockType, DocBlock, WebDocument (gxos::web)

#include <string>

namespace gxos {
namespace apps {

// Pull guideWeb types into gxos::apps so existing Navigator call-sites compile
// without modification.
using gxos::web::resolveRelativeUrl;
using gxos::web::parseHtml;

} // namespace apps
} // namespace gxos
