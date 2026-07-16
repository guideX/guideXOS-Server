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
	uint64_t    parentSerial = 0;
	uint16_t    childIndex = 0;
	uint16_t    childCount = 0;
	uint16_t    siblingCount = 0;
	uint16_t    typeIndex = 0;
	uint16_t    typeCount = 0;
	uint64_t    previousSiblingSerial = 0;
	bool        hasLinkTarget = false;
	bool        visited = false;
};

enum class CssCombinator : uint8_t {
	Descendant = 0,
	Child      = 1,
	AdjacentSibling = 2,
	GeneralSibling = 3,
};

struct CssSpecificity {
	uint16_t idCount = 0;
	uint16_t classCount = 0;
	uint16_t elementCount = 0;
};

struct CssSimpleSelector {
	std::string tagName;
	std::vector<std::string> classNames;
	std::string id;
};

enum class CssPseudoClass : uint8_t {
	FirstChild = 0,
	LastChild,
	OnlyChild,
	NthChild,
	FirstOfType,
	LastOfType,
	OnlyOfType,
	NthOfType,
	Not,
	Root,
	Link,
	Visited,
};

struct CssNthExpression {
	int a = 0;
	int b = 0;
};

struct CssPseudoClassSelector {
	CssPseudoClass type = CssPseudoClass::FirstChild;
	CssNthExpression nth;
	CssSimpleSelector notSelector;
};

struct CssSelectorPart {
	std::string tagName;
	std::vector<std::string> classNames;
	std::string id;
	std::vector<CssPseudoClassSelector> pseudoClasses;
};

struct WebStyle {
	// CSS parser bookkeeping.  These masks are bounded to the supported
	// property subset and let the cascade distinguish an explicit false/zero
	// value from an unspecified property.
	uint64_t specifiedProperties = 0;
	uint64_t importantProperties = 0;
	uint64_t inheritedProperties = 0;
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
	int               specificity = 0; // legacy score for diagnostics/backward compatibility
	CssSpecificity    specificityTuple;
	std::vector<CssSelectorPart> selectorParts;
	std::vector<CssCombinator> combinators;
	bool              hasVisitedPseudo = false;
	WebStyle          style;
	uint32_t          sourceOrder = 0;
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
	int    unsupportedSelectorCount = 0;
	int    parseErrorCount = 0;
	bool   styleBlockCapped = false;
	size_t styleBytesProcessed = 0;
	int    clampedValueCount = 0;
	int    borderWidthClampCount = 0;
	int    borderSpacingClampCount = 0;
	int    lineBreakCount = 0;
	int    selectorGroupsParsed = 0;
	int    compoundSelectorsParsed = 0;
	int    childCombinatorCount = 0;
	int    descendantCombinatorCount = 0;
	int    adjacentSiblingCombinatorCount = 0;
	int    generalSiblingCombinatorCount = 0;
	int    adjacentSiblingMatches = 0;
	int    generalSiblingMatches = 0;
	int    siblingScanSteps = 0;
	int    siblingScanClamps = 0;
	int    siblingMetadataClamps = 0;
	int    siblingMetadataErrors = 0;
	int    selectorMatches = 0;
	int    specificityOverrides = 0;
	int    sourceOrderOverrides = 0;
	int    inlineOverrides = 0;
	int    inheritedPropertiesApplied = 0;
	int    selectorDepthClamps = 0;
	int    selectorGroupClamps = 0;
	int    cascadePropertyResolutions = 0;
	int    importantDeclarationsApplied = 0;
	int    ruleCapCount = 0;
	int    declarationCapCount = 0;
	int    declarationsProcessed = 0;
	int    inheritanceDepthClamps = 0;
	int    pseudoClassesParsed = 0;
	int    structuralPseudoMatches = 0;
	int    firstChildMatches = 0;
	int    lastChildMatches = 0;
	int    nthChildMatches = 0;
	int    ofTypeMatches = 0;
	int    notMatches = 0;
	int    linkPseudoMatches = 0;
	int    visitedPseudoMatches = 0;
	int    pseudoClassClamps = 0;
	int    nthExpressionParseErrors = 0;
	int    structuralMetadataClamps = 0;
	int    selectorEvaluationStepClamps = 0;
	uint32_t nextSourceOrder = 1;
	std::string computedStyleEvidence;
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
	HtmlElementRef elementMetadata;
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
	HtmlElementRef        documentElement;
	bool                  hasDocumentElement = false;
	HtmlElementRef        bodyElement;
	bool                  hasBodyElement = false;
	// Bounded structural-node metadata retained for selector relationships.
	// This is not a DOM tree: records are compact, serial-addressed, and capped
	// by the parser's structural metadata limit.
	std::vector<HtmlElementRef> structuralElements;
	WebStyle              bodyStyle;
	std::vector<WebStyleRule> styleRules;
	CssDiagnostics        cssDiagnostics;
	FormsDiagnostics      formsDiagnostics;
};

} // namespace web
} // namespace gxos
