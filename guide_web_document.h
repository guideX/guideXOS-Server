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
	FormTextInput = 6,  // simple text input control for GET forms
	FormSubmit    = 7,  // submit button for a simple form
	FormCheckbox  = 8,
	FormRadio     = 9,
	FormTextarea  = 10,
	FormSelect    = 11,
};

enum class StyleSelectorType : uint8_t {
	Element = 0,
	Class   = 1,
	Id      = 2,
};

enum class TextAlign : uint8_t {
	Inherit = 0,
	Left    = 1,
	Center  = 2,
	Right   = 3,
};

enum class WhiteSpaceMode : uint8_t {
	Inherit = 0,
	Normal  = 1,
	Pre     = 2,
	PreWrap = 3,
};

enum class OverflowWrapMode : uint8_t {
	Inherit   = 0,
	Normal    = 1,
	BreakWord = 2,
};

enum class WordBreakMode : uint8_t {
	Inherit  = 0,
	Normal   = 1,
	BreakAll = 2,
};

enum class BorderLineStyle : uint8_t {
	Inherit = 0,
	None    = 1,
	Hidden  = 2,
	Solid   = 3,
	Dashed  = 4,
	Dotted  = 5,
};

enum class TableBorderCollapseMode : uint8_t {
	Inherit  = 0,
	Separate = 1,
	Collapse = 2,
};

enum class ListStyleType : uint8_t {
	Inherit      = 0,
	None         = 1,
	Disc         = 2,
	Circle       = 3,
	Square       = 4,
	Decimal      = 5,
	LowerAlpha   = 6,
	UpperAlpha   = 7,
	LowerRoman   = 8,
	UpperRoman   = 9,
};

enum class GenericFontFamily : uint8_t {
	Inherit   = 0,
	SansSerif = 1,
	Serif     = 2,
	Monospace = 3,
};

struct HtmlElementRef {
	std::string tagName;
	std::string className;
	std::string id;
	std::string inlineStyle;
	uint64_t    serial = 0;
};

struct CssSelectorPart {
	std::string tagName;
	std::vector<std::string> classNames;
	std::string id;
};

struct WebStyle {
	bool     hasColor = false;
	uint32_t color = 0;
	bool     hasBackgroundColor = false;
	uint32_t backgroundColor = 0;
	bool     bold = false;
	bool     italic = false;
	bool     underline = false;
	bool     lineThrough = false;
	bool     hasTextDecoration = false;
	bool     displayNone = false;
	bool     listStyleNone = false;
	ListStyleType listStyleType = ListStyleType::Inherit;
	TableBorderCollapseMode borderCollapse = TableBorderCollapseMode::Inherit;
	int      borderSpacingHorizontal = -1;
	int      borderSpacingVertical = -1;
	GenericFontFamily genericFontFamily = GenericFontFamily::Inherit;
	TextAlign textAlign = TextAlign::Inherit;
	bool     lineHeightNormal = false;
	int      marginTop = -1;
	int      marginRight = -1;
	int      marginBottom = -1;
	int      marginLeft = -1;
	int      padding = -1;
	int      paddingTop = -1;
	int      paddingRight = -1;
	int      paddingBottom = -1;
	int      paddingLeft = -1;
	int      fontScaleOrSize = -1;
	int      lineHeight = -1;
	int      width = -1;
	int      widthPercent = -1;
	int      height = -1;
	int      heightPercent = -1;
	int      maxWidth = -1;
	int      maxWidthPercent = -1;
	int      maxHeight = -1;
	int      maxHeightPercent = -1;
	WhiteSpaceMode   whiteSpace = WhiteSpaceMode::Inherit;
	OverflowWrapMode overflowWrap = OverflowWrapMode::Inherit;
	WordBreakMode    wordBreak = WordBreakMode::Inherit;
	bool     hasBorderTop = false;
	int      borderTopWidth = 0;
	uint32_t borderTopColor = 0;
	BorderLineStyle borderTopStyle = BorderLineStyle::Inherit;
	bool     hasBorderRight = false;
	int      borderRightWidth = 0;
	uint32_t borderRightColor = 0;
	BorderLineStyle borderRightStyle = BorderLineStyle::Inherit;
	bool     hasBorderBottom = false;
	int      borderBottomWidth = 0;
	uint32_t borderBottomColor = 0;
	BorderLineStyle borderBottomStyle = BorderLineStyle::Inherit;
	bool     hasBorderLeft = false;
	int      borderLeftWidth = 0;
	uint32_t borderLeftColor = 0;
	BorderLineStyle borderLeftStyle = BorderLineStyle::Inherit;
};

struct WebStyleRule {
	StyleSelectorType selectorType = StyleSelectorType::Element;
	std::string       selector;
	int               specificity = 0;
	std::vector<CssSelectorPart> selectorParts;
	WebStyle          style;
};

struct CssDiagnostics {
	bool   cssEnabled = false;
	bool   cssDetected = false;
	int    styleRuleCount = 0;
	int    styleBlockCount = 0;
	int    inlineStyleCount = 0;
	int    externalStylesheetLoadedCount = 0;
	int    unsupportedExternalStylesheetCount = 0;
	int    unsupportedRuleCount = 0;
	int    unsupportedDeclarationCount = 0;
	int    parseErrorCount = 0;
	bool   styleBlockCapped = false;
	size_t styleBytesProcessed = 0;
	int    clampedValueCount = 0;
	int    borderWidthClampCount = 0;
	int    borderSpacingClampCount = 0;
	int    lineBreakCount = 0;
};

struct FormsDiagnostics {
	int  formCount = 0;
	int  textInputCount = 0;
	int  checkboxCount = 0;
	int  radioCount = 0;
	int  textareaCount = 0;
	int  selectCount = 0;
	int  submitCount = 0;
	int  unsupportedControlCount = 0;
	bool hasUnsupportedMethod = false;
	bool hasUnsupportedEncoding = false;
};

struct FormOption {
	std::string value;
	std::string text;
	bool selected = false;
};

struct DocBlock {
	BlockType   type;
	std::string text;  // display text; for Image this mirrors alt text
	std::string url;   // Link destination, or resolved Image URL
	std::string src;   // original Image src attribute, if any
	std::string alt;   // Image alt text, if any
	int         width  = 0; // optional Image width attribute in CSS pixels
	int         height = 0; // optional Image height attribute in CSS pixels
	bool        imageSizeAttrClamped = false; // width/height attributes normalized or clamped
	std::string tagName;
	std::string className;
	std::string id;
	std::string inlineStyle;
	std::vector<HtmlElementRef> ancestors;
	WebStyle    style;
	int         formIndex = -1;
	std::string formAction;
	std::string formMethod;
	std::string formEncoding;
	std::string inputName;
	std::string inputValue;
	std::string inputType;
	std::string placeholder;
	std::string submitLabel;
	bool        checked = false;
	std::vector<FormOption> options;
	int         selectedOption = -1;
	int         visibleRows = 0;
	int         visibleCols = 0;
	bool        formUnsupported = false;
};

struct WebDocument {
	std::string           url;
	std::string           title;
	std::vector<DocBlock> blocks;
	HtmlElementRef        bodyElement;
	bool                  hasBodyElement = false;
	WebStyle              bodyStyle;
	std::vector<WebStyleRule> styleRules;
	CssDiagnostics        cssDiagnostics;
	FormsDiagnostics      formsDiagnostics;
};

} // namespace web
} // namespace gxos
