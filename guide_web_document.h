#pragma once
// guide_web_document.h
//
// Reusable guideWeb document model.
//
// Defines the core data types shared by any component that produces or
// consumes parsed web documents: the HTML parser, Navigator, the future Help
// Viewer, HTML e-mail preview, Control Panel rich pages, etc.
//
// This header has no dependency on navigator.h or any Navigator-specific type.
// It depends only on the C++ standard library.

#include <cstdint>
#include <string>
#include <vector>

namespace gxos {
namespace web {

// =============================================================================
// Document model
// =============================================================================

enum class BlockType : uint8_t {
	Heading      = 0,   // large / bold heading
	Paragraph    = 1,   // body-text paragraph
	Link         = 2,   // inline link-style text; url field carries the destination
	ListItem     = 3,   // bullet list item (rendered with a dash prefix and indent)
	Preformatted = 4,   // whitespace-preserved text (e.g. <pre>, plain .txt files)
	Image        = 5,   // local image block; url carries the resolved image URL
};

enum class StyleSelectorType : uint8_t {
	Element = 0,
	Class   = 1,
	Id      = 2,
};

struct WebStyle {
	bool     hasColor = false;
	uint32_t color = 0;
	bool     hasBackgroundColor = false;
	uint32_t backgroundColor = 0;
	bool     bold = false;
	bool     underline = false;
	int      marginTop = -1;
	int      marginBottom = -1;
	int      marginLeft = -1;
	int      padding = -1;
	int      fontScaleOrSize = -1;
};

struct WebStyleRule {
	StyleSelectorType selectorType = StyleSelectorType::Element;
	std::string       selector;
	WebStyle          style;
};

struct CssDiagnostics {
	bool   cssDetected = false;
	int    styleRuleCount = 0;
	int    unsupportedExternalStylesheetCount = 0;
	int    unsupportedDeclarationCount = 0;
	bool   styleBlockCapped = false;
	size_t styleBytesProcessed = 0;
};

struct DocBlock {
	BlockType   type;
	std::string text;  // display text; for Image this mirrors alt text
	std::string url;   // Link destination, or resolved Image URL
	std::string src;   // original Image src attribute, if any
	std::string alt;   // Image alt text, if any
	int         width  = 0; // optional Image width attribute in CSS pixels
	int         height = 0; // optional Image height attribute in CSS pixels
	std::string tagName;
	std::string className;
	std::string id;
	WebStyle    style;
};

struct WebDocument {
	std::string           url;
	std::string           title;
	std::vector<DocBlock> blocks;
	WebStyle              bodyStyle;
	std::vector<WebStyleRule> styleRules;
	CssDiagnostics        cssDiagnostics;
};

} // namespace web
} // namespace gxos
