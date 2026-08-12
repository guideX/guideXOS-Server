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
#include <array>
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
	FormLabel     = 12,
};

// Bounded form metadata. This is a value object rather than a DOM node or a
// live submission model. Values are retained only for the existing local
// text-control primitive and are excluded from diagnostics/evidence.
enum class FormControlType : uint8_t {
	None = 0,
	Text,
	Password,
	Search,
	Email,
	Url,
	Number,
	Checkbox,
	Radio,
	Button,
	Submit,
	Reset,
	Textarea,
	Select,
	Option,
	Unsupported,
};

// Navigator focus is deliberately a small session-local state machine.  The
// parser owns this enum so :focus matching can use the same bounded runtime
// table without depending on Navigator.
enum class FormFocusOrigin : uint8_t {
	None = 0,
	Mouse,
	Keyboard,
	ProgrammaticInternalSmoke,
};

// Cancellation is intentionally classified rather than exposed as a DOM
// event.  The Navigator uses this small enum to prove that an armed keyboard
// activation was discarded before a document/control lifecycle boundary.
enum class FormFocusCancellationReason : uint8_t {
	None = 0,
	Escape,
	Navigation,
	Deactivation,
	StateChange,
	GenerationMismatch,
	KeyMismatch,
};

enum class FormAccessibilityRole : uint8_t {
	None = 0,
	Checkbox,
	Radio,
	Button,
	Textbox,
	PasswordTextbox,
	Textarea,
	Select,
};

enum class FormAccessibilityLabelSource : uint8_t {
	None = 0,
	Wrapping,
	ForId,
};

enum class FormAccessibilityNameSource : uint8_t {
	None = 0,
	LabelWrapping,
	LabelForId,
	ButtonText,
	InputValuePresence,
	Placeholder,
	ControlTypeFallback,
};

enum class FormFocusRevealResult : uint8_t {
	None = 0,
	Scroll,
	Noop,
	Clamped,
};

struct FormControlMetadata {
	FormControlType type = FormControlType::None;
	uint64_t logicalSerial = 0;
	uint64_t parentFormSerial = 0;
	uint64_t parentFieldsetSerial = 0;
	std::string name;
	std::string value;
	std::string placeholder;
	std::string label;
	std::string inputType;
	std::string associatedId;
	bool metadataComplete = false;
	bool supported = false;
	bool checked = false;
	bool disabled = false;
	bool required = false;
	bool readOnly = false;
	bool selected = false;
	bool multiple = false;
	bool hidden = false;
	int size = 0;
	int rows = 0;
	int cols = 0;
	int optionCount = 0;
	int selectedOptionIndex = -1;
};

// Session-local form state.  The fixed table is intentionally part of the
// current document rather than the parsed author metadata: it is discarded
// when Navigator replaces the document and is never serialized or shared.
constexpr size_t kFormRuntimeControlCap = 128;

struct FormRuntimeControlState {
	uint64_t logicalSerial = 0;
	FormControlType type = FormControlType::None;
	uint64_t parentFormSerial = 0;
	uint64_t parentFieldsetSerial = 0;
	bool checked = false;
	bool initialChecked = false;
	bool disabled = false;
	uint32_t activationCount = 0;
	bool metadataValid = false;
};

// Presence-only accessibility evidence for one supported control.  It never
// stores author text, values, passwords, form names, or form actions.  The
// fixture ID is populated only for the fixed Phase 2H fixture.
struct FormAccessibilityRecord {
	uint64_t logicalSerial = 0;
	uint64_t documentGeneration = 0;
	std::string fixtureId;
	FormAccessibilityRole role = FormAccessibilityRole::None;
	bool focusable = false;
	bool focused = false;
	FormFocusOrigin focusOrigin = FormFocusOrigin::None;
	bool focusMatch = false;
	bool focusVisibleMatch = false;
	bool checked = false;
	bool disabled = false;
	bool required = false;
	bool readOnly = false;
	bool visible = false;
	bool labelAssociated = false;
	FormAccessibilityLabelSource labelSource = FormAccessibilityLabelSource::None;
	bool accessibleNamePresent = false;
	FormAccessibilityNameSource accessibleNameSource = FormAccessibilityNameSource::None;
	bool metadataComplete = false;
	bool focusRingDrawn = false;
	bool focusRingClamped = false;
	FormFocusRevealResult revealResult = FormFocusRevealResult::None;
	std::string winningSelectorCategory = "none";
	std::string winningPseudo = "none";
	uint16_t winningSpecificityId = 0;
	uint16_t winningSpecificityClass = 0;
	uint16_t winningSpecificityElement = 0;
	uint32_t winningSourceOrder = 0;
};

struct FormRuntimeStateTable {
	std::array<FormRuntimeControlState, kFormRuntimeControlCap> controls{};
	size_t count = 0;
	bool initialized = false;
	uint64_t documentGeneration = 0;
	uint64_t focusedLogicalSerial = 0;
	uint64_t focusedDocumentGeneration = 0;
	FormFocusOrigin focusOrigin = FormFocusOrigin::None;
	bool focusValid = false;
	uint64_t pressedKeyboardLogicalSerial = 0;
	uint64_t pressedKeyboardDocumentGeneration = 0;
	uint8_t pressedKeyboardKey = 0; // 32 = Space, 13 = Enter
	bool keyboardActivationArmed = false;
	std::array<FormAccessibilityRecord, kFormRuntimeControlCap> accessibilityRecords{};
	size_t accessibilityRecordCount = 0;
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

// Phase 3A keeps CSS lengths deliberately compact.  The payload is pixels for
// Px/Zero and a whole percentage for Percent.  Unset is the cascade sentinel;
// Auto and None remain explicit so an accepted declaration cannot be confused
// with an omitted property.  Parser-side validity/clamp state is retained for
// bounded evidence and is never used to turn an unsupported unit into zero.
enum class CssLengthType : uint8_t {
	Unset = 0,
	Auto,
	Px,
	Percent,
	Zero,
	None,
	Content,
};

struct CssLengthValue {
	CssLengthType type = CssLengthType::Unset;
	int value = 0;
	bool valid = false;
	bool clamped = false;
};

enum class BoxSizingMode : uint8_t {
	ContentBox = 0,
	BorderBox = 1,
};

// The Navigator CSS subset keeps display as an explicit computed value.  The
// value is intentionally narrow: unsupported layout modes never fall through
// to inline-block by accident.
enum class DisplayMode : uint8_t {
	Block = 0,
	Inline,
	InlineBlock,
	Flex,
	InlineFlex,
	None,
};

enum class FlexDirectionMode : uint8_t {
	Row = 0,
	RowReverse,
	Column,
	ColumnReverse,
};

enum class FlexWrapMode : uint8_t {
	NoWrap = 0,
	Wrap,
	WrapReverse,
};

enum class AlignContentMode : uint8_t {
	Stretch = 0,
	FlexStart,
	FlexEnd,
	Center,
	SpaceBetween,
	SpaceAround,
};

enum class JustifyContentMode : uint8_t {
	FlexStart = 0,
	FlexEnd,
	Center,
	SpaceBetween,
	SpaceAround,
	SpaceEvenly,
};

enum class AlignItemsMode : uint8_t {
	Stretch = 0,
	FlexStart,
	FlexEnd,
	Center,
	Baseline,
};

enum class AlignSelfMode : uint8_t {
	Auto = 0,
	Stretch,
	FlexStart,
	FlexEnd,
	Center,
	Baseline,
};

// Navigator keeps positioning to a bounded, layout-local subset. Fixed is a
// typed viewport-layer mode; sticky remains an explicit unsupported value and
// is never aliased to absolute positioning.
enum class PositionMode : uint8_t {
	Static = 0,
	Relative,
	Absolute,
	Fixed,
};

// Phase 3E keeps traditional physical floats deliberately narrow.  Logical
// float values and other positioning modes remain unsupported and therefore
// never fall through to a physical side by accident.
enum class FloatMode : uint8_t {
	None = 0,
	Left,
	Right,
};

enum class ClearMode : uint8_t {
	None = 0,
	Left,
	Right,
	Both,
};

enum class OverflowMode : uint8_t {
	Inherit = 0,
	Visible,
	Hidden,
	Auto,
	Scroll,
};

enum class VisibilityMode : uint8_t {
	Visible = 0,
	Hidden = 1,
};

enum class VerticalAlignMode : uint8_t {
	Inherit = 0,
	Baseline,
	Middle,
	Top,
	Bottom,
	TextTop,
	TextBottom,
	Sub,
	Super,
	LengthPx,
	Percent,
};

enum class WhiteSpaceMode : uint8_t {
	Inherit = 0,
	Normal  = 1,
	Nowrap  = 2,
	Pre     = 3,
	PreWrap = 4,
	PreLine = 5,
};

enum class LineHeightMode : uint8_t {
	Normal = 0,
	Px,
	Unitless,
	Percent,
};

// Phase 3B keeps inline participation separate from the legacy block stream.
// Items are bounded runs/atoms, never glyph objects or a retained DOM tree.
enum class InlineItemKind : uint8_t {
	TextRun       = 0,
	ForcedBreak   = 1,
	ReplacedImage = 2,
	FormControl   = 3,
	AtomicBlock   = 4,
};

struct WebInlineItem {
	InlineItemKind kind = InlineItemKind::TextRun;
	uint64_t flowSerial = 0;       // nearest block/line-flow element
	uint64_t ownerSerial = 0;      // closest inline/replaced logical element
	uint64_t parentSerial = 0;
	uint64_t atomicContainerSerial = 0; // nearest bounded inline-block context
	int      blockIndex = -1;      // legacy target identity for hit/focus routing
	std::string text;              // bounded text run; empty for atomic items
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
	FormControlMetadata formControl;
};

// Compact content ownership summary for one logical element serial.  This is
// intentionally metadata, not a heap-owned DOM node or child collection.
struct HtmlElementContentMetadata {
	uint64_t serial = 0;
	uint16_t elementChildCount = 0;
	uint16_t visibleTextByteCount = 0;
	bool     hasElementChild = false;
	bool     hasNonWhitespaceText = false;
	bool     hasImageOrMediaChild = false;
	bool     hasVisibleBreak = false;
	bool     hasVisibleReplacedContent = false;
	bool     hasRenderableContent = false;
	bool     contentMetadataComplete = false;
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
	Empty,
	Checked,
	Disabled,
	Enabled,
	Required,
	ReadOnly,
	ReadWrite,
	Focus,
	FocusVisible,
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
	DisplayMode display = DisplayMode::Block;
	FlexDirectionMode flexDirection = FlexDirectionMode::Row;
	FlexWrapMode flexWrap = FlexWrapMode::NoWrap;
	AlignContentMode alignContent = AlignContentMode::Stretch;
	JustifyContentMode justifyContent = JustifyContentMode::FlexStart;
	AlignItemsMode alignItems = AlignItemsMode::Stretch;
	AlignSelfMode alignSelf = AlignSelfMode::Auto;
	bool     flexDirectionSpecified = false;
	bool     flexWrapSpecified = false;
	bool     alignContentSpecified = false;
	bool     justifyContentSpecified = false;
	bool     alignItemsSpecified = false;
	bool     alignSelfSpecified = false;
	int      flexGrow1000 = 0;
	int      flexShrink1000 = 1000;
	int      order = 0;
	bool     flexGrowSpecified = false;
	bool     flexShrinkSpecified = false;
	bool     orderSpecified = false;
	CssLengthValue flexBasisValue;
	CssLengthValue gapValue;
	CssLengthValue rowGapValue;
	CssLengthValue columnGapValue;
	bool     flexBasisSpecified = false;
	bool     gapSpecified = false;
	bool     rowGapSpecified = false;
	bool     columnGapSpecified = false;
	PositionMode position = PositionMode::Static;
	CssLengthValue topValue;
	CssLengthValue rightValue;
	CssLengthValue bottomValue;
	CssLengthValue leftValue;
	bool     zIndexAuto = true;
	int      zIndex = 0;
	FloatMode floatMode = FloatMode::None;
	ClearMode clearMode = ClearMode::None;
	BoxSizingMode boxSizing = BoxSizingMode::ContentBox;
	// box-sizing is not inherited.  This provenance bit lets the compact
	// table renderer project a table's outer sizing model onto its cell-backed
	// box without overwriting an explicitly styled cell.
	bool     boxSizingSpecified = false;
	CssLengthValue widthValue;
	CssLengthValue heightValue;
	CssLengthValue minWidthValue;
	CssLengthValue maxWidthValue;
	CssLengthValue minHeightValue;
	CssLengthValue maxHeightValue;
	OverflowMode overflowX = OverflowMode::Visible;
	OverflowMode overflowY = OverflowMode::Visible;
	VisibilityMode visibility = VisibilityMode::Visible;
	int      opacityPercent = 100;
	int      effectiveOpacityPercent = 100;
	VerticalAlignMode verticalAlign = VerticalAlignMode::Baseline;
	int      verticalAlignValue = 0;
	bool     verticalAlignValueClamped = false;
	bool     listStyleNone = false;
	ListStyleType listStyleType = ListStyleType::Inherit;
	TableBorderCollapseMode borderCollapse = TableBorderCollapseMode::Inherit;
	int      borderSpacingHorizontal = -1;
	int      borderSpacingVertical = -1;
	GenericFontFamily genericFontFamily = GenericFontFamily::Inherit;
	TextAlign textAlign = TextAlign::Inherit;
	bool     lineHeightNormal = false;
	LineHeightMode lineHeightMode = LineHeightMode::Normal;
	int      lineHeightValue = 0; // px, percent, or unitless multiplier ×1000
	int      marginTop = -1;
	int      marginRight = -1;
	int      marginBottom = -1;
	int      marginLeft = -1;
	// Phase 3D keeps authored units until layout.  The legacy integer fields
	// remain populated for existing callers and default styles.
	CssLengthValue marginTopValue;
	CssLengthValue marginRightValue;
	CssLengthValue marginBottomValue;
	CssLengthValue marginLeftValue;
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
	int      minWidth = -1;
	int      minWidthPercent = -1;
	int      maxWidth = -1;
	int      maxWidthPercent = -1;
	bool     maxWidthNone = false;
	int      minHeight = -1;
	int      minHeightPercent = -1;
	int      maxHeight = -1;
	int      maxHeightPercent = -1;
	bool     maxHeightNone = false;
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

struct FormContainerMetadata {
	std::string tagName;
	std::string className;
	std::string id;
	std::string inlineStyle;
	std::string legendText;
	uint64_t serial = 0;
	uint64_t parentSerial = 0;
	WebStyle style;
	bool metadataComplete = false;
};

// Computed styles for structural ancestors are retained in one bounded,
// serial-addressed table.  This lets Navigator resolve descendant percentages
// against a definite containing-block basis without adding a DOM tree or
// duplicating a full style object on every ancestor reference.
struct CssComputedStyleRecord {
	uint64_t serial = 0;
	WebStyle style;
	bool valid = false;
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
	uint16_t          evidenceRuleIndex = 0;
	uint8_t           evidenceGroupIndex = 0;
	uint32_t          evidenceSelectorHash = 0;
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
	int    lengthValueClampCount = 0;
	int    invalidLengthValueCount = 0;
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
	// Bounded Flexbox diagnostics. These counters describe the compact layout
	// snapshot; they do not imply a retained DOM or scene graph.
	int    flexContainers = 0;
	int    inlineFlexContainers = 0;
	int    flexItems = 0;
	int    flexAnonymousItems = 0;
	int    flexNestedContainers = 0;
	int    flexNestedMultilineContainers = 0;
	int    flexColumnWrappedContainers = 0;
	int    flexLines = 0;
	int    flexWrappedContainers = 0;
	int    flexWrapReverseContainers = 0;
	int    flexAlignContentContainers = 0;
	int    flexStretchedLines = 0;
	int    flexWrapUnsupported = 0;
	int    flexAbsoluteExcluded = 0;
	int    flexDisplayNoneExcluded = 0;
	int    flexOrderSortItems = 0;
	int    flexBaseSizeQueries = 0;
	int    flexIntrinsicQueries = 0;
	int    flexAutomaticMinimumApplied = 0;
	int    flexAutomaticMinimumZero = 0;
	int    flexGrowIterations = 0;
	int    flexShrinkIterations = 0;
	int    flexFreezeIterations = 0;
	int    flexCrossSizePasses = 0;
	int    flexBaselineItems = 0;
	int    flexAutoMarginAbsorptions = 0;
	int    flexGapClamps = 0;
	int    flexGeometryClamps = 0;
	int    flexDepthClamps = 0;
	int    flexOperationClamps = 0;
	int    flexUnsupportedDeclarations = 0;
	int    flexEvidenceRecords = 0;
	std::string flexEvidence;
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
	int    emptyPseudoParsed = 0;
	int    emptyPseudoMatches = 0;
	int    emptyMetadataIncomplete = 0;
	int    contentMetadataClamps = 0;
	int    selectorGroupMemberRecoveries = 0;
	int    commentScanClamps = 0;
	int    unterminatedCommentErrors = 0;
	int    unbalancedParenthesisErrors = 0;
	int    unbalancedBracketErrors = 0;
	int    unterminatedStringErrors = 0;
	int    invalidCombinatorSequences = 0;
	int    identifierEscapeRejections = 0;
	int    selectorMemberParseFailures = 0;
	int    selectorRecoverySuccesses = 0;
	int    checkedPseudoParsed = 0;
	int    checkedPseudoMatches = 0;
	int    disabledPseudoParsed = 0;
	int    disabledPseudoMatches = 0;
	int    enabledPseudoParsed = 0;
	int    enabledPseudoMatches = 0;
	int    requiredPseudoParsed = 0;
	int    requiredPseudoMatches = 0;
	int    readonlyPseudoParsed = 0;
	int    readonlyPseudoMatches = 0;
	int    readwritePseudoParsed = 0;
	int    readwritePseudoMatches = 0;
	int    focusPseudoParsed = 0;
	int    focusPseudoMatches = 0;
	int    focusVisiblePseudoParsed = 0;
	int    focusVisiblePseudoMatches = 0;
	int    checkedRuntimeRecomputations = 0;
	int    runtimeFocusRecomputations = 0;
	// Phase 3D bounded block-flow diagnostics.
	int    marginCollapseSets = 0;
	int    marginCollapseParticipants = 0;
	int    marginCollapseSibling = 0;
	int    marginCollapseParentTop = 0;
	int    marginCollapseParentBottom = 0;
	int    marginCollapseEmpty = 0;
	int    marginCollapsePositiveOnly = 0;
	int    marginCollapseNegativeOnly = 0;
	int    marginCollapseMixed = 0;
	int    marginCollapseBlockedBorder = 0;
	int    marginCollapseBlockedPadding = 0;
	int    marginCollapseBlockedBfc = 0;
	int    marginCollapseBlockedHeight = 0;
	int    marginCollapseBlockedContent = 0;
	int    marginCollapseDepthClamps = 0;
	int    marginGeometryClamps = 0;
	int    bfcRoot = 0;
	int    bfcInlineBlock = 0;
	int    bfcOverflow = 0;
	int    bfcAtomic = 0;
	// Phase 3E bounded float/clear diagnostics.
	int    floatLeft = 0;
	int    floatRight = 0;
	int    floatBlockifications = 0;
	int    floatRecords = 0;
	int    floatPlacementAttempts = 0;
	int    floatPlacementDownshifts = 0;
	int    floatSideBySide = 0;
	int    floatWidthOverflows = 0;
	int    floatLineExclusions = 0;
	int    floatZeroWidthLineAdvances = 0;
	int    floatBfcAvoidances = 0;
	int    floatBfcDownshifts = 0;
	int    clearLeft = 0;
	int    clearRight = 0;
	int    clearBoth = 0;
	int    clearanceApplied = 0;
	int    floatContainmentBoundaries = 0;
	int    floatScopeSuppressions = 0;
	int    floatHeightContainments = 0;
	int    bfcFloatContainments = 0;
	int    bfcFloatHeightExtensions = 0;
	int    bfcFloatHeightNoops = 0;
	int    bfcFloatAvoidanceAttempts = 0;
	int    bfcFloatAvoidanceFits = 0;
	int    bfcFloatAvoidanceDownshifts = 0;
	int    bfcFloatTooWide = 0;
	int    nestedFloatContexts = 0;
	int    nestedFloatDepthClamps = 0;
	int    floatInsideInlineBlock = 0;
	int    floatInsideFloat = 0;
	int    floatListCases = 0;
	int    floatTableCellCases = 0;
	int    floatTableAvoidanceCases = 0;
	int    floatedTableUnsupported = 0;
	int    floatDocumentExtentExtensions = 0;
	int    floatGeometryClamps = 0;
	int    floatPlacementAttemptClamps = 0;
	int    floatExclusionScanClamps = 0;
	int    floatBfcDepthClamps = 0;
	int    floatEvidenceRecords = 0;
	std::string floatEvidence;
	// Phase 3G bounded positioning diagnostics.  Geometry/evidence records are
	// retained by Navigator; these counters stay compact and document-local.
	int    positionStatic = 0;
	int    positionRelative = 0;
	int    positionAbsolute = 0;
	int    positionFixed = 0;
	int    positionUnsupportedFixed = 0;
	int    positionUnsupportedSticky = 0;
	int    relativeOffsets = 0;
	int    relativePercentageOffsets = 0;
	int    absoluteBoxes = 0;
	int    absoluteBlockifications = 0;
	int    positionedContainingBlocks = 0;
	int    positionRootFallbacks = 0;
	int    positionAncestryClamps = 0;
	int    absoluteStaticPositionUses = 0;
	int    absoluteShrinkToFit = 0;
	int    absoluteOutOfFlow = 0;
	int    fixedViewportRecords = 0;
	int    fixedAbsoluteDescendants = 0;
	int    fixedFlexExclusions = 0;
	int    fixedHitTestRecords = 0;
	int    fixedStackingRecords = 0;
	int    fixedExtentExclusions = 0;
	int    positionDocumentExtentExtensions = 0;
	int    zIndexAuto = 0;
	int    zIndexNegative = 0;
	int    zIndexZero = 0;
	int    zIndexPositive = 0;
	int    positionHitOcclusions = 0;
	int    positionGeometryClamps = 0;
	int    positionUnsupportedTable = 0;
	// Phase 3H keeps stacking ownership bounded to positioning-created owners.
	// These counters are evidence for the supported subset, not a general CSS
	// stacking-context implementation.
	int    positionStackingOwners = 0;
	int    positionStackingDepthMax = 0;
	int    positionStackingDepthClamps = 0;
	int    positionNestedZRecords = 0;
	int    positionNegativeZRecords = 0;
	int    positionPositiveZRecords = 0;
	int    positionEqualZSourceOrders = 0;
	int    positionInlineFragmentOwners = 0;
	int    positionInlineFragmentsShifted = 0;
	int    positionInlineAncestryClamps = 0;
	int    positionInlineContainingBlocks = 0;
	int    positionInlineContainingBlockIncomplete = 0;
	int    positionStaticSnapshots = 0;
	int    positionStaticSnapshotFallbacks = 0;
	int    positionLifecycleResets = 0;
	int    positionedEvidenceRecords = 0;
	std::string positionedEvidence;
	int    marginCollapseEvidenceRecords = 0;
	std::string marginCollapseEvidence;
	uint32_t nextSourceOrder = 1;
	std::string computedStyleEvidence;
	std::vector<uint64_t> computedStyleEvidenceSerials;
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
	int  htmlFormsParsed = 0;
	int  htmlFieldsetsParsed = 0;
	int  htmlLabelsParsed = 0;
	int  htmlInputsParsed = 0;
	int  htmlButtonsParsed = 0;
	int  htmlTextareasParsed = 0;
	int  htmlSelectsParsed = 0;
	int  htmlOptionsParsed = 0;
	int  htmlHiddenControls = 0;
	int  controlMetadataClamps = 0;
	int  controlTextTruncations = 0;
	int  formControlsRendered = 0;
	int  formControlsUnsupported = 0;
	int  formInteractionsDeferred = 0;
	int  formRuntimeControlsInitialized = 0;
	int  formCheckboxActivations = 0;
	int  formCheckboxToggles = 0;
	int  formRadioActivations = 0;
	int  formRadioGroupUnchecks = 0;
	int  formLabelActivations = 0;
	int  formButtonActivations = 0;
	int  formDisabledActivationBlocks = 0;
	int  formHiddenHitTargetsSuppressed = 0;
	int  formDuplicateActivationSuppressed = 0;
	int  formRuntimeStateResets = 0;
	int  formHitTargetsRegistered = 0;
	int  formHitTargetClamps = 0;
	int  formFocusableControls = 0;
	int  formFocusChanges = 0;
	int  formFocusClears = 0;
	int  formFocusWraps = 0;
	int  formTabForward = 0;
	int  formTabBackward = 0;
	int  formKeyboardActivations = 0;
	int  formSpaceActivations = 0;
	int  formEnterActivations = 0;
	int  formKeyRepeatSuppressed = 0;
	int  formStaleKeyActivationBlocks = 0;
	int  formDisabledFocusSkips = 0;
	int  formHiddenFocusSkips = 0;
	int  formFocusStateResets = 0;
	int  formFocusCancelEscape = 0;
	int  formFocusCancelNavigation = 0;
	int  formFocusCancelDeactivation = 0;
	int  formFocusCancelStateChange = 0;
	int  formFocusCancelGenerationMismatch = 0;
	int  formFocusCancelKeyMismatch = 0;
	int  formFocusOriginMouse = 0;
	int  formFocusOriginKeyboard = 0;
	int  formFocusVisibleMatches = 0;
	int  formFocusRingDraws = 0;
	int  formFocusRingClamps = 0;
	int  formFocusRevealScrolls = 0;
	int  formFocusRevealNoops = 0;
	int  formFocusRevealClamps = 0;
	int  formAccessibilityRecords = 0;
	int  formAccessibilityMetadataClamps = 0;
	int  formAccessibleNamePresent = 0;
	int  formAccessibleNameMissing = 0;
	int  formLabelAssociationsValid = 0;
	int  formLabelAssociationsInvalid = 0;
	std::string formInteractionMode = "session_local_non_submitting";
};

struct FormOption {
	std::string value;
	std::string text;
	bool selected = false;
	bool disabled = false;
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
	FormControlMetadata formControl;
	std::string labelFor;
	// Non-zero when this legacy block was emitted from the bounded inline flow
	// belonging to the serial.  It lets Navigator collapse adjacent parser
	// records into one line-formatting context without changing existing block
	// consumers.
	uint64_t    inlineFlowSerial = 0;
	// Non-zero when the block belongs to an embedded inline-block formatting
	// context.  This is structural ownership metadata, not a retained DOM link.
	uint64_t    atomicContainerSerial = 0;
};

struct WebDocument {
	std::string           url;
	std::string           title;
	std::vector<DocBlock> blocks;
	std::vector<WebInlineItem> inlineItems;
	HtmlElementRef        documentElement;
	bool                  hasDocumentElement = false;
	HtmlElementRef        bodyElement;
	bool                  hasBodyElement = false;
	// Bounded structural-node metadata retained for selector relationships.
	// This is not a DOM tree: records are compact, serial-addressed, and capped
	// by the parser's structural metadata limit.
	std::vector<HtmlElementRef> structuralElements;
	// Bounded content summaries keyed by the same logical serials as
	// structuralElements.  Entries are capped with the structural registry.
	std::vector<HtmlElementContentMetadata> contentMetadata;
	std::vector<CssComputedStyleRecord> computedStyles;
	WebStyle              bodyStyle;
	std::vector<WebStyleRule> styleRules;
	CssDiagnostics        cssDiagnostics;
	FormsDiagnostics      formsDiagnostics;
	std::vector<FormContainerMetadata> formContainers;
	FormRuntimeStateTable formRuntimeState;
};

} // namespace web
} // namespace gxos
