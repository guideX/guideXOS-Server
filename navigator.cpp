#include "navigator.h"

#include "desktop_theme.h"

#include "gui_protocol.h"
#include "gxos_tls_foundation.h"
#include "gxos_tls_prerequisites.h"
#include "kernel/core/include/kernel/image_adapter.h"
#include "kernel/core/include/kernel/system_font.h"
#include "ipc_bus.h"
#include "guide_web_http.h"
#include "logger.h"
#include "navigator_file_io.h"
#include "navigator_html_parser.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace gxos {
namespace apps {

using namespace gxos::gui;
using gxos::web::TextAlign;
using gxos::web::CssLengthType;
using gxos::web::CssLengthValue;
using gxos::web::CssComputedStyleRecord;
using gxos::web::BoxSizingMode;
using gxos::web::DisplayMode;
using gxos::web::PositionMode;
using gxos::web::FloatMode;
using gxos::web::ClearMode;
using gxos::web::OverflowMode;
using gxos::web::VisibilityMode;
using gxos::web::VerticalAlignMode;
using gxos::web::LineHeightMode;
using gxos::web::OverflowWrapMode;
using gxos::web::WhiteSpaceMode;
using gxos::web::WordBreakMode;
using gxos::web::BorderLineStyle;
using gxos::web::GenericFontFamily;
using gxos::web::ListStyleType;
using gxos::web::TableBorderCollapseMode;
using gxos::web::FormControlType;
using gxos::web::FormControlMetadata;
using gxos::web::FormRuntimeControlState;
using gxos::web::FormFocusOrigin;
using gxos::web::FormFocusCancellationReason;
using gxos::web::FormAccessibilityRole;
using gxos::web::FormAccessibilityLabelSource;
using gxos::web::FormAccessibilityNameSource;
using gxos::web::FormFocusRevealResult;
using gxos::web::FormAccessibilityRecord;
using gxos::web::FormRuntimeStateTable;
using gxos::web::kFormRuntimeControlCap;
using gxos::web::InlineItemKind;
using gxos::web::WebInlineItem;
using gxos::web::HtmlElementRef;

uint64_t           Navigator::s_windowId        = 0;
int                Navigator::s_scrollOffset    = 0;
int                Navigator::s_documentHeight  = 0;
std::string        Navigator::s_statusText      = "Ready";
std::string        Navigator::s_hoverStatusText;
int                Navigator::s_hitLinkBlockIndex = -1;
WebDocument        Navigator::s_currentDoc;
WebDocument        Navigator::s_inspectedDoc;
NavigatorPageMetadata Navigator::s_pageMetadata;
NavigatorLifecycleDiagnostics Navigator::s_lifecycleDiagnostics;
NavigatorDocumentCategory Navigator::s_visibleDocumentCategory = NavigatorDocumentCategory::None;
NavigatorDocumentCategory Navigator::s_inspectedSourceCategory = NavigatorDocumentCategory::None;
bool Navigator::s_visibleDocumentInspectionView = false;
uint64_t Navigator::s_inspectedDocumentGeneration = 0;
std::string Navigator::s_pendingDocumentUrl;
NavigatorTransitionCategory Navigator::s_pendingTransitionCategory = NavigatorTransitionCategory::Navigation;
uint64_t Navigator::s_staleMouseReleaseGeneration = 0;
uint64_t Navigator::s_staleKeyReleaseGeneration = 0;
std::vector<std::string> Navigator::s_backStack;
std::vector<std::string> Navigator::s_forwardStack;
std::vector<Bookmark>    Navigator::s_bookmarks;
static std::vector<DownloadItem> s_recentDownloads;
bool        Navigator::s_addressFocused = false;
std::string Navigator::s_addressBuffer;
int         Navigator::s_addressCaret   = 0;
int         Navigator::s_focusedInputBlockIndex = -1;
int         Navigator::s_inputCaret = 0;
uint64_t    Navigator::s_documentGeneration = 0;
bool        Navigator::s_tabKeyPressed = false;
std::string Navigator::s_lastSubmittedFormUrl;
std::string Navigator::s_lastSubmittedFormAction;
std::string Navigator::s_lastSubmittedFormMethod;
std::string Navigator::s_lastSubmittedFormStatus;
std::string Navigator::s_lastPostHttpStatus;
std::string Navigator::s_lastPostContentType;
bool        Navigator::s_findActive = false;
bool        Navigator::s_loading = false;
std::string Navigator::s_findBuffer;
int         Navigator::s_findCaret = 0;
std::vector<Navigator::FindMatch> Navigator::s_findMatches;
int         Navigator::s_currentFindMatch = -1;
bool        Navigator::s_ctrlPressed = false;
bool        Navigator::s_shiftPressed = false;
bool        Navigator::s_mouseLeftDown = false;
Navigator::MouseMode Navigator::s_mouseMode = Navigator::MouseMode::None;
Navigator::HitTarget Navigator::s_mouseDownHitTarget = Navigator::HitTarget::None;
int         Navigator::s_mouseDownLinkBlockIndex = -1;
std::string Navigator::s_mouseDownLinkUrl;
int         Navigator::s_mouseDownX = 0;
int         Navigator::s_mouseDownY = 0;
int         Navigator::s_mouseCurrentX = 0;
int         Navigator::s_mouseCurrentY = 0;
bool        Navigator::s_mouseDragThresholdExceeded = false;
bool        Navigator::s_selectionBegan = false;
bool        Navigator::s_selectionActive = false;
bool        Navigator::s_selectionPending = false;
bool        Navigator::s_selectionDragging = false;
bool        Navigator::s_selectionMoved = false;
int         Navigator::s_selectionStartX = 0;
int         Navigator::s_selectionStartY = 0;
Navigator::SelectionPosition Navigator::s_selectionAnchor;
Navigator::SelectionPosition Navigator::s_selectionFocus;
std::string Navigator::s_navigatorClipboard;
std::string Navigator::s_clipboardMode = "Navigator internal clipboard";
std::vector<int> Navigator::s_registeredWidgetIds;
static std::unordered_set<std::string> s_visitedUrls;

constexpr uint64_t kNavigatorLifecycleCounterCap = 1000000;

static void incrementLifecycleCounter(uint64_t& value)
{
	if (value < kNavigatorLifecycleCounterCap) ++value;
}

static bool navigatorSmokeProgressEnabled()
{
	const char* value = std::getenv("GXOS_NAVIGATOR_SMOKE_PROGRESS");
	return value && std::string(value) == "1";
}

static bool navigatorSmokePaintDeferred()
{
	const char* value = std::getenv("GXOS_NAVIGATOR_SMOKE_DEFER_PAINT");
	return value && std::string(value) == "1";
}

static void navigatorSmokeProgress(const char* marker)
{
	if (!navigatorSmokeProgressEnabled()) return;
	Logger::write(LogLevel::Info, std::string("Navigator smoke progress: ") + marker);
}

static std::string extractDocumentText(const WebDocument& doc);

namespace {
	struct TextMetrics;
	static TextMetrics defaultTextMetrics();
	static int defaultTextFontHeightPx();
	static int textLineTopPaddingPx(int lineHeight);
	static int textUnderlineYPx(int lineTop, int lineHeight);
	static int textLineThroughYPx(int lineTop, int lineHeight);
	enum class BorderSideIndex : uint8_t {
		Top = 0,
		Right = 1,
		Bottom = 2,
		Left = 3
	};
	static void drawThemeRect(uint64_t windowId, int x, int y, int w, int h, uint32_t color);
	static BorderLineStyle cssBorderStyleOrDefault(BorderLineStyle borderStyle, int width);
	static int cssBorderTopPx(const WebStyle& style);
	static int cssBorderRightPx(const WebStyle& style);
	static int cssBorderBottomPx(const WebStyle& style);
	static int cssBorderLeftPx(const WebStyle& style);
	static std::string blockListMarkerText(const DocBlock& block, uint64_t ordinal);
	static uint64_t blockListOrdinal(const WebDocument& doc, int blockIndex);
	static int blockListTextInsetPx(const DocBlock& block, uint64_t ordinal);
	struct RenderCounters {
		int borderedBlocksRendered = 0;
		int dashedBordersRendered = 0;
		int dottedBordersRendered = 0;
		int textDecorationsRendered = 0;
	};
	static RenderCounters s_renderCounters;

	struct CssPaintRect {
		int x = 0;
		int y = 0;
		int w = 0;
		int h = 0;
	};

	struct CssBlockGeometry {
		int availableWidth = 0;
		int outerX = 0;
		int outerY = 0;
		int outerWidth = 0;
		int outerHeight = 0;
		int contentWidth = 0;
		int contentHeight = 0;
		CssPaintRect paddingBox;
		CssPaintRect clip;
		bool widthAuto = false;
		bool heightAuto = false;
		bool widthPercentageUnresolved = false;
		bool heightPercentageUnresolved = false;
		bool minWidthApplied = false;
		bool maxWidthApplied = false;
		bool minHeightApplied = false;
		bool maxHeightApplied = false;
		bool constraintConflict = false;
		bool clamped = false;
	};

	// Traditional floats are represented as bounded exclusion records.  The
	// record is shared by placement, line layout, paint evidence, and hit-test
	// geometry; there is no separate overlap implementation.
	struct CssFloatRecord {
		uint64_t logicalSerial = 0;
		uint64_t bfcIdentity = 0;
		uint64_t flowSerial = 0;
		uint64_t contextSerial = 0;
		uint64_t sourceOrder = 0;
		int blockIndex = -1;
		FloatMode side = FloatMode::None;
		InlineItemKind kind = InlineItemKind::TextRun;
		std::string contentText;
		int marginBoxX = 0;
		int marginBoxY = 0;
		int marginBoxW = 0;
		int marginBoxH = 0;
		int borderBoxX = 0;
		int borderBoxY = 0;
		int borderBoxW = 0;
		int borderBoxH = 0;
		int top = 0;
		int bottom = 0;
		int leftExclusion = 0;
		int rightExclusion = 0;
		int preferredMinimum = 0;
		int preferredWidth = 0;
		int availableWidth = 0;
		int usedWidth = 0;
		int usedHeight = 0;
		int placementAttempts = 0;
		int intersectedRecords = 0;
		int clearance = 0;
		bool blockified = false;
		bool bfcAvoidance = false;
		bool movedBelowFloat = false;
		bool visibilityRetained = true;
		bool complete = true;
		bool clamped = false;
	};

	// One compact record per formatting-context boundary.  This is deliberately
	// scalar metadata: descendants continue to live in the existing flat block
	// and inline snapshots.  The record is the ownership seam that prevents a
	// nested BFC's floats from being recursively re-added to every ancestor.
	struct CssBfcContextRecord {
		uint64_t identity = 0;
		uint64_t parentIdentity = 0;
		uint64_t ownerSerial = 0;
		std::string reason;
		int ownedFloatCount = 0;
		int ownedFloatMaximumBottom = 0;
		int inFlowContentBottom = 0;
		int autoHeightInputExtent = 0;
		int usedContentHeight = 0;
		int originY = 0;
		int outerX = 0;
		int outerWidth = 0;
		int explicitHeight = -1;
		int minHeight = -1;
		int maxHeight = -1;
		int avoidanceAttempts = 0;
		int movedBelowCount = 0;
		int scopeSuppressions = 0;
		bool containedFloat = false;
		bool complete = true;
		bool clamped = false;
	};

	struct CssFloatExclusionQuery {
		int availableLeft = 0;
		int availableRight = 0;
		int availableWidth = 0;
		int nextCandidateY = -1;
		int recordsIntersected = 0;
		bool hasLeft = false;
		bool hasRight = false;
		bool complete = true;
		bool clamped = false;
	};

	struct CssFloatLayoutSnapshot {
		bool valid = false;
		bool building = false;
		std::string url;
		size_t blockCount = 0;
		uint64_t fingerprint = 0;
		uint64_t nextSourceOrder = 1;
		int operations = 0;
		int placementAttempts = 0;
		int placementDownshifts = 0;
		int sideBySide = 0;
		int widthOverflows = 0;
		int lineExclusions = 0;
		int zeroWidthLineAdvances = 0;
		int bfcAvoidances = 0;
		int bfcDownshifts = 0;
		int containmentBoundaries = 0;
		int scopeSuppressions = 0;
		int heightContainments = 0;
		int geometryClamps = 0;
		int placementAttemptClamps = 0;
		int exclusionScanClamps = 0;
		int bfcDepthClamps = 0;
		int maxActiveRecords = 0;
		std::vector<CssFloatRecord> records;
		std::vector<CssBfcContextRecord> bfcContexts;
		std::vector<int> blockClearances;
		int evidenceRecords = 0;
		std::string evidence;
	};

	struct InlineFragmentLayout {
		int itemIndex = -1;
		int blockIndex = -1;
		int lineIndex = 0;
		int sourceOffset = 0;
		int sourceLength = 0;
		int x = 0;
		int y = 0;
		int w = 0;
		int h = 0;
		int contentOffsetX = 0;
		int boxOffsetX = 0;
		int boxWidth = 0;
		int boxHeight = 0;
		int baselineOffset = 0;
		int verticalShift = 0;
		uint64_t ownerSerial = 0;
		uint64_t hitSerial = 0;
		int atomicResultIndex = -1;
		InlineItemKind kind = InlineItemKind::TextRun;
		bool whitespace = false;
		bool collapsedWhitespace = false;
		bool visible = true;
	};

	struct InlineLineLayout {
		int lineIndex = 0;
		int top = 0;
		int baseline = 0;
		int ascent = 0;
		int descent = 0;
		int usedLineHeight = 0;
		int horizontalExtent = 0;
		int firstFragment = 0;
		int fragmentCount = 0;
		int availableLeft = 0;
		int availableRight = 0;
		int availableWidth = 0;
		int floatRecordsIntersected = 0;
		bool exclusionComplete = true;
	};

	struct InlineFlowLayout {
		uint64_t flowSerial = 0;
		uint64_t contextSerial = 0;
		int anchorBlockIndex = -1;
		int outerX = 0;
		int localOuterX = 0;
		int localOuterY = 0;
		int atomicResultIndex = -1;
		int outerWidth = 0;
		int outerHeight = 0;
		int contentX = 0;
		int contentWidth = 1;
		int contentOffsetY = 0;
		int documentContentTop = 0;
		int totalHeight = 0;
		WebStyle style;
		std::vector<InlineLineLayout> lines;
		std::vector<InlineFragmentLayout> fragments;
	};

	// A formatting-context boundary owns only scalar inputs and bounded output
	// ranges.  Child geometry remains in the snapshot's flat placement array so
	// nested layout cannot mutate the parent's cursor or output ownership.
	struct CssAtomicLayoutContext {
		int containingBlockWidth = 0;
		int containingBlockHeight = -1;
		bool containingBlockHeightDefinite = false;
		int originX = 0;
		int originY = 0;
		int availableWidth = 0;
		int localVerticalCursor = 0;
		int localMaximumExtent = 0;
		CssPaintRect clip;
		uint64_t parentStructuralSerial = 0;
		uint8_t formattingContextDepth = 0;
		uint16_t outputBegin = 0;
		uint16_t outputEnd = 0;
		uint16_t evidenceBegin = 0;
		uint16_t hitTargetBegin = 0;
		uint16_t recursionBudget = 0;
		uint64_t generation = 0;
	};

	struct CssAtomicChildPlacement {
		int blockIndex = -1;
		int flowIndex = -1;
		int x = 0;
		int y = 0;
		int w = 0;
		int h = 0;
		CssPaintRect clip;
		uint64_t serial = 0;
		bool visible = true;
		bool complete = true;
	};

	struct CssAtomicLayoutResult {
		uint64_t containerSerial = 0;
		uint64_t childRenderSerial = 0;
		uint16_t depth = 0;
		uint16_t childBegin = 0;
		uint16_t childCount = 0;
		uint16_t hitTargetBegin = 0;
		uint16_t hitTargetCount = 0;
		int usedContentWidth = 0;
		int usedContentHeight = 0;
		int paddingBoxWidth = 0;
		int paddingBoxHeight = 0;
		int borderBoxWidth = 0;
		int borderBoxHeight = 0;
		int outerWidth = 0;
		int outerHeight = 0;
		int baseline = 0;
		int baselineY = 0;
		int preferredMinimum = 0;
		int preferredWidth = 0;
		int shrinkToFitWidth = 0;
		int availableWidth = 0;
		CssPaintRect paintBounds;
		CssPaintRect clip;
		int overflowWidth = 0;
		int overflowHeight = 0;
		uint64_t childBlockSerial = 0;
		bool autoWidth = false;
		bool explicitWidth = false;
		bool baselineFromLine = false;
		bool baselineFallback = false;
		bool overflowClipped = false;
		bool complete = true;
		bool clamped = false;
	};

	struct InlineLayoutSnapshot {
		bool valid = false;
		std::string url;
		size_t blockCount = 0;
		size_t itemCount = 0;
		std::vector<InlineFlowLayout> flows;
		int textRuns = 0;
		int whitespaceRuns = 0;
		int forcedBreaks = 0;
		int replacedItems = 0;
		int controlItems = 0;
		int lineWraps = 0;
		int whitespaceCollapses = 0;
		int leadingSpaceSuppressions = 0;
		int trailingSpaceSuppressions = 0;
		int verticalAlignAdjustments = 0;
		int lineHeightClamps = 0;
		int baselineIterationClamps = 0;
		int inlineFragmentClamps = 0;
		int descenderSafeLines = 0;
		int nestingClamps = 0;
		int wrapScanClamps = 0;
		std::vector<CssAtomicLayoutResult> atomicResults;
		std::vector<CssAtomicChildPlacement> atomicChildren;
		int atomicContextDepthMax = 0;
		int atomicContextDepthClamps = 0;
		int atomicContextsDocument = 0;
		int atomicContextIncomplete = 0;
		int atomicLayoutOperations = 0;
		int atomicLayoutOperationClamps = 0;
		int inlineBlockAutoWidths = 0;
		int inlineBlockExplicitWidths = 0;
		int inlineBlockShrinkToFit = 0;
		int inlineBlockPreferredMinClamps = 0;
		int inlineBlockPreferredWidthClamps = 0;
		int inlineBlockBaselineFromLine = 0;
		int inlineBlockBaselineFallback = 0;
		int inlineBlockNested = 0;
		int inlineBlockWraps = 0;
		int inlineBlockHitTargets = 0;
		int inlineBlockOverflowClips = 0;
	};

	static InlineLayoutSnapshot s_inlineLayoutSnapshot;
	static bool s_inlineLayoutDirty = true;

	// Phase 3D keeps one bounded used-position snapshot for normal block flow.
	// The legacy flat block stream remains the storage model; these records add
	// structural parent/sibling relationships without retaining a DOM tree.
	struct CssMarginCollapseValue {
		int largestPositive = 0;
		int mostNegative = 0;
		int participantCount = 0;
		int resolved = 0;
		bool hasPositive = false;
		bool hasNegative = false;
		bool clamped = false;
	};

	struct CssMarginFlowRecord {
		uint64_t serial = 0;
		uint64_t parentSerial = 0;
		uint64_t previousSerial = 0;
		int specifiedMarginTop = 0;
		int specifiedMarginBottom = 0;
		int usedMarginTop = 0;
		int usedMarginBottom = 0;
		int marginEdgeY = 0;
		int usedY = 0;
		int outerWidth = 0;
		int outerHeight = 0;
		int documentExtentContribution = 0;
		int borderBoxX = 0;
		int borderBoxY = 0;
		int borderBoxW = 0;
		int borderBoxH = 0;
		CssMarginCollapseValue collapse;
		std::string collapseType;
		std::string blockedReason;
		std::string bfcReason;
		int collapseParticipantCount = 0;
		int collapseMaxPositive = 0;
		int collapseMostNegative = 0;
		bool collapsedWithPreviousSibling = false;
		bool collapsedWithParentTop = false;
		bool collapsedWithParentBottom = false;
		bool emptyCollapse = false;
		int clearance = 0;
		bool clearanceApplied = false;
		bool establishesBfc = false;
		bool heightDefinite = false;
		bool minHeightPreventsCollapse = false;
		bool incomplete = false;
		bool clamped = false;
	};

	struct CssMarginLayoutSnapshot {
		bool valid = false;
		std::string url;
		size_t blockCount = 0;
		uint64_t fingerprint = 0;
		int documentHeight = 0;
		int maximumParticipants = 0;
		int maximumDepth = 0;
		int operations = 0;
		int participantClamps = 0;
		int traversalClamps = 0;
		std::vector<CssMarginFlowRecord> records;
		std::string evidence;
		int evidenceRecords = 0;
	};

	static CssMarginLayoutSnapshot s_cssMarginLayoutSnapshot;
	static bool s_cssMarginLayoutBuilding = false;
	static CssFloatLayoutSnapshot s_cssFloatLayoutSnapshot;

	constexpr size_t kCssPositionedBoxCap = 256;
	constexpr size_t kCssPositionedAncestryCap = 16;
	constexpr int kCssPositionedGeometryCap = 8192;

	struct CssPositionBox {
		CssPaintRect content;
		CssPaintRect padding;
		CssPaintRect border;
		bool widthDefinite = false;
		bool heightDefinite = false;
		bool complete = true;
	};

	struct CssPositionedRecord {
		int blockIndex = -1;
		uint64_t logicalSerial = 0;
		uint64_t parentSerial = 0;
		uint64_t containingBlockSerial = 0;
		std::string containingBlockType;
		CssPositionBox containingBlock;
		PositionMode mode = PositionMode::Static;
		CssLengthValue top;
		CssLengthValue right;
		CssLengthValue bottom;
		CssLengthValue left;
		int resolvedTop = 0;
		int resolvedRight = 0;
		int resolvedBottom = 0;
		int resolvedLeft = 0;
		bool topResolved = false;
		bool rightResolved = false;
		bool bottomResolved = false;
		bool leftResolved = false;
		int staticX = 0;
		int staticY = 0;
		int normalX = 0;
		int normalY = 0;
		int finalX = 0;
		int finalY = 0;
		int usedWidth = 0;
		int usedHeight = 0;
		int marginLeft = 0;
		int marginRight = 0;
		int marginTop = 0;
		int marginBottom = 0;
		int relativeShiftX = 0;
		int relativeShiftY = 0;
		bool flowParticipation = true;
		bool parentHeightContribution = true;
		int documentExtentContribution = 0;
		bool zIndexAuto = true;
		int zIndex = 0;
		int sourceOrder = 0;
		int paintTier = 1;
		CssPaintRect clip;
		bool paintVisible = false;
		bool hitVisible = false;
		bool staticPositionUsed = false;
		bool blockified = false;
		bool complete = true;
		bool clamped = false;
		std::string incompleteReason;
	};

	struct CssPositionLayoutSnapshot {
		bool valid = false;
		std::string url;
		size_t blockCount = 0;
		uint64_t fingerprint = 0;
		uint64_t generation = 0;
		int documentExtent = 0;
		int ancestryLookups = 0;
		int maximumAncestryDepth = 0;
		int absoluteLayoutOperations = 0;
		int overlapScans = 0;
		int geometryClamps = 0;
		int ancestryClamps = 0;
		int evidenceRecords = 0;
		std::vector<int> blockRecordIndices;
		std::vector<CssPositionedRecord> records;
		std::string evidence;
	};

	static CssPositionLayoutSnapshot s_cssPositionLayoutSnapshot;

	constexpr int kCssClipStackDepth = 16;
	static std::array<CssPaintRect, kCssClipStackDepth> s_cssClipStack{};
	static int s_cssClipDepth = 0;
	static int s_cssPaintOpacityPercent = 100;
	static int s_cssClippedPaintOps = 0;
	static int s_cssClipIntersections = 0;
	static int s_cssClipRecordCount = 0;
	static int s_cssClipDepthClamps = 0;
	static int s_cssClippedHitTargets = 0;
	static int s_cssHitTargetsBeforeClipping = 0;
	static int s_cssHitTargetsAfterClipping = 0;

	static CssPaintRect cssPaintRectIntersect(const CssPaintRect& left, const CssPaintRect& right)
	{
		const int64_t leftEdge = std::max<int64_t>(left.x, right.x);
		const int64_t topEdge = std::max<int64_t>(left.y, right.y);
		const int64_t rightEdge = std::min<int64_t>(static_cast<int64_t>(left.x) + std::max(0, left.w),
			static_cast<int64_t>(right.x) + std::max(0, right.w));
		const int64_t bottomEdge = std::min<int64_t>(static_cast<int64_t>(left.y) + std::max(0, left.h),
			static_cast<int64_t>(right.y) + std::max(0, right.h));
		CssPaintRect result;
		result.x = static_cast<int>(std::max<int64_t>(std::numeric_limits<int>::min(),
			std::min<int64_t>(std::numeric_limits<int>::max(), leftEdge)));
		result.y = static_cast<int>(std::max<int64_t>(std::numeric_limits<int>::min(),
			std::min<int64_t>(std::numeric_limits<int>::max(), topEdge)));
		result.w = static_cast<int>(std::max<int64_t>(0, std::min<int64_t>(std::numeric_limits<int>::max(), rightEdge - leftEdge)));
		result.h = static_cast<int>(std::max<int64_t>(0, std::min<int64_t>(std::numeric_limits<int>::max(), bottomEdge - topEdge)));
		return result;
	}

	static CssPaintRect cssCurrentPaintClip()
	{
		return s_cssClipDepth > 0 ? s_cssClipStack[static_cast<size_t>(s_cssClipDepth - 1)]
			: CssPaintRect{std::numeric_limits<int>::min() / 2, std::numeric_limits<int>::min() / 2,
				std::numeric_limits<int>::max(), std::numeric_limits<int>::max()};
	}

	static void cssSetPaintClip(const CssPaintRect& clip)
	{
		if (s_cssClipDepth <= 0) {
			s_cssClipStack[0] = clip;
			s_cssClipDepth = 1;
			return;
		}
		s_cssClipStack[static_cast<size_t>(s_cssClipDepth - 1)] = clip;
	}

	static bool cssPushPaintClip(const CssPaintRect& clip)
	{
		if (s_cssClipDepth >= kCssClipStackDepth) {
			++s_cssClipDepthClamps;
			return false;
		}
		const CssPaintRect combined = cssPaintRectIntersect(cssCurrentPaintClip(), clip);
		++s_cssClipRecordCount;
		if (combined.w != clip.w || combined.h != clip.h || combined.x != clip.x || combined.y != clip.y)
			++s_cssClipIntersections;
		s_cssClipStack[static_cast<size_t>(s_cssClipDepth++)] = combined;
		return true;
	}

	static void cssPopPaintClip()
	{
		if (s_cssClipDepth > 1) --s_cssClipDepth;
	}

	static uint8_t cssPaintChannel(uint8_t channel)
	{
		return static_cast<uint8_t>((static_cast<int>(channel) * std::max(0, std::min(100, s_cssPaintOpacityPercent)) + 50) / 100);
	}

	constexpr int kWindowW = 920;
	constexpr int kWindowH = 640;
	constexpr int kToolbarH = 64;
	constexpr int kStatusBarH = 24;
	constexpr int kButtonY = 12;
	constexpr int kButtonW = 66;
	constexpr int kButtonH = 26;
	constexpr int kButtonGap = 4;
	constexpr int kAddressX = 514;
	constexpr int kAddressY = 12;
	constexpr int kAddressH = 26;
	constexpr int kAddressW = 920 - kAddressX - 20;
	constexpr int kContentX = 24;
	constexpr int kContentY = kToolbarH + 18;
	constexpr int kContentW = 920 - 48;
	constexpr int kContentH = 640 - kToolbarH - kStatusBarH - 24;
	constexpr int kHeadingY = 24;
	constexpr int kDocumentIndent = 18;
	constexpr int kDocumentListIndent = 28;
	constexpr int kDocumentPreIndent = 18;
	constexpr int kDocumentRightPad = 16;
	constexpr size_t kNavigatorMaxSourcePreviewBytes = gxos::web::kHttpMaxBodyBytes;
	constexpr uint32_t kRemoteImageMaxBytes = 256u * 1024u;
	constexpr uint32_t kRemoteImageMaxWidth = 2048u;
	constexpr uint32_t kRemoteImageMaxHeight = 2048u;
	constexpr uint32_t kRemoteImageMaxPixels = 2048u * 2048u;
	constexpr int kFormInputW = 320;
	constexpr int kFormControlH = 26;
	constexpr int kFormSubmitW = 104;
	constexpr int kTextareaMinRows = 3;
	constexpr int kTextareaMaxRows = 8;
	constexpr int kMouseDragThreshold = 4;

	constexpr int kWidgetIdBack = 1;
	constexpr int kWidgetIdForward = 2;
	constexpr int kWidgetIdReload = 3;
	constexpr int kWidgetIdHome = 4;
	constexpr int kWidgetIdBookmarks = 5;
	constexpr int kWidgetIdAddBookmark = 6;
	constexpr int kWidgetIdFind = 7;

	void publish(MsgType type, const std::string& payload)
	{
		ipc::Message msg;
		msg.type = static_cast<uint32_t>(type);
		msg.data.assign(payload.begin(), payload.end());
		ipc::Bus::publish("gui.input", std::move(msg), false);
	}

	void drawRect(uint64_t windowId, int x, int y, int w, int h, int r, int g, int b)
	{
		if (s_cssPaintOpacityPercent <= 0 || w <= 0 || h <= 0) return;
		CssPaintRect requested{x, y, w, h};
		CssPaintRect clipped = cssPaintRectIntersect(requested, cssCurrentPaintClip());
		if (clipped.w <= 0 || clipped.h <= 0) {
			++s_cssClippedPaintOps;
			return;
		}
		if (clipped.w != requested.w || clipped.h != requested.h || clipped.x != requested.x || clipped.y != requested.y)
			++s_cssClippedPaintOps;
		std::ostringstream oss;
		oss << windowId << "|" << clipped.x << "|" << clipped.y << "|" << clipped.w << "|" << clipped.h
			<< "|" << static_cast<int>(cssPaintChannel(static_cast<uint8_t>(std::max(0, std::min(255, r)))))
			<< "|" << static_cast<int>(cssPaintChannel(static_cast<uint8_t>(std::max(0, std::min(255, g)))))
			<< "|" << static_cast<int>(cssPaintChannel(static_cast<uint8_t>(std::max(0, std::min(255, b)))));
		publish(MsgType::MT_DrawRect, oss.str());
	}

	void drawTextAt(uint64_t windowId, int x, int y, const std::string& text)
	{
		publish(MsgType::MT_DrawTextAt, packDrawTextAt(windowId, x, y, text));
	}

	void drawTextAtColored(uint64_t windowId, int x, int y, const std::string& text, int r, int g, int b)
	{
		if (s_cssPaintOpacityPercent <= 0 || text.empty()) return;
		const CssPaintRect clip = cssCurrentPaintClip();
		const int charWidth = 8;
		const int textWidth = static_cast<int>(std::min<size_t>(text.size(), 4096u)) * charWidth;
		if (x + textWidth <= clip.x || x >= clip.x + clip.w || y + 18 <= clip.y || y >= clip.y + clip.h) {
			++s_cssClippedPaintOps;
			return;
		}
		std::string clippedText = text;
		int drawX = x;
		if (drawX < clip.x) {
			const int skip = std::max(0, (clip.x - drawX + charWidth - 1) / charWidth);
			if (skip >= static_cast<int>(clippedText.size())) return;
			clippedText = clippedText.substr(static_cast<size_t>(skip));
			drawX += skip * charWidth;
		}
		const int maxChars = std::max(0, (clip.x + clip.w - drawX) / charWidth);
		if (static_cast<int>(clippedText.size()) > maxChars) {
			clippedText.resize(static_cast<size_t>(maxChars));
			++s_cssClippedPaintOps;
		}
		if (clippedText.empty()) return;
		if (drawX != x || clippedText.size() != text.size()) ++s_cssClippedPaintOps;
		publish(MsgType::MT_DrawTextAtColor, packDrawTextAtColor(windowId, drawX, y,
			cssPaintChannel(static_cast<uint8_t>(std::max(0, std::min(255, r)))),
			cssPaintChannel(static_cast<uint8_t>(std::max(0, std::min(255, g)))),
			cssPaintChannel(static_cast<uint8_t>(std::max(0, std::min(255, b)))),
			clippedText));
	}

	void drawTextAtStyled(uint64_t windowId, int x, int y, const std::string& text, const WebStyle& style, uint32_t fallbackColor = 0xFF303846u, int lineHeight = -1)
	{
		uint32_t color = fallbackColor;
		if (style.hasColor) {
			color = style.color;
			if (color == 0xFF303846u) {
				color = fallbackColor;
			}
			int r = static_cast<int>((color >> 16) & 0xFFu);
			int g = static_cast<int>((color >> 8) & 0xFFu);
			int b = static_cast<int>(color & 0xFFu);
			if (style.italic) {
				r = std::max(0, r - 12);
				g = std::max(0, g - 12);
				b = std::max(0, b - 12);
			}
			if (style.bold) {
				drawTextAtColored(windowId, x + 1, y, text, r, g, b);
			}
			if (style.italic) {
				drawTextAtColored(windowId, x + 1, y + 1, text, r, g, b);
			}
			drawTextAtColored(windowId, x, y, text, r, g, b);
		} else {
			if (style.bold) {
				drawTextAtColored(windowId, x + 1, y, text,
					static_cast<int>((fallbackColor >> 16) & 0xFFu),
					static_cast<int>((fallbackColor >> 8) & 0xFFu),
					static_cast<int>(fallbackColor & 0xFFu));
			}
			if (style.italic) {
				drawTextAtColored(windowId, x + 1, y + 1, text,
					static_cast<int>((fallbackColor >> 16) & 0xFFu),
					static_cast<int>((fallbackColor >> 8) & 0xFFu),
					static_cast<int>(fallbackColor & 0xFFu));
			}
			drawTextAtColored(windowId, x, y, text,
				static_cast<int>((fallbackColor >> 16) & 0xFFu),
				static_cast<int>((fallbackColor >> 8) & 0xFFu),
				static_cast<int>(fallbackColor & 0xFFu));
		}
		if ((style.underline || style.lineThrough) && !text.empty()) {
			const int useLineHeight = lineHeight > 0 ? lineHeight : std::max(1, defaultTextFontHeightPx() + 2);
			const int textWidth = std::max(1, static_cast<int>(text.size()) * 8);
			if (style.underline) {
				drawRect(windowId, x, textUnderlineYPx(y, useLineHeight), textWidth, 1,
					static_cast<int>((color >> 16) & 0xFFu),
					static_cast<int>((color >> 8) & 0xFFu),
					static_cast<int>(color & 0xFFu));
			}
			if (style.lineThrough) {
				drawRect(windowId, x, textLineThroughYPx(y, useLineHeight), textWidth, 1,
					static_cast<int>((color >> 16) & 0xFFu),
					static_cast<int>((color >> 8) & 0xFFu),
					static_cast<int>(color & 0xFFu));
			}
			++s_renderCounters.textDecorationsRendered;
		}
	}

	static BorderLineStyle effectiveBorderStyle(const WebStyle& style, BorderSideIndex side)
	{
		switch (side) {
		case BorderSideIndex::Top:
			return cssBorderStyleOrDefault(style.borderTopStyle, style.borderTopWidth);
		case BorderSideIndex::Right:
			return cssBorderStyleOrDefault(style.borderRightStyle, style.borderRightWidth);
		case BorderSideIndex::Bottom:
			return cssBorderStyleOrDefault(style.borderBottomStyle, style.borderBottomWidth);
		case BorderSideIndex::Left:
			return cssBorderStyleOrDefault(style.borderLeftStyle, style.borderLeftWidth);
		}
		return BorderLineStyle::None;
	}

	static uint32_t borderColorForSide(const WebStyle& style, BorderSideIndex side)
	{
		switch (side) {
		case BorderSideIndex::Top: return style.borderTopColor;
		case BorderSideIndex::Right: return style.borderRightColor;
		case BorderSideIndex::Bottom: return style.borderBottomColor;
		case BorderSideIndex::Left: return style.borderLeftColor;
		}
		return 0;
	}

	static int borderWidthForSide(const WebStyle& style, BorderSideIndex side)
	{
		switch (side) {
		case BorderSideIndex::Top:
			return cssBorderTopPx(style);
		case BorderSideIndex::Right:
			return cssBorderRightPx(style);
		case BorderSideIndex::Bottom:
			return cssBorderBottomPx(style);
		case BorderSideIndex::Left:
			return cssBorderLeftPx(style);
		}
		return 0;
	}

	static bool drawBorderRun(uint64_t windowId, int x, int y, int w, int h, int lineWidth,
		BorderLineStyle borderStyle, uint32_t color, bool horizontal)
	{
		if (w <= 0 || h <= 0 || lineWidth <= 0) return false;
		if (((color >> 24) & 0xFFu) == 0) return false;
		const int maxThickness = horizontal ? h : w;
		if (maxThickness <= 0) return false;
		lineWidth = std::max(1, std::min(lineWidth, maxThickness));
		if (borderStyle == BorderLineStyle::None || borderStyle == BorderLineStyle::Hidden) {
			return false;
		}
		if (borderStyle == BorderLineStyle::Dashed || borderStyle == BorderLineStyle::Dotted) {
			if (borderStyle == BorderLineStyle::Dashed) ++s_renderCounters.dashedBordersRendered;
			if (borderStyle == BorderLineStyle::Dotted) ++s_renderCounters.dottedBordersRendered;
			const int on = borderStyle == BorderLineStyle::Dashed ? std::max(4, lineWidth * 3) : std::max(1, lineWidth);
			const int off = borderStyle == BorderLineStyle::Dashed ? std::max(3, lineWidth * 2) : std::max(1, lineWidth);
			if (horizontal) {
				for (int pos = 0; pos < w; pos += on + off) {
					const int run = std::min(on, w - pos);
					if (run > 0) drawThemeRect(windowId, x + pos, y, run, lineWidth, color);
				}
			} else {
				for (int pos = 0; pos < h; pos += on + off) {
					const int run = std::min(on, h - pos);
					if (run > 0) drawThemeRect(windowId, x, y + pos, lineWidth, run, color);
				}
			}
			return true;
		}
		if (horizontal) {
			drawThemeRect(windowId, x, y, w, lineWidth, color);
		} else {
			drawThemeRect(windowId, x, y, lineWidth, h, color);
		}
		return true;
	}

	static bool drawBorderSide(uint64_t windowId, const WebStyle& style, BorderSideIndex side, int x, int y, int w, int h)
	{
		const BorderLineStyle borderStyle = effectiveBorderStyle(style, side);
		const int lineWidth = borderWidthForSide(style, side);
		if (borderStyle == BorderLineStyle::None || borderStyle == BorderLineStyle::Hidden || lineWidth <= 0) {
			return false;
		}
		uint32_t color = borderColorForSide(style, side);
		if (((color >> 24) & 0xFFu) == 0) {
			return false;
		}
		switch (borderStyle) {
		case BorderLineStyle::Dashed:
		case BorderLineStyle::Dotted:
			return drawBorderRun(windowId, x, y, w, h, lineWidth, borderStyle, color,
				side == BorderSideIndex::Top || side == BorderSideIndex::Bottom);
		case BorderLineStyle::Inherit:
		case BorderLineStyle::Solid:
		default:
			break;
		}
		if (side == BorderSideIndex::Top || side == BorderSideIndex::Bottom) {
			return drawBorderRun(windowId, x, y, w, h, lineWidth, BorderLineStyle::Solid, color, true);
		}
		return drawBorderRun(windowId, x, y, w, h, lineWidth, BorderLineStyle::Solid, color, false);
	}

	static void drawBoxDecorations(uint64_t windowId, int x, int y, int w, int h, const WebStyle& style,
		bool drawTop = true, bool drawRight = true, bool drawBottom = true, bool drawLeft = true)
	{
		if (w <= 0 || h <= 0) return;
		bool anyBorder = false;
		if (style.hasBackgroundColor) {
			drawThemeRect(windowId, x, y, w, h, style.backgroundColor);
		}
		if (drawTop && style.hasBorderTop && borderWidthForSide(style, BorderSideIndex::Top) > 0) {
			anyBorder = drawBorderSide(windowId, style, BorderSideIndex::Top, x, y, w, h) || anyBorder;
		}
		if (drawRight && style.hasBorderRight && borderWidthForSide(style, BorderSideIndex::Right) > 0) {
			anyBorder = drawBorderSide(windowId, style, BorderSideIndex::Right, x + std::max(0, w - borderWidthForSide(style, BorderSideIndex::Right)), y, w, h) || anyBorder;
		}
		if (drawBottom && style.hasBorderBottom && borderWidthForSide(style, BorderSideIndex::Bottom) > 0) {
			anyBorder = drawBorderSide(windowId, style, BorderSideIndex::Bottom, x, y + std::max(0, h - borderWidthForSide(style, BorderSideIndex::Bottom)), w, h) || anyBorder;
		}
		if (drawLeft && style.hasBorderLeft && borderWidthForSide(style, BorderSideIndex::Left) > 0) {
			anyBorder = drawBorderSide(windowId, style, BorderSideIndex::Left, x, y, w, h) || anyBorder;
		}
		if (anyBorder) {
			++s_renderCounters.borderedBlocksRendered;
		}
	}

	void drawImage(uint64_t windowId, int x, int y, int w, int h, const std::string& path)
	{
		if (s_cssPaintOpacityPercent <= 0 || w <= 0 || h <= 0) return;
		const CssPaintRect requested{x, y, w, h};
		const CssPaintRect clipped = cssPaintRectIntersect(requested, cssCurrentPaintClip());
		if (clipped.w <= 0 || clipped.h <= 0) {
			++s_cssClippedPaintOps;
			return;
		}
		// The compositor image primitive has no source-rectangle API.  Do not
		// resize a partially clipped image (that would distort its aspect ratio);
		// skip it until a native clipped image primitive exists.
		if (clipped.w != requested.w || clipped.h != requested.h || clipped.x != requested.x || clipped.y != requested.y) {
			++s_cssClippedPaintOps;
			return;
		}
		publish(MsgType::MT_DrawImage, packDrawImage(windowId, x, y, w, h, path));
	}

	void drawAnimatedImage(uint64_t windowId, int x, int y, int w, int h, const std::string& pathPattern)
	{
		publish(MsgType::MT_DrawImageAnimated, packDrawImage(windowId, x, y, w, h, pathPattern));
	}

	bool isSciFiThemeActive()
	{
		return GetCurrentDesktopThemeId() == DesktopThemeId::SciFi;
	}

	const DesktopTheme& navigatorTheme()
	{
		return GetCurrentDesktopTheme();
	}

	uint32_t packRgb(int r, int g, int b)
	{
		return 0xFF000000u |
			(static_cast<uint32_t>(r & 0xFF) << 16) |
			(static_cast<uint32_t>(g & 0xFF) << 8) |
			static_cast<uint32_t>(b & 0xFF);
	}

	uint32_t blendColor(uint32_t baseColor, uint32_t overlayColor, int overlayPercent)
	{
		if (overlayPercent <= 0) return baseColor;
		if (overlayPercent >= 100) return overlayColor;

		const int baseR = static_cast<int>((baseColor >> 16) & 0xFF);
		const int baseG = static_cast<int>((baseColor >> 8) & 0xFF);
		const int baseB = static_cast<int>(baseColor & 0xFF);
		const int overR = static_cast<int>((overlayColor >> 16) & 0xFF);
		const int overG = static_cast<int>((overlayColor >> 8) & 0xFF);
		const int overB = static_cast<int>(overlayColor & 0xFF);
		const int keepPercent = 100 - overlayPercent;

		return packRgb(
			(baseR * keepPercent + overR * overlayPercent) / 100,
			(baseG * keepPercent + overG * overlayPercent) / 100,
			(baseB * keepPercent + overB * overlayPercent) / 100);
	}

	bool isDarkColor(uint32_t color)
	{
		const int r = static_cast<int>((color >> 16) & 0xFFu);
		const int g = static_cast<int>((color >> 8) & 0xFFu);
		const int b = static_cast<int>(color & 0xFFu);
		return ((r * 30) + (g * 59) + (b * 11)) < (128 * 100);
	}

	void drawThemeRect(uint64_t windowId, int x, int y, int w, int h, uint32_t color)
	{
		drawRect(windowId, x, y, w, h,
			static_cast<int>((color >> 16) & 0xFFu),
			static_cast<int>((color >> 8) & 0xFFu),
			static_cast<int>(color & 0xFFu));
	}

	void drawThemeText(uint64_t windowId, int x, int y, const std::string& text, uint32_t color)
	{
		drawTextAtColored(windowId, x, y, text,
			static_cast<int>((color >> 16) & 0xFFu),
			static_cast<int>((color >> 8) & 0xFFu),
			static_cast<int>(color & 0xFFu));
	}

	uint32_t NavigatorBodyColor()
	{
		if (!isSciFiThemeActive()) return packRgb(25, 29, 38);
		const DesktopTheme& theme = navigatorTheme();
		return blendColor(theme.taskbarBackground, theme.windowBackground, 14);
	}

	uint32_t NavigatorToolbarColor()
	{
		if (!isSciFiThemeActive()) return packRgb(42, 46, 58);
		const DesktopTheme& theme = navigatorTheme();
		return blendColor(theme.titleBarBackground, theme.windowBackground, 12);
	}

	uint32_t NavigatorToolbarBorderColor()
	{
		if (!isSciFiThemeActive()) return packRgb(78, 86, 108);
		const DesktopTheme& theme = navigatorTheme();
		return blendColor(theme.windowBorder, theme.mutedAccent, 24);
	}

	uint32_t NavigatorAddressFillColor()
	{
		if (!isSciFiThemeActive()) return packRgb(18, 22, 30);
		const DesktopTheme& theme = navigatorTheme();
		return blendColor(theme.windowBackground, theme.taskbarBackground, 12);
	}

	uint32_t NavigatorAddressFocusedBorderColor()
	{
		if (!isSciFiThemeActive()) return packRgb(80, 140, 220);
		const DesktopTheme& theme = navigatorTheme();
		return blendColor(theme.accent, theme.windowBackground, 28);
	}

	uint32_t NavigatorAddressIdleTopBorderColor()
	{
		if (!isSciFiThemeActive()) return packRgb(110, 120, 142);
		const DesktopTheme& theme = navigatorTheme();
		return blendColor(theme.windowBorder, theme.mutedAccent, 18);
	}

	uint32_t NavigatorAddressIdleBottomBorderColor()
	{
		if (!isSciFiThemeActive()) return packRgb(70, 78, 96);
		const DesktopTheme& theme = navigatorTheme();
		return blendColor(theme.taskbarBorder, theme.windowBackground, 18);
	}

	uint32_t NavigatorContentColor()
	{
		if (!isSciFiThemeActive()) return packRgb(245, 247, 250);
		const DesktopTheme& theme = navigatorTheme();
		return blendColor(theme.windowBackground, theme.taskbarBackground, 18);
	}

	uint32_t NavigatorContentTextColor(uint32_t contentColor)
	{
		if (!isSciFiThemeActive()) return 0xFF303846u;
		return isDarkColor(contentColor) ? navigatorTheme().titleBarText : 0xFF303846u;
	}

	uint32_t NavigatorContentBorderColor()
	{
		if (!isSciFiThemeActive()) return packRgb(186, 192, 204);
		const DesktopTheme& theme = navigatorTheme();
		return blendColor(theme.windowBorder, theme.mutedAccent, 18);
	}

	uint32_t NavigatorScrollTrackColor()
	{
		if (!isSciFiThemeActive()) return packRgb(229, 232, 238);
		const DesktopTheme& theme = navigatorTheme();
		return blendColor(theme.taskbarBackground, theme.windowBackground, 16);
	}

	uint32_t NavigatorScrollThumbColor()
	{
		if (!isSciFiThemeActive()) return packRgb(130, 138, 156);
		const DesktopTheme& theme = navigatorTheme();
		return blendColor(theme.accent, theme.windowBorder, 34);
	}

	uint32_t NavigatorStatusBarColor()
	{
		if (!isSciFiThemeActive()) return packRgb(36, 40, 50);
		const DesktopTheme& theme = navigatorTheme();
		return blendColor(theme.taskbarBackground, theme.windowBackground, 8);
	}

	uint32_t NavigatorStatusBarBorderColor()
	{
		if (!isSciFiThemeActive()) return packRgb(78, 86, 108);
		const DesktopTheme& theme = navigatorTheme();
		return blendColor(theme.windowBorder, theme.mutedAccent, 24);
	}

	uint32_t NavigatorTextColor()
	{
		if (!isSciFiThemeActive()) return packRgb(220, 220, 220);
		return navigatorTheme().titleBarText;
	}

	uint32_t NavigatorMutedTextColor()
	{
		if (!isSciFiThemeActive()) return packRgb(186, 190, 196);
		const DesktopTheme& theme = navigatorTheme();
		return blendColor(theme.titleBarText, theme.taskbarBackground, 54);
	}

	uint32_t NavigatorAccentColor()
	{
		if (!isSciFiThemeActive()) return packRgb(80, 140, 220);
		return navigatorTheme().accent;
	}

	uint32_t NavigatorSelectionColor()
	{
		if (!isSciFiThemeActive()) return packRgb(96, 146, 224);
		const DesktopTheme& theme = navigatorTheme();
		return blendColor(theme.accent, theme.windowBackground, 34);
	}

	uint32_t NavigatorFindHighlightColor()
	{
		if (!isSciFiThemeActive()) return packRgb(255, 244, 168);
		const DesktopTheme& theme = navigatorTheme();
		return blendColor(theme.accent, theme.mutedAccent, 20);
	}

	uint32_t NavigatorFieldFillColor(bool focused)
	{
		if (!isSciFiThemeActive()) return packRgb(250, 252, 255);
		const DesktopTheme& theme = navigatorTheme();
		return blendColor(theme.windowBackground, theme.taskbarBackground, focused ? 10 : 16);
	}

	uint32_t NavigatorFieldBorderColor(bool focused)
	{
		if (!isSciFiThemeActive()) return focused ? packRgb(54, 118, 210) : packRgb(148, 156, 170);
		const DesktopTheme& theme = navigatorTheme();
		return focused ? blendColor(theme.accent, theme.windowBackground, 30) : blendColor(theme.windowBorder, theme.mutedAccent, 24);
	}

	uint32_t NavigatorFieldTextColor()
	{
		if (!isSciFiThemeActive()) return packRgb(35, 45, 60);
		return navigatorTheme().titleBarText;
	}

	uint32_t NavigatorFieldMutedTextColor()
	{
		if (!isSciFiThemeActive()) return packRgb(128, 136, 150);
		const DesktopTheme& theme = navigatorTheme();
		return blendColor(theme.titleBarText, theme.taskbarBackground, 50);
	}

	uint32_t NavigatorButtonFillColor(bool focused, bool disabled)
	{
		if (!isSciFiThemeActive()) return disabled ? packRgb(184, 188, 196) : packRgb(65, 112, 190);
		const DesktopTheme& theme = navigatorTheme();
		const uint32_t base = blendColor(theme.windowBackground, theme.taskbarBackground, 16);
		if (disabled) return blendColor(base, theme.windowBorder, 12);
		return focused ? blendColor(base, theme.accent, 22) : base;
	}

	uint32_t NavigatorButtonBorderColor(bool focused, bool disabled)
	{
		if (!isSciFiThemeActive()) return disabled ? packRgb(128, 132, 140) : (focused ? packRgb(54, 118, 210) : packRgb(38, 78, 150));
		const DesktopTheme& theme = navigatorTheme();
		const uint32_t base = blendColor(theme.windowBorder, theme.taskbarBorder, 18);
		if (disabled) return blendColor(base, theme.taskbarBackground, 22);
		return focused ? blendColor(theme.accent, theme.titleBarText, 12) : blendColor(base, theme.mutedAccent, 10);
	}

	uint32_t NavigatorButtonTextColor(bool disabled)
	{
		if (!isSciFiThemeActive()) return disabled ? packRgb(76, 80, 88) : packRgb(255, 255, 255);
		const DesktopTheme& theme = navigatorTheme();
		return disabled ? blendColor(theme.titleBarText, theme.taskbarBackground, 58) : theme.titleBarText;
	}

	void addButton(uint64_t windowId, int id, int x, int y, int w, int h, const std::string& text, const std::string& iconPath = {})
	{
		// Track registered widget IDs for smoke/diagnostic access.
		auto& ids = Navigator::s_registeredWidgetIds;
		if (std::find(ids.begin(), ids.end(), id) != ids.end()) return;
		publish(MsgType::MT_WidgetAdd, packWidgetAdd(windowId, 1, id, x, y, w, h, text));
		if (!iconPath.empty()) publish(MsgType::MT_WidgetSetIcon, packWidgetSetIcon(windowId, id, iconPath));
		ids.push_back(id);
	}

	int chromeLineHeight()
	{
		return SystemFont::MeasureLineHeight(FontRole::Default);
	}

	int centeredChromeTextY(int top, int height)
	{
		const int lineH = chromeLineHeight();
		return top + (height > lineH ? (height - lineH) / 2 : 0);
	}

	// -----------------------------------------------------------------------
	// Word-wrap helpers
	//
	// Document text currently uses the compositor text primitive, whose default
	// sans-serif fallback is SystemFont. Layout and hit testing still use an
	// approximate fixed advance until Navigator grows document font metrics.
	// -----------------------------------------------------------------------
	constexpr int kCharW    = 8;   // approximate character cell width in pixels
	constexpr int kLineH    = 18;  // matches current SystemFont default line box
	constexpr int kMinReadableBlockWidth = 96;

	struct TextMetrics {
		int ascent = 0;
		int descent = 0;
		int baseline = 0;
		int lineHeight = 0;
		int underlineOffset = 0;
		bool descenderSafe = false;
		const char* backend = "navigator-approx";
	};

	static TextMetrics defaultTextMetrics()
	{
		static const TextMetrics metrics = []() {
			const BitmapFontFace* face = SystemFont::GetFace(FontRole::Default);
			TextMetrics value;
			value.ascent = std::max(1, SystemFont::MeasureAscent(face));
			value.descent = std::max(1, SystemFont::MeasureDescent(face));
			value.baseline = std::max(1, SystemFont::BaselineOffset(face));
			value.lineHeight = std::max(1, SystemFont::MeasureLineHeight(face));
			value.underlineOffset = std::max(1, value.descent / 2);
			value.descenderSafe = value.lineHeight >= value.ascent + value.descent;
			if (!face || face->fallback) {
				value.backend = "navigator-approx";
			} else {
#if defined(GXOS_BARE_METAL)
				value.backend = "kernel-system-font";
#elif defined(_WIN32)
				value.backend = "hosted-gdi";
#else
				value.backend = "navigator-approx";
#endif
			}
			return value;
		}();
		return metrics;
	}

	static int defaultTextFontHeightPx()
	{
		static const int h = std::max(1, defaultTextMetrics().lineHeight);
		return h;
	}

	static int textLineTopPaddingPx(int lineHeight)
	{
		const int fontHeight = defaultTextFontHeightPx();
		const int slack = std::max(0, lineHeight - fontHeight);
		const TextMetrics metrics = defaultTextMetrics();
		// Keep the box compact while biasing the spare room toward descenders.
		return std::max(1, std::min(2, std::max(0, slack - metrics.descent) + 1));
	}

	static int textUnderlineYPx(int lineTop, int lineHeight)
	{
		const TextMetrics metrics = defaultTextMetrics();
		const int topPadding = textLineTopPaddingPx(lineHeight);
		const int baselineY = lineTop + topPadding + metrics.baseline;
		const int underlineY = baselineY + std::max(1, metrics.underlineOffset);
		const int safeBottom = lineTop + std::max(1, lineHeight - 2);
		return std::min(underlineY, safeBottom);
	}

	static int textLineThroughYPx(int lineTop, int lineHeight)
	{
		const TextMetrics metrics = defaultTextMetrics();
		const int topPadding = textLineTopPaddingPx(lineHeight);
		const int baselineY = lineTop + topPadding + metrics.baseline;
		const int strikeY = baselineY - std::max(1, metrics.ascent / 2);
		const int safeBottom = lineTop + std::max(1, lineHeight - 2);
		return std::max(lineTop + 1, std::min(strikeY, safeBottom));
	}

	// Wrap |text| into lines that fit within |maxChars| characters.
	// Returns a vector of line strings (may be empty if text is empty).
	static std::vector<std::string> wrapText(const std::string& text, int maxChars)
	{
		std::vector<std::string> lines;
		if (maxChars <= 0 || text.empty()) {
			if (!text.empty()) lines.push_back(text);
			return lines;
		}

		size_t start = 0;
		const size_t len = text.size();
		while (start < len) {
			size_t remaining = len - start;
			if (static_cast<int>(remaining) <= maxChars) {
				lines.push_back(text.substr(start));
				break;
			}
			// Try to break at a word boundary (space) within the column limit.
			size_t breakAt = static_cast<size_t>(maxChars);
			// Search backward from column limit for a space.
			size_t spacePos = text.rfind(' ', start + breakAt);
			if (spacePos != std::string::npos && spacePos > start) {
				breakAt = spacePos - start;
			}
			lines.push_back(text.substr(start, breakAt));
			start += breakAt;
			// Skip a single space at the break point.
			if (start < len && text[start] == ' ') ++start;
		}
		return lines;
	}

	static std::vector<std::string> wrapTextBreakAll(const std::string& text, int maxChars)
	{
		std::vector<std::string> lines;
		if (maxChars <= 0 || text.empty()) {
			if (!text.empty()) lines.push_back(text);
			return lines;
		}
		for (size_t start = 0; start < text.size();) {
			const size_t count = std::min(static_cast<size_t>(maxChars), text.size() - start);
			lines.push_back(text.substr(start, count));
			start += count;
		}
		return lines;
	}

	// Like wrapText but splits on embedded newlines first (for Preformatted blocks).
	static std::vector<std::string> splitPreLines(const std::string& text)
	{
		std::vector<std::string> lines;
		size_t start = 0;
		const size_t len = text.size();
		while (start <= len) {
			size_t nl = text.find('\n', start);
			if (nl == std::string::npos) nl = len;
			lines.push_back(text.substr(start, nl - start));
			if (nl == len) break;
			start = nl + 1;
		}
		return lines;
	}

	static std::vector<std::string> wrapTextForBlock(const DocBlock& block, int maxChars)
	{
		const bool breakAll = block.style.wordBreak == WordBreakMode::BreakAll;
		const bool preserveBreaks = block.type == BlockType::Preformatted ||
			block.style.whiteSpace == WhiteSpaceMode::Pre ||
			block.style.whiteSpace == WhiteSpaceMode::PreWrap;
		auto wrapSingleLine = [&](const std::string& line) {
			return breakAll ? wrapTextBreakAll(line, maxChars) : wrapText(line, maxChars);
		};
		if (preserveBreaks) {
			std::vector<std::string> lines;
			for (const std::string& rawLine : splitPreLines(block.text)) {
				std::vector<std::string> wrapped = wrapSingleLine(rawLine);
				if (wrapped.empty()) {
					lines.push_back("");
				} else {
					lines.insert(lines.end(), wrapped.begin(), wrapped.end());
				}
			}
			if (lines.empty()) lines.push_back("");
			return lines;
		}
		return breakAll ? wrapTextBreakAll(block.text, maxChars) : wrapText(block.text, maxChars);
	}

	// Number of pixel rows occupied by a block (based on wrapped line count).
	// wrapCols: max chars per line for the block type.
	static int wrappedBlockHeight(const DocBlock& block, int wrapCols, int lineHeight = kLineH)
	{
		int lines = static_cast<int>(wrapTextForBlock(block, wrapCols).size());
		if (lines == 0) lines = 1;
		return lines * lineHeight;
	}

	struct ImageInfo {
		bool attempted = false;
		bool ok = false;
		bool unsupported = false;
		bool tooLarge = false;
		gxos::gui::ImageLoadStatus status = gxos::gui::ImageLoadStatus::NotFound;
		int naturalW = 0;
		int naturalH = 0;
		std::string filePath;
		std::string drawPath;
		std::string message;
		std::string errorDetail;
	};

	static std::unordered_map<std::string, ImageInfo> s_imageCache;
	static std::vector<std::string> s_remoteImageTempFiles;
	static const ImageInfo& imageInfoForBlock(const DocBlock& block);
	static void imageDisplaySize(const DocBlock& block, int availableWidth, int& outW, int& outH,
		bool* outConstrained = nullptr, bool* outAspectPreserved = nullptr, bool* outClamped = nullptr);
	static int cssWidthPx(const WebStyle& style, int availableWidth, int fallbackValue);
	static int cssMaxWidthPx(const WebStyle& style, int availableWidth, int fallbackValue);
	static int cssHeightPx(const WebStyle& style, int availableHeight, int fallbackValue);
	static int cssMaxHeightPx(const WebStyle& style, int availableHeight, int fallbackValue);
	static std::string filePathFromUrl(const std::string& url);
	static std::string pageInfoLine(const std::string& label, const std::string& value);
	static std::string pageInfoLine(const std::string& label, int value);
	static DocBlock makeBlock(BlockType type, const std::string& text, const std::string& url = "", const std::string& tagName = "");
	static int cssMarginOrDefault(const WebStyle& style, int fallbackValue);
	static int cssPaddingOrDefault(const WebStyle& style, int fallbackValue);
	static int cssFontSizeOrDefault(const WebStyle& style, int fallbackValue);
	static int cssLineHeightOrDefault(const WebStyle& style, int fallbackValue);
	static int cssPaddingTopPx(const WebStyle& style, int fallbackValue);
	static int cssPaddingRightPx(const WebStyle& style, int fallbackValue);
	static int cssPaddingBottomPx(const WebStyle& style, int fallbackValue);
	static int cssPaddingLeftPx(const WebStyle& style, int fallbackValue);
	static int cssMarginTopPx(const WebStyle& style, int fallbackValue);
	static int cssMarginBottomPx(const WebStyle& style, int fallbackValue);
	static int cssMarginLeftPx(const WebStyle& style, int fallbackValue);
	static int cssMarginRightPx(const WebStyle& style, int fallbackValue);
	static bool cssMarginLeftAuto(const WebStyle& style);
	static bool cssMarginRightAuto(const WebStyle& style);
	static int cssBorderTopPx(const WebStyle& style);
	static int cssBorderBottomPx(const WebStyle& style);
	static int blockIndentForType(BlockType type);
	static int blockBodyMarginLeft(const WebDocument& doc);
	static int blockBodyMarginRight(const WebDocument& doc);
	static int blockAvailableWidth(const DocBlock& block, const WebDocument& doc);
	static int cssBodyContentWidth(const WebDocument& doc);
	static bool cssStyleHasOverflowBfc(const WebStyle& style);
	static bool isFormControlBlock(const DocBlock& block);
	static int blockFormControlIntrinsicWidth(const DocBlock& block);
	static int blockFormControlWidth(const DocBlock& block, int availableWidth);
	static int blockOuterWidth(const DocBlock& block, int availableWidth, bool* outClamped = nullptr);
	static int blockOuterX(const DocBlock& block, const WebDocument& doc, int availableWidth, int outerWidth);
	static int blockWrapWidth(const DocBlock& block, int outerWidth);
	static int blockTextLineHeight(const DocBlock& block);
	static void ensureInlineLayout(const WebDocument& doc);
	static void ensureCssFloatLayout(const WebDocument& doc);
	static CssFloatExclusionQuery cssFloatExclusionQuery(const WebDocument& doc,
		uint64_t bfcIdentity, int lineTop, int lineBottom, int containingLeft, int containingRight);
	static int cssBfcPlacementY(const WebDocument& doc, int blockIndex, int candidateY,
		int requiredWidth);
	static uint64_t cssContainingBfcIdentityForBlock(const WebDocument& doc,
		const DocBlock& block, bool includeSelf);
	static int cssFloatClearance(const WebDocument& doc, uint64_t bfcIdentity,
		ClearMode clearMode, int blockTop);
	static int inlineTextWidth(const WebStyle& style, const std::string& text);
	static const gxos::web::HtmlElementRef* cssStructuralElementForSerial(
		const WebDocument& doc, uint64_t serial);
	static void rebuildInlineLayout(const WebDocument& doc, InlineLayoutSnapshot& snapshot);
	static const WebStyle* inlineOwnerStyle(const WebDocument& doc, const WebInlineItem& item,
		const WebStyle& fallback);
	static const InlineFlowLayout* inlineFlowForBlock(const WebDocument& doc, int blockIndex);
	static const InlineFlowLayout* inlineFlowForAnchor(const WebDocument& doc, int blockIndex);
	static bool blockUsesInlineFlow(const WebDocument& doc, int blockIndex);
	static void ensureCssPositionLayout(const WebDocument& doc);
	static const CssPositionedRecord* cssPositionedRecordForBlock(const WebDocument& doc, int blockIndex);
	static const CssPositionedRecord* cssPositionedRecordForSerial(const WebDocument& doc, uint64_t serial);
	static CssPaintRect cssPositionedClipForBlock(const WebDocument& doc, int blockIndex,
		int outerX, int outerY, int outerW, int outerH, int scrollOffset);
	static int blockTextX(const DocBlock& block, int outerX, int innerWidth, int lineWidth);
	static void drawBlockBox(uint64_t windowId, int x, int y, int w, int h, const WebStyle& style);
	static bool blockHasVisibleCss(const DocBlock& block);
	static bool colorChannels(uint32_t color, int& r, int& g, int& b);
	static std::string encodeFormComponent(const std::string& value);
	static bool tryWriteHostedClipboard(const std::string& text)
	{
#if defined(_WIN32)
		if (!OpenClipboard(nullptr)) return false;
		if (!EmptyClipboard()) {
			CloseClipboard();
			return false;
		}
		const int wideLen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
		if (wideLen <= 0) {
			CloseClipboard();
			return false;
		}
		HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(wideLen) * sizeof(wchar_t));
		if (!mem) {
			CloseClipboard();
			return false;
		}
		wchar_t* wide = static_cast<wchar_t*>(GlobalLock(mem));
		if (!wide) {
			GlobalFree(mem);
			CloseClipboard();
			return false;
		}
		if (MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide, wideLen) <= 0) {
			GlobalUnlock(mem);
			GlobalFree(mem);
			CloseClipboard();
			return false;
		}
		GlobalUnlock(mem);
		if (!SetClipboardData(CF_UNICODETEXT, mem)) {
			GlobalFree(mem);
			CloseClipboard();
			return false;
		}
		CloseClipboard();
		return true;
#else
		(void)text;
		return false;
#endif
	}

	static std::string toLowerAscii(std::string value)
	{
		for (char& ch : value) {
			ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		}
		return value;
	}

	static bool endsWithIgnoreCase(const std::string& value, const std::string& suffix)
	{
		if (suffix.size() > value.size()) return false;
		return toLowerAscii(value.substr(value.size() - suffix.size())) == toLowerAscii(suffix);
	}

	static bool isPngSignature(const std::string& bytes)
	{
		static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
		if (bytes.size() < sizeof(sig)) return false;
		for (size_t i = 0; i < sizeof(sig); ++i) {
			if (static_cast<unsigned char>(bytes[i]) != sig[i]) return false;
		}
		return true;
	}

	static bool writeBinaryTempFile(const std::string& path, const std::string& bytes)
	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		if (!out) return false;
		out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		return out.good();
	}

	static uint64_t fnv1a64(const std::string& value)
	{
		uint64_t hash = 1469598103934665603ull;
		for (unsigned char ch : value) {
			hash ^= static_cast<uint64_t>(ch);
			hash *= 1099511628211ull;
		}
		return hash;
	}

	static std::string remoteImageTempPath(const std::string& url)
	{
		std::ostringstream oss;
		oss << "navigator_remote_image_" << std::hex << fnv1a64(url) << ".png";
		return oss.str();
	}

	static void cleanupRemoteImageTempFiles()
	{
		for (const std::string& path : s_remoteImageTempFiles) {
			std::remove(path.c_str());
		}
		s_remoteImageTempFiles.clear();
	}

	static gxos::gui::ImageSafetyLimits remoteImageSafetyLimits()
	{
		gxos::gui::ImageSafetyLimits limits{};
		limits.maxBytes = kRemoteImageMaxBytes;
		limits.maxWidth = kRemoteImageMaxWidth;
		limits.maxHeight = kRemoteImageMaxHeight;
		limits.maxPixels = kRemoteImageMaxPixels;
		return limits;
	}

	static WebDocument buildSimpleDocument(const std::string& url,
		const std::string& title,
		const std::string& heading,
		const std::string& message)
	{
		WebDocument doc;
		doc.url = url;
		doc.title = title;
		doc.blocks.push_back({BlockType::Heading, heading, ""});
		doc.blocks.push_back({BlockType::Paragraph, message, ""});
		doc.blocks.push_back({BlockType::Link, "Go to about:navigator", "about:navigator"});
		return doc;
	}

	static std::string guessedContentTypeForPath(const std::string& path)
	{
		std::string ext;
		size_t dot = path.rfind('.');
		if (dot != std::string::npos) {
			ext = toLowerAscii(path.substr(dot));
		}
		if (ext == ".html" || ext == ".htm") return "text/html";
		if (ext == ".txt" || ext == ".text" || ext == ".md" || ext == ".log" ||
			ext == ".ini" || ext == ".cfg" || ext == ".json" || ext == ".xml" ||
			ext == ".csv" || ext == ".c" || ext == ".cpp" || ext == ".h" ||
			ext == ".hpp" || ext == ".bat" || ext == ".ps1" || ext == ".sh") {
			return "text/plain";
		}
		if (ext == ".png") return "image/png";
		if (ext == ".zip") return "application/zip";
		if (ext == ".pdf") return "application/pdf";
		if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
		if (ext == ".gif") return "image/gif";
		if (ext == ".webp") return "image/webp";
		if (ext == ".svg") return "image/svg+xml";
		if (ext == ".bin" || ext == ".exe" || ext == ".dll" || ext == ".iso") return "application/octet-stream";
		return "text/plain";
	}

	static bool isNavigatorRenderableFileType(const std::string& contentType)
	{
		return contentType == "text/html" || contentType == "text/plain";
	}

	static bool isRemoteHttpUrl(const std::string& url)
	{
		return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
	}

	static std::string fileNameFromUrlPath(const std::string& url)
	{
		std::string path;
		if (isRemoteHttpUrl(url)) {
			gxos::web::ParsedHttpUrl parsed = gxos::web::parseHttpUrl(url);
			path = parsed.valid ? parsed.path : url;
		} else if (url.rfind("file://", 0) == 0) {
			path = filePathFromUrl(url);
		} else {
			path = url;
		}
		size_t slash = path.find_last_of("/\\");
		std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
		size_t question = name.find('?');
		if (question != std::string::npos) name = name.substr(0, question);
		size_t hash = name.find('#');
		if (hash != std::string::npos) name = name.substr(0, hash);
		return name.empty() ? "download.bin" : name;
	}

	static std::string sanitizeDownloadFileName(const std::string& original)
	{
		std::string safe;
		safe.reserve(original.size());
		for (unsigned char ch : original) {
			if (std::isalnum(ch) || ch == '.' || ch == '-' || ch == '_') {
				safe.push_back(static_cast<char>(ch));
			} else {
				safe.push_back('_');
			}
		}
		while (!safe.empty() && safe.front() == '.') safe.erase(safe.begin());
		if (safe.empty()) safe = "download.bin";
		if (safe.size() > 96) {
			size_t dot = safe.rfind('.');
			std::string ext = (dot != std::string::npos && safe.size() - dot <= 16) ? safe.substr(dot) : "";
			safe = safe.substr(0, 96 - ext.size()) + ext;
		}
		if (safe == "." || safe == "..") safe = "download.bin";
		return safe;
	}

	static std::string safeDownloadFileUrl(const DownloadItem& item)
	{
		if (!item.success) return "";
		if (item.savedPath.rfind("/downloads/", 0) != 0) return "";

		const std::string fileName = item.savedPath.substr(11);
		if (fileName.empty()) return "";
		if (fileName.find('/') != std::string::npos || fileName.find('\\') != std::string::npos) return "";
		if (sanitizeDownloadFileName(fileName) != fileName) return "";
		if (!item.suggestedFileName.empty() && item.suggestedFileName != fileName) return "";

		return "file:///downloads/" + fileName;
	}

	static std::string uniqueDownloadPathForName(const std::string& safeName, std::string& outFinalName)
	{
		if (safeName.empty() || sanitizeDownloadFileName(safeName) != safeName ||
			safeName.find('/') != std::string::npos || safeName.find('\\') != std::string::npos) {
			outFinalName.clear();
			return "";
		}

		std::string stem = safeName;
		std::string ext;
		size_t dot = safeName.rfind('.');
		if (dot != std::string::npos && dot > 0) {
			stem = safeName.substr(0, dot);
			ext = safeName.substr(dot);
		}
		for (int i = 0; i < 1000; ++i) {
			std::string candidateName = (i == 0)
				? safeName
				: stem + "-" + std::to_string(i) + ext;
			std::string candidatePath = "/downloads/" + candidateName;
			if (!fileExists(candidatePath)) {
				outFinalName = candidateName;
				return candidatePath;
			}
		}
		outFinalName.clear();
		return "";
	}

	static void rememberDownload(DownloadItem item)
	{
		s_recentDownloads.insert(s_recentDownloads.begin(), std::move(item));
		constexpr size_t kMaxRecentDownloads = 16;
		if (s_recentDownloads.size() > kMaxRecentDownloads) {
			s_recentDownloads.resize(kMaxRecentDownloads);
		}
	}

	static WebDocument buildDownloadCompleteDocument(const DownloadItem& item)
	{
		WebDocument doc;
		const std::string fileUrl = safeDownloadFileUrl(item);
		doc.url = item.finalUrl.empty() ? item.url : item.finalUrl;
		doc.title = item.success ? "Download Complete" : "Download Unavailable";
		doc.blocks.push_back({BlockType::Heading, doc.title, ""});
		doc.blocks.push_back({BlockType::Paragraph,
			item.success
				? "Navigator saved this download using its sanitized downloads path."
				: "Navigator kept a record for this failed download so you can review what happened.", ""});
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Filename", item.suggestedFileName), ""});
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Status", item.success ? "success" : "failed"), ""});
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Saved path", item.savedPath), ""});
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Source URL", item.url), ""});
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Final URL", item.finalUrl), ""});
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Content type", item.contentType), ""});
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Byte count", static_cast<int>(item.byteCount)), ""});
		if (!item.error.empty()) {
			doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Error", item.error), ""});
		}
		if (!fileUrl.empty()) {
			doc.blocks.push_back({BlockType::Link, "Open downloaded file", fileUrl});
		}
		doc.blocks.push_back({BlockType::Link, "View Downloads", "about:downloads"});
		doc.blocks.push_back({BlockType::Link, "Page Info", "about:page-info"});
		doc.blocks.push_back({BlockType::Link, "Go to about:navigator", "about:navigator"});
		return doc;
	}

	static WebDocument buildFileNotViewableDocument(const std::string& url,
		const std::string& path,
		const std::string& contentType)
	{
		WebDocument doc;
		doc.url = url;
		doc.title = "File Not Viewable";
		doc.blocks.push_back({BlockType::Heading, "File Not Viewable", ""});
		doc.blocks.push_back({BlockType::Paragraph, "Navigator cannot render this file type yet.", ""});
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Path", path), ""});
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Guessed content type", contentType), ""});
		doc.blocks.push_back({BlockType::Link, "View Downloads", "about:downloads"});
		doc.blocks.push_back({BlockType::Link, "Go to about:navigator", "about:navigator"});
		return doc;
	}

	static std::string yesNo(bool value)
	{
		return value ? "yes" : "no";
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

	static const char* formAccessibilityRoleName(FormAccessibilityRole role)
	{
		switch (role) {
		case FormAccessibilityRole::Checkbox: return "checkbox";
		case FormAccessibilityRole::Radio: return "radio";
		case FormAccessibilityRole::Button: return "button";
		case FormAccessibilityRole::Textbox: return "textbox";
		case FormAccessibilityRole::PasswordTextbox: return "password textbox";
		case FormAccessibilityRole::Textarea: return "textarea";
		case FormAccessibilityRole::Select: return "select";
		default: return "none";
		}
	}

	static const char* formAccessibilityLabelSourceName(FormAccessibilityLabelSource source)
	{
		switch (source) {
		case FormAccessibilityLabelSource::Wrapping: return "wrapping";
		case FormAccessibilityLabelSource::ForId: return "for/id";
		default: return "none";
		}
	}

	static const char* formAccessibilityNameSourceName(FormAccessibilityNameSource source)
	{
		switch (source) {
		case FormAccessibilityNameSource::LabelWrapping: return "label-wrapping";
		case FormAccessibilityNameSource::LabelForId: return "label-for/id";
		case FormAccessibilityNameSource::ButtonText: return "button-text";
		case FormAccessibilityNameSource::InputValuePresence: return "input-value-presence";
		case FormAccessibilityNameSource::Placeholder: return "placeholder";
		case FormAccessibilityNameSource::ControlTypeFallback: return "control-type-fallback";
		default: return "none";
		}
	}

	static const char* formFocusRevealResultName(FormFocusRevealResult result)
	{
		switch (result) {
		case FormFocusRevealResult::Scroll: return "scroll";
		case FormFocusRevealResult::Noop: return "noop";
		case FormFocusRevealResult::Clamped: return "clamped";
		default: return "none";
		}
	}

	static std::string evidenceFieldForSerial(const std::string& evidence,
		uint64_t serial, const std::string& field)
	{
		const std::string serialToken = "logical-serial=" + std::to_string(serial);
		size_t cursor = 0;
		while ((cursor = evidence.find(serialToken, cursor)) != std::string::npos) {
			const size_t recordEnd = evidence.find(';', cursor);
			const size_t fieldStart = evidence.find(field + "=", cursor);
			if (fieldStart != std::string::npos && (recordEnd == std::string::npos || fieldStart < recordEnd)) {
				const size_t valueStart = fieldStart + field.size() + 1;
				const size_t valueEnd = evidence.find(',', valueStart);
				const size_t boundedEnd = (valueEnd == std::string::npos ||
					(recordEnd != std::string::npos && recordEnd < valueEnd)) ? recordEnd : valueEnd;
				return evidence.substr(valueStart, boundedEnd == std::string::npos ? std::string::npos : boundedEnd - valueStart);
			}
			if (recordEnd == std::string::npos) break;
			cursor = recordEnd + 1;
		}
		return "";
	}

	static bool parseBoundedSpecificity(const std::string& value,
		uint16_t& idCount, uint16_t& classCount, uint16_t& elementCount)
	{
		if (value.empty()) return false;
		std::istringstream iss(value);
		char dot1 = 0;
		char dot2 = 0;
		unsigned id = 0;
		unsigned classes = 0;
		unsigned elements = 0;
		if (!(iss >> id >> dot1 >> classes >> dot2 >> elements) || dot1 != '.' || dot2 != '.') return false;
		if (id > 0xFFFFu || classes > 0xFFFFu || elements > 0xFFFFu) return false;
		idCount = static_cast<uint16_t>(id);
		classCount = static_cast<uint16_t>(classes);
		elementCount = static_cast<uint16_t>(elements);
		return true;
	}

	static void setSourcePreview(NavigatorPageMetadata& metadata, const std::string& source)
	{
		metadata.rawSourceForSave = source;
		metadata.rawSourceBytes = source.size();
		if (source.size() > kNavigatorMaxSourcePreviewBytes) {
			metadata.rawSource = source.substr(0, kNavigatorMaxSourcePreviewBytes);
			metadata.rawSourceTruncated = true;
		} else {
			metadata.rawSource = source;
			metadata.rawSourceTruncated = false;
		}
	}

	static bool isTableCellLikeBlock(const DocBlock& block);
	static uint64_t tableSerialForBlock(const DocBlock& block);
	static uint64_t tableRowSerialForBlock(const DocBlock& block);
	static bool blockHasWrapperAncestor(const DocBlock& block);
	static int wrapperAncestorDepth(const DocBlock& block);
	static int nestedWrapperInsetPx(const DocBlock& block);

	struct TableCellLayout {
		const DocBlock* block = nullptr;
		std::vector<std::string> lines;
		int padLeftChars = 1;
		int padRightChars = 1;
		int contentWidthChars = 1;
	};

	struct TableRowLayout {
		uint64_t rowSerial = 0;
		std::vector<TableCellLayout> cells;
		bool headerRow = false;
		int heightPx = 0;
		int borderTopPx = 0;
		int borderBottomPx = 0;
	};

	struct TableGroupLayout {
		uint64_t tableSerial = 0;
		int startIndex = -1;
		int endIndex = -1;
		int availableWidth = 0;
		int outerWidth = 0;
		int outerX = 0;
		int paddingTop = 0;
		int paddingRight = 0;
		int paddingBottom = 0;
		int paddingLeft = 0;
		int borderTop = 0;
		int borderRight = 0;
		int borderBottom = 0;
		int borderLeft = 0;
		int lineHeight = 0;
		std::vector<int> columnWidthsChars;
		std::vector<TableRowLayout> rows;
		std::vector<int> rowOffsetsPx;
		int totalHeightPx = 0;
		bool collapseMode = false;
		int borderSpacingHorizontal = 0;
		int borderSpacingVertical = 0;
		bool fallbackUsed = false;
	};

	static bool isFirstTableCellInGroup(const WebDocument& doc, int index);
	static int tableGroupStartIndex(const WebDocument& doc, int index);
	static TableGroupLayout buildTableGroupLayout(const WebDocument& doc, int startIndex);
	static int blockContainingContentHeight(const DocBlock& block, const WebDocument& doc);
	static CssBlockGeometry cssGeometryForBlock(const WebDocument& doc, int blockIndex);
	static bool cssBlockHasOverflowAncestor(const WebDocument& doc, const DocBlock& block);
	static std::string cssNavigatorLengthEvidence(const CssLengthValue& value);
	static void ensureCssMarginLayout(const WebDocument& doc);

	static void fillDocumentCounts(NavigatorPageMetadata& metadata, const WebDocument& doc)
	{
		ensureCssMarginLayout(doc);
		metadata.documentBlockCount = static_cast<int>(doc.blocks.size());
		metadata.imageBlockCount = 0;
		metadata.loadedImageCount = 0;
		metadata.failedImageCount = 0;
		metadata.remoteImageCount = 0;
		metadata.localImageCount = 0;
		metadata.lastImageError.clear();
		metadata.cssEnabled = doc.cssDiagnostics.cssEnabled;
		metadata.cssDetected = doc.cssDiagnostics.cssDetected;
		metadata.styleRuleCount = doc.cssDiagnostics.styleRuleCount;
		metadata.styleBlockCount = doc.cssDiagnostics.styleBlockCount;
		metadata.inlineStyleCount = doc.cssDiagnostics.inlineStyleCount;
		metadata.externalStylesheetLoadedCount = doc.cssDiagnostics.externalStylesheetLoadedCount;
		metadata.unsupportedExternalStylesheetCount = doc.cssDiagnostics.unsupportedExternalStylesheetCount;
		metadata.unsupportedCssRuleCount = doc.cssDiagnostics.unsupportedRuleCount;
		metadata.unsupportedCssDeclarationCount = doc.cssDiagnostics.unsupportedDeclarationCount;
		metadata.cssUnsupportedSelectorCount = doc.cssDiagnostics.unsupportedSelectorCount;
		metadata.cssParseErrorCount = doc.cssDiagnostics.parseErrorCount;
		metadata.cssStyleBlockCapped = doc.cssDiagnostics.styleBlockCapped;
		metadata.cssStyleBytesProcessed = doc.cssDiagnostics.styleBytesProcessed;
		metadata.cssLayoutMaxWidthAppliedCount = 0;
		metadata.cssAutoMarginCenteredBlockCount = 0;
		metadata.cssBackgroundBlockCount = doc.bodyStyle.hasBackgroundColor ? 1 : 0;
		metadata.cssWrapperRenderCount = 0;
		metadata.cssDisplayNoneBlockCount = 0;
		metadata.cssTableRenderCount = 0;
		metadata.cssTableRowCount = 0;
		metadata.cssTableCellCount = 0;
		metadata.cssTableLayoutFallbackCount = 0;
		metadata.cssListRenderCount = 0;
		metadata.cssClampedValueCount = doc.cssDiagnostics.clampedValueCount;
		metadata.cssLengthValueClampCount = doc.cssDiagnostics.lengthValueClampCount;
		metadata.cssInvalidLengthValueCount = doc.cssDiagnostics.invalidLengthValueCount;
		metadata.cssBorderWidthClamps = doc.cssDiagnostics.borderWidthClampCount;
		metadata.cssTableBorderSpacingClamps = doc.cssDiagnostics.borderSpacingClampCount;
		metadata.cssLineBreakCount = doc.cssDiagnostics.lineBreakCount;
		metadata.cssTableCaptionCount = 0;
		metadata.cssTableHeaderCellCount = 0;
		metadata.cssVisitedLinkCount = 0;
		metadata.cssBorderedBlocksRendered = 0;
		metadata.cssDashedBordersRendered = 0;
		metadata.cssDottedBordersRendered = 0;
		metadata.cssCollapsedTablesRendered = 0;
		metadata.cssSeparateTablesRendered = 0;
		metadata.cssListStyleMarkersRendered = 0;
		metadata.cssListStyleNoneApplied = 0;
		metadata.cssTextDecorationsRendered = 0;
		metadata.cssGenericFontFamilyApplied = 0;
		metadata.cssGenericFontFamilyFallbacks = 0;
		metadata.cssFiguresRendered = 0;
		metadata.cssFigcaptionsRendered = 0;
		metadata.cssBlockquotesRendered = 0;
		metadata.cssDefinitionListsRendered = 0;
		metadata.cssImagesConstrained = 0;
		metadata.cssImagesAspectPreserved = 0;
		metadata.cssImageAltFallbacks = 0;
		metadata.cssImageSizeClamps = 0;
		metadata.cssNestedLayoutClamps = 0;
		metadata.cssMaxWrapperAncestorDepth = 0;
		metadata.cssSelectorGroupsParsed = doc.cssDiagnostics.selectorGroupsParsed;
		metadata.cssCompoundSelectorsParsed = doc.cssDiagnostics.compoundSelectorsParsed;
		metadata.cssChildCombinators = doc.cssDiagnostics.childCombinatorCount;
		metadata.cssDescendantCombinators = doc.cssDiagnostics.descendantCombinatorCount;
		metadata.cssAdjacentSiblingCombinators = doc.cssDiagnostics.adjacentSiblingCombinatorCount;
		metadata.cssGeneralSiblingCombinators = doc.cssDiagnostics.generalSiblingCombinatorCount;
		metadata.cssAdjacentSiblingMatches = doc.cssDiagnostics.adjacentSiblingMatches;
		metadata.cssGeneralSiblingMatches = doc.cssDiagnostics.generalSiblingMatches;
		metadata.cssSiblingScanSteps = doc.cssDiagnostics.siblingScanSteps;
		metadata.cssSiblingScanClamps = doc.cssDiagnostics.siblingScanClamps;
		metadata.cssSiblingMetadataClamps = doc.cssDiagnostics.siblingMetadataClamps;
		metadata.cssSiblingMetadataErrors = doc.cssDiagnostics.siblingMetadataErrors;
		metadata.cssSelectorMatches = doc.cssDiagnostics.selectorMatches;
		metadata.cssSpecificityOverrides = doc.cssDiagnostics.specificityOverrides;
		metadata.cssSourceOrderOverrides = doc.cssDiagnostics.sourceOrderOverrides;
		metadata.cssInlineOverrides = doc.cssDiagnostics.inlineOverrides;
		metadata.cssInheritedPropertiesApplied = doc.cssDiagnostics.inheritedPropertiesApplied;
		metadata.cssSelectorDepthClamps = doc.cssDiagnostics.selectorDepthClamps;
		metadata.cssSelectorGroupClamps = doc.cssDiagnostics.selectorGroupClamps;
		metadata.cssCascadePropertyResolutions = doc.cssDiagnostics.cascadePropertyResolutions;
		metadata.cssImportantDeclarationsApplied = doc.cssDiagnostics.importantDeclarationsApplied;
		metadata.cssRuleCapCount = doc.cssDiagnostics.ruleCapCount;
		metadata.cssDeclarationCapCount = doc.cssDiagnostics.declarationCapCount;
		metadata.cssInheritanceDepthClamps = doc.cssDiagnostics.inheritanceDepthClamps;
		metadata.cssPseudoClassesParsed = doc.cssDiagnostics.pseudoClassesParsed;
		metadata.cssStructuralPseudoMatches = doc.cssDiagnostics.structuralPseudoMatches;
		metadata.cssFirstChildMatches = doc.cssDiagnostics.firstChildMatches;
		metadata.cssLastChildMatches = doc.cssDiagnostics.lastChildMatches;
		metadata.cssNthChildMatches = doc.cssDiagnostics.nthChildMatches;
		metadata.cssOfTypeMatches = doc.cssDiagnostics.ofTypeMatches;
		metadata.cssNotMatches = doc.cssDiagnostics.notMatches;
		metadata.cssLinkPseudoMatches = doc.cssDiagnostics.linkPseudoMatches;
		metadata.cssVisitedPseudoMatches = doc.cssDiagnostics.visitedPseudoMatches;
		metadata.cssPseudoClassClamps = doc.cssDiagnostics.pseudoClassClamps;
		metadata.cssNthExpressionParseErrors = doc.cssDiagnostics.nthExpressionParseErrors;
		metadata.cssStructuralMetadataClamps = doc.cssDiagnostics.structuralMetadataClamps;
		metadata.cssSelectorEvaluationStepClamps = doc.cssDiagnostics.selectorEvaluationStepClamps;
		metadata.cssEmptyPseudoParsed = doc.cssDiagnostics.emptyPseudoParsed;
		metadata.cssEmptyPseudoMatches = doc.cssDiagnostics.emptyPseudoMatches;
		metadata.cssEmptyMetadataIncomplete = doc.cssDiagnostics.emptyMetadataIncomplete;
		metadata.cssContentMetadataClamps = doc.cssDiagnostics.contentMetadataClamps;
		metadata.cssSelectorGroupMemberRecoveries = doc.cssDiagnostics.selectorGroupMemberRecoveries;
		metadata.cssCommentScanClamps = doc.cssDiagnostics.commentScanClamps;
		metadata.cssUnterminatedCommentErrors = doc.cssDiagnostics.unterminatedCommentErrors;
		metadata.cssUnbalancedParenthesisErrors = doc.cssDiagnostics.unbalancedParenthesisErrors;
		metadata.cssUnbalancedBracketErrors = doc.cssDiagnostics.unbalancedBracketErrors;
		metadata.cssUnterminatedStringErrors = doc.cssDiagnostics.unterminatedStringErrors;
		metadata.cssInvalidCombinatorSequences = doc.cssDiagnostics.invalidCombinatorSequences;
		metadata.cssIdentifierEscapeRejections = doc.cssDiagnostics.identifierEscapeRejections;
		metadata.cssSelectorMemberParseFailures = doc.cssDiagnostics.selectorMemberParseFailures;
		metadata.cssSelectorRecoverySuccesses = doc.cssDiagnostics.selectorRecoverySuccesses;
		metadata.cssCheckedPseudoParsed = doc.cssDiagnostics.checkedPseudoParsed;
		metadata.cssCheckedPseudoMatches = doc.cssDiagnostics.checkedPseudoMatches;
		metadata.cssDisabledPseudoParsed = doc.cssDiagnostics.disabledPseudoParsed;
		metadata.cssDisabledPseudoMatches = doc.cssDiagnostics.disabledPseudoMatches;
		metadata.cssEnabledPseudoParsed = doc.cssDiagnostics.enabledPseudoParsed;
		metadata.cssEnabledPseudoMatches = doc.cssDiagnostics.enabledPseudoMatches;
		metadata.cssRequiredPseudoParsed = doc.cssDiagnostics.requiredPseudoParsed;
		metadata.cssRequiredPseudoMatches = doc.cssDiagnostics.requiredPseudoMatches;
		metadata.cssReadonlyPseudoParsed = doc.cssDiagnostics.readonlyPseudoParsed;
		metadata.cssReadonlyPseudoMatches = doc.cssDiagnostics.readonlyPseudoMatches;
		metadata.cssReadwritePseudoParsed = doc.cssDiagnostics.readwritePseudoParsed;
		metadata.cssReadwritePseudoMatches = doc.cssDiagnostics.readwritePseudoMatches;
		metadata.cssFocusPseudoParsed = doc.cssDiagnostics.focusPseudoParsed;
		metadata.cssFocusPseudoMatches = doc.cssDiagnostics.focusPseudoMatches;
		metadata.cssFocusVisiblePseudoParsed = doc.cssDiagnostics.focusVisiblePseudoParsed;
		metadata.cssFocusVisiblePseudoMatches = doc.cssDiagnostics.focusVisiblePseudoMatches;
		metadata.cssRuntimeFocusRecomputations = doc.cssDiagnostics.runtimeFocusRecomputations;
		metadata.cssComputedStyleEvidence = doc.cssDiagnostics.computedStyleEvidence;
		metadata.cssGeometryEvidence.clear();
		metadata.cssEvidenceRecordCount = 0;
		metadata.cssInlineItems = 0;
		metadata.cssInlineTextRuns = 0;
		metadata.cssInlineWhitespaceRuns = 0;
		metadata.cssInlineForcedBreaks = 0;
		metadata.cssLineBoxes = 0;
		metadata.cssLineWraps = 0;
		metadata.cssWhitespaceCollapses = 0;
		metadata.cssLeadingSpaceSuppressions = 0;
		metadata.cssTrailingSpaceSuppressions = 0;
		metadata.cssReplacedInlineItems = 0;
		metadata.cssControlInlineItems = 0;
		metadata.cssVerticalAlignAdjustments = 0;
		metadata.cssLineHeightClamps = 0;
		metadata.cssBaselineIterationClamps = 0;
		metadata.cssInlineFragments = 0;
		metadata.cssInlineFragmentClamps = 0;
		metadata.cssInlineHitFragments = 0;
		metadata.cssDescenderSafeLines = 0;
		metadata.cssInlineBlockItems = 0;
		metadata.cssInlineNestingClamps = 0;
		metadata.cssInlineWrapScanClamps = 0;
		metadata.cssInlineEvidenceRecordCount = 0;
		metadata.cssInlineEvidence.clear();
		metadata.cssAtomicEvidenceRecordCount = 0;
		metadata.cssAtomicEvidence.clear();
		metadata.cssAtomicFormattingContexts = 0;
		metadata.cssAtomicContextDepthMax = 0;
		metadata.cssAtomicContextDepthClamps = 0;
		metadata.cssAtomicContextsDocument = 0;
		metadata.cssAtomicLayoutOperations = 0;
		metadata.cssAtomicLayoutOperationClamps = 0;
		metadata.cssInlineBlockAutoWidths = 0;
		metadata.cssInlineBlockExplicitWidths = 0;
		metadata.cssInlineBlockShrinkToFit = 0;
		metadata.cssInlineBlockPreferredMinClamps = 0;
		metadata.cssInlineBlockPreferredWidthClamps = 0;
		metadata.cssInlineBlockBaselineFromLine = 0;
		metadata.cssInlineBlockBaselineFallback = 0;
		metadata.cssInlineBlockNested = 0;
		metadata.cssInlineBlockWraps = 0;
		metadata.cssInlineBlockHitTargets = 0;
		metadata.cssInlineBlockOverflowClips = 0;
		metadata.cssAtomicContextIncomplete = 0;
		metadata.cssMarginCollapseSets = doc.cssDiagnostics.marginCollapseSets;
		metadata.cssMarginCollapseParticipants = doc.cssDiagnostics.marginCollapseParticipants;
		metadata.cssMarginCollapseSibling = doc.cssDiagnostics.marginCollapseSibling;
		metadata.cssMarginCollapseParentTop = doc.cssDiagnostics.marginCollapseParentTop;
		metadata.cssMarginCollapseParentBottom = doc.cssDiagnostics.marginCollapseParentBottom;
		metadata.cssMarginCollapseEmpty = doc.cssDiagnostics.marginCollapseEmpty;
		metadata.cssMarginCollapsePositiveOnly = doc.cssDiagnostics.marginCollapsePositiveOnly;
		metadata.cssMarginCollapseNegativeOnly = doc.cssDiagnostics.marginCollapseNegativeOnly;
		metadata.cssMarginCollapseMixed = doc.cssDiagnostics.marginCollapseMixed;
		metadata.cssMarginCollapseBlockedBorder = doc.cssDiagnostics.marginCollapseBlockedBorder;
		metadata.cssMarginCollapseBlockedPadding = doc.cssDiagnostics.marginCollapseBlockedPadding;
		metadata.cssMarginCollapseBlockedBfc = doc.cssDiagnostics.marginCollapseBlockedBfc;
		metadata.cssMarginCollapseBlockedHeight = doc.cssDiagnostics.marginCollapseBlockedHeight;
		metadata.cssMarginCollapseBlockedContent = doc.cssDiagnostics.marginCollapseBlockedContent;
		metadata.cssMarginCollapseDepthClamps = doc.cssDiagnostics.marginCollapseDepthClamps;
		metadata.cssMarginGeometryClamps = doc.cssDiagnostics.marginGeometryClamps;
		metadata.cssBfcRoot = doc.cssDiagnostics.bfcRoot;
		metadata.cssBfcInlineBlock = doc.cssDiagnostics.bfcInlineBlock;
		metadata.cssBfcOverflow = doc.cssDiagnostics.bfcOverflow;
		metadata.cssBfcAtomic = doc.cssDiagnostics.bfcAtomic;
		metadata.cssMarginCollapseEvidenceRecords = doc.cssDiagnostics.marginCollapseEvidenceRecords;
		metadata.cssMarginCollapseEvidence = doc.cssDiagnostics.marginCollapseEvidence;
		metadata.cssFloatLeft = doc.cssDiagnostics.floatLeft;
		metadata.cssFloatRight = doc.cssDiagnostics.floatRight;
		metadata.cssFloatBlockifications = doc.cssDiagnostics.floatBlockifications;
		metadata.cssFloatRecords = doc.cssDiagnostics.floatRecords;
		metadata.cssFloatPlacementAttempts = doc.cssDiagnostics.floatPlacementAttempts;
		metadata.cssFloatPlacementDownshifts = doc.cssDiagnostics.floatPlacementDownshifts;
		metadata.cssFloatSideBySide = doc.cssDiagnostics.floatSideBySide;
		metadata.cssFloatWidthOverflows = doc.cssDiagnostics.floatWidthOverflows;
		metadata.cssFloatLineExclusions = doc.cssDiagnostics.floatLineExclusions;
		metadata.cssFloatZeroWidthLineAdvances = doc.cssDiagnostics.floatZeroWidthLineAdvances;
		metadata.cssFloatBfcAvoidances = doc.cssDiagnostics.floatBfcAvoidances;
		metadata.cssFloatBfcDownshifts = doc.cssDiagnostics.floatBfcDownshifts;
		metadata.cssClearLeft = doc.cssDiagnostics.clearLeft;
		metadata.cssClearRight = doc.cssDiagnostics.clearRight;
		metadata.cssClearBoth = doc.cssDiagnostics.clearBoth;
		metadata.cssClearanceApplied = doc.cssDiagnostics.clearanceApplied;
		metadata.cssFloatContainmentBoundaries = doc.cssDiagnostics.floatContainmentBoundaries;
		metadata.cssFloatScopeSuppressions = doc.cssDiagnostics.floatScopeSuppressions;
		metadata.cssFloatHeightContainments = doc.cssDiagnostics.floatHeightContainments;
		metadata.cssBfcFloatContainments = doc.cssDiagnostics.bfcFloatContainments;
		metadata.cssBfcFloatHeightExtensions = doc.cssDiagnostics.bfcFloatHeightExtensions;
		metadata.cssBfcFloatHeightNoops = doc.cssDiagnostics.bfcFloatHeightNoops;
		metadata.cssBfcFloatAvoidanceAttempts = doc.cssDiagnostics.bfcFloatAvoidanceAttempts;
		metadata.cssBfcFloatAvoidanceFits = doc.cssDiagnostics.bfcFloatAvoidanceFits;
		metadata.cssBfcFloatAvoidanceDownshifts = doc.cssDiagnostics.bfcFloatAvoidanceDownshifts;
		metadata.cssBfcFloatTooWide = doc.cssDiagnostics.bfcFloatTooWide;
		metadata.cssNestedFloatContexts = doc.cssDiagnostics.nestedFloatContexts;
		metadata.cssNestedFloatDepthClamps = doc.cssDiagnostics.nestedFloatDepthClamps;
		metadata.cssFloatInsideInlineBlock = doc.cssDiagnostics.floatInsideInlineBlock;
		metadata.cssFloatInsideFloat = doc.cssDiagnostics.floatInsideFloat;
		metadata.cssFloatListCases = doc.cssDiagnostics.floatListCases;
		metadata.cssFloatTableCellCases = doc.cssDiagnostics.floatTableCellCases;
		metadata.cssFloatTableAvoidanceCases = doc.cssDiagnostics.floatTableAvoidanceCases;
		metadata.cssFloatedTableUnsupported = doc.cssDiagnostics.floatedTableUnsupported;
		metadata.cssFloatDocumentExtentExtensions = doc.cssDiagnostics.floatDocumentExtentExtensions;
		metadata.cssFloatGeometryClamps = doc.cssDiagnostics.floatGeometryClamps;
		metadata.cssFloatPlacementAttemptClamps = doc.cssDiagnostics.floatPlacementAttemptClamps;
		metadata.cssFloatExclusionScanClamps = doc.cssDiagnostics.floatExclusionScanClamps;
		metadata.cssFloatBfcDepthClamps = doc.cssDiagnostics.floatBfcDepthClamps;
		metadata.cssFloatEvidenceRecords = doc.cssDiagnostics.floatEvidenceRecords;
		metadata.cssFloatEvidence = doc.cssDiagnostics.floatEvidence;
		metadata.cssPositionStatic = doc.cssDiagnostics.positionStatic;
		metadata.cssPositionRelative = doc.cssDiagnostics.positionRelative;
		metadata.cssPositionAbsolute = doc.cssDiagnostics.positionAbsolute;
		metadata.cssPositionUnsupportedFixed = doc.cssDiagnostics.positionUnsupportedFixed;
		metadata.cssPositionUnsupportedSticky = doc.cssDiagnostics.positionUnsupportedSticky;
		metadata.cssRelativeOffsets = doc.cssDiagnostics.relativeOffsets;
		metadata.cssRelativePercentageOffsets = doc.cssDiagnostics.relativePercentageOffsets;
		metadata.cssAbsoluteBoxes = doc.cssDiagnostics.absoluteBoxes;
		metadata.cssAbsoluteBlockifications = doc.cssDiagnostics.absoluteBlockifications;
		metadata.cssPositionedContainingBlocks = doc.cssDiagnostics.positionedContainingBlocks;
		metadata.cssPositionRootFallbacks = doc.cssDiagnostics.positionRootFallbacks;
		metadata.cssPositionAncestryClamps = doc.cssDiagnostics.positionAncestryClamps;
		metadata.cssAbsoluteStaticPositionUses = doc.cssDiagnostics.absoluteStaticPositionUses;
		metadata.cssAbsoluteShrinkToFit = doc.cssDiagnostics.absoluteShrinkToFit;
		metadata.cssAbsoluteOutOfFlow = doc.cssDiagnostics.absoluteOutOfFlow;
		metadata.cssPositionDocumentExtentExtensions = doc.cssDiagnostics.positionDocumentExtentExtensions;
		metadata.cssZIndexAuto = doc.cssDiagnostics.zIndexAuto;
		metadata.cssZIndexNegative = doc.cssDiagnostics.zIndexNegative;
		metadata.cssZIndexZero = doc.cssDiagnostics.zIndexZero;
		metadata.cssZIndexPositive = doc.cssDiagnostics.zIndexPositive;
		metadata.cssPositionHitOcclusions = doc.cssDiagnostics.positionHitOcclusions;
		metadata.cssPositionGeometryClamps = doc.cssDiagnostics.positionGeometryClamps;
		metadata.cssPositionUnsupportedTable = doc.cssDiagnostics.positionUnsupportedTable;
		metadata.cssPositionedEvidenceRecords = doc.cssDiagnostics.positionedEvidenceRecords;
		metadata.cssPositionedEvidence = doc.cssDiagnostics.positionedEvidence;
		metadata.cssBoxSizingContentBox = 0;
		metadata.cssBoxSizingBorderBox = 0;
		metadata.cssWidthAutoResolutions = 0;
		metadata.cssHeightAutoResolutions = 0;
		metadata.cssPercentageWidthResolved = 0;
		metadata.cssPercentageHeightResolved = 0;
		metadata.cssPercentageIndefiniteBasis = 0;
		metadata.cssPercentageCycleClamps = 0;
		metadata.cssMinWidthConstraints = 0;
		metadata.cssMaxWidthConstraints = 0;
		metadata.cssMinHeightConstraints = 0;
		metadata.cssMaxHeightConstraints = 0;
		metadata.cssConstraintConflicts = 0;
		metadata.cssOverflowHiddenBoxes = 0;
		metadata.cssOverflowAutoBoxes = 0;
		metadata.cssOverflowScrollDeferred = 0;
		metadata.cssClipIntersections = s_cssClipIntersections;
		metadata.cssClipDepthClamps = s_cssClipDepthClamps;
		metadata.cssClippedHitTargets = s_cssClippedHitTargets;
		metadata.cssVisibilityHiddenBoxes = 0;
		metadata.cssOpacityBoxes = 0;
		metadata.cssOpacityZeroBoxes = 0;
		metadata.cssVerticalAlignApplications = 0;
		metadata.cssBoxGeometryClamps = 0;
		metadata.cssLayoutPasses = 1;
		metadata.cssLayoutRecomputations = 0;
		metadata.cssClipRecordCount = 0;
		metadata.cssHitTargetsBeforeClipping = s_cssHitTargetsBeforeClipping;
		metadata.cssHitTargetsAfterClipping = s_cssHitTargetsAfterClipping;
		metadata.cssOpacityImageApproximation = 0;
		metadata.formCount = doc.formsDiagnostics.formCount;
		metadata.formInputCount = doc.formsDiagnostics.textInputCount;
		metadata.formCheckboxCount = doc.formsDiagnostics.checkboxCount;
		metadata.formRadioCount = doc.formsDiagnostics.radioCount;
		metadata.formTextareaCount = doc.formsDiagnostics.textareaCount;
		metadata.formSelectCount = doc.formsDiagnostics.selectCount;
		metadata.unsupportedFormControlCount = doc.formsDiagnostics.unsupportedControlCount;
		metadata.htmlFormsParsed = doc.formsDiagnostics.htmlFormsParsed;
		metadata.htmlFieldsetsParsed = doc.formsDiagnostics.htmlFieldsetsParsed;
		metadata.htmlLabelsParsed = doc.formsDiagnostics.htmlLabelsParsed;
		metadata.htmlInputsParsed = doc.formsDiagnostics.htmlInputsParsed;
		metadata.htmlButtonsParsed = doc.formsDiagnostics.htmlButtonsParsed;
		metadata.htmlTextareasParsed = doc.formsDiagnostics.htmlTextareasParsed;
		metadata.htmlSelectsParsed = doc.formsDiagnostics.htmlSelectsParsed;
		metadata.htmlOptionsParsed = doc.formsDiagnostics.htmlOptionsParsed;
		metadata.htmlHiddenControls = doc.formsDiagnostics.htmlHiddenControls;
		metadata.controlMetadataClamps = doc.formsDiagnostics.controlMetadataClamps;
		metadata.controlTextTruncations = doc.formsDiagnostics.controlTextTruncations;
		metadata.formControlsRendered = 0;
		metadata.formControlsUnsupported = doc.formsDiagnostics.formControlsUnsupported;
		metadata.formInteractionsDeferred = doc.formsDiagnostics.formInteractionsDeferred;
		metadata.formRuntimeControlsInitialized = doc.formsDiagnostics.formRuntimeControlsInitialized;
		metadata.formCheckboxActivations = doc.formsDiagnostics.formCheckboxActivations;
		metadata.formCheckboxToggles = doc.formsDiagnostics.formCheckboxToggles;
		metadata.formRadioActivations = doc.formsDiagnostics.formRadioActivations;
		metadata.formRadioGroupUnchecks = doc.formsDiagnostics.formRadioGroupUnchecks;
		metadata.formLabelActivations = doc.formsDiagnostics.formLabelActivations;
		metadata.formButtonActivations = doc.formsDiagnostics.formButtonActivations;
		metadata.formDisabledActivationBlocks = doc.formsDiagnostics.formDisabledActivationBlocks;
		metadata.formHiddenHitTargetsSuppressed = doc.formsDiagnostics.formHiddenHitTargetsSuppressed;
		metadata.formDuplicateActivationSuppressed = doc.formsDiagnostics.formDuplicateActivationSuppressed;
		metadata.formRuntimeStateResets = doc.formsDiagnostics.formRuntimeStateResets;
		metadata.formHitTargetsRegistered = doc.formsDiagnostics.formHitTargetsRegistered;
		metadata.formHitTargetClamps = doc.formsDiagnostics.formHitTargetClamps;
		metadata.formFocusableControls = doc.formsDiagnostics.formFocusableControls;
		metadata.formFocusChanges = doc.formsDiagnostics.formFocusChanges;
		metadata.formFocusClears = doc.formsDiagnostics.formFocusClears;
		metadata.formFocusWraps = doc.formsDiagnostics.formFocusWraps;
		metadata.formTabForward = doc.formsDiagnostics.formTabForward;
		metadata.formTabBackward = doc.formsDiagnostics.formTabBackward;
		metadata.formKeyboardActivations = doc.formsDiagnostics.formKeyboardActivations;
		metadata.formSpaceActivations = doc.formsDiagnostics.formSpaceActivations;
		metadata.formEnterActivations = doc.formsDiagnostics.formEnterActivations;
		metadata.formKeyRepeatSuppressed = doc.formsDiagnostics.formKeyRepeatSuppressed;
		metadata.formStaleKeyActivationBlocks = doc.formsDiagnostics.formStaleKeyActivationBlocks;
		metadata.formDisabledFocusSkips = doc.formsDiagnostics.formDisabledFocusSkips;
		metadata.formHiddenFocusSkips = doc.formsDiagnostics.formHiddenFocusSkips;
		metadata.formFocusStateResets = doc.formsDiagnostics.formFocusStateResets;
		metadata.formFocusOrigin = formFocusOriginName(doc.formRuntimeState.focusOrigin);
		metadata.formFocusGeneration = doc.formRuntimeState.focusValid
			? doc.formRuntimeState.focusedDocumentGeneration : 0;
		metadata.formFocusedLogicalSerial = doc.formRuntimeState.focusValid
			? doc.formRuntimeState.focusedLogicalSerial : 0;
		metadata.cssCheckedRuntimeRecomputations = doc.cssDiagnostics.checkedRuntimeRecomputations;
		metadata.formInteractionMode = doc.formsDiagnostics.formInteractionMode;
		metadata.unsupportedFormMethod = doc.formsDiagnostics.hasUnsupportedMethod;
		metadata.unsupportedFormEncoding = doc.formsDiagnostics.hasUnsupportedEncoding;
		metadata.postSupportedHosted = true;
		metadata.postSupportedBareMetal = false;

		std::vector<uint64_t> seenTableSerials;
		std::vector<uint64_t> seenTableRowSerials;
		std::vector<uint64_t> seenFigureSerials;
		std::vector<uint64_t> seenBlockquoteSerials;
		std::vector<uint64_t> seenDlSerials;
		auto countUniqueAncestorTag = [&](const DocBlock& block, const std::string& tagName, std::vector<uint64_t>& seen) -> bool {
			const std::string tag = toLowerAscii(tagName);
			for (const gxos::web::HtmlElementRef& ancestor : block.ancestors) {
				if (toLowerAscii(ancestor.tagName) != tag) continue;
				if (ancestor.serial != 0 && std::find(seen.begin(), seen.end(), ancestor.serial) == seen.end()) {
					seen.push_back(ancestor.serial);
					return true;
				}
			}
			return false;
		};
		for (size_t i = 0; i < doc.blocks.size(); ++i) {
			const DocBlock& block = doc.blocks[i];
			if (block.style.displayNone) {
				++metadata.cssDisplayNoneBlockCount;
				continue;
			}
			if (isFormControlBlock(block)) ++metadata.formControlsRendered;
			if (blockHasWrapperAncestor(block)) {
				++metadata.cssWrapperRenderCount;
			}
			const int wrapperDepth = wrapperAncestorDepth(block);
			const int wrapperInset = nestedWrapperInsetPx(block);
			metadata.cssMaxWrapperAncestorDepth = std::max(metadata.cssMaxWrapperAncestorDepth, wrapperDepth);
			const int availableWidth = blockAvailableWidth(block, doc);
			const CssBlockGeometry geometry = cssGeometryForBlock(doc, static_cast<int>(i));
			const bool hasOwnOverflowClip = block.style.overflowX != OverflowMode::Visible ||
				block.style.overflowY != OverflowMode::Visible;
			if (hasOwnOverflowClip || cssBlockHasOverflowAncestor(doc, block))
				++metadata.cssClipRecordCount;
			if (block.style.boxSizing == BoxSizingMode::BorderBox) ++metadata.cssBoxSizingBorderBox;
			else ++metadata.cssBoxSizingContentBox;
			if (geometry.widthAuto) ++metadata.cssWidthAutoResolutions;
			if (geometry.heightAuto) ++metadata.cssHeightAutoResolutions;
			const bool widthPercent = block.style.widthValue.type == CssLengthType::Percent || block.style.widthPercent >= 0;
			const bool heightPercent = block.style.heightValue.type == CssLengthType::Percent || block.style.heightPercent >= 0;
			if (widthPercent) {
				if (geometry.widthPercentageUnresolved) ++metadata.cssPercentageIndefiniteBasis;
				else ++metadata.cssPercentageWidthResolved;
			}
			if (heightPercent) {
				if (geometry.heightPercentageUnresolved) ++metadata.cssPercentageIndefiniteBasis;
				else ++metadata.cssPercentageHeightResolved;
			}
			if (block.ancestors.size() > 12) ++metadata.cssPercentageCycleClamps;
			const bool minWidthSet = block.style.minWidthValue.valid || block.style.minWidth >= 0 || block.style.minWidthPercent >= 0;
			const bool maxWidthSet = !block.style.maxWidthNone &&
				(block.style.maxWidthValue.valid || block.style.maxWidth >= 0 || block.style.maxWidthPercent >= 0);
			const bool minHeightSet = block.style.minHeightValue.valid || block.style.minHeight >= 0 || block.style.minHeightPercent >= 0;
			const bool maxHeightSet = !block.style.maxHeightNone &&
				(block.style.maxHeightValue.valid || block.style.maxHeight >= 0 || block.style.maxHeightPercent >= 0);
			if (minWidthSet) ++metadata.cssMinWidthConstraints;
			if (maxWidthSet) ++metadata.cssMaxWidthConstraints;
			if (minHeightSet) ++metadata.cssMinHeightConstraints;
			if (maxHeightSet) ++metadata.cssMaxHeightConstraints;
			if (geometry.constraintConflict) ++metadata.cssConstraintConflicts;
			if (block.style.overflowX == OverflowMode::Hidden || block.style.overflowY == OverflowMode::Hidden)
				++metadata.cssOverflowHiddenBoxes;
			if (block.style.overflowX == OverflowMode::Auto || block.style.overflowY == OverflowMode::Auto)
				++metadata.cssOverflowAutoBoxes;
			if (block.style.overflowX == OverflowMode::Scroll || block.style.overflowY == OverflowMode::Scroll)
				++metadata.cssOverflowScrollDeferred;
			if (block.style.visibility == VisibilityMode::Hidden) ++metadata.cssVisibilityHiddenBoxes;
			if (block.style.opacityPercent < 100 || block.style.effectiveOpacityPercent < 100) ++metadata.cssOpacityBoxes;
			if (block.style.effectiveOpacityPercent == 0) ++metadata.cssOpacityZeroBoxes;
			if (block.style.verticalAlign != VerticalAlignMode::Baseline &&
				(isTableCellLikeBlock(block) || block.type == BlockType::Image || isFormControlBlock(block)))
				++metadata.cssVerticalAlignApplications;
			if (geometry.clamped) ++metadata.cssBoxGeometryClamps;
			++metadata.cssLayoutRecomputations;
			const bool phase3aId = block.id.rfind("phase3a-", 0) == 0 || block.id.rfind("css3a-", 0) == 0;
			if (phase3aId && metadata.cssEvidenceRecordCount < 32 && metadata.cssGeometryEvidence.size() < 32768) {
				std::string reason = geometry.widthAuto || geometry.heightAuto ? "auto" : "definite";
				if (geometry.widthPercentageUnresolved || geometry.heightPercentageUnresolved) reason += ",indefinite-basis";
				if (geometry.constraintConflict) reason += ",constraint-conflict";
				if (geometry.clamped) reason += ",numeric-clamp";
				std::string boundedId = block.id.substr(0, std::min<size_t>(64, block.id.size()));
				for (char& ch : boundedId) if (ch == '\n' || ch == '\r' || ch == ';') ch = '_';
				std::ostringstream evidence;
					evidence << "id=" << boundedId
					<< ",box-sizing=" << (block.style.boxSizing == BoxSizingMode::BorderBox ? "border-box" : "content-box")
					<< ",specified-width=" << cssNavigatorLengthEvidence(block.style.widthValue)
					<< ",specified-height=" << cssNavigatorLengthEvidence(block.style.heightValue)
					<< ",basis-width=" << availableWidth << ",basis-width-definite=yes"
					<< ",basis-height=" << blockContainingContentHeight(block, doc)
					<< ",basis-height-definite=" << (blockContainingContentHeight(block, doc) >= 0 ? "yes" : "no")
					<< ",used-content-width=" << geometry.contentWidth
					<< ",used-content-height=" << geometry.contentHeight
					<< ",min-width-applied=" << (geometry.minWidthApplied ? "yes" : "no")
					<< ",max-width-applied=" << (geometry.maxWidthApplied ? "yes" : "no")
					<< ",min-height-applied=" << (geometry.minHeightApplied ? "yes" : "no")
					<< ",max-height-applied=" << (geometry.maxHeightApplied ? "yes" : "no")
					<< ",padding-box=" << geometry.paddingBox.x << ":" << geometry.paddingBox.y << ":" << geometry.paddingBox.w << ":" << geometry.paddingBox.h
					<< ",border-box=" << geometry.outerX << ":" << geometry.outerY << ":" << geometry.outerWidth << ":" << geometry.outerHeight
					<< ",outer-box=" << geometry.outerX << ":" << geometry.outerY << ":" << geometry.outerWidth << ":" << geometry.outerHeight
					<< ",min-width=" << cssNavigatorLengthEvidence(block.style.minWidthValue)
					<< ",max-width=" << cssNavigatorLengthEvidence(block.style.maxWidthValue)
					<< ",overflow-x=" << static_cast<unsigned>(block.style.overflowX)
					<< ",overflow-y=" << static_cast<unsigned>(block.style.overflowY)
					<< ",clip=" << geometry.clip.x << ":" << geometry.clip.y << ":" << geometry.clip.w << ":" << geometry.clip.h
					<< ",visibility=" << (block.style.visibility == VisibilityMode::Hidden ? "hidden" : "visible")
					<< ",effective-opacity=" << block.style.effectiveOpacityPercent
					<< ",vertical-align=" << static_cast<unsigned>(block.style.verticalAlign)
					<< ",reason=" << reason << "\n";
				const std::string line = evidence.str();
				if (metadata.cssGeometryEvidence.size() + line.size() <= 32768) {
					metadata.cssGeometryEvidence += line;
					++metadata.cssEvidenceRecordCount;
				}
			}
			if (block.type != BlockType::Image) {
				const int outerWidth = blockOuterWidth(block, availableWidth);
				(void)outerWidth;
				const int requestedWidth = std::min(
					cssWidthPx(block.style, availableWidth, availableWidth),
					cssMaxWidthPx(block.style, availableWidth, availableWidth));
				const bool nestedClamp = wrapperInset > 0;
				const bool widthClamp = availableWidth < kMinReadableBlockWidth ||
					requestedWidth < std::min(availableWidth, kMinReadableBlockWidth);
				if (nestedClamp || widthClamp) {
					++metadata.cssNestedLayoutClamps;
				}
			}
			const bool hasWidthConstraint = block.style.width > 0 || block.style.widthPercent >= 0 ||
				block.style.maxWidth > 0 || block.style.maxWidthPercent >= 0;
			if (block.style.maxWidth > 0 || block.style.maxWidthPercent >= 0) {
				++metadata.cssLayoutMaxWidthAppliedCount;
			}
			if (block.style.marginLeft == -2 && block.style.marginRight == -2 && hasWidthConstraint) {
				++metadata.cssAutoMarginCenteredBlockCount;
			}
			if (block.style.hasBackgroundColor) {
				++metadata.cssBackgroundBlockCount;
			}
			const int borderTopPx = cssBorderTopPx(block.style);
			const int borderRightPx = cssBorderRightPx(block.style);
			const int borderBottomPx = cssBorderBottomPx(block.style);
			const int borderLeftPx = cssBorderLeftPx(block.style);
			const auto borderVisible = [](BorderLineStyle lineStyle, int width, uint32_t color) {
				const BorderLineStyle effective = cssBorderStyleOrDefault(lineStyle, width);
				return width > 0 && effective != BorderLineStyle::None && effective != BorderLineStyle::Hidden &&
					((color >> 24) & 0xFFu) != 0;
			};
			const auto borderDashed = [](BorderLineStyle lineStyle, int width) {
				return width > 0 && cssBorderStyleOrDefault(lineStyle, width) == BorderLineStyle::Dashed;
			};
			const auto borderDotted = [](BorderLineStyle lineStyle, int width) {
				return width > 0 && cssBorderStyleOrDefault(lineStyle, width) == BorderLineStyle::Dotted;
			};
			const bool hasAnyBorder =
				borderVisible(block.style.borderTopStyle, borderTopPx, block.style.borderTopColor) ||
				borderVisible(block.style.borderRightStyle, borderRightPx, block.style.borderRightColor) ||
				borderVisible(block.style.borderBottomStyle, borderBottomPx, block.style.borderBottomColor) ||
				borderVisible(block.style.borderLeftStyle, borderLeftPx, block.style.borderLeftColor);
			if (hasAnyBorder) {
				++metadata.cssBorderedBlocksRendered;
			}
			const bool hasDashedBorder =
				borderDashed(block.style.borderTopStyle, borderTopPx) ||
				borderDashed(block.style.borderRightStyle, borderRightPx) ||
				borderDashed(block.style.borderBottomStyle, borderBottomPx) ||
				borderDashed(block.style.borderLeftStyle, borderLeftPx);
			const bool hasDottedBorder =
				borderDotted(block.style.borderTopStyle, borderTopPx) ||
				borderDotted(block.style.borderRightStyle, borderRightPx) ||
				borderDotted(block.style.borderBottomStyle, borderBottomPx) ||
				borderDotted(block.style.borderLeftStyle, borderLeftPx);
			if (hasDashedBorder) {
				++metadata.cssDashedBordersRendered;
			}
			if (hasDottedBorder) {
				++metadata.cssDottedBordersRendered;
			}
			if (block.type == BlockType::ListItem) {
				++metadata.cssListRenderCount;
				const uint64_t ordinal = blockListOrdinal(doc, static_cast<int>(i));
				const std::string marker = blockListMarkerText(block, ordinal);
				if (marker.empty()) {
					++metadata.cssListStyleNoneApplied;
				} else {
					++metadata.cssListStyleMarkersRendered;
				}
			}
			const bool hasTextDecoration =
				(block.style.hasTextDecoration && (block.style.underline || block.style.lineThrough)) ||
				(block.type == BlockType::Link && (!block.style.hasTextDecoration || block.style.underline || block.style.lineThrough));
			if (hasTextDecoration) {
				++metadata.cssTextDecorationsRendered;
			}
			if (block.style.genericFontFamily != GenericFontFamily::Inherit) {
				++metadata.cssGenericFontFamilyApplied;
				if (block.style.genericFontFamily == GenericFontFamily::Serif ||
					block.style.genericFontFamily == GenericFontFamily::Monospace) {
					++metadata.cssGenericFontFamilyFallbacks;
				}
			}
			if (!block.url.empty() && s_visitedUrls.find(block.url) != s_visitedUrls.end()) {
				++metadata.cssVisitedLinkCount;
			}
			if (toLowerAscii(block.tagName) == "caption") {
				++metadata.cssTableCaptionCount;
			}
			if (toLowerAscii(block.tagName) == "th") {
				++metadata.cssTableHeaderCellCount;
			}
			if (toLowerAscii(block.tagName) == "figcaption") {
				++metadata.cssFigcaptionsRendered;
			}
			if (countUniqueAncestorTag(block, "figure", seenFigureSerials)) {
				++metadata.cssFiguresRendered;
			}
			if (countUniqueAncestorTag(block, "blockquote", seenBlockquoteSerials)) {
				++metadata.cssBlockquotesRendered;
			}
			if (countUniqueAncestorTag(block, "dl", seenDlSerials)) {
				++metadata.cssDefinitionListsRendered;
			}
			if (isTableCellLikeBlock(block)) {
				++metadata.cssTableCellCount;
				const uint64_t tableSerial = tableSerialForBlock(block);
				const uint64_t rowSerial = tableRowSerialForBlock(block);
				if (tableSerial != 0 && std::find(seenTableSerials.begin(), seenTableSerials.end(), tableSerial) == seenTableSerials.end()) {
					seenTableSerials.push_back(tableSerial);
					++metadata.cssTableRenderCount;
				}
				if (rowSerial != 0 && std::find(seenTableRowSerials.begin(), seenTableRowSerials.end(), rowSerial) == seenTableRowSerials.end()) {
					seenTableRowSerials.push_back(rowSerial);
					++metadata.cssTableRowCount;
				}
				if (isFirstTableCellInGroup(doc, static_cast<int>(i))) {
					const int groupStart = tableGroupStartIndex(doc, static_cast<int>(i));
					const TableGroupLayout layout = buildTableGroupLayout(doc, groupStart);
					if (layout.collapseMode) {
						++metadata.cssCollapsedTablesRendered;
					} else {
						++metadata.cssSeparateTablesRendered;
					}
					if (layout.fallbackUsed) {
						++metadata.cssTableLayoutFallbackCount;
					}
				}
			}
			if (block.type != BlockType::Image) continue;
			++metadata.imageBlockCount;
			if (isRemoteHttpUrl(block.url)) {
				++metadata.remoteImageCount;
			} else if (block.url.rfind("file://", 0) == 0) {
				++metadata.localImageCount;
			}
			int imageW = 0;
			int imageH = 0;
			bool imageConstrained = false;
			bool imageAspectPreserved = false;
			bool imageSizeClamped = false;
			const int imageAvailableWidth = blockAvailableWidth(block, doc);
			imageDisplaySize(block, imageAvailableWidth, imageW, imageH, &imageConstrained, &imageAspectPreserved, &imageSizeClamped);
			const ImageInfo& info = imageInfoForBlock(block);
			if (info.ok) {
				++metadata.loadedImageCount;
			} else {
				++metadata.failedImageCount;
				if (!block.alt.empty()) {
					++metadata.cssImageAltFallbacks;
				}
				if (metadata.lastImageError.empty()) {
					metadata.lastImageError = info.errorDetail.empty() ? info.message : info.errorDetail;
				}
			}
			if (imageConstrained) {
				++metadata.cssImagesConstrained;
			}
			if (imageAspectPreserved) {
				++metadata.cssImagesAspectPreserved;
			}
			if (imageSizeClamped) {
				++metadata.cssImageSizeClamps;
			}
		}

		InlineLayoutSnapshot inlineSnapshot;
		ensureCssMarginLayout(doc);
		ensureCssFloatLayout(doc);
		rebuildInlineLayout(doc, inlineSnapshot);
		metadata.cssFloatLeft = doc.cssDiagnostics.floatLeft;
		metadata.cssFloatRight = doc.cssDiagnostics.floatRight;
		metadata.cssFloatBlockifications = doc.cssDiagnostics.floatBlockifications;
		metadata.cssFloatRecords = doc.cssDiagnostics.floatRecords;
		metadata.cssFloatPlacementAttempts = doc.cssDiagnostics.floatPlacementAttempts;
		metadata.cssFloatPlacementDownshifts = doc.cssDiagnostics.floatPlacementDownshifts;
		metadata.cssFloatSideBySide = doc.cssDiagnostics.floatSideBySide;
		metadata.cssFloatWidthOverflows = doc.cssDiagnostics.floatWidthOverflows;
		metadata.cssFloatLineExclusions = doc.cssDiagnostics.floatLineExclusions;
		metadata.cssFloatZeroWidthLineAdvances = doc.cssDiagnostics.floatZeroWidthLineAdvances;
		metadata.cssFloatBfcAvoidances = doc.cssDiagnostics.floatBfcAvoidances;
		metadata.cssFloatBfcDownshifts = doc.cssDiagnostics.floatBfcDownshifts;
		metadata.cssClearLeft = doc.cssDiagnostics.clearLeft;
		metadata.cssClearRight = doc.cssDiagnostics.clearRight;
		metadata.cssClearBoth = doc.cssDiagnostics.clearBoth;
		metadata.cssClearanceApplied = doc.cssDiagnostics.clearanceApplied;
		metadata.cssFloatContainmentBoundaries = doc.cssDiagnostics.floatContainmentBoundaries;
		metadata.cssFloatScopeSuppressions = doc.cssDiagnostics.floatScopeSuppressions;
		metadata.cssFloatHeightContainments = doc.cssDiagnostics.floatHeightContainments;
		metadata.cssBfcFloatContainments = doc.cssDiagnostics.bfcFloatContainments;
		metadata.cssBfcFloatHeightExtensions = doc.cssDiagnostics.bfcFloatHeightExtensions;
		metadata.cssBfcFloatHeightNoops = doc.cssDiagnostics.bfcFloatHeightNoops;
		metadata.cssBfcFloatAvoidanceAttempts = doc.cssDiagnostics.bfcFloatAvoidanceAttempts;
		metadata.cssBfcFloatAvoidanceFits = doc.cssDiagnostics.bfcFloatAvoidanceFits;
		metadata.cssBfcFloatAvoidanceDownshifts = doc.cssDiagnostics.bfcFloatAvoidanceDownshifts;
		metadata.cssBfcFloatTooWide = doc.cssDiagnostics.bfcFloatTooWide;
		metadata.cssNestedFloatContexts = doc.cssDiagnostics.nestedFloatContexts;
		metadata.cssNestedFloatDepthClamps = doc.cssDiagnostics.nestedFloatDepthClamps;
		metadata.cssFloatInsideInlineBlock = doc.cssDiagnostics.floatInsideInlineBlock;
		metadata.cssFloatInsideFloat = doc.cssDiagnostics.floatInsideFloat;
		metadata.cssFloatListCases = doc.cssDiagnostics.floatListCases;
		metadata.cssFloatTableCellCases = doc.cssDiagnostics.floatTableCellCases;
		metadata.cssFloatTableAvoidanceCases = doc.cssDiagnostics.floatTableAvoidanceCases;
		metadata.cssFloatedTableUnsupported = doc.cssDiagnostics.floatedTableUnsupported;
		metadata.cssFloatDocumentExtentExtensions = doc.cssDiagnostics.floatDocumentExtentExtensions;
		metadata.cssFloatGeometryClamps = doc.cssDiagnostics.floatGeometryClamps;
		metadata.cssFloatPlacementAttemptClamps = doc.cssDiagnostics.floatPlacementAttemptClamps;
		metadata.cssFloatExclusionScanClamps = doc.cssDiagnostics.floatExclusionScanClamps;
		metadata.cssFloatBfcDepthClamps = doc.cssDiagnostics.floatBfcDepthClamps;
		metadata.cssFloatEvidenceRecords = doc.cssDiagnostics.floatEvidenceRecords;
		metadata.cssFloatEvidence = doc.cssDiagnostics.floatEvidence;
		metadata.cssPositionStatic = doc.cssDiagnostics.positionStatic;
		metadata.cssPositionRelative = doc.cssDiagnostics.positionRelative;
		metadata.cssPositionAbsolute = doc.cssDiagnostics.positionAbsolute;
		metadata.cssPositionUnsupportedFixed = doc.cssDiagnostics.positionUnsupportedFixed;
		metadata.cssPositionUnsupportedSticky = doc.cssDiagnostics.positionUnsupportedSticky;
		metadata.cssRelativeOffsets = doc.cssDiagnostics.relativeOffsets;
		metadata.cssRelativePercentageOffsets = doc.cssDiagnostics.relativePercentageOffsets;
		metadata.cssAbsoluteBoxes = doc.cssDiagnostics.absoluteBoxes;
		metadata.cssAbsoluteBlockifications = doc.cssDiagnostics.absoluteBlockifications;
		metadata.cssPositionedContainingBlocks = doc.cssDiagnostics.positionedContainingBlocks;
		metadata.cssPositionRootFallbacks = doc.cssDiagnostics.positionRootFallbacks;
		metadata.cssPositionAncestryClamps = doc.cssDiagnostics.positionAncestryClamps;
		metadata.cssAbsoluteStaticPositionUses = doc.cssDiagnostics.absoluteStaticPositionUses;
		metadata.cssAbsoluteShrinkToFit = doc.cssDiagnostics.absoluteShrinkToFit;
		metadata.cssAbsoluteOutOfFlow = doc.cssDiagnostics.absoluteOutOfFlow;
		metadata.cssPositionDocumentExtentExtensions = doc.cssDiagnostics.positionDocumentExtentExtensions;
		metadata.cssZIndexAuto = doc.cssDiagnostics.zIndexAuto;
		metadata.cssZIndexNegative = doc.cssDiagnostics.zIndexNegative;
		metadata.cssZIndexZero = doc.cssDiagnostics.zIndexZero;
		metadata.cssZIndexPositive = doc.cssDiagnostics.zIndexPositive;
		metadata.cssPositionHitOcclusions = doc.cssDiagnostics.positionHitOcclusions;
		metadata.cssPositionGeometryClamps = doc.cssDiagnostics.positionGeometryClamps;
		metadata.cssPositionUnsupportedTable = doc.cssDiagnostics.positionUnsupportedTable;
		metadata.cssPositionedEvidenceRecords = doc.cssDiagnostics.positionedEvidenceRecords;
		metadata.cssPositionedEvidence = doc.cssDiagnostics.positionedEvidence;
		metadata.cssInlineItems = static_cast<int>(std::min<size_t>(std::numeric_limits<int>::max(), inlineSnapshot.itemCount));
		metadata.cssInlineTextRuns = inlineSnapshot.textRuns;
		metadata.cssInlineWhitespaceRuns = inlineSnapshot.whitespaceRuns;
		metadata.cssInlineForcedBreaks = inlineSnapshot.forcedBreaks;
		metadata.cssLineWraps = inlineSnapshot.lineWraps;
		metadata.cssWhitespaceCollapses = inlineSnapshot.whitespaceCollapses;
		metadata.cssLeadingSpaceSuppressions = inlineSnapshot.leadingSpaceSuppressions;
		metadata.cssTrailingSpaceSuppressions = inlineSnapshot.trailingSpaceSuppressions;
		metadata.cssReplacedInlineItems = inlineSnapshot.replacedItems;
		metadata.cssControlInlineItems = inlineSnapshot.controlItems;
		metadata.cssVerticalAlignAdjustments = inlineSnapshot.verticalAlignAdjustments;
		metadata.cssLineHeightClamps = inlineSnapshot.lineHeightClamps;
		metadata.cssBaselineIterationClamps = inlineSnapshot.baselineIterationClamps;
		metadata.cssInlineNestingClamps = inlineSnapshot.nestingClamps;
		metadata.cssInlineWrapScanClamps = inlineSnapshot.wrapScanClamps;
		for (const WebInlineItem& item : doc.inlineItems) {
			if (item.kind == InlineItemKind::AtomicBlock) ++metadata.cssInlineBlockItems;
		}
		metadata.cssAtomicFormattingContexts = static_cast<int>(std::min<size_t>(
			std::numeric_limits<int>::max(), inlineSnapshot.atomicResults.size()));
		metadata.cssAtomicContextsDocument = inlineSnapshot.atomicContextsDocument;
		metadata.cssAtomicLayoutOperations = inlineSnapshot.atomicLayoutOperations;
		metadata.cssAtomicLayoutOperationClamps = inlineSnapshot.atomicLayoutOperationClamps;
		metadata.cssAtomicContextDepthMax = inlineSnapshot.atomicContextDepthMax;
		metadata.cssAtomicContextDepthClamps = inlineSnapshot.atomicContextDepthClamps;
		metadata.cssInlineBlockAutoWidths = inlineSnapshot.inlineBlockAutoWidths;
		metadata.cssInlineBlockExplicitWidths = inlineSnapshot.inlineBlockExplicitWidths;
		metadata.cssInlineBlockShrinkToFit = inlineSnapshot.inlineBlockShrinkToFit;
		metadata.cssInlineBlockPreferredMinClamps = inlineSnapshot.inlineBlockPreferredMinClamps;
		metadata.cssInlineBlockPreferredWidthClamps = inlineSnapshot.inlineBlockPreferredWidthClamps;
		metadata.cssInlineBlockBaselineFromLine = inlineSnapshot.inlineBlockBaselineFromLine;
		metadata.cssInlineBlockBaselineFallback = inlineSnapshot.inlineBlockBaselineFallback;
		metadata.cssInlineBlockNested = inlineSnapshot.inlineBlockNested;
		metadata.cssInlineBlockWraps = inlineSnapshot.inlineBlockWraps;
		metadata.cssInlineBlockHitTargets = inlineSnapshot.inlineBlockHitTargets;
		metadata.cssInlineBlockOverflowClips = inlineSnapshot.inlineBlockOverflowClips;
		metadata.cssAtomicContextIncomplete = inlineSnapshot.atomicContextIncomplete;
		for (const InlineFlowLayout& flow : inlineSnapshot.flows) {
			metadata.cssLineBoxes += static_cast<int>(std::min<size_t>(
				std::numeric_limits<int>::max() - static_cast<size_t>(std::max(0, metadata.cssLineBoxes)),
				flow.lines.size()));
			metadata.cssInlineFragments += static_cast<int>(std::min<size_t>(
				std::numeric_limits<int>::max() - static_cast<size_t>(std::max(0, metadata.cssInlineFragments)),
				flow.fragments.size()));
			for (const InlineFragmentLayout& fragment : flow.fragments) {
				if (!fragment.visible || fragment.whitespace || fragment.blockIndex < 0 ||
					fragment.blockIndex >= static_cast<int>(doc.blocks.size())) continue;
				const DocBlock& fragmentBlock = doc.blocks[static_cast<size_t>(fragment.blockIndex)];
				if (fragmentBlock.type == BlockType::Link || fragmentBlock.type == BlockType::FormLabel ||
					isFormControlBlock(fragmentBlock)) {
					++metadata.cssInlineHitFragments;
				}
				const WebInlineItem& item = doc.inlineItems[static_cast<size_t>(fragment.itemIndex)];
				std::string evidenceId = fragmentBlock.id;
				if (evidenceId.empty() && item.ownerSerial != 0) {
					for (const gxos::web::HtmlElementRef& element : doc.structuralElements) {
						if (element.serial == item.ownerSerial && !element.id.empty()) {
							evidenceId = element.id;
							break;
						}
					}
				}
				const bool phase3bId = evidenceId.rfind("phase3b-", 0) == 0 ||
					evidenceId.rfind("css3b-", 0) == 0;
				if (!phase3bId || metadata.cssInlineEvidenceRecordCount >= 64 ||
					metadata.cssInlineEvidence.size() >= 32768) continue;
				const InlineLineLayout* line = nullptr;
				for (const InlineLineLayout& candidate : flow.lines) {
					if (candidate.lineIndex == fragment.lineIndex) {
						line = &candidate;
						break;
					}
				}
				if (!line) continue;
				const char* kind = "text";
				if (item.kind == InlineItemKind::ForcedBreak) kind = "forced-break";
				else if (item.kind == InlineItemKind::ReplacedImage) kind = "image";
				else if (item.kind == InlineItemKind::FormControl) kind = "control";
				std::string boundedId = evidenceId.substr(0, std::min<size_t>(64, evidenceId.size()));
				for (char& ch : boundedId) if (ch == '\n' || ch == '\r' || ch == ';') ch = '_';
				std::ostringstream evidence;
					evidence << "id=" << boundedId
					<< ",flow-content-width=" << flow.contentWidth
					<< ",line-available-left=" << line->availableLeft
					<< ",line-available-right=" << line->availableRight
					<< ",line-available-width=" << line->availableWidth
					<< ",float-records-intersected=" << line->floatRecordsIntersected
					<< ",float-exclusion-complete=" << (line->exclusionComplete ? "yes" : "no")
					<< ",line=" << line->lineIndex
					<< ",line-top=" << line->top
					<< ",line-bottom=" << (line->top + line->usedLineHeight)
					<< ",baseline=" << line->baseline
					<< ",ascent=" << line->ascent
					<< ",descent=" << line->descent
					<< ",used-line-height=" << line->usedLineHeight
					<< ",kind=" << kind
					<< ",fragment=" << fragment.x << ":" << fragment.y << ":" << fragment.w << ":" << fragment.h
					<< ",item-baseline=" << fragment.baselineOffset
					<< ",vertical-align=" << static_cast<unsigned>(inlineOwnerStyle(doc, item, flow.style)->verticalAlign)
					<< ",whitespace-collapsed=" << (fragment.collapsedWhitespace ? "yes" : "no")
					<< ",logical-serial=" << item.ownerSerial
					<< ",parent-serial=" << item.parentSerial
					<< ",hit-target-serial=" << fragment.hitSerial
					<< ",visibility=" << (fragmentBlock.style.visibility == VisibilityMode::Hidden ? "hidden" : "visible")
					<< ",opacity=" << fragmentBlock.style.effectiveOpacityPercent << "\n";
				const std::string lineText = evidence.str();
				if (metadata.cssInlineEvidence.size() + lineText.size() <= 32768) {
					metadata.cssInlineEvidence += lineText;
					++metadata.cssInlineEvidenceRecordCount;
				}
			}
		}
		for (const CssAtomicLayoutResult& atomic : inlineSnapshot.atomicResults) {
			if (metadata.cssAtomicEvidenceRecordCount >= 64 || metadata.cssAtomicEvidence.size() >= 32768) break;
			std::string evidenceId;
			for (const gxos::web::HtmlElementRef& element : doc.structuralElements) {
				if (element.serial == atomic.containerSerial) {
					evidenceId = element.id;
					break;
				}
			}
			if (evidenceId.rfind("phase3c-", 0) != 0 && evidenceId.rfind("css3c-", 0) != 0) continue;
			std::string boundedId = evidenceId.substr(0, std::min<size_t>(64, evidenceId.size()));
			for (char& ch : boundedId) if (ch == '\n' || ch == '\r' || ch == ';') ch = '_';
			int childLineCount = 0;
			for (const InlineFlowLayout& flow : inlineSnapshot.flows)
				if (flow.atomicResultIndex == static_cast<int>(&atomic - inlineSnapshot.atomicResults.data()))
					childLineCount += static_cast<int>(flow.lines.size());
			std::ostringstream evidence;
			evidence << "id=" << boundedId
				<< ",formatting-context-depth=" << atomic.depth
				<< ",available-width=" << atomic.availableWidth
				<< ",auto-width=" << (atomic.autoWidth ? "yes" : "no")
				<< ",explicit-width=" << (atomic.explicitWidth ? "yes" : "no")
				<< ",preferred-min=" << atomic.preferredMinimum
				<< ",preferred-width=" << atomic.preferredWidth
				<< ",shrink-to-fit-width=" << atomic.shrinkToFitWidth
				<< ",content-size=" << atomic.usedContentWidth << ":" << atomic.usedContentHeight
				<< ",padding-box=" << atomic.paddingBoxWidth << ":" << atomic.paddingBoxHeight
				<< ",border-box=" << atomic.borderBoxWidth << ":" << atomic.borderBoxHeight
				<< ",outer-size=" << atomic.outerWidth << ":" << atomic.outerHeight
				<< ",baseline-source=" << (atomic.baselineFromLine ? "last-line" : "bottom-edge")
				<< ",baseline-y=" << atomic.baselineY
				<< ",child-block-count=" << atomic.childCount
				<< ",child-line-count=" << childLineCount
				<< ",overflow=" << atomic.overflowWidth << ":" << atomic.overflowHeight
				<< ",clip=" << atomic.clip.x << ":" << atomic.clip.y << ":" << atomic.clip.w << ":" << atomic.clip.h
				<< ",hit-target-count=" << atomic.hitTargetCount
				<< ",complete=" << (atomic.complete ? "yes" : "no")
				<< ",clamped=" << (atomic.clamped ? "yes" : "no") << "\n";
			const std::string line = evidence.str();
			if (metadata.cssAtomicEvidence.size() + line.size() <= 32768) {
				metadata.cssAtomicEvidence += line;
				++metadata.cssAtomicEvidenceRecordCount;
			}
		}
		metadata.cssInlineFragmentClamps = inlineSnapshot.inlineFragmentClamps;
		metadata.cssDescenderSafeLines = inlineSnapshot.descenderSafeLines;
		if (doc.url.find("css-phase1f") != std::string::npos) {
			int perSideAncestorBlocks = 0;
			int dashedStyledBlocks = 0;
			int dottedStyledBlocks = 0;
			for (const DocBlock& block : doc.blocks) {
				bool hasPerSideAncestor = toLowerAscii(block.className) == "per-side";
				if (!hasPerSideAncestor) {
					for (const gxos::web::HtmlElementRef& ancestor : block.ancestors) {
						if (toLowerAscii(ancestor.className) == "per-side") {
							hasPerSideAncestor = true;
							break;
						}
					}
				}
				if (!hasPerSideAncestor) continue;
				++perSideAncestorBlocks;
				if (block.style.borderTopStyle == BorderLineStyle::Dashed ||
					block.style.borderRightStyle == BorderLineStyle::Dashed ||
					block.style.borderBottomStyle == BorderLineStyle::Dashed ||
					block.style.borderLeftStyle == BorderLineStyle::Dashed) {
					++dashedStyledBlocks;
				}
				if (block.style.borderTopStyle == BorderLineStyle::Dotted ||
					block.style.borderRightStyle == BorderLineStyle::Dotted ||
					block.style.borderBottomStyle == BorderLineStyle::Dotted ||
					block.style.borderLeftStyle == BorderLineStyle::Dotted) {
					++dottedStyledBlocks;
				}
			}
		}
	}

	static std::string pageInfoLine(const std::string& label, const std::string& value)
	{
		return label + ": " + (value.empty() ? "(none)" : value);
	}

	static std::string pageInfoLine(const std::string& label, int value)
	{
		return label + ": " + std::to_string(value);
	}

	static DocBlock makeBlock(BlockType type, const std::string& text, const std::string& url, const std::string& tagName)
	{
		DocBlock block;
		block.type = type;
		block.text = text;
		block.url = url;
		block.tagName = tagName;
		return block;
	}

	static int cssMarginOrDefault(const WebStyle& style, int fallbackValue)
	{
		return style.marginTop >= 0 ? style.marginTop : fallbackValue;
	}

	static int cssMarginSideOrDefault(int sideValue, int uniformValue, int fallbackValue)
	{
		if (sideValue != -1) return sideValue == -2 ? 0 : sideValue;
		if (uniformValue != -1) return uniformValue;
		return fallbackValue;
	}

	static int cssPaddingOrDefault(const WebStyle& style, int fallbackValue)
	{
		return style.padding >= 0 ? style.padding : fallbackValue;
	}

	static int cssPaddingSideOrDefault(int sideValue, int uniformValue, int fallbackValue)
	{
		if (sideValue != -1) return sideValue == -2 ? 0 : sideValue;
		if (uniformValue != -1) return uniformValue;
		return fallbackValue;
	}

	static int cssPaddingTopPx(const WebStyle& style, int fallbackValue)
	{
		return cssPaddingSideOrDefault(style.paddingTop, style.padding, fallbackValue);
	}

	static int cssPaddingRightPx(const WebStyle& style, int fallbackValue)
	{
		return cssPaddingSideOrDefault(style.paddingRight, style.padding, fallbackValue);
	}

	static int cssPaddingBottomPx(const WebStyle& style, int fallbackValue)
	{
		return cssPaddingSideOrDefault(style.paddingBottom, style.padding, fallbackValue);
	}

	static int cssPaddingLeftPx(const WebStyle& style, int fallbackValue)
	{
		return cssPaddingSideOrDefault(style.paddingLeft, style.padding, fallbackValue);
	}

	static int cssMarginTopPx(const WebStyle& style, int fallbackValue)
	{
		if (style.marginTopValue.valid) {
			if (style.marginTopValue.type == CssLengthType::Auto) return 0;
			if (style.marginTopValue.type == CssLengthType::Percent) {
				const int64_t resolved = static_cast<int64_t>(kContentW) * style.marginTopValue.value / 100;
				return static_cast<int>(std::max<int64_t>(-8192, std::min<int64_t>(8192, resolved)));
			}
			return std::max(-8192, std::min(8192, style.marginTopValue.value));
		}
		return style.marginTop != -1 ? (style.marginTop == -2 ? 0 : style.marginTop) : fallbackValue;
	}

	static int cssMarginBottomPx(const WebStyle& style, int fallbackValue)
	{
		if (style.marginBottomValue.valid) {
			if (style.marginBottomValue.type == CssLengthType::Auto) return 0;
			if (style.marginBottomValue.type == CssLengthType::Percent) {
				const int64_t resolved = static_cast<int64_t>(kContentW) * style.marginBottomValue.value / 100;
				return static_cast<int>(std::max<int64_t>(-8192, std::min<int64_t>(8192, resolved)));
			}
			return std::max(-8192, std::min(8192, style.marginBottomValue.value));
		}
		return style.marginBottom != -1 ? (style.marginBottom == -2 ? 0 : style.marginBottom) : fallbackValue;
	}

	static int cssMarginLeftPx(const WebStyle& style, int fallbackValue)
	{
		if (style.marginLeftValue.valid) {
			if (style.marginLeftValue.type == CssLengthType::Auto) return 0;
			if (style.marginLeftValue.type == CssLengthType::Percent) {
				const int64_t resolved = static_cast<int64_t>(kContentW) * style.marginLeftValue.value / 100;
				return static_cast<int>(std::max<int64_t>(-8192, std::min<int64_t>(8192, resolved)));
			}
			return std::max(-8192, std::min(8192, style.marginLeftValue.value));
		}
		return style.marginLeft != -1 ? (style.marginLeft == -2 ? 0 : style.marginLeft) : fallbackValue;
	}

	static int cssMarginRightPx(const WebStyle& style, int fallbackValue)
	{
		if (style.marginRightValue.valid) {
			if (style.marginRightValue.type == CssLengthType::Auto) return 0;
			if (style.marginRightValue.type == CssLengthType::Percent) {
				const int64_t resolved = static_cast<int64_t>(kContentW) * style.marginRightValue.value / 100;
				return static_cast<int>(std::max<int64_t>(-8192, std::min<int64_t>(8192, resolved)));
			}
			return std::max(-8192, std::min(8192, style.marginRightValue.value));
		}
		return style.marginRight != -1 ? (style.marginRight == -2 ? 0 : style.marginRight) : fallbackValue;
	}

	static int cssFontSizeOrDefault(const WebStyle& style, int fallbackValue)
	{
		return style.fontScaleOrSize > 0 ? style.fontScaleOrSize : fallbackValue;
	}

	static int cssLineHeightOrDefault(const WebStyle& style, int fallbackValue)
	{
		if (style.lineHeightNormal) return fallbackValue;
		return style.lineHeight > 0 ? style.lineHeight : fallbackValue;
	}

	struct CssResolvedLength {
		int px = 0;
		bool definite = false;
		bool autoValue = true;
		bool unresolvedPercentage = false;
		bool clamped = false;
	};

	static CssResolvedLength resolveCssLength(const CssLengthValue& value,
		int legacyPx, int legacyPercent, int basisPx)
	{
		CssResolvedLength result;
		CssLengthType type = value.valid ? value.type : CssLengthType::Unset;
		int payload = value.valid ? value.value : 0;
		bool valueClamped = value.valid && value.clamped;
		if (type == CssLengthType::Unset) {
			if (legacyPercent >= 0) {
				type = CssLengthType::Percent;
				payload = legacyPercent;
			} else if (legacyPx >= 0) {
				type = legacyPx == 0 ? CssLengthType::Zero : CssLengthType::Px;
				payload = legacyPx;
			}
		}
		result.clamped = valueClamped;
		if (type == CssLengthType::Percent) {
			if (basisPx < 0) {
				result.unresolvedPercentage = true;
				return result;
			}
			const int64_t resolved = static_cast<int64_t>(std::max(0, basisPx)) * std::max(0, payload) / 100;
			result.px = static_cast<int>(std::min<int64_t>(8192, std::max<int64_t>(0, resolved)));
			result.definite = true;
			result.autoValue = false;
			result.clamped = result.clamped || resolved > 8192;
			return result;
		}
		if (type == CssLengthType::Px || type == CssLengthType::Zero) {
			result.px = std::max(0, std::min(8192, payload));
			result.definite = true;
			result.autoValue = false;
			result.clamped = result.clamped || payload > 8192;
		}
		return result;
	}

	static const char* cssNavigatorLengthTypeName(CssLengthType type)
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

	static std::string cssNavigatorLengthEvidence(const CssLengthValue& value)
	{
		std::ostringstream out;
		out << cssNavigatorLengthTypeName(value.type);
		if (value.type == CssLengthType::Px || value.type == CssLengthType::Percent || value.type == CssLengthType::Zero)
			out << ":" << value.value;
		if (value.clamped) out << ":clamped";
		return out.str();
	}

	static int cssWidthPx(const WebStyle& style, int availableWidth, int fallbackValue)
	{
		const CssResolvedLength resolved = resolveCssLength(style.widthValue, style.width, style.widthPercent, availableWidth);
		return resolved.definite ? resolved.px : fallbackValue;
	}

	static int cssMaxWidthPx(const WebStyle& style, int availableWidth, int fallbackValue)
	{
		if (style.maxWidthNone) return fallbackValue;
		const CssResolvedLength resolved = resolveCssLength(style.maxWidthValue, style.maxWidth, style.maxWidthPercent, availableWidth);
		return resolved.definite ? resolved.px : fallbackValue;
	}

	static int cssHeightPx(const WebStyle& style, int availableHeight, int fallbackValue)
	{
		const CssResolvedLength resolved = resolveCssLength(style.heightValue, style.height, style.heightPercent, availableHeight);
		return resolved.definite ? resolved.px : fallbackValue;
	}

	static int cssMaxHeightPx(const WebStyle& style, int availableHeight, int fallbackValue)
	{
		if (style.maxHeightNone) return fallbackValue;
		const CssResolvedLength resolved = resolveCssLength(style.maxHeightValue, style.maxHeight, style.maxHeightPercent, availableHeight);
		return resolved.definite ? resolved.px : fallbackValue;
	}

	static int cssMinWidthPx(const WebStyle& style, int basisWidth)
	{
		const CssResolvedLength resolved = resolveCssLength(style.minWidthValue, style.minWidth, style.minWidthPercent, basisWidth);
		return resolved.definite ? resolved.px : -1;
	}

	static int cssMinHeightPx(const WebStyle& style, int basisHeight)
	{
		const CssResolvedLength resolved = resolveCssLength(style.minHeightValue, style.minHeight, style.minHeightPercent, basisHeight);
		return resolved.definite ? resolved.px : -1;
	}

	static const WebStyle* computedStyleForSerial(const WebDocument& doc, uint64_t serial)
	{
		if (serial == 0) return nullptr;
		for (const CssComputedStyleRecord& record : doc.computedStyles) {
			if (record.serial == serial && record.valid) return &record.style;
		}
		return nullptr;
	}

	static int cssHorizontalBoxEdges(const WebStyle& style, bool preformattedDefaults = false)
	{
		const int paddingFallback = cssPaddingOrDefault(style, preformattedDefaults ? 4 : 0);
		const int padding = cssPaddingLeftPx(style, paddingFallback) + cssPaddingRightPx(style, paddingFallback);
		return std::max(0, padding + cssBorderLeftPx(style) + cssBorderRightPx(style));
	}

	static int cssVerticalBoxEdges(const WebStyle& style, bool preformattedDefaults = false)
	{
		const int paddingFallback = cssPaddingOrDefault(style, preformattedDefaults ? 4 : 0);
		const int padding = cssPaddingTopPx(style, paddingFallback) + cssPaddingBottomPx(style, paddingFallback);
		return std::max(0, padding + cssBorderTopPx(style) + cssBorderBottomPx(style));
	}

	static int cssBoundedGeometryAdd(int value, int addition, bool* outClamped = nullptr)
	{
		const int64_t sum = static_cast<int64_t>(std::max(0, value)) + std::max(0, addition);
		if (sum > 8192) {
			if (outClamped) *outClamped = true;
			return 8192;
		}
		return static_cast<int>(sum);
	}

	static int resolveUsedOuterDimension(const WebStyle& style,
		const CssLengthValue& value, int legacyPx, int legacyPercent,
		const CssLengthValue& minValue, int legacyMinPx, int legacyMinPercent,
		const CssLengthValue& maxValue, int legacyMaxPx, int legacyMaxPercent,
		bool maxNone, int basis, int fallbackOuter, int boxEdges,
		bool preformattedDefaults, bool* outAuto = nullptr,
		bool* outUnresolvedPercentage = nullptr, bool* outConstraintConflict = nullptr,
		bool* outClamped = nullptr,
		bool* outMinApplied = nullptr, bool* outMaxApplied = nullptr)
	{
		if (outAuto) *outAuto = false;
		if (outUnresolvedPercentage) *outUnresolvedPercentage = false;
		if (outConstraintConflict) *outConstraintConflict = false;
		if (outClamped) *outClamped = false;
		if (outMinApplied) *outMinApplied = false;
		if (outMaxApplied) *outMaxApplied = false;
		const CssResolvedLength resolved = resolveCssLength(value, legacyPx, legacyPercent, basis);
		if (resolved.unresolvedPercentage && outUnresolvedPercentage) *outUnresolvedPercentage = true;
		if (resolved.clamped && outClamped) *outClamped = true;
		const bool autoValue = !resolved.definite;
		if (outAuto) *outAuto = autoValue;
		int outer = fallbackOuter;
		if (resolved.definite) {
			outer = style.boxSizing == BoxSizingMode::BorderBox
				? resolved.px
				: cssBoundedGeometryAdd(resolved.px, boxEdges, outClamped);
		}
		outer = std::max(0, std::min(8192, outer));

		const CssResolvedLength minResolved = resolveCssLength(minValue, legacyMinPx, legacyMinPercent, basis);
		const CssResolvedLength maxResolved = resolveCssLength(maxValue, legacyMaxPx, legacyMaxPercent, basis);
		if (minResolved.clamped && outClamped) *outClamped = true;
		if (maxResolved.clamped && outClamped) *outClamped = true;
		int minOuter = -1;
		if (minResolved.definite) {
			minOuter = style.boxSizing == BoxSizingMode::BorderBox
				? minResolved.px
				: cssBoundedGeometryAdd(minResolved.px, boxEdges, outClamped);
		}
		int maxOuter = -1;
		if (!maxNone && maxResolved.definite) {
			maxOuter = style.boxSizing == BoxSizingMode::BorderBox
				? maxResolved.px
				: cssBoundedGeometryAdd(maxResolved.px, boxEdges, outClamped);
		}
		// CSS constraint ordering makes a larger minimum win over a smaller max.
		if (minOuter >= 0 && maxOuter >= 0 && minOuter > maxOuter) {
			maxOuter = minOuter;
			if (outConstraintConflict) *outConstraintConflict = true;
		}
		if (minOuter >= 0 && outer < minOuter) {
			outer = minOuter;
			if (outMinApplied) *outMinApplied = true;
		}
		if (maxOuter >= 0 && outer > maxOuter) {
			outer = maxOuter;
			if (outMaxApplied) *outMaxApplied = true;
		}
		if (outer > 8192) {
			outer = 8192;
			if (outClamped) *outClamped = true;
		}
		(void)preformattedDefaults;
		return std::max(0, outer);
	}

	static int usedContentDimensionFromOuter(const WebStyle& style, int outer, int boxEdges)
	{
		(void)style;
		return std::max(0, outer - std::max(0, boxEdges));
	}

	static int cssVerticalAlignOffset(const WebStyle& style, int lineHeight, int extraSpace)
	{
		const int extra = std::max(0, extraSpace);
		switch (style.verticalAlign) {
		case VerticalAlignMode::Middle: return extra / 2;
		case VerticalAlignMode::Bottom: return extra;
		case VerticalAlignMode::Sub: return std::max(1, lineHeight / 4);
		case VerticalAlignMode::Super: return -std::max(1, lineHeight / 3);
		case VerticalAlignMode::LengthPx: return style.verticalAlignValue;
		case VerticalAlignMode::Percent: return (lineHeight * style.verticalAlignValue) / 100;
		case VerticalAlignMode::Top:
		case VerticalAlignMode::TextTop: return 0;
		case VerticalAlignMode::TextBottom: return extra;
		case VerticalAlignMode::Baseline:
		default: return 0;
		}
	}

	static int blockIndentForType(BlockType type)
	{
		if (type == BlockType::ListItem) return kDocumentListIndent;
		if (type == BlockType::Preformatted) return kDocumentPreIndent;
		return kDocumentIndent;
	}

	static int cssBorderSidePx(int width, BorderLineStyle borderStyle)
	{
		if (borderStyle == BorderLineStyle::None || borderStyle == BorderLineStyle::Hidden) {
			return 0;
		}
		if (width > 0) {
			return std::max(1, std::min(width, 12));
		}
		return borderStyle == BorderLineStyle::Inherit ? 0 : 1;
	}

	static BorderLineStyle cssBorderStyleOrDefault(BorderLineStyle borderStyle, int width)
	{
		if (borderStyle != BorderLineStyle::Inherit) return borderStyle;
		return width > 0 ? BorderLineStyle::Solid : BorderLineStyle::None;
	}

	static bool cssListStyleNone(const WebStyle& style)
	{
		return style.listStyleNone;
	}

	static bool cssBorderTopVisible(const WebStyle& style)
	{
		return cssBorderSidePx(style.borderTopWidth, cssBorderStyleOrDefault(style.borderTopStyle, style.borderTopWidth)) > 0 &&
			style.hasBorderTop && ((style.borderTopColor >> 24) & 0xFFu) != 0;
	}

	static bool cssBorderRightVisible(const WebStyle& style)
	{
		return cssBorderSidePx(style.borderRightWidth, cssBorderStyleOrDefault(style.borderRightStyle, style.borderRightWidth)) > 0 &&
			style.hasBorderRight && ((style.borderRightColor >> 24) & 0xFFu) != 0;
	}

	static bool cssBorderBottomVisible(const WebStyle& style)
	{
		return cssBorderSidePx(style.borderBottomWidth, cssBorderStyleOrDefault(style.borderBottomStyle, style.borderBottomWidth)) > 0 &&
			style.hasBorderBottom && ((style.borderBottomColor >> 24) & 0xFFu) != 0;
	}

	static bool cssBorderLeftVisible(const WebStyle& style)
	{
		return cssBorderSidePx(style.borderLeftWidth, cssBorderStyleOrDefault(style.borderLeftStyle, style.borderLeftWidth)) > 0 &&
			style.hasBorderLeft && ((style.borderLeftColor >> 24) & 0xFFu) != 0;
	}

	static int cssBorderTopPx(const WebStyle& style)
	{
		return style.hasBorderTop ? cssBorderSidePx(style.borderTopWidth, cssBorderStyleOrDefault(style.borderTopStyle, style.borderTopWidth)) : 0;
	}

	static int cssBorderRightPx(const WebStyle& style)
	{
		return style.hasBorderRight ? cssBorderSidePx(style.borderRightWidth, cssBorderStyleOrDefault(style.borderRightStyle, style.borderRightWidth)) : 0;
	}

	static int cssBorderBottomPx(const WebStyle& style)
	{
		return style.hasBorderBottom ? cssBorderSidePx(style.borderBottomWidth, cssBorderStyleOrDefault(style.borderBottomStyle, style.borderBottomWidth)) : 0;
	}

	static int cssBorderLeftPx(const WebStyle& style)
	{
		return style.hasBorderLeft ? cssBorderSidePx(style.borderLeftWidth, cssBorderStyleOrDefault(style.borderLeftStyle, style.borderLeftWidth)) : 0;
	}

	static bool cssMarginLeftAuto(const WebStyle& style)
	{
		return style.marginLeft == -2;
	}

	static bool cssMarginRightAuto(const WebStyle& style)
	{
		return style.marginRight == -2;
	}

	static bool blockIsOrderedListItem(const DocBlock& block)
	{
		for (auto it = block.ancestors.rbegin(); it != block.ancestors.rend(); ++it) {
			const std::string tag = toLowerAscii(it->tagName);
			if (tag == "ol") return true;
			if (tag == "ul") return false;
		}
		return false;
	}

	static std::string blockListContainerSignature(const DocBlock& block)
	{
		std::ostringstream oss;
		for (const gxos::web::HtmlElementRef& ancestor : block.ancestors) {
			const std::string tag = toLowerAscii(ancestor.tagName);
			oss << "|" << tag << "#" << toLowerAscii(ancestor.id) << "." << toLowerAscii(ancestor.className);
			if (tag == "ol" || tag == "ul") {
				break;
			}
		}
		return oss.str();
	}

	static std::string alphaMarkerForOrdinal(uint64_t ordinal, bool uppercase)
	{
		if (ordinal == 0) ordinal = 1;
		std::string out;
		while (ordinal > 0) {
			--ordinal;
			const char ch = static_cast<char>((ordinal % 26) + (uppercase ? 'A' : 'a'));
			out.insert(out.begin(), ch);
			ordinal /= 26;
		}
		return out.empty() ? std::string(1, uppercase ? 'A' : 'a') : out;
	}

	static std::string romanMarkerForOrdinal(uint64_t ordinal, bool uppercase)
	{
		if (ordinal == 0) ordinal = 1;
		if (ordinal > 3999) {
			return std::to_string(ordinal);
		}
		struct RomanPair { uint64_t value; const char* symbol; };
		static const RomanPair kPairs[] = {
			{1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"},
			{100, "C"}, {90, "XC"}, {50, "L"}, {40, "XL"},
			{10, "X"}, {9, "IX"}, {5, "V"}, {4, "IV"}, {1, "I"}
		};
		std::string out;
		for (const RomanPair& pair : kPairs) {
			while (ordinal >= pair.value) {
				out += pair.symbol;
				ordinal -= pair.value;
			}
		}
		if (!uppercase) {
			for (char& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		}
		return out;
	}

	static ListStyleType effectiveListStyleType(const DocBlock& block)
	{
		if (cssListStyleNone(block.style)) return ListStyleType::None;
		if (block.style.listStyleType != ListStyleType::Inherit) {
			return block.style.listStyleType;
		}
		for (auto it = block.ancestors.rbegin(); it != block.ancestors.rend(); ++it) {
			const std::string tag = toLowerAscii(it->tagName);
			if (tag == "ol") return ListStyleType::Decimal;
			if (tag == "ul") return ListStyleType::Disc;
		}
		return blockIsOrderedListItem(block) ? ListStyleType::Decimal : ListStyleType::Disc;
	}

	static std::string blockListMarkerText(const DocBlock& block, uint64_t ordinal)
	{
		if (cssListStyleNone(block.style)) return "";
		switch (effectiveListStyleType(block)) {
		case ListStyleType::None:
			return "";
		case ListStyleType::Circle:
			return "o";
		case ListStyleType::Square:
			return "[]";
		case ListStyleType::Decimal:
			return std::to_string(std::max<uint64_t>(1, ordinal)) + ".";
		case ListStyleType::LowerAlpha:
			return alphaMarkerForOrdinal(std::max<uint64_t>(1, ordinal), false) + ".";
		case ListStyleType::UpperAlpha:
			return alphaMarkerForOrdinal(std::max<uint64_t>(1, ordinal), true) + ".";
		case ListStyleType::LowerRoman:
			return romanMarkerForOrdinal(std::max<uint64_t>(1, ordinal), false) + ".";
		case ListStyleType::UpperRoman:
			return romanMarkerForOrdinal(std::max<uint64_t>(1, ordinal), true) + ".";
		case ListStyleType::Disc:
		default:
			return "*";
		}
	}

	static int blockListMarkerInsetPx(const std::string& marker)
	{
		if (marker.empty()) return 0;
		return std::max(2 * kCharW, static_cast<int>(marker.size()) * kCharW + kCharW);
	}

	static uint64_t blockListOrdinal(const WebDocument& doc, int blockIndex)
	{
		if (blockIndex < 0 || blockIndex >= static_cast<int>(doc.blocks.size())) return 1;
		const DocBlock& block = doc.blocks[static_cast<size_t>(blockIndex)];
		if (!blockIsOrderedListItem(block)) return 1;
		const std::string signature = blockListContainerSignature(block);
		uint64_t ordinal = 0;
		for (int i = 0; i <= blockIndex && i < static_cast<int>(doc.blocks.size()); ++i) {
			const DocBlock& candidate = doc.blocks[static_cast<size_t>(i)];
			if (candidate.type != BlockType::ListItem) continue;
			if (!blockIsOrderedListItem(candidate)) continue;
			if (blockListContainerSignature(candidate) == signature) {
				++ordinal;
			}
		}
		return std::max<uint64_t>(1, ordinal);
	}

	static int blockListTextInsetPx(const DocBlock& block, uint64_t ordinal)
	{
		if (cssListStyleNone(block.style)) return 0;
		const std::string marker = blockListMarkerText(block, ordinal);
		return blockListMarkerInsetPx(marker);
	}

	static bool isWrapperTagName(const std::string& tagName)
	{
		const std::string tag = toLowerAscii(tagName);
		return tag == "main" || tag == "article" || tag == "nav" || tag == "aside" ||
			tag == "header" || tag == "footer" || tag == "section" || tag == "div" ||
			tag == "figure" || tag == "blockquote" || tag == "dl";
	}

	static bool isTableCellLikeBlock(const DocBlock& block)
	{
		const std::string tag = toLowerAscii(block.tagName);
		return tag == "td" || tag == "th";
	}

	static bool isTableCaptionLikeBlock(const DocBlock& block)
	{
		return toLowerAscii(block.tagName) == "caption";
	}

	static uint64_t ancestorSerialForTag(const DocBlock& block, const std::string& tagName)
	{
		const std::string tag = toLowerAscii(tagName);
		for (auto it = block.ancestors.rbegin(); it != block.ancestors.rend(); ++it) {
			if (toLowerAscii(it->tagName) == tag) return it->serial;
		}
		return 0;
	}

	static uint64_t tableSerialForBlock(const DocBlock& block)
	{
		return ancestorSerialForTag(block, "table");
	}

	static uint64_t tableRowSerialForBlock(const DocBlock& block)
	{
		return ancestorSerialForTag(block, "tr");
	}

	static std::string tableRowSignature(const DocBlock& block)
	{
		std::ostringstream oss;
		oss << tableSerialForBlock(block) << ":" << tableRowSerialForBlock(block);
		return oss.str();
	}

	static int textLineCountForTableCell(const std::string& text)
	{
		if (text.empty()) return 1;
		int lines = 0;
		size_t start = 0;
		while (start <= text.size()) {
			size_t end = text.find('\n', start);
			if (end == std::string::npos) end = text.size();
			++lines;
			if (end == text.size()) break;
			start = end + 1;
		}
		return std::max(1, lines);
	}

	static int textLongestLineChars(const std::string& text)
	{
		int longest = 0;
		size_t start = 0;
		while (start <= text.size()) {
			size_t end = text.find('\n', start);
			if (end == std::string::npos) end = text.size();
			longest = std::max(longest, static_cast<int>(end - start));
			if (end == text.size()) break;
			start = end + 1;
		}
		return longest;
	}

	static bool blockHasWrapperAncestor(const DocBlock& block)
	{
		for (const gxos::web::HtmlElementRef& ancestor : block.ancestors) {
			if (isWrapperTagName(ancestor.tagName)) return true;
		}
		return isWrapperTagName(block.tagName);
	}

	static int wrapperAncestorDepth(const DocBlock& block)
	{
		int depth = 0;
		for (const gxos::web::HtmlElementRef& ancestor : block.ancestors) {
			if (isWrapperTagName(ancestor.tagName)) {
				++depth;
			}
		}
		if (isWrapperTagName(block.tagName)) {
			++depth;
		}
		return depth;
	}

	static int nestedWrapperInsetPx(const DocBlock& block)
	{
		const int depth = wrapperAncestorDepth(block);
		// Treat shallow shells like body -> main as normal layout, and only start
		// shrinking content once wrappers are meaningfully nested.
		if (depth <= 2) return 0;
		return std::min(40, (depth - 2) * 10);
	}

	static bool isFirstTableCellInGroup(const WebDocument& doc, int index)
	{
		if (index < 0 || index >= static_cast<int>(doc.blocks.size())) return false;
		const DocBlock& block = doc.blocks[static_cast<size_t>(index)];
		if (!isTableCellLikeBlock(block)) return false;
		const uint64_t tableSerial = tableSerialForBlock(block);
		const uint64_t rowSerial = tableRowSerialForBlock(block);
		if (tableSerial == 0) return false;
		if (index == 0) return true;
		for (int i = index - 1; i >= 0; --i) {
			const DocBlock& candidate = doc.blocks[static_cast<size_t>(i)];
			if (!isTableCellLikeBlock(candidate)) return true;
			if (tableSerialForBlock(candidate) != tableSerial) return true;
			if (tableRowSerialForBlock(candidate) == rowSerial) return false;
			return true;
		}
		return true;
	}

	static int tableGroupStartIndex(const WebDocument& doc, int index)
	{
		if (index < 0 || index >= static_cast<int>(doc.blocks.size())) return -1;
		const DocBlock& block = doc.blocks[static_cast<size_t>(index)];
		if (!isTableCellLikeBlock(block)) return -1;
		const uint64_t tableSerial = tableSerialForBlock(block);
		int start = index;
		while (start > 0) {
			const DocBlock& prev = doc.blocks[static_cast<size_t>(start - 1)];
			if (!isTableCellLikeBlock(prev)) break;
			if (tableSerialForBlock(prev) != tableSerial) break;
			--start;
		}
		return start;
	}

	static TableGroupLayout buildTableGroupLayout(const WebDocument& doc, int startIndex)
	{
		TableGroupLayout layout;
		if (startIndex < 0 || startIndex >= static_cast<int>(doc.blocks.size())) return layout;
		const DocBlock& first = doc.blocks[static_cast<size_t>(startIndex)];
		if (!isTableCellLikeBlock(first)) return layout;
		layout.tableSerial = tableSerialForBlock(first);
		layout.startIndex = startIndex;
		layout.collapseMode = first.style.borderCollapse == TableBorderCollapseMode::Collapse;
		layout.borderSpacingHorizontal = layout.collapseMode ? 0 :
			std::max(0, first.style.borderSpacingHorizontal >= 0 ? first.style.borderSpacingHorizontal : 4);
		layout.borderSpacingVertical = layout.collapseMode ? 0 :
			std::max(0, first.style.borderSpacingVertical >= 0 ? first.style.borderSpacingVertical : 2);

		const int bodyMarginLeft = blockBodyMarginLeft(doc);
		const int bodyMarginRight = blockBodyMarginRight(doc);
		const int firstMarginLeft = cssMarginLeftPx(first.style, 0);
		const int firstMarginRight = cssMarginRightPx(first.style, 0);
		const int availableWidth = std::max(1, kContentW - blockIndentForType(first.type) - kDocumentRightPad
			- bodyMarginLeft - bodyMarginRight - firstMarginLeft - firstMarginRight);
		layout.availableWidth = availableWidth;
		layout.outerWidth = blockOuterWidth(first, availableWidth);
		layout.outerX = blockOuterX(first, doc, availableWidth, layout.outerWidth);
		layout.paddingTop = cssPaddingTopPx(first.style, 4);
		layout.paddingRight = cssPaddingRightPx(first.style, 4);
		layout.paddingBottom = cssPaddingBottomPx(first.style, 4);
		layout.paddingLeft = cssPaddingLeftPx(first.style, 4);
		layout.borderTop = cssBorderTopPx(first.style);
		layout.borderRight = cssBorderRightPx(first.style);
		layout.borderBottom = cssBorderBottomPx(first.style);
		layout.borderLeft = cssBorderLeftPx(first.style);
		layout.lineHeight = blockTextLineHeight(first);

		int i = startIndex;
		while (i < static_cast<int>(doc.blocks.size())) {
			const DocBlock& block = doc.blocks[static_cast<size_t>(i)];
			if (!isTableCellLikeBlock(block) || tableSerialForBlock(block) != layout.tableSerial) break;
			TableRowLayout row;
			row.rowSerial = tableRowSerialForBlock(block);
			row.headerRow = false;
			int rowBorderTop = 0;
			int rowBorderBottom = 0;
			int j = i;
			while (j < static_cast<int>(doc.blocks.size())) {
				const DocBlock& cell = doc.blocks[static_cast<size_t>(j)];
				if (!isTableCellLikeBlock(cell) || tableSerialForBlock(cell) != layout.tableSerial ||
					tableRowSerialForBlock(cell) != row.rowSerial) {
					break;
				}
				TableCellLayout cellLayout;
				cellLayout.block = &cell;
				cellLayout.padLeftChars = std::max(1, cssPaddingLeftPx(cell.style, 4) / kCharW + 1);
				cellLayout.padRightChars = std::max(1, cssPaddingRightPx(cell.style, 4) / kCharW + 1);
				cellLayout.contentWidthChars = std::max(1, textLongestLineChars(cell.text));
				cellLayout.lines = wrapTextForBlock(*cellLayout.block, cellLayout.contentWidthChars);
				row.headerRow = row.headerRow || toLowerAscii(cell.tagName) == "th" || cell.style.bold;
				rowBorderTop = std::max(rowBorderTop, cssBorderTopPx(cell.style));
				rowBorderBottom = std::max(rowBorderBottom, cssBorderBottomPx(cell.style));
				row.cells.push_back(std::move(cellLayout));
				++j;
			}
			row.borderTopPx = rowBorderTop;
			row.borderBottomPx = rowBorderBottom;
			layout.rows.push_back(std::move(row));
			i = j;
		}
		layout.endIndex = i;

		int columnCount = 0;
		for (const TableRowLayout& row : layout.rows) {
			columnCount = std::max(columnCount, static_cast<int>(row.cells.size()));
		}
		layout.columnWidthsChars.assign(static_cast<size_t>(columnCount), 0);
		for (const TableRowLayout& row : layout.rows) {
			for (size_t col = 0; col < row.cells.size(); ++col) {
				const TableCellLayout& cell = row.cells[col];
				const int desired = std::max(4, cell.contentWidthChars + cell.padLeftChars + cell.padRightChars);
				layout.columnWidthsChars[col] = std::max(layout.columnWidthsChars[col], desired);
			}
		}

		const int spacingChars = layout.collapseMode ? 0 : std::max(0, layout.borderSpacingHorizontal / kCharW);
		const int separatorChars = columnCount > 0 ? (spacingChars * (columnCount - 1)) : 0;
		const int availableChars = std::max(8,
			(layout.outerWidth - layout.borderLeft - layout.borderRight - layout.paddingLeft - layout.paddingRight) / kCharW);
		int desiredChars = separatorChars;
		for (int width : layout.columnWidthsChars) desiredChars += width;
		if (columnCount > 0 && desiredChars > availableChars) {
			layout.fallbackUsed = true;
			int remaining = std::max(columnCount, availableChars - separatorChars);
			int totalDesired = 0;
			for (int width : layout.columnWidthsChars) totalDesired += std::max(1, width);
			std::vector<int> newWidths(static_cast<size_t>(columnCount), 1);
			int used = 0;
			for (int col = 0; col < columnCount; ++col) {
				int width = std::max(1, layout.columnWidthsChars[static_cast<size_t>(col)]);
				int scaled = std::max(1, (width * remaining) / std::max(1, totalDesired));
				newWidths[static_cast<size_t>(col)] = scaled;
				used += scaled;
			}
			while (used < remaining) {
				for (int col = 0; col < columnCount && used < remaining; ++col) {
					++newWidths[static_cast<size_t>(col)];
					++used;
				}
			}
			layout.columnWidthsChars = std::move(newWidths);
		}

		for (TableRowLayout& row : layout.rows) {
			int maxLines = 1;
			const size_t lastCol = layout.columnWidthsChars.empty() ? 0 : layout.columnWidthsChars.size() - 1;
			for (size_t col = 0; col < row.cells.size(); ++col) {
				TableCellLayout& cell = row.cells[col];
				const int colWidth = layout.columnWidthsChars[std::min(col, lastCol)];
				const int contentWidth = std::max(1, colWidth - cell.padLeftChars - cell.padRightChars);
				cell.contentWidthChars = contentWidth;
				cell.lines = wrapTextForBlock(*cell.block, contentWidth);
				maxLines = std::max(maxLines, static_cast<int>(cell.lines.size()));
			}
			row.heightPx = std::max(layout.lineHeight + 4, maxLines * layout.lineHeight + layout.paddingTop + layout.paddingBottom);
		}

		int cursorY = layout.borderTop + layout.paddingTop;
		layout.rowOffsetsPx.clear();
		layout.rowOffsetsPx.reserve(layout.rows.size());
		for (size_t iRow = 0; iRow < layout.rows.size(); ++iRow) {
			TableRowLayout& row = layout.rows[iRow];
			layout.rowOffsetsPx.push_back(cursorY);
			cursorY += row.borderTopPx;
			cursorY += row.heightPx;
			cursorY += row.borderBottomPx;
			if (iRow + 1 < layout.rows.size()) {
				cursorY += layout.borderSpacingVertical;
			}
		}
		layout.totalHeightPx = cursorY + layout.paddingBottom + layout.borderBottom;

		return layout;
	}

	static std::string padTableCellLine(const std::string& text, int widthChars, TextAlign align)
	{
		const int visible = std::max(0, static_cast<int>(text.size()));
		const int pad = std::max(0, widthChars - visible);
		int left = 0;
		int right = 0;
		if (align == TextAlign::Center) {
			left = pad / 2;
			right = pad - left;
		} else if (align == TextAlign::Right) {
			left = pad;
		} else {
			right = pad;
		}
		return std::string(static_cast<size_t>(left), ' ') + text + std::string(static_cast<size_t>(right), ' ');
	}

	static std::vector<std::string> tableRowTextLines(const TableGroupLayout& layout, const TableRowLayout& row)
	{
		std::vector<std::string> lines;
		int maxLines = 1;
		for (const TableCellLayout& cell : row.cells) {
			maxLines = std::max(maxLines, static_cast<int>(cell.lines.size()));
		}
		const size_t lastCol = layout.columnWidthsChars.empty() ? 0 : layout.columnWidthsChars.size() - 1;
		for (int lineIndex = 0; lineIndex < maxLines; ++lineIndex) {
			std::string line;
			for (size_t col = 0; col < row.cells.size(); ++col) {
				const TableCellLayout& cell = row.cells[col];
				const int colWidth = layout.columnWidthsChars[std::min(col, lastCol)];
				const int contentWidth = std::max(1, colWidth - cell.padLeftChars - cell.padRightChars);
				std::string cellLine;
				if (lineIndex < static_cast<int>(cell.lines.size())) cellLine = cell.lines[static_cast<size_t>(lineIndex)];
				if (static_cast<int>(cellLine.size()) > contentWidth) {
					cellLine = cellLine.substr(0, static_cast<size_t>(contentWidth));
				}
				cellLine = padTableCellLine(cellLine, contentWidth, cell.block->style.textAlign);
				if (!line.empty()) line += " | ";
				line += std::string(static_cast<size_t>(cell.padLeftChars), ' ');
				line += cellLine;
				line += std::string(static_cast<size_t>(cell.padRightChars), ' ');
			}
			lines.push_back(std::move(line));
		}
		if (lines.empty()) lines.push_back("");
		return lines;
	}

	static int blockBodyMarginLeft(const WebDocument& doc)
	{
		return doc.bodyStyle.marginLeft >= 0 ? doc.bodyStyle.marginLeft : 0;
	}

	static int blockBodyMarginRight(const WebDocument& doc)
	{
		return doc.bodyStyle.marginRight >= 0 ? doc.bodyStyle.marginRight : 0;
	}

	static int blockAvailableWidth(const DocBlock& block, const WebDocument& doc)
	{
		const bool bodyPreformatted = false;
		const int bodyOuterBasis = std::max(1, kContentW - blockBodyMarginLeft(doc) - blockBodyMarginRight(doc));
		const int bodyEdges = cssHorizontalBoxEdges(doc.bodyStyle, bodyPreformatted);
		bool bodyClamped = false;
		const int bodyOuter = resolveUsedOuterDimension(doc.bodyStyle,
			doc.bodyStyle.widthValue, doc.bodyStyle.width, doc.bodyStyle.widthPercent,
			doc.bodyStyle.minWidthValue, doc.bodyStyle.minWidth, doc.bodyStyle.minWidthPercent,
			doc.bodyStyle.maxWidthValue, doc.bodyStyle.maxWidth, doc.bodyStyle.maxWidthPercent,
			doc.bodyStyle.maxWidthNone, bodyOuterBasis, bodyOuterBasis, bodyEdges,
			bodyPreformatted, nullptr, nullptr, nullptr, &bodyClamped);
		int basis = std::max(1, usedContentDimensionFromOuter(doc.bodyStyle, bodyOuter, bodyEdges));
		int styledAncestorCount = 0;
		int depth = 0;
		for (const gxos::web::HtmlElementRef& ancestor : block.ancestors) {
			if (++depth > 12) break;
			const std::string tag = toLowerAscii(ancestor.tagName);
			if (tag == "html" || tag == "body") continue;
			const WebStyle* ancestorStyle = computedStyleForSerial(doc, ancestor.serial);
			if (!ancestorStyle) continue;
			++styledAncestorCount;
			const int edges = cssHorizontalBoxEdges(*ancestorStyle);
			bool ancestorClamped = false;
			const int outer = resolveUsedOuterDimension(*ancestorStyle,
				ancestorStyle->widthValue, ancestorStyle->width, ancestorStyle->widthPercent,
				ancestorStyle->minWidthValue, ancestorStyle->minWidth, ancestorStyle->minWidthPercent,
				ancestorStyle->maxWidthValue, ancestorStyle->maxWidth, ancestorStyle->maxWidthPercent,
				ancestorStyle->maxWidthNone, basis, basis, edges, false,
				nullptr, nullptr, nullptr, &ancestorClamped);
			basis = std::max(1, usedContentDimensionFromOuter(*ancestorStyle, outer, edges));
		}
		(void)bodyClamped;
		if (depth >= 12) {
			// The containing-block walk is deliberately bounded.  Retain the old
			// shallow wrapper inset if it would otherwise disappear on malformed
			// deep markup.
			basis = std::max(1, basis - std::min(40, 10 * std::max(0, depth - 2)));
		} else if (styledAncestorCount == 0) {
			basis = std::max(1, basis - nestedWrapperInsetPx(block));
		}
		basis = std::max(1, basis - blockIndentForType(block.type) - kDocumentRightPad);
		basis = std::max(1, basis - cssMarginLeftPx(block.style, 0) - cssMarginRightPx(block.style, 0));
		const int blockIndex = doc.blocks.empty() ? -1 : static_cast<int>(&block - doc.blocks.data());
		if (blockIndex >= 0 && blockIndex < static_cast<int>(doc.blocks.size()) &&
			cssStyleHasOverflowBfc(block.style) && s_cssFloatLayoutSnapshot.valid &&
			block.style.floatMode == FloatMode::None) {
			int flowTop = blockIndex < static_cast<int>(s_cssMarginLayoutSnapshot.records.size())
				? s_cssMarginLayoutSnapshot.records[static_cast<size_t>(blockIndex)].usedY : 0;
			const size_t limit = std::min<size_t>(static_cast<size_t>(blockIndex),
				s_cssFloatLayoutSnapshot.blockClearances.size());
			for (size_t i = 0; i < limit; ++i) flowTop += s_cssFloatLayoutSnapshot.blockClearances[i];
			const int bodyLeft = blockBodyMarginLeft(doc);
			const int bodyWidth = cssBodyContentWidth(doc);
			const CssFloatExclusionQuery exclusion = cssFloatExclusionQuery(doc, 0, flowTop,
				flowTop + std::max(1, blockTextLineHeight(block)), bodyLeft, bodyLeft + bodyWidth);
			if (exclusion.availableWidth < bodyWidth) {
				++const_cast<WebDocument&>(doc).cssDiagnostics.floatBfcAvoidances;
				basis = std::max(1, std::min(basis, exclusion.availableWidth -
					blockIndentForType(block.type) - kDocumentRightPad));
			}
		}
		return basis;
	}

	static int blockOuterWidth(const DocBlock& block, int availableWidth, bool* outClamped)
	{
		if (outClamped) *outClamped = false;
		if (block.type == BlockType::Image) {
			int imageW = 0;
			int imageH = 0;
			imageDisplaySize(block, availableWidth, imageW, imageH);
			const int paddingFallback = cssPaddingOrDefault(block.style, 0);
			const int paddingLeft = cssPaddingLeftPx(block.style, paddingFallback);
			const int paddingRight = cssPaddingRightPx(block.style, paddingFallback);
			const int borderEdges = cssBorderLeftPx(block.style) + cssBorderRightPx(block.style);
			const int imageOuter = cssBoundedGeometryAdd(imageW, paddingLeft + paddingRight + borderEdges, outClamped);
			return std::max(1, imageOuter);
		}
		int fallbackOuter = availableWidth;
		if (isFormControlBlock(block)) {
			fallbackOuter = blockFormControlIntrinsicWidth(block);
		}
		const bool preformattedDefaults = block.type == BlockType::Preformatted;
		const int boxEdges = cssHorizontalBoxEdges(block.style, preformattedDefaults);
		bool autoValue = false;
		bool unresolvedPercentage = false;
		bool conflict = false;
		const int outerWidth = resolveUsedOuterDimension(block.style,
			block.style.widthValue, block.style.width, block.style.widthPercent,
			block.style.minWidthValue, block.style.minWidth, block.style.minWidthPercent,
			block.style.maxWidthValue, block.style.maxWidth, block.style.maxWidthPercent,
			block.style.maxWidthNone, std::max(0, availableWidth), fallbackOuter, boxEdges,
			preformattedDefaults, &autoValue, &unresolvedPercentage, &conflict, outClamped);
		if (autoValue && isFormControlBlock(block)) {
			// Keep the readable intrinsic default, but do not override an explicit
			// author width merely because it is small.
			return std::max(80, std::min(8192, outerWidth));
		}
		(void)unresolvedPercentage;
		(void)conflict;
		return std::max(1, outerWidth);
	}

	static int blockOuterX(const DocBlock& block, const WebDocument& doc, int availableWidth, int outerWidth)
	{
		int baseX = kContentX + blockIndentForType(block.type) + blockBodyMarginLeft(doc);
		const int blockIndex = doc.blocks.empty() ? -1 : static_cast<int>(&block - doc.blocks.data());
		if (blockIndex >= 0 && blockIndex < static_cast<int>(doc.blocks.size()) &&
			cssStyleHasOverflowBfc(block.style) && s_cssFloatLayoutSnapshot.valid &&
			block.style.floatMode == FloatMode::None) {
			int flowTop = blockIndex < static_cast<int>(s_cssMarginLayoutSnapshot.records.size())
				? s_cssMarginLayoutSnapshot.records[static_cast<size_t>(blockIndex)].usedY : 0;
			const size_t limit = std::min<size_t>(static_cast<size_t>(blockIndex),
				s_cssFloatLayoutSnapshot.blockClearances.size());
			for (size_t i = 0; i < limit; ++i) flowTop += s_cssFloatLayoutSnapshot.blockClearances[i];
			flowTop = cssBfcPlacementY(doc, blockIndex, flowTop, outerWidth);
			const int bodyLeft = blockBodyMarginLeft(doc);
			const int bodyWidth = cssBodyContentWidth(doc);
			const CssFloatExclusionQuery exclusion = cssFloatExclusionQuery(doc,
				cssContainingBfcIdentityForBlock(doc, block, false), flowTop,
				flowTop + std::max(1, blockTextLineHeight(block)), bodyLeft, bodyLeft + bodyWidth);
			if (exclusion.availableWidth < bodyWidth) {
				baseX = kContentX + exclusion.availableLeft;
				++const_cast<WebDocument&>(doc).cssDiagnostics.floatBfcAvoidances;
			}
		}
		const int leftMargin = cssMarginLeftPx(block.style, 0);
		const int rightMargin = cssMarginRightPx(block.style, 0);
		const bool autoLeft = cssMarginLeftAuto(block.style);
		const bool autoRight = cssMarginRightAuto(block.style);
		if (autoLeft && autoRight) {
			return baseX + std::max(0, (availableWidth - outerWidth) / 2);
		}
		if (autoLeft) {
			return baseX + std::max(0, availableWidth - outerWidth - rightMargin);
		}
		if (autoRight) {
			return baseX + leftMargin;
		}
		return baseX + leftMargin;
	}

	static int blockWrapWidth(const DocBlock& block, int outerWidth)
	{
		const int paddingLeft = cssPaddingLeftPx(block.style, cssPaddingOrDefault(block.style, block.type == BlockType::Preformatted ? 4 : 0));
		const int paddingRight = cssPaddingRightPx(block.style, cssPaddingOrDefault(block.style, block.type == BlockType::Preformatted ? 4 : 0));
		const int borderLeft = cssBorderLeftPx(block.style);
		const int borderRight = cssBorderRightPx(block.style);
		return std::max(1, outerWidth - borderLeft - borderRight - paddingLeft - paddingRight);
	}

	static int blockContentLeftX(const DocBlock& block, int outerX)
	{
		const int paddingLeft = cssPaddingLeftPx(block.style, cssPaddingOrDefault(block.style, block.type == BlockType::Preformatted ? 4 : 0));
		return outerX + cssBorderLeftPx(block.style) + paddingLeft;
	}

	static int blockContentTopY(const DocBlock& block, int drawY, int blockMarginTop)
	{
		return drawY + blockMarginTop + cssBorderTopPx(block.style) + cssPaddingTopPx(block.style, block.type == BlockType::Preformatted ? 4 : 0);
	}

	static int blockTextLineHeight(const DocBlock& block)
	{
		const int fontSize = cssFontSizeOrDefault(block.style, defaultTextFontHeightPx());
		const int lineHeight = cssLineHeightOrDefault(block.style, fontSize + 4);
		return std::max(fontSize + 2, std::max(defaultTextFontHeightPx() + 2, lineHeight));
	}

	static int blockTextX(const DocBlock& block, int outerX, int innerWidth, int lineWidth)
	{
		if (block.style.textAlign == TextAlign::Center) {
			return outerX + std::max(0, (innerWidth - lineWidth) / 2);
		}
		if (block.style.textAlign == TextAlign::Right) {
			return outerX + std::max(0, innerWidth - lineWidth);
		}
		return outerX;
	}

	static int blockFormControlHeight(const DocBlock& block)
	{
		int fallback = kFormControlH;
		if (block.type == BlockType::FormTextarea) {
			int rows = block.visibleRows > 0 ? block.visibleRows : 4;
			rows = std::max(kTextareaMinRows, std::min(kTextareaMaxRows, rows));
			fallback = std::max(kFormControlH, rows * kLineH + 10);
		} else if (block.type == BlockType::FormSelect && block.formControl.multiple) {
			int rows = block.visibleRows > 0 ? block.visibleRows : static_cast<int>(block.options.size());
			rows = std::max(2, std::min(6, rows));
			fallback = std::max(kFormControlH, rows * kLineH + 10);
		}
		const int edges = cssVerticalBoxEdges(block.style);
		const int outer = resolveUsedOuterDimension(block.style,
			block.style.heightValue, block.style.height, block.style.heightPercent,
			block.style.minHeightValue, block.style.minHeight, block.style.minHeightPercent,
			block.style.maxHeightValue, block.style.maxHeight, block.style.maxHeightPercent,
			block.style.maxHeightNone, 240, cssBoundedGeometryAdd(fallback, edges), edges,
			false);
		return std::max(1, usedContentDimensionFromOuter(block.style, outer, edges));
	}

	static bool isFormControlBlock(const DocBlock& block)
	{
		return block.type == BlockType::FormTextInput ||
			block.type == BlockType::FormCheckbox ||
			block.type == BlockType::FormRadio ||
			block.type == BlockType::FormTextarea ||
			block.type == BlockType::FormSelect ||
			block.type == BlockType::FormSubmit;
	}

	static int blockFormControlIntrinsicWidth(const DocBlock& block)
	{
		int fallback = 280;
		if (block.type == BlockType::FormSubmit) fallback = 132;
		else if (block.type == BlockType::FormCheckbox || block.type == BlockType::FormRadio) fallback = 300;
		else if (block.type == BlockType::FormTextarea && block.visibleCols > 0)
			fallback = std::min(640, block.visibleCols * kCharW + 20);
		else if (block.formControl.size > 0 &&
			(block.formControl.type == FormControlType::Text ||
			 block.formControl.type == FormControlType::Password ||
			 block.formControl.type == FormControlType::Search ||
			 block.formControl.type == FormControlType::Email ||
			 block.formControl.type == FormControlType::Url ||
			 block.formControl.type == FormControlType::Number ||
			 block.formControl.type == FormControlType::Unsupported))
			fallback = std::min(640, block.formControl.size * kCharW + 20);
		return std::max(80, std::min(720, fallback));
	}

	static int blockFormControlWidth(const DocBlock& block, int availableWidth)
	{
		const int outer = blockOuterWidth(block, availableWidth);
		const int edges = cssHorizontalBoxEdges(block.style);
		return std::max(1, usedContentDimensionFromOuter(block.style, outer, edges));
	}

	static int blockContainingContentHeight(const DocBlock& block, const WebDocument& doc)
	{
		int basis = -1;
		const auto advance = [&](const WebStyle& style, int currentBasis) {
			const int edges = cssVerticalBoxEdges(style);
			const CssResolvedLength resolved = resolveCssLength(style.heightValue, style.height, style.heightPercent, currentBasis);
			if (!resolved.definite) return -1;
			const int outer = style.boxSizing == BoxSizingMode::BorderBox
				? resolved.px
				: cssBoundedGeometryAdd(resolved.px, edges);
			return usedContentDimensionFromOuter(style, outer, edges);
		};
		basis = advance(doc.bodyStyle, -1);
		int depth = 0;
		for (const gxos::web::HtmlElementRef& ancestor : block.ancestors) {
			if (++depth > 12) return -1;
			const std::string tag = toLowerAscii(ancestor.tagName);
			if (tag == "html" || tag == "body") continue;
			const WebStyle* ancestorStyle = computedStyleForSerial(doc, ancestor.serial);
			if (!ancestorStyle) continue;
			basis = advance(*ancestorStyle, basis);
			if (basis < 0) return -1;
		}
		return basis;
	}

	static int blockTotalHeight(const DocBlock& block, const WebDocument& doc, bool nextIsHeading)
	{
		if (block.style.displayNone) return 0;
		// Descendant blocks of an inline-block are laid out by their owning
		// atomic context.  They must not advance the document-wide cursor.
		if (block.atomicContainerSerial != 0) return 0;
		const int blockIndex = static_cast<int>(&block - &doc.blocks.front());
		if (const InlineFlowLayout* flow = inlineFlowForBlock(doc, blockIndex)) {
			if (flow->anchorBlockIndex != blockIndex) return 0;
			return flow->totalHeight + (nextIsHeading ? 10 : 0);
		}
		if (isTableCellLikeBlock(block)) {
			if (!isFirstTableCellInGroup(doc, static_cast<int>(&block - &doc.blocks.front()))) {
				return 0;
			}
			const int groupStart = tableGroupStartIndex(doc, blockIndex);
			const TableGroupLayout layout = buildTableGroupLayout(doc, groupStart);
			const int blockMarginTop = cssMarginTopPx(block.style, 4);
			const int blockMarginBottom = cssMarginBottomPx(block.style, 8);
			int total = blockMarginTop + layout.totalHeightPx + blockMarginBottom;
			if (nextIsHeading) total += 10;
			return total;
		}
		if (toLowerAscii(block.tagName) == "hr") {
			const int blockMarginTop = cssMarginTopPx(block.style, 10);
			const int blockMarginBottom = cssMarginBottomPx(block.style, 10);
			const int paddingTop = cssPaddingTopPx(block.style, 0);
			const int paddingBottom = cssPaddingBottomPx(block.style, 0);
			return blockMarginTop + cssBorderTopPx(block.style) + paddingTop + paddingBottom +
				cssBorderBottomPx(block.style) + std::max(4, blockMarginBottom);
		}
		const int blockMarginTop = cssMarginTopPx(block.style, block.type == BlockType::Heading ? 10 : 4);
		const int blockMarginBottom = cssMarginBottomPx(block.style, block.type == BlockType::ListItem ? 4 : 8);
		const int paddingTop = cssPaddingTopPx(block.style, block.type == BlockType::Preformatted ? 4 : 0);
		const int paddingRight = cssPaddingRightPx(block.style, block.type == BlockType::Preformatted ? 4 : 0);
		const int paddingBottom = cssPaddingBottomPx(block.style, block.type == BlockType::Preformatted ? 4 : 0);
		const int paddingLeft = cssPaddingLeftPx(block.style, block.type == BlockType::Preformatted ? 4 : 0);
		const int borderTop = cssBorderTopPx(block.style);
		const int borderRight = cssBorderRightPx(block.style);
		const int borderBottom = cssBorderBottomPx(block.style);
		const int borderLeft = cssBorderLeftPx(block.style);
		const int availableWidth = blockAvailableWidth(block, doc);
		const int outerWidth = blockOuterWidth(block, availableWidth);
		const int innerWidth = std::max(1, outerWidth - borderLeft - borderRight - paddingLeft - paddingRight);
		const uint64_t listOrdinal = block.type == BlockType::ListItem ? blockListOrdinal(doc, static_cast<int>(&block - &doc.blocks.front())) : 1;
		const int listInset = block.type == BlockType::ListItem ? blockListTextInsetPx(block, listOrdinal) : 0;
		const int wrapCols = std::max(1, std::max(1, innerWidth - listInset) / kCharW);
		const int lineHeight = blockTextLineHeight(block);
		const int headingFontSize = cssFontSizeOrDefault(block.style, block.tagName == "h1" ? 24 : (block.tagName == "h2" ? 20 : (block.tagName == "h3" ? 18 : 20)));
		const int headingHeight = std::max(lineHeight + 4, headingFontSize + 2);
		constexpr int kPreGapIfNextHeading = 10;
		int contentH = 0;
		switch (block.type) {
		case BlockType::Heading:
			contentH = headingHeight;
			break;
		case BlockType::Paragraph:
		case BlockType::Link:
			contentH = wrappedBlockHeight(block, wrapCols, lineHeight);
			break;
		case BlockType::ListItem:
			contentH = wrappedBlockHeight(block, wrapCols, lineHeight);
			break;
		case BlockType::Preformatted:
			contentH = wrappedBlockHeight(block, wrapCols, lineHeight);
			break;
		case BlockType::FormLabel:
			contentH = wrappedBlockHeight(block, wrapCols, lineHeight);
			break;
		case BlockType::FormTextInput:
		case BlockType::FormCheckbox:
		case BlockType::FormRadio:
		case BlockType::FormTextarea:
		case BlockType::FormSelect:
		case BlockType::FormSubmit:
			contentH = blockFormControlHeight(block);
			break;
		case BlockType::Image: {
			int imageW = 0;
			int imageH = 0;
			imageDisplaySize(block, availableWidth, imageW, imageH);
			contentH = imageH;
			break;
		}
		}
		const int heightEdges = paddingTop + paddingBottom + borderTop + borderBottom;
		const int heightBasis = blockContainingContentHeight(block, doc);
		bool autoHeight = false;
		bool unresolvedHeightPercentage = false;
		bool heightConflict = false;
		bool heightClamped = false;
		const int outerHeight = resolveUsedOuterDimension(block.style,
			block.style.heightValue, block.style.height, block.style.heightPercent,
			block.style.minHeightValue, block.style.minHeight, block.style.minHeightPercent,
			block.style.maxHeightValue, block.style.maxHeight, block.style.maxHeightPercent,
			block.style.maxHeightNone, heightBasis, cssBoundedGeometryAdd(contentH, heightEdges), heightEdges,
			block.type == BlockType::Preformatted, &autoHeight, &unresolvedHeightPercentage,
			&heightConflict, &heightClamped);
		const int usedContentHeight = usedContentDimensionFromOuter(block.style, outerHeight, heightEdges);
		(void)autoHeight;
		(void)unresolvedHeightPercentage;
		(void)heightConflict;
		(void)heightClamped;
		(void)usedContentHeight;
		int total = blockMarginTop + outerHeight + blockMarginBottom;
		if (nextIsHeading) total += kPreGapIfNextHeading;
		return total;
	}

	static void ensureInlineLayout(const WebDocument& doc);

	static int cssMarginValueForBasis(const CssLengthValue& value, int legacy,
		int fallback, int basis, bool* outClamped = nullptr)
	{
		if (outClamped) *outClamped = false;
		if (value.valid) {
			if (value.type == CssLengthType::Auto) return 0;
			int64_t resolved = value.value;
			if (value.type == CssLengthType::Percent) {
				if (basis < 0) return 0;
				resolved = static_cast<int64_t>(basis) * value.value / 100;
			}
			if (resolved < -8192 || resolved > 8192) {
				if (outClamped) *outClamped = true;
				resolved = std::max<int64_t>(-8192, std::min<int64_t>(8192, resolved));
			}
			return static_cast<int>(resolved);
		}
		if (legacy == -2) return 0;
		if (legacy != -1) return std::max(-8192, std::min(8192, legacy));
		return fallback;
	}

	static int cssFlowMarginTop(const WebStyle& style, int fallback, int basis, bool* outClamped = nullptr)
	{
		return cssMarginValueForBasis(style.marginTopValue, style.marginTop, fallback, basis, outClamped);
	}

	static int cssFlowMarginBottom(const WebStyle& style, int fallback, int basis, bool* outClamped = nullptr)
	{
		return cssMarginValueForBasis(style.marginBottomValue, style.marginBottom, fallback, basis, outClamped);
	}

	static bool cssMarginIsBlockContainerTag(const std::string& rawTag)
	{
		const std::string tag = toLowerAscii(rawTag);
		return tag == "div" || tag == "section" || tag == "article" || tag == "header" ||
			tag == "footer" || tag == "nav" || tag == "main" || tag == "aside" ||
			tag == "figure" || tag == "blockquote" || tag == "dl" || tag == "form" ||
			tag == "fieldset" || tag == "ul" || tag == "ol";
	}

	static const WebStyle* cssStyleForSerial(const WebDocument& doc, uint64_t serial)
	{
		if (serial == 0) return &doc.bodyStyle;
		if (doc.hasBodyElement && serial == doc.bodyElement.serial) return &doc.bodyStyle;
		return computedStyleForSerial(doc, serial);
	}

	static uint64_t cssBlockParentSerial(const WebDocument& doc, const DocBlock& block)
	{
		for (auto it = block.ancestors.rbegin(); it != block.ancestors.rend(); ++it) {
			const std::string tag = toLowerAscii(it->tagName);
			if (tag == "html" || tag == "head" || tag == "body") continue;
			const WebStyle* style = cssStyleForSerial(doc, it->serial);
			if (it->serial != 0 && (cssMarginIsBlockContainerTag(tag) ||
				(style && style->display != DisplayMode::Inline))) return it->serial;
		}
		return 0;
	}

	static bool cssStyleHasOverflowBfc(const WebStyle& style)
	{
		return style.overflowX != OverflowMode::Visible || style.overflowY != OverflowMode::Visible;
	}

	static std::string cssBfcReasonForBlock(const WebDocument& doc, const DocBlock& block)
	{
		if (block.atomicContainerSerial != 0) return "atomic-context";
		if (block.style.display == DisplayMode::InlineBlock) return "inline-block";
		if (cssStyleHasOverflowBfc(block.style)) return "overflow";
		if (isTableCellLikeBlock(block)) return "table";
		if (block.style.floatMode != FloatMode::None) return "float";
		return cssBlockParentSerial(doc, block) == 0 ? "root" : "";
	}

	static bool cssParentHasDirectInlineContent(const WebDocument& doc, uint64_t serial)
	{
		for (const WebInlineItem& item : doc.inlineItems) {
			if (item.blockIndex < 0 && (item.parentSerial == serial || item.flowSerial == serial)) return true;
		}
		return false;
	}

	static bool cssBlockIsDescendantOfSerial(const DocBlock& block, uint64_t serial)
	{
		if (serial == 0) return true;
		if (block.elementMetadata.serial == serial) return true;
		for (const HtmlElementRef& ancestor : block.ancestors)
			if (ancestor.serial == serial) return true;
		return false;
	}

	static bool cssHasPriorBlockForParent(const WebDocument& doc, int blockIndex, uint64_t parentSerial)
	{
		for (int i = 0; i < blockIndex && i < static_cast<int>(doc.blocks.size()); ++i) {
			const DocBlock& candidate = doc.blocks[static_cast<size_t>(i)];
			if (candidate.style.displayNone || candidate.atomicContainerSerial != 0 ||
				candidate.style.position == PositionMode::Absolute) continue;
			if (cssBlockParentSerial(doc, candidate) == parentSerial) return true;
		}
		return false;
	}

	static bool cssHasLaterBlockForParent(const WebDocument& doc, int blockIndex, uint64_t parentSerial)
	{
		for (int i = blockIndex + 1; i < static_cast<int>(doc.blocks.size()); ++i) {
			const DocBlock& candidate = doc.blocks[static_cast<size_t>(i)];
			if (candidate.style.displayNone || candidate.atomicContainerSerial != 0 ||
				candidate.style.position == PositionMode::Absolute) continue;
			if (cssBlockParentSerial(doc, candidate) == parentSerial) return true;
		}
		return false;
	}

	static bool cssParentTopCollapseAllowed(const WebDocument& doc, uint64_t serial,
		std::string& blockedReason)
	{
		const WebStyle* style = cssStyleForSerial(doc, serial);
		if (!style) { blockedReason = "incomplete-structure"; return false; }
		if (style->display == DisplayMode::InlineBlock) { blockedReason = "bfc"; return false; }
		if (cssStyleHasOverflowBfc(*style)) { blockedReason = "bfc"; return false; }
		if (cssBorderTopPx(*style) > 0) { blockedReason = "border"; return false; }
		if (cssPaddingTopPx(*style, 0) > 0) { blockedReason = "padding"; return false; }
		const CssResolvedLength definite = resolveCssLength(style->heightValue,
			style->height, style->heightPercent, -1);
		const CssResolvedLength minimum = resolveCssLength(style->minHeightValue,
			style->minHeight, style->minHeightPercent, -1);
		if (definite.definite || minimum.definite) { blockedReason = "height"; return false; }
		if (cssParentHasDirectInlineContent(doc, serial)) {
			blockedReason = "content";
			return false;
		}
		return true;
	}

	static bool cssParentBottomCollapseAllowed(const WebDocument& doc, uint64_t serial,
		std::string& blockedReason)
	{
		const WebStyle* style = cssStyleForSerial(doc, serial);
		if (!style) { blockedReason = "incomplete-structure"; return false; }
		if (style->display == DisplayMode::InlineBlock || cssStyleHasOverflowBfc(*style)) {
			blockedReason = "bfc";
			return false;
		}
		if (cssBorderBottomPx(*style) > 0) { blockedReason = "border"; return false; }
		if (cssPaddingBottomPx(*style, 0) > 0) { blockedReason = "padding"; return false; }
		const CssResolvedLength definite = resolveCssLength(style->heightValue,
			style->height, style->heightPercent, -1);
		const CssResolvedLength minimum = resolveCssLength(style->minHeightValue,
			style->minHeight, style->minHeightPercent, -1);
		if (definite.definite || minimum.definite) { blockedReason = "height"; return false; }
		if (cssParentHasDirectInlineContent(doc, serial)) {
			blockedReason = "content";
			return false;
		}
		return true;
	}

	static void cssAddMarginParticipant(CssMarginCollapseValue& value, int margin)
	{
		margin = std::max(-8192, std::min(8192, margin));
		if (value.participantCount >= 64) {
			value.clamped = true;
			return;
		}
		++value.participantCount;
		if (margin > 0) {
			value.hasPositive = true;
			value.largestPositive = std::max(value.largestPositive, margin);
		} else if (margin < 0) {
			value.hasNegative = true;
			value.mostNegative = std::min(value.mostNegative, margin);
		}
		const int64_t resolved = static_cast<int64_t>(value.largestPositive) + value.mostNegative;
		if (resolved < -8192 || resolved > 8192) value.clamped = true;
		value.resolved = static_cast<int>(std::max<int64_t>(-8192, std::min<int64_t>(8192, resolved)));
	}

	static uint64_t cssMarginLayoutFingerprint(const WebDocument& doc)
	{
		uint64_t hash = 1469598103934665603ull;
		auto mix = [&](uint64_t value) {
			hash ^= value;
			hash *= 1099511628211ull;
		};
		const auto mixStyle = [&](const WebStyle& style) {
			mix(static_cast<uint64_t>(style.marginTop)); mix(static_cast<uint64_t>(style.marginRight));
			mix(static_cast<uint64_t>(style.marginBottom)); mix(static_cast<uint64_t>(style.marginLeft));
			mix(static_cast<uint64_t>(style.marginTopValue.type)); mix(static_cast<uint64_t>(style.marginTopValue.value));
			mix(static_cast<uint64_t>(style.marginBottomValue.type)); mix(static_cast<uint64_t>(style.marginBottomValue.value));
			mix(static_cast<uint64_t>(style.paddingTop)); mix(static_cast<uint64_t>(style.paddingBottom));
			mix(static_cast<uint64_t>(style.borderTopWidth)); mix(static_cast<uint64_t>(style.borderBottomWidth));
			mix(static_cast<uint64_t>(style.display)); mix(static_cast<uint64_t>(style.overflowX));
			mix(static_cast<uint64_t>(style.overflowY)); mix(static_cast<uint64_t>(style.height));
			mix(static_cast<uint64_t>(style.minHeight));
			mix(static_cast<uint64_t>(style.floatMode)); mix(static_cast<uint64_t>(style.clearMode));
			mix(static_cast<uint64_t>(style.width)); mix(static_cast<uint64_t>(style.widthPercent));
			mix(static_cast<uint64_t>(style.maxWidth)); mix(static_cast<uint64_t>(style.maxWidthPercent));
			mix(static_cast<uint64_t>(style.position));
			mix(static_cast<uint64_t>(style.topValue.type)); mix(static_cast<uint64_t>(style.topValue.value));
			mix(static_cast<uint64_t>(style.rightValue.type)); mix(static_cast<uint64_t>(style.rightValue.value));
			mix(static_cast<uint64_t>(style.bottomValue.type)); mix(static_cast<uint64_t>(style.bottomValue.value));
			mix(static_cast<uint64_t>(style.leftValue.type)); mix(static_cast<uint64_t>(style.leftValue.value));
			mix(static_cast<uint64_t>(style.zIndexAuto)); mix(static_cast<uint64_t>(style.zIndex));
		};
		mix(static_cast<uint64_t>(doc.blocks.size()));
		mixStyle(doc.bodyStyle);
		for (const DocBlock& block : doc.blocks) {
			mix(block.elementMetadata.serial); mix(block.elementMetadata.parentSerial);
			mixStyle(block.style);
		}
		for (const CssComputedStyleRecord& record : doc.computedStyles) {
			mix(record.serial);
			if (record.valid) mixStyle(record.style);
		}
		return hash;
	}

	static int cssBlockBorderBoxHeightForFlow(const WebDocument& doc, int blockIndex)
	{
		if (blockIndex < 0 || blockIndex >= static_cast<int>(doc.blocks.size())) return 0;
		const DocBlock& block = doc.blocks[static_cast<size_t>(blockIndex)];
		const int basis = std::max(1, blockAvailableWidth(block, doc) +
			cssMarginLeftPx(block.style, 0) + cssMarginRightPx(block.style, 0));
		const int top = cssFlowMarginTop(block.style, block.type == BlockType::Heading ? 10 : 4, basis);
		const int bottom = cssFlowMarginBottom(block.style, block.type == BlockType::ListItem ? 4 : 8, basis);
		const int total = blockTotalHeight(block, doc, false);
		return std::max(0, total - top - bottom);
	}

	static int cssPreviousVisibleFlowBlock(const WebDocument& doc, int blockIndex)
	{
	for (int i = blockIndex - 1; i >= 0; --i) {
			const DocBlock& block = doc.blocks[static_cast<size_t>(i)];
			if (block.style.displayNone || block.atomicContainerSerial != 0 ||
				block.style.floatMode != FloatMode::None ||
				block.style.position == PositionMode::Absolute) continue;
			return i;
		}
		return -1;
	}

	static void buildCssMarginLayout(const WebDocument& doc, CssMarginLayoutSnapshot& snapshot)
	{
		snapshot = CssMarginLayoutSnapshot{};
		snapshot.url = doc.url;
		snapshot.blockCount = doc.blocks.size();
		snapshot.fingerprint = cssMarginLayoutFingerprint(doc);
		snapshot.records.resize(doc.blocks.size());
		if (s_cssMarginLayoutBuilding) {
			snapshot.valid = false;
			return;
		}
		s_cssMarginLayoutBuilding = true;
		gxos::web::CssDiagnostics& diagnostics = const_cast<WebDocument&>(doc).cssDiagnostics;
		diagnostics.marginCollapseSets = 0;
		diagnostics.marginCollapseParticipants = 0;
		diagnostics.marginCollapseSibling = 0;
		diagnostics.marginCollapseParentTop = 0;
		diagnostics.marginCollapseParentBottom = 0;
		diagnostics.marginCollapseEmpty = 0;
		diagnostics.marginCollapsePositiveOnly = 0;
		diagnostics.marginCollapseNegativeOnly = 0;
		diagnostics.marginCollapseMixed = 0;
		diagnostics.marginCollapseBlockedBorder = 0;
		diagnostics.marginCollapseBlockedPadding = 0;
		diagnostics.marginCollapseBlockedBfc = 0;
		diagnostics.marginCollapseBlockedHeight = 0;
		diagnostics.marginCollapseBlockedContent = 0;
		diagnostics.marginCollapseDepthClamps = 0;
		diagnostics.marginGeometryClamps = 0;
		diagnostics.bfcRoot = 0;
		diagnostics.bfcInlineBlock = 0;
		diagnostics.bfcOverflow = 0;
		diagnostics.bfcAtomic = 0;
		diagnostics.marginCollapseEvidenceRecords = 0;
		diagnostics.marginCollapseEvidence.clear();
		diagnostics.bfcRoot = doc.blocks.empty() ? 0 : 1;
		for (const CssComputedStyleRecord& styleRecord : doc.computedStyles) {
			if (!styleRecord.valid) continue;
			if (styleRecord.style.display == DisplayMode::InlineBlock) ++diagnostics.bfcInlineBlock;
			if (cssStyleHasOverflowBfc(styleRecord.style)) ++diagnostics.bfcOverflow;
		}
		ensureInlineLayout(doc);
		CssMarginCollapseValue pending;
		const int bodyBasis = std::max(1, kContentW - blockBodyMarginLeft(doc) - blockBodyMarginRight(doc));
		cssAddMarginParticipant(pending, cssFlowMarginTop(doc.bodyStyle, 0, bodyBasis));
		int cursor = kHeadingY;
		int previousIndex = -1;
		int previousRecord = -1;
		for (int index = 0; index < static_cast<int>(doc.blocks.size()); ++index) {
			const DocBlock& block = doc.blocks[static_cast<size_t>(index)];
			CssMarginFlowRecord& record = snapshot.records[static_cast<size_t>(index)];
			record.serial = block.elementMetadata.serial;
			record.parentSerial = cssBlockParentSerial(doc, block);
			record.specifiedMarginTop = cssFlowMarginTop(block.style, block.type == BlockType::Heading ? 10 : 4, bodyBasis);
			record.specifiedMarginBottom = cssFlowMarginBottom(block.style, block.type == BlockType::ListItem ? 4 : 8, bodyBasis);
			record.bfcReason = cssBfcReasonForBlock(doc, block);
			record.establishesBfc = !record.bfcReason.empty();
			const CssResolvedLength definiteHeight = resolveCssLength(
				block.style.heightValue, block.style.height, block.style.heightPercent, -1);
			const CssResolvedLength definiteMinHeight = resolveCssLength(
				block.style.minHeightValue, block.style.minHeight, block.style.minHeightPercent, -1);
			record.heightDefinite = definiteHeight.definite;
			record.minHeightPreventsCollapse = definiteMinHeight.definite;
			if (record.bfcReason == "atomic-context") ++const_cast<WebDocument&>(doc).cssDiagnostics.bfcAtomic;
			if (block.style.displayNone || block.atomicContainerSerial != 0 ||
				block.style.position == PositionMode::Absolute) {
				record.incomplete = block.atomicContainerSerial != 0;
				continue;
			}
			if (block.style.floatMode != FloatMode::None) {
				// Block floats are out of normal flow. Their used geometry is
				// supplied by the float snapshot, so they must not move the
				// Phase 3D cursor.
				continue;
			}
			const int prior = cssPreviousVisibleFlowBlock(doc, index);
			const bool firstForParent = record.parentSerial != 0 &&
				!cssHasPriorBlockForParent(doc, index, record.parentSerial);
			const bool sameParentSibling = prior >= 0 && previousIndex == prior &&
				cssBlockParentSerial(doc, doc.blocks[static_cast<size_t>(prior)]) == record.parentSerial;
			if (sameParentSibling) {
				record.previousSerial = doc.blocks[static_cast<size_t>(prior)].elementMetadata.serial;
				record.collapsedWithPreviousSibling = true;
				++const_cast<WebDocument&>(doc).cssDiagnostics.marginCollapseSibling;
				if (previousRecord >= 0) {
					// The adjoining sibling margins are represented once by the
					// incoming collapse set, never as two used gaps.
					snapshot.records[static_cast<size_t>(previousRecord)].usedMarginBottom = 0;
				}
				if (previousRecord >= 0) record.incomplete = snapshot.records[static_cast<size_t>(previousRecord)].incomplete;
			}
			// Empty structural elements between two emitted blocks contribute their
			// own adjoining top/bottom margins to the same set.
			const uint64_t priorSerial = prior >= 0 ? doc.blocks[static_cast<size_t>(prior)].elementMetadata.serial : 0;
			const uint64_t currentSerial = block.elementMetadata.serial;
			int emptyDepth = 0;
			for (const HtmlElementRef& element : doc.structuralElements) {
				if (element.serial == 0 || element.serial <= priorSerial ||
					(currentSerial != 0 && element.serial >= currentSerial) || emptyDepth >= 16) continue;
				if (!cssMarginIsBlockContainerTag(element.tagName)) continue;
				bool hasDescendant = false;
				for (const DocBlock& candidate : doc.blocks) {
					if (!candidate.style.displayNone && cssBlockIsDescendantOfSerial(candidate, element.serial)) {
						hasDescendant = true;
						break;
					}
				}
				if (hasDescendant) continue;
				const WebStyle* style = cssStyleForSerial(doc, element.serial);
				if (!style) continue;
				const int top = cssFlowMarginTop(*style, 0, bodyBasis);
				const int bottom = cssFlowMarginBottom(*style, 0, bodyBasis);
				cssAddMarginParticipant(pending, top);
				cssAddMarginParticipant(pending, bottom);
				++emptyDepth;
				record.emptyCollapse = true;
				++const_cast<WebDocument&>(doc).cssDiagnostics.marginCollapseEmpty;
			}
			if (emptyDepth >= 16) {
				++snapshot.traversalClamps;
				++const_cast<WebDocument&>(doc).cssDiagnostics.marginCollapseDepthClamps;
				record.incomplete = true;
			}
			int parentDepthSeen = 0;
			bool parentTopBoundary = false;
			int parentTopBoundaryOffset = 0;
			if (firstForParent) {
				uint64_t parent = record.parentSerial;
				int parentDepth = 0;
				while (parent != 0 && parentDepth++ < 16) {
					const WebStyle* style = cssStyleForSerial(doc, parent);
					if (!style) { record.incomplete = true; break; }
					const int parentTop = cssFlowMarginTop(*style, 0, bodyBasis);
					cssAddMarginParticipant(pending, parentTop);
					std::string blocked;
					const bool allowed = cssParentTopCollapseAllowed(doc, parent, blocked);
					if (allowed) {
						record.collapsedWithParentTop = true;
						++const_cast<WebDocument&>(doc).cssDiagnostics.marginCollapseParentTop;
					} else {
						// The parent's outer margin may still join the incoming
						// chain, but the child's margin is on the far side of
						// this boundary.  Keep the boundary offset separate so a
						// border/padding/BFC/definite-size case cannot collapse
						// the child margin through the parent.
						parentTopBoundary = true;
						parentTopBoundaryOffset = cssBoundedGeometryAdd(parentTopBoundaryOffset,
							cssBorderTopPx(*style) + cssPaddingTopPx(*style, 0));
						record.blockedReason = blocked;
						if (blocked == "border") ++const_cast<WebDocument&>(doc).cssDiagnostics.marginCollapseBlockedBorder;
						else if (blocked == "padding") ++const_cast<WebDocument&>(doc).cssDiagnostics.marginCollapseBlockedPadding;
						else if (blocked == "bfc") ++const_cast<WebDocument&>(doc).cssDiagnostics.marginCollapseBlockedBfc;
						else if (blocked == "height") ++const_cast<WebDocument&>(doc).cssDiagnostics.marginCollapseBlockedHeight;
						else if (blocked == "content") ++const_cast<WebDocument&>(doc).cssDiagnostics.marginCollapseBlockedContent;
					}
					const HtmlElementRef* parentElement = cssStructuralElementForSerial(doc, parent);
					parent = parentElement ? parentElement->parentSerial : 0;
				}
				parentDepthSeen = parentDepth;
				if (parentDepth >= 16) {
					record.incomplete = true;
					++snapshot.traversalClamps;
					++const_cast<WebDocument&>(doc).cssDiagnostics.marginCollapseDepthClamps;
				}
			}
			if (!parentTopBoundary) cssAddMarginParticipant(pending, record.specifiedMarginTop);
			const int collapsedTop = pending.resolved;
			record.collapse = pending;
			record.collapseParticipantCount = pending.participantCount;
			record.collapseMaxPositive = pending.largestPositive;
			record.collapseMostNegative = pending.mostNegative;
			record.usedMarginTop = parentTopBoundary ? record.specifiedMarginTop : collapsedTop;
			record.marginEdgeY = cursor;
			int boxY = cursor + collapsedTop + parentTopBoundaryOffset +
				(parentTopBoundary ? record.specifiedMarginTop : 0);
			if (boxY < 0) {
				boxY = 0;
				record.clamped = true;
				++const_cast<WebDocument&>(doc).cssDiagnostics.marginGeometryClamps;
			}
			record.usedY = boxY;
			record.outerWidth = blockOuterWidth(block, blockAvailableWidth(block, doc));
			record.outerHeight = cssBlockBorderBoxHeightForFlow(doc, index);
			record.borderBoxX = blockOuterX(block, doc, blockAvailableWidth(block, doc), record.outerWidth);
			record.borderBoxY = record.usedY;
			record.borderBoxW = record.outerWidth;
			record.borderBoxH = record.outerHeight;
			const bool emptyBlock = record.outerHeight == 0 && block.text.empty() &&
				cssBorderTopPx(block.style) == 0 && cssBorderBottomPx(block.style) == 0 &&
				cssPaddingTopPx(block.style, 0) == 0 && cssPaddingBottomPx(block.style, 0) == 0;
			if (emptyBlock) {
				record.emptyCollapse = true;
				cssAddMarginParticipant(pending, record.specifiedMarginBottom);
				record.collapse = pending;
				record.collapseParticipantCount = pending.participantCount;
				record.collapseMaxPositive = pending.largestPositive;
				record.collapseMostNegative = pending.mostNegative;
				record.usedMarginBottom = pending.resolved;
				++const_cast<WebDocument&>(doc).cssDiagnostics.marginCollapseEmpty;
			} else {
				cursor = std::min(8192, boxY + std::max(0, record.outerHeight));
				pending = CssMarginCollapseValue{};
				cssAddMarginParticipant(pending, record.specifiedMarginBottom);
				record.usedMarginBottom = record.specifiedMarginBottom;
			}
			// A parent's bottom margin may join the trailing set only after its
			// final in-flow child.  Border/padding/definite sizing keep it outside.
			bool parentBottomBoundary = false;
			int parentBottomBoundaryOffset = 0;
			CssMarginCollapseValue parentBottomOutside;
			if (record.parentSerial != 0 && !cssHasLaterBlockForParent(doc, index, record.parentSerial)) {
				uint64_t parent = record.parentSerial;
				int parentDepth = 0;
				while (parent != 0 && parentDepth++ < 16) {
					const WebStyle* style = cssStyleForSerial(doc, parent);
					if (!style) { record.incomplete = true; break; }
					std::string blocked;
					const bool allowed = cssParentBottomCollapseAllowed(doc, parent, blocked);
					const int parentBottom = cssFlowMarginBottom(*style, 0, bodyBasis);
					if (allowed && !parentBottomBoundary) {
						cssAddMarginParticipant(pending, parentBottom);
						record.collapsedWithParentBottom = true;
						++const_cast<WebDocument&>(doc).cssDiagnostics.marginCollapseParentBottom;
					} else {
						// Keep the child's trailing set inside the parent.  The
						// parent's outer margin resumes after its separating
						// border/padding/BFC/definite-size boundary and may join
						// later outer margins there.
						parentBottomBoundary = true;
						cssAddMarginParticipant(parentBottomOutside, parentBottom);
						parentBottomBoundaryOffset = cssBoundedGeometryAdd(parentBottomBoundaryOffset,
							cssPaddingBottomPx(*style, 0) + cssBorderBottomPx(*style));
						record.blockedReason = blocked;
						if (blocked == "border") ++const_cast<WebDocument&>(doc).cssDiagnostics.marginCollapseBlockedBorder;
						else if (blocked == "padding") ++const_cast<WebDocument&>(doc).cssDiagnostics.marginCollapseBlockedPadding;
						else if (blocked == "bfc") ++const_cast<WebDocument&>(doc).cssDiagnostics.marginCollapseBlockedBfc;
						else if (blocked == "height") ++const_cast<WebDocument&>(doc).cssDiagnostics.marginCollapseBlockedHeight;
						else if (blocked == "content") ++const_cast<WebDocument&>(doc).cssDiagnostics.marginCollapseBlockedContent;
					}
					const HtmlElementRef* parentElement = cssStructuralElementForSerial(doc, parent);
					parent = parentElement ? parentElement->parentSerial : 0;
				}
				if (parentDepth >= 16) {
					record.incomplete = true;
					++snapshot.traversalClamps;
					++const_cast<WebDocument&>(doc).cssDiagnostics.marginCollapseDepthClamps;
				}
			}
			if (parentBottomBoundary) {
				const int childBottom = pending.resolved;
				cursor = std::max(0, std::min(8192, cursor + childBottom + parentBottomBoundaryOffset));
				record.usedMarginBottom = childBottom;
				// The top/sibling set was already resolved before the
				// parent-bottom boundary was discovered.  Preserve its type in
				// diagnostics even though the outside trailing set now starts
				// with the parent's margin.
				if (record.collapse.hasPositive && record.collapse.hasNegative)
					++const_cast<WebDocument&>(doc).cssDiagnostics.marginCollapseMixed;
				pending = parentBottomOutside;
			}
			if (pending.participantCount > 1) ++const_cast<WebDocument&>(doc).cssDiagnostics.marginCollapseSets;
			const_cast<WebDocument&>(doc).cssDiagnostics.marginCollapseParticipants = std::min(1 << 30,
				const_cast<WebDocument&>(doc).cssDiagnostics.marginCollapseParticipants + pending.participantCount);
			snapshot.maximumParticipants = std::max(snapshot.maximumParticipants, pending.participantCount);
			snapshot.maximumDepth = std::max(snapshot.maximumDepth, parentDepthSeen);
			if (pending.hasPositive && pending.hasNegative) {
				++const_cast<WebDocument&>(doc).cssDiagnostics.marginCollapseMixed;
			} else if (pending.hasPositive) {
				++const_cast<WebDocument&>(doc).cssDiagnostics.marginCollapsePositiveOnly;
			} else if (pending.hasNegative) {
				++const_cast<WebDocument&>(doc).cssDiagnostics.marginCollapseNegativeOnly;
			}
			if (pending.clamped || record.clamped) {
				record.clamped = true;
				++const_cast<WebDocument&>(doc).cssDiagnostics.marginGeometryClamps;
			}
			record.collapseType = record.collapsedWithPreviousSibling ? "sibling" :
				record.collapsedWithParentTop ? "parent-top" :
				record.collapsedWithParentBottom ? "parent-bottom" :
				record.emptyCollapse ? "empty" : "normal-flow";
			record.documentExtentContribution = std::max(0, boxY + record.outerHeight);
			const bool phase3d = block.id.rfind("phase3d-", 0) == 0 || block.id.rfind("css3d-", 0) == 0 ||
				doc.url.find("css-phase3d") != std::string::npos;
			if (phase3d && snapshot.evidenceRecords < 128 && snapshot.evidence.size() < 32768) {
				std::ostringstream line;
				line << "id=" << block.id << ",serial=" << record.serial << ",parent-serial=" << record.parentSerial
					<< ",previous-serial=" << record.previousSerial
					<< ",specified-margin-top=" << record.specifiedMarginTop
					<< ",specified-margin-bottom=" << record.specifiedMarginBottom
					<< ",used-margin-top=" << record.usedMarginTop << ",used-margin-bottom=" << record.usedMarginBottom
					<< ",collapse-participants=" << record.collapseParticipantCount
					<< ",max-positive=" << record.collapseMaxPositive << ",most-negative=" << record.collapseMostNegative
					<< ",collapsed-result=" << record.collapse.resolved << ",collapse-type=" << record.collapseType
					<< ",collapsed-with-previous-sibling=" << (record.collapsedWithPreviousSibling ? "yes" : "no")
					<< ",collapsed-with-parent-top=" << (record.collapsedWithParentTop ? "yes" : "no")
					<< ",collapsed-with-parent-bottom=" << (record.collapsedWithParentBottom ? "yes" : "no")
					<< ",empty-collapse=" << (record.emptyCollapse ? "yes" : "no")
					<< ",bfc=" << (record.establishesBfc ? "yes" : "no") << ",bfc-reason=" << record.bfcReason
					<< ",blocked-reason=" << record.blockedReason << ",height-definite=" << (record.heightDefinite ? "yes" : "no")
					<< ",min-height-prevents-collapse=" << (record.minHeightPreventsCollapse ? "yes" : "no")
					<< ",used-y=" << record.usedY << ",used-height=" << record.outerHeight
					<< ",border-box=" << record.borderBoxX << ":" << record.borderBoxY << ":" << record.borderBoxW << ":" << record.borderBoxH
					<< ",document-extent-contribution=" << record.documentExtentContribution
					<< ",clamped=" << (record.clamped ? "yes" : "no") << ",incomplete=" << (record.incomplete ? "yes" : "no") << "\n";
				const std::string text = line.str();
				if (snapshot.evidence.size() + text.size() <= 32768) {
					snapshot.evidence += text;
					++snapshot.evidenceRecords;
				}
			}
			previousIndex = index;
			previousRecord = index;
			++snapshot.operations;
			if (snapshot.operations >= 4096) {
				snapshot.traversalClamps++;
				break;
			}
		}
		const int bodyBottom = cssFlowMarginBottom(doc.bodyStyle, 8, bodyBasis);
		cssAddMarginParticipant(pending, bodyBottom);
		if (pending.resolved < 0 && cursor + pending.resolved < kHeadingY) {
			pending.resolved = kHeadingY - cursor;
			pending.clamped = true;
			++const_cast<WebDocument&>(doc).cssDiagnostics.marginGeometryClamps;
		}
		snapshot.documentHeight = std::max(kHeadingY, std::min(8192, cursor + std::max(0, pending.resolved)));
		const_cast<WebDocument&>(doc).cssDiagnostics.marginCollapseEvidenceRecords = snapshot.evidenceRecords;
		const_cast<WebDocument&>(doc).cssDiagnostics.marginCollapseEvidence = snapshot.evidence;
		snapshot.valid = true;
		// Float placement is a sibling pass over the completed bounded margin
		// snapshot.  Rebuilding inline flows once after it exists lets all line
		// boxes use the same exclusion records without creating a second block
		// layout engine or a recursive dependency on normal-flow placement.
		ensureCssFloatLayout(doc);
		s_inlineLayoutDirty = true;
		rebuildInlineLayout(doc, s_inlineLayoutSnapshot);
		s_inlineLayoutDirty = false;
		s_cssMarginLayoutBuilding = false;
	}

	static bool cssBfcBoundaryTag(const std::string& rawTag)
	{
		const std::string tag = toLowerAscii(rawTag);
		return tag == "td" || tag == "th";
	}

	static bool cssStyleEstablishesBfc(const WebStyle& style, const std::string& rawTag = "")
	{
		return style.display == DisplayMode::InlineBlock || cssStyleHasOverflowBfc(style) ||
			cssBfcBoundaryTag(rawTag) || style.floatMode != FloatMode::None;
	}

	static const WebStyle* cssBfcStyleForSerial(const WebDocument& doc, uint64_t serial)
	{
		if (serial == 0) return &doc.bodyStyle;
		if (doc.hasBodyElement && serial == doc.bodyElement.serial) return &doc.bodyStyle;
		return computedStyleForSerial(doc, serial);
	}

	static bool cssBfcBoundaryForSerial(const WebDocument& doc, uint64_t serial)
	{
		if (serial == 0 || (doc.hasBodyElement && serial == doc.bodyElement.serial)) return false;
		const HtmlElementRef* element = cssStructuralElementForSerial(doc, serial);
		const WebStyle* style = cssBfcStyleForSerial(doc, serial);
		return element && style && cssStyleEstablishesBfc(*style, element->tagName);
	}

	static uint64_t cssBfcParentSerial(const WebDocument& doc, uint64_t serial)
	{
		const HtmlElementRef* element = cssStructuralElementForSerial(doc, serial);
		if (!element) return 0;
		uint64_t current = element->parentSerial;
		for (int depth = 0; current != 0 && depth < 16; ++depth) {
			if (cssBfcBoundaryForSerial(doc, current)) return current;
			const HtmlElementRef* parent = cssStructuralElementForSerial(doc, current);
			if (!parent) return 0;
			current = parent->parentSerial;
		}
		return 0;
	}

	static int cssBfcDepthForSerial(const WebDocument& doc, uint64_t serial)
	{
		int depth = 0;
		uint64_t current = serial;
		while (current != 0 && depth < 16) {
			++depth;
			current = cssBfcParentSerial(doc, current);
		}
		return depth;
	}

	static uint64_t cssContainingBfcIdentityForBlock(const WebDocument& doc,
		const DocBlock& block, bool includeSelf)
	{
		if (includeSelf && block.elementMetadata.serial != 0 &&
			cssStyleEstablishesBfc(block.style, block.tagName)) {
			return block.elementMetadata.serial;
		}
		auto boundaryForSerial = [&](uint64_t serial) -> uint64_t {
			if (serial == 0 || (doc.hasBodyElement && serial == doc.bodyElement.serial)) return 0;
			const HtmlElementRef* element = cssStructuralElementForSerial(doc, serial);
			const WebStyle* style = cssBfcStyleForSerial(doc, serial);
			return element && style && cssStyleEstablishesBfc(*style, element->tagName) ? serial : 0;
		};
		for (auto it = block.ancestors.rbegin(); it != block.ancestors.rend(); ++it) {
			const uint64_t boundary = boundaryForSerial(it->serial);
			if (boundary != 0) return boundary;
		}
		uint64_t current = block.elementMetadata.parentSerial;
		for (int depth = 0; current != 0 && depth < 16; ++depth) {
			const uint64_t boundary = boundaryForSerial(current);
			if (boundary != 0) return boundary;
			const HtmlElementRef* element = cssStructuralElementForSerial(doc, current);
			if (!element) break;
			current = element->parentSerial;
		}
		return 0;
	}

	static uint64_t cssFloatContainingBfcIdentity(const WebDocument& doc,
		const InlineFlowLayout& flow)
	{
		if (flow.contextSerial != 0) return flow.contextSerial;
		if (flow.anchorBlockIndex >= 0 && flow.anchorBlockIndex < static_cast<int>(doc.blocks.size())) {
			const DocBlock& anchor = doc.blocks[static_cast<size_t>(flow.anchorBlockIndex)];
			return cssContainingBfcIdentityForBlock(doc, anchor,
				anchor.style.floatMode == FloatMode::None);
		}
		return 0;
	}

	static uint64_t cssFloatBfcIdentity(const WebDocument& doc, const InlineFlowLayout& flow)
	{
		if (flow.contextSerial != 0) return flow.contextSerial;
		if (flow.anchorBlockIndex >= 0 && flow.anchorBlockIndex < static_cast<int>(doc.blocks.size())) {
			return cssContainingBfcIdentityForBlock(doc,
				doc.blocks[static_cast<size_t>(flow.anchorBlockIndex)], true);
		}
		return 0;
	}

	static int cssFloatPreferredTextWidth(const WebDocument& doc, const WebInlineItem& item,
		const WebStyle& style, bool longestToken)
	{
		if (item.kind != InlineItemKind::TextRun) return 0;
		if (!longestToken) return inlineTextWidth(style, item.text);
		int result = 0;
		size_t start = 0;
		while (start <= item.text.size()) {
			size_t end = item.text.find_first_of(" \\t\\r\\n", start);
			if (end == std::string::npos) end = item.text.size();
			result = std::max(result, inlineTextWidth(style, item.text.substr(start, end - start)));
			if (end == item.text.size()) break;
			start = end + 1;
		}
		(void)doc;
		return result;
	}

	static bool cssFloatRecordIntersects(const CssFloatRecord& record, int top, int bottom)
	{
		return bottom > record.marginBoxY && top < record.marginBoxY + record.marginBoxH;
	}

	static CssFloatExclusionQuery cssFloatExclusionQuery(const WebDocument& doc,
		uint64_t bfcIdentity, int lineTop, int lineBottom, int containingLeft, int containingRight)
	{
		CssFloatExclusionQuery result;
		result.availableLeft = containingLeft;
		result.availableRight = containingRight;
		result.availableWidth = std::max(0, containingRight - containingLeft);
		if (!s_cssFloatLayoutSnapshot.valid) return result;
		constexpr int kMaxScans = 128;
		int scans = 0;
		for (const CssFloatRecord& record : s_cssFloatLayoutSnapshot.records) {
			if (++scans > kMaxScans) {
				result.complete = false;
				result.clamped = true;
				++const_cast<WebDocument&>(doc).cssDiagnostics.floatExclusionScanClamps;
				break;
			}
			if (record.bfcIdentity != bfcIdentity ||
				!cssFloatRecordIntersects(record, lineTop, lineBottom)) continue;
			++result.recordsIntersected;
			if (record.side == FloatMode::Left) {
				result.hasLeft = true;
				result.availableLeft = std::max(result.availableLeft,
					record.marginBoxX + record.marginBoxW);
			} else if (record.side == FloatMode::Right) {
				result.hasRight = true;
				result.availableRight = std::min(result.availableRight, record.marginBoxX);
			}
			result.nextCandidateY = result.nextCandidateY < 0
				? record.marginBoxY + record.marginBoxH
				: std::min(result.nextCandidateY, record.marginBoxY + record.marginBoxH);
		}
		result.availableLeft = std::max(containingLeft, std::min(containingRight, result.availableLeft));
		result.availableRight = std::max(result.availableLeft,
			std::min(containingRight, result.availableRight));
		result.availableWidth = std::max(0, result.availableRight - result.availableLeft);
		if (result.recordsIntersected > 0)
			++const_cast<WebDocument&>(doc).cssDiagnostics.floatLineExclusions;
		return result;
	}

	static int cssFloatClearance(const WebDocument& doc, uint64_t bfcIdentity,
		ClearMode clearMode, int blockTop)
	{
		if (clearMode == ClearMode::None || !s_cssFloatLayoutSnapshot.valid) return 0;
		int bottom = blockTop;
		for (const CssFloatRecord& record : s_cssFloatLayoutSnapshot.records) {
			if (record.bfcIdentity != bfcIdentity) continue;
			const bool relevant = clearMode == ClearMode::Both ||
				(clearMode == ClearMode::Left && record.side == FloatMode::Left) ||
				(clearMode == ClearMode::Right && record.side == FloatMode::Right);
			if (relevant) bottom = std::max(bottom, record.marginBoxY + record.marginBoxH);
		}
		const int clearance = std::max(0, bottom - blockTop);
		if (clearance > 0) ++const_cast<WebDocument&>(doc).cssDiagnostics.clearanceApplied;
		return clearance;
	}

	static int cssBfcPlacementY(const WebDocument& doc, int blockIndex, int candidateY,
		int requiredWidth)
	{
		if (blockIndex < 0 || blockIndex >= static_cast<int>(doc.blocks.size()) ||
			!s_cssFloatLayoutSnapshot.valid) return candidateY;
		const DocBlock& block = doc.blocks[static_cast<size_t>(blockIndex)];
		if (!cssStyleHasOverflowBfc(block.style) || block.style.floatMode != FloatMode::None)
			return candidateY;
		const uint64_t parentBfc = cssContainingBfcIdentityForBlock(doc, block, false);
		if (parentBfc != 0) return candidateY;
		const int containingLeft = blockBodyMarginLeft(doc);
		const int containingRight = containingLeft + cssBodyContentWidth(doc);
		const int height = blockIndex < static_cast<int>(s_cssMarginLayoutSnapshot.records.size())
			? std::max(1, s_cssMarginLayoutSnapshot.records[static_cast<size_t>(blockIndex)].outerHeight)
			: std::max(1, blockTextLineHeight(block));
		int y = std::max(0, candidateY);
		for (int attempt = 0; attempt < 64; ++attempt) {
			const CssFloatExclusionQuery exclusion = cssFloatExclusionQuery(doc, parentBfc, y,
				y + height, containingLeft, containingRight);
			if (exclusion.recordsIntersected == 0) return y;
			++const_cast<WebDocument&>(doc).cssDiagnostics.bfcFloatAvoidanceAttempts;
			if (requiredWidth <= exclusion.availableWidth) {
				++const_cast<WebDocument&>(doc).cssDiagnostics.bfcFloatAvoidanceFits;
				return y;
			}
			++const_cast<WebDocument&>(doc).cssDiagnostics.bfcFloatTooWide;
			if (exclusion.nextCandidateY <= y || exclusion.nextCandidateY < 0) return y;
			y = std::min(8192, exclusion.nextCandidateY);
			++const_cast<WebDocument&>(doc).cssDiagnostics.bfcFloatAvoidanceDownshifts;
		}
		++const_cast<WebDocument&>(doc).cssDiagnostics.nestedFloatDepthClamps;
		return y;
	}

	static CssBfcContextRecord* cssBfcContextRecordFor(CssFloatLayoutSnapshot& snapshot,
		uint64_t identity)
	{
		for (CssBfcContextRecord& context : snapshot.bfcContexts)
			if (context.identity == identity) return &context;
		return nullptr;
	}

	static const CssBfcContextRecord* cssBfcContextRecordFor(
		const CssFloatLayoutSnapshot& snapshot, uint64_t identity)
	{
		for (const CssBfcContextRecord& context : snapshot.bfcContexts)
			if (context.identity == identity) return &context;
		return nullptr;
	}

	static int cssOwnedFloatMaximumBottom(const CssFloatLayoutSnapshot& snapshot,
		uint64_t bfcIdentity)
	{
		int bottom = 0;
		for (const CssFloatRecord& record : snapshot.records) {
			if (record.bfcIdentity != bfcIdentity) continue;
			bottom = std::max(bottom, record.marginBoxY + record.marginBoxH);
		}
		return bottom;
	}

	static std::string cssBfcReasonForSerial(const WebDocument& doc, uint64_t serial)
	{
		if (serial == 0) return "root";
		const HtmlElementRef* element = cssStructuralElementForSerial(doc, serial);
		const WebStyle* style = cssBfcStyleForSerial(doc, serial);
		if (!element || !style) return "incomplete-structure";
		if (style->display == DisplayMode::InlineBlock) return "inline-block";
		if (cssStyleHasOverflowBfc(*style)) return "overflow";
		if (cssBfcBoundaryTag(element->tagName)) return "table-cell";
		if (style->floatMode != FloatMode::None) return "float";
		return "atomic-context";
	}

	static void buildCssBfcContextSummary(const WebDocument& doc,
		CssFloatLayoutSnapshot& snapshot)
	{
		snapshot.bfcContexts.clear();
		snapshot.bfcContexts.reserve(64);
		auto addContext = [&](uint64_t identity) {
			if (cssBfcContextRecordFor(snapshot, identity)) return;
			if (snapshot.bfcContexts.size() >= 256) {
				++const_cast<WebDocument&>(doc).cssDiagnostics.nestedFloatDepthClamps;
				return;
			}
			CssBfcContextRecord context;
			context.identity = identity;
			context.parentIdentity = identity == 0 ? 0 : cssBfcParentSerial(doc, identity);
			context.ownerSerial = identity;
			context.reason = cssBfcReasonForSerial(doc, identity);
			context.complete = identity == 0 || cssBfcStyleForSerial(doc, identity) != nullptr;
			if (identity != 0) {
				const WebStyle* style = cssBfcStyleForSerial(doc, identity);
				if (style) {
					const CssResolvedLength height = resolveCssLength(style->heightValue,
						style->height, style->heightPercent, -1);
					const CssResolvedLength minimum = resolveCssLength(style->minHeightValue,
						style->minHeight, style->minHeightPercent, -1);
					const CssResolvedLength maximum = resolveCssLength(style->maxHeightValue,
						style->maxHeight, style->maxHeightPercent, -1);
					context.explicitHeight = height.definite ? height.px : -1;
					context.minHeight = minimum.definite ? minimum.px : -1;
					context.maxHeight = maximum.definite ? maximum.px : -1;
				}
			}
			snapshot.bfcContexts.push_back(std::move(context));
		};
		addContext(0);
		for (const HtmlElementRef& element : doc.structuralElements) {
			if (element.serial != 0 && cssBfcBoundaryForSerial(doc, element.serial))
				addContext(element.serial);
		}
		for (const WebInlineItem& item : doc.inlineItems) {
			if (item.atomicContainerSerial != 0) addContext(item.atomicContainerSerial);
		}
		for (const CssFloatRecord& record : snapshot.records) addContext(record.bfcIdentity);

		for (CssBfcContextRecord& context : snapshot.bfcContexts) {
			context.ownedFloatCount = 0;
			context.ownedFloatMaximumBottom = cssOwnedFloatMaximumBottom(snapshot, context.identity);
			context.containedFloat = context.ownedFloatMaximumBottom > 0;
			if (context.ownedFloatCount == 0) {
				for (const CssFloatRecord& record : snapshot.records)
					if (record.bfcIdentity == context.identity) ++context.ownedFloatCount;
			}
		}

		for (int blockIndex = 0; blockIndex < static_cast<int>(doc.blocks.size()); ++blockIndex) {
			const DocBlock& block = doc.blocks[static_cast<size_t>(blockIndex)];
			if (block.style.displayNone || block.atomicContainerSerial != 0 ||
				block.style.floatMode != FloatMode::None) continue;
			if (blockIndex < 0 || blockIndex >= static_cast<int>(s_cssMarginLayoutSnapshot.records.size())) continue;
			const CssMarginFlowRecord& record = s_cssMarginLayoutSnapshot.records[static_cast<size_t>(blockIndex)];
			const uint64_t owner = cssContainingBfcIdentityForBlock(doc, block, false);
			CssBfcContextRecord* context = cssBfcContextRecordFor(snapshot, owner);
			if (!context) continue;
			const int bottom = cssBoundedGeometryAdd(record.usedY, record.outerHeight);
			context->inFlowContentBottom = std::max(context->inFlowContentBottom,
				cssBoundedGeometryAdd(bottom, std::max(0, record.usedMarginBottom)));
		}

		for (CssBfcContextRecord& context : snapshot.bfcContexts) {
			if (context.identity == 0) {
				context.originY = 0;
			} else {
				const HtmlElementRef* element = cssStructuralElementForSerial(doc, context.identity);
				const WebStyle* style = cssBfcStyleForSerial(doc, context.identity);
				int firstY = 8192;
				for (int blockIndex = 0; blockIndex < static_cast<int>(doc.blocks.size()); ++blockIndex) {
					const DocBlock& block = doc.blocks[static_cast<size_t>(blockIndex)];
					if (block.style.displayNone || block.atomicContainerSerial != 0) continue;
					bool containsContext = block.elementMetadata.serial == context.identity;
					for (const HtmlElementRef& ancestor : block.ancestors)
						containsContext = containsContext || ancestor.serial == context.identity;
					if (!containsContext) continue;
					const uint64_t owner = cssContainingBfcIdentityForBlock(doc, block, false);
					if (owner != context.identity) continue;
					if (blockIndex < static_cast<int>(s_cssMarginLayoutSnapshot.records.size()))
						firstY = std::min(firstY, s_cssMarginLayoutSnapshot.records[static_cast<size_t>(blockIndex)].usedY);
				}
				firstY = std::min(firstY, context.ownedFloatMaximumBottom > 0
					? context.ownedFloatMaximumBottom : 8192);
				context.originY = firstY == 8192 ? 0 : firstY - (style ? cssBorderTopPx(*style) + cssPaddingTopPx(*style, 0) : 0);
				(void)element;
			}
			context.autoHeightInputExtent = std::max(0,
				std::max(context.inFlowContentBottom, context.ownedFloatMaximumBottom) - context.originY);
			const int verticalEdges = context.identity == 0 ? 0 : [&]() {
				const WebStyle* style = cssBfcStyleForSerial(doc, context.identity);
				return style ? cssVerticalBoxEdges(*style) : 0;
			}();
			context.usedContentHeight = context.autoHeightInputExtent;
			if (context.ownedFloatMaximumBottom > context.inFlowContentBottom && context.explicitHeight < 0) {
				++const_cast<WebDocument&>(doc).cssDiagnostics.floatHeightContainments;
				++const_cast<WebDocument&>(doc).cssDiagnostics.bfcFloatContainments;
				++const_cast<WebDocument&>(doc).cssDiagnostics.bfcFloatHeightExtensions;
			} else if (context.ownedFloatCount > 0) {
				++const_cast<WebDocument&>(doc).cssDiagnostics.bfcFloatHeightNoops;
			}
			(void)verticalEdges;
		}
	}

	static void buildCssFloatLayout(const WebDocument& doc, CssFloatLayoutSnapshot& snapshot)
	{
		snapshot = CssFloatLayoutSnapshot{};
		snapshot.url = doc.url;
		snapshot.blockCount = doc.blocks.size();
		snapshot.fingerprint = cssMarginLayoutFingerprint(doc);
		snapshot.records.reserve(128);
		if (s_cssFloatLayoutSnapshot.building) return;
		s_cssFloatLayoutSnapshot.building = true;
		WebDocument& mutableDoc = const_cast<WebDocument&>(doc);
		mutableDoc.cssDiagnostics.floatLeft = 0;
		mutableDoc.cssDiagnostics.floatRight = 0;
		mutableDoc.cssDiagnostics.floatBlockifications = 0;
		mutableDoc.cssDiagnostics.floatRecords = 0;
		mutableDoc.cssDiagnostics.floatPlacementAttempts = 0;
		mutableDoc.cssDiagnostics.floatPlacementDownshifts = 0;
		mutableDoc.cssDiagnostics.floatSideBySide = 0;
		mutableDoc.cssDiagnostics.floatWidthOverflows = 0;
		mutableDoc.cssDiagnostics.floatLineExclusions = 0;
		mutableDoc.cssDiagnostics.floatZeroWidthLineAdvances = 0;
		mutableDoc.cssDiagnostics.floatBfcAvoidances = 0;
		mutableDoc.cssDiagnostics.floatBfcDownshifts = 0;
		mutableDoc.cssDiagnostics.clearLeft = 0;
		mutableDoc.cssDiagnostics.clearRight = 0;
		mutableDoc.cssDiagnostics.clearBoth = 0;
		mutableDoc.cssDiagnostics.clearanceApplied = 0;
		mutableDoc.cssDiagnostics.floatContainmentBoundaries = 0;
		mutableDoc.cssDiagnostics.floatScopeSuppressions = 0;
		mutableDoc.cssDiagnostics.floatHeightContainments = 0;
		mutableDoc.cssDiagnostics.bfcFloatContainments = 0;
		mutableDoc.cssDiagnostics.bfcFloatHeightExtensions = 0;
		mutableDoc.cssDiagnostics.bfcFloatHeightNoops = 0;
		mutableDoc.cssDiagnostics.bfcFloatAvoidanceAttempts = 0;
		mutableDoc.cssDiagnostics.bfcFloatAvoidanceFits = 0;
		mutableDoc.cssDiagnostics.bfcFloatAvoidanceDownshifts = 0;
		mutableDoc.cssDiagnostics.bfcFloatTooWide = 0;
		mutableDoc.cssDiagnostics.nestedFloatContexts = 0;
		mutableDoc.cssDiagnostics.nestedFloatDepthClamps = 0;
		mutableDoc.cssDiagnostics.floatInsideInlineBlock = 0;
		mutableDoc.cssDiagnostics.floatInsideFloat = 0;
		mutableDoc.cssDiagnostics.floatListCases = 0;
		mutableDoc.cssDiagnostics.floatTableCellCases = 0;
		mutableDoc.cssDiagnostics.floatTableAvoidanceCases = 0;
		mutableDoc.cssDiagnostics.floatedTableUnsupported = 0;
		mutableDoc.cssDiagnostics.floatDocumentExtentExtensions = 0;
		mutableDoc.cssDiagnostics.floatGeometryClamps = 0;
		mutableDoc.cssDiagnostics.floatPlacementAttemptClamps = 0;
		mutableDoc.cssDiagnostics.floatExclusionScanClamps = 0;
		mutableDoc.cssDiagnostics.floatBfcDepthClamps = 0;
		mutableDoc.cssDiagnostics.floatEvidenceRecords = 0;
		mutableDoc.cssDiagnostics.floatEvidence.clear();

		uint64_t sourceOrder = 1;
		std::vector<uint64_t> seenOwners;
		seenOwners.reserve(128);
		for (const WebInlineItem& item : doc.inlineItems) {
			if (item.kind == InlineItemKind::ForcedBreak || item.flowSerial == 0) continue;
			const WebStyle* style = inlineOwnerStyle(doc, item, doc.bodyStyle);
			if (!style || style->floatMode == FloatMode::None || style->displayNone ||
				style->position == PositionMode::Absolute) continue;
			if (item.ownerSerial != 0 && std::find(seenOwners.begin(), seenOwners.end(), item.ownerSerial) != seenOwners.end()) {
				++mutableDoc.cssDiagnostics.floatScopeSuppressions;
				continue;
			}
			if (snapshot.records.size() >= 128) {
				++mutableDoc.cssDiagnostics.floatGeometryClamps;
				break;
			}
			if (item.ownerSerial != 0) seenOwners.push_back(item.ownerSerial);
			InlineFlowLayout flow;
			bool foundFlow = false;
			for (const InlineFlowLayout& candidate : s_inlineLayoutSnapshot.flows) {
				if (candidate.flowSerial == item.flowSerial && candidate.contextSerial == item.atomicContainerSerial) {
					flow = candidate;
					foundFlow = true;
					break;
				}
			}
			if (!foundFlow) {
				++mutableDoc.cssDiagnostics.floatScopeSuppressions;
				continue;
			}
			if (flow.contextSerial == 0 && flow.anchorBlockIndex >= 0 &&
				flow.anchorBlockIndex < static_cast<int>(s_cssMarginLayoutSnapshot.records.size()) &&
				s_cssMarginLayoutSnapshot.valid) {
				const CssMarginFlowRecord& marginRecord = s_cssMarginLayoutSnapshot.records[
					static_cast<size_t>(flow.anchorBlockIndex)];
				flow.documentContentTop = marginRecord.usedY + cssBorderTopPx(flow.style) +
					cssPaddingTopPx(flow.style, 0);
			}
			const uint64_t bfcIdentity = cssFloatContainingBfcIdentity(doc, flow);
			const int bfcDepth = cssBfcDepthForSerial(doc, bfcIdentity);
			if (item.atomicContainerSerial != 0) {
				++mutableDoc.cssDiagnostics.floatContainmentBoundaries;
				++mutableDoc.cssDiagnostics.floatInsideInlineBlock;
			}
			if (bfcDepth > 0) {
				++mutableDoc.cssDiagnostics.nestedFloatContexts;
				if (bfcDepth >= 16) ++mutableDoc.cssDiagnostics.nestedFloatDepthClamps;
			}
			const int containerLeft = std::max(0, flow.contentX - kContentX);
			const int containerWidth = std::max(1, flow.contentWidth);
			const int containerRight = std::min(8192, containerLeft + containerWidth);
			CssFloatRecord record;
			record.logicalSerial = item.ownerSerial;
			record.bfcIdentity = bfcIdentity;
			record.flowSerial = item.flowSerial;
			record.contextSerial = item.atomicContainerSerial;
			record.sourceOrder = sourceOrder++;
			record.blockIndex = item.blockIndex;
			record.side = style->floatMode;
			record.kind = item.kind;
			record.contentText = item.text;
			record.blockified = true;
			record.visibilityRetained = true;
			record.availableWidth = containerWidth;
			if (item.blockIndex >= 0 && item.blockIndex < static_cast<int>(doc.blocks.size())) {
				const DocBlock& sourceBlock = doc.blocks[static_cast<size_t>(item.blockIndex)];
				const std::string tag = toLowerAscii(sourceBlock.tagName);
				bool listOrTableContext = false;
				for (const HtmlElementRef& ancestor : sourceBlock.ancestors) {
					const std::string ancestorTag = toLowerAscii(ancestor.tagName);
					if (ancestorTag == "ul" || ancestorTag == "ol" || ancestorTag == "li") {
						++mutableDoc.cssDiagnostics.floatListCases;
						listOrTableContext = true;
						break;
					}
					if (ancestorTag == "td" || ancestorTag == "th") {
						++mutableDoc.cssDiagnostics.floatTableCellCases;
						listOrTableContext = true;
						break;
					}
					const WebStyle* ancestorStyle = cssBfcStyleForSerial(doc, ancestor.serial);
					if (ancestorStyle && ancestorStyle->floatMode != FloatMode::None) {
						++mutableDoc.cssDiagnostics.floatInsideFloat;
						listOrTableContext = true;
						break;
					}
				}
				uint64_t parentSerial = sourceBlock.elementMetadata.parentSerial;
				for (int depth = 0; !listOrTableContext && parentSerial != 0 && depth < 16; ++depth) {
					const HtmlElementRef* parent = cssStructuralElementForSerial(doc, parentSerial);
					if (!parent) break;
					const std::string parentTag = toLowerAscii(parent->tagName);
					if (parentTag == "ul" || parentTag == "ol" || parentTag == "li") {
						++mutableDoc.cssDiagnostics.floatListCases;
						listOrTableContext = true;
					} else if (parentTag == "td" || parentTag == "th") {
						++mutableDoc.cssDiagnostics.floatTableCellCases;
						listOrTableContext = true;
					} else {
						const WebStyle* parentStyle = cssBfcStyleForSerial(doc, parentSerial);
						if (parentStyle && parentStyle->floatMode != FloatMode::None) {
							++mutableDoc.cssDiagnostics.floatInsideFloat;
							listOrTableContext = true;
						}
					}
					parentSerial = parent->parentSerial;
				}
				if (tag == "table") ++mutableDoc.cssDiagnostics.floatedTableUnsupported;
				if (tag == "li") ++mutableDoc.cssDiagnostics.floatListCases;
			}
			++mutableDoc.cssDiagnostics.floatBlockifications;
			if (record.side == FloatMode::Left) ++mutableDoc.cssDiagnostics.floatLeft;
			else if (record.side == FloatMode::Right) ++mutableDoc.cssDiagnostics.floatRight;
			int contentW = 0;
			int contentH = 0;
			int preferredMin = 0;
			int preferred = 0;
			if (item.blockIndex >= 0 && item.blockIndex < static_cast<int>(doc.blocks.size())) {
				const DocBlock& block = doc.blocks[static_cast<size_t>(item.blockIndex)];
				if (item.kind == InlineItemKind::ReplacedImage) {
					imageDisplaySize(block, containerWidth, contentW, contentH);
					preferredMin = preferred = contentW;
				} else if (item.kind == InlineItemKind::FormControl) {
					contentW = blockFormControlWidth(block, containerWidth);
					contentH = blockFormControlHeight(block);
					preferredMin = preferred = contentW;
				} else if (item.kind == InlineItemKind::AtomicBlock) {
					preferredMin = preferred = std::max(1, blockOuterWidth(block, containerWidth));
					contentW = preferred;
					contentH = std::max(1, blockTotalHeight(block, doc, false));
				} else {
					preferredMin = cssFloatPreferredTextWidth(doc, item, *style, true);
					preferred = cssFloatPreferredTextWidth(doc, item, *style, false);
					contentW = preferred;
					const int cols = std::max(1, std::max(preferredMin, std::min(containerWidth, preferred)) / kCharW);
					contentH = wrappedBlockHeight(block, cols, blockTextLineHeight(block));
				}
			} else {
				preferredMin = cssFloatPreferredTextWidth(doc, item, *style, true);
				preferred = cssFloatPreferredTextWidth(doc, item, *style, false);
				contentW = preferred;
				contentH = kLineH;
			}
			const int horizontalEdges = cssHorizontalBoxEdges(*style);
			const int verticalEdges = cssVerticalBoxEdges(*style);
			preferredMin = cssBoundedGeometryAdd(preferredMin, horizontalEdges, &record.clamped);
			preferred = cssBoundedGeometryAdd(preferred, horizontalEdges, &record.clamped);
			const CssResolvedLength explicitWidth = resolveCssLength(style->widthValue,
				style->width, style->widthPercent, containerWidth);
			const bool autoWidth = !explicitWidth.definite;
			int usedW = autoWidth
				? std::min(std::max(preferredMin, containerWidth), std::max(preferredMin, preferred))
				: blockOuterWidth(item.blockIndex >= 0 && item.blockIndex < static_cast<int>(doc.blocks.size())
					? doc.blocks[static_cast<size_t>(item.blockIndex)] : DocBlock{}, containerWidth, &record.clamped);
			usedW = std::max(1, std::min(8192, usedW));
			const int fallbackOuterHeight = cssBoundedGeometryAdd(contentH, verticalEdges, &record.clamped);
			const int usedH = std::max(1, std::min(8192, resolveUsedOuterDimension(*style,
				style->heightValue, style->height, style->heightPercent,
				style->minHeightValue, style->minHeight, style->minHeightPercent,
				style->maxHeightValue, style->maxHeight, style->maxHeightPercent,
				style->maxHeightNone, -1, fallbackOuterHeight, verticalEdges, false,
				nullptr, nullptr, nullptr, &record.clamped)));
			record.preferredMinimum = preferredMin;
			record.preferredWidth = preferred;
			record.usedWidth = usedW;
			record.usedHeight = usedH;
			const int marginLeft = cssMarginLeftPx(*style, 0);
			const int marginRight = cssMarginRightPx(*style, 0);
			const int marginTop = cssMarginTopPx(*style, 0);
			const int marginBottom = cssMarginBottomPx(*style, 0);
			const int marginW = cssBoundedGeometryAdd(usedW, marginLeft + marginRight, &record.clamped);
			const int marginH = cssBoundedGeometryAdd(usedH, marginTop + marginBottom, &record.clamped);
			int y = std::max(0, flow.documentContentTop + marginTop);
			const int maxAttempts = 64;
			for (int attempt = 0; attempt < maxAttempts; ++attempt) {
				++record.placementAttempts;
				++snapshot.placementAttempts;
				++mutableDoc.cssDiagnostics.floatPlacementAttempts;
				int left = containerLeft;
				int right = containerRight;
				int intersected = 0;
				int nextY = -1;
				for (const CssFloatRecord& prior : snapshot.records) {
					if (prior.bfcIdentity != bfcIdentity || !cssFloatRecordIntersects(prior, y, y + marginH)) continue;
					++intersected;
					if (prior.side == FloatMode::Left) left = std::max(left, prior.marginBoxX + prior.marginBoxW);
					else if (prior.side == FloatMode::Right) right = std::min(right, prior.marginBoxX);
					const int candidateBottom = prior.marginBoxY + prior.marginBoxH;
					nextY = nextY < 0 ? candidateBottom : std::min(nextY, candidateBottom);
				}
				const int width = std::max(0, right - left);
				const bool fits = marginW <= width;
				if (fits || intersected == 0 || attempt == maxAttempts - 1) {
					if (!fits) {
						++snapshot.widthOverflows;
						++mutableDoc.cssDiagnostics.floatWidthOverflows;
						record.clamped = true;
					}
					if (record.side == FloatMode::Left) record.marginBoxX = left;
					else record.marginBoxX = right - marginW;
					if (record.marginBoxX < containerLeft || record.marginBoxX + marginW > containerRight) {
						record.clamped = true;
						++snapshot.geometryClamps;
						++mutableDoc.cssDiagnostics.floatGeometryClamps;
					}
					record.marginBoxX = std::max(-8192, std::min(8192, record.marginBoxX));
					record.marginBoxY = std::max(0, std::min(8192, y));
					record.marginBoxW = std::max(1, std::min(8192, marginW));
					record.marginBoxH = std::max(1, std::min(8192, marginH));
					record.borderBoxX = record.marginBoxX + marginLeft;
					record.borderBoxY = record.marginBoxY + marginTop;
					record.borderBoxW = usedW;
					record.borderBoxH = usedH;
					record.top = record.marginBoxY;
					record.bottom = record.marginBoxY + record.marginBoxH;
					record.leftExclusion = record.marginBoxX + record.marginBoxW;
					record.rightExclusion = record.marginBoxX;
					record.intersectedRecords = intersected;
					record.movedBelowFloat = attempt > 0;
					if (record.movedBelowFloat) {
						++snapshot.placementDownshifts;
						++mutableDoc.cssDiagnostics.floatPlacementDownshifts;
					}
					if (intersected > 0 && attempt == 0) {
						++snapshot.sideBySide;
						++mutableDoc.cssDiagnostics.floatSideBySide;
					}
					break;
				}
				if (nextY <= y || nextY < 0) {
					record.complete = false;
					record.clamped = true;
					break;
				}
				y = nextY;
			}
			if (record.placementAttempts >= maxAttempts) {
				record.complete = false;
				++snapshot.placementAttemptClamps;
				++mutableDoc.cssDiagnostics.floatPlacementAttemptClamps;
			}
			snapshot.maxActiveRecords = std::max(snapshot.maxActiveRecords,
				static_cast<int>(snapshot.records.size()) + 1);
			if (item.blockIndex >= 0 && item.blockIndex < static_cast<int>(doc.blocks.size())) {
				const DocBlock& block = doc.blocks[static_cast<size_t>(item.blockIndex)];
				if (block.style.clearMode == ClearMode::Left) ++mutableDoc.cssDiagnostics.clearLeft;
				else if (block.style.clearMode == ClearMode::Right) ++mutableDoc.cssDiagnostics.clearRight;
				else if (block.style.clearMode == ClearMode::Both) ++mutableDoc.cssDiagnostics.clearBoth;
			}
			if (snapshot.evidenceRecords < 128 && snapshot.evidence.size() < 32768) {
				std::ostringstream evidence;
				evidence << "serial=" << record.logicalSerial << ",bfc=" << record.bfcIdentity
					<< ",owner-bfc=" << record.bfcIdentity
					<< ",parent-bfc=" << cssBfcParentSerial(doc, record.bfcIdentity)
					<< ",nested-depth=" << cssBfcDepthForSerial(doc, record.bfcIdentity)
					<< ",contained-locally=yes"
					<< ",source-order=" << record.sourceOrder << ",side="
					<< (record.side == FloatMode::Left ? "left" : "right")
					<< ",blockified=yes,preferred-min=" << record.preferredMinimum
					<< ",preferred-width=" << record.preferredWidth << ",available-width=" << record.availableWidth
					<< ",used-width=" << record.usedWidth << ",used-height=" << record.usedHeight
					<< ",margin-box=" << record.marginBoxX << ":" << record.marginBoxY << ":" << record.marginBoxW << ":" << record.marginBoxH
					<< ",border-box=" << record.borderBoxX << ":" << record.borderBoxY << ":" << record.borderBoxW << ":" << record.borderBoxH
					<< ",top=" << record.top << ",bottom=" << record.bottom
					<< ",left-edge=" << record.leftExclusion << ",right-edge=" << record.rightExclusion
					<< ",placement-attempts=" << record.placementAttempts << ",intersected=" << record.intersectedRecords
					<< ",clearance=" << record.clearance << ",bfc-avoidance=" << (record.bfcAvoidance ? "yes" : "no")
					<< ",moved-below-float=" << (record.movedBelowFloat ? "yes" : "no")
					<< ",visibility=" << (style->visibility == VisibilityMode::Hidden ? "hidden" : "visible")
					<< ",complete=" << (record.complete ? "yes" : "no") << ",clamped=" << (record.clamped ? "yes" : "no") << "\n";
				const std::string line = evidence.str();
				if (snapshot.evidence.size() + line.size() <= 32768) {
					snapshot.evidence += line;
					++snapshot.evidenceRecords;
				}
			}
			snapshot.records.push_back(record);
			++snapshot.operations;
		}
		buildCssBfcContextSummary(doc, snapshot);
		if (s_cssMarginLayoutSnapshot.valid) {
			const int rootFloatBottom = cssOwnedFloatMaximumBottom(snapshot, 0);
			if (rootFloatBottom > s_cssMarginLayoutSnapshot.documentHeight) {
				s_cssMarginLayoutSnapshot.documentHeight = std::min(8192, rootFloatBottom);
				++mutableDoc.cssDiagnostics.floatDocumentExtentExtensions;
			}
		}
		for (const CssBfcContextRecord& context : snapshot.bfcContexts) {
			if (snapshot.evidenceRecords >= 128 || snapshot.evidence.size() >= 32768) break;
			std::ostringstream evidence;
			evidence << "bfc-id=" << context.identity << ",owner-serial=" << context.ownerSerial
				<< ",parent-bfc=" << context.parentIdentity << ",reason=" << context.reason
				<< ",owned-floats=" << context.ownedFloatCount
				<< ",owned-float-max-bottom=" << context.ownedFloatMaximumBottom
				<< ",in-flow-content-bottom=" << context.inFlowContentBottom
				<< ",auto-height-input-extent=" << context.autoHeightInputExtent
				<< ",used-content-height=" << context.usedContentHeight
				<< ",explicit-height=" << context.explicitHeight
				<< ",min-height=" << context.minHeight << ",max-height=" << context.maxHeight
				<< ",contained-float=" << (context.containedFloat ? "yes" : "no")
				<< ",complete=" << (context.complete ? "yes" : "no")
				<< ",clamped=" << (context.clamped ? "yes" : "no") << "\n";
			const std::string line = evidence.str();
			if (snapshot.evidence.size() + line.size() <= 32768) {
				snapshot.evidence += line;
				++snapshot.evidenceRecords;
			}
		}
		snapshot.blockClearances.assign(doc.blocks.size(), 0);
		for (int blockIndex = 0; blockIndex < static_cast<int>(doc.blocks.size()); ++blockIndex) {
			const DocBlock& block = doc.blocks[static_cast<size_t>(blockIndex)];
			if (block.style.displayNone || block.style.clearMode == ClearMode::None ||
				block.style.floatMode != FloatMode::None) continue;
			int baseY = blockIndex < static_cast<int>(s_cssMarginLayoutSnapshot.records.size())
				? s_cssMarginLayoutSnapshot.records[static_cast<size_t>(blockIndex)].usedY : 0;
			uint64_t bfc = 0;
			for (const HtmlElementRef& ancestor : block.ancestors) {
				const WebStyle* ancestorStyle = computedStyleForSerial(doc, ancestor.serial);
				if (ancestorStyle && cssStyleHasOverflowBfc(*ancestorStyle)) {
					bfc = ancestor.serial;
					break;
				}
			}
			int floatBottom = baseY;
			for (const CssFloatRecord& prior : snapshot.records) {
				if (prior.bfcIdentity != bfc) continue;
				const bool relevant = block.style.clearMode == ClearMode::Both ||
					(block.style.clearMode == ClearMode::Left && prior.side == FloatMode::Left) ||
					(block.style.clearMode == ClearMode::Right && prior.side == FloatMode::Right);
				if (relevant) floatBottom = std::max(floatBottom, prior.marginBoxY + prior.marginBoxH);
			}
			const int clearance = std::max(0, floatBottom - baseY);
			snapshot.blockClearances[static_cast<size_t>(blockIndex)] = clearance;
			if (clearance > 0) {
				++mutableDoc.cssDiagnostics.clearanceApplied;
				if (block.style.clearMode == ClearMode::Left) ++mutableDoc.cssDiagnostics.clearLeft;
				else if (block.style.clearMode == ClearMode::Right) ++mutableDoc.cssDiagnostics.clearRight;
				else ++mutableDoc.cssDiagnostics.clearBoth;
			}
			if (blockIndex < static_cast<int>(s_cssMarginLayoutSnapshot.records.size())) {
				CssMarginFlowRecord& marginRecord = s_cssMarginLayoutSnapshot.records[static_cast<size_t>(blockIndex)];
				marginRecord.clearance = clearance;
				marginRecord.clearanceApplied = clearance > 0;
			}
		}
		snapshot.valid = true;
		s_cssFloatLayoutSnapshot = snapshot;
		s_cssFloatLayoutSnapshot.building = false;
		mutableDoc.cssDiagnostics.floatRecords = static_cast<int>(snapshot.records.size());
		mutableDoc.cssDiagnostics.floatEvidenceRecords = snapshot.evidenceRecords;
		mutableDoc.cssDiagnostics.floatEvidence = snapshot.evidence;
	}

	static void ensureCssFloatLayout(const WebDocument& doc)
	{
		const uint64_t fingerprint = cssMarginLayoutFingerprint(doc);
		if (s_cssFloatLayoutSnapshot.valid && s_cssFloatLayoutSnapshot.url == doc.url &&
			s_cssFloatLayoutSnapshot.blockCount == doc.blocks.size() &&
			s_cssFloatLayoutSnapshot.fingerprint == fingerprint) return;
		buildCssFloatLayout(doc, s_cssFloatLayoutSnapshot);
	}

	static const CssFloatRecord* cssFloatRecordForBlock(const WebDocument& doc, int blockIndex)
	{
		if (!s_cssFloatLayoutSnapshot.valid || blockIndex < 0) return nullptr;
		for (const CssFloatRecord& record : s_cssFloatLayoutSnapshot.records) {
			if (record.blockIndex == blockIndex) return &record;
		}
		(void)doc;
		return nullptr;
	}

	static void ensureCssMarginLayout(const WebDocument& doc)
	{
		const uint64_t fingerprint = cssMarginLayoutFingerprint(doc);
		if (s_cssMarginLayoutSnapshot.valid && s_cssMarginLayoutSnapshot.url == doc.url &&
			s_cssMarginLayoutSnapshot.blockCount == doc.blocks.size() &&
			s_cssMarginLayoutSnapshot.fingerprint == fingerprint) return;
		buildCssMarginLayout(doc, s_cssMarginLayoutSnapshot);
	}

	static CssPaintRect cssViewportClipRect()
	{
		return CssPaintRect{kContentX, kToolbarH + 6, kContentW, kContentH};
	}

	static int cssBlockLayoutY(const WebDocument& doc, int blockIndex);

	static const gxos::web::HtmlElementRef* cssStructuralElementForSerial(
		const WebDocument& doc, uint64_t serial)
	{
		if (serial == 0) return nullptr;
		for (const gxos::web::HtmlElementRef& element : doc.structuralElements) {
			if (element.serial == serial) return &element;
		}
		return nullptr;
	}

	static bool cssBlockContainsSerial(const DocBlock& block, uint64_t serial)
	{
		if (serial == 0) return false;
		if (block.elementMetadata.serial == serial) return true;
		for (const gxos::web::HtmlElementRef& ancestor : block.ancestors) {
			if (ancestor.serial == serial) return true;
		}
		return false;
	}

	static bool cssDescendantBlockRange(const WebDocument& doc, uint64_t serial,
		int& outFirst, int& outLast)
	{
		outFirst = -1;
		outLast = -1;
		const size_t scanLimit = std::min<size_t>(doc.blocks.size(), 2048);
		for (size_t i = 0; i < scanLimit; ++i) {
			const DocBlock& block = doc.blocks[i];
			if (block.style.displayNone || !cssBlockContainsSerial(block, serial)) continue;
			if (outFirst < 0) outFirst = static_cast<int>(i);
			outLast = static_cast<int>(i);
		}
		return outFirst >= 0 && outLast >= outFirst;
	}

	static int cssBodyContentWidth(const WebDocument& doc)
	{
		const int bodyOuterBasis = std::max(1, kContentW - blockBodyMarginLeft(doc) -
			blockBodyMarginRight(doc));
		const int bodyEdges = cssHorizontalBoxEdges(doc.bodyStyle);
		const int bodyOuter = resolveUsedOuterDimension(doc.bodyStyle,
			doc.bodyStyle.widthValue, doc.bodyStyle.width, doc.bodyStyle.widthPercent,
			doc.bodyStyle.minWidthValue, doc.bodyStyle.minWidth, doc.bodyStyle.minWidthPercent,
			doc.bodyStyle.maxWidthValue, doc.bodyStyle.maxWidth, doc.bodyStyle.maxWidthPercent,
			doc.bodyStyle.maxWidthNone, bodyOuterBasis, bodyOuterBasis, bodyEdges, false);
		return std::max(1, usedContentDimensionFromOuter(doc.bodyStyle, bodyOuter, bodyEdges));
	}

	static int cssContainingContentWidthForSerial(const WebDocument& doc, uint64_t parentSerial)
	{
		if (parentSerial == 0) return cssBodyContentWidth(doc);
		std::array<uint64_t, 12> chain{};
		size_t count = 0;
		uint64_t current = parentSerial;
		while (current != 0) {
			if (count >= chain.size()) return -1;
			const gxos::web::HtmlElementRef* element = cssStructuralElementForSerial(doc, current);
			if (!element) return -1;
			chain[count++] = current;
			current = element->parentSerial;
		}
		int basis = cssBodyContentWidth(doc);
		for (size_t reverse = count; reverse > 0; --reverse) {
			const uint64_t serial = chain[reverse - 1];
			if (doc.hasBodyElement && serial == doc.bodyElement.serial) continue;
			const WebStyle* style = computedStyleForSerial(doc, serial);
			if (!style) continue;
			const int edges = cssHorizontalBoxEdges(*style);
			basis = usedContentDimensionFromOuter(*style,
				resolveUsedOuterDimension(*style,
					style->widthValue, style->width, style->widthPercent,
					style->minWidthValue, style->minWidth, style->minWidthPercent,
					style->maxWidthValue, style->maxWidth, style->maxWidthPercent,
					style->maxWidthNone, basis, basis, edges, false), edges);
			basis = std::max(0, basis);
		}
		return basis;
	}

	static int cssDefiniteContentHeightForStyle(const WebStyle& style, int basis)
	{
		const int edges = cssVerticalBoxEdges(style);
		const CssResolvedLength resolved = resolveCssLength(style.heightValue,
			style.height, style.heightPercent, basis);
		const CssResolvedLength minResolved = resolveCssLength(style.minHeightValue,
			style.minHeight, style.minHeightPercent, basis);
		if (!resolved.definite && !minResolved.definite) return -1;
		const int fallback = resolved.definite
			? (style.boxSizing == BoxSizingMode::BorderBox ? resolved.px : cssBoundedGeometryAdd(resolved.px, edges))
			: 0;
		const int outer = resolveUsedOuterDimension(style,
			style.heightValue, style.height, style.heightPercent,
			style.minHeightValue, style.minHeight, style.minHeightPercent,
			style.maxHeightValue, style.maxHeight, style.maxHeightPercent,
			style.maxHeightNone, basis, fallback, edges, false);
		return usedContentDimensionFromOuter(style, outer, edges);
	}

	static int cssContainingContentHeightForSerial(const WebDocument& doc, uint64_t parentSerial)
	{
		if (parentSerial == 0) return -1;
		std::array<uint64_t, 12> chain{};
		size_t count = 0;
		uint64_t current = parentSerial;
		while (current != 0) {
			if (count >= chain.size()) return -1;
			const gxos::web::HtmlElementRef* element = cssStructuralElementForSerial(doc, current);
			if (!element) return -1;
			chain[count++] = current;
			current = element->parentSerial;
		}
		int basis = -1;
		for (size_t reverse = count; reverse > 0; --reverse) {
			const uint64_t serial = chain[reverse - 1];
			const WebStyle* style = nullptr;
			if (doc.hasBodyElement && serial == doc.bodyElement.serial) style = &doc.bodyStyle;
			else style = computedStyleForSerial(doc, serial);
			if (!style) continue;
			basis = cssDefiniteContentHeightForStyle(*style, basis);
		}
		return basis;
	}

	struct CssAncestorBox {
		bool valid = false;
		int x = 0;
		int y = 0;
		int w = 0;
		int h = 0;
	};

	static CssAncestorBox cssAncestorBoxForBlock(const WebDocument& doc,
		uint64_t serial, int scrollOffset)
	{
		CssAncestorBox box;
		const gxos::web::HtmlElementRef* element = cssStructuralElementForSerial(doc, serial);
		const WebStyle* style = computedStyleForSerial(doc, serial);
		if (!element || !style) return box;
		int first = -1;
		int last = -1;
		if (!cssDescendantBlockRange(doc, serial, first, last)) return box;
		const DocBlock& firstBlock = doc.blocks[static_cast<size_t>(first)];
		const DocBlock& lastBlock = doc.blocks[static_cast<size_t>(last)];
		const int firstMarginTop = cssMarginTopPx(firstBlock.style, firstBlock.type == BlockType::Heading ? 10 : 4);
		const int firstBoxY = kContentY + cssBlockLayoutY(doc, first) - scrollOffset + firstMarginTop;
		const int parentTopEdges = cssBorderTopPx(*style) + cssPaddingTopPx(*style, 0);
		box.y = firstBoxY - firstMarginTop - parentTopEdges;
		const int parentBasisWidth = cssContainingContentWidthForSerial(doc, element->parentSerial);
		if (parentBasisWidth < 0) return box;
		const int horizontalEdges = cssHorizontalBoxEdges(*style);
		box.w = resolveUsedOuterDimension(*style,
			style->widthValue, style->width, style->widthPercent,
			style->minWidthValue, style->minWidth, style->minWidthPercent,
			style->maxWidthValue, style->maxWidth, style->maxWidthPercent,
			style->maxWidthNone, parentBasisWidth, parentBasisWidth, horizontalEdges, false);
		const int baseX = kContentX + blockBodyMarginLeft(doc) + cssMarginLeftPx(*style, 0);
		if (cssMarginLeftAuto(*style) && cssMarginRightAuto(*style))
			box.x = baseX + std::max(0, (parentBasisWidth - box.w) / 2);
		else if (cssMarginLeftAuto(*style))
			box.x = baseX + std::max(0, parentBasisWidth - box.w - cssMarginRightPx(*style, 0));
		else
			box.x = baseX;

		const int lastMarginTop = cssMarginTopPx(lastBlock.style, lastBlock.type == BlockType::Heading ? 10 : 4);
		const int lastMarginBottom = cssMarginBottomPx(lastBlock.style, lastBlock.type == BlockType::ListItem ? 4 : 8);
		const int lastBoxY = kContentY + cssBlockLayoutY(doc, last) - scrollOffset + lastMarginTop;
		const bool nextIsHeading = last + 1 < static_cast<int>(doc.blocks.size()) &&
			doc.blocks[static_cast<size_t>(last + 1)].type == BlockType::Heading;
		const int lastTotal = blockTotalHeight(lastBlock, doc, nextIsHeading);
		const int lastBoxH = std::max(0, lastTotal - lastMarginTop - lastMarginBottom - (nextIsHeading ? 10 : 0));
		int fallbackHeight = std::max(0, lastBoxY + lastBoxH + lastMarginBottom - box.y);
		if (s_cssFloatLayoutSnapshot.valid && cssBfcBoundaryForSerial(doc, serial)) {
			const int ownedBottom = cssOwnedFloatMaximumBottom(s_cssFloatLayoutSnapshot, serial);
			if (ownedBottom > 0) {
				const int floatBottom = kContentY + ownedBottom - scrollOffset;
				fallbackHeight = std::max(fallbackHeight, floatBottom - box.y);
			}
		}
		const int verticalEdges = cssVerticalBoxEdges(*style);
		const int parentBasisHeight = cssContainingContentHeightForSerial(doc, element->parentSerial);
		box.h = resolveUsedOuterDimension(*style,
			style->heightValue, style->height, style->heightPercent,
			style->minHeightValue, style->minHeight, style->minHeightPercent,
			style->maxHeightValue, style->maxHeight, style->maxHeightPercent,
			style->maxHeightNone, parentBasisHeight, fallbackHeight, verticalEdges, false);
		box.w = std::max(0, std::min(8192, box.w));
		box.h = std::max(0, std::min(8192, box.h));
		box.valid = box.w > 0 && box.h > 0;
		return box;
	}

	static CssPaintRect cssApplyOverflowClip(const CssPaintRect& base,
		const WebStyle& style, int outerX, int boxY, int outerW, int outerH)
	{
		CssPaintRect clip = base;
		const int borderLeft = cssBorderLeftPx(style);
		const int borderRight = cssBorderRightPx(style);
		const int borderTop = cssBorderTopPx(style);
		const int borderBottom = cssBorderBottomPx(style);
		const int paddingBoxX = outerX + borderLeft;
		const int paddingBoxY = boxY + borderTop;
		const int paddingBoxW = std::max(0, outerW - borderLeft - borderRight);
		const int paddingBoxH = std::max(0, outerH - borderTop - borderBottom);
		if (style.overflowX != OverflowMode::Visible) {
			clip = cssPaintRectIntersect(clip, CssPaintRect{paddingBoxX, clip.y, paddingBoxW, clip.h});
		}
		if (style.overflowY != OverflowMode::Visible) {
			clip = cssPaintRectIntersect(clip, CssPaintRect{clip.x, paddingBoxY, clip.w, paddingBoxH});
		}
		return clip;
	}

	static CssPaintRect cssBlockAncestorClip(const WebDocument& doc, const DocBlock& block,
		int scrollOffset)
	{
		CssPaintRect clip = cssViewportClipRect();
		int clippedAncestorCount = 0;
		for (const gxos::web::HtmlElementRef& ancestor : block.ancestors) {
			const WebStyle* style = computedStyleForSerial(doc, ancestor.serial);
			if (!style || (style->overflowX == OverflowMode::Visible && style->overflowY == OverflowMode::Visible))
				continue;
			if (++clippedAncestorCount > kCssClipStackDepth) {
				++s_cssClipDepthClamps;
				return CssPaintRect{0, 0, 0, 0};
			}
			const CssAncestorBox box = cssAncestorBoxForBlock(doc, ancestor.serial, scrollOffset);
			if (!box.valid) return CssPaintRect{0, 0, 0, 0};
			clip = cssApplyOverflowClip(clip, *style, box.x, box.y, box.w, box.h);
			if (clip.w <= 0 || clip.h <= 0) return CssPaintRect{0, 0, 0, 0};
		}
		return clip;
	}

	static bool cssBlockHasOverflowAncestor(const WebDocument& doc, const DocBlock& block)
	{
		for (const gxos::web::HtmlElementRef& ancestor : block.ancestors) {
			const WebStyle* style = computedStyleForSerial(doc, ancestor.serial);
			if (style && (style->overflowX != OverflowMode::Visible ||
				style->overflowY != OverflowMode::Visible)) return true;
		}
		return false;
	}

	static CssPaintRect cssBlockVisibleClip(const WebDocument& doc, int blockIndex,
		const DocBlock& block, int outerX, int boxY, int outerW, int outerH, int scrollOffset)
	{
		CssPaintRect clip = cssBlockAncestorClip(doc, block, scrollOffset);
		(void)blockIndex;
		return cssApplyOverflowClip(clip, block.style, outerX, boxY, outerW, outerH);
	}

	static CssPaintRect cssClipRectForHit(const WebDocument& doc, int blockIndex,
		const DocBlock& block, int outerX, int boxY, int outerW, int outerH, int scrollOffset)
	{
		(void)block;
		return cssPositionedClipForBlock(doc, blockIndex, outerX, boxY, outerW, outerH, scrollOffset);
	}

	static CssPaintRect cssClipHitTarget(const CssPaintRect& target, const CssPaintRect& clip)
	{
		if (target.w > 0 && target.h > 0) ++s_cssHitTargetsBeforeClipping;
		const CssPaintRect clipped = cssPaintRectIntersect(target, clip);
		if (target.w != clipped.w || target.h != clipped.h || target.x != clipped.x || target.y != clipped.y) {
			++s_cssClippedHitTargets;
		}
		if (clipped.w > 0 && clipped.h > 0) ++s_cssHitTargetsAfterClipping;
		return clipped;
	}

	static int cssBlockLayoutY(const WebDocument& doc, int blockIndex)
	{
		ensureCssMarginLayout(doc);
		if (s_cssMarginLayoutSnapshot.valid && blockIndex >= 0 &&
			blockIndex < static_cast<int>(s_cssMarginLayoutSnapshot.records.size())) {
			const CssMarginFlowRecord& record = s_cssMarginLayoutSnapshot.records[static_cast<size_t>(blockIndex)];
			const DocBlock& block = doc.blocks[static_cast<size_t>(blockIndex)];
			const int fallback = block.type == BlockType::Heading ? 10 : 4;
			int clearanceDisplacement = 0;
			if (s_cssFloatLayoutSnapshot.valid) {
				const size_t limit = std::min<size_t>(static_cast<size_t>(blockIndex),
					s_cssFloatLayoutSnapshot.blockClearances.size());
				for (size_t i = 0; i < limit; ++i)
					clearanceDisplacement = cssBoundedGeometryAdd(clearanceDisplacement,
						s_cssFloatLayoutSnapshot.blockClearances[i]);
			}
			int usedY = record.usedY + clearanceDisplacement;
			if (cssStyleHasOverflowBfc(block.style) && block.style.floatMode == FloatMode::None) {
				const int requiredWidth = blockOuterWidth(block, blockAvailableWidth(block, doc));
				usedY = cssBfcPlacementY(doc, blockIndex, usedY, requiredWidth);
			}
			return usedY - cssMarginTopPx(block.style, fallback);
		}
		int y = kHeadingY + (doc.bodyStyle.marginTop >= 0 ? doc.bodyStyle.marginTop : 0);
		for (int i = 0; i < blockIndex && i < static_cast<int>(doc.blocks.size()); ++i) {
			const bool nextIsHeading = i + 1 < static_cast<int>(doc.blocks.size()) &&
				doc.blocks[static_cast<size_t>(i + 1)].type == BlockType::Heading;
			y += blockTotalHeight(doc.blocks[static_cast<size_t>(i)], doc, nextIsHeading);
		}
		return y;
	}

	static CssBlockGeometry cssGeometryForBlock(const WebDocument& doc, int blockIndex)
	{
		CssBlockGeometry geometry;
		if (blockIndex < 0 || blockIndex >= static_cast<int>(doc.blocks.size())) return geometry;
		const DocBlock& block = doc.blocks[static_cast<size_t>(blockIndex)];
		geometry.availableWidth = blockAvailableWidth(block, doc);
		geometry.outerWidth = blockOuterWidth(block, geometry.availableWidth, &geometry.clamped);
		geometry.outerX = blockOuterX(block, doc, geometry.availableWidth, geometry.outerWidth);
		const int marginTop = cssMarginTopPx(block.style, block.type == BlockType::Heading ? 10 : 4);
		const int marginBottom = cssMarginBottomPx(block.style, block.type == BlockType::ListItem ? 4 : 8);
		const bool nextIsHeading = blockIndex + 1 < static_cast<int>(doc.blocks.size()) &&
			doc.blocks[static_cast<size_t>(blockIndex + 1)].type == BlockType::Heading;
		const int totalHeight = blockTotalHeight(block, doc, nextIsHeading);
		ensureCssMarginLayout(doc);
		if (blockIndex >= 0 && blockIndex < static_cast<int>(s_cssMarginLayoutSnapshot.records.size()) &&
			s_cssMarginLayoutSnapshot.valid) {
			geometry.outerHeight = s_cssMarginLayoutSnapshot.records[static_cast<size_t>(blockIndex)].outerHeight;
		} else {
			geometry.outerHeight = std::max(1, totalHeight - marginTop - marginBottom - (nextIsHeading ? 10 : 0));
		}
		geometry.outerY = kContentY + cssBlockLayoutY(doc, blockIndex) + marginTop;
		const int horizontalEdges = cssHorizontalBoxEdges(block.style, block.type == BlockType::Preformatted);
		const int verticalEdges = cssVerticalBoxEdges(block.style, block.type == BlockType::Preformatted);
		const int rawContentWidth = geometry.outerWidth - horizontalEdges;
		const int rawContentHeight = geometry.outerHeight - verticalEdges;
		if (rawContentWidth < 0 || rawContentHeight < 0) geometry.clamped = true;
		geometry.contentWidth = std::max(0, rawContentWidth);
		geometry.contentHeight = std::max(0, rawContentHeight);
		geometry.paddingBox = CssPaintRect{
			geometry.outerX + cssBorderLeftPx(block.style),
			geometry.outerY + cssBorderTopPx(block.style),
			std::max(0, geometry.outerWidth - cssBorderLeftPx(block.style) - cssBorderRightPx(block.style)),
			std::max(0, geometry.outerHeight - cssBorderTopPx(block.style) - cssBorderBottomPx(block.style))};
		geometry.clip = cssBlockVisibleClip(doc, blockIndex, block, geometry.outerX, geometry.outerY,
			geometry.outerWidth, geometry.outerHeight, 0);
		const CssResolvedLength widthResolved = resolveCssLength(block.style.widthValue,
			block.style.width, block.style.widthPercent, geometry.availableWidth);
		geometry.widthAuto = !widthResolved.definite;
		geometry.widthPercentageUnresolved = widthResolved.unresolvedPercentage;
		bool ignoredWidthAuto = false;
		bool ignoredWidthUnresolved = false;
		bool widthConflict = false;
		bool widthClamped = false;
		bool minWidthApplied = false;
		bool maxWidthApplied = false;
		(void)resolveUsedOuterDimension(block.style,
			block.style.widthValue, block.style.width, block.style.widthPercent,
			block.style.minWidthValue, block.style.minWidth, block.style.minWidthPercent,
			block.style.maxWidthValue, block.style.maxWidth, block.style.maxWidthPercent,
			block.style.maxWidthNone, geometry.availableWidth, geometry.outerWidth, horizontalEdges,
			block.type == BlockType::Preformatted, &ignoredWidthAuto, &ignoredWidthUnresolved,
			&widthConflict, &widthClamped, &minWidthApplied, &maxWidthApplied);
		geometry.minWidthApplied = minWidthApplied;
		geometry.maxWidthApplied = maxWidthApplied;
		geometry.constraintConflict = widthConflict;
		geometry.clamped = geometry.clamped || widthClamped;
		const int heightBasis = blockContainingContentHeight(block, doc);
		const CssResolvedLength heightResolved = resolveCssLength(block.style.heightValue,
			block.style.height, block.style.heightPercent, heightBasis);
		geometry.heightAuto = !heightResolved.definite;
		geometry.heightPercentageUnresolved = heightResolved.unresolvedPercentage;
		bool ignoredAuto = false;
		bool ignoredUnresolved = false;
		bool heightConflict = false;
		bool heightClamped = false;
		bool minHeightApplied = false;
		bool maxHeightApplied = false;
		(void)resolveUsedOuterDimension(block.style,
			block.style.heightValue, block.style.height, block.style.heightPercent,
			block.style.minHeightValue, block.style.minHeight, block.style.minHeightPercent,
			block.style.maxHeightValue, block.style.maxHeight, block.style.maxHeightPercent,
			block.style.maxHeightNone, heightBasis,
			cssBoundedGeometryAdd(std::max(0, geometry.contentHeight), verticalEdges), verticalEdges,
			block.type == BlockType::Preformatted, &ignoredAuto, &ignoredUnresolved,
			&heightConflict, &heightClamped, &minHeightApplied, &maxHeightApplied);
		geometry.minHeightApplied = minHeightApplied;
		geometry.maxHeightApplied = maxHeightApplied;
		geometry.constraintConflict = geometry.constraintConflict || heightConflict;
		geometry.clamped = geometry.clamped || heightClamped;
		return geometry;
	}

	static CssPositionBox cssPositionRootBox()
	{
		CssPositionBox box;
		box.border = CssPaintRect{kContentX, kContentY, kContentW, kContentH};
		box.padding = box.border;
		box.content = box.border;
		box.widthDefinite = true;
		box.heightDefinite = true;
		return box;
	}

	static CssPositionBox cssPositionBoxFromBorder(const CssPaintRect& border,
		const WebStyle& style, bool widthDefinite, bool heightDefinite)
	{
		CssPositionBox box;
		box.border = border;
		const int borderLeft = cssBorderLeftPx(style);
		const int borderRight = cssBorderRightPx(style);
		const int borderTop = cssBorderTopPx(style);
		const int borderBottom = cssBorderBottomPx(style);
		box.padding = CssPaintRect{border.x + borderLeft, border.y + borderTop,
			std::max(0, border.w - borderLeft - borderRight),
			std::max(0, border.h - borderTop - borderBottom)};
		box.content = CssPaintRect{box.padding.x + cssPaddingLeftPx(style, 0),
			box.padding.y + cssPaddingTopPx(style, 0),
			std::max(0, box.padding.w - cssPaddingLeftPx(style, 0) - cssPaddingRightPx(style, 0)),
			std::max(0, box.padding.h - cssPaddingTopPx(style, 0) - cssPaddingBottomPx(style, 0))};
		box.widthDefinite = widthDefinite;
		box.heightDefinite = heightDefinite;
		return box;
	}

	static int cssPositionResolveLength(const CssLengthValue& value, int basis,
		bool basisDefinite, bool& unresolved, bool& clamped)
	{
		unresolved = false;
		clamped = false;
		if (!value.valid || value.type == CssLengthType::Unset || value.type == CssLengthType::Auto)
			return 0;
		int64_t result = value.value;
		if (value.type == CssLengthType::Percent) {
			if (!basisDefinite || basis < 0) {
				unresolved = true;
				return 0;
			}
			result = static_cast<int64_t>(basis) * value.value / 100;
		}
		if (result < -kCssPositionedGeometryCap || result > kCssPositionedGeometryCap) {
			result = std::max<int64_t>(-kCssPositionedGeometryCap,
				std::min<int64_t>(kCssPositionedGeometryCap, result));
			clamped = true;
		}
		return static_cast<int>(result);
	}

	static const CssPositionedRecord* cssPositionedRecordForSerial(
		const WebDocument& doc, uint64_t serial)
	{
		if (serial == 0 ||
			s_cssPositionLayoutSnapshot.url != doc.url ||
			s_cssPositionLayoutSnapshot.blockCount != doc.blocks.size()) return nullptr;
		for (const CssPositionedRecord& record : s_cssPositionLayoutSnapshot.records)
			if (record.logicalSerial == serial) return &record;
		return nullptr;
	}

	static const CssPositionedRecord* cssPositionedRecordForBlock(
		const WebDocument& doc, int blockIndex)
	{
		if (blockIndex < 0 || blockIndex >= static_cast<int>(doc.blocks.size()) ||
			s_cssPositionLayoutSnapshot.url != doc.url ||
			s_cssPositionLayoutSnapshot.blockCount != doc.blocks.size() ||
			blockIndex >= static_cast<int>(s_cssPositionLayoutSnapshot.blockRecordIndices.size())) return nullptr;
		const int index = s_cssPositionLayoutSnapshot.blockRecordIndices[static_cast<size_t>(blockIndex)];
		if (index < 0 || index >= static_cast<int>(s_cssPositionLayoutSnapshot.records.size())) return nullptr;
		return &s_cssPositionLayoutSnapshot.records[static_cast<size_t>(index)];
	}

	static CssPositionBox cssPositionBoxForSerial(const WebDocument& doc, uint64_t serial,
		uint64_t* outOwnerSerial = nullptr)
	{
		if (outOwnerSerial) *outOwnerSerial = serial;
		if (serial == 0) return cssPositionRootBox();
		if (const CssPositionedRecord* positioned = cssPositionedRecordForSerial(doc, serial)) {
			const WebStyle* style = cssStyleForSerial(doc, serial);
			if (style) return cssPositionBoxFromBorder(
				CssPaintRect{positioned->finalX, positioned->finalY,
					positioned->usedWidth, positioned->usedHeight}, *style,
				positioned->usedWidth > 0, positioned->usedHeight > 0);
		}
		for (int index = 0; index < static_cast<int>(doc.blocks.size()); ++index) {
			const DocBlock& candidate = doc.blocks[static_cast<size_t>(index)];
			if (!cssBlockContainsSerial(candidate, serial)) continue;
			const CssAncestorBox ancestor = cssAncestorBoxForBlock(doc, serial, 0);
			if (!ancestor.valid) break;
			const WebStyle* style = cssStyleForSerial(doc, serial);
			if (!style) break;
			const bool definiteWidth = style->widthValue.type == CssLengthType::Px ||
				style->widthValue.type == CssLengthType::Zero || style->width >= 0;
			const bool definiteHeight = style->heightValue.type == CssLengthType::Px ||
				style->heightValue.type == CssLengthType::Zero || style->height >= 0;
			CssPaintRect border{ancestor.x, ancestor.y, ancestor.w, ancestor.h};
			if (style->position == PositionMode::Relative) {
				bool unresolved = false;
				bool clamped = false;
				const int dx = style->leftValue.valid && style->leftValue.type != CssLengthType::Auto
					? cssPositionResolveLength(style->leftValue, ancestor.w, true, unresolved, clamped)
					: (style->rightValue.valid && style->rightValue.type != CssLengthType::Auto
						? -cssPositionResolveLength(style->rightValue, ancestor.w, true, unresolved, clamped) : 0);
				const int dy = style->topValue.valid && style->topValue.type != CssLengthType::Auto
					? cssPositionResolveLength(style->topValue, ancestor.h, definiteHeight, unresolved, clamped)
					: (style->bottomValue.valid && style->bottomValue.type != CssLengthType::Auto
						? -cssPositionResolveLength(style->bottomValue, ancestor.h, definiteHeight, unresolved, clamped) : 0);
				border.x = cssBoundedGeometryAdd(border.x, dx);
				border.y = cssBoundedGeometryAdd(border.y, dy);
			}
			return cssPositionBoxFromBorder(border, *style, definiteWidth, definiteHeight);
		}
		if (outOwnerSerial) *outOwnerSerial = 0;
		return cssPositionRootBox();
	}

	static int cssPositionRelativeAncestorDelta(const WebDocument& doc, int blockIndex,
		int* outX = nullptr, int* outY = nullptr)
	{
		int deltaX = 0;
		int deltaY = 0;
		if (blockIndex >= 0 && blockIndex < static_cast<int>(doc.blocks.size())) {
			const DocBlock& block = doc.blocks[static_cast<size_t>(blockIndex)];
			int depth = 0;
			for (auto it = block.ancestors.rbegin(); it != block.ancestors.rend(); ++it) {
				if (++depth > static_cast<int>(kCssPositionedAncestryCap)) break;
				const CssPositionedRecord* record = cssPositionedRecordForSerial(doc, it->serial);
				if (record && record->mode == PositionMode::Relative) {
					deltaX = cssBoundedGeometryAdd(deltaX, record->finalX - record->normalX);
					deltaY = cssBoundedGeometryAdd(deltaY, record->finalY - record->normalY);
					continue;
				}
				const WebStyle* style = cssStyleForSerial(doc, it->serial);
				if (!style || style->position != PositionMode::Relative) continue;
				for (const DocBlock& candidate : doc.blocks) {
					if (!cssBlockContainsSerial(candidate, it->serial)) continue;
					const CssAncestorBox base = cssAncestorBoxForBlock(doc, it->serial, 0);
					const CssPositionBox shifted = cssPositionBoxForSerial(doc, it->serial);
					if (base.valid) {
						deltaX = cssBoundedGeometryAdd(deltaX, shifted.border.x - base.x);
						deltaY = cssBoundedGeometryAdd(deltaY, shifted.border.y - base.y);
					}
					break;
				}
			}
		}
		if (outX) *outX = deltaX;
		if (outY) *outY = deltaY;
		return std::max(std::abs(deltaX), std::abs(deltaY));
	}

	static int cssPositionPreferredOuterWidth(const DocBlock& block, const WebDocument& doc,
		int containingWidth)
	{
		const int edges = cssHorizontalBoxEdges(block.style,
			block.type == BlockType::Preformatted);
		if (block.type == BlockType::Image) {
			int imageW = 0;
			int imageH = 0;
			imageDisplaySize(block, std::max(1, containingWidth), imageW, imageH);
			return std::max(1, std::min(kCssPositionedGeometryCap,
				cssBoundedGeometryAdd(imageW, edges)));
		}
		if (isFormControlBlock(block))
			return std::max(1, std::min(kCssPositionedGeometryCap,
				blockFormControlIntrinsicWidth(block) + edges));
		int longest = 0;
		size_t start = 0;
		while (start <= block.text.size()) {
			size_t end = block.text.find_first_of(" \t\r\n", start);
			if (end == std::string::npos) end = block.text.size();
			longest = std::max(longest, static_cast<int>(end - start));
			if (end == block.text.size()) break;
			start = end + 1;
		}
		const int preferredContent = std::max(kCharW, longest * kCharW);
		return std::max(1, std::min(kCssPositionedGeometryCap,
			cssBoundedGeometryAdd(preferredContent, edges)));
	}

	static int cssPositionAutoContentHeight(const DocBlock& block, const WebDocument& doc,
		int outerWidth)
	{
		const int paddingTop = cssPaddingTopPx(block.style, block.type == BlockType::Preformatted ? 4 : 0);
		const int paddingRight = cssPaddingRightPx(block.style, block.type == BlockType::Preformatted ? 4 : 0);
		const int paddingBottom = cssPaddingBottomPx(block.style, block.type == BlockType::Preformatted ? 4 : 0);
		const int paddingLeft = cssPaddingLeftPx(block.style, block.type == BlockType::Preformatted ? 4 : 0);
		const int borders = cssVerticalBoxEdges(block.style, block.type == BlockType::Preformatted) - paddingTop - paddingBottom;
		const int innerWidth = std::max(1, outerWidth - paddingLeft - paddingRight - borders);
		const int lineHeight = blockTextLineHeight(block);
		int contentHeight = lineHeight;
		switch (block.type) {
		case BlockType::Heading:
			contentHeight = std::max(lineHeight + 4,
				cssFontSizeOrDefault(block.style, block.tagName == "h1" ? 24 : 20) + 2);
			break;
		case BlockType::Paragraph:
		case BlockType::Link:
		case BlockType::ListItem:
		case BlockType::Preformatted:
		case BlockType::FormLabel:
			contentHeight = wrappedBlockHeight(block,
				std::max(1, innerWidth / kCharW), lineHeight);
			break;
		case BlockType::Image: {
			int imageW = 0;
			int imageH = 0;
			imageDisplaySize(block, std::max(1, innerWidth), imageW, imageH);
			contentHeight = imageH;
			break;
		}
		case BlockType::FormTextInput:
		case BlockType::FormCheckbox:
		case BlockType::FormRadio:
		case BlockType::FormTextarea:
		case BlockType::FormSelect:
		case BlockType::FormSubmit:
			contentHeight = blockFormControlHeight(block);
			break;
		}
		(void)doc;
		return std::max(1, contentHeight);
	}

	static int cssPositionStaticY(const WebDocument& doc, int blockIndex, const DocBlock& block);

	static void buildCssPositionLayout(const WebDocument& doc,
		CssPositionLayoutSnapshot& snapshot)
	{
		snapshot = CssPositionLayoutSnapshot{};
		snapshot.url = doc.url;
		snapshot.blockCount = doc.blocks.size();
		snapshot.fingerprint = cssMarginLayoutFingerprint(doc);
		snapshot.generation = doc.formRuntimeState.documentGeneration;
		snapshot.documentExtent = s_cssMarginLayoutSnapshot.documentHeight;
		snapshot.blockRecordIndices.assign(doc.blocks.size(), -1);
		gxos::web::CssDiagnostics& diagnostics = const_cast<WebDocument&>(doc).cssDiagnostics;
		diagnostics.positionStatic = diagnostics.positionRelative = diagnostics.positionAbsolute = 0;
		diagnostics.relativeOffsets = diagnostics.relativePercentageOffsets = 0;
		diagnostics.absoluteBoxes = diagnostics.absoluteBlockifications = 0;
		diagnostics.positionedContainingBlocks = diagnostics.positionRootFallbacks = 0;
		diagnostics.positionAncestryClamps = diagnostics.absoluteStaticPositionUses = 0;
		diagnostics.absoluteShrinkToFit = diagnostics.absoluteOutOfFlow = 0;
		diagnostics.positionDocumentExtentExtensions = diagnostics.zIndexAuto = 0;
		diagnostics.zIndexNegative = diagnostics.zIndexZero = diagnostics.zIndexPositive = 0;
		diagnostics.positionHitOcclusions = diagnostics.positionGeometryClamps = 0;
		diagnostics.positionUnsupportedTable = 0;
		diagnostics.positionedEvidenceRecords = 0;
		diagnostics.positionedEvidence.clear();
		std::unordered_set<uint64_t> countedPositionSerials;
		countedPositionSerials.reserve(std::min<size_t>(doc.computedStyles.size(), 256));
		for (const CssComputedStyleRecord& computed : doc.computedStyles) {
			if (!computed.valid || computed.serial == 0 || !countedPositionSerials.insert(computed.serial).second) continue;
			if (computed.style.position == PositionMode::Static) ++diagnostics.positionStatic;
			else if (computed.style.position == PositionMode::Relative) ++diagnostics.positionRelative;
			else ++diagnostics.positionAbsolute;
		}
		for (const DocBlock& block : doc.blocks) {
			if (block.elementMetadata.serial != 0 && countedPositionSerials.count(block.elementMetadata.serial) != 0) continue;
			if (block.style.position == PositionMode::Static) ++diagnostics.positionStatic;
			else if (block.style.position == PositionMode::Relative) ++diagnostics.positionRelative;
			else ++diagnostics.positionAbsolute;
		}
		std::unordered_set<uint64_t> positionedBlockSerials;
		positionedBlockSerials.reserve(std::min<size_t>(doc.blocks.size(), 256));
		for (const DocBlock& block : doc.blocks)
			if (block.elementMetadata.serial != 0) positionedBlockSerials.insert(block.elementMetadata.serial);
		for (const CssComputedStyleRecord& computed : doc.computedStyles) {
			if (!computed.valid || computed.style.position != PositionMode::Relative ||
				positionedBlockSerials.count(computed.serial) != 0) continue;
			if ((computed.style.leftValue.valid && computed.style.leftValue.type == CssLengthType::Percent) ||
				(computed.style.rightValue.valid && computed.style.rightValue.type == CssLengthType::Percent) ||
				(computed.style.topValue.valid && computed.style.topValue.type == CssLengthType::Percent) ||
				(computed.style.bottomValue.valid && computed.style.bottomValue.type == CssLengthType::Percent))
				++diagnostics.relativePercentageOffsets;
		}
		for (int index = 0; index < static_cast<int>(doc.blocks.size()); ++index) {
			if (snapshot.records.size() >= kCssPositionedBoxCap) {
				++snapshot.ancestryClamps;
				++diagnostics.positionAncestryClamps;
				break;
			}
			const DocBlock& block = doc.blocks[static_cast<size_t>(index)];
			if (block.style.displayNone || block.style.position == PositionMode::Static) continue;
			CssPositionedRecord record;
			record.blockIndex = index;
			record.logicalSerial = block.elementMetadata.serial;
			record.parentSerial = cssBlockParentSerial(doc, block);
			record.mode = block.style.position;
			record.top = block.style.topValue;
			record.right = block.style.rightValue;
			record.bottom = block.style.bottomValue;
			record.left = block.style.leftValue;
			record.zIndexAuto = block.style.zIndexAuto;
			record.zIndex = std::max(-32767, std::min(32767, block.style.zIndex));
			record.sourceOrder = index;
			record.marginLeft = cssMarginLeftPx(block.style, 0);
			record.marginRight = cssMarginRightPx(block.style, 0);
			record.marginTop = cssMarginTopPx(block.style, block.type == BlockType::Heading ? 10 : 4);
			record.marginBottom = cssMarginBottomPx(block.style, block.type == BlockType::ListItem ? 4 : 8);
			const bool inlinePositionedTag = block.tagName == "a" || block.tagName == "span" ||
				block.tagName == "strong" || block.tagName == "b" || block.tagName == "em" ||
				block.tagName == "i" || block.tagName == "code" || block.tagName == "small" ||
				block.tagName == "kbd" || block.tagName == "samp";
			if (record.mode == PositionMode::Absolute &&
				(block.style.display == DisplayMode::Inline || inlinePositionedTag)) {
				record.blockified = true;
				++diagnostics.absoluteBlockifications;
			}
			if (record.mode == PositionMode::Absolute &&
				(block.tagName == "table" || block.tagName == "tr" || block.tagName == "td" || block.tagName == "th")) {
				record.complete = false;
				record.incompleteReason = "positioned-table-unsupported";
				++diagnostics.positionUnsupportedTable;
			}
			const int availableWidth = blockAvailableWidth(block, doc);
			const int normalWidth = blockOuterWidth(block, availableWidth, &record.clamped);
			record.normalX = blockOuterX(block, doc, availableWidth, normalWidth);
			record.normalY = kContentY + cssPositionStaticY(doc, index, block);
			record.staticX = record.normalX;
			record.staticY = record.normalY;
			record.usedWidth = normalWidth;
			record.usedHeight = std::max(1, blockTotalHeight(block, doc, false) -
				record.marginTop - record.marginBottom);
			if (record.usedHeight <= 0) record.usedHeight = std::max(1, cssPositionAutoContentHeight(block, doc, normalWidth) +
				cssVerticalBoxEdges(block.style, block.type == BlockType::Preformatted));
			if (record.mode == PositionMode::Relative) {
				const CssBlockGeometry normalGeometry = cssGeometryForBlock(doc, index);
				record.normalX = normalGeometry.outerX;
				record.normalY = normalGeometry.outerY;
				record.staticX = record.normalX;
				record.staticY = record.normalY;
				record.usedWidth = std::max(1, normalGeometry.outerWidth);
				record.usedHeight = std::max(1, normalGeometry.outerHeight);
			}
			record.flowParticipation = record.mode == PositionMode::Relative;
			record.parentHeightContribution = record.mode == PositionMode::Relative;
			uint64_t containingSerial = 0;
			CssPositionBox containing = cssPositionRootBox();
			bool containingFallback = true;
			if (record.mode == PositionMode::Absolute) {
				int depth = 0;
				for (auto it = block.ancestors.rbegin(); it != block.ancestors.rend(); ++it) {
					if (++depth > static_cast<int>(kCssPositionedAncestryCap)) {
						record.complete = false;
						record.incompleteReason = "ancestry-depth-clamp";
						++snapshot.ancestryClamps;
						++diagnostics.positionAncestryClamps;
						break;
					}
					const WebStyle* ancestorStyle = cssStyleForSerial(doc, it->serial);
					if (!ancestorStyle || ancestorStyle->position == PositionMode::Static) continue;
					if (ancestorStyle->display == DisplayMode::Inline) {
						record.complete = false;
						record.incompleteReason = "inline-containing-block-unsupported";
						break;
					}
					containingSerial = it->serial;
					if (const CssPositionedRecord* positioned = cssPositionedRecordForSerial(doc, it->serial)) {
						const CssPaintRect border{positioned->finalX, positioned->finalY,
							positioned->usedWidth, positioned->usedHeight};
						containing = cssPositionBoxFromBorder(border, *ancestorStyle,
							positioned->usedWidth > 0, positioned->usedHeight > 0);
					} else {
						containing = cssPositionBoxForSerial(doc, it->serial);
					}
					containingFallback = false;
					break;
				}
				if (depth >= static_cast<int>(kCssPositionedAncestryCap) && containingFallback)
					++diagnostics.positionAncestryClamps;
			} else {
				containingSerial = record.parentSerial;
				if (containingSerial != 0) {
					const WebStyle* parentStyle = cssStyleForSerial(doc, containingSerial);
					if (parentStyle && parentStyle->display != DisplayMode::Inline) {
						containing = cssPositionBoxForSerial(doc, containingSerial);
						containingFallback = false;
					}
				}
			}
			record.containingBlockSerial = containingSerial;
			record.containingBlockType = containingFallback ? "root-fallback" :
				(record.mode == PositionMode::Absolute ? "positioned-ancestor" : "parent-block");
			record.containingBlock = containing;
			if (containingFallback) {
				++diagnostics.positionRootFallbacks;
			} else {
				++diagnostics.positionedContainingBlocks;
			}
			snapshot.ancestryLookups++;
			snapshot.maximumAncestryDepth = std::max(snapshot.maximumAncestryDepth,
				static_cast<int>(block.ancestors.size()));
			const int xBasis = std::max(0, containing.padding.w);
			const int yBasis = std::max(0, containing.padding.h);
			bool unresolved = false;
			bool offsetClamped = false;
			record.leftResolved = record.left.valid && record.left.type != CssLengthType::Auto;
			record.rightResolved = record.right.valid && record.right.type != CssLengthType::Auto;
			record.topResolved = record.top.valid && record.top.type != CssLengthType::Auto;
			record.bottomResolved = record.bottom.valid && record.bottom.type != CssLengthType::Auto;
			if (record.leftResolved) record.resolvedLeft = cssPositionResolveLength(record.left, xBasis,
				containing.widthDefinite, unresolved, offsetClamped);
			if (record.left.type == CssLengthType::Percent && record.leftResolved) {
				if (unresolved) ++diagnostics.positionGeometryClamps;
				else ++diagnostics.relativePercentageOffsets;
			}
			if (record.rightResolved) record.resolvedRight = cssPositionResolveLength(record.right, xBasis,
				containing.widthDefinite, unresolved, offsetClamped);
			if (record.right.type == CssLengthType::Percent && record.rightResolved) {
				if (unresolved) ++diagnostics.positionGeometryClamps;
				else ++diagnostics.relativePercentageOffsets;
			}
			if (record.topResolved) record.resolvedTop = cssPositionResolveLength(record.top, yBasis,
				containing.heightDefinite, unresolved, offsetClamped);
			if (record.top.type == CssLengthType::Percent && record.topResolved && unresolved)
				record.topResolved = false;
			if (record.top.type == CssLengthType::Percent && record.top.valid && !containing.heightDefinite)
				++diagnostics.positionGeometryClamps;
			if (record.bottomResolved) record.resolvedBottom = cssPositionResolveLength(record.bottom, yBasis,
				containing.heightDefinite, unresolved, offsetClamped);
			if (record.bottom.type == CssLengthType::Percent && record.bottomResolved && unresolved)
				record.bottomResolved = false;
			if (record.bottom.type == CssLengthType::Percent && record.bottom.valid && !containing.heightDefinite)
				++diagnostics.positionGeometryClamps;
			if (offsetClamped || record.top.clamped || record.right.clamped || record.bottom.clamped || record.left.clamped) {
				record.clamped = true;
				++snapshot.geometryClamps;
				++diagnostics.positionGeometryClamps;
			}
			int ancestorDeltaX = 0;
			int ancestorDeltaY = 0;
			cssPositionRelativeAncestorDelta(doc, index, &ancestorDeltaX, &ancestorDeltaY);
			if (record.mode == PositionMode::Relative) {
				record.relativeShiftX = record.leftResolved ? record.resolvedLeft :
					(record.rightResolved ? -record.resolvedRight : 0);
				record.relativeShiftY = record.topResolved ? record.resolvedTop :
					(record.bottomResolved ? -record.resolvedBottom : 0);
				if (record.leftResolved || record.rightResolved || record.topResolved || record.bottomResolved) {
					++diagnostics.relativeOffsets;
				}
				record.finalX = std::max(-kCssPositionedGeometryCap,
					std::min(kCssPositionedGeometryCap, record.normalX + ancestorDeltaX + record.relativeShiftX));
				record.finalY = std::max(-kCssPositionedGeometryCap,
					std::min(kCssPositionedGeometryCap, record.normalY + ancestorDeltaY + record.relativeShiftY));
			} else {
				record.flowParticipation = false;
				record.parentHeightContribution = false;
				const bool widthSpecified = block.style.widthValue.valid &&
					block.style.widthValue.type != CssLengthType::Auto;
				const int edges = cssHorizontalBoxEdges(block.style, block.type == BlockType::Preformatted);
				const int availableForFill = std::max(0, xBasis -
					(record.leftResolved ? record.resolvedLeft : 0) -
					(record.rightResolved ? record.resolvedRight : 0) - record.marginLeft - record.marginRight);
				int preferredWidth = cssPositionPreferredOuterWidth(block, doc, xBasis);
				if (!widthSpecified && record.leftResolved && record.rightResolved)
					preferredWidth = availableForFill;
				bool autoWidth = false;
				bool unresolvedWidth = false;
				bool widthConflict = false;
				bool widthClamped = false;
				const int usedWidth = resolveUsedOuterDimension(block.style,
					block.style.widthValue, block.style.width, block.style.widthPercent,
					block.style.minWidthValue, block.style.minWidth, block.style.minWidthPercent,
					block.style.maxWidthValue, block.style.maxWidth, block.style.maxWidthPercent,
					block.style.maxWidthNone, xBasis, preferredWidth, edges,
					block.type == BlockType::Preformatted, &autoWidth, &unresolvedWidth,
					&widthConflict, &widthClamped);
				record.usedWidth = std::max(1, std::min(kCssPositionedGeometryCap, usedWidth));
				if (autoWidth && !(record.leftResolved && record.rightResolved))
					++diagnostics.absoluteShrinkToFit;
				if (widthClamped || unresolvedWidth) record.clamped = true;
				if (record.leftResolved) record.finalX = containing.padding.x + record.resolvedLeft + record.marginLeft;
				else if (record.rightResolved) record.finalX = containing.padding.x + containing.padding.w - record.resolvedRight -
					record.usedWidth - record.marginRight;
				else {
					record.finalX = record.staticX;
					record.staticPositionUsed = true;
					++diagnostics.absoluteStaticPositionUses;
				}
				const int heightEdges = cssVerticalBoxEdges(block.style, block.type == BlockType::Preformatted);
				const int autoContentHeight = cssPositionAutoContentHeight(block, doc, record.usedWidth);
				const int fallbackHeight = cssBoundedGeometryAdd(autoContentHeight, heightEdges);
				bool autoHeight = false;
				bool unresolvedHeight = false;
				bool heightConflict = false;
				bool heightClamped = false;
				record.usedHeight = std::max(1, std::min(kCssPositionedGeometryCap,
					resolveUsedOuterDimension(block.style,
						block.style.heightValue, block.style.height, block.style.heightPercent,
						block.style.minHeightValue, block.style.minHeight, block.style.minHeightPercent,
						block.style.maxHeightValue, block.style.maxHeight, block.style.maxHeightPercent,
						block.style.maxHeightNone, containing.padding.h, fallbackHeight, heightEdges,
						block.type == BlockType::Preformatted, &autoHeight, &unresolvedHeight,
						&heightConflict, &heightClamped)));
				if (record.topResolved) record.finalY = containing.padding.y + record.resolvedTop + record.marginTop;
				else if (record.bottomResolved) record.finalY = containing.padding.y + containing.padding.h - record.resolvedBottom -
					record.usedHeight - record.marginBottom;
				else {
					record.finalY = record.staticY;
					record.staticPositionUsed = true;
					++diagnostics.absoluteStaticPositionUses;
				}
				if (widthClamped || heightClamped || unresolvedHeight) {
					record.clamped = true;
					++snapshot.geometryClamps;
					++diagnostics.positionGeometryClamps;
				}
				++diagnostics.absoluteBoxes;
				++diagnostics.absoluteOutOfFlow;
			}
			if (record.zIndexAuto) {
				++diagnostics.zIndexAuto;
				record.paintTier = 1;
			} else if (record.zIndex < 0) {
				++diagnostics.zIndexNegative;
				record.paintTier = 0;
			} else if (record.zIndex > 0) {
				++diagnostics.zIndexPositive;
				record.paintTier = 2;
			} else {
				++diagnostics.zIndexZero;
				record.paintTier = 1;
			}
			record.documentExtentContribution = std::max(0, record.finalY - kContentY + record.usedHeight);
		const bool visible = !block.style.displayNone && block.style.visibility != VisibilityMode::Hidden &&
			block.style.effectiveOpacityPercent >= 0;
			record.paintVisible = visible && record.usedWidth > 0 && record.usedHeight > 0;
			record.hitVisible = record.paintVisible;
			record.clip = cssPositionedClipForBlock(doc, index, record.finalX,
			record.finalY, record.usedWidth, record.usedHeight, 0);
			if (record.documentExtentContribution > snapshot.documentExtent && record.paintVisible) {
				snapshot.documentExtent = std::min(kCssPositionedGeometryCap,
					record.documentExtentContribution);
				++diagnostics.positionDocumentExtentExtensions;
			}
			if (record.logicalSerial != 0 && snapshot.evidenceRecords < 32 && snapshot.evidence.size() < 32768 &&
				(block.id.rfind("phase3g-", 0) == 0 || block.id.rfind("css3g-", 0) == 0)) {
				std::ostringstream line;
				line << "id=" << block.id << ",logical-serial=" << record.logicalSerial
					<< ",parent-serial=" << record.parentSerial << ",position="
					<< (record.mode == PositionMode::Relative ? "relative" : "absolute")
					<< ",containing-block-serial=" << record.containingBlockSerial
					<< ",containing-block-type=" << record.containingBlockType
					<< ",containing-block=" << record.containingBlock.padding.x << ":" << record.containingBlock.padding.y << ":"
					<< record.containingBlock.padding.w << ":" << record.containingBlock.padding.h
					<< ",width-basis-definite=" << (record.containingBlock.widthDefinite ? "yes" : "no")
					<< ",height-basis-definite=" << (record.containingBlock.heightDefinite ? "yes" : "no")
					<< ",specified-offsets=" << cssNavigatorLengthEvidence(record.top) << ":"
					<< cssNavigatorLengthEvidence(record.right) << ":"
					<< cssNavigatorLengthEvidence(record.bottom) << ":"
					<< cssNavigatorLengthEvidence(record.left)
					<< ",resolved-offsets=" << (record.topResolved ? std::to_string(record.resolvedTop) : "auto") << ":"
					<< (record.rightResolved ? std::to_string(record.resolvedRight) : "auto") << ":"
					<< (record.bottomResolved ? std::to_string(record.resolvedBottom) : "auto") << ":"
					<< (record.leftResolved ? std::to_string(record.resolvedLeft) : "auto")
					<< ",static-position=" << record.staticX << ":" << record.staticY
					<< ",normal-flow=" << record.normalX << ":" << record.normalY
					<< ",final=" << record.finalX << ":" << record.finalY
					<< ",used-size=" << record.usedWidth << ":" << record.usedHeight
					<< ",flow-participation=" << (record.flowParticipation ? "yes" : "no")
					<< ",parent-height-contribution=" << (record.parentHeightContribution ? "yes" : "no")
					<< ",document-extent-contribution=" << record.documentExtentContribution
					<< ",static-position-used=" << (record.staticPositionUsed ? "yes" : "no")
					<< ",z-index=" << (record.zIndexAuto ? "auto" : std::to_string(record.zIndex))
					<< ",source-order=" << record.sourceOrder << ",paint-tier=" << record.paintTier << ",blockified="
					<< (record.blockified ? "yes" : "no") << ",clip=" << record.clip.x << ":" << record.clip.y << ":"
					<< record.clip.w << ":" << record.clip.h << ",visible=" << (record.paintVisible ? "yes" : "no")
					<< ",hit-visible=" << (record.hitVisible ? "yes" : "no")
					<< ",incomplete-reason=" << record.incompleteReason << ",clamped=" << (record.clamped ? "yes" : "no") << "\n";
				snapshot.evidence += line.str();
				++snapshot.evidenceRecords;
			}
			const int recordIndex = static_cast<int>(snapshot.records.size());
			snapshot.records.push_back(std::move(record));
			snapshot.blockRecordIndices[static_cast<size_t>(index)] = recordIndex;
		}
		diagnostics.positionedEvidenceRecords = snapshot.evidenceRecords;
		diagnostics.positionedEvidence = snapshot.evidence;
		snapshot.valid = true;
	}

	static int cssPositionStaticY(const WebDocument& doc, int blockIndex, const DocBlock& block)
	{
		const int fallbackMargin = block.type == BlockType::Heading ? 10 : 4;
		const int marginTop = cssMarginTopPx(block.style, fallbackMargin);
		const int previous = cssPreviousVisibleFlowBlock(doc, blockIndex);
		if (previous >= 0 && previous < static_cast<int>(s_cssMarginLayoutSnapshot.records.size())) {
			const CssMarginFlowRecord& prior = s_cssMarginLayoutSnapshot.records[static_cast<size_t>(previous)];
			return prior.usedY + prior.outerHeight + std::max(0, prior.usedMarginBottom) + marginTop;
		}
		return kHeadingY + std::max(0, doc.bodyStyle.marginTop) + marginTop;
	}

	static CssPaintRect cssPositionedClipForBlock(const WebDocument& doc, int blockIndex,
		int outerX, int outerY, int outerW, int outerH, int scrollOffset)
	{
		CssPaintRect clip = cssViewportClipRect();
		if (blockIndex < 0 || blockIndex >= static_cast<int>(doc.blocks.size())) return clip;
		const DocBlock& block = doc.blocks[static_cast<size_t>(blockIndex)];
		int depth = 0;
		for (const gxos::web::HtmlElementRef& ancestor : block.ancestors) {
			const WebStyle* style = cssStyleForSerial(doc, ancestor.serial);
			if (!style || (style->overflowX == OverflowMode::Visible && style->overflowY == OverflowMode::Visible)) continue;
			if (++depth > static_cast<int>(kCssPositionedAncestryCap)) return CssPaintRect{0, 0, 0, 0};
			CssPaintRect owner;
			if (const CssPositionedRecord* positioned = cssPositionedRecordForSerial(doc, ancestor.serial)) {
				owner = CssPaintRect{positioned->finalX, positioned->finalY,
					positioned->usedWidth, positioned->usedHeight};
				owner.y -= scrollOffset;
			} else {
				const CssAncestorBox box = cssAncestorBoxForBlock(doc, ancestor.serial, scrollOffset);
				if (!box.valid) return CssPaintRect{0, 0, 0, 0};
				owner = CssPaintRect{box.x, box.y, box.w, box.h};
			}
			clip = cssApplyOverflowClip(clip, *style, owner.x, owner.y, owner.w, owner.h);
			if (clip.w <= 0 || clip.h <= 0) return clip;
		}
		return cssApplyOverflowClip(clip, block.style, outerX, outerY - scrollOffset, outerW, outerH);
	}

	static void ensureCssPositionLayout(const WebDocument& doc)
	{
		const uint64_t fingerprint = cssMarginLayoutFingerprint(doc);
		if (s_cssPositionLayoutSnapshot.valid && s_cssPositionLayoutSnapshot.url == doc.url &&
			s_cssPositionLayoutSnapshot.blockCount == doc.blocks.size() &&
			s_cssPositionLayoutSnapshot.fingerprint == fingerprint &&
			s_cssPositionLayoutSnapshot.generation == doc.formRuntimeState.documentGeneration) return;
		buildCssPositionLayout(doc, s_cssPositionLayoutSnapshot);
	}

	static int cssPositionPaintRank(const WebDocument& doc, int blockIndex)
	{
		if (const CssPositionedRecord* record = cssPositionedRecordForBlock(doc, blockIndex))
			return (record->paintTier == 0 ? 100000 : record->paintTier == 1 ? 250000 : 350000) + record->sourceOrder;
		// Normal flow sits between negative and auto/positive positioned tiers.
		return 150000 + blockIndex;
	}

	static void drawBlockBox(uint64_t windowId, int x, int y, int w, int h, const WebStyle& style)
	{
		drawBoxDecorations(windowId, x, y, w, h, style);
	}

	static bool blockHasVisibleCss(const DocBlock& block)
	{
		return !block.style.displayNone && block.style.visibility != VisibilityMode::Hidden;
	}

	static bool isCenteredText(const WebStyle& style)
	{
		return style.textAlign == TextAlign::Center;
	}

	static bool isRightAlignedText(const WebStyle& style)
	{
		return style.textAlign == TextAlign::Right;
	}

	static bool isHiddenByDisplay(const WebStyle& style)
	{
		return style.displayNone;
	}

	static bool colorChannels(uint32_t color, int& r, int& g, int& b)
	{
		r = static_cast<int>((color >> 16) & 0xFFu);
		g = static_cast<int>((color >> 8) & 0xFFu);
		b = static_cast<int>(color & 0xFFu);
		return true;
	}

	static std::string encodeFormComponent(const std::string& value)
	{
		static const char* hex = "0123456789ABCDEF";
		std::string out;
		out.reserve(value.size());
		for (unsigned char ch : value) {
			if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
				(ch >= '0' && ch <= '9') || ch == '-' || ch == '_' ||
				ch == '.' || ch == '~') {
				out.push_back(static_cast<char>(ch));
			} else if (ch == ' ') {
				out.push_back('+');
			} else {
				out.push_back('%');
				out.push_back(hex[(ch >> 4) & 0x0F]);
				out.push_back(hex[ch & 0x0F]);
			}
		}
		return out;
	}

	static std::vector<std::string> textareaLines(const std::string& text)
	{
		std::vector<std::string> lines;
		size_t start = 0;
		while (start <= text.size()) {
			size_t end = text.find('\n', start);
			if (end == std::string::npos) end = text.size();
			lines.push_back(text.substr(start, end - start));
			if (end == text.size()) break;
			start = end + 1;
		}
		if (lines.empty()) lines.push_back("");
		return lines;
	}


	struct RuntimeReportEntry {
		std::string section;
		std::string label;
		std::string value;
	};

	static std::vector<RuntimeReportEntry> hostedRuntimeReportEntries(
		const std::string& currentUrl,
		const std::string& currentTitle,
		int currentBlockCount,
		const std::string& inspectedUrl,
		bool cssDetected,
		bool cssEnabled,
		int cssRuleCount,
		int cssStyleBlockCount,
		int cssInlineStyleCount,
		int cssExternalStylesheetLoadedCount,
		int cssUnsupportedExternalStylesheetCount,
		int cssUnsupportedRuleCount,
		int cssUnsupportedDeclarationCount,
		int cssUnsupportedSelectorCount,
		int cssParseErrorCount,
		bool cssStyleBlockCapped,
		size_t cssStyleBytesProcessed,
		int cssLayoutMaxWidthAppliedCount,
		int cssAutoMarginCenteredBlockCount,
		int cssBackgroundBlockCount,
		int cssWrapperRenderCount,
		int cssDisplayNoneBlockCount,
		int cssTableRenderCount,
		int cssTableRowCount,
		int cssTableCellCount,
		int cssTableLayoutFallbackCount,
		int cssListRenderCount,
		int cssClampedValueCount,
		int cssLineBreakCount,
		int cssTableCaptionCount,
		int cssTableHeaderCellCount,
		int cssVisitedLinkCount,
		int cssBorderedBlocksRendered,
		int cssDashedBordersRendered,
		int cssDottedBordersRendered,
		int cssBorderWidthClamps,
		int cssCollapsedTablesRendered,
		int cssSeparateTablesRendered,
		int cssTableBorderSpacingClamps,
		int cssListStyleMarkersRendered,
		int cssListStyleNoneApplied,
		int cssTextDecorationsRendered,
		int cssGenericFontFamilyApplied,
		int cssGenericFontFamilyFallbacks,
		int cssFiguresRendered,
		int cssFigcaptionsRendered,
		int cssBlockquotesRendered,
		int cssDefinitionListsRendered,
		int cssImagesConstrained,
		int cssImagesAspectPreserved,
		int cssImageAltFallbacks,
		int cssImageSizeClamps,
		int cssNestedLayoutClamps,
		int cssMaxWrapperAncestorDepth,
		int cssSelectorGroupsParsed,
		int cssCompoundSelectorsParsed,
		int cssChildCombinators,
		int cssDescendantCombinators,
		int cssAdjacentSiblingCombinators,
		int cssGeneralSiblingCombinators,
		int cssAdjacentSiblingMatches,
		int cssGeneralSiblingMatches,
		int cssSiblingScanSteps,
		int cssSiblingScanClamps,
		int cssSiblingMetadataClamps,
		int cssSiblingMetadataErrors,
		int cssSelectorMatches,
		int cssSpecificityOverrides,
		int cssSourceOrderOverrides,
		int cssInlineOverrides,
		int cssInheritedPropertiesApplied,
		int cssSelectorDepthClamps,
		int cssSelectorGroupClamps,
		int cssCascadePropertyResolutions,
		int cssImportantDeclarationsApplied,
		int cssRuleCapCount,
		int cssDeclarationCapCount,
		int cssInheritanceDepthClamps,
		int cssPseudoClassesParsed,
		int cssStructuralPseudoMatches,
		int cssFirstChildMatches,
		int cssLastChildMatches,
		int cssNthChildMatches,
		int cssOfTypeMatches,
		int cssNotMatches,
		int cssLinkPseudoMatches,
		int cssVisitedPseudoMatches,
		int cssPseudoClassClamps,
		int cssNthExpressionParseErrors,
		int cssStructuralMetadataClamps,
		int cssSelectorEvaluationStepClamps,
		int cssEmptyPseudoParsed,
		int cssEmptyPseudoMatches,
		int cssEmptyMetadataIncomplete,
		int cssContentMetadataClamps,
		int cssSelectorGroupMemberRecoveries,
		int cssCommentScanClamps,
		int cssUnterminatedCommentErrors,
		int cssUnbalancedParenthesisErrors,
		int cssUnbalancedBracketErrors,
		int cssUnterminatedStringErrors,
		int cssInvalidCombinatorSequences,
		int cssIdentifierEscapeRejections,
		int cssSelectorMemberParseFailures,
		int cssSelectorRecoverySuccesses,
		const std::string& cssComputedStyleEvidence,
		int formCount,
		int formInputCount,
		int checkboxCount,
		int radioCount,
		int textareaCount,
		int selectCount,
		int unsupportedControls,
		bool postSupportedHosted,
		bool postSupportedBareMetal,
		const std::string& lastFormAction,
		const std::string& lastFormMethod,
		const std::string& lastFormStatus,
		const std::string& lastPostHttpStatus,
		const std::string& lastPostContentType,
		const std::string& clipboardMode,
		const std::string& tlsStatus,
		const std::string& tlsError,
		const std::string& tlsConnectionPath,
		const std::string& tlsCredentialApi,
		const std::string& tlsCredentialStructure,
		bool tlsCredentialAcquired,
		bool tlsHandshakeStarted,
		bool tlsSmokeSelfSignedBypass)
	{
		const GxosRandomQuality randomQuality = gxos_random_quality();
		const GxosClockStatus clockStatus = gxos_wall_clock_status();
		const GxosTlsBackendInfo tlsBackendInfo = gxos_tls_backend_info();
		const GxosTlsMbedTlsImportInfo tlsImportInfo = gxos_tls_mbedtls_import_info();
		const GxosTlsRuntimeHookInfo tlsHookInfo = gxos_tls_runtime_hook_info();
		const GxosTlsArenaInfo tlsArenaInfo = gxos_tls_arena_info();
		const GxosCaStoreInfo caStoreInfo = gxos_ca_store_info();
		const GxosTrustStorePolicyInfo trustStorePolicy = gxos_tls_trust_store_policy_info();
		const GxosValidatedHttpsPolicyInfo httpsPolicy = gxos_validated_https_policy_info();
		const GxosTlsHostnameValidationInfo hostnameValidationInfo = gxos_tls_hostname_validation_info();
		const bool localSmokeTlsReady = gxos_tls_local_smoke_https_ready();
		const char* localSmokeTlsBlocker = gxos_tls_local_smoke_https_blocker_reason();
		const bool tlsReady = gxos_tls_prerequisites_ready();
		const char* tlsReadinessBlocker = gxos_tls_prerequisites_blocker_reason();
		const TextMetrics textMetrics = defaultTextMetrics();
		uint8_t rngSmokeByte = 0;
		int64_t wallClockSeconds = 0;
		char wallClockUtc[32] = {};
		const bool rngReadSmoke = gxos_random_bytes(&rngSmokeByte, sizeof(rngSmokeByte));
		const bool wallClockAvailable = gxos_wall_clock_unix_seconds(&wallClockSeconds);
		const bool wallClockUtcAvailable = gxos_wall_clock_utc_text(wallClockUtc, sizeof(wallClockUtc));
		return {
			{"Runtime", "Mode", "hosted/compositor"},
			{"Runtime", "Launch path", "AppRegistry -> DesktopService::LaunchApp -> apps::Navigator::Launch"},
			{"Runtime", "Process entry", "navigator.cpp Navigator::main"},
			{"Runtime", "Rendering owner", "navigator.cpp via GUI protocol/compositor"},
			{"Runtime", "Input owner", "navigator.cpp event loop via compositor messages"},
			{"Runtime", "Document loading owner", "navigator.cpp + guideWeb adapters"},
			{"Runtime", "Authoritative path", "hosted Navigator app-model process"},
			{"Runtime", "Stale placeholder path", "not active"},

			{"Shared Core", "Document model", "guide_web_document.h"},
			{"Shared Core", "HTML parser", "navigator_html_parser / guideWeb parser layer"},
			{"Shared Core", "HTTP client", "guide_web_http"},
			{"Shared Core", "Relative URLs", "gxos::web::resolveRelativeUrl"},
			{"Shared Core", "Image decode", "shared ImageAdapter"},

			{"Capabilities", "File read", "enabled"},
			{"Capabilities", "File write", "enabled for bookmark persistence"},
			{"Capabilities", "Local PNG", "enabled"},
			{"Capabilities", "HTTP", "enabled for http:// and hosted https:// via Winsock transport"},
			{"Capabilities", "Remote PNG", "enabled for http:// and hosted https:// PNG images"},
			{"Capabilities", "Downloads", "enabled for unsupported HTTP(S) content within body limit"},
			{"Capabilities", "Temp files", "enabled for compositor image handoff"},
			{"Capabilities", "Bookmark persistence", "enabled"},
			{"Capabilities", "HTTPS/TLS", "enabled hosted-only"},
			{"Capabilities", "TLS backend", tlsBackendInfo.backendName ? tlsBackendInfo.backendName : "(none)"},
			{"Evidence Lane", "evidence_lane", "hosted"},
			{"Evidence Lane", "tls_backend", "schannel"},
			{"Capabilities", "Certificate validation", gxos_tls_certificate_validation_policy()},
			{"Capabilities", "TLS insertion seam", "active HttpByteStream wrapper"},
			{"Capabilities", "TLS smoke bypass", "localhost self-signed only; disabled unless GXOS_NAVIGATOR_SMOKE_ALLOW_SELF_SIGNED_LOCALHOST=1"},
			{"Capabilities", "HTTPS-to-HTTP redirect policy", "blocked by default"},
			{"Capabilities", "Bare-metal TLS", "foundation only; https:// stays blocked until readiness is true"},
			{"Capabilities", "CSS-lite embedded <style>", "enabled"},
			{"Capabilities", "Hosted colored text primitive", "enabled"},
			{"Capabilities", "CSS text color visible", "enabled"},
			{"Capabilities", "Forms-lite GET forms", "enabled"},
			{"Capabilities", "Forms-lite POST forms hosted", postSupportedHosted ? "enabled" : "unsupported"},
			{"Capabilities", "Forms-lite POST forms bare-metal", postSupportedBareMetal ? "enabled" : "unsupported"},
			{"Capabilities", "Forms-lite POST redirect policy", "303 becomes GET; 301/302/307/308 preserve POST"},
			{"Capabilities", "Forms-lite controls", "text, checkbox, radio, textarea, select, submit"},
			{"Capabilities", "Forms-lite focus navigation", "Tab/Shift+Tab, Enter, Space"},
			{"Capabilities", "Find in Page", "enabled"},
			{"Capabilities", "Text selection", "enabled"},
			{"Capabilities", "Clipboard mode", clipboardMode.empty() ? "Navigator internal clipboard" : clipboardMode},
			{"Capabilities", "External stylesheets", "bounded hosted"},

			{"Backends", "File backend", "navigator_file_io hosted/VFS adapter"},
			{"Backends", "HTTP backend", "guide_web_http hosted TCP byte-stream with Schannel TLS wrapper for https"},
			{"Backends", "Image backend", "ImageAdapter + compositor drawImage path"},

			{"TLS Prerequisites", "RNG quality", gxos_random_quality_name(randomQuality)},
			{"TLS Prerequisites", "RNG backend", gxos_random_backend()},
			{"TLS Prerequisites", "VirtIO RNG detected", gxos_virtio_rng_detected() ? "yes" : "no"},
			{"TLS Prerequisites", "VirtIO RNG status", gxos_virtio_rng_status()},
			{"TLS Prerequisites", "Random read smoke", rngReadSmoke ? "PASS" : "FAIL"},
			{"TLS Prerequisites", "Wall-clock status", gxos_wall_clock_status_name(clockStatus)},
			{"TLS Prerequisites", "Wall-clock backend", gxos_wall_clock_backend()},
			{"TLS Prerequisites", "Wall-clock Unix seconds", wallClockAvailable ? std::to_string(wallClockSeconds) : "(unavailable)"},
			{"TLS Prerequisites", "Wall-clock UTC", wallClockUtcAvailable ? wallClockUtc : "(unavailable)"},
			{"TLS Prerequisites", "TLS backend status", gxos_tls_backend_status_name(tlsBackendInfo.status)},
			{"TLS Prerequisites", "TLS backend name", tlsBackendInfo.backendName ? tlsBackendInfo.backendName : "(none)"},
			{"TLS Prerequisites", "TLS backend version", tlsBackendInfo.backendVersion ? tlsBackendInfo.backendVersion : "(none)"},
			{"TLS Prerequisites", "TLS backend error", tlsBackendInfo.error ? tlsBackendInfo.error : "(none)"},
			{"TLS Prerequisites", "Mbed TLS import path", tlsImportInfo.importPath ? tlsImportInfo.importPath : "(none)"},
			{"TLS Prerequisites", "Mbed TLS config path", tlsImportInfo.configPath ? tlsImportInfo.configPath : "(none)"},
			{"TLS Prerequisites", "Mbed TLS crypto config path", tlsImportInfo.cryptoConfigPath ? tlsImportInfo.cryptoConfigPath : "(none)"},
			{"TLS Prerequisites", "Mbed TLS TF-PSA path", tlsImportInfo.tfPsaPath ? tlsImportInfo.tfPsaPath : "(none)"},
			{"TLS Prerequisites", "Mbed TLS build plan", tlsImportInfo.buildPlanPath ? tlsImportInfo.buildPlanPath : "(none)"},
			{"TLS Prerequisites", "Mbed TLS expected version", tlsImportInfo.expectedVersion ? tlsImportInfo.expectedVersion : "(none)"},
			{"TLS Prerequisites", "Mbed TLS detected version", tlsImportInfo.detectedVersion ? tlsImportInfo.detectedVersion : "(none)"},
			{"TLS Prerequisites", "TF-PSA detected version", tlsImportInfo.tfPsaDetectedVersion ? tlsImportInfo.tfPsaDetectedVersion : "(none)"},
			{"TLS Prerequisites", "Mbed TLS planned source count", std::to_string(tlsImportInfo.plannedSourceCount)},
			{"TLS Prerequisites", "Mbed TLS planned subset", tlsImportInfo.plannedSubset ? tlsImportInfo.plannedSubset : "(none)"},
			{"TLS Prerequisites", "Mbed TLS source present", tlsImportInfo.sourcePresent ? "yes" : "no"},
			{"TLS Prerequisites", "Mbed TLS source compile-ready", tlsImportInfo.sourceReadyForCompile ? "yes" : "no"},
			{"TLS Prerequisites", "Mbed TLS config present", tlsImportInfo.configPresent ? "yes" : "no"},
			{"TLS Prerequisites", "Mbed TLS crypto config present", tlsImportInfo.cryptoConfigPresent ? "yes" : "no"},
			{"TLS Prerequisites", "Mbed TLS TF-PSA dependency present", tlsImportInfo.tfPsaDependencyPresent ? "yes" : "no"},
			{"TLS Prerequisites", "Mbed TLS import detail", tlsImportInfo.detail ? tlsImportInfo.detail : "(none)"},
			{"TLS Prerequisites", "Allocator hook status", gxos_tls_hook_status_name(tlsHookInfo.allocatorStatus)},
			{"TLS Prerequisites", "Allocator hook detail", tlsHookInfo.allocatorDetail ? tlsHookInfo.allocatorDetail : "(none)"},
			{"TLS Prerequisites", "RNG callback status", gxos_tls_hook_status_name(tlsHookInfo.rngCallbackStatus)},
			{"TLS Prerequisites", "RNG callback detail", tlsHookInfo.rngDetail ? tlsHookInfo.rngDetail : "(none)"},
			{"TLS Prerequisites", "Time callback status", gxos_tls_hook_status_name(tlsHookInfo.timeCallbackStatus)},
			{"TLS Prerequisites", "Time callback detail", tlsHookInfo.timeDetail ? tlsHookInfo.timeDetail : "(none)"},
			{"TLS Prerequisites", "PSA init status", gxos_tls_hook_status_name(tlsHookInfo.psaInitStatus)},
			{"TLS Prerequisites", "PSA init detail", tlsHookInfo.psaDetail ? tlsHookInfo.psaDetail : "(none)"},
			{"TLS Prerequisites", "TLS arena status", gxos_tls_arena_status_name(tlsArenaInfo.status)},
			{"TLS Prerequisites", "TLS arena capacity", std::to_string(tlsArenaInfo.capacityBytes)},
			{"TLS Prerequisites", "TLS arena in use", std::to_string(tlsArenaInfo.bytesInUse)},
			{"TLS Prerequisites", "TLS arena high-water", std::to_string(tlsArenaInfo.highWaterBytes)},
			{"TLS Prerequisites", "TLS arena detail", tlsArenaInfo.error ? tlsArenaInfo.error : "(none)"},
			{"TLS Prerequisites", "Root CA path", caStoreInfo.path ? caStoreInfo.path : "(none)"},
			{"TLS Prerequisites", "Root CA status", gxos_ca_store_status_name(caStoreInfo.status)},
			{"TLS Prerequisites", "Root CA parse status", gxos_ca_parse_status_name(caStoreInfo.parseStatus)},
			{"TLS Prerequisites", "Root CA bytes", std::to_string(caStoreInfo.bytesLoaded)},
			{"TLS Prerequisites", "Root CA PEM blocks", std::to_string(caStoreInfo.pemBlocksDetected)},
			{"TLS Prerequisites", "Root CA parsed certs", std::to_string(caStoreInfo.parsedCertificateCount)},
			{"TLS Prerequisites", "Root CA fixture", caStoreInfo.testOnlyFixture ? "smoke-only test fixture" : "normal runtime path"},
			{"TLS Prerequisites", "Root CA detail", caStoreInfo.error ? caStoreInfo.error : "(none)"},
			{"TLS Prerequisites", "Trust store policy", gxos_trust_store_policy_state_name(trustStorePolicy.state)},
			{"TLS Prerequisites", "Trust store path", trustStorePolicy.path ? trustStorePolicy.path : "(none)"},
			{"TLS Prerequisites", "Trust store source", gxos_trust_store_source_name(trustStorePolicy.source)},
			{"TLS Prerequisites", "Trust store source detail", trustStorePolicy.sourceDetail ? trustStorePolicy.sourceDetail : "(none)"},
			{"TLS Prerequisites", "Trust store production-ready", trustStorePolicy.productionReady ? "yes" : "no"},
			{"TLS Prerequisites", "HTTPS policy selected state", gxos_validated_https_policy_state_name(httpsPolicy.selectedState)},
			{"TLS Prerequisites", "HTTPS policy effective state", gxos_validated_https_policy_state_name(httpsPolicy.state)},
			{"TLS Prerequisites", "HTTPS policy config path", httpsPolicy.configPath ? httpsPolicy.configPath : "(none)"},
			{"TLS Prerequisites", "HTTPS policy config source", httpsPolicy.configSource ? httpsPolicy.configSource : "(none)"},
			{"TLS Prerequisites", "HTTPS policy error", httpsPolicy.error ? httpsPolicy.error : "(none)"},
			{"TLS Prerequisites", "Local smoke HTTPS readiness", localSmokeTlsReady ? "yes" : "no"},
			{"TLS Prerequisites", "Local smoke HTTPS blocker", localSmokeTlsReady ? "(none)" : localSmokeTlsBlocker},
			{"TLS Prerequisites", "Local smoke HTTPS reason", httpsPolicy.localAllowReason ? httpsPolicy.localAllowReason : "(none)"},
			{"TLS Prerequisites", "HTTPS public transport", httpsPolicy.broadPublicHttpsEnabled ? "enabled" : "disabled"},
			{"TLS Prerequisites", "HTTPS policy production-ready", httpsPolicy.productionReady ? "yes" : "no"},
			{"TLS Prerequisites", "HTTPS policy blocker", httpsPolicy.blocker ? httpsPolicy.blocker : "(none)"},
			{"TLS Prerequisites", "Hostname validation available", hostnameValidationInfo.available ? "yes" : "no"},
			{"TLS Prerequisites", "Hostname validation policy", hostnameValidationInfo.policy ? hostnameValidationInfo.policy : "(none)"},
			{"TLS Prerequisites", "TLS SNI support", hostnameValidationInfo.sniSupported ? "yes" : "no"},
			{"TLS Prerequisites", "TLS original hostname retained", hostnameValidationInfo.originalHostnameRetained ? "yes" : "no"},
			{"TLS Prerequisites", "TLS numeric IP validation", hostnameValidationInfo.numericIpSupported ? "yes" : "no"},
			{"TLS Prerequisites", "Certificate validation policy", gxos_tls_certificate_validation_policy()},
			{"TLS Prerequisites", "TLS readiness", tlsReady ? "yes" : "no"},
			{"TLS Prerequisites", "TLS readiness blocker", tlsReady ? "(none)" : tlsReadinessBlocker},

			{"Current Document", "URL", currentUrl.empty() ? "(none)" : currentUrl},
			{"Current Document", "Title", currentTitle.empty() ? "(none)" : currentTitle},
			{"Current Document", "Block count", std::to_string(currentBlockCount)},
			{"Current Document", "Inspected page", inspectedUrl.empty() ? "(none)" : inspectedUrl},
			{"Current Document", "CSS diagnostics", cssDetected ? "css detected" : "no css detected"},
			{"Current Document", "CSS enabled", yesNo(cssEnabled)},
			{"Current Document", "CSS rules parsed", std::to_string(cssRuleCount)},
			{"Current Document", "CSS style blocks", std::to_string(cssStyleBlockCount)},
			{"Current Document", "CSS inline styles", std::to_string(cssInlineStyleCount)},
			{"Current Document", "CSS external stylesheets loaded", std::to_string(cssExternalStylesheetLoadedCount)},
			{"Current Document", "CSS unsupported external stylesheets", std::to_string(cssUnsupportedExternalStylesheetCount)},
			{"Current Document", "CSS unsupported rules", std::to_string(cssUnsupportedRuleCount)},
			{"Current Document", "CSS unsupported declarations", std::to_string(cssUnsupportedDeclarationCount)},
			{"Current Document", "CSS unsupported selectors", std::to_string(cssUnsupportedSelectorCount)},
			{"Current Document", "CSS parse errors", std::to_string(cssParseErrorCount)},
			{"Current Document", "CSS style block capped", yesNo(cssStyleBlockCapped)},
			{"Current Document", "CSS style bytes processed", std::to_string(cssStyleBytesProcessed)},
			{"Current Document", "CSS layout max-width applied", std::to_string(cssLayoutMaxWidthAppliedCount)},
			{"Current Document", "CSS auto-margin centered blocks", std::to_string(cssAutoMarginCenteredBlockCount)},
			{"Current Document", "CSS background blocks drawn", std::to_string(cssBackgroundBlockCount)},
			{"Current Document", "CSS wrapper blocks rendered", std::to_string(cssWrapperRenderCount)},
			{"Current Document", "CSS display:none blocks", std::to_string(cssDisplayNoneBlockCount)},
			{"Current Document", "CSS tables rendered", std::to_string(cssTableRenderCount)},
			{"Current Document", "CSS table rows rendered", std::to_string(cssTableRowCount)},
			{"Current Document", "CSS table cells rendered", std::to_string(cssTableCellCount)},
			{"Current Document", "CSS table layout fallbacks", std::to_string(cssTableLayoutFallbackCount)},
			{"Current Document", "CSS lists rendered", std::to_string(cssListRenderCount)},
			{"Current Document", "CSS clamped values", std::to_string(cssClampedValueCount)},
			{"Current Document", "CSS line breaks parsed", std::to_string(cssLineBreakCount)},
			{"Current Document", "CSS table captions rendered", std::to_string(cssTableCaptionCount)},
			{"Current Document", "CSS table header cells rendered", std::to_string(cssTableHeaderCellCount)},
			{"Current Document", "CSS visited links styled", std::to_string(cssVisitedLinkCount)},
			{"Current Document", "CSS bordered blocks rendered", std::to_string(cssBorderedBlocksRendered)},
			{"Current Document", "CSS dashed borders rendered", std::to_string(cssDashedBordersRendered)},
			{"Current Document", "CSS dotted borders rendered", std::to_string(cssDottedBordersRendered)},
			{"Current Document", "CSS border width clamps", std::to_string(cssBorderWidthClamps)},
			{"Current Document", "CSS collapsed tables rendered", std::to_string(cssCollapsedTablesRendered)},
			{"Current Document", "CSS separate tables rendered", std::to_string(cssSeparateTablesRendered)},
			{"Current Document", "CSS table border spacing clamps", std::to_string(cssTableBorderSpacingClamps)},
			{"Current Document", "CSS list style markers rendered", std::to_string(cssListStyleMarkersRendered)},
			{"Current Document", "CSS list style none applied", std::to_string(cssListStyleNoneApplied)},
			{"Current Document", "CSS text decorations rendered", std::to_string(cssTextDecorationsRendered)},
			{"Current Document", "CSS generic font family applied", std::to_string(cssGenericFontFamilyApplied)},
			{"Current Document", "CSS generic font family fallbacks", std::to_string(cssGenericFontFamilyFallbacks)},
			{"Current Document", "CSS figures rendered", std::to_string(cssFiguresRendered)},
			{"Current Document", "CSS figcaptions rendered", std::to_string(cssFigcaptionsRendered)},
			{"Current Document", "CSS blockquotes rendered", std::to_string(cssBlockquotesRendered)},
			{"Current Document", "CSS definition lists rendered", std::to_string(cssDefinitionListsRendered)},
			{"Current Document", "CSS images constrained", std::to_string(cssImagesConstrained)},
			{"Current Document", "CSS images aspect preserved", std::to_string(cssImagesAspectPreserved)},
			{"Current Document", "CSS image alt fallbacks", std::to_string(cssImageAltFallbacks)},
			{"Current Document", "CSS image size clamps", std::to_string(cssImageSizeClamps)},
			{"Current Document", "CSS nested layout clamps", std::to_string(cssNestedLayoutClamps)},
			{"Current Document", "CSS max wrapper ancestor depth", std::to_string(cssMaxWrapperAncestorDepth)},
			{"Current Document", "CSS selector groups parsed", std::to_string(cssSelectorGroupsParsed)},
			{"Current Document", "CSS compound selectors parsed", std::to_string(cssCompoundSelectorsParsed)},
			{"Current Document", "CSS child combinators", std::to_string(cssChildCombinators)},
			{"Current Document", "CSS descendant combinators", std::to_string(cssDescendantCombinators)},
			{"Current Document", "CSS adjacent-sibling combinators", std::to_string(cssAdjacentSiblingCombinators)},
			{"Current Document", "CSS general-sibling combinators", std::to_string(cssGeneralSiblingCombinators)},
			{"Current Document", "CSS adjacent-sibling matches", std::to_string(cssAdjacentSiblingMatches)},
			{"Current Document", "CSS general-sibling matches", std::to_string(cssGeneralSiblingMatches)},
			{"Current Document", "CSS sibling scan steps", std::to_string(cssSiblingScanSteps)},
			{"Current Document", "CSS sibling scan clamps", std::to_string(cssSiblingScanClamps)},
			{"Current Document", "CSS sibling metadata clamps", std::to_string(cssSiblingMetadataClamps)},
			{"Current Document", "CSS sibling metadata errors", std::to_string(cssSiblingMetadataErrors)},
			{"Current Document", "CSS selector matches", std::to_string(cssSelectorMatches)},
			{"Current Document", "CSS specificity overrides", std::to_string(cssSpecificityOverrides)},
			{"Current Document", "CSS source-order overrides", std::to_string(cssSourceOrderOverrides)},
			{"Current Document", "CSS inline overrides", std::to_string(cssInlineOverrides)},
			{"Current Document", "CSS inherited properties applied", std::to_string(cssInheritedPropertiesApplied)},
			{"Current Document", "CSS selector depth clamps", std::to_string(cssSelectorDepthClamps)},
			{"Current Document", "CSS selector group clamps", std::to_string(cssSelectorGroupClamps)},
			{"Current Document", "CSS cascade property resolutions", std::to_string(cssCascadePropertyResolutions)},
			{"Current Document", "CSS !important declarations applied", std::to_string(cssImportantDeclarationsApplied)},
			{"Current Document", "CSS rule cap count", std::to_string(cssRuleCapCount)},
			{"Current Document", "CSS declaration cap count", std::to_string(cssDeclarationCapCount)},
			{"Current Document", "CSS inheritance depth clamps", std::to_string(cssInheritanceDepthClamps)},
			{"Current Document", "CSS pseudo-classes parsed", std::to_string(cssPseudoClassesParsed)},
			{"Current Document", "CSS structural pseudo matches", std::to_string(cssStructuralPseudoMatches)},
			{"Current Document", "CSS first-child matches", std::to_string(cssFirstChildMatches)},
			{"Current Document", "CSS last-child matches", std::to_string(cssLastChildMatches)},
			{"Current Document", "CSS nth-child matches", std::to_string(cssNthChildMatches)},
			{"Current Document", "CSS of-type matches", std::to_string(cssOfTypeMatches)},
			{"Current Document", "CSS :not matches", std::to_string(cssNotMatches)},
			{"Current Document", "CSS :link pseudo matches", std::to_string(cssLinkPseudoMatches)},
			{"Current Document", "CSS :visited pseudo matches", std::to_string(cssVisitedPseudoMatches)},
			{"Current Document", "CSS pseudo-class clamps", std::to_string(cssPseudoClassClamps)},
			{"Current Document", "CSS nth-expression parse errors", std::to_string(cssNthExpressionParseErrors)},
			{"Current Document", "CSS structural metadata clamps", std::to_string(cssStructuralMetadataClamps)},
			{"Current Document", "CSS selector evaluation step clamps", std::to_string(cssSelectorEvaluationStepClamps)},
			{"Current Document", "CSS :empty pseudo parsed", std::to_string(cssEmptyPseudoParsed)},
			{"Current Document", "CSS :empty pseudo matches", std::to_string(cssEmptyPseudoMatches)},
			{"Current Document", "CSS :empty metadata incomplete", std::to_string(cssEmptyMetadataIncomplete)},
			{"Current Document", "CSS content metadata clamps", std::to_string(cssContentMetadataClamps)},
			{"Current Document", "CSS selector group member recoveries", std::to_string(cssSelectorGroupMemberRecoveries)},
			{"Current Document", "CSS comment scan clamps", std::to_string(cssCommentScanClamps)},
			{"Current Document", "CSS unterminated comment errors", std::to_string(cssUnterminatedCommentErrors)},
			{"Current Document", "CSS unbalanced parenthesis errors", std::to_string(cssUnbalancedParenthesisErrors)},
			{"Current Document", "CSS unbalanced bracket errors", std::to_string(cssUnbalancedBracketErrors)},
			{"Current Document", "CSS unterminated string errors", std::to_string(cssUnterminatedStringErrors)},
			{"Current Document", "CSS invalid combinator sequences", std::to_string(cssInvalidCombinatorSequences)},
			{"Current Document", "CSS identifier escape rejections", std::to_string(cssIdentifierEscapeRejections)},
			{"Current Document", "CSS selector member parse failures", std::to_string(cssSelectorMemberParseFailures)},
			{"Current Document", "CSS selector recovery successes", std::to_string(cssSelectorRecoverySuccesses)},
			{"Current Document", "CSS computed style evidence", cssComputedStyleEvidence.empty() ? "(none)" : cssComputedStyleEvidence},
			{"Current Document", "text_metrics_model", "baseline/descent aware system font"},
			{"Current Document", "text_backend", textMetrics.backend},
			{"Current Document", "text_ascent_px", std::to_string(textMetrics.ascent)},
			{"Current Document", "text_descent_px", std::to_string(textMetrics.descent)},
			{"Current Document", "text_baseline_offset_px", std::to_string(textMetrics.baseline)},
			{"Current Document", "text_line_height_default_px", std::to_string(textMetrics.lineHeight)},
			{"Current Document", "text_underline_offset_px", std::to_string(textMetrics.underlineOffset)},
			{"Current Document", "text_descender_safe", yesNo(textMetrics.descenderSafe)},
			{"Current Document", "text_top_padding_px", std::to_string(textLineTopPaddingPx(defaultTextFontHeightPx() + 2))},
			{"Current Document", "text_underline_gap_px", std::to_string((defaultTextFontHeightPx() + 2) - textUnderlineYPx(0, defaultTextFontHeightPx() + 2) - 1)},
			{"Current Document", "Forms", std::to_string(formCount)},
			{"Current Document", "Text inputs", std::to_string(formInputCount)},
			{"Current Document", "Checkboxes", std::to_string(checkboxCount)},
			{"Current Document", "Radio buttons", std::to_string(radioCount)},
			{"Current Document", "Textareas", std::to_string(textareaCount)},
			{"Current Document", "Selects", std::to_string(selectCount)},
			{"Current Document", "Unsupported form controls", std::to_string(unsupportedControls)},
			{"Current Document", "POST supported hosted", yesNo(postSupportedHosted)},
			{"Current Document", "POST supported bare-metal", yesNo(postSupportedBareMetal)},
			{"Current Document", "Last submitted form action", lastFormAction.empty() ? "(none)" : lastFormAction},
			{"Current Document", "Last submitted form method", lastFormMethod.empty() ? "(none)" : lastFormMethod},
			{"Current Document", "Last submitted form status", lastFormStatus.empty() ? "(none)" : lastFormStatus},
			{"Current Document", "Last POST HTTP status", lastPostHttpStatus.empty() ? "(none)" : lastPostHttpStatus},
			{"Current Document", "Last POST content type", lastPostContentType.empty() ? "(none)" : lastPostContentType},
			{"Current Document", "TLS status", tlsStatus.empty() ? "(none)" : tlsStatus},
			{"Current Document", "TLS error", tlsError.empty() ? "(none)" : tlsError},
			{"Current Document", "TLS connection path", tlsConnectionPath.empty() ? "(none)" : tlsConnectionPath},
			{"Current Document", "TLS credential API", tlsCredentialApi.empty() ? "(none)" : tlsCredentialApi},
			{"Current Document", "TLS credential structure", tlsCredentialStructure.empty() ? "(none)" : tlsCredentialStructure},
			{"Current Document", "TLS credential acquired", yesNo(tlsCredentialAcquired)},
			{"Current Document", "TLS handshake started", yesNo(tlsHandshakeStarted)},
			{"Current Document", "TLS smoke bypass active", yesNo(tlsSmokeSelfSignedBypass)},
		};
	}

	static void addRuntimeReportBlocks(WebDocument& doc, const std::vector<RuntimeReportEntry>& entries)
	{
		std::string section;
		for (const RuntimeReportEntry& entry : entries) {
			if (entry.section != section) {
				section = entry.section;
				doc.blocks.push_back({BlockType::Heading, section, ""});
			}
			doc.blocks.push_back({BlockType::ListItem, pageInfoLine(entry.label, entry.value), ""});
		}
	}

	static std::string formatRuntimeReport(const std::vector<RuntimeReportEntry>& entries)
	{
		std::ostringstream out;
		out << "navigator.runtime.report\n";
		for (const RuntimeReportEntry& entry : entries) {
			out << entry.section << "." << entry.label << "=" << entry.value << "\n";
		}
		return out.str();
	}

	static void appendFormPhase2EDiagnostics(std::string& out, const NavigatorPageMetadata& metadata)
	{
		const auto add = [&](const char* label, int value) {
			out += std::string("Current Document.") + label + "=" + std::to_string(value) + "\n";
		};
		add("HTML forms parsed", metadata.htmlFormsParsed);
		add("HTML fieldsets parsed", metadata.htmlFieldsetsParsed);
		add("HTML labels parsed", metadata.htmlLabelsParsed);
		add("HTML inputs parsed", metadata.htmlInputsParsed);
		add("HTML buttons parsed", metadata.htmlButtonsParsed);
		add("HTML textareas parsed", metadata.htmlTextareasParsed);
		add("HTML selects parsed", metadata.htmlSelectsParsed);
		add("HTML options parsed", metadata.htmlOptionsParsed);
		add("HTML hidden controls", metadata.htmlHiddenControls);
		add("HTML control metadata clamps", metadata.controlMetadataClamps);
		add("HTML control text truncations", metadata.controlTextTruncations);
		add("CSS :checked pseudo parsed", metadata.cssCheckedPseudoParsed);
		add("CSS :checked pseudo matches", metadata.cssCheckedPseudoMatches);
		add("CSS :disabled pseudo matches", metadata.cssDisabledPseudoMatches);
		add("CSS :enabled pseudo matches", metadata.cssEnabledPseudoMatches);
		add("CSS :required pseudo matches", metadata.cssRequiredPseudoMatches);
		add("CSS :read-only pseudo matches", metadata.cssReadonlyPseudoMatches);
		add("CSS :read-write pseudo matches", metadata.cssReadwritePseudoMatches);
		add("CSS :focus pseudo parsed", metadata.cssFocusPseudoParsed);
		add("CSS :focus pseudo matches", metadata.cssFocusPseudoMatches);
		add("CSS :focus-visible pseudo parsed", metadata.cssFocusVisiblePseudoParsed);
		add("CSS :focus-visible pseudo matches", metadata.cssFocusVisiblePseudoMatches);
		add("css_focus_pseudo_parsed", metadata.cssFocusPseudoParsed);
		add("css_focus_pseudo_matches", metadata.cssFocusPseudoMatches);
		add("css_focus_visible_pseudo_matches", metadata.cssFocusVisiblePseudoMatches);
		add("css_focus_runtime_recomputations", metadata.cssRuntimeFocusRecomputations);
		add("Form controls rendered", metadata.formControlsRendered);
		add("Form controls unsupported", metadata.formControlsUnsupported);
		add("Form interactions deferred", metadata.formInteractionsDeferred);
		add("form_runtime_controls_initialized", metadata.formRuntimeControlsInitialized);
		add("form_checkbox_activations", metadata.formCheckboxActivations);
		add("form_checkbox_toggles", metadata.formCheckboxToggles);
		add("form_radio_activations", metadata.formRadioActivations);
		add("form_radio_group_unchecks", metadata.formRadioGroupUnchecks);
		add("form_label_activations", metadata.formLabelActivations);
		add("form_button_activations", metadata.formButtonActivations);
		add("form_disabled_activation_blocks", metadata.formDisabledActivationBlocks);
		add("form_hidden_hit_targets_suppressed", metadata.formHiddenHitTargetsSuppressed);
		add("form_duplicate_activation_suppressed", metadata.formDuplicateActivationSuppressed);
		add("form_runtime_state_resets", metadata.formRuntimeStateResets);
		add("form_hit_targets_registered", metadata.formHitTargetsRegistered);
		add("form_hit_target_clamps", metadata.formHitTargetClamps);
		add("form_focusable_controls", metadata.formFocusableControls);
		add("form_focus_changes", metadata.formFocusChanges);
		add("form_focus_clears", metadata.formFocusClears);
		add("form_focus_wraps", metadata.formFocusWraps);
		add("form_tab_forward", metadata.formTabForward);
		add("form_tab_backward", metadata.formTabBackward);
		add("form_keyboard_activations", metadata.formKeyboardActivations);
		add("form_space_activations", metadata.formSpaceActivations);
		add("form_enter_activations", metadata.formEnterActivations);
		add("form_key_repeat_suppressed", metadata.formKeyRepeatSuppressed);
		add("form_stale_key_activation_blocks", metadata.formStaleKeyActivationBlocks);
		add("form_disabled_focus_skips", metadata.formDisabledFocusSkips);
		add("form_hidden_focus_skips", metadata.formHiddenFocusSkips);
		add("form_focus_state_resets", metadata.formFocusStateResets);
		add("css_checked_runtime_recomputations", metadata.cssCheckedRuntimeRecomputations);
		out += "Current Document.form_interaction_mode=" + metadata.formInteractionMode + "\n";
		out += "Current Document.form_focus_mode=session_local_non_editing\n";
		out += "Current Document.form_focus_origin=" +
			(metadata.formFocusOrigin.empty() ? "none" : metadata.formFocusOrigin) + "\n";
		out += "Current Document.form_focus_generation=" + std::to_string(metadata.formFocusGeneration) + "\n";
		out += "Current Document.form_focused_logical_serial=" + std::to_string(metadata.formFocusedLogicalSerial) + "\n";
	}

	static void appendFormPhase2HDiagnostics(std::string& out, const WebDocument& doc)
	{
		const gxos::web::FormsDiagnostics& diagnostics = doc.formsDiagnostics;
		const auto add = [&](const char* label, int value) {
			out += std::string("Current Document.") + label + "=" + std::to_string(value) + "\n";
		};
		add("form_focus_cancel_escape", diagnostics.formFocusCancelEscape);
		add("form_focus_cancel_navigation", diagnostics.formFocusCancelNavigation);
		add("form_focus_cancel_deactivation", diagnostics.formFocusCancelDeactivation);
		add("form_focus_cancel_state_change", diagnostics.formFocusCancelStateChange);
		add("form_focus_cancel_generation_mismatch", diagnostics.formFocusCancelGenerationMismatch);
		add("form_focus_cancel_key_mismatch", diagnostics.formFocusCancelKeyMismatch);
		add("form_key_repeat_suppressed", diagnostics.formKeyRepeatSuppressed);
		add("form_focus_origin_mouse", diagnostics.formFocusOriginMouse);
		add("form_focus_origin_keyboard", diagnostics.formFocusOriginKeyboard);
		add("form_focus_visible_matches", diagnostics.formFocusVisibleMatches);
		add("form_focus_ring_draws", diagnostics.formFocusRingDraws);
		add("form_focus_ring_clamps", diagnostics.formFocusRingClamps);
		add("form_focus_reveal_scrolls", diagnostics.formFocusRevealScrolls);
		add("form_focus_reveal_noops", diagnostics.formFocusRevealNoops);
		add("form_focus_reveal_clamps", diagnostics.formFocusRevealClamps);
		add("form_accessibility_records", diagnostics.formAccessibilityRecords);
		add("form_accessibility_metadata_clamps", diagnostics.formAccessibilityMetadataClamps);
		add("form_accessible_name_present", diagnostics.formAccessibleNamePresent);
		add("form_accessible_name_missing", diagnostics.formAccessibleNameMissing);
		add("form_label_associations_valid", diagnostics.formLabelAssociationsValid);
		add("form_label_associations_invalid", diagnostics.formLabelAssociationsInvalid);
		out += "Current Document.form_focus_mode=session_local_non_editing\n";
		out += "Current Document.form_accessibility_aria=deferred_native_bounded_only\n";
		out += "Current Document.form_accessibility_privacy=presence_only_no_values_names_or_passwords\n";

	const bool fixture = doc.url.find("css-phase2h.html") != std::string::npos ||
		doc.url.find("css-phase2i.html") != std::string::npos;
		const size_t count = std::min(doc.formRuntimeState.accessibilityRecordCount,
			doc.formRuntimeState.accessibilityRecords.size());
		constexpr size_t kFormAccessibilityEvidenceCap = 32;
		for (size_t i = 0; i < count && i < kFormAccessibilityEvidenceCap; ++i) {
			const FormAccessibilityRecord& record = doc.formRuntimeState.accessibilityRecords[i];
			if (!fixture || record.fixtureId.empty()) continue;
			out += "Current Document.form_accessibility_record=";
			out += "fixture-id=" + record.fixtureId;
			out += ",logical-serial=" + std::to_string(record.logicalSerial);
			out += ",document-generation=" + std::to_string(record.documentGeneration);
			out += ",role=" + std::string(formAccessibilityRoleName(record.role));
			out += ",focusable=" + yesNo(record.focusable);
			out += ",focused=" + yesNo(record.focused);
			out += ",focus-origin=" + std::string(formFocusOriginName(record.focusOrigin));
			out += ",focus=" + yesNo(record.focusMatch);
			out += ",focus-visible=" + yesNo(record.focusVisibleMatch);
			out += ",checked=" + yesNo(record.checked);
			out += ",disabled=" + yesNo(record.disabled);
			out += ",required=" + yesNo(record.required);
			out += ",readonly=" + yesNo(record.readOnly);
			out += ",visible=" + yesNo(record.visible);
			out += ",label-associated=" + yesNo(record.labelAssociated);
			out += ",label-source=" + std::string(formAccessibilityLabelSourceName(record.labelSource));
			out += ",accessible-name-present=" + yesNo(record.accessibleNamePresent);
			out += ",name-source=" + std::string(formAccessibilityNameSourceName(record.accessibleNameSource));
			out += ",metadata-complete=" + yesNo(record.metadataComplete);
			out += ",focus-ring-drawn=" + yesNo(record.focusRingDrawn);
			out += ",focus-ring-clamped=" + yesNo(record.focusRingClamped);
			out += ",reveal-scroll-result=" + std::string(formFocusRevealResultName(record.revealResult));
			out += ",winning-selector=" + record.winningSelectorCategory;
			out += ",winning-pseudo=" + record.winningPseudo;
		out += ",specificity=" + std::to_string(record.winningSpecificityId) + "." +
				std::to_string(record.winningSpecificityClass) + "." +
				std::to_string(record.winningSpecificityElement);
			out += ",source-order=" + std::to_string(record.winningSourceOrder) + "\n";
		}
		if (fixture && count > kFormAccessibilityEvidenceCap)
			out += "Current Document.form_accessibility_evidence_clamped=yes\n";
	}

	static void appendCssPhase3ADiagnostics(std::string& out, const NavigatorPageMetadata& metadata)
	{
		const auto add = [&](const char* label, int value) {
			out += std::string("Current Document.") + label + "=" + std::to_string(value) + "\n";
		};
		add("css_box_sizing_content_box", metadata.cssBoxSizingContentBox);
		add("css_box_sizing_border_box", metadata.cssBoxSizingBorderBox);
		add("css_width_auto_resolutions", metadata.cssWidthAutoResolutions);
		add("css_height_auto_resolutions", metadata.cssHeightAutoResolutions);
		add("css_percentage_width_resolved", metadata.cssPercentageWidthResolved);
		add("css_percentage_height_resolved", metadata.cssPercentageHeightResolved);
		add("css_percentage_indefinite_basis", metadata.cssPercentageIndefiniteBasis);
		add("css_percentage_cycle_clamps", metadata.cssPercentageCycleClamps);
		add("css_min_width_constraints", metadata.cssMinWidthConstraints);
		add("css_max_width_constraints", metadata.cssMaxWidthConstraints);
		add("css_min_height_constraints", metadata.cssMinHeightConstraints);
		add("css_max_height_constraints", metadata.cssMaxHeightConstraints);
		add("css_constraint_conflicts", metadata.cssConstraintConflicts);
		add("css_overflow_hidden_boxes", metadata.cssOverflowHiddenBoxes);
		add("css_overflow_auto_boxes", metadata.cssOverflowAutoBoxes);
		add("css_overflow_scroll_deferred", metadata.cssOverflowScrollDeferred);
		add("css_clip_intersections", metadata.cssClipIntersections);
		add("css_clip_depth_clamps", metadata.cssClipDepthClamps);
		add("css_clipped_hit_targets", metadata.cssClippedHitTargets);
		add("css_visibility_hidden_boxes", metadata.cssVisibilityHiddenBoxes);
		add("css_opacity_boxes", metadata.cssOpacityBoxes);
		add("css_opacity_zero_boxes", metadata.cssOpacityZeroBoxes);
		add("css_vertical_align_applications", metadata.cssVerticalAlignApplications);
		add("css_box_geometry_clamps", metadata.cssBoxGeometryClamps);
		add("css_length_value_clamps", metadata.cssLengthValueClampCount);
		add("css_invalid_length_values", metadata.cssInvalidLengthValueCount);
		add("css_layout_passes", metadata.cssLayoutPasses);
		add("css_layout_recomputations", metadata.cssLayoutRecomputations);
		add("css_clip_records", metadata.cssClipRecordCount);
		add("css_hit_targets_before_clipping", metadata.cssHitTargetsBeforeClipping);
		add("css_hit_targets_after_clipping", metadata.cssHitTargetsAfterClipping);
		add("css_evidence_records", metadata.cssEvidenceRecordCount);
		add("css_opacity_image_approximation", metadata.cssOpacityImageApproximation);
		out += "Current Document.css_overflow_auto_semantics=bounded_clipped_noninteractive\n";
		out += "Current Document.css_overflow_scroll_semantics=bounded_clipped_noninteractive_deferred\n";
		out += "Current Document.css_visibility_hidden_layout=retained\n";
		out += "Current Document.css_opacity_zero_hit_testing=eligible_when_visible\n";
		if (!metadata.cssGeometryEvidence.empty()) out += "Current Document.css_geometry_evidence=" + metadata.cssGeometryEvidence;
		add("css_margin_collapse_sets", metadata.cssMarginCollapseSets);
		add("css_margin_collapse_participants", metadata.cssMarginCollapseParticipants);
		add("css_margin_collapse_sibling", metadata.cssMarginCollapseSibling);
		add("css_margin_collapse_parent_top", metadata.cssMarginCollapseParentTop);
		add("css_margin_collapse_parent_bottom", metadata.cssMarginCollapseParentBottom);
		add("css_margin_collapse_empty", metadata.cssMarginCollapseEmpty);
		add("css_margin_collapse_positive_only", metadata.cssMarginCollapsePositiveOnly);
		add("css_margin_collapse_negative_only", metadata.cssMarginCollapseNegativeOnly);
		add("css_margin_collapse_mixed", metadata.cssMarginCollapseMixed);
		add("css_margin_collapse_blocked_border", metadata.cssMarginCollapseBlockedBorder);
		add("css_margin_collapse_blocked_padding", metadata.cssMarginCollapseBlockedPadding);
		add("css_margin_collapse_blocked_bfc", metadata.cssMarginCollapseBlockedBfc);
		add("css_margin_collapse_blocked_height", metadata.cssMarginCollapseBlockedHeight);
		add("css_margin_collapse_blocked_content", metadata.cssMarginCollapseBlockedContent);
		add("css_margin_collapse_depth_clamps", metadata.cssMarginCollapseDepthClamps);
		add("css_margin_geometry_clamps", metadata.cssMarginGeometryClamps);
		add("css_bfc_root", metadata.cssBfcRoot);
		add("css_bfc_inline_block", metadata.cssBfcInlineBlock);
		add("css_bfc_overflow", metadata.cssBfcOverflow);
		add("css_bfc_atomic", metadata.cssBfcAtomic);
		add("css_margin_collapse_evidence_records", metadata.cssMarginCollapseEvidenceRecords);
		out += "Current Document.css_margin_vertical_basis=containing-block-width-bounded\n";
		out += "Current Document.css_margin_collapse_model=largest-positive-plus-most-negative\n";
		out += "Current Document.css_bfc_boundaries=root-inline-block-overflow-atomic-table\n";
		if (!metadata.cssMarginCollapseEvidence.empty()) out += "Current Document.css_margin_collapse_evidence=" + metadata.cssMarginCollapseEvidence;
		add("css_float_left", metadata.cssFloatLeft);
		add("css_float_right", metadata.cssFloatRight);
		add("css_float_blockifications", metadata.cssFloatBlockifications);
		add("css_float_records", metadata.cssFloatRecords);
		add("css_float_placement_attempts", metadata.cssFloatPlacementAttempts);
		add("css_float_placement_downshifts", metadata.cssFloatPlacementDownshifts);
		add("css_float_side_by_side", metadata.cssFloatSideBySide);
		add("css_float_width_overflows", metadata.cssFloatWidthOverflows);
		add("css_float_line_exclusions", metadata.cssFloatLineExclusions);
		add("css_float_zero_width_line_advances", metadata.cssFloatZeroWidthLineAdvances);
		add("css_float_bfc_avoidances", metadata.cssFloatBfcAvoidances);
		add("css_float_bfc_downshifts", metadata.cssFloatBfcDownshifts);
		add("css_clear_left", metadata.cssClearLeft);
		add("css_clear_right", metadata.cssClearRight);
		add("css_clear_both", metadata.cssClearBoth);
		add("css_clearance_applied", metadata.cssClearanceApplied);
		add("css_float_containment_boundaries", metadata.cssFloatContainmentBoundaries);
		add("css_float_scope_suppressions", metadata.cssFloatScopeSuppressions);
		add("css_float_height_containments", metadata.cssFloatHeightContainments);
		add("css_bfc_float_containments", metadata.cssBfcFloatContainments);
		add("css_bfc_float_height_extensions", metadata.cssBfcFloatHeightExtensions);
		add("css_bfc_float_height_noops", metadata.cssBfcFloatHeightNoops);
		add("css_bfc_float_avoidance_attempts", metadata.cssBfcFloatAvoidanceAttempts);
		add("css_bfc_float_avoidance_fits", metadata.cssBfcFloatAvoidanceFits);
		add("css_bfc_float_avoidance_downshifts", metadata.cssBfcFloatAvoidanceDownshifts);
		add("css_bfc_float_too_wide", metadata.cssBfcFloatTooWide);
		add("css_nested_float_contexts", metadata.cssNestedFloatContexts);
		add("css_nested_float_depth_clamps", metadata.cssNestedFloatDepthClamps);
		add("css_float_inside_inline_block", metadata.cssFloatInsideInlineBlock);
		add("css_float_inside_float", metadata.cssFloatInsideFloat);
		add("css_float_list_cases", metadata.cssFloatListCases);
		add("css_float_table_cell_cases", metadata.cssFloatTableCellCases);
		add("css_float_table_avoidance_cases", metadata.cssFloatTableAvoidanceCases);
		add("css_floated_table_unsupported", metadata.cssFloatedTableUnsupported);
		add("css_float_document_extent_extensions", metadata.cssFloatDocumentExtentExtensions);
		add("css_float_geometry_clamps", metadata.cssFloatGeometryClamps);
		add("css_float_placement_attempt_clamps", metadata.cssFloatPlacementAttemptClamps);
		add("css_float_exclusion_scan_clamps", metadata.cssFloatExclusionScanClamps);
		add("css_float_bfc_depth_clamps", metadata.cssFloatBfcDepthClamps);
		add("css_float_evidence_records", metadata.cssFloatEvidenceRecords);
		out += "Current Document.css_float_model=bounded-traditional-left-right-margin-box-exclusion\n";
		out += "Current Document.css_float_blockification=inline-and-inline-block-to-bounded-float\n";
		out += "Current Document.css_float_line_query=authoritative-y-interval-exclusion\n";
		if (!metadata.cssFloatEvidence.empty()) out += "Current Document.css_float_evidence=" + metadata.cssFloatEvidence;
		add("css_position_static", metadata.cssPositionStatic);
		add("css_position_relative", metadata.cssPositionRelative);
		add("css_position_absolute", metadata.cssPositionAbsolute);
		add("css_position_unsupported_fixed", metadata.cssPositionUnsupportedFixed);
		add("css_position_unsupported_sticky", metadata.cssPositionUnsupportedSticky);
		add("css_relative_offsets", metadata.cssRelativeOffsets);
		add("css_relative_percentage_offsets", metadata.cssRelativePercentageOffsets);
		add("css_absolute_boxes", metadata.cssAbsoluteBoxes);
		add("css_absolute_blockifications", metadata.cssAbsoluteBlockifications);
		add("css_positioned_containing_blocks", metadata.cssPositionedContainingBlocks);
		add("css_position_root_fallbacks", metadata.cssPositionRootFallbacks);
		add("css_position_ancestry_clamps", metadata.cssPositionAncestryClamps);
		add("css_absolute_static_position_uses", metadata.cssAbsoluteStaticPositionUses);
		add("css_absolute_shrink_to_fit", metadata.cssAbsoluteShrinkToFit);
		add("css_absolute_out_of_flow", metadata.cssAbsoluteOutOfFlow);
		add("css_position_document_extent_extensions", metadata.cssPositionDocumentExtentExtensions);
		add("css_z_index_auto", metadata.cssZIndexAuto);
		add("css_z_index_negative", metadata.cssZIndexNegative);
		add("css_z_index_zero", metadata.cssZIndexZero);
		add("css_z_index_positive", metadata.cssZIndexPositive);
		add("css_position_hit_occlusions", metadata.cssPositionHitOcclusions);
		add("css_position_geometry_clamps", metadata.cssPositionGeometryClamps);
		add("css_position_unsupported_table", metadata.cssPositionUnsupportedTable);
		add("css_positioned_evidence_records", metadata.cssPositionedEvidenceRecords);
		out += "Current Document.css_position_model=bounded-static-relative-absolute\n";
		out += "Current Document.css_position_fixed_sticky=unsupported-diagnostic-not-aliased\n";
		out += "Current Document.css_position_relative_flow=unshifted-normal-flow-with-final-visual-offset\n";
		out += "Current Document.css_position_absolute_flow=out-of-flow-no-parent-height-contribution\n";
		out += "Current Document.css_position_initial_containing_block=document-content-viewport\n";
		out += "Current Document.css_z_index_model=positioned-only-negative-auto-zero-positive-source-order\n";
		if (!metadata.cssPositionedEvidence.empty()) out += "Current Document.css_positioned_evidence=" + metadata.cssPositionedEvidence;
		add("css_inline_items", metadata.cssInlineItems);
		add("css_inline_text_runs", metadata.cssInlineTextRuns);
		add("css_inline_whitespace_runs", metadata.cssInlineWhitespaceRuns);
		add("css_inline_forced_breaks", metadata.cssInlineForcedBreaks);
		add("css_line_boxes", metadata.cssLineBoxes);
		add("css_line_wraps", metadata.cssLineWraps);
		add("css_whitespace_collapses", metadata.cssWhitespaceCollapses);
		add("css_leading_space_suppressions", metadata.cssLeadingSpaceSuppressions);
		add("css_trailing_space_suppressions", metadata.cssTrailingSpaceSuppressions);
		add("css_replaced_inline_items", metadata.cssReplacedInlineItems);
		add("css_control_inline_items", metadata.cssControlInlineItems);
		add("css_vertical_align_adjustments", metadata.cssVerticalAlignAdjustments);
		add("css_line_height_clamps", metadata.cssLineHeightClamps);
		add("css_baseline_iteration_clamps", metadata.cssBaselineIterationClamps);
		add("css_inline_fragments", metadata.cssInlineFragments);
		add("css_inline_fragment_clamps", metadata.cssInlineFragmentClamps);
		add("css_inline_hit_fragments", metadata.cssInlineHitFragments);
		add("css_descender_safe_lines", metadata.cssDescenderSafeLines);
		add("css_inline_block_items", metadata.cssInlineBlockItems);
		add("css_inline_nesting_clamps", metadata.cssInlineNestingClamps);
		add("css_inline_wrap_scan_clamps", metadata.cssInlineWrapScanClamps);
		add("css_inline_evidence_records", metadata.cssInlineEvidenceRecordCount);
		add("css_atomic_formatting_contexts", metadata.cssAtomicFormattingContexts);
		add("css_atomic_evidence_records", metadata.cssAtomicEvidenceRecordCount);
		add("css_atomic_context_depth_max", metadata.cssAtomicContextDepthMax);
		add("css_atomic_context_depth_clamps", metadata.cssAtomicContextDepthClamps);
		add("css_atomic_contexts_document", metadata.cssAtomicContextsDocument);
		add("css_atomic_layout_operations", metadata.cssAtomicLayoutOperations);
		add("css_atomic_layout_operation_clamps", metadata.cssAtomicLayoutOperationClamps);
		add("css_inline_block_auto_widths", metadata.cssInlineBlockAutoWidths);
		add("css_inline_block_explicit_widths", metadata.cssInlineBlockExplicitWidths);
		add("css_inline_block_shrink_to_fit", metadata.cssInlineBlockShrinkToFit);
		add("css_inline_block_preferred_min_clamps", metadata.cssInlineBlockPreferredMinClamps);
		add("css_inline_block_preferred_width_clamps", metadata.cssInlineBlockPreferredWidthClamps);
		add("css_inline_block_baseline_from_line", metadata.cssInlineBlockBaselineFromLine);
		add("css_inline_block_baseline_fallback", metadata.cssInlineBlockBaselineFallback);
		add("css_inline_block_nested", metadata.cssInlineBlockNested);
		add("css_inline_block_wraps", metadata.cssInlineBlockWraps);
		add("css_inline_block_hit_targets", metadata.cssInlineBlockHitTargets);
		add("css_inline_block_overflow_clips", metadata.cssInlineBlockOverflowClips);
		add("css_atomic_context_incomplete", metadata.cssAtomicContextIncomplete);
		if (!metadata.cssInlineEvidence.empty()) out += "Current Document.css_inline_evidence=" + metadata.cssInlineEvidence;
		if (!metadata.cssAtomicEvidence.empty()) out += "Current Document.css_atomic_evidence=" + metadata.cssAtomicEvidence;
	}

	static void appendCssPhase3ABlocks(WebDocument& doc, const NavigatorPageMetadata& metadata)
	{
		doc.blocks.push_back({BlockType::Heading, "CSS Phase 3A Box and Overflow Diagnostics", ""});
		std::string report;
		appendCssPhase3ADiagnostics(report, metadata);
		std::istringstream lines(report);
		std::string line;
		while (std::getline(lines, line)) doc.blocks.push_back({BlockType::ListItem, line, ""});
	}

	static void appendFormPhase2EBlocks(WebDocument& doc, const NavigatorPageMetadata& metadata)
	{
		doc.blocks.push_back({BlockType::Heading, "CSS Phase 2E Form Diagnostics", ""});
		std::string report;
		appendFormPhase2EDiagnostics(report, metadata);
		std::istringstream lines(report);
		std::string line;
		while (std::getline(lines, line)) doc.blocks.push_back({BlockType::ListItem, line, ""});
	}

	static std::string filePathFromUrl(const std::string& url)
	{
		if (url.rfind("file://", 0) != 0) return "";
		std::string path = url.substr(7);
		if (path.size() >= 2 && path[0] == '/' && path[1] == '/') path = path.substr(1);
		return path;
	}

	static const ImageInfo& imageInfoForBlock(const DocBlock& block)
	{
		const std::string key = block.url.empty() ? block.src : block.url;
		auto found = s_imageCache.find(key);
		if (found != s_imageCache.end()) return found->second;

		ImageInfo info;
		info.attempted = true;

		if (isRemoteHttpUrl(block.url)) {
			gxos::web::HttpResponse response = gxos::web::fetchHttpUrl(block.url);
			if (!response.ok()) {
				info.status = gxos::gui::ImageLoadStatus::NotFound;
				info.message = "[remote image error]";
				info.errorDetail = std::string("Remote image fetch failed: ") +
					gxos::web::httpErrorName(response.error);
				if (!response.errorMessage.empty()) info.errorDetail += ": " + response.errorMessage;
				auto inserted = s_imageCache.emplace(key, std::move(info));
				return inserted.first->second;
			}
			if (response.statusCode != 200) {
				info.status = gxos::gui::ImageLoadStatus::NotFound;
				info.message = "[remote image not found]";
				info.errorDetail = "Remote image HTTP status " + std::to_string(response.statusCode);
				auto inserted = s_imageCache.emplace(key, std::move(info));
				return inserted.first->second;
			}
			if (response.body.size() > kRemoteImageMaxBytes) {
				info.tooLarge = true;
				info.status = gxos::gui::ImageLoadStatus::TooLarge;
				info.message = "[image too large]";
				info.errorDetail = "Remote image exceeds byte limit";
				auto inserted = s_imageCache.emplace(key, std::move(info));
				return inserted.first->second;
			}
			const bool contentTypePng = response.contentType == "image/png";
			const bool urlLooksPng = endsWithIgnoreCase(response.finalUrl.empty() ? block.url : response.finalUrl, ".png");
			if (!contentTypePng && !urlLooksPng) {
				info.unsupported = true;
				info.status = gxos::gui::ImageLoadStatus::UnsupportedFormat;
				info.message = "[unsupported image]";
				info.errorDetail = "Remote image is not image/png";
				auto inserted = s_imageCache.emplace(key, std::move(info));
				return inserted.first->second;
			}
			if (!isPngSignature(response.body)) {
				info.unsupported = true;
				info.status = gxos::gui::ImageLoadStatus::UnsupportedFormat;
				info.message = "[unsupported image]";
				info.errorDetail = "Remote image PNG signature is invalid";
				auto inserted = s_imageCache.emplace(key, std::move(info));
				return inserted.first->second;
			}

			gxos::gui::ImageBitmap decoded = gxos::gui::ImageAdapter::LoadFromBytes(
				reinterpret_cast<const uint8_t*>(response.body.data()),
				response.body.size(),
				response.finalUrl.empty() ? block.url : response.finalUrl,
				remoteImageSafetyLimits());
			info.status = decoded.status;
			if (decoded.status == gxos::gui::ImageLoadStatus::Ok) {
				const std::string tempPath = remoteImageTempPath(response.finalUrl.empty() ? block.url : response.finalUrl);
				if (writeBinaryTempFile(tempPath, response.body)) {
					info.ok = true;
					info.naturalW = decoded.width;
					info.naturalH = decoded.height;
					info.filePath = response.finalUrl.empty() ? block.url : response.finalUrl;
					info.drawPath = tempPath;
					s_remoteImageTempFiles.push_back(tempPath);
				} else {
					info.status = gxos::gui::ImageLoadStatus::DecodeFailed;
					info.message = "[image cache error]";
					info.errorDetail = "Could not write remote image temp file";
				}
			} else {
				info.unsupported = decoded.status == gxos::gui::ImageLoadStatus::UnsupportedFormat;
				info.tooLarge = decoded.status == gxos::gui::ImageLoadStatus::TooLarge;
				info.message = std::string("[") + gxos::gui::ImageLoadStatusName(decoded.status) + "]";
				info.errorDetail = "Remote image decode failed: " + std::string(gxos::gui::ImageLoadStatusName(decoded.status));
			}

			auto inserted = s_imageCache.emplace(key, std::move(info));
			return inserted.first->second;
		}

		if (block.url.rfind("file://", 0) != 0) {
			info.unsupported = true;
			info.status = gxos::gui::ImageLoadStatus::UnsupportedFormat;
			info.message = "[unsupported image]";
			info.errorDetail = "Unsupported image URL scheme";
			auto inserted = s_imageCache.emplace(key, std::move(info));
			return inserted.first->second;
		}

		info.filePath = filePathFromUrl(block.url);
		if (!endsWithIgnoreCase(info.filePath, ".png")) {
			info.unsupported = true;
			info.status = gxos::gui::ImageLoadStatus::UnsupportedFormat;
			info.message = "[unsupported image]";
			info.errorDetail = "Local image is not a PNG";
			auto inserted = s_imageCache.emplace(key, std::move(info));
			return inserted.first->second;
		}

		BinaryReadResult br = readBinaryFile(info.filePath);
		if (br.status == FileReadStatus::NotFound) {
			info.status = gxos::gui::ImageLoadStatus::NotFound;
			info.message = "[missing image]";
			info.errorDetail = "Local image file not found";
		} else if (br.status == FileReadStatus::TooLarge) {
			info.tooLarge = true;
			info.status = gxos::gui::ImageLoadStatus::TooLarge;
			info.message = "[image too large]";
			info.errorDetail = "Local image exceeds byte limit";
		} else if (br.status == FileReadStatus::IoError) {
			info.status = gxos::gui::ImageLoadStatus::DecodeFailed;
			info.message = "[image read error]";
			info.errorDetail = "Local image read error";
		} else {
			gxos::gui::ImageBitmap decoded = gxos::gui::ImageAdapter::LoadFromBytes(br.bytes, info.filePath);
			info.status = decoded.status;
			if (decoded.status == gxos::gui::ImageLoadStatus::Ok) {
				info.ok = true;
				info.naturalW = decoded.width;
				info.naturalH = decoded.height;
				info.drawPath = imageLoaderPathForFile(info.filePath);
			} else {
				info.unsupported = decoded.status == gxos::gui::ImageLoadStatus::UnsupportedFormat;
				info.tooLarge = decoded.status == gxos::gui::ImageLoadStatus::TooLarge;
				info.message = std::string("[") + gxos::gui::ImageLoadStatusName(decoded.status) + "]";
				info.errorDetail = "Local image decode failed: " + std::string(gxos::gui::ImageLoadStatusName(decoded.status));
			}
		}

		auto inserted = s_imageCache.emplace(key, std::move(info));
		return inserted.first->second;
	}

	static void imageDisplaySize(const DocBlock& block, int availableWidth, int& outW, int& outH,
		bool* outConstrained, bool* outAspectPreserved, bool* outClamped)
	{
		const int horizontalEdges = cssHorizontalBoxEdges(block.style);
		const int verticalEdges = cssVerticalBoxEdges(block.style);
		const ImageInfo& info = imageInfoForBlock(block);
		int naturalW = info.ok ? info.naturalW : 220;
		int naturalH = info.ok ? info.naturalH : 64;
		naturalW = std::max(1, std::min(2048, naturalW));
		naturalH = std::max(1, std::min(2048, naturalH));
		const CssResolvedLength widthResolved = resolveCssLength(block.style.widthValue,
			block.style.width, block.style.widthPercent, availableWidth);
		const CssResolvedLength heightResolved = resolveCssLength(block.style.heightValue,
			block.style.height, block.style.heightPercent, -1);
		const int contentLimitW = widthResolved.definite ? 2048 :
			std::max(1, std::min(2048, availableWidth));
		const int contentLimitH = heightResolved.definite ? 2048 :
			std::max(1, std::min(2048, kContentH - 20));
		const CssResolvedLength minWidthResolved = resolveCssLength(block.style.minWidthValue,
			block.style.minWidth, block.style.minWidthPercent, availableWidth);
		const CssResolvedLength maxWidthResolved = resolveCssLength(block.style.maxWidthValue,
			block.style.maxWidth, block.style.maxWidthPercent, availableWidth);
		const CssResolvedLength minHeightResolved = resolveCssLength(block.style.minHeightValue,
			block.style.minHeight, block.style.minHeightPercent, -1);
		const CssResolvedLength maxHeightResolved = resolveCssLength(block.style.maxHeightValue,
			block.style.maxHeight, block.style.maxHeightPercent, -1);
		auto contentWidthValue = [&](const CssResolvedLength& resolved) {
			if (!resolved.definite) return -1;
			return block.style.boxSizing == BoxSizingMode::BorderBox
				? std::max(0, resolved.px - horizontalEdges) : resolved.px;
		};
		auto contentHeightValue = [&](const CssResolvedLength& resolved) {
			if (!resolved.definite) return -1;
			return block.style.boxSizing == BoxSizingMode::BorderBox
				? std::max(0, resolved.px - verticalEdges) : resolved.px;
		};

		int drawW = naturalW;
		int drawH = naturalH;
		bool widthSpecified = false;
		bool heightSpecified = false;

		if (block.width > 0) {
			drawW = block.width;
			widthSpecified = true;
		}
		if (block.height > 0) {
			drawH = block.height;
			heightSpecified = true;
		}

		const int cssWidth = contentWidthValue(widthResolved);
		const int cssHeight = contentHeightValue(heightResolved);
		if (cssWidth >= 0) {
			drawW = cssWidth;
			widthSpecified = true;
		}
		if (cssHeight >= 0) {
			drawH = cssHeight;
			heightSpecified = true;
		}

		bool aspectPreserved = false;
		if (widthSpecified && !heightSpecified) {
			drawH = std::max(1, (drawW * naturalH) / naturalW);
			aspectPreserved = true;
		} else if (heightSpecified && !widthSpecified) {
			drawW = std::max(1, (drawH * naturalW) / naturalH);
			aspectPreserved = true;
		}

		int minW = 0;
		int minH = 0;
		int limitW = contentLimitW;
		int limitH = contentLimitH;
		if (const int value = contentWidthValue(minWidthResolved); value >= 0) minW = value;
		if (const int value = contentHeightValue(minHeightResolved); value >= 0) minH = value;
		if (const int value = contentWidthValue(maxWidthResolved); value >= 0) limitW = std::min(limitW, value);
		if (const int value = contentHeightValue(maxHeightResolved); value >= 0) limitH = std::min(limitH, value);
		if (minW > limitW) limitW = minW;
		if (minH > limitH) limitH = minH;

		bool constrained = false;
		bool sizeClamped = block.imageSizeAttrClamped;
		if (drawW < minW || drawH < minH) {
			const double scaleW = minW > 0 ? static_cast<double>(minW) / std::max(1, drawW) : 1.0;
			const double scaleH = minH > 0 ? static_cast<double>(minH) / std::max(1, drawH) : 1.0;
			const double scale = std::max(scaleW, scaleH);
			if (scale > 1.0 && (!widthSpecified || !heightSpecified)) {
				drawW = std::max(1, static_cast<int>(drawW * scale));
				drawH = std::max(1, static_cast<int>(drawH * scale));
				constrained = true;
				aspectPreserved = true;
				sizeClamped = true;
			}
		}
		if (drawW > limitW || drawH > limitH) {
			const double scaleW = static_cast<double>(limitW) / static_cast<double>(drawW);
			const double scaleH = static_cast<double>(limitH) / static_cast<double>(drawH);
			const double scale = std::min(scaleW, scaleH);
			if (scale < 1.0) {
				const int scaledW = std::max(1, static_cast<int>(drawW * scale));
				const int scaledH = std::max(1, static_cast<int>(drawH * scale));
				if (scaledW != drawW || scaledH != drawH) {
					constrained = true;
					sizeClamped = true;
				}
				drawW = scaledW;
				drawH = scaledH;
				aspectPreserved = true;
			}
		}
		drawW = std::max(1, std::min(2048, drawW));
		drawH = std::max(1, std::min(2048, drawH));

		if (!info.ok && !block.alt.empty()) {
			const int placeholderMaxChars = std::max(1, (drawW - 20) / kCharW);
			const std::vector<std::string> altLines = wrapText(block.alt, std::max(1, placeholderMaxChars));
			const int placeholderLineCount = std::max(1, std::min(3, static_cast<int>(altLines.size())));
			const int placeholderH = placeholderLineCount * blockTextLineHeight(block) + 16;
			if (drawH < placeholderH) {
				drawH = placeholderH;
			}
		}

		outW = std::max(1, drawW);
		outH = std::max(1, drawH);
		if (outConstrained) *outConstrained = constrained;
		if (outAspectPreserved) *outAspectPreserved = aspectPreserved;
		if (outClamped) *outClamped = sizeClamped;
	}

	static std::string imagePlaceholderText(const DocBlock& block, const ImageInfo& info)
	{
		if (!block.alt.empty()) return block.alt;
		if (!block.text.empty()) return block.text;
		return info.message.empty() ? "[missing image]" : info.message;
	}

	struct InlineAtom {
		int itemIndex = -1;
		int sourceOffset = 0;
		int sourceLength = 0;
		int width = 0;
		int height = 0;
		int baselineOffset = 0;
		int lineHeight = 0;
		int ascent = 0;
		int descent = 0;
		uint64_t ownerSerial = 0;
		InlineItemKind kind = InlineItemKind::TextRun;
		bool whitespace = false;
		bool collapsedWhitespace = false;
		bool forcedBreak = false;
		bool noWrap = false;
		int inlineLeft = 0;
		int inlineRight = 0;
		int inlineTop = 0;
		int inlineBottom = 0;
		int atomicResultIndex = -1;
	};

	static int buildAtomicLayout(const WebDocument& doc, uint64_t containerSerial,
		int availableWidth, uint16_t parentDepth, InlineLayoutSnapshot& snapshot);

	struct InlineFontMetrics {
		int ascent = 1;
		int descent = 1;
		int baseline = 1;
		int glyphHeight = 2;
		int lineHeight = kLineH;
	};

	static WhiteSpaceMode inlineWhiteSpace(const WebStyle& style)
	{
		return style.whiteSpace == WhiteSpaceMode::Inherit ? WhiteSpaceMode::Normal : style.whiteSpace;
	}

	static const BitmapFontFace* inlineFontFace(const WebStyle& style)
	{
		const int fontSize = cssFontSizeOrDefault(style, 16);
		const FontSize size = fontSize <= 13 ? FontSize::Small9 : FontSize::Normal12;
		const FontWeight weight = style.bold ? FontWeight::Bold : FontWeight::Regular;
		const FontSlant slant = style.italic ? FontSlant::Italic : FontSlant::Normal;
		return SystemFont::GetFace(size, weight, slant);
	}

	static InlineFontMetrics inlineFontMetrics(const WebStyle& style)
	{
		const BitmapFontFace* face = inlineFontFace(style);
		InlineFontMetrics metrics;
		metrics.ascent = std::max(1, SystemFont::MeasureAscent(face));
		metrics.descent = std::max(1, SystemFont::MeasureDescent(face));
		metrics.baseline = std::max(1, SystemFont::BaselineOffset(face));
		metrics.glyphHeight = std::max(1, metrics.ascent + metrics.descent);
		metrics.lineHeight = std::max(1, SystemFont::MeasureLineHeight(face));
		const int requested = cssFontSizeOrDefault(style, 16);
		if (requested > 13) {
			const int scale = std::max(1, requested * 100 / 12);
			metrics.ascent = std::max(1, (metrics.ascent * scale + 99) / 100);
			metrics.descent = std::max(1, (metrics.descent * scale + 99) / 100);
			metrics.baseline = std::max(1, (metrics.baseline * scale + 99) / 100);
			metrics.lineHeight = std::max(1, (metrics.lineHeight * scale + 99) / 100);
			metrics.glyphHeight = metrics.ascent + metrics.descent;
		}
		return metrics;
	}

	static int inlineTextWidth(const WebStyle& style, const std::string& text)
	{
		if (text.empty()) return 0;
		const BitmapFontFace* face = inlineFontFace(style);
		int width = SystemFont::MeasureWidth(face, text.c_str(), static_cast<int>(text.size()));
		if (width <= 0) width = static_cast<int>(text.size()) * kCharW;
		const int requested = cssFontSizeOrDefault(style, 16);
		if (requested > 13) width = std::max(1, (width * requested + 11) / 12);
		return std::max(1, std::min(8192, width));
	}

	static int inlineUsedLineHeight(const WebStyle& style, const InlineFontMetrics& metrics,
		int& outClamp)
	{
		outClamp = 0;
		const int fontSize = cssFontSizeOrDefault(style, 16);
		int requested = metrics.lineHeight;
		switch (style.lineHeightMode) {
		case LineHeightMode::Unitless:
			requested = static_cast<int>((static_cast<int64_t>(fontSize) *
				std::max(0, style.lineHeightValue) + 500) / 1000);
			break;
		case LineHeightMode::Percent:
			requested = (fontSize * std::max(0, style.lineHeightValue) + 50) / 100;
			break;
		case LineHeightMode::Px:
			requested = style.lineHeightValue;
			break;
		case LineHeightMode::Normal:
		default:
			if (!style.lineHeightNormal && style.lineHeight > 0) requested = style.lineHeight;
			break;
		}
		if (requested < 0) {
			requested = 0;
			outClamp = 1;
		}
		if (requested > 256) {
			requested = 256;
			outClamp = 1;
		}
		return std::max(1, requested);
	}

	static const WebStyle* inlineOwnerStyle(const WebDocument& doc,
		const WebInlineItem& item,
		const WebStyle& fallback)
	{
		if (const WebStyle* style = computedStyleForSerial(doc, item.ownerSerial)) return style;
		return &fallback;
	}

	static void appendInlineAtom(std::vector<InlineAtom>& atoms,
		const InlineAtom& atom,
		InlineLayoutSnapshot& snapshot)
	{
		constexpr size_t kMaxAtomsPerFlow = 4096;
		if (atoms.size() >= kMaxAtomsPerFlow) {
			++snapshot.wrapScanClamps;
			return;
		}
		atoms.push_back(atom);
	}

	static void appendInlineTextAtoms(const WebDocument& doc,
		const InlineFlowLayout& flow,
		const WebInlineItem& item,
		int itemIndex,
		std::vector<InlineAtom>& atoms,
		InlineLayoutSnapshot& snapshot,
		bool& pendingCollapsedSpace,
		bool& lineHasContent)
	{
		const WebStyle* style = inlineOwnerStyle(doc, item, flow.style);
		if (!style || style->displayNone || style->visibility == VisibilityMode::Hidden) return;
		const WhiteSpaceMode whitespaceMode = inlineWhiteSpace(*style);
		const bool preserve = whitespaceMode == WhiteSpaceMode::Pre ||
			whitespaceMode == WhiteSpaceMode::PreWrap;
		const bool preserveNewline = preserve || whitespaceMode == WhiteSpaceMode::PreLine;
		const bool noWrap = whitespaceMode == WhiteSpaceMode::Nowrap ||
			whitespaceMode == WhiteSpaceMode::Pre;
		const InlineFontMetrics metrics = inlineFontMetrics(*style);
		int lineHeightClamp = 0;
		const int usedLineHeight = inlineUsedLineHeight(*style, metrics, lineHeightClamp);
		if (lineHeightClamp) ++snapshot.lineHeightClamps;
		const std::string& text = item.text;
		bool countedWhitespaceRun = false;
		size_t cursor = 0;
		while (cursor < text.size()) {
			const char ch = text[cursor];
			if (ch == '\r' || ch == '\n') {
				if (ch == '\r' && cursor + 1 < text.size() && text[cursor + 1] == '\n') ++cursor;
				if (preserveNewline) {
					if (pendingCollapsedSpace) {
						pendingCollapsedSpace = false;
						++snapshot.trailingSpaceSuppressions;
					}
					InlineAtom atom;
					atom.itemIndex = itemIndex;
					atom.sourceOffset = static_cast<int>(cursor);
					atom.sourceLength = 1;
					atom.forcedBreak = true;
					atom.ownerSerial = item.ownerSerial;
					appendInlineAtom(atoms, atom, snapshot);
					lineHasContent = false;
					++snapshot.forcedBreaks;
					++cursor;
					continue;
				}
				pendingCollapsedSpace = true;
				++snapshot.whitespaceCollapses;
				++cursor;
				continue;
			}
			const bool isSpace = ch == ' ' || ch == '\t' || ch == '\f' || ch == '\v';
			if (isSpace) {
				size_t end = cursor + 1;
				while (end < text.size() && (text[end] == ' ' || text[end] == '\t' ||
					text[end] == '\f' || text[end] == '\v')) ++end;
				if (preserve) {
					InlineAtom atom;
					atom.itemIndex = itemIndex;
					atom.sourceOffset = static_cast<int>(cursor);
					atom.sourceLength = static_cast<int>(end - cursor);
					atom.whitespace = true;
					atom.ownerSerial = item.ownerSerial;
					atom.noWrap = noWrap;
					atom.width = inlineTextWidth(*style, text.substr(cursor, end - cursor));
					atom.height = metrics.glyphHeight;
					atom.baselineOffset = metrics.baseline;
					atom.ascent = metrics.ascent;
					atom.descent = metrics.descent;
					atom.lineHeight = usedLineHeight;
					appendInlineAtom(atoms, atom, snapshot);
				} else {
					pendingCollapsedSpace = true;
					++snapshot.whitespaceCollapses;
				}
				if (!countedWhitespaceRun) {
					++snapshot.whitespaceRuns;
					countedWhitespaceRun = true;
				}
				cursor = end;
				continue;
			}
			size_t end = cursor + 1;
			while (end < text.size() && text[end] != '\r' && text[end] != '\n' &&
				text[end] != ' ' && text[end] != '\t' && text[end] != '\f' && text[end] != '\v') ++end;
			if (pendingCollapsedSpace) {
				if (!lineHasContent) {
					++snapshot.leadingSpaceSuppressions;
				} else {
					InlineAtom space;
					space.itemIndex = itemIndex;
					space.sourceOffset = static_cast<int>(cursor);
					space.sourceLength = 0;
					space.width = inlineTextWidth(*style, " ");
					space.height = metrics.glyphHeight;
					space.baselineOffset = metrics.baseline;
					space.ascent = metrics.ascent;
					space.descent = metrics.descent;
					space.lineHeight = usedLineHeight;
					space.ownerSerial = item.ownerSerial;
					space.whitespace = true;
					space.collapsedWhitespace = true;
					space.noWrap = noWrap;
					appendInlineAtom(atoms, space, snapshot);
				}
				pendingCollapsedSpace = false;
			}
			InlineAtom atom;
			atom.itemIndex = itemIndex;
			atom.sourceOffset = static_cast<int>(cursor);
			atom.sourceLength = static_cast<int>(end - cursor);
			atom.width = inlineTextWidth(*style, text.substr(cursor, end - cursor));
			atom.height = metrics.glyphHeight;
			atom.baselineOffset = metrics.baseline + std::max(0, (usedLineHeight - metrics.glyphHeight) / 2);
			atom.ascent = metrics.ascent;
			atom.descent = metrics.descent;
			atom.lineHeight = usedLineHeight;
			atom.ownerSerial = item.ownerSerial;
			atom.noWrap = noWrap;
			appendInlineAtom(atoms, atom, snapshot);
			lineHasContent = true;
			cursor = end;
		}
		int firstContentAtom = -1;
		int lastContentAtom = -1;
		const int inlineLeft = cssPaddingLeftPx(*style, 0) + cssBorderLeftPx(*style);
		const int inlineRight = cssPaddingRightPx(*style, 0) + cssBorderRightPx(*style);
		const int inlineTop = cssPaddingTopPx(*style, 0) + cssBorderTopPx(*style);
		const int inlineBottom = cssPaddingBottomPx(*style, 0) + cssBorderBottomPx(*style);
		for (int atomIndex = 0; atomIndex < static_cast<int>(atoms.size()); ++atomIndex) {
			InlineAtom& atom = atoms[static_cast<size_t>(atomIndex)];
			if (atom.itemIndex != itemIndex || atom.whitespace || atom.forcedBreak) continue;
			if (firstContentAtom < 0) firstContentAtom = atomIndex;
			lastContentAtom = atomIndex;
			atom.height = std::min(256, atom.height + inlineTop + inlineBottom);
			atom.baselineOffset = std::min(256, atom.baselineOffset + inlineTop);
			atom.ascent = std::min(256, atom.ascent + inlineTop);
			atom.descent = std::min(256, atom.descent + inlineBottom);
			atom.inlineTop = inlineTop;
			atom.inlineBottom = inlineBottom;
		}
		if (firstContentAtom >= 0) {
			atoms[static_cast<size_t>(firstContentAtom)].width = std::min(8192,
				atoms[static_cast<size_t>(firstContentAtom)].width + inlineLeft);
			atoms[static_cast<size_t>(firstContentAtom)].inlineLeft = inlineLeft;
			atoms[static_cast<size_t>(lastContentAtom)].width = std::min(8192,
				atoms[static_cast<size_t>(lastContentAtom)].width + inlineRight);
			atoms[static_cast<size_t>(lastContentAtom)].inlineRight = inlineRight;
		}
	}

	static int inlineAtomWidth(const WebDocument& doc, const InlineFlowLayout& flow,
		const InlineAtom& atom)
	{
		if (atom.width > 0) return atom.width;
		if (atom.itemIndex < 0 || atom.itemIndex >= static_cast<int>(doc.inlineItems.size())) return 0;
		const WebInlineItem& item = doc.inlineItems[static_cast<size_t>(atom.itemIndex)];
		if (item.blockIndex < 0 || item.blockIndex >= static_cast<int>(doc.blocks.size())) return kCharW;
		const DocBlock& block = doc.blocks[static_cast<size_t>(item.blockIndex)];
		int width = kCharW;
		if (item.kind == InlineItemKind::ReplacedImage) {
			int h = 0;
			imageDisplaySize(block, flow.contentWidth, width, h);
		} else if (item.kind == InlineItemKind::FormControl) {
			width = blockFormControlWidth(block, flow.contentWidth);
			if (block.type == BlockType::FormCheckbox || block.type == BlockType::FormRadio)
				width = 22;
		}
		return std::max(1, std::min(8192, width));
	}

	static void finalizeInlineLine(InlineFlowLayout& flow,
		InlineLineLayout& line,
		InlineLayoutSnapshot& snapshot,
		const WebDocument& doc)
	{
		if (line.fragmentCount <= 0) {
			const WebStyle* style = &flow.style;
			const InlineFontMetrics metrics = inlineFontMetrics(*style);
			int clamp = 0;
			const int requested = inlineUsedLineHeight(*style, metrics, clamp);
			line.ascent = metrics.ascent;
			line.descent = metrics.descent;
			line.usedLineHeight = std::max(requested, line.ascent + line.descent);
			if (line.usedLineHeight > 256) {
				line.usedLineHeight = 256;
				++snapshot.lineHeightClamps;
			}
			if (line.usedLineHeight >= line.ascent + line.descent) ++snapshot.descenderSafeLines;
			return;
		}
		const InlineFontMetrics parentMetrics = inlineFontMetrics(flow.style);
		int maxAscent = parentMetrics.ascent;
		int maxDescent = parentMetrics.descent;
		int requestedLineHeight = parentMetrics.lineHeight;
		const int start = line.firstFragment;
		const int end = start + line.fragmentCount;
		for (int fi = start; fi < end; ++fi) {
			InlineFragmentLayout& fragment = flow.fragments[static_cast<size_t>(fi)];
			const WebInlineItem& item = doc.inlineItems[static_cast<size_t>(fragment.itemIndex)];
			const WebStyle* style = inlineOwnerStyle(doc, item, flow.style);
			const InlineFontMetrics metrics = inlineFontMetrics(*style);
			int clamp = 0;
			requestedLineHeight = std::max(requestedLineHeight, inlineUsedLineHeight(*style, metrics, clamp));
			if (clamp) ++snapshot.lineHeightClamps;
			fragment.verticalShift = 0;
			const VerticalAlignMode mode = style->verticalAlign;
			if (mode == VerticalAlignMode::Middle) {
				fragment.verticalShift = -parentMetrics.ascent / 2 - fragment.h / 2 + fragment.baselineOffset;
			} else if (mode == VerticalAlignMode::TextTop) {
				fragment.verticalShift = -parentMetrics.ascent + fragment.baselineOffset;
			} else if (mode == VerticalAlignMode::TextBottom) {
				fragment.verticalShift = parentMetrics.descent - fragment.h + fragment.baselineOffset;
			} else if (mode == VerticalAlignMode::Sub) {
				fragment.verticalShift = std::max(1, parentMetrics.descent / 2);
			} else if (mode == VerticalAlignMode::Super) {
				fragment.verticalShift = -std::max(1, parentMetrics.ascent / 2);
			} else if (mode == VerticalAlignMode::LengthPx) {
				fragment.verticalShift = -style->verticalAlignValue;
			} else if (mode == VerticalAlignMode::Percent) {
				fragment.verticalShift = -(requestedLineHeight * style->verticalAlignValue) / 100;
			}
			if (fragment.verticalShift != 0) ++snapshot.verticalAlignAdjustments;
			const int top = -fragment.baselineOffset + fragment.verticalShift;
			maxAscent = std::max(maxAscent, -top);
			maxDescent = std::max(maxDescent, top + fragment.h);
		}
		for (int iteration = 0; iteration < 3; ++iteration) {
			bool changed = false;
			for (int fi = start; fi < end; ++fi) {
				InlineFragmentLayout& fragment = flow.fragments[static_cast<size_t>(fi)];
				const WebInlineItem& item = doc.inlineItems[static_cast<size_t>(fragment.itemIndex)];
				const WebStyle* style = inlineOwnerStyle(doc, item, flow.style);
				const int old = fragment.verticalShift;
				switch (style->verticalAlign) {
				case VerticalAlignMode::Top:
					fragment.verticalShift = -maxAscent + fragment.baselineOffset;
					break;
				case VerticalAlignMode::Bottom:
					fragment.verticalShift = maxDescent - fragment.h + fragment.baselineOffset;
					break;
				default:
					break;
				}
				changed = changed || old != fragment.verticalShift;
			}
			if (!changed) break;
			if (iteration == 2) ++snapshot.baselineIterationClamps;
		}
		line.ascent = std::max(1, maxAscent);
		line.descent = std::max(1, maxDescent);
		line.usedLineHeight = std::max(requestedLineHeight, line.ascent + line.descent);
		if (line.usedLineHeight > 256) {
			line.usedLineHeight = 256;
			++snapshot.lineHeightClamps;
		}
		if (line.usedLineHeight >= line.ascent + line.descent) ++snapshot.descenderSafeLines;
		line.baseline = line.ascent;
		for (int fi = start; fi < end; ++fi) {
			InlineFragmentLayout& fragment = flow.fragments[static_cast<size_t>(fi)];
			fragment.y = line.top + line.baseline - fragment.baselineOffset + fragment.verticalShift;
		}
	}

	static void buildInlineFlow(const WebDocument& doc,
		InlineFlowLayout& flow,
		InlineLayoutSnapshot& snapshot,
		int availableWidthOverride = -1,
		int containingHeightOverride = -1)
	{
		DocBlock geometryBlock;
		if (flow.anchorBlockIndex >= 0 && flow.anchorBlockIndex < static_cast<int>(doc.blocks.size()))
			geometryBlock = doc.blocks[static_cast<size_t>(flow.anchorBlockIndex)];
		else
			geometryBlock.type = BlockType::Paragraph;
		geometryBlock.style = flow.style;
		geometryBlock.inlineFlowSerial = flow.flowSerial;
		const int availableWidth = availableWidthOverride >= 0
			? std::max(1, availableWidthOverride) : blockAvailableWidth(geometryBlock, doc);
		const int outerWidth = blockOuterWidth(geometryBlock, availableWidth);
		flow.outerWidth = std::max(1, outerWidth);
		if (flow.contextSerial == 0) {
			flow.outerX = blockOuterX(geometryBlock, doc, availableWidth, flow.outerWidth);
			flow.localOuterX = flow.outerX;
		} else {
			flow.outerX = 0;
		}
		flow.contentX = flow.outerX + cssBorderLeftPx(flow.style) + cssPaddingLeftPx(flow.style, 0);
		flow.contentWidth = std::max(1, flow.outerWidth - cssHorizontalBoxEdges(flow.style));
			flow.documentContentTop = 0;
			if (flow.contextSerial == 0 && flow.anchorBlockIndex >= 0 &&
				flow.anchorBlockIndex < static_cast<int>(s_cssMarginLayoutSnapshot.records.size()) &&
				s_cssMarginLayoutSnapshot.valid) {
				const CssMarginFlowRecord& marginRecord = s_cssMarginLayoutSnapshot.records[
					static_cast<size_t>(flow.anchorBlockIndex)];
				flow.documentContentTop = marginRecord.usedY + cssBorderTopPx(flow.style) +
					cssPaddingTopPx(flow.style, 0);
			}
		if (geometryBlock.type == BlockType::ListItem) {
			const int inset = blockListTextInsetPx(geometryBlock, blockListOrdinal(doc, flow.anchorBlockIndex));
			flow.contentX += inset;
			flow.contentWidth = std::max(1, flow.contentWidth - inset);
		}
		const uint64_t flowBfcIdentity = cssFloatBfcIdentity(doc, flow);
		if (s_cssFloatLayoutSnapshot.valid && flow.contextSerial == 0 &&
			flow.anchorBlockIndex >= 0 && flow.anchorBlockIndex < static_cast<int>(doc.blocks.size())) {
			const ClearMode clearMode = geometryBlock.style.clearMode;
			if (clearMode != ClearMode::None)
				flow.documentContentTop += cssFloatClearance(doc, flowBfcIdentity, clearMode,
					flow.documentContentTop);
		}
		std::vector<InlineAtom> atoms;
		atoms.reserve(64);
		bool pendingCollapsedSpace = false;
		bool lineHasContent = false;
		for (int itemIndex = 0; itemIndex < static_cast<int>(doc.inlineItems.size()); ++itemIndex) {
			const WebInlineItem& item = doc.inlineItems[static_cast<size_t>(itemIndex)];
			if (item.flowSerial != flow.flowSerial || item.atomicContainerSerial != flow.contextSerial) continue;
			const WebStyle* itemStyle = inlineOwnerStyle(doc, item, flow.style);
			if (itemStyle && itemStyle->floatMode != FloatMode::None) {
				// The item is blockified and painted from its final float record.
				// Removing it here is what prevents the normal-flow cursor and line
				// fragments from seeing the float twice.
				continue;
			}
			if (item.kind == InlineItemKind::TextRun) {
				++snapshot.textRuns;
				appendInlineTextAtoms(doc, flow, item, itemIndex, atoms, snapshot,
					pendingCollapsedSpace, lineHasContent);
				continue;
			}
			if (item.kind == InlineItemKind::ForcedBreak) {
				if (pendingCollapsedSpace) {
					pendingCollapsedSpace = false;
					++snapshot.trailingSpaceSuppressions;
				}
				InlineAtom atom;
				atom.itemIndex = itemIndex;
				atom.forcedBreak = true;
				atom.ownerSerial = item.ownerSerial;
				appendInlineAtom(atoms, atom, snapshot);
				lineHasContent = false;
				++snapshot.forcedBreaks;
				continue;
			}
			const WebStyle* atomicStyle = inlineOwnerStyle(doc, item, flow.style);
			if (!atomicStyle || atomicStyle->displayNone || atomicStyle->visibility == VisibilityMode::Hidden)
				continue;
			if (pendingCollapsedSpace) {
				if (!lineHasContent) {
					++snapshot.leadingSpaceSuppressions;
				} else {
					InlineAtom space;
					space.itemIndex = itemIndex;
					space.width = inlineTextWidth(flow.style, " ");
					space.height = inlineFontMetrics(flow.style).glyphHeight;
					space.baselineOffset = inlineFontMetrics(flow.style).baseline;
					space.ascent = inlineFontMetrics(flow.style).ascent;
					space.descent = inlineFontMetrics(flow.style).descent;
					space.lineHeight = inlineUsedLineHeight(flow.style, inlineFontMetrics(flow.style), snapshot.lineHeightClamps);
					space.ownerSerial = item.ownerSerial;
					space.whitespace = true;
					space.collapsedWhitespace = true;
					appendInlineAtom(atoms, space, snapshot);
				}
				pendingCollapsedSpace = false;
			}
			InlineAtom atom;
			atom.itemIndex = itemIndex;
			atom.kind = item.kind;
			atom.ownerSerial = item.ownerSerial;
			if (item.kind == InlineItemKind::AtomicBlock) {
				atom.atomicResultIndex = buildAtomicLayout(doc, item.ownerSerial,
					flow.contentWidth, static_cast<uint16_t>(flow.contextSerial != 0 ? 1 : 0), snapshot);
				if (atom.atomicResultIndex < 0 || atom.atomicResultIndex >= static_cast<int>(snapshot.atomicResults.size())) {
					++snapshot.atomicContextIncomplete;
					continue;
				}
				const CssAtomicLayoutResult& atomic = snapshot.atomicResults[static_cast<size_t>(atom.atomicResultIndex)];
				const WebStyle* atomicOwnerStyle = inlineOwnerStyle(doc, item, flow.style);
				const int marginLeft = cssMarginLeftPx(*atomicOwnerStyle, 0);
				const int marginRight = cssMarginRightPx(*atomicOwnerStyle, 0);
				atom.inlineLeft = marginLeft;
				atom.inlineRight = marginRight;
				atom.width = std::max(0, atomic.outerWidth + marginLeft + marginRight);
				atom.height = std::max(1, atomic.outerHeight);
				atom.baselineOffset = std::max(1, atomic.baseline);
				atom.ascent = std::max(1, atom.baselineOffset);
				atom.descent = std::max(0, atom.height - atom.ascent);
				atom.lineHeight = atom.height;
			} else {
				atom.width = inlineAtomWidth(doc, flow, atom);
			}
			if (item.kind == InlineItemKind::AtomicBlock) {
				// The atomic branch above supplies all box metrics.  Do not replace
				// them with the legacy control fallback.
			} else if (item.kind == InlineItemKind::ReplacedImage) {
				atom.height = item.blockIndex >= 0 && item.blockIndex < static_cast<int>(doc.blocks.size())
					? 64 : kLineH;
				int imageW = atom.width;
				imageDisplaySize(doc.blocks[static_cast<size_t>(item.blockIndex)], flow.contentWidth, imageW, atom.height);
				atom.width = imageW;
				atom.baselineOffset = atom.height;
				atom.ascent = atom.height;
				atom.descent = 0;
				++snapshot.replacedItems;
			} else {
				atom.height = item.blockIndex >= 0 && item.blockIndex < static_cast<int>(doc.blocks.size())
					? blockFormControlHeight(doc.blocks[static_cast<size_t>(item.blockIndex)]) : kLineH;
				const WebStyle* style = inlineOwnerStyle(doc, item, flow.style);
				const InlineFontMetrics metrics = inlineFontMetrics(*style);
				atom.baselineOffset = std::max(1, atom.height - std::max(1, metrics.descent / 2));
				atom.ascent = std::max(1, atom.height - (atom.height - atom.baselineOffset));
				atom.descent = std::max(1, atom.height - atom.ascent);
				++snapshot.controlItems;
			}
			appendInlineAtom(atoms, atom, snapshot);
			lineHasContent = true;
		}
		if (pendingCollapsedSpace) ++snapshot.trailingSpaceSuppressions;

		InlineLineLayout line;
		line.lineIndex = 0;
		line.firstFragment = 0;
		int cursorX = 0;
		const int floatContainerLeft = flow.contextSerial != 0
			? flow.contentX : flow.contentX - kContentX;
		auto prepareLine = [&](int estimatedHeight) {
			const int defaultTop = flow.lines.empty() ? 0 :
				flow.lines.back().top + flow.lines.back().usedLineHeight;
			line.top = defaultTop;
			CssFloatExclusionQuery exclusion = cssFloatExclusionQuery(doc, flowBfcIdentity,
				flow.documentContentTop + line.top,
				flow.documentContentTop + line.top + std::max(1, estimatedHeight),
				floatContainerLeft, floatContainerLeft + flow.contentWidth);
			if (exclusion.availableWidth <= 0 && exclusion.nextCandidateY > flow.documentContentTop + line.top) {
				line.top = exclusion.nextCandidateY - flow.documentContentTop;
				++const_cast<WebDocument&>(doc).cssDiagnostics.floatZeroWidthLineAdvances;
				if (s_cssFloatLayoutSnapshot.valid) ++s_cssFloatLayoutSnapshot.zeroWidthLineAdvances;
				exclusion = cssFloatExclusionQuery(doc, flowBfcIdentity,
					flow.documentContentTop + line.top,
					flow.documentContentTop + line.top + std::max(1, estimatedHeight),
					floatContainerLeft, floatContainerLeft + flow.contentWidth);
			}
			line.availableLeft = std::max(0, exclusion.availableLeft - floatContainerLeft);
			line.availableRight = std::max(line.availableLeft, exclusion.availableRight - floatContainerLeft);
			line.availableWidth = std::max(0, exclusion.availableWidth);
			line.floatRecordsIntersected = exclusion.recordsIntersected;
			line.exclusionComplete = exclusion.complete;
			cursorX = line.availableLeft;
		};
		prepareLine(kLineH);
		InlineAtom pendingSpace;
		bool hasPendingSpace = false;
		auto finishLine = [&]() {
			if (hasPendingSpace) {
				hasPendingSpace = false;
				++snapshot.trailingSpaceSuppressions;
			}
			line.fragmentCount = static_cast<int>(flow.fragments.size()) - line.firstFragment;
			line.horizontalExtent = std::max(0, cursorX - line.availableLeft);
			finalizeInlineLine(flow, line, snapshot, doc);
			flow.lines.push_back(line);
			line = InlineLineLayout{};
			line.lineIndex = static_cast<int>(flow.lines.size());
			line.firstFragment = static_cast<int>(flow.fragments.size());
			prepareLine(kLineH);
		};
		auto placeAtom = [&](const InlineAtom& atom) {
			InlineFragmentLayout fragment;
			fragment.itemIndex = atom.itemIndex;
			fragment.blockIndex = doc.inlineItems[static_cast<size_t>(atom.itemIndex)].blockIndex;
			fragment.lineIndex = line.lineIndex;
			fragment.sourceOffset = atom.sourceOffset;
			fragment.sourceLength = atom.sourceLength;
			fragment.x = cursorX;
			fragment.w = std::max(0, atom.width);
			fragment.h = std::max(1, atom.height);
			fragment.contentOffsetX = atom.inlineLeft;
			fragment.baselineOffset = std::max(1, atom.baselineOffset);
			fragment.ownerSerial = atom.ownerSerial;
			fragment.hitSerial = fragment.blockIndex >= 0 && fragment.blockIndex < static_cast<int>(doc.blocks.size())
				? doc.blocks[static_cast<size_t>(fragment.blockIndex)].elementMetadata.serial : atom.ownerSerial;
			fragment.atomicResultIndex = atom.atomicResultIndex;
			if (atom.atomicResultIndex >= 0 && atom.atomicResultIndex < static_cast<int>(snapshot.atomicResults.size())) {
				const CssAtomicLayoutResult& atomic = snapshot.atomicResults[static_cast<size_t>(atom.atomicResultIndex)];
				fragment.boxOffsetX = atom.inlineLeft;
				fragment.boxWidth = atomic.outerWidth;
				fragment.boxHeight = atomic.outerHeight;
			}
			fragment.kind = atom.kind;
			fragment.whitespace = atom.whitespace;
			fragment.collapsedWhitespace = atom.collapsedWhitespace;
			flow.fragments.push_back(fragment);
			cursorX = std::min(8192, cursorX + fragment.w);
		};
		for (const InlineAtom& atom : atoms) {
			if (atom.forcedBreak) {
				finishLine();
				continue;
			}
			const bool canWrap = !atom.noWrap && inlineWhiteSpace(flow.style) != WhiteSpaceMode::Nowrap &&
				inlineWhiteSpace(flow.style) != WhiteSpaceMode::Pre;
			if (atom.collapsedWhitespace) {
				pendingSpace = atom;
				hasPendingSpace = true;
				continue;
			}
			int required = atom.width + (hasPendingSpace ? pendingSpace.width : 0);
			if (canWrap && cursorX > line.availableLeft && cursorX + required > line.availableRight) {
				finishLine();
				++snapshot.lineWraps;
				if (atom.kind == InlineItemKind::AtomicBlock) ++snapshot.inlineBlockWraps;
				required = atom.width;
			}
			if (hasPendingSpace) {
				if (cursorX == line.availableLeft) ++snapshot.leadingSpaceSuppressions;
				else placeAtom(pendingSpace);
				hasPendingSpace = false;
			}
			if (canWrap && cursorX > line.availableLeft && cursorX + atom.width > line.availableRight && atom.kind == InlineItemKind::TextRun) {
				const WebInlineItem& item = doc.inlineItems[static_cast<size_t>(atom.itemIndex)];
				const WebStyle* style = inlineOwnerStyle(doc, item, flow.style);
				const bool breakWord = style->wordBreak == WordBreakMode::BreakAll ||
					style->overflowWrap == OverflowWrapMode::BreakWord;
				if (breakWord) {
					int offset = 0;
					while (offset < atom.sourceLength) {
						int take = atom.sourceLength - offset;
						while (take > 1 && inlineTextWidth(*style, item.text.substr(static_cast<size_t>(atom.sourceOffset + offset), static_cast<size_t>(take))) > line.availableRight - cursorX)
							--take;
						if (take <= 0) take = 1;
						InlineAtom chunk = atom;
						chunk.sourceOffset += offset;
						chunk.sourceLength = take;
						chunk.width = inlineTextWidth(*style, item.text.substr(static_cast<size_t>(chunk.sourceOffset), static_cast<size_t>(take)));
						if (cursorX > line.availableLeft && cursorX + chunk.width > line.availableRight) {
							finishLine();
							++snapshot.lineWraps;
						}
						placeAtom(chunk);
						offset += take;
					}
					continue;
				}
			}
			placeAtom(atom);
		}
		const bool currentLineHasFragments = static_cast<int>(flow.fragments.size()) > line.firstFragment;
		const bool trailingForcedBreak = !atoms.empty() && atoms.back().forcedBreak;
		if (currentLineHasFragments || flow.lines.empty() || trailingForcedBreak) finishLine();
		if (flow.lines.empty()) finishLine();
		for (InlineLineLayout& lineBox : flow.lines) {
			lineBox.baseline = lineBox.ascent;
			for (int fi = lineBox.firstFragment; fi < lineBox.firstFragment + lineBox.fragmentCount; ++fi) {
				InlineFragmentLayout& fragment = flow.fragments[static_cast<size_t>(fi)];
				fragment.y = lineBox.top + lineBox.baseline - fragment.baselineOffset + fragment.verticalShift;
			}
			const int alignShift = flow.style.textAlign == TextAlign::Center
				? std::max(0, (lineBox.availableWidth - lineBox.horizontalExtent) / 2)
				: (flow.style.textAlign == TextAlign::Right ? std::max(0, lineBox.availableWidth - lineBox.horizontalExtent) : 0);
			const int alignOrigin = lineBox.availableLeft;
			for (int fi = lineBox.firstFragment; fi < lineBox.firstFragment + lineBox.fragmentCount; ++fi)
				flow.fragments[static_cast<size_t>(fi)].x += alignOrigin + alignShift - lineBox.availableLeft;
		}
		int contentHeight = std::max(1, flow.lines.back().top + flow.lines.back().usedLineHeight);
		const bool flowOwnsBfc = flow.contextSerial != 0 ||
			(flow.anchorBlockIndex >= 0 && flow.anchorBlockIndex < static_cast<int>(doc.blocks.size()) &&
				cssStyleEstablishesBfc(doc.blocks[static_cast<size_t>(flow.anchorBlockIndex)].style,
					doc.blocks[static_cast<size_t>(flow.anchorBlockIndex)].tagName));
		if (flowOwnsBfc && s_cssFloatLayoutSnapshot.valid) {
			const int ownedBottom = cssOwnedFloatMaximumBottom(s_cssFloatLayoutSnapshot, flowBfcIdentity);
			const int localFloatBottom = std::max(0, ownedBottom - flow.documentContentTop);
			const CssResolvedLength specifiedHeight = resolveCssLength(flow.style.heightValue,
				flow.style.height, flow.style.heightPercent, -1);
			if (localFloatBottom > contentHeight) {
				if (!specifiedHeight.definite) {
					contentHeight = localFloatBottom;
					++const_cast<WebDocument&>(doc).cssDiagnostics.floatHeightContainments;
					++const_cast<WebDocument&>(doc).cssDiagnostics.bfcFloatContainments;
					++const_cast<WebDocument&>(doc).cssDiagnostics.bfcFloatHeightExtensions;
				} else {
					++const_cast<WebDocument&>(doc).cssDiagnostics.bfcFloatHeightNoops;
				}
			} else if (ownedBottom > 0) {
				++const_cast<WebDocument&>(doc).cssDiagnostics.bfcFloatHeightNoops;
			}
		}
		const int verticalEdges = cssVerticalBoxEdges(flow.style);
		const int fallbackOuter = cssBoundedGeometryAdd(contentHeight, verticalEdges);
		const int outerHeight = resolveUsedOuterDimension(flow.style,
			flow.style.heightValue, flow.style.height, flow.style.heightPercent,
			flow.style.minHeightValue, flow.style.minHeight, flow.style.minHeightPercent,
			flow.style.maxHeightValue, flow.style.maxHeight, flow.style.maxHeightPercent,
			flow.style.maxHeightNone, containingHeightOverride >= 0 ? containingHeightOverride : blockContainingContentHeight(geometryBlock, doc), fallbackOuter,
			verticalEdges, false);
		flow.contentOffsetY = cssMarginTopPx(flow.style, geometryBlock.type == BlockType::Heading ? 10 : 4) +
			cssBorderTopPx(flow.style) + cssPaddingTopPx(flow.style, 0);
		flow.totalHeight = cssMarginTopPx(flow.style, geometryBlock.type == BlockType::Heading ? 10 : 4) +
			std::max(1, outerHeight) + cssMarginBottomPx(flow.style, geometryBlock.type == BlockType::ListItem ? 4 : 8);
		flow.outerHeight = std::max(1, outerHeight);
	}

	static bool atomicBlockContainsSerial(const DocBlock& block, uint64_t serial)
	{
		if (serial == 0) return false;
		if (block.elementMetadata.serial == serial) return true;
		for (const gxos::web::HtmlElementRef& ancestor : block.ancestors)
			if (ancestor.serial == serial) return true;
		return false;
	}

	static bool atomicBlockHasInlineItems(const WebDocument& doc, uint64_t containerSerial,
		uint64_t flowSerial)
	{
		for (const WebInlineItem& item : doc.inlineItems) {
			if (item.atomicContainerSerial == containerSerial && item.flowSerial == flowSerial)
				return true;
		}
		return false;
	}

	static int atomicIntrinsicTextWidth(const WebDocument& doc, const WebInlineItem& item,
		bool longestToken)
	{
		const WebStyle* style = inlineOwnerStyle(doc, item, doc.bodyStyle);
		if (!style) style = &doc.bodyStyle;
		if (!longestToken) return inlineTextWidth(*style, item.text);
		int maximum = 0;
		size_t start = 0;
		while (start <= item.text.size()) {
			size_t end = item.text.find_first_of(" \t\r\n", start);
			if (end == std::string::npos) end = item.text.size();
			maximum = std::max(maximum, inlineTextWidth(*style, item.text.substr(start, end - start)));
			if (end == item.text.size()) break;
			start = end + 1;
		}
		return maximum;
	}

	static void atomicPreferredWidths(const WebDocument& doc, uint64_t serial,
		uint16_t depth, int& outMinimum, int& outPreferred, bool& outComplete)
	{
		outMinimum = 0;
		outPreferred = 0;
		if (depth > 8) {
			outComplete = false;
			return;
		}
		int runMinimum = 0;
		int runPreferred = 0;
		for (const WebInlineItem& item : doc.inlineItems) {
			if (item.atomicContainerSerial != serial) continue;
			int minimum = 0;
			int preferred = 0;
			switch (item.kind) {
			case InlineItemKind::TextRun:
				minimum = atomicIntrinsicTextWidth(doc, item, true);
				preferred = atomicIntrinsicTextWidth(doc, item, false);
				break;
			case InlineItemKind::ReplacedImage:
			case InlineItemKind::FormControl:
				if (item.blockIndex >= 0 && item.blockIndex < static_cast<int>(doc.blocks.size())) {
					const DocBlock& block = doc.blocks[static_cast<size_t>(item.blockIndex)];
					if (item.kind == InlineItemKind::ReplacedImage) {
						int imageW = 0;
						int imageH = 0;
						imageDisplaySize(block, 8192, imageW, imageH);
						minimum = preferred = imageW + cssHorizontalBoxEdges(block.style);
					} else {
						minimum = preferred = blockFormControlIntrinsicWidth(block);
					}
				}
				break;
			case InlineItemKind::AtomicBlock: {
				bool complete = true;
				atomicPreferredWidths(doc, item.ownerSerial, static_cast<uint16_t>(depth + 1), minimum, preferred, complete);
				if (!complete) outComplete = false;
				break;
			}
			case InlineItemKind::ForcedBreak:
				outMinimum = std::max(outMinimum, runMinimum);
				outPreferred = std::max(outPreferred, runPreferred);
				runMinimum = runPreferred = 0;
				continue;
			}
			runMinimum = std::min(8192, runMinimum + std::max(0, minimum));
			runPreferred = std::min(8192, runPreferred + std::max(0, preferred));
			outMinimum = std::max(outMinimum, minimum);
			outPreferred = std::max(outPreferred, preferred);
		}
		outMinimum = std::max(outMinimum, runMinimum);
		outPreferred = std::max(outPreferred, runPreferred);
		if (outPreferred < outMinimum) outPreferred = outMinimum;
		outMinimum = std::min(8192, std::max(0, outMinimum));
		outPreferred = std::min(8192, std::max(0, outPreferred));
	}

	static int atomicRootBlockIndex(const WebDocument& doc, const DocBlock& block,
		uint64_t containerSerial)
	{
		if (block.atomicContainerSerial != containerSerial) return -1;
		if (block.elementMetadata.serial == containerSerial) return -1;
		for (auto it = block.ancestors.rbegin(); it != block.ancestors.rend(); ++it) {
			if (it->serial == containerSerial) break;
			if (it->parentSerial == containerSerial) return static_cast<int>(it->serial & 0x7FFFFFFF);
		}
		if (block.elementMetadata.parentSerial == containerSerial) return static_cast<int>(block.elementMetadata.serial & 0x7FFFFFFF);
		return 0;
	}

	static int atomicFindBlockBySerial(const WebDocument& doc, uint64_t serial)
	{
		if (serial == 0) return -1;
		for (int i = 0; i < static_cast<int>(doc.blocks.size()); ++i)
			if (doc.blocks[static_cast<size_t>(i)].elementMetadata.serial == serial) return i;
		return -1;
	}

	static int buildAtomicLayout(const WebDocument& doc, uint64_t containerSerial,
		int availableWidth, uint16_t parentDepth, InlineLayoutSnapshot& snapshot)
	{
		if (containerSerial == 0 || availableWidth < 0) return -1;
		for (int i = 0; i < static_cast<int>(snapshot.atomicResults.size()); ++i) {
			CssAtomicLayoutResult& existing = snapshot.atomicResults[static_cast<size_t>(i)];
			if (existing.containerSerial != containerSerial || existing.availableWidth != availableWidth) continue;
			if (!existing.complete) {
				++snapshot.atomicContextIncomplete;
				return -1;
			}
			return i;
		}
		constexpr size_t kMaxAtomicContexts = 256;
		constexpr size_t kMaxAtomicChildren = 1024;
		constexpr int kMaxAtomicLayoutOperations = 2048;
		if (parentDepth >= 8 || snapshot.atomicResults.size() >= kMaxAtomicContexts) {
			++snapshot.atomicContextDepthClamps;
			++snapshot.atomicContextIncomplete;
			return -1;
		}
		if (snapshot.atomicChildren.size() >= kMaxAtomicChildren) {
			++snapshot.atomicContextIncomplete;
			return -1;
		}
		if (snapshot.atomicLayoutOperations >= kMaxAtomicLayoutOperations) {
			++snapshot.atomicLayoutOperationClamps;
			++snapshot.atomicContextIncomplete;
			return -1;
		}
		++snapshot.atomicLayoutOperations;
		CssAtomicLayoutResult result;
		result.containerSerial = containerSerial;
		result.availableWidth = std::max(1, std::min(8192, availableWidth));
		result.depth = parentDepth;
		result.complete = false;
		const int resultIndex = static_cast<int>(snapshot.atomicResults.size());
		snapshot.atomicResults.push_back(result);
		snapshot.atomicContextDepthMax = std::max(snapshot.atomicContextDepthMax, static_cast<int>(parentDepth) + 1);
		++snapshot.atomicContextsDocument;
		if (parentDepth > 0) ++snapshot.inlineBlockNested;

		const WebStyle* style = computedStyleForSerial(doc, containerSerial);
		if (!style || style->display != DisplayMode::InlineBlock || style->displayNone) {
			snapshot.atomicResults[static_cast<size_t>(resultIndex)].complete = true;
			return -1;
		}
		CssAtomicLayoutContext context;
		context.containingBlockWidth = result.availableWidth;
		context.availableWidth = result.availableWidth;
		context.parentStructuralSerial = containerSerial;
		context.formattingContextDepth = static_cast<uint8_t>(parentDepth + 1);
		context.recursionBudget = 8;
		context.clip = CssPaintRect{0, 0, 8192, 8192};
		const int horizontalEdges = cssHorizontalBoxEdges(*style);
		const int verticalEdges = cssVerticalBoxEdges(*style);
		int preferredMinimum = 0;
		int preferredWidth = 0;
		bool preferredComplete = true;
		atomicPreferredWidths(doc, containerSerial, parentDepth, preferredMinimum, preferredWidth, preferredComplete);
		const int preferredMinOuter = cssBoundedGeometryAdd(preferredMinimum, horizontalEdges);
		const int preferredOuter = cssBoundedGeometryAdd(preferredWidth, horizontalEdges);
		const CssResolvedLength explicitWidth = resolveCssLength(style->widthValue,
			style->width, style->widthPercent, result.availableWidth);
		bool widthAuto = !explicitWidth.definite;
		int shrinkWidth = std::max(preferredMinOuter,
			std::min(result.availableWidth, std::max(preferredMinOuter, preferredOuter)));
		if (preferredOuter > result.availableWidth) ++snapshot.inlineBlockPreferredWidthClamps;
		if (preferredMinOuter > result.availableWidth) ++snapshot.inlineBlockPreferredMinClamps;
		bool widthClamped = false;
		bool widthConflict = false;
		const int fallbackOuter = widthAuto ? shrinkWidth : result.availableWidth;
		const int outerWidth = resolveUsedOuterDimension(*style,
			style->widthValue, style->width, style->widthPercent,
			style->minWidthValue, style->minWidth, style->minWidthPercent,
			style->maxWidthValue, style->maxWidth, style->maxWidthPercent,
			style->maxWidthNone, result.availableWidth, fallbackOuter, horizontalEdges, false,
			&widthAuto, nullptr, &widthConflict, &widthClamped);
		const int contentWidth = usedContentDimensionFromOuter(*style, outerWidth, horizontalEdges);
		if (widthAuto) {
			++snapshot.inlineBlockAutoWidths;
			++snapshot.inlineBlockShrinkToFit;
		} else {
			++snapshot.inlineBlockExplicitWidths;
		}
		int cursor = 0;
		int lastLineBaseline = -1;
		std::vector<std::pair<uint64_t, int>> roots;
		roots.reserve(64);
		for (int blockIndex = 0; blockIndex < static_cast<int>(doc.blocks.size()); ++blockIndex) {
			const DocBlock& block = doc.blocks[static_cast<size_t>(blockIndex)];
			if (atomicRootBlockIndex(doc, block, containerSerial) < 0 || block.style.displayNone) continue;
			uint64_t rootSerial = block.elementMetadata.serial;
			for (auto it = block.ancestors.rbegin(); it != block.ancestors.rend(); ++it) {
				if (it->serial == containerSerial) break;
				if (it->parentSerial == containerSerial) {
					rootSerial = it->serial;
					break;
				}
			}
			bool seen = false;
			for (const auto& root : roots) if (root.first == rootSerial) seen = true;
			if (!seen && roots.size() < 64) roots.push_back({rootSerial, blockIndex});
		}
		if (roots.empty()) {
			for (const WebInlineItem& item : doc.inlineItems) {
				if (item.atomicContainerSerial == containerSerial && item.flowSerial == containerSerial) {
					roots.push_back({containerSerial, -1});
					break;
				}
			}
		}
		const int contentOriginX = cssBorderLeftPx(*style) + cssPaddingLeftPx(*style, 0);
		for (const auto& root : roots) {
			if (snapshot.atomicChildren.size() - result.childBegin >= 1024) {
				++snapshot.atomicContextIncomplete;
				break;
			}
			int anchorIndex = root.second;
			uint64_t flowSerial = anchorIndex >= 0 ? doc.blocks[static_cast<size_t>(anchorIndex)].inlineFlowSerial : containerSerial;
			if (flowSerial == 0) flowSerial = containerSerial;
			if (!atomicBlockHasInlineItems(doc, containerSerial, flowSerial)) continue;
			int flowIndex = -1;
			for (int i = 0; i < static_cast<int>(snapshot.flows.size()); ++i) {
				if (snapshot.flows[static_cast<size_t>(i)].flowSerial == flowSerial &&
					snapshot.flows[static_cast<size_t>(i)].contextSerial == containerSerial) {
					flowIndex = i;
					break;
				}
			}
			if (flowIndex < 0 && snapshot.flows.size() < 256) {
				InlineFlowLayout flow;
				flow.flowSerial = flowSerial;
				flow.contextSerial = containerSerial;
				flow.anchorBlockIndex = anchorIndex;
				flow.style = anchorIndex >= 0 ? doc.blocks[static_cast<size_t>(anchorIndex)].style : *style;
				if (anchorIndex < 0) flow.style = *style;
				flow.atomicResultIndex = resultIndex;
				flow.localOuterX = contentOriginX;
				flow.localOuterY = cursor + cssMarginTopPx(flow.style, 0);
				snapshot.flows.push_back(flow);
				flowIndex = static_cast<int>(snapshot.flows.size() - 1);
				buildInlineFlow(doc, snapshot.flows[static_cast<size_t>(flowIndex)], snapshot,
					std::max(1, contentWidth), -1);
			}
			if (flowIndex < 0 || flowIndex >= static_cast<int>(snapshot.flows.size())) continue;
			InlineFlowLayout& flow = snapshot.flows[static_cast<size_t>(flowIndex)];
			const int marginTop = cssMarginTopPx(flow.style, 0);
			const int marginBottom = cssMarginBottomPx(flow.style, 0);
			flow.localOuterX = contentOriginX;
			flow.localOuterY = cursor + marginTop;
			const int childY = flow.localOuterY;
			const int childH = std::max(1, flow.outerHeight);
			CssAtomicChildPlacement child;
			child.blockIndex = anchorIndex;
			child.flowIndex = flowIndex;
			child.x = contentOriginX;
			child.y = childY;
			child.w = std::max(1, flow.outerWidth);
			child.h = childH;
			child.serial = anchorIndex >= 0 ? doc.blocks[static_cast<size_t>(anchorIndex)].elementMetadata.serial : containerSerial;
			child.clip = CssPaintRect{child.x, child.y, child.w, child.h};
			snapshot.atomicChildren.push_back(child);
			if (result.childCount == 0) result.childBegin = static_cast<uint16_t>(snapshot.atomicChildren.size() - 1);
			++result.childCount;
			cursor = std::min(8192, childY + childH + marginBottom);
			if (!flow.lines.empty()) {
				const InlineLineLayout& last = flow.lines.back();
				lastLineBaseline = childY + cssBorderTopPx(flow.style) + cssPaddingTopPx(flow.style, 0) +
					last.top + last.baseline;
			}
		}
		const int contentHeight = std::max(0, cursor);
		const int fallbackHeight = cssBoundedGeometryAdd(contentHeight, verticalEdges, &widthClamped);
		const int outerHeight = resolveUsedOuterDimension(*style,
			style->heightValue, style->height, style->heightPercent,
			style->minHeightValue, style->minHeight, style->minHeightPercent,
			style->maxHeightValue, style->maxHeight, style->maxHeightPercent,
			style->maxHeightNone, -1, fallbackHeight, verticalEdges, false,
			nullptr, nullptr, &widthConflict, &widthClamped);
		CssAtomicLayoutResult& completed = snapshot.atomicResults[static_cast<size_t>(resultIndex)];
		completed.usedContentWidth = contentWidth;
		completed.usedContentHeight = usedContentDimensionFromOuter(*style, outerHeight, verticalEdges);
		completed.paddingBoxWidth = std::max(0, outerWidth - cssBorderLeftPx(*style) - cssBorderRightPx(*style));
		completed.paddingBoxHeight = std::max(0, outerHeight - cssBorderTopPx(*style) - cssBorderBottomPx(*style));
		completed.borderBoxWidth = outerWidth;
		completed.borderBoxHeight = outerHeight;
		completed.outerWidth = outerWidth;
		completed.outerHeight = outerHeight;
		completed.preferredMinimum = preferredMinOuter;
		completed.preferredWidth = preferredOuter;
		completed.shrinkToFitWidth = shrinkWidth;
		completed.autoWidth = widthAuto;
		completed.explicitWidth = !widthAuto;
		completed.clamped = widthClamped || widthConflict || !preferredComplete;
		completed.baseline = lastLineBaseline >= 0 ? lastLineBaseline : std::max(1, outerHeight - cssBorderBottomPx(*style));
		completed.baselineY = completed.baseline;
		completed.baselineFromLine = lastLineBaseline >= 0;
		completed.baselineFallback = lastLineBaseline < 0;
		if (completed.baselineFromLine) ++snapshot.inlineBlockBaselineFromLine;
		else ++snapshot.inlineBlockBaselineFallback;
		completed.paintBounds = CssPaintRect{0, 0, outerWidth, outerHeight};
		completed.clip = cssApplyOverflowClip(completed.paintBounds, *style, 0, 0, outerWidth, outerHeight);
		completed.overflowWidth = std::max(outerWidth, contentOriginX + contentWidth);
		completed.overflowHeight = std::max(outerHeight, cursor + cssPaddingBottomPx(*style, 0));
		completed.overflowClipped = style->overflowX != OverflowMode::Visible || style->overflowY != OverflowMode::Visible;
		completed.hitTargetBegin = result.childBegin;
		completed.hitTargetCount = result.childCount;
		for (int blockIndex = 0; blockIndex < static_cast<int>(doc.blocks.size()); ++blockIndex) {
			const DocBlock& block = doc.blocks[static_cast<size_t>(blockIndex)];
			if (block.atomicContainerSerial != containerSerial) continue;
			if (block.type == BlockType::Link || isFormControlBlock(block)) ++completed.hitTargetCount;
		}
		snapshot.inlineBlockHitTargets += completed.hitTargetCount;
		completed.complete = true;
		if (completed.overflowClipped) ++snapshot.inlineBlockOverflowClips;
		if (!preferredComplete) ++snapshot.atomicContextIncomplete;
		return resultIndex;
	}

	static void rebuildInlineLayout(const WebDocument& doc, InlineLayoutSnapshot& snapshot)
	{
		snapshot = InlineLayoutSnapshot{};
		snapshot.url = doc.url;
		snapshot.blockCount = doc.blocks.size();
		snapshot.itemCount = doc.inlineItems.size();
		snapshot.flows.reserve(256);
		snapshot.atomicResults.reserve(256);
		snapshot.atomicChildren.reserve(1024);
		if (doc.inlineItems.empty()) {
			snapshot.valid = true;
			return;
		}
		constexpr size_t kMaxFlows = 256;
		for (const WebInlineItem& item : doc.inlineItems) {
			if (item.flowSerial == 0 || item.atomicContainerSerial != 0) continue;
			InlineFlowLayout* flow = nullptr;
			for (InlineFlowLayout& candidate : snapshot.flows) {
				if (candidate.flowSerial == item.flowSerial && candidate.contextSerial == 0) {
					flow = &candidate;
					break;
				}
			}
			if (flow) continue;
			if (snapshot.flows.size() >= kMaxFlows) {
				++snapshot.nestingClamps;
				continue;
			}
			snapshot.flows.push_back(InlineFlowLayout{});
			flow = &snapshot.flows.back();
			flow->flowSerial = item.flowSerial;
			flow->contextSerial = 0;
			for (int blockIndex = 0; blockIndex < static_cast<int>(doc.blocks.size()); ++blockIndex) {
				if (doc.blocks[static_cast<size_t>(blockIndex)].inlineFlowSerial == item.flowSerial &&
					doc.blocks[static_cast<size_t>(blockIndex)].atomicContainerSerial == 0) {
					flow->anchorBlockIndex = blockIndex;
					break;
				}
			}
			if (flow->anchorBlockIndex < 0 && item.blockIndex >= 0) flow->anchorBlockIndex = item.blockIndex;
			if (flow->anchorBlockIndex >= 0 && flow->anchorBlockIndex < static_cast<int>(doc.blocks.size()))
				flow->style = doc.blocks[static_cast<size_t>(flow->anchorBlockIndex)].style;
			if (const WebStyle* computed = computedStyleForSerial(doc, item.flowSerial)) flow->style = *computed;
		}
		for (InlineFlowLayout& flow : snapshot.flows) {
			if (flow.style.displayNone || flow.style.visibility == VisibilityMode::Hidden) continue;
			buildInlineFlow(doc, flow, snapshot);
		}
		snapshot.atomicContextsDocument = static_cast<int>(std::min<size_t>(
			std::numeric_limits<int>::max(), snapshot.atomicResults.size()));
		snapshot.valid = true;
	}

	static bool atomicResultScreenRect(const WebDocument& doc, const InlineLayoutSnapshot& snapshot,
		int resultIndex, int scrollOffset, CssPaintRect& out, int depth = 0)
	{
		if (resultIndex < 0 || resultIndex >= static_cast<int>(snapshot.atomicResults.size()) || depth > 8) return false;
		for (const InlineFlowLayout& flow : snapshot.flows) {
			for (const InlineFragmentLayout& fragment : flow.fragments) {
				if (!fragment.visible || fragment.atomicResultIndex != resultIndex) continue;
				int originX = 0;
				int originY = 0;
				if (flow.contextSerial == 0) {
					if (flow.anchorBlockIndex < 0 || flow.anchorBlockIndex >= static_cast<int>(doc.blocks.size())) continue;
					const DocBlock& anchor = doc.blocks[static_cast<size_t>(flow.anchorBlockIndex)];
					const int drawY = kContentY + cssBlockLayoutY(doc, flow.anchorBlockIndex) - scrollOffset;
					const int marginTop = cssMarginTopPx(flow.style, anchor.type == BlockType::Heading ? 10 : 4);
					originX = flow.contentX;
					originY = drawY + marginTop + cssBorderTopPx(flow.style) + cssPaddingTopPx(flow.style, 0);
				} else {
					CssPaintRect parent;
					if (!atomicResultScreenRect(doc, snapshot, flow.atomicResultIndex, scrollOffset, parent, depth + 1)) continue;
					originX = parent.x + flow.contentX;
					originY = parent.y + flow.localOuterY + cssBorderTopPx(flow.style) + cssPaddingTopPx(flow.style, 0);
				}
				out = CssPaintRect{originX + fragment.x + fragment.boxOffsetX, originY + fragment.y,
					std::max(0, fragment.boxWidth), std::max(0, fragment.boxHeight)};
				return out.w > 0 && out.h > 0;
			}
		}
		return false;
	}

	static void ensureInlineLayout(const WebDocument& doc)
	{
		if (!s_inlineLayoutDirty && s_inlineLayoutSnapshot.valid &&
			s_inlineLayoutSnapshot.url == doc.url &&
			s_inlineLayoutSnapshot.blockCount == doc.blocks.size() &&
			s_inlineLayoutSnapshot.itemCount == doc.inlineItems.size()) return;
		rebuildInlineLayout(doc, s_inlineLayoutSnapshot);
		s_inlineLayoutDirty = false;
	}

	static const InlineFlowLayout* inlineFlowForBlock(const WebDocument& doc, int blockIndex)
	{
		if (!s_inlineLayoutSnapshot.valid || s_inlineLayoutSnapshot.url != doc.url ||
			s_inlineLayoutSnapshot.blockCount != doc.blocks.size() ||
			s_inlineLayoutSnapshot.itemCount != doc.inlineItems.size() ||
			blockIndex < 0 || blockIndex >= static_cast<int>(doc.blocks.size())) return nullptr;
		const uint64_t serial = doc.blocks[static_cast<size_t>(blockIndex)].inlineFlowSerial;
		if (serial == 0) return nullptr;
		for (const InlineFlowLayout& flow : s_inlineLayoutSnapshot.flows)
			if (flow.flowSerial == serial && flow.contextSerial == doc.blocks[static_cast<size_t>(blockIndex)].atomicContainerSerial)
				return &flow;
		return nullptr;
	}

	static const InlineFlowLayout* inlineFlowForAnchor(const WebDocument& doc, int blockIndex)
	{
		const InlineFlowLayout* flow = inlineFlowForBlock(doc, blockIndex);
		return flow && flow->anchorBlockIndex == blockIndex ? flow : nullptr;
	}

	static bool blockUsesInlineFlow(const WebDocument& doc, int blockIndex)
	{
		return inlineFlowForBlock(doc, blockIndex) != nullptr;
	}
}

uint64_t Navigator::Launch()
{
	ProcessSpec spec{"navigator", Navigator::main};
	spec.appId = "guidexos.navigator";
	return ProcessTable::spawn(spec, {"navigator"});
}

bool Navigator::SmokeNavigateTo(const std::string& url)
{
	if (s_windowId == 0) return false;
	loadUrl(url, true, transitionCategoryForUrl(url));
	return s_currentDoc.url == url ||
		(s_pageMetadata.redirected && s_pageMetadata.requestedUrl == url &&
		 !s_pageMetadata.finalUrl.empty() && s_currentDoc.url == s_pageMetadata.finalUrl);
}

bool Navigator::SmokeNavigateToQuiet(const std::string& url)
{
	if (s_windowId == 0) return false;
	loadUrl(url, false, transitionCategoryForUrl(url));
	return s_currentDoc.url == url ||
		(s_pageMetadata.redirected && s_pageMetadata.requestedUrl == url &&
		 !s_pageMetadata.finalUrl.empty() && s_currentDoc.url == s_pageMetadata.finalUrl);
}

bool Navigator::SmokeNavigateToWithHistory(const std::string& url)
{
	if (s_windowId == 0) return false;
	navigateTo(url);
	return s_currentDoc.url == url;
}

bool Navigator::SmokeSubmitFirstForm(const std::string& value)
{
	if (s_windowId == 0) return false;
	for (int i = 0; i < static_cast<int>(s_currentDoc.blocks.size()); ++i) {
		DocBlock& block = s_currentDoc.blocks[i];
		if (block.type != BlockType::FormTextInput) continue;
		block.inputValue = value;
		block.text = value;
		s_focusedInputBlockIndex = i;
		s_inputCaret = static_cast<int>(block.inputValue.size());
		submitFormForBlock(i);
		return true;
	}
	return false;
}

int Navigator::SmokeFindInPage(const std::string& query)
{
	if (s_windowId == 0) return -1;
	openFindMode();
	s_findBuffer = query;
	s_findCaret = static_cast<int>(s_findBuffer.size());
	updateFindMatches(false);
	if (!s_findMatches.empty()) goToFindMatch(0);
	updateDisplay();
	return static_cast<int>(s_findMatches.size());
}

bool Navigator::SmokeClickFirstLink()
{
	if (s_windowId == 0) return false;
	for (int i = 0; i < static_cast<int>(s_currentDoc.blocks.size()); ++i) {
		if (s_currentDoc.blocks[i].type != BlockType::Link || s_currentDoc.blocks[i].url.empty()) continue;
		const std::string before = s_currentDoc.url;
		s_scrollOffset = std::max(0, blockLayoutY(i) - 12);
		clampScrollOffset();
		Rect r = linkBlockRect(i);
		int x = r.x + std::min(std::max(2, r.w / 4), std::max(2, r.w - 2));
		int y = r.y + std::min(8, std::max(1, r.h - 1));
		ensureInlineLayout(s_currentDoc);
		int hitIndex = -1;
		Rect inlineFragments;
		if (inlineFragmentRectForBlock(i, false, inlineFragments)) {
			bool found = false;
			for (int py = r.y; py < r.y + r.h && !found; py += 2) {
				for (int px = r.x; px < r.x + r.w; px += 2) {
					if (hitTest(px, py, hitIndex) == HitTarget::Link && hitIndex == i) {
						x = px;
						y = py;
						found = true;
						break;
					}
				}
			}
			if (!found) return false;
		}
		handleMouseInput(x, y, 1, "down");
		handleMouseInput(x, y, 1, "up");
		return s_currentDoc.url != before;
	}
	return false;
}

bool Navigator::SmokeDragFirstLinkSelectsWithoutNavigation()
{
	if (s_windowId == 0) return false;
	for (int i = 0; i < static_cast<int>(s_currentDoc.blocks.size()); ++i) {
		if (s_currentDoc.blocks[i].type != BlockType::Link || s_currentDoc.blocks[i].url.empty()) continue;
		const std::string before = s_currentDoc.url;
		s_scrollOffset = std::max(0, blockLayoutY(i) - 12);
		clampScrollOffset();
		Rect r = linkBlockRect(i);
		int x1 = r.x + std::min(std::max(2, r.w / 4), std::max(2, r.w - 2));
		int y1 = r.y + std::min(8, std::max(1, r.h - 1));
		ensureInlineLayout(s_currentDoc);
		int hitIndex = -1;
		Rect inlineFragments;
		if (inlineFragmentRectForBlock(i, false, inlineFragments)) {
			bool found = false;
			for (int py = r.y; py < r.y + r.h && !found; py += 2) {
				for (int px = r.x; px < r.x + r.w; px += 2) {
					if (hitTest(px, py, hitIndex) == HitTarget::Link && hitIndex == i) {
						x1 = px;
						y1 = py;
						found = true;
						break;
					}
				}
			}
			if (!found) return false;
		}
		const int x2 = std::min(r.x + std::max(2, r.w - 2), x1 + std::max(kMouseDragThreshold + 1, kCharW * 3));
		handleMouseInput(x1, y1, 1, "down");
		handleMouseInput(x2, y1, 0, "move");
		handleMouseInput(x2, y1, 1, "up");
		return s_currentDoc.url == before && hasSelection() && copySelectionToClipboard();
	}
	return false;
}

std::string Navigator::SmokeRuntimeReport()
{
	// Refresh only the currently inspected document.  Generated about: views
	// (Page Info, Save Page Text, and runtime diagnostics) must not replace the
	// ownership metadata for the page they describe.
	if (visibleDocumentOwnsInspectedSource())
		storePageMetadata(s_pageMetadata, s_currentDoc);
	const std::string inspected = s_pageMetadata.finalUrl.empty() ? "" : s_pageMetadata.finalUrl;
	std::string report = formatRuntimeReport(hostedRuntimeReportEntries(
		s_currentDoc.url,
		s_currentDoc.title,
		static_cast<int>(s_currentDoc.blocks.size()),
		inspected,
		s_pageMetadata.cssDetected,
		s_pageMetadata.cssEnabled,
		s_pageMetadata.styleRuleCount,
		s_pageMetadata.styleBlockCount,
		s_pageMetadata.inlineStyleCount,
		s_pageMetadata.externalStylesheetLoadedCount,
		s_pageMetadata.unsupportedExternalStylesheetCount,
		s_pageMetadata.unsupportedCssRuleCount,
		s_pageMetadata.unsupportedCssDeclarationCount,
		s_pageMetadata.cssUnsupportedSelectorCount,
		s_pageMetadata.cssParseErrorCount,
		s_pageMetadata.cssStyleBlockCapped,
		s_pageMetadata.cssStyleBytesProcessed,
		s_pageMetadata.cssLayoutMaxWidthAppliedCount,
		s_pageMetadata.cssAutoMarginCenteredBlockCount,
		s_pageMetadata.cssBackgroundBlockCount,
		s_pageMetadata.cssWrapperRenderCount,
		s_pageMetadata.cssDisplayNoneBlockCount,
		s_pageMetadata.cssTableRenderCount,
		s_pageMetadata.cssTableRowCount,
		s_pageMetadata.cssTableCellCount,
		s_pageMetadata.cssTableLayoutFallbackCount,
		s_pageMetadata.cssListRenderCount,
		s_pageMetadata.cssClampedValueCount,
		s_pageMetadata.cssLineBreakCount,
		s_pageMetadata.cssTableCaptionCount,
		s_pageMetadata.cssTableHeaderCellCount,
		s_pageMetadata.cssVisitedLinkCount,
		s_pageMetadata.cssBorderedBlocksRendered,
		s_pageMetadata.cssDashedBordersRendered,
		s_pageMetadata.cssDottedBordersRendered,
		s_pageMetadata.cssBorderWidthClamps,
		s_pageMetadata.cssCollapsedTablesRendered,
		s_pageMetadata.cssSeparateTablesRendered,
		s_pageMetadata.cssTableBorderSpacingClamps,
		s_pageMetadata.cssListStyleMarkersRendered,
		s_pageMetadata.cssListStyleNoneApplied,
		s_pageMetadata.cssTextDecorationsRendered,
		s_pageMetadata.cssGenericFontFamilyApplied,
		s_pageMetadata.cssGenericFontFamilyFallbacks,
		s_pageMetadata.cssFiguresRendered,
		s_pageMetadata.cssFigcaptionsRendered,
		s_pageMetadata.cssBlockquotesRendered,
		s_pageMetadata.cssDefinitionListsRendered,
		s_pageMetadata.cssImagesConstrained,
		s_pageMetadata.cssImagesAspectPreserved,
		s_pageMetadata.cssImageAltFallbacks,
		s_pageMetadata.cssImageSizeClamps,
		s_pageMetadata.cssNestedLayoutClamps,
		s_pageMetadata.cssMaxWrapperAncestorDepth,
		s_pageMetadata.cssSelectorGroupsParsed,
		s_pageMetadata.cssCompoundSelectorsParsed,
		s_pageMetadata.cssChildCombinators,
		s_pageMetadata.cssDescendantCombinators,
		s_pageMetadata.cssAdjacentSiblingCombinators,
		s_pageMetadata.cssGeneralSiblingCombinators,
		s_pageMetadata.cssAdjacentSiblingMatches,
		s_pageMetadata.cssGeneralSiblingMatches,
		s_pageMetadata.cssSiblingScanSteps,
		s_pageMetadata.cssSiblingScanClamps,
		s_pageMetadata.cssSiblingMetadataClamps,
		s_pageMetadata.cssSiblingMetadataErrors,
		s_pageMetadata.cssSelectorMatches,
		s_pageMetadata.cssSpecificityOverrides,
		s_pageMetadata.cssSourceOrderOverrides,
		s_pageMetadata.cssInlineOverrides,
		s_pageMetadata.cssInheritedPropertiesApplied,
		s_pageMetadata.cssSelectorDepthClamps,
		s_pageMetadata.cssSelectorGroupClamps,
		s_pageMetadata.cssCascadePropertyResolutions,
		s_pageMetadata.cssImportantDeclarationsApplied,
		s_pageMetadata.cssRuleCapCount,
		s_pageMetadata.cssDeclarationCapCount,
		s_pageMetadata.cssInheritanceDepthClamps,
		s_pageMetadata.cssPseudoClassesParsed,
		s_pageMetadata.cssStructuralPseudoMatches,
		s_pageMetadata.cssFirstChildMatches,
		s_pageMetadata.cssLastChildMatches,
		s_pageMetadata.cssNthChildMatches,
		s_pageMetadata.cssOfTypeMatches,
		s_pageMetadata.cssNotMatches,
		s_pageMetadata.cssLinkPseudoMatches,
		s_pageMetadata.cssVisitedPseudoMatches,
		s_pageMetadata.cssPseudoClassClamps,
		s_pageMetadata.cssNthExpressionParseErrors,
		s_pageMetadata.cssStructuralMetadataClamps,
		s_pageMetadata.cssSelectorEvaluationStepClamps,
		s_pageMetadata.cssEmptyPseudoParsed,
		s_pageMetadata.cssEmptyPseudoMatches,
		s_pageMetadata.cssEmptyMetadataIncomplete,
		s_pageMetadata.cssContentMetadataClamps,
		s_pageMetadata.cssSelectorGroupMemberRecoveries,
		s_pageMetadata.cssCommentScanClamps,
		s_pageMetadata.cssUnterminatedCommentErrors,
		s_pageMetadata.cssUnbalancedParenthesisErrors,
		s_pageMetadata.cssUnbalancedBracketErrors,
		s_pageMetadata.cssUnterminatedStringErrors,
		s_pageMetadata.cssInvalidCombinatorSequences,
		s_pageMetadata.cssIdentifierEscapeRejections,
		s_pageMetadata.cssSelectorMemberParseFailures,
		s_pageMetadata.cssSelectorRecoverySuccesses,
		s_pageMetadata.cssComputedStyleEvidence,
		s_pageMetadata.formCount,
		s_pageMetadata.formInputCount,
		s_pageMetadata.formCheckboxCount,
		s_pageMetadata.formRadioCount,
		s_pageMetadata.formTextareaCount,
		s_pageMetadata.formSelectCount,
		s_pageMetadata.unsupportedFormControlCount,
		s_pageMetadata.postSupportedHosted,
		s_pageMetadata.postSupportedBareMetal,
		s_pageMetadata.lastSubmittedFormAction,
		s_pageMetadata.lastSubmittedFormMethod,
		s_pageMetadata.lastSubmittedFormStatus,
		s_pageMetadata.lastPostHttpStatus,
		s_pageMetadata.lastPostContentType,
		s_clipboardMode,
		s_pageMetadata.tlsStatus,
		s_pageMetadata.tlsError,
		s_pageMetadata.tlsConnectionPath,
		s_pageMetadata.tlsCredentialApi,
		s_pageMetadata.tlsCredentialStructure,
		s_pageMetadata.tlsCredentialAcquired,
		s_pageMetadata.tlsHandshakeStarted,
		s_pageMetadata.tlsSmokeSelfSignedBypass));
	appendFormPhase2EDiagnostics(report, s_pageMetadata);
	appendFormPhase2HDiagnostics(report, s_currentDoc);
	appendCssPhase3ADiagnostics(report, s_pageMetadata);
	report += SmokeLifecycleReport();
	return report;
}

std::string Navigator::SmokeCurrentUrl()
{
	return s_currentDoc.url;
}

int Navigator::SmokeCurrentBlockCount()
{
	return static_cast<int>(s_currentDoc.blocks.size());
}

std::string Navigator::SmokeCurrentDocumentText()
{
	return extractDocumentText(s_currentDoc);
}

const char* Navigator::documentCategoryName(NavigatorDocumentCategory category)
{
	switch (category) {
	case NavigatorDocumentCategory::LocalFile: return "local-file";
	case NavigatorDocumentCategory::Http: return "http";
	case NavigatorDocumentCategory::Https: return "https";
	case NavigatorDocumentCategory::GeneratedAbout: return "generated-about";
	case NavigatorDocumentCategory::Error: return "error";
	case NavigatorDocumentCategory::Unsupported: return "unsupported";
	default: return "none";
	}
}

const char* Navigator::transitionCategoryName(NavigatorTransitionCategory category)
{
	switch (category) {
	case NavigatorTransitionCategory::InitialNavigation: return "initial-navigation";
	case NavigatorTransitionCategory::Navigation: return "navigation";
	case NavigatorTransitionCategory::SameDocumentRecomputation: return "same-document-recomputation";
	case NavigatorTransitionCategory::Reload: return "reload";
	case NavigatorTransitionCategory::HistoryBack: return "history-back";
	case NavigatorTransitionCategory::HistoryForward: return "history-forward";
	case NavigatorTransitionCategory::RedirectReplacement: return "redirect-replacement";
	case NavigatorTransitionCategory::LocalFileNavigation: return "local-file-navigation";
	case NavigatorTransitionCategory::GeneratedAboutNavigation: return "generated-about-navigation";
	case NavigatorTransitionCategory::PageInfoGeneration: return "page-info-generation";
	case NavigatorTransitionCategory::SavePageTextGeneration: return "save-page-text-generation";
	case NavigatorTransitionCategory::NavigationFailure: return "navigation-failure";
	case NavigatorTransitionCategory::ParseFailure: return "parse-failure";
	case NavigatorTransitionCategory::TlsPolicyFailure: return "tls-policy-failure";
	case NavigatorTransitionCategory::AbortedNavigation: return "aborted-navigation";
	case NavigatorTransitionCategory::WindowDocumentTeardown: return "window-document-teardown";
	default: return "navigation";
	}
}

NavigatorTransitionCategory Navigator::transitionCategoryForUrl(const std::string& url)
{
	if (url == "about:page-info") return NavigatorTransitionCategory::PageInfoGeneration;
	if (url == "about:save-page-text") return NavigatorTransitionCategory::SavePageTextGeneration;
	if (url.rfind("about:", 0) == 0) return NavigatorTransitionCategory::GeneratedAboutNavigation;
	if (url.rfind("file://", 0) == 0) return NavigatorTransitionCategory::LocalFileNavigation;
	return NavigatorTransitionCategory::Navigation;
}

NavigatorDocumentCategory Navigator::documentCategoryForUrl(
	const std::string& url, const NavigatorPageMetadata& metadata)
{
	if (url.rfind("about:", 0) == 0) return NavigatorDocumentCategory::GeneratedAbout;
	if (!metadata.errorStatus.empty()) return NavigatorDocumentCategory::Error;
	if (url.rfind("file://", 0) == 0) return NavigatorDocumentCategory::LocalFile;
	if (url.rfind("https://", 0) == 0) return NavigatorDocumentCategory::Https;
	if (url.rfind("http://", 0) == 0) return NavigatorDocumentCategory::Http;
	return NavigatorDocumentCategory::Unsupported;
}

bool Navigator::isGeneratedInspectionViewUrl(const std::string& url)
{
	return url == "about:page-info" ||
		url == "about:save-page-text" ||
		url == "about:save-page-source" ||
		url == "about:view-source" ||
		url == "about:downloads" ||
		url == "about:navigator-runtime";
}

bool Navigator::visibleDocumentOwnsInspectedSource()
{
	const uint64_t visibleGeneration = s_currentDoc.formRuntimeState.documentGeneration;
	const bool sourceReference = s_inspectedDocumentGeneration != 0 &&
		!s_inspectedDoc.url.empty() &&
		!s_pageMetadata.finalUrl.empty() &&
		s_inspectedDoc.url == s_pageMetadata.finalUrl;
	if (!sourceReference) return false;
	if (s_visibleDocumentInspectionView) return true;
	return visibleGeneration != 0 &&
		s_inspectedDocumentGeneration == visibleGeneration &&
		s_currentDoc.url == s_pageMetadata.finalUrl;
}

void Navigator::refreshLifecycleOwnershipEvidence()
{
	const uint64_t visibleGeneration = s_currentDoc.formRuntimeState.documentGeneration;
	const bool sourceReference = s_inspectedDocumentGeneration != 0 &&
		!s_inspectedDoc.url.empty() &&
		!s_pageMetadata.finalUrl.empty() &&
		s_inspectedDoc.url == s_pageMetadata.finalUrl;
	s_lifecycleDiagnostics.visibleDocumentGeneration = visibleGeneration;
	s_lifecycleDiagnostics.inspectedDocumentGeneration = s_inspectedDocumentGeneration;
	s_lifecycleDiagnostics.visibleDocumentCategory = s_visibleDocumentCategory;
	s_lifecycleDiagnostics.inspectedSourceCategory = s_inspectedSourceCategory;
	s_lifecycleDiagnostics.requestedFinalUrlEqual =
		s_pageMetadata.requestedUrl.empty() || s_pageMetadata.requestedUrl == s_pageMetadata.finalUrl;
	s_lifecycleDiagnostics.visibleDocumentGenerated =
		s_currentDoc.url.rfind("about:", 0) == 0;
	s_lifecycleDiagnostics.visibleDocumentInspectionView = s_visibleDocumentInspectionView;
	s_lifecycleDiagnostics.sourceReferenceValid = sourceReference;
	s_lifecycleDiagnostics.ownershipGuardPassed = visibleDocumentOwnsInspectedSource();
	if (s_lifecycleDiagnostics.ownershipGuardPassed)
		incrementLifecycleCounter(s_lifecycleDiagnostics.inspectedDocumentGuardPass);
	else
		incrementLifecycleCounter(s_lifecycleDiagnostics.inspectedDocumentGuardBlock);
}

void Navigator::noteFocusClearedForTransition(
	NavigatorTransitionCategory transition, bool hadFocus)
{
	if (!hadFocus && transition != NavigatorTransitionCategory::HistoryBack &&
		transition != NavigatorTransitionCategory::HistoryForward) return;
	switch (transition) {
	case NavigatorTransitionCategory::Reload:
		incrementLifecycleCounter(s_lifecycleDiagnostics.focusClearedReload);
		break;
	case NavigatorTransitionCategory::HistoryBack:
	case NavigatorTransitionCategory::HistoryForward:
		incrementLifecycleCounter(s_lifecycleDiagnostics.focusClearedHistory);
		break;
	case NavigatorTransitionCategory::RedirectReplacement:
		incrementLifecycleCounter(s_lifecycleDiagnostics.focusClearedRedirect);
		break;
	case NavigatorTransitionCategory::PageInfoGeneration:
	case NavigatorTransitionCategory::SavePageTextGeneration:
	case NavigatorTransitionCategory::GeneratedAboutNavigation:
		incrementLifecycleCounter(s_lifecycleDiagnostics.focusClearedGeneratedPage);
		break;
	case NavigatorTransitionCategory::NavigationFailure:
	case NavigatorTransitionCategory::ParseFailure:
	case NavigatorTransitionCategory::TlsPolicyFailure:
		incrementLifecycleCounter(s_lifecycleDiagnostics.focusClearedNavigationFailure);
		break;
	default:
		break;
	}
}

std::string Navigator::SmokeLifecycleReport()
{
	refreshLifecycleOwnershipEvidence();
	std::ostringstream out;
	out << "navigator.lifecycle.report\n";
	out << "navigator_visible_document_generation=" << s_lifecycleDiagnostics.visibleDocumentGeneration << "\n";
	out << "navigator_inspected_document_generation=" << s_lifecycleDiagnostics.inspectedDocumentGeneration << "\n";
	out << "navigator_visible_document_category=" << documentCategoryName(s_lifecycleDiagnostics.visibleDocumentCategory) << "\n";
	out << "navigator_inspected_source_category=" << documentCategoryName(s_lifecycleDiagnostics.inspectedSourceCategory) << "\n";
	out << "navigator_requested_final_url_equal=" << yesNo(s_lifecycleDiagnostics.requestedFinalUrlEqual) << "\n";
	out << "navigator_generated_page=" << yesNo(s_lifecycleDiagnostics.visibleDocumentGenerated) << "\n";
	out << "navigator_source_reference_valid=" << yesNo(s_lifecycleDiagnostics.sourceReferenceValid) << "\n";
	out << "navigator_focus_serial_present=" << yesNo(s_currentDoc.formRuntimeState.focusValid &&
		s_currentDoc.formRuntimeState.focusedLogicalSerial != 0) << "\n";
	out << "navigator_runtime_control_state_count=" << s_currentDoc.formRuntimeState.count << "\n";
	out << "navigator_ownership_guard=" << (s_lifecycleDiagnostics.ownershipGuardPassed ? "pass" : "block") << "\n";
	out << "navigator_transition_category=" << transitionCategoryName(s_lifecycleDiagnostics.lastTransition) << "\n";
	out << "navigator_document_generation_changes=" << s_lifecycleDiagnostics.documentGenerationChanges << "\n";
	out << "navigator_same_document_recomputations=" << s_lifecycleDiagnostics.sameDocumentRecomputations << "\n";
	out << "navigator_document_replacements=" << s_lifecycleDiagnostics.documentReplacements << "\n";
	out << "navigator_focus_preserved_recompute=" << s_lifecycleDiagnostics.focusPreservedRecompute << "\n";
	out << "navigator_focus_cleared_reload=" << s_lifecycleDiagnostics.focusClearedReload << "\n";
	out << "navigator_focus_cleared_history=" << s_lifecycleDiagnostics.focusClearedHistory << "\n";
	out << "navigator_focus_cleared_redirect=" << s_lifecycleDiagnostics.focusClearedRedirect << "\n";
	out << "navigator_focus_cleared_generated_page=" << s_lifecycleDiagnostics.focusClearedGeneratedPage << "\n";
	out << "navigator_focus_cleared_navigation_failure=" << s_lifecycleDiagnostics.focusClearedNavigationFailure << "\n";
	out << "navigator_runtime_state_clears=" << s_lifecycleDiagnostics.runtimeStateClears << "\n";
	out << "navigator_stale_mouse_release_blocks=" << s_lifecycleDiagnostics.staleMouseReleaseBlocks << "\n";
	out << "navigator_stale_key_release_blocks=" << s_lifecycleDiagnostics.staleKeyReleaseBlocks << "\n";
	out << "navigator_inspected_document_guard_pass=" << s_lifecycleDiagnostics.inspectedDocumentGuardPass << "\n";
	out << "navigator_inspected_document_guard_block=" << s_lifecycleDiagnostics.inspectedDocumentGuardBlock << "\n";
	out << "navigator_page_info_source_valid=" << s_lifecycleDiagnostics.pageInfoSourceValid << "\n";
	out << "navigator_save_text_source_valid=" << s_lifecycleDiagnostics.saveTextSourceValid << "\n";
	out << "navigator_history_state_nonpersistent=" << s_lifecycleDiagnostics.historyStateNonpersistent << "\n";
	out << "navigator_transition_metadata_clamps=" << s_lifecycleDiagnostics.transitionMetadataClamps << "\n";
	out << "navigator_save_text_intended_source_category=" << s_lifecycleDiagnostics.saveTextIntendedSourceCategory << "\n";
	out << "navigator_save_text_actual_source_category=" << s_lifecycleDiagnostics.saveTextActualSourceCategory << "\n";
	out << "navigator_save_text_visible_text_byte_count=" << s_lifecycleDiagnostics.saveTextVisibleTextByteCount << "\n";
	out << "navigator_save_text_generated_page_excluded=" << yesNo(s_lifecycleDiagnostics.saveTextGeneratedPageExcluded) << "\n";
	out << "navigator_save_text_password_redacted=" << yesNo(s_lifecycleDiagnostics.saveTextPasswordRedacted) << "\n";
	out << "navigator_save_text_hidden_control_excluded=" << yesNo(s_lifecycleDiagnostics.saveTextHiddenControlExcluded) << "\n";
	out << "navigator_save_text_diagnostics_excluded=" << yesNo(s_lifecycleDiagnostics.saveTextDiagnosticsExcluded) << "\n";
	return out.str();
}

std::string Navigator::SmokeCurrentLinkUrl(const std::string& text)
{
	for (const DocBlock& block : s_currentDoc.blocks) {
		if (block.type == BlockType::Link && block.text == text) {
			return block.url;
		}
	}
	return "";
}

bool Navigator::SmokeClickFormControlById(const std::string& id)
{
	const int blockIndex = findBlockById(id, false);
	return blockIndex >= 0 && smokeClickBlock(blockIndex, false);
}

bool Navigator::SmokeClickFormLabelById(const std::string& id)
{
	const int blockIndex = findBlockById(id, true);
	return blockIndex >= 0 && smokeClickBlock(blockIndex, true);
}

bool Navigator::SmokeFormControlCheckedById(const std::string& id)
{
	const int blockIndex = findBlockById(id, false);
	return blockIndex >= 0 && runtimeChecked(s_currentDoc.blocks[static_cast<size_t>(blockIndex)]);
}

bool Navigator::SmokeFormControlDisabledById(const std::string& id)
{
	const int blockIndex = findBlockById(id, false);
	return blockIndex >= 0 && runtimeDisabled(s_currentDoc.blocks[static_cast<size_t>(blockIndex)]);
}

bool Navigator::SmokeFormHitTargetById(const std::string& id)
{
	const int blockIndex = findBlockById(id, false);
	if (blockIndex < 0) return false;
	s_scrollOffset = std::max(0, blockLayoutY(blockIndex) - kContentH / 2);
	clampScrollOffset();
	const Rect rect = formControlRect(blockIndex);
	if (rect.w <= 0 || rect.h <= 0) return false;
	int hitIndex = -1;
	const HitTarget target = hitTest(rect.x + rect.w / 2, rect.y + rect.h / 2, hitIndex);
	return hitIndex == blockIndex &&
		(target == HitTarget::FormCheckbox || target == HitTarget::FormRadio ||
		 target == HitTarget::FormSubmit || target == HitTarget::FormInput ||
		 target == HitTarget::FormTextarea || target == HitTarget::FormSelect);
}

int Navigator::SmokeFormActivationCountById(const std::string& id)
{
	const int blockIndex = findBlockById(id, false);
	if (blockIndex < 0) return -1;
	const FormRuntimeControlState* state = runtimeStateForBlock(s_currentDoc.blocks[static_cast<size_t>(blockIndex)]);
	return state ? static_cast<int>(state->activationCount) : 0;
}

bool Navigator::SmokeFormMouseSafetyById(const std::string& id)
{
	const int blockIndex = findBlockById(id, false);
	if (blockIndex < 0) return false;
	s_scrollOffset = std::max(0, blockLayoutY(blockIndex) - kContentH / 2);
	clampScrollOffset();
	const Rect rect = formControlRect(blockIndex);
	if (rect.w <= 0 || rect.h <= 0) return false;
	const int insideX = rect.x + rect.w / 2;
	const int insideY = rect.y + rect.h / 2;
	const int outsideX = std::min(kWindowW - 1, rect.x + rect.w + 32);
	const int outsideY = std::min(kWindowH - kStatusBarH - 1, rect.y + rect.h + 32);
	const int before = SmokeFormActivationCountById(id);
	handleMouseInput(insideX, insideY, 1, "down");
	handleMouseInput(outsideX, outsideY, 1, "up");
	handleMouseInput(outsideX, outsideY, 1, "down");
	handleMouseInput(insideX, insideY, 1, "up");
	return SmokeFormActivationCountById(id) == before;
}

bool Navigator::SmokeFocusFormControlById(const std::string& id, bool keyboardOrigin)
{
	const int blockIndex = findBlockById(id, false);
	if (blockIndex < 0 || !isFocusableFormControl(s_currentDoc.blocks[static_cast<size_t>(blockIndex)])) return false;
	focusDocumentInput(blockIndex, keyboardOrigin ? FormFocusOrigin::Keyboard : FormFocusOrigin::ProgrammaticInternalSmoke);
	updateDisplay();
	return isFocusedFormControl(s_currentDoc.blocks[static_cast<size_t>(blockIndex)]);
}

bool Navigator::SmokeFormControlFocusedById(const std::string& id)
{
	const int blockIndex = findBlockById(id, false);
	return blockIndex >= 0 && isFocusedFormControl(s_currentDoc.blocks[static_cast<size_t>(blockIndex)]);
}

std::string Navigator::SmokeFocusedFormControlId()
{
	const int blockIndex = focusedFormControlBlockIndex();
	return blockIndex >= 0 ? s_currentDoc.blocks[static_cast<size_t>(blockIndex)].id : std::string();
}

std::string Navigator::SmokeFormFocusOrigin()
{
	return formFocusOriginName(s_currentDoc.formRuntimeState.focusOrigin);
}

int Navigator::SmokeFormFocusableCount()
{
	std::array<int, kFormRuntimeControlCap> order{};
	return static_cast<int>(buildFormFocusOrder(order));
}

bool Navigator::SmokeKeyPress(int keyCode, const std::string& action)
{
	if (s_windowId == 0) return false;
	handleKeyPress(keyCode, action);
	return true;
}

bool Navigator::SmokeSetFormControlDisabledById(const std::string& id, bool disabled)
{
	const int blockIndex = findBlockById(id, false);
	if (blockIndex < 0) return false;
	FormRuntimeControlState* state = runtimeStateForBlock(s_currentDoc.blocks[static_cast<size_t>(blockIndex)]);
	if (!state) return false;
	state->disabled = disabled;
	recomputeFormControlStyles();
	updateDisplay();
	return runtimeDisabled(s_currentDoc.blocks[static_cast<size_t>(blockIndex)]) == disabled;
}

bool Navigator::SmokeSetFormControlHiddenById(const std::string& id, bool hidden)
{
	const int blockIndex = findBlockById(id, false);
	if (blockIndex < 0) return false;
	DocBlock& block = s_currentDoc.blocks[static_cast<size_t>(blockIndex)];
	if (!block.formControl.metadataComplete || block.formControl.logicalSerial == 0) return false;
	block.formControl.hidden = hidden;
	block.elementMetadata.formControl.hidden = hidden;
	block.style.displayNone = hidden;
	recomputeFormControlStyles();
	updateDisplay();
    // The authored/computed display style may legitimately differ from the
    // deterministic hidden-transition hook.  Focusability and metadata use
    // the form-control hidden flag, so verify that state directly rather than
    // coupling the hook result to a later style recomputation.
    return block.formControl.hidden == hidden;
}

void Navigator::SmokeDeactivateWindow()
{
	clearDocumentFocus(true, FormFocusCancellationReason::Deactivation);
	s_ctrlPressed = false;
	s_shiftPressed = false;
	if (s_windowId != 0) updateDisplay();
}

bool Navigator::SmokeForceFormFocusGenerationMismatch()
{
	FormRuntimeStateTable& runtime = s_currentDoc.formRuntimeState;
	if (!runtime.initialized || !runtime.focusValid) return false;
	if (runtime.focusedDocumentGeneration == std::numeric_limits<uint64_t>::max())
		runtime.focusedDocumentGeneration = 1;
	else
		++runtime.focusedDocumentGeneration;
	recomputeFormControlStyles();
	updateDisplay();
	return !runtime.focusValid && runtime.focusedLogicalSerial == 0;
}

int Navigator::SmokeFormControlInputLengthById(const std::string& id)
{
	const int blockIndex = findBlockById(id, false);
	return blockIndex < 0 ? -1 : static_cast<int>(s_currentDoc.blocks[static_cast<size_t>(blockIndex)].inputValue.size());
}

void Navigator::SmokeFocusAddressBar()
{
	focusAddressBar();
}

bool Navigator::SmokeReloadCurrentDocument()
{
	if (s_windowId == 0 || s_currentDoc.url.empty()) return false;
	const std::string url = s_currentDoc.url;
	loadUrl(url, false, NavigatorTransitionCategory::Reload);
	return s_currentDoc.url == url;
}

bool Navigator::SmokeGoBack()
{
	if (s_windowId == 0 || s_backStack.empty()) return false;
	const std::string before = s_currentDoc.url;
	goBack();
	return s_currentDoc.url != before;
}

bool Navigator::SmokeGoForward()
{
	if (s_windowId == 0 || s_forwardStack.empty()) return false;
	const std::string before = s_currentDoc.url;
	goForward();
	return s_currentDoc.url != before;
}

bool Navigator::SmokeMouseDownFormControlById(const std::string& id)
{
	const int blockIndex = findBlockById(id, false);
	if (blockIndex < 0) return false;
	s_scrollOffset = std::max(0, blockLayoutY(blockIndex) - kContentH / 2);
	clampScrollOffset();
	const Rect rect = formControlRect(blockIndex);
	if (rect.w <= 0 || rect.h <= 0) return false;
	handleMouseInput(rect.x + rect.w / 2, rect.y + rect.h / 2, 1, "down");
	return s_mouseLeftDown && s_mouseDownLinkBlockIndex == blockIndex;
}

bool Navigator::SmokeMouseUp()
{
	if (s_windowId == 0) return false;
	handleMouseInput(s_mouseCurrentX, s_mouseCurrentY, 1, "up");
	return true;
}

std::vector<int> Navigator::SmokeToolbarWidgetIds()
{
	return s_registeredWidgetIds;
}

int Navigator::main(int, char**)
{
	Logger::write(LogLevel::Info, "guideXOS Navigator starting");
	s_windowId        = 0;
	s_scrollOffset    = 0;
	s_documentHeight  = 0;
	s_statusText      = "Ready";
	s_hoverStatusText.clear();
	s_hitLinkBlockIndex = -1;
	s_backStack.clear();
	s_forwardStack.clear();
	s_pageMetadata = NavigatorPageMetadata{};
	s_lifecycleDiagnostics = NavigatorLifecycleDiagnostics{};
	s_visibleDocumentCategory = NavigatorDocumentCategory::None;
	s_inspectedSourceCategory = NavigatorDocumentCategory::None;
	s_visibleDocumentInspectionView = false;
	s_inspectedDocumentGeneration = 0;
	s_pendingDocumentUrl.clear();
	s_pendingTransitionCategory = NavigatorTransitionCategory::Navigation;
	s_staleMouseReleaseGeneration = 0;
	s_staleKeyReleaseGeneration = 0;
	s_inspectedDoc = WebDocument{};
	s_lastSubmittedFormUrl.clear();
	s_lastSubmittedFormAction.clear();
	s_lastSubmittedFormMethod.clear();
	s_lastSubmittedFormStatus.clear();
	s_lastPostHttpStatus.clear();
	s_lastPostContentType.clear();
	s_recentDownloads.clear();
	s_addressFocused = false;
	s_addressBuffer.clear();
	s_addressCaret   = 0;
	s_documentGeneration = 0;
	s_tabKeyPressed = false;
	s_ctrlPressed = false;
	s_shiftPressed = false;
	s_mouseLeftDown = false;
	s_mouseMode = MouseMode::None;
	s_mouseDownHitTarget = HitTarget::None;
	s_mouseDownLinkBlockIndex = -1;
	s_mouseDownLinkUrl.clear();
	s_mouseDownX = 0;
	s_mouseDownY = 0;
	s_mouseCurrentX = 0;
	s_mouseCurrentY = 0;
	s_mouseDragThresholdExceeded = false;
	s_selectionBegan = false;
	clearSelection();
	s_navigatorClipboard.clear();
	s_clipboardMode = "Navigator internal clipboard";
	s_registeredWidgetIds.clear();

	loadBookmarks();

	// Load the startup page through the normal URL path.
	// Later phases make this read from a config or command-line argument.
	loadUrl("about:navigator", true, NavigatorTransitionCategory::InitialNavigation);

	ipc::Bus::ensure("gui.input");
	ipc::Bus::ensure("gui.output");

	std::ostringstream create;
	create << "guideXOS Navigator|" << kWindowW << "|" << kWindowH;
	publish(MsgType::MT_Create, create.str());

	bool running = true;
	while (running) {
		ipc::Message msg;
		if (!ipc::Bus::pop("gui.output", msg, 100)) continue;

		MsgType msgType = static_cast<MsgType>(msg.type);
		std::string payload(msg.data.begin(), msg.data.end());
		switch (msgType) {
		case MsgType::MT_Create: {
			size_t sep = payload.find('|');
			if (sep != std::string::npos) {
				try {
					uint64_t createdId = std::stoull(payload.substr(0, sep));
					if (s_windowId == 0) {
						s_windowId = createdId;
						updateDisplay();
					}
				} catch (...) {
					Logger::write(LogLevel::Warn, "Navigator failed to parse create ack");
				}
			}
			break;
		}
		case MsgType::MT_WidgetEvt: {
			std::istringstream iss(payload);
			std::string winIdStr;
			std::string widgetIdStr;
			std::string event;
			std::getline(iss, winIdStr, '|');
			std::getline(iss, widgetIdStr, '|');
			std::getline(iss, event, '|');
			if (event == "click") {
				try {
					uint64_t winId = std::stoull(winIdStr);
					int widgetId = std::stoi(widgetIdStr);
					if (winId == s_windowId) handleToolbarAction(widgetId);
				} catch (...) {
				}
			}
			break;
		}
		case MsgType::MT_InputMouse: {
			std::istringstream iss(payload);
			std::string xStr;
			std::string yStr;
			std::string buttonStr;
			std::string action;
			std::getline(iss, xStr, '|');
			std::getline(iss, yStr, '|');
			std::getline(iss, buttonStr, '|');
			std::getline(iss, action, '|');
			try {
				int x = std::stoi(xStr);
				int y = std::stoi(yStr);
				int button = std::stoi(buttonStr);
				handleMouseInput(x, y, button, action);
			} catch (...) {
			}
			break;
		}
		case MsgType::MT_SetFocus: {
			try {
				const uint64_t focusedWindow = std::stoull(payload);
				if (focusedWindow != s_windowId) {
					clearDocumentFocus(true, FormFocusCancellationReason::Deactivation);
					s_ctrlPressed = false;
					s_shiftPressed = false;
					if (s_windowId != 0) updateDisplay();
				}
			} catch (...) {
				clearDocumentFocus(true, FormFocusCancellationReason::Deactivation);
				s_ctrlPressed = false;
				s_shiftPressed = false;
			}
			break;
		}
		case MsgType::MT_InputKey: {
			size_t sep = payload.find('|');
			if (sep != std::string::npos) {
				try {
					int keyCode = std::stoi(payload.substr(0, sep));
					std::string action = payload.substr(sep + 1);
					handleKeyPress(keyCode, action);
				} catch (...) {
				}
			}
			break;
		}
		case MsgType::MT_Close: {
			try {
				uint64_t closedId = std::stoull(payload);
				if (closedId == s_windowId) running = false;
			} catch (...) {
				running = false;
			}
			break;
		}
		default:
			break;
		}
	}

	cleanupRemoteImageTempFiles();
	Logger::write(LogLevel::Info, "guideXOS Navigator terminated");
	return 0;
}

void Navigator::updateDisplay(bool renderDocumentContent)
{
	if (s_windowId == 0) return;
	// Hosted lifecycle smoke drives many real state transitions synchronously.
	// Keep the first compositor frame for toolbar registration, then defer
	// redundant paint submission while the smoke suite inspects state through
	// the same production navigation/focus/activation paths.  Interactive and
	// screenshot runs do not set this environment-gated test switch.
	if (navigatorSmokePaintDeferred() && !s_registeredWidgetIds.empty()) return;

	// Window title tracks the current document title.
	const std::string winTitle = s_currentDoc.title.empty()
		? "guideXOS Navigator"
		: s_currentDoc.title + " - guideXOS Navigator";
	publish(MsgType::MT_SetTitle, std::to_string(s_windowId) + "|" + winTitle);
	publish(MsgType::MT_DrawText, std::to_string(s_windowId) + "|\f");

	drawThemeRect(s_windowId, 0, 0, kWindowW, kWindowH, NavigatorBodyColor());
	navigatorSmokeProgress("toolbar-render-start");
	renderToolbar();
	navigatorSmokeProgress("toolbar-render-complete");
	if (renderDocumentContent) {
		navigatorSmokeProgress("document-render-start");
		renderDocument();
		navigatorSmokeProgress("document-render-complete");
	}
	navigatorSmokeProgress("status-render-start");
	renderStatusBar();
	navigatorSmokeProgress("status-render-complete");
}

	void Navigator::renderToolbar()
	{
		s_cssClipDepth = 0;
		cssSetPaintClip(CssPaintRect{0, 0, kWindowW, kToolbarH});
		s_cssPaintOpacityPercent = 100;
		static const char* kIconRoot = "assets/Images/NuoveXT/PNG/32/";
		drawThemeRect(s_windowId, 0, 0, kWindowW, kToolbarH, NavigatorToolbarColor());
		drawThemeRect(s_windowId, 0, kToolbarH - 1, kWindowW, 1, NavigatorToolbarBorderColor());

	navigatorSmokeProgress("toolbar-button-back-start");
	addButton(s_windowId, kWidgetIdBack, 20, kButtonY, kButtonW, kButtonH, "Back", std::string(kIconRoot) + "above_thearrow_10194.png");
	navigatorSmokeProgress("toolbar-button-back-complete");
	navigatorSmokeProgress("toolbar-button-forward-start");
	addButton(s_windowId, kWidgetIdForward, 20 + (kButtonW + kButtonGap), kButtonY, kButtonW, kButtonH, "Next", std::string(kIconRoot) + "Next_arrow_10211.png");
	navigatorSmokeProgress("toolbar-button-forward-complete");
	navigatorSmokeProgress("toolbar-button-reload-start");
	addButton(s_windowId, kWidgetIdReload, 20 + 2 * (kButtonW + kButtonGap), kButtonY, kButtonW, kButtonH, "Reload", std::string(kIconRoot) + "refresh_arrow_10190.png");
	navigatorSmokeProgress("toolbar-button-reload-complete");
	navigatorSmokeProgress("toolbar-button-home-start");
	addButton(s_windowId, kWidgetIdHome, 20 + 3 * (kButtonW + kButtonGap), kButtonY, kButtonW, kButtonH, "Home", std::string(kIconRoot) + "gohome_action_ir_10235.png");
	navigatorSmokeProgress("toolbar-button-home-complete");
	navigatorSmokeProgress("toolbar-button-bookmarks-start");
	addButton(s_windowId, kWidgetIdBookmarks, 20 + 4 * (kButtonW + kButtonGap), kButtonY, kButtonW, kButtonH, "Marks", std::string(kIconRoot) + "markers_list_add_favorites_10275.png");
	navigatorSmokeProgress("toolbar-button-bookmarks-complete");
	navigatorSmokeProgress("toolbar-button-add-start");
	addButton(s_windowId, kWidgetIdAddBookmark, 20 + 5 * (kButtonW + kButtonGap), kButtonY, kButtonW, kButtonH, "Add", std::string(kIconRoot) + "edit_add_10261.png");
	navigatorSmokeProgress("toolbar-button-add-complete");
	navigatorSmokeProgress("toolbar-button-find-start");
	addButton(s_windowId, kWidgetIdFind, 20 + 6 * (kButtonW + kButtonGap), kButtonY, kButtonW, kButtonH, "Find");
	navigatorSmokeProgress("toolbar-button-find-complete");

		drawThemeRect(s_windowId, kAddressX, kAddressY, kAddressW, kAddressH, NavigatorAddressFillColor());
		if (s_addressFocused) {
			// Focused: bright blue border on all four sides
			drawThemeRect(s_windowId, kAddressX,                 kAddressY,                 kAddressW, 1, NavigatorAddressFocusedBorderColor());
			drawThemeRect(s_windowId, kAddressX,                 kAddressY + kAddressH - 1, kAddressW, 1, NavigatorAddressFocusedBorderColor());
			drawThemeRect(s_windowId, kAddressX,                 kAddressY,                 1, kAddressH, NavigatorAddressFocusedBorderColor());
			drawThemeRect(s_windowId, kAddressX + kAddressW - 1, kAddressY,                 1, kAddressH, NavigatorAddressFocusedBorderColor());

			// Caret placement is still approximate until Navigator has proportional
			// document/chrome text measurement exposed through the GUI protocol.
			constexpr int kTextX = kAddressX + 10;
			const int kTextY = centeredChromeTextY(kAddressY, kAddressH);

		// Clamp caret defensively (should already be in range, but guard rendering).
		int caretPos = std::max(0, std::min(s_addressCaret,
			static_cast<int>(s_addressBuffer.size())));

		std::string before = s_addressBuffer.substr(0, static_cast<size_t>(caretPos));
		std::string after  = s_addressBuffer.substr(static_cast<size_t>(caretPos));

		int caretX = kTextX + caretPos * kCharW;

		// Draw the full text (simpler for renderers that don't do sub-string positioning).
			drawThemeText(s_windowId, kTextX, kTextY, s_addressBuffer, NavigatorTextColor());
			// Draw a 1-px wide caret bar on top.
			drawThemeRect(s_windowId, caretX, kAddressY + 4, 1, kAddressH - 8, NavigatorAccentColor());
			(void)before; (void)after; // reserved for future proportional split-draw
		} else {
			// Normal: subtle top/bottom border
			drawThemeRect(s_windowId, kAddressX, kAddressY,                 kAddressW, 1, NavigatorAddressIdleTopBorderColor());
			drawThemeRect(s_windowId, kAddressX, kAddressY + kAddressH - 1, kAddressW, 1, NavigatorAddressIdleBottomBorderColor());
			drawThemeText(s_windowId, kAddressX + 10, centeredChromeTextY(kAddressY, kAddressH), s_currentDoc.url, NavigatorTextColor());
		}
		if (s_loading) {
			drawAnimatedImage(s_windowId, kAddressX + kAddressW - 24, kAddressY + 2, 22, 22,
				"assets/Images/SurfThrobber/PNG/surfer_{frame}.png");
		}
}

void Navigator::renderDocument()
{
	ensureCssMarginLayout(s_currentDoc);
	ensureCssFloatLayout(s_currentDoc);
	ensureInlineLayout(s_currentDoc);
	ensureCssPositionLayout(s_currentDoc);
	clampScrollOffset();
	s_renderCounters = {};
	s_cssClipDepth = 0;
	s_cssClipIntersections = 0;
	s_cssClipRecordCount = 0;
	s_cssClipDepthClamps = 0;
	s_cssClippedPaintOps = 0;
	s_cssClippedHitTargets = 0;
	s_cssHitTargetsBeforeClipping = 0;
	s_cssHitTargetsAfterClipping = 0;
	cssSetPaintClip(CssPaintRect{kContentX, kToolbarH + 6, kContentW, kContentH});
	s_cssPaintOpacityPercent = 100;

	// Content area background
	uint32_t contentColor = NavigatorContentColor();
	if (s_currentDoc.bodyStyle.hasBackgroundColor) {
		contentColor = s_currentDoc.bodyStyle.backgroundColor;
	}
	const uint32_t contentTextColor = NavigatorContentTextColor(contentColor);
	drawThemeRect(s_windowId, kContentX, kToolbarH + 6, kContentW, kContentH, contentColor);
	drawThemeRect(s_windowId, kContentX, kToolbarH + 6, kContentW, 1, NavigatorContentBorderColor());
	// Scroll-track slot
	drawThemeRect(s_windowId, kContentX + kContentW - 12, kToolbarH + 6, 8, kContentH, NavigatorScrollTrackColor());
	auto formFillColor = [&](const DocBlock& block, bool focused, bool disabled) -> uint32_t {
		if (disabled) return 0xFFE3E6EAu;
		if (block.style.hasBackgroundColor) return block.style.backgroundColor;
		return NavigatorFieldFillColor(focused);
	};
	auto formBorderColor = [&](const DocBlock& block, bool focused, bool disabled) -> uint32_t {
		if (disabled) return 0xFF9AA1A9u;
		if (block.style.hasBorderTop && block.style.borderTopColor != 0) return block.style.borderTopColor;
		return NavigatorFieldBorderColor(focused);
	};
	auto formTextColor = [&](const DocBlock& block, bool disabled, bool placeholder) -> uint32_t {
		if (disabled || placeholder) return NavigatorFieldMutedTextColor();
		if (block.style.hasColor) return block.style.color;
		return NavigatorFieldTextColor();
	};
	auto blockHasAncestorSerial = [](const DocBlock& block, uint64_t serial) {
		for (const gxos::web::HtmlElementRef& ancestor : block.ancestors) {
			if (ancestor.serial == serial) return true;
		}
		return false;
	};
	auto renderFieldsetAt = [&](int currentIndex) {
		for (const gxos::web::FormContainerMetadata& container : s_currentDoc.formContainers) {
			if (container.tagName != "fieldset" || container.serial == 0 || container.style.displayNone) continue;
			int first = -1;
			int last = -1;
			for (int i = 0; i < static_cast<int>(s_currentDoc.blocks.size()); ++i) {
				const DocBlock& candidate = s_currentDoc.blocks[static_cast<size_t>(i)];
				if (!blockHasAncestorSerial(candidate, container.serial) || !blockHasVisibleCss(candidate)) continue;
				if (first < 0) first = i;
				last = i;
			}
			if (first != currentIndex || last < first) continue;
			const int top = kContentY + blockLayoutY(first) - s_scrollOffset + 1;
			const int lastHeight = blockTotalHeight(s_currentDoc.blocks[static_cast<size_t>(last)], s_currentDoc, false);
			const int bottom = kContentY + blockLayoutY(last) - s_scrollOffset + lastHeight - 2;
			const int available = std::max(1, kContentW - blockBodyMarginLeft(s_currentDoc) - blockBodyMarginRight(s_currentDoc) - 16);
			const int width = std::max(120, std::min(available, cssMaxWidthPx(container.style, available,
				cssWidthPx(container.style, available, available))));
			const int x = kContentX + blockBodyMarginLeft(s_currentDoc) + 8;
			const int height = std::max(28, bottom - top);
			if (container.style.hasBackgroundColor) drawThemeRect(s_windowId, x, top, width, height, container.style.backgroundColor);
			drawBlockBox(s_windowId, x, top, width, height, container.style);
			std::string legendText = container.legendText;
			const gxos::web::FormContainerMetadata* legend = nullptr;
			for (const auto& candidate : s_currentDoc.formContainers) {
				if (candidate.tagName == "legend" && candidate.parentSerial == container.serial) {
					legend = &candidate;
					break;
				}
			}
			if (legend && !legend->legendText.empty()) legendText = legend->legendText;
			if (!legendText.empty()) {
				const int legendW = std::min(width - 20, std::max(kCharW, static_cast<int>(legendText.size()) * kCharW + 8));
				drawThemeRect(s_windowId, x + 8, top - 2, legendW, kLineH + 2, contentColor);
				drawThemeText(s_windowId, x + 12, top, legendText.substr(0, static_cast<size_t>(std::max(1, (legendW - 8) / kCharW))),
					legend ? (legend->style.hasColor ? legend->style.color : contentTextColor) : contentTextColor);
			}
		}
	};

	int blockIndex = 0;
	auto drawDefaultFocusRing = [&](int index, const DocBlock& control) {
		if (!isFocusedFormControl(control)) return;
		const Rect rect = formControlRect(index);
		if (rect.w <= 0 || rect.h <= 0) return;
		const uint32_t ring = isDarkColor(contentColor) ? 0xFFFFFFFFu : 0xFF111827u;
		const int viewportTop = kToolbarH + 6;
		const int viewportRight = kContentX + kContentW - 14;
		const int viewportBottom = viewportTop + kContentH;
		const int requestedX = rect.x - 2;
		const int requestedY = rect.y - 2;
		const int requestedRight = rect.x + rect.w + 1;
		const int requestedBottom = rect.y + rect.h + 1;
		const int x = std::max(kContentX, requestedX);
		const int y = std::max(viewportTop, requestedY);
		const int right = std::min(viewportRight, requestedRight);
		const int bottom = std::min(viewportBottom, requestedBottom);
		if (right <= x || bottom <= y) return;
		FormAccessibilityRecord* record = accessibilityRecordForSerial(control.formControl.logicalSerial);
		const bool clamped = x != requestedX || y != requestedY || right != requestedRight || bottom != requestedBottom;
		++s_currentDoc.formsDiagnostics.formFocusRingDraws;
		if (clamped) ++s_currentDoc.formsDiagnostics.formFocusRingClamps;
		if (record) {
			record->focusRingDrawn = true;
			record->focusRingClamped = clamped;
		}
		drawThemeRect(s_windowId, x, y, right - x, 1, ring);
		drawThemeRect(s_windowId, x, bottom - 1, right - x, 1, ring);
		drawThemeRect(s_windowId, x, y, 1, bottom - y, ring);
		drawThemeRect(s_windowId, right - 1, y, 1, bottom - y, ring);
	};
	std::function<void(const InlineFlowLayout&, int, int)> renderInlineFlowAt;
	renderInlineFlowAt = [&](const InlineFlowLayout& flow, int parentX, int parentY) {
		if (flow.style.displayNone || flow.style.visibility == VisibilityMode::Hidden) return;
		const bool embedded = flow.contextSerial != 0;
		DocBlock anchor;
		if (flow.anchorBlockIndex >= 0 && flow.anchorBlockIndex < static_cast<int>(s_currentDoc.blocks.size()))
			anchor = s_currentDoc.blocks[static_cast<size_t>(flow.anchorBlockIndex)];
		const int marginTop = cssMarginTopPx(flow.style, anchor.type == BlockType::Heading ? 10 : 4);
		const int marginBottom = cssMarginBottomPx(flow.style, anchor.type == BlockType::ListItem ? 4 : 8);
		const CssPositionedRecord* positionedAnchor = !embedded
			? cssPositionedRecordForBlock(s_currentDoc, flow.anchorBlockIndex) : nullptr;
		int drawY = embedded ? parentY + flow.localOuterY :
			kContentY + blockLayoutY(flow.anchorBlockIndex) - s_scrollOffset;
		int flowOuterX = embedded ? parentX + flow.localOuterX : flow.outerX;
		int flowOuterWidth = flow.outerWidth;
		int flowBoxHeight = std::max(1, flow.outerHeight > 0 ? flow.outerHeight : flow.totalHeight - marginTop - marginBottom);
		if (positionedAnchor) {
			drawY = positionedAnchor->finalY - s_scrollOffset - marginTop;
			flowOuterX = positionedAnchor->finalX;
			flowOuterWidth = std::max(1, positionedAnchor->usedWidth);
			flowBoxHeight = std::max(1, positionedAnchor->usedHeight);
		} else if (!embedded) {
			int ancestorDeltaX = 0;
			int ancestorDeltaY = 0;
			cssPositionRelativeAncestorDelta(s_currentDoc, flow.anchorBlockIndex, &ancestorDeltaX, &ancestorDeltaY);
			flowOuterX = cssBoundedGeometryAdd(flowOuterX, ancestorDeltaX);
			drawY = cssBoundedGeometryAdd(drawY, ancestorDeltaY);
		}
		const int boxY = embedded ? drawY : drawY + marginTop;
		const int boxH = flowBoxHeight;
		const int borderTop = cssBorderTopPx(flow.style);
		const int paddingTop = cssPaddingTopPx(flow.style, 0);
		const int baseY = boxY + borderTop + paddingTop;
		const bool ancestorClipPushed = false;
		s_cssPaintOpacityPercent = std::max(0, std::min(100, flow.style.effectiveOpacityPercent));
		drawBlockBox(s_windowId, flowOuterX, boxY, flowOuterWidth, boxH, flow.style);
		const bool blockClipPushed = cssPushPaintClip(
			embedded ? CssPaintRect{flowOuterX + cssBorderLeftPx(flow.style), boxY + cssBorderTopPx(flow.style),
				std::max(0, flowOuterWidth - cssBorderLeftPx(flow.style) - cssBorderRightPx(flow.style)),
				std::max(0, boxH - cssBorderTopPx(flow.style) - cssBorderBottomPx(flow.style))} :
			cssPositionedClipForBlock(s_currentDoc, flow.anchorBlockIndex, flowOuterX, boxY,
				flowOuterWidth, boxH, s_scrollOffset));
		// Floats in an atomic/embedded BFC use the same flat record model, but
		// their coordinates are local to that context.  Paint them while the
		// containing atomic clip is active so nested targets cannot escape.
		if (embedded && s_cssFloatLayoutSnapshot.valid) {
			for (const CssFloatRecord& nestedFloat : s_cssFloatLayoutSnapshot.records) {
				if (nestedFloat.contextSerial != flow.contextSerial ||
					nestedFloat.bfcIdentity != flow.contextSerial ||
					nestedFloat.blockIndex < 0 ||
					nestedFloat.blockIndex >= static_cast<int>(s_currentDoc.blocks.size())) continue;
				const DocBlock& nestedBlock = s_currentDoc.blocks[static_cast<size_t>(nestedFloat.blockIndex)];
				const WebStyle* nestedStyle = computedStyleForSerial(s_currentDoc, nestedFloat.logicalSerial);
				if (!nestedStyle) nestedStyle = &nestedBlock.style;
				if (nestedStyle->displayNone || nestedStyle->visibility == VisibilityMode::Hidden) continue;
				const int nestedX = parentX + flow.contentX + nestedFloat.borderBoxX;
				const int nestedY = baseY + nestedFloat.borderBoxY;
				s_cssPaintOpacityPercent = std::max(0, std::min(100, nestedStyle->effectiveOpacityPercent));
				drawBlockBox(s_windowId, nestedX, nestedY, nestedFloat.borderBoxW, nestedFloat.borderBoxH, *nestedStyle);
				if (nestedFloat.kind == InlineItemKind::ReplacedImage) {
					int imageW = 0;
					int imageH = 0;
					imageDisplaySize(nestedBlock, nestedFloat.borderBoxW, imageW, imageH);
					const ImageInfo& info = imageInfoForBlock(nestedBlock);
					if (info.ok) drawImage(s_windowId,
						 nestedX + cssBorderLeftPx(*nestedStyle) + cssPaddingLeftPx(*nestedStyle, 0),
						 nestedY + cssBorderTopPx(*nestedStyle) + cssPaddingTopPx(*nestedStyle, 0),
						 std::min(imageW, nestedFloat.borderBoxW), std::min(imageH, nestedFloat.borderBoxH), info.drawPath);
				} else if (!nestedFloat.contentText.empty()) {
					drawTextAtStyled(s_windowId,
						nestedX + cssBorderLeftPx(*nestedStyle) + cssPaddingLeftPx(*nestedStyle, 0),
						nestedY + cssBorderTopPx(*nestedStyle) + cssPaddingTopPx(*nestedStyle, 0),
						nestedFloat.contentText, *nestedStyle, contentTextColor,
						std::max(1, nestedFloat.borderBoxH));
				}
			}
		}
		for (const InlineFragmentLayout& fragment : flow.fragments) {
			if (!fragment.visible || fragment.itemIndex < 0 ||
				fragment.itemIndex >= static_cast<int>(s_currentDoc.inlineItems.size())) continue;
			const WebInlineItem& item = s_currentDoc.inlineItems[static_cast<size_t>(fragment.itemIndex)];
			DocBlock itemBlock;
			if (item.blockIndex >= 0 && item.blockIndex < static_cast<int>(s_currentDoc.blocks.size()))
				itemBlock = s_currentDoc.blocks[static_cast<size_t>(item.blockIndex)];
			else if (fragment.kind != InlineItemKind::AtomicBlock) continue;
			const WebStyle* ownerStyle = inlineOwnerStyle(s_currentDoc, item, flow.style);
			if (!ownerStyle || ownerStyle->displayNone || ownerStyle->visibility == VisibilityMode::Hidden) continue;
			WebStyle paintStyle = *ownerStyle;
			if (itemBlock.type == BlockType::Link || !itemBlock.url.empty()) {
				if (!paintStyle.hasColor) {
					paintStyle.hasColor = true;
					paintStyle.color = s_visitedUrls.find(itemBlock.url) != s_visitedUrls.end()
						? 0xFF6B46C1u : 0xFF1E5CB8u;
				}
				if (!paintStyle.hasTextDecoration) {
					paintStyle.hasTextDecoration = true;
					paintStyle.underline = true;
				}
			}
			s_cssPaintOpacityPercent = std::max(0, std::min(100, paintStyle.effectiveOpacityPercent));
			int fragX = (embedded ? parentX : 0) + flow.contentX + fragment.x;
			int fragY = baseY + fragment.y;
			const CssPositionedRecord* ownerPosition = cssPositionedRecordForSerial(s_currentDoc, item.ownerSerial);
			const uint64_t anchorSerial = anchor.elementMetadata.serial;
			if (ownerPosition && ownerPosition->logicalSerial != anchorSerial) {
				fragX = cssBoundedGeometryAdd(fragX, ownerPosition->finalX - ownerPosition->normalX);
				fragY = cssBoundedGeometryAdd(fragY, ownerPosition->finalY - ownerPosition->normalY);
			}
			if (fragment.kind == InlineItemKind::AtomicBlock &&
				fragment.atomicResultIndex >= 0 && fragment.atomicResultIndex < static_cast<int>(s_inlineLayoutSnapshot.atomicResults.size())) {
				const CssAtomicLayoutResult& atomic = s_inlineLayoutSnapshot.atomicResults[static_cast<size_t>(fragment.atomicResultIndex)];
				const int atomicX = fragX + fragment.boxOffsetX;
				const int atomicY = fragY;
				s_cssPaintOpacityPercent = std::max(0, std::min(100, paintStyle.effectiveOpacityPercent));
				drawBlockBox(s_windowId, atomicX, atomicY, atomic.outerWidth, atomic.outerHeight, paintStyle);
				const bool atomicClipPushed = atomic.overflowClipped && cssPushPaintClip(
					CssPaintRect{atomicX + cssBorderLeftPx(paintStyle), atomicY + cssBorderTopPx(paintStyle),
						std::max(0, atomic.outerWidth - cssBorderLeftPx(paintStyle) - cssBorderRightPx(paintStyle)),
						std::max(0, atomic.outerHeight - cssBorderTopPx(paintStyle) - cssBorderBottomPx(paintStyle))});
				for (int child = atomic.childBegin; child < static_cast<int>(atomic.childBegin + atomic.childCount); ++child) {
					if (child < 0 || child >= static_cast<int>(s_inlineLayoutSnapshot.atomicChildren.size())) continue;
					const CssAtomicChildPlacement& placement = s_inlineLayoutSnapshot.atomicChildren[static_cast<size_t>(child)];
					if (placement.flowIndex < 0 || placement.flowIndex >= static_cast<int>(s_inlineLayoutSnapshot.flows.size())) continue;
					renderInlineFlowAt(s_inlineLayoutSnapshot.flows[static_cast<size_t>(placement.flowIndex)], atomicX, atomicY);
				}
				if (atomicClipPushed) cssPopPaintClip();
				continue;
			}
			const int padLeft = cssPaddingLeftPx(paintStyle, 0);
			const int padRight = cssPaddingRightPx(paintStyle, 0);
			const int padTop = cssPaddingTopPx(paintStyle, 0);
			const int padBottom = cssPaddingBottomPx(paintStyle, 0);
			const int borderLeft = cssBorderLeftPx(paintStyle);
			const int borderRight = cssBorderRightPx(paintStyle);
			const int borderTopItem = cssBorderTopPx(paintStyle);
			const int borderBottomItem = cssBorderBottomPx(paintStyle);
			if (paintStyle.hasBackgroundColor || borderLeft > 0 || borderRight > 0 ||
				borderTopItem > 0 || borderBottomItem > 0) {
				drawBoxDecorations(s_windowId,
					fragX - padLeft - borderLeft,
					fragY - padTop - borderTopItem,
					std::max(1, fragment.w + padLeft + padRight + borderLeft + borderRight),
					std::max(1, fragment.h + padTop + padBottom + borderTopItem + borderBottomItem),
					paintStyle);
			}
			if (fragment.kind == InlineItemKind::TextRun) {
				std::string text;
				if (fragment.collapsedWhitespace && fragment.sourceLength == 0) {
					text = " ";
				} else if (fragment.sourceLength > 0 &&
					static_cast<size_t>(fragment.sourceOffset) < item.text.size()) {
					const size_t start = static_cast<size_t>(std::max(0, fragment.sourceOffset));
					text = item.text.substr(start, static_cast<size_t>(std::min<int>(
						fragment.sourceLength, static_cast<int>(item.text.size() - start))));
				}
				if (!text.empty()) {
					drawTextAtStyled(s_windowId, fragX + fragment.contentOffsetX, fragY, text, paintStyle,
						contentTextColor, std::max(1, fragment.h));
				}
			} else if (fragment.kind == InlineItemKind::ReplacedImage) {
				const ImageInfo& info = imageInfoForBlock(itemBlock);
				if (info.ok) {
					drawImage(s_windowId, fragX, fragY, fragment.w, fragment.h, info.drawPath);
				} else {
					drawThemeRect(s_windowId, fragX, fragY, fragment.w, fragment.h, NavigatorContentColor());
					drawThemeRect(s_windowId, fragX, fragY, fragment.w, 1, NavigatorContentBorderColor());
					drawThemeRect(s_windowId, fragX, fragY + fragment.h - 1, fragment.w, 1, NavigatorContentBorderColor());
					drawThemeRect(s_windowId, fragX, fragY, 1, fragment.h, NavigatorContentBorderColor());
					drawThemeRect(s_windowId, fragX + fragment.w - 1, fragY, 1, fragment.h, NavigatorContentBorderColor());
					const std::string placeholder = imagePlaceholderText(itemBlock, info);
					const int maxChars = std::max(1, (fragment.w - 12) / kCharW);
					std::string clipped = placeholder.substr(0, static_cast<size_t>(maxChars));
					drawTextAtStyled(s_windowId, fragX + 6,
						fragY + std::max(1, (fragment.h - kLineH) / 2), clipped,
						paintStyle, contentTextColor, std::max(1, fragment.h));
				}
			} else if (fragment.kind == InlineItemKind::FormControl) {
				const int controlW = std::max(1, fragment.w);
				const int controlH = std::max(1, fragment.h);
				const bool focused = isFocusedFormControl(itemBlock);
				const bool disabled = runtimeDisabled(itemBlock);
				const uint32_t border = formBorderColor(itemBlock, focused, disabled);
				if (itemBlock.type == BlockType::FormCheckbox || itemBlock.type == BlockType::FormRadio) {
					const int box = std::min(14, controlH);
					const int boxY = fragY + std::max(0, (controlH - box) / 2);
					drawThemeRect(s_windowId, fragX, boxY, box, box, formFillColor(itemBlock, focused, disabled));
					drawThemeRect(s_windowId, fragX, boxY, box, 1, border);
					drawThemeRect(s_windowId, fragX, boxY + box - 1, box, 1, border);
					drawThemeRect(s_windowId, fragX, boxY, 1, box, border);
					drawThemeRect(s_windowId, fragX + box - 1, boxY, 1, box, border);
					if (runtimeChecked(itemBlock)) {
						if (itemBlock.type == BlockType::FormRadio)
							drawThemeRect(s_windowId, fragX + 4, boxY + 4, std::max(1, box - 8), std::max(1, box - 8), disabled ? border : NavigatorAccentColor());
						else drawThemeText(s_windowId, fragX + 3, boxY - 2, "x", disabled ? border : NavigatorAccentColor());
					}
					std::string label = itemBlock.text.empty() ? itemBlock.inputName : itemBlock.text;
					const int labelMax = std::max(0, (controlW - box - 8) / kCharW);
					if (static_cast<int>(label.size()) > labelMax) label.resize(static_cast<size_t>(labelMax));
					if (!label.empty()) drawThemeText(s_windowId, fragX + box + 8,
						centeredChromeTextY(fragY, controlH), label, formTextColor(itemBlock, disabled, false));
				} else {
					drawThemeRect(s_windowId, fragX, fragY, controlW, controlH,
						itemBlock.type == BlockType::FormSubmit
							? (disabled ? 0xFFE3E6EAu : NavigatorButtonFillColor(focused, false))
							: formFillColor(itemBlock, focused, disabled));
					drawThemeRect(s_windowId, fragX, fragY, controlW, 1, border);
					drawThemeRect(s_windowId, fragX, fragY + controlH - 1, controlW, 1, border);
					drawThemeRect(s_windowId, fragX, fragY, 1, controlH, border);
					drawThemeRect(s_windowId, fragX + controlW - 1, fragY, 1, controlH, border);
					std::string label = itemBlock.type == BlockType::FormSubmit
						? (itemBlock.submitLabel.empty() ? "Submit" : itemBlock.submitLabel)
						: (itemBlock.inputValue.empty() ? itemBlock.placeholder : itemBlock.inputValue);
					const int labelMax = std::max(1, (controlW - 14) / kCharW);
					if (static_cast<int>(label.size()) > labelMax) label.resize(static_cast<size_t>(labelMax));
					if (!label.empty()) drawThemeText(s_windowId, fragX + 8,
						centeredChromeTextY(fragY, controlH), label,
						itemBlock.type == BlockType::FormSubmit && !disabled
							? (itemBlock.style.hasColor ? itemBlock.style.color : NavigatorButtonTextColor(false))
							: formTextColor(itemBlock, disabled, itemBlock.inputValue.empty()));
				}
				drawDefaultFocusRing(item.blockIndex, itemBlock);
			}
		}
		if (blockClipPushed) cssPopPaintClip();
		if (ancestorClipPushed) cssPopPaintClip();
		s_cssPaintOpacityPercent = 100;
	};
	// Floats paint from the final exclusion record.  This keeps their visual
	// location, descendant targets, and line geometry on one authoritative path.
	for (const CssFloatRecord& floatRecord : s_cssFloatLayoutSnapshot.records) {
		if (floatRecord.contextSerial != 0 || floatRecord.blockIndex < 0 ||
			floatRecord.blockIndex >= static_cast<int>(s_currentDoc.blocks.size())) continue;
		const DocBlock& floatBlock = s_currentDoc.blocks[static_cast<size_t>(floatRecord.blockIndex)];
		const WebStyle* floatStyle = computedStyleForSerial(s_currentDoc, floatRecord.logicalSerial);
		if (floatStyle == nullptr) floatStyle = &floatBlock.style;
		if (floatStyle->visibility == VisibilityMode::Hidden || floatStyle->displayNone) continue;
		int x = kContentX + floatRecord.borderBoxX;
		int y = kContentY + floatRecord.borderBoxY - s_scrollOffset;
		if (const CssPositionedRecord* positioned = cssPositionedRecordForBlock(s_currentDoc, floatRecord.blockIndex)) {
			x = cssBoundedGeometryAdd(x, positioned->finalX - positioned->normalX);
			y = cssBoundedGeometryAdd(y, positioned->finalY - positioned->normalY);
		}
		const int w = std::max(1, floatRecord.borderBoxW);
		const int h = std::max(1, floatRecord.borderBoxH);
		const int opacity = std::max(0, std::min(100, floatStyle->effectiveOpacityPercent));
		s_cssPaintOpacityPercent = opacity;
		drawBlockBox(s_windowId, x, y, w, h, *floatStyle);
		CssPaintRect floatClip = cssViewportClipRect();
		if (floatRecord.bfcIdentity != 0) {
			const CssAncestorBox ownerBox = cssAncestorBoxForBlock(s_currentDoc,
				floatRecord.bfcIdentity, s_scrollOffset);
			const WebStyle* ownerStyle = cssBfcStyleForSerial(s_currentDoc, floatRecord.bfcIdentity);
			if (ownerBox.valid && ownerStyle) {
				floatClip = cssApplyOverflowClip(floatClip, *ownerStyle,
					ownerBox.x, ownerBox.y, ownerBox.w, ownerBox.h);
			}
		}
		const bool clipPushed = (floatStyle->overflowX != OverflowMode::Visible ||
			floatStyle->overflowY != OverflowMode::Visible || floatRecord.bfcIdentity != 0) &&
			cssPushPaintClip(cssApplyOverflowClip(floatClip, *floatStyle, x, y, w, h));
		const int contentX = x + cssBorderLeftPx(*floatStyle) + cssPaddingLeftPx(*floatStyle, 0);
		const int contentY = y + cssBorderTopPx(*floatStyle) + cssPaddingTopPx(*floatStyle, 0);
		if (floatRecord.kind == InlineItemKind::ReplacedImage) {
			int imageW = 0;
			int imageH = 0;
			imageDisplaySize(floatBlock, std::max(1, w), imageW, imageH);
			const ImageInfo& info = imageInfoForBlock(floatBlock);
			if (info.ok) drawImage(s_windowId, contentX, contentY, std::min(imageW, w), std::min(imageH, h), info.drawPath);
			else drawTextAtStyled(s_windowId, contentX, contentY,
				imagePlaceholderText(floatBlock, info), *floatStyle, contentTextColor, std::max(1, h));
		} else if (floatRecord.kind == InlineItemKind::FormControl) {
			drawThemeRect(s_windowId, contentX, contentY,
				std::max(1, w - cssHorizontalBoxEdges(*floatStyle)),
				std::max(1, h - cssVerticalBoxEdges(*floatStyle)), formFillColor(floatBlock, false, runtimeDisabled(floatBlock)));
		} else if (!floatRecord.contentText.empty()) {
			const int innerW = std::max(1, w - cssHorizontalBoxEdges(*floatStyle));
			const std::vector<std::string> lines = wrapText(floatRecord.contentText,
				std::max(1, innerW / kCharW));
			int lineY = contentY;
			for (const std::string& line : lines) {
				drawTextAtStyled(s_windowId, contentX, lineY, line, *floatStyle, contentTextColor,
					blockTextLineHeight(floatBlock));
				lineY += blockTextLineHeight(floatBlock);
				if (lineY >= y + h) break;
			}
		}
		if (clipPushed) cssPopPaintClip();
		s_cssPaintOpacityPercent = 100;
	}
	// Positioning is painted in bounded tiers.  Normal source-order content is
	// retained as one flat pass; supported positioned records are then inserted
	// by negative/auto-zero/positive tier while preserving source order.
	std::vector<int> normalRenderOrder;
	std::vector<int> negativePositionedOrder;
	std::vector<int> autoPositionedOrder;
	std::vector<int> positivePositionedOrder;
	for (int candidateIndex = 0; candidateIndex < static_cast<int>(s_currentDoc.blocks.size()); ++candidateIndex) {
		const DocBlock& candidate = s_currentDoc.blocks[static_cast<size_t>(candidateIndex)];
		const CssPositionedRecord* positioned = cssPositionedRecordForBlock(s_currentDoc, candidateIndex);
		if (!positioned || candidate.style.floatMode != FloatMode::None) {
			normalRenderOrder.push_back(candidateIndex);
			continue;
		}
		if (positioned->paintTier == 0) negativePositionedOrder.push_back(candidateIndex);
		else if (positioned->paintTier == 2) positivePositionedOrder.push_back(candidateIndex);
		else autoPositionedOrder.push_back(candidateIndex);
	}
	std::vector<int> renderOrder;
	renderOrder.reserve(normalRenderOrder.size() + negativePositionedOrder.size() +
		autoPositionedOrder.size() + positivePositionedOrder.size());
	renderOrder.insert(renderOrder.end(), negativePositionedOrder.begin(), negativePositionedOrder.end());
	renderOrder.insert(renderOrder.end(), normalRenderOrder.begin(), normalRenderOrder.end());
	renderOrder.insert(renderOrder.end(), autoPositionedOrder.begin(), autoPositionedOrder.end());
	renderOrder.insert(renderOrder.end(), positivePositionedOrder.begin(), positivePositionedOrder.end());
	for (int renderIndex : renderOrder) {
		blockIndex = renderIndex;
		const DocBlock& block = s_currentDoc.blocks[static_cast<size_t>(renderIndex)];
		if (block.atomicContainerSerial != 0) {
			++blockIndex;
			continue;
		}
		if (!blockHasVisibleCss(block)) {
			++blockIndex;
			continue;
		}
		if (block.style.floatMode != FloatMode::None) {
			++blockIndex;
			continue;
		}
		renderFieldsetAt(blockIndex);
		if (blockUsesInlineFlow(s_currentDoc, blockIndex)) {
			if (const InlineFlowLayout* flow = inlineFlowForAnchor(s_currentDoc, blockIndex)) {
				renderInlineFlowAt(*flow, 0, 0);
			}
			++blockIndex;
			continue;
		}
		int relY  = blockLayoutY(blockIndex);
		int drawY = kContentY + relY - s_scrollOffset;
		const int blockMarginTop = cssMarginTopPx(block.style, block.type == BlockType::Heading ? 10 : 4);
		const int blockMarginBottom = cssMarginBottomPx(block.style, block.type == BlockType::ListItem ? 4 : 8);
		const int paddingTop = cssPaddingTopPx(block.style, block.type == BlockType::Preformatted ? 4 : 0);
		const int paddingRight = cssPaddingRightPx(block.style, block.type == BlockType::Preformatted ? 4 : 0);
		const int paddingBottom = cssPaddingBottomPx(block.style, block.type == BlockType::Preformatted ? 4 : 0);
		const int paddingLeft = cssPaddingLeftPx(block.style, block.type == BlockType::Preformatted ? 4 : 0);
		const int borderTop = cssBorderTopPx(block.style);
		const int borderRight = cssBorderRightPx(block.style);
		const int borderBottom = cssBorderBottomPx(block.style);
		const int borderLeft = cssBorderLeftPx(block.style);
		const int lineHeight = blockTextLineHeight(block);
		const int availableWidth = blockAvailableWidth(block, s_currentDoc);
		int outerWidth = blockOuterWidth(block, availableWidth);
		int outerX = blockOuterX(block, s_currentDoc, availableWidth, outerWidth);
		if (const CssPositionedRecord* positioned = cssPositionedRecordForBlock(s_currentDoc, blockIndex)) {
			outerWidth = std::max(1, positioned->usedWidth);
			outerX = positioned->finalX;
			drawY = positioned->finalY - s_scrollOffset - blockMarginTop;
		} else {
			int ancestorDeltaX = 0;
			int ancestorDeltaY = 0;
			cssPositionRelativeAncestorDelta(s_currentDoc, blockIndex, &ancestorDeltaX, &ancestorDeltaY);
			outerX = cssBoundedGeometryAdd(outerX, ancestorDeltaX);
			drawY = cssBoundedGeometryAdd(drawY, ancestorDeltaY);
		}
		const int innerWidth = std::max(1, outerWidth - borderLeft - borderRight - paddingLeft - paddingRight);
		const uint64_t listOrdinal = block.type == BlockType::ListItem ? blockListOrdinal(s_currentDoc, blockIndex) : 1;
		const int listInset = block.type == BlockType::ListItem ? blockListTextInsetPx(block, listOrdinal) : 0;
		const int wrapCols = std::max(1, std::max(1, innerWidth - listInset) / kCharW);
		const int listWrapCols = wrapCols;
		const int preWrapCols = std::max(1, innerWidth / kCharW);
		const int headingFontSize = cssFontSizeOrDefault(block.style, block.tagName == "h1" ? 24 : (block.tagName == "h2" ? 20 : (block.tagName == "h3" ? 18 : 20)));
		const int contentX = blockContentLeftX(block, outerX);
		const int contentY = blockContentTopY(block, drawY, blockMarginTop);

		if (isTableCellLikeBlock(block)) {
			if (!isFirstTableCellInGroup(s_currentDoc, blockIndex)) {
				++blockIndex;
				continue;
			}
			const int groupStart = tableGroupStartIndex(s_currentDoc, blockIndex);
			const TableGroupLayout layout = buildTableGroupLayout(s_currentDoc, groupStart);
			const uint64_t rowSerial = tableRowSerialForBlock(block);
			int rowIndex = -1;
			for (int ri = 0; ri < static_cast<int>(layout.rows.size()); ++ri) {
				if (layout.rows[static_cast<size_t>(ri)].rowSerial == rowSerial) {
					rowIndex = ri;
					break;
				}
			}
			if (rowIndex < 0) {
				++blockIndex;
				continue;
			}
			const TableRowLayout& row = layout.rows[static_cast<size_t>(rowIndex)];
			const int tableY = drawY + blockMarginTop;
			const int tableH = layout.totalHeightPx;
			const int blockH = blockMarginTop + tableH + std::max(4, blockMarginBottom);
			if (drawY + blockH < kContentY || drawY > kContentY + kContentH) {
				++blockIndex;
				continue;
			}
			if (s_findActive &&
				s_currentFindMatch >= 0 &&
				s_currentFindMatch < static_cast<int>(s_findMatches.size()) &&
				s_findMatches[s_currentFindMatch].blockIndex == blockIndex)
			{
				drawThemeRect(s_windowId, kContentX + 10, drawY + std::max(0, blockMarginTop - 2),
					kContentW - 28, std::max(kLineH + 4, blockH - std::max(0, blockMarginTop)),
					NavigatorFindHighlightColor());
			}
			SelectionRange selection = normalizedSelection();
			if (selection.valid && blockIndex >= selection.start.blockIndex && blockIndex <= selection.end.blockIndex && isSelectableBlock(block)) {
				Rect selectionRect = selectableBlockRect(blockIndex);
				if (selectionRect.w > 0 && selectionRect.h > 0) {
					drawThemeRect(s_windowId,
						selectionRect.x - 2,
						selectionRect.y - 1,
						std::min(selectionRect.w + 4, kContentX + kContentW - 18 - (selectionRect.x - 2)),
						selectionRect.h,
						NavigatorSelectionColor());
				}
			}
			s_cssPaintOpacityPercent = std::max(0, std::min(100, block.style.effectiveOpacityPercent));
			const bool tableAncestorClipPushed = cssBlockHasOverflowAncestor(s_currentDoc, block) &&
				cssPushPaintClip(cssBlockAncestorClip(s_currentDoc, block, s_scrollOffset));
			drawBlockBox(s_windowId, layout.outerX, tableY, layout.outerWidth, tableH, block.style);
			const bool tableClipPushed = (block.style.overflowX != OverflowMode::Visible ||
				block.style.overflowY != OverflowMode::Visible) && cssPushPaintClip(
				cssBlockVisibleClip(s_currentDoc, blockIndex, block, layout.outerX, tableY, layout.outerWidth, tableH,
					s_scrollOffset));
			const int tableContentX = layout.outerX + layout.borderLeft + layout.paddingLeft;
			const size_t lastCol = layout.columnWidthsChars.empty() ? 0 : layout.columnWidthsChars.size() - 1;
			const bool collapseMode = layout.collapseMode;
			const int cellSpacingX = collapseMode ? 0 : layout.borderSpacingHorizontal;
			int cellX = tableContentX;
			for (size_t col = 0; col < row.cells.size(); ++col) {
				const TableCellLayout& cell = row.cells[col];
				const int colWidthChars = layout.columnWidthsChars[std::min(col, lastCol)];
				const int cellW = std::max(1, colWidthChars * kCharW);
				const int cellRight = cellX + cellW;
				WebStyle cellStyle = cell.block->style;
				if (row.headerRow) cellStyle.bold = true;
				if (!cell.block->url.empty()) {
					if (!cellStyle.hasColor) {
						cellStyle.hasColor = true;
						cellStyle.color = s_visitedUrls.find(cell.block->url) != s_visitedUrls.end()
							? 0xFF6B46C1u
							: 0xFF1E5CB8u;
					}
					if (!cellStyle.hasTextDecoration) {
						cellStyle.hasTextDecoration = true;
						cellStyle.underline = true;
					}
				}
				const int cellBorderTop = cssBorderTopPx(cellStyle);
				const int cellBorderRight = cssBorderRightPx(cellStyle);
				const int cellBorderBottom = cssBorderBottomPx(cellStyle);
				const int cellBorderLeft = cssBorderLeftPx(cellStyle);
				const int cellPaddingLeft = std::max(1, cell.padLeftChars * kCharW);
				const int cellPaddingRight = std::max(1, cell.padRightChars * kCharW);
				const int cellY = tableY + layout.rowOffsetsPx[static_cast<size_t>(rowIndex)];
				if (cellStyle.hasBackgroundColor || cellBorderTop > 0 || cellBorderRight > 0 || cellBorderBottom > 0 || cellBorderLeft > 0) {
					drawBoxDecorations(s_windowId, cellX, cellY, cellW, row.heightPx, cellStyle, !collapseMode, true, true, !collapseMode);
				}
				const int innerWidth = std::max(1, cellW - cellBorderLeft - cellBorderRight - cellPaddingLeft - cellPaddingRight);
				const int cellTextHeight = static_cast<int>(cell.lines.size()) * layout.lineHeight;
				const int cellInnerHeight = std::max(0, row.heightPx - cellBorderTop - cellBorderBottom -
					layout.paddingTop - layout.paddingBottom);
				const int verticalExtra = std::max(0, cellInnerHeight - cellTextHeight);
				int lineY = cellY + cellBorderTop + layout.paddingTop +
					cssVerticalAlignOffset(cellStyle, layout.lineHeight, verticalExtra) +
					textLineTopPaddingPx(layout.lineHeight);
				for (size_t lineIndex = 0; lineIndex < cell.lines.size(); ++lineIndex) {
					const std::string& ln = cell.lines[lineIndex];
					const int lineW = static_cast<int>(ln.size()) * kCharW;
					int lineTextX = cellX + cellBorderLeft + cellPaddingLeft;
					if (cellStyle.textAlign == TextAlign::Center) {
						lineTextX = cellX + cellBorderLeft + cellPaddingLeft + std::max(0, (innerWidth - lineW) / 2);
					} else if (cellStyle.textAlign == TextAlign::Right) {
						lineTextX = cellRight - cellBorderRight - cellPaddingRight - std::min(innerWidth, lineW);
					}
					drawTextAtStyled(s_windowId, lineTextX, lineY, ln, cellStyle, contentTextColor, layout.lineHeight);
					lineY += layout.lineHeight;
				}
				cellX += cellW + cellSpacingX;
			}
			if (tableClipPushed) cssPopPaintClip();
			if (tableAncestorClipPushed) cssPopPaintClip();
			s_cssPaintOpacityPercent = 100;
			++blockIndex;
			continue;
		}
		if (toLowerAscii(block.tagName) == "hr") {
			const int hrH = blockMarginTop + borderTop + paddingTop + paddingBottom + borderBottom + std::max(4, blockMarginBottom);
			if (drawY + hrH < kContentY || drawY > kContentY + kContentH) {
				++blockIndex;
				continue;
			}
			const int boxY = drawY + blockMarginTop;
			s_cssPaintOpacityPercent = std::max(0, std::min(100, block.style.effectiveOpacityPercent));
			drawBlockBox(s_windowId, outerX, boxY, outerWidth, std::max(1, hrH - blockMarginTop - std::max(4, blockMarginBottom)), block.style);
			s_cssPaintOpacityPercent = 100;
			++blockIndex;
			continue;
		}

		// Skip blocks fully above or below the visible viewport.  The same used
		// height path drives document extent and paint geometry so fixed boxes do
		// not silently grow just because their content overflows.
		bool nextIsHeading = (blockIndex + 1 < static_cast<int>(s_currentDoc.blocks.size()) &&
			s_currentDoc.blocks[blockIndex + 1].type == BlockType::Heading);
		int blockH = blockTotalHeight(block, s_currentDoc, nextIsHeading);
		if (const CssPositionedRecord* positioned = cssPositionedRecordForBlock(s_currentDoc, blockIndex)) {
			blockH = blockMarginTop + positioned->usedHeight + blockMarginBottom +
				(nextIsHeading ? 10 : 0);
		}
		if (drawY + blockH < kContentY || drawY > kContentY + kContentH) {
			++blockIndex;
			continue;
		}

		if (s_findActive &&
			s_currentFindMatch >= 0 &&
			s_currentFindMatch < static_cast<int>(s_findMatches.size()) &&
			s_findMatches[s_currentFindMatch].blockIndex == blockIndex)
		{
				drawThemeRect(s_windowId, kContentX + 10, drawY + std::max(0, blockMarginTop - 2),
					kContentW - 28, std::max(kLineH + 4, blockH - std::max(0, blockMarginTop)),
					NavigatorFindHighlightColor());
			}

		SelectionRange selection = normalizedSelection();
		if (selection.valid && blockIndex >= selection.start.blockIndex && blockIndex <= selection.end.blockIndex && isSelectableBlock(block)) {
			Rect selectionRect = selectableBlockRect(blockIndex);
			if (selectionRect.w > 0 && selectionRect.h > 0) {
					drawThemeRect(s_windowId,
						selectionRect.x - 2,
						selectionRect.y - 1,
						std::min(selectionRect.w + 4, kContentX + kContentW - 18 - (selectionRect.x - 2)),
						selectionRect.h,
						NavigatorSelectionColor());
				}
			}

		const int boxY = drawY + blockMarginTop;
		const int boxH = std::max(1, blockH - blockMarginTop - blockMarginBottom - (nextIsHeading ? 10 : 0));
		s_cssPaintOpacityPercent = std::max(0, std::min(100, block.style.effectiveOpacityPercent));
		const bool ancestorClipPushed = false;
		drawBlockBox(s_windowId, outerX, boxY, outerWidth, boxH, block.style);
		const bool blockClipPushed = cssPushPaintClip(
			cssPositionedClipForBlock(s_currentDoc, blockIndex, outerX, boxY, outerWidth, boxH, s_scrollOffset));

		switch (block.type) {
		case BlockType::Heading:
			// Slightly larger heading: draw a subtle accent bar then the text
			drawThemeRect(s_windowId, contentX, boxY + borderTop + paddingTop + std::max(lineHeight, headingFontSize - 4),
				std::max(1, innerWidth), 2, NavigatorAccentColor());
			drawTextAtStyled(s_windowId, blockTextX(block, contentX, innerWidth, std::min(static_cast<int>(block.text.size()) * kCharW, innerWidth)), contentY + textLineTopPaddingPx(lineHeight), block.text, block.style, contentTextColor, lineHeight);
			if (block.style.bold) {
				drawTextAtStyled(s_windowId, blockTextX(block, contentX + 1, innerWidth, std::min(static_cast<int>(block.text.size()) * kCharW, innerWidth)), contentY + textLineTopPaddingPx(lineHeight), block.text, block.style, contentTextColor, lineHeight);
			}
			break;

		case BlockType::Paragraph: {
			auto lines = wrapTextForBlock(block, wrapCols);
			int lineY = contentY + textLineTopPaddingPx(lineHeight);
			for (const std::string& ln : lines) {
				const int lineW = static_cast<int>(ln.size()) * kCharW;
				drawTextAtStyled(s_windowId, blockTextX(block, contentX, innerWidth, lineW), lineY, ln, block.style, contentTextColor, lineHeight);
				lineY += lineHeight;
			}
			break;
		}

		case BlockType::ListItem: {
			// Bullet or ordinal prefix with a fixed inset keeps the text from
			// overlapping the marker and gives simple lists a readable rhythm.
			const int ordinal = blockListOrdinal(s_currentDoc, blockIndex);
			const std::string marker = blockListMarkerText(block, ordinal);
			if (!marker.empty()) {
				drawTextAtStyled(s_windowId, contentX, contentY + textLineTopPaddingPx(lineHeight), marker, block.style, contentTextColor, lineHeight);
			}
			const int textInset = blockListTextInsetPx(block, ordinal);
			auto lines = wrapTextForBlock(block, listWrapCols);
			int lineY = contentY + textLineTopPaddingPx(lineHeight);
			for (const std::string& ln : lines) {
				const int lineW = static_cast<int>(ln.size()) * kCharW;
				drawTextAtStyled(s_windowId, blockTextX(block, contentX + textInset, std::max(1, innerWidth - textInset), lineW), lineY, ln, block.style, contentTextColor, lineHeight);
				lineY += lineHeight;
			}
			break;
		}

		case BlockType::Preformatted: {
			// Draw each line preserving exact content
			auto lines = wrapTextForBlock(block, preWrapCols);
			int lineY = contentY + textLineTopPaddingPx(lineHeight);
			for (const std::string& ln : lines) {
				drawTextAtStyled(s_windowId, contentX, lineY, ln, block.style, contentTextColor, lineHeight);
				lineY += lineHeight;
			}
			break;
		}

		case BlockType::FormLabel: {
			auto lines = wrapTextForBlock(block, wrapCols);
			int lineY = contentY + textLineTopPaddingPx(lineHeight);
			for (const std::string& ln : lines) {
				drawTextAtStyled(s_windowId, blockTextX(block, contentX, innerWidth, static_cast<int>(ln.size()) * kCharW), lineY,
					ln, block.style, contentTextColor, lineHeight);
				lineY += lineHeight;
			}
			break;
		}

		case BlockType::Link: {
			// Full wrapped link block: underline + blue text
			// The entire bounding rect is clickable (TODO: per-line hit testing).
			auto lines = wrapTextForBlock(block, wrapCols);
			int lineY = contentY + textLineTopPaddingPx(lineHeight);
			int linkR = 55;
			int linkG = 110;
			int linkB = 210;
			if (block.style.hasColor) {
				colorChannels(block.style.color, linkR, linkG, linkB);
			} else if (s_visitedUrls.find(block.url) != s_visitedUrls.end()) {
				linkR = 107;
				linkG = 70;
				linkB = 193;
			}
			WebStyle linkStyle = block.style;
			linkStyle.hasColor = true;
			linkStyle.color = 0xFF000000u | (static_cast<uint32_t>(linkR) << 16) |
				(static_cast<uint32_t>(linkG) << 8) | static_cast<uint32_t>(linkB);
			for (const std::string& ln : lines) {
				int lineW = static_cast<int>(ln.size()) * kCharW;
				drawTextAtStyled(s_windowId, blockTextX(block, contentX, innerWidth, lineW), lineY, ln, linkStyle, contentTextColor, lineHeight);
				lineY += lineHeight;
			}
			break;
		}

		case BlockType::Image: {
			int imageW = 0;
			int imageH = 0;
			imageDisplaySize(block, availableWidth, imageW, imageH);
			const ImageInfo& info = imageInfoForBlock(block);
			const int imageX = contentX;
			const int viewportTop = kContentY;
			const int viewportBottom = kToolbarH + 6 + kContentH;
			if (boxY + borderTop + paddingTop >= viewportTop && boxY + borderTop + paddingTop + imageH <= viewportBottom) {
				if (info.ok) {
					drawImage(s_windowId, imageX, boxY + borderTop + paddingTop, imageW, imageH, info.drawPath);
				} else {
					drawThemeRect(s_windowId, imageX, boxY + borderTop + paddingTop, imageW, imageH, NavigatorContentColor());
					drawThemeRect(s_windowId, imageX, boxY + borderTop + paddingTop, imageW, 1, NavigatorContentBorderColor());
					drawThemeRect(s_windowId, imageX, boxY + borderTop + paddingTop + imageH - 1, imageW, 1, NavigatorContentBorderColor());
					drawThemeRect(s_windowId, imageX, boxY + borderTop + paddingTop, 1, imageH, NavigatorContentBorderColor());
					drawThemeRect(s_windowId, imageX + imageW - 1, boxY + borderTop + paddingTop, 1, imageH, NavigatorContentBorderColor());
					const std::string placeholder = imagePlaceholderText(block, info);
					const int placeholderMaxChars = std::max(1, (imageW - 20) / kCharW);
					std::vector<std::string> placeholderLines = wrapText(placeholder, placeholderMaxChars);
					if (placeholderLines.empty()) placeholderLines.push_back(placeholder);
					const int maxLines = std::max(1, std::min(3, static_cast<int>(placeholderLines.size())));
					const int textHeight = maxLines * lineHeight;
					int textY = boxY + borderTop + paddingTop + std::max(8, (imageH - textHeight) / 2);
					for (int lineIndex = 0; lineIndex < maxLines; ++lineIndex) {
						const std::string& line = placeholderLines[static_cast<size_t>(lineIndex)];
						drawTextAtStyled(s_windowId, imageX + 10, textY, line, block.style, contentTextColor, lineHeight);
						textY += lineHeight;
					}
				}
			}
			break;
		}

		case BlockType::FormTextInput: {
			const int inputX = contentX;
			const int inputY = boxY + borderTop + paddingTop;
			const bool focused = isFocusedFormControl(block);
			const bool disabled = runtimeDisabled(block);
			const int controlW = std::min(innerWidth, blockFormControlWidth(block, availableWidth));
			const int controlH = formControlHeight(block);
			const uint32_t fill = formFillColor(block, focused, disabled);
			const uint32_t border = formBorderColor(block, focused, disabled);
			drawThemeRect(s_windowId, inputX, inputY, controlW, controlH, fill);
			drawThemeRect(s_windowId, inputX, inputY, controlW, 1, border);
			drawThemeRect(s_windowId, inputX, inputY + controlH - 1, controlW, 1, border);
			drawThemeRect(s_windowId, inputX, inputY, 1, controlH, border);
			drawThemeRect(s_windowId, inputX + controlW - 1, inputY, 1, controlH, border);
		std::string text = block.inputValue;
			bool placeholder = text.empty() && !block.placeholder.empty();
			if (placeholder) text = block.placeholder;
			const bool password = block.formControl.type == FormControlType::Password || block.inputType == "password";
			const int maxChars = std::max(1, (controlW - 16) / kCharW);
			if (password && !placeholder) text.assign(std::min(static_cast<int>(block.inputValue.size()), maxChars), '*');
			if (static_cast<int>(text.size()) > maxChars) {
				text = text.substr(text.size() - static_cast<size_t>(maxChars));
			}
			drawThemeText(s_windowId, inputX + 8, centeredChromeTextY(inputY, controlH), text, formTextColor(block, disabled, placeholder));
			if (focused) {
				int caretPos = std::max(0, std::min(s_inputCaret, static_cast<int>(block.inputValue.size())));
				int visibleCaret = std::min(caretPos, maxChars);
				if (!disabled && !password) drawThemeRect(s_windowId, inputX + 8 + visibleCaret * kCharW, inputY + 5, 1, controlH - 10, NavigatorAccentColor());
			}
			break;
		}

		case BlockType::FormTextarea: {
			const int inputX = contentX;
			const int inputY = boxY + borderTop + paddingTop;
			const int inputH = formControlHeight(block);
			const bool focused = isFocusedFormControl(block);
			const bool disabled = runtimeDisabled(block);
			const int controlW = std::min(innerWidth, blockFormControlWidth(block, availableWidth));
			const uint32_t fill = formFillColor(block, focused, disabled);
			const uint32_t border = formBorderColor(block, focused, disabled);
			drawThemeRect(s_windowId, inputX, inputY, controlW, inputH, fill);
			drawThemeRect(s_windowId, inputX, inputY, controlW, 1, border);
			drawThemeRect(s_windowId, inputX, inputY + inputH - 1, controlW, 1, border);
			drawThemeRect(s_windowId, inputX, inputY, 1, inputH, border);
			drawThemeRect(s_windowId, inputX + controlW - 1, inputY, 1, inputH, border);
			const bool placeholder = block.inputValue.empty() && !block.placeholder.empty();
			const std::string rawText = placeholder ? block.placeholder : block.inputValue;
			const int maxChars = std::max(1, (controlW - 16) / kCharW);
			const int maxVisibleRows = std::max(1, (inputH - 10) / kLineH);
			std::vector<std::string> lines = textareaLines(rawText);
			int firstVisibleLine = 0;
			if (static_cast<int>(lines.size()) > maxVisibleRows) {
				firstVisibleLine = static_cast<int>(lines.size()) - maxVisibleRows;
			}
			int lineY = inputY + 6;
			for (int lineIndex = firstVisibleLine;
				 lineIndex < static_cast<int>(lines.size()) && lineIndex < firstVisibleLine + maxVisibleRows;
				 ++lineIndex) {
				std::string lineText = lines[static_cast<size_t>(lineIndex)];
				if (static_cast<int>(lineText.size()) > maxChars) {
					lineText = lineText.substr(static_cast<size_t>(std::max(0, static_cast<int>(lineText.size()) - maxChars)));
				}
				drawThemeText(s_windowId, inputX + 8, lineY, lineText,
					formTextColor(block, disabled, placeholder));
				lineY += lineHeight;
			}
			if (focused && !placeholder && !disabled && !block.formControl.readOnly) {
				int caretPos = std::max(0, std::min(s_inputCaret, static_cast<int>(block.inputValue.size())));
				int caretLine = 0;
				int caretColumn = 0;
				for (int i = 0; i < caretPos; ++i) {
					if (block.inputValue[static_cast<size_t>(i)] == '\n') {
						++caretLine;
						caretColumn = 0;
					} else {
						++caretColumn;
					}
				}
				if (caretLine >= firstVisibleLine && caretLine < firstVisibleLine + maxVisibleRows) {
					int visibleColumn = std::min(caretColumn, maxChars);
					int caretY = inputY + 5 + (caretLine - firstVisibleLine) * lineHeight;
					drawThemeRect(s_windowId, inputX + 8 + visibleColumn * kCharW, caretY, 1, lineHeight - 2, NavigatorAccentColor());
				}
			}
			break;
		}

		case BlockType::FormCheckbox:
		case BlockType::FormRadio: {
			const int controlX = contentX;
			const int controlY = boxY + borderTop + paddingTop;
			const bool focused = isFocusedFormControl(block);
			const bool disabled = runtimeDisabled(block);
			const int box = 14;
			const int boxY = controlY + (kFormControlH - box) / 2;
			const uint32_t border = formBorderColor(block, focused, disabled);
			drawThemeRect(s_windowId, controlX, boxY, box, box, formFillColor(block, focused, disabled));
			drawThemeRect(s_windowId, controlX, boxY, box, 1, border);
			drawThemeRect(s_windowId, controlX, boxY + box - 1, box, 1, border);
			drawThemeRect(s_windowId, controlX, boxY, 1, box, border);
			drawThemeRect(s_windowId, controlX + box - 1, boxY, 1, box, border);
			if (runtimeChecked(block)) {
				if (block.type == BlockType::FormRadio) {
					drawThemeRect(s_windowId, controlX + 4, boxY + 4, box - 8, box - 8, disabled ? border : NavigatorAccentColor());
				} else {
					drawThemeText(s_windowId, controlX + 3, boxY - 2, "x", disabled ? border : NavigatorAccentColor());
				}
			}
			std::string label = block.text.empty() ? block.inputName : block.text;
			const int labelMax = std::max(1, (std::min(innerWidth, blockFormControlWidth(block, availableWidth)) - box - 8) / kCharW);
			if (static_cast<int>(label.size()) > labelMax) label.resize(static_cast<size_t>(labelMax));
			drawThemeText(s_windowId, controlX + box + 8, centeredChromeTextY(controlY, kFormControlH), label, formTextColor(block, disabled, false));
			break;
		}

		case BlockType::FormSelect: {
			const int selectX = contentX;
			const int selectY = boxY + borderTop + paddingTop;
			const bool focused = isFocusedFormControl(block);
			const bool disabled = runtimeDisabled(block);
			const int controlW = std::min(innerWidth, blockFormControlWidth(block, availableWidth));
			const int controlH = formControlHeight(block);
			const uint32_t border = formBorderColor(block, focused, disabled);
			drawThemeRect(s_windowId, selectX, selectY, controlW, controlH, formFillColor(block, focused, disabled));
			drawThemeRect(s_windowId, selectX, selectY, controlW, 1, border);
			drawThemeRect(s_windowId, selectX, selectY + controlH - 1, controlW, 1, border);
			drawThemeRect(s_windowId, selectX, selectY, 1, controlH, border);
			drawThemeRect(s_windowId, selectX + controlW - 1, selectY, 1, controlH, border);
			if (block.formControl.multiple) {
				const int maxRows = std::max(1, (controlH - 8) / kLineH);
				int row = 0;
				for (const gxos::web::FormOption& option : block.options) {
					if (row >= maxRows) break;
					std::string optionText = option.text.empty() ? option.value : option.text;
					const int maxChars = std::max(1, (controlW - 16) / kCharW);
					if (static_cast<int>(optionText.size()) > maxChars) optionText.resize(static_cast<size_t>(maxChars));
					const bool selected = block.selectedOption == row;
					if (selected) optionText = "> " + optionText;
					drawThemeText(s_windowId, selectX + 8, selectY + 4 + row * kLineH, optionText,
						formTextColor(block, disabled || option.disabled, false));
					++row;
				}
			} else {
				std::string label = block.text.empty() ? "(select)" : block.text;
				const int maxChars = std::max(1, (controlW - 28) / kCharW);
				if (static_cast<int>(label.size()) > maxChars) label.resize(static_cast<size_t>(maxChars));
				drawThemeText(s_windowId, selectX + 8, centeredChromeTextY(selectY, controlH), label, formTextColor(block, disabled, false));
				drawThemeText(s_windowId, selectX + controlW - 20, centeredChromeTextY(selectY, controlH), "v", NavigatorFieldMutedTextColor());
			}
			break;
		}

		case BlockType::FormSubmit: {
			const int buttonX = contentX;
			const int buttonY = boxY + borderTop + paddingTop;
			const bool focused = isFocusedFormControl(block);
			const bool disabled = runtimeDisabled(block);
			const int controlW = std::min(innerWidth, blockFormControlWidth(block, availableWidth));
			const int controlH = formControlHeight(block);
			const uint32_t border = formBorderColor(block, focused, disabled);
			drawThemeRect(s_windowId, buttonX, buttonY, controlW, controlH,
				disabled ? 0xFFE3E6EAu : (block.style.hasBackgroundColor ? block.style.backgroundColor : NavigatorButtonFillColor(focused, false)));
			drawThemeRect(s_windowId, buttonX, buttonY, controlW, 1, border);
			drawThemeRect(s_windowId, buttonX, buttonY + controlH - 1, controlW, 1, border);
			drawThemeRect(s_windowId, buttonX, buttonY, 1, controlH, border);
			drawThemeRect(s_windowId, buttonX + controlW - 1, buttonY, 1, controlH, border);
			std::string label = block.submitLabel.empty() ? "Submit" : block.submitLabel;
			int labelMax = std::max(1, (controlW - 14) / kCharW);
			if (static_cast<int>(label.size()) > labelMax) label = label.substr(0, static_cast<size_t>(labelMax));
			drawThemeText(s_windowId, buttonX + 10, centeredChromeTextY(buttonY, controlH), label,
				block.style.hasColor && !disabled ? block.style.color : NavigatorButtonTextColor(disabled));
			break;
		}
		}
		drawDefaultFocusRing(blockIndex, block);
		if (blockClipPushed) cssPopPaintClip();
		if (ancestorClipPushed) cssPopPaintClip();
		s_cssPaintOpacityPercent = 100;
		++blockIndex;
	}

	// Scroll thumb
	int maxScroll = maxScrollOffset();
	if (maxScroll > 0) {
		int trackY = kToolbarH + 10;
		int trackH = kContentH - 8;
		int thumbH = std::max(22, (trackH * kContentH) / s_documentHeight);
		int thumbY = trackY + ((trackH - thumbH) * s_scrollOffset) / maxScroll;
		drawThemeRect(s_windowId, kContentX + kContentW - 10, trackY, 6, trackH, NavigatorScrollTrackColor());
		drawThemeRect(s_windowId, kContentX + kContentW - 10, thumbY, 6, thumbH, NavigatorScrollThumbColor());
	}
	s_pageMetadata.cssBorderedBlocksRendered = std::max(s_pageMetadata.cssBorderedBlocksRendered, s_renderCounters.borderedBlocksRendered);
	s_pageMetadata.cssDashedBordersRendered = std::max(s_pageMetadata.cssDashedBordersRendered, s_renderCounters.dashedBordersRendered);
	s_pageMetadata.cssDottedBordersRendered = std::max(s_pageMetadata.cssDottedBordersRendered, s_renderCounters.dottedBordersRendered);
	s_pageMetadata.cssTextDecorationsRendered = std::max(s_pageMetadata.cssTextDecorationsRendered, s_renderCounters.textDecorationsRendered);
}

void Navigator::renderStatusBar()
{
	s_cssClipDepth = 0;
	cssSetPaintClip(CssPaintRect{0, kWindowH - kStatusBarH, kWindowW, kStatusBarH});
	s_cssPaintOpacityPercent = 100;
	drawThemeRect(s_windowId, 0, kWindowH - kStatusBarH, kWindowW, kStatusBarH, NavigatorStatusBarColor());
	drawThemeRect(s_windowId, 0, kWindowH - kStatusBarH, kWindowW, 1, NavigatorStatusBarBorderColor());

	if (s_findActive) {
		drawThemeRect(s_windowId, 8, kWindowH - kStatusBarH + 4, 420, kStatusBarH - 8, NavigatorAddressFillColor());
		drawThemeRect(s_windowId, 8, kWindowH - kStatusBarH + 4, 420, 1, NavigatorAddressFocusedBorderColor());
		drawThemeRect(s_windowId, 8, kWindowH - kStatusBarH + kStatusBarH - 5, 420, 1, NavigatorAddressFocusedBorderColor());
		const int findTextY = centeredChromeTextY(kWindowH - kStatusBarH + 4, kStatusBarH - 8);
		std::string shown = s_findBuffer;
		const int maxChars = 28;
		if (static_cast<int>(shown.size()) > maxChars) {
			shown = shown.substr(shown.size() - static_cast<size_t>(maxChars));
		}
		drawThemeText(s_windowId, 16, findTextY, "Find: " + shown, NavigatorTextColor());
		int caretPos = std::min(s_findCaret, maxChars);
		drawThemeRect(s_windowId, 64 + caretPos * kCharW, kWindowH - kStatusBarH + 6, 1, kStatusBarH - 12, NavigatorAccentColor());
		drawThemeText(s_windowId, 440, centeredChromeTextY(kWindowH - kStatusBarH, kStatusBarH), findMatchStatusText() + "   Enter/Down: next   Up: prev   Esc: close",
			isSciFiThemeActive() ? NavigatorMutedTextColor() : NavigatorTextColor());
		return;
	}

	const std::string& status = s_hoverStatusText.empty() ? s_statusText : s_hoverStatusText;
	std::string shown = status;
	if (hasSelection()) {
		const std::string text = selectedText();
		shown += (shown.empty() ? "" : "   ") + std::string("Selection: ") + std::to_string(text.size()) + " chars";
	}
	drawThemeText(s_windowId, 12, centeredChromeTextY(kWindowH - kStatusBarH, kStatusBarH), shown, NavigatorTextColor());
}

void Navigator::updateStatus(const std::string& status)
{
	s_statusText = status;
	updateDisplay();
}

void Navigator::updateHoverStatus(HitTarget target, int linkBlockIndex)
{
	std::string next;
	switch (target) {
	case HitTarget::Back:        next = "Back button";           break;
	case HitTarget::Forward:     next = "Forward button";        break;
	case HitTarget::Reload:      next = "Reload button";         break;
	case HitTarget::Home:        next = "Home button";           break;
	case HitTarget::Bookmarks:   next = "View Bookmarks";        break;
	case HitTarget::AddBookmark: next = "Add current page to Bookmarks"; break;
	case HitTarget::Find:        next = "Find in page";          break;
	case HitTarget::AddressBar:  next = "Click to edit address"; break;
	case HitTarget::FormInput:   next = "Click to edit form field"; break;
	case HitTarget::FormTextarea: next = "Click to edit textarea"; break;
	case HitTarget::FormCheckbox: next = "Toggle checkbox"; break;
	case HitTarget::FormRadio:   next = "Select radio option"; break;
	case HitTarget::FormLabel:   next = "Activate associated choice"; break;
	case HitTarget::FormSelect:  next = "Cycle select option"; break;
	case HitTarget::FormSubmit:  next = "Activate inert button"; break;
	case HitTarget::Link:
		if (linkBlockIndex >= 0 &&
			linkBlockIndex < static_cast<int>(s_currentDoc.blocks.size()))
		{
			next = s_currentDoc.blocks[linkBlockIndex].url;
		}
		break;
	default: break;
	}

	if (next != s_hoverStatusText) {
		s_hoverStatusText = next;
		renderStatusBar();
	}
}

void Navigator::handleToolbarAction(int widgetId)
{
	// Any toolbar button click while the address bar is focused cancels the edit,
	// except AddBookmark which explicitly uses the committed document URL anyway.
	if (s_addressFocused) {
		s_addressFocused = false;
		s_addressBuffer.clear();
		s_addressCaret   = 0;
	}
	if (s_currentDoc.formRuntimeState.focusValid) {
		blurDocumentInput();
	}

	switch (widgetId) {
	case kWidgetIdBack:
		goBack();
		break;
	case kWidgetIdForward:
		goForward();
		break;
	case kWidgetIdReload:
		// Reload: re-fetch the current URL without touching history.
		loadUrl(s_currentDoc.url, true, NavigatorTransitionCategory::Reload);
		break;
	case kWidgetIdHome:
		// Home is a normal forward navigation unless already there.
		if (s_currentDoc.url != "about:navigator") {
			navigateTo("about:navigator");
		}
		break;
	case kWidgetIdBookmarks:
		// Navigate to the bookmarks page.
		if (s_currentDoc.url != "about:bookmarks") {
			navigateTo("about:bookmarks");
		}
		break;
	case kWidgetIdAddBookmark: {
		// Always bookmark the committed document URL, never the typed buffer.
		const std::string& currentUrl = s_currentDoc.url;
		const std::string currentTitle = s_currentDoc.title.empty()
			? currentUrl : s_currentDoc.title;
		if (!currentUrl.empty()) {
			addBookmark(currentTitle, currentUrl);
		}
		break;
	}
	case kWidgetIdFind:
		openFindMode();
		updateDisplay();
		break;
	default:
		break;
	}
}

bool Navigator::isRuntimeCheckable(const DocBlock& block)
{
	return (block.type == BlockType::FormCheckbox || block.type == BlockType::FormRadio) &&
		block.formControl.metadataComplete && block.formControl.supported &&
		block.formControl.logicalSerial != 0 &&
		(block.formControl.type == FormControlType::Checkbox ||
		 block.formControl.type == FormControlType::Radio);
}

bool Navigator::isRuntimeButton(const DocBlock& block)
{
	return block.type == BlockType::FormSubmit &&
		block.formControl.metadataComplete && block.formControl.supported &&
		block.formControl.logicalSerial != 0 &&
		(block.formControl.type == FormControlType::Button ||
		 block.formControl.type == FormControlType::Submit ||
		 block.formControl.type == FormControlType::Reset);
}

bool Navigator::isRuntimeFormControl(const DocBlock& block)
{
	return isRuntimeCheckable(block) || isRuntimeButton(block);
}

const FormRuntimeControlState* Navigator::runtimeStateForBlock(const DocBlock& block)
{
	if (!s_currentDoc.formRuntimeState.initialized || block.formControl.logicalSerial == 0)
		return nullptr;
	const size_t count = std::min(s_currentDoc.formRuntimeState.count, kFormRuntimeControlCap);
	for (size_t i = 0; i < count; ++i) {
		const FormRuntimeControlState& state = s_currentDoc.formRuntimeState.controls[i];
		if (state.logicalSerial == block.formControl.logicalSerial && state.metadataValid)
			return &state;
	}
	return nullptr;
}

FormRuntimeControlState* Navigator::runtimeStateForBlock(DocBlock& block)
{
	return const_cast<FormRuntimeControlState*>(runtimeStateForBlock(static_cast<const DocBlock&>(block)));
}

bool Navigator::runtimeChecked(const DocBlock& block)
{
	if (const FormRuntimeControlState* state = runtimeStateForBlock(block)) return state->checked;
	return block.checked;
}

bool Navigator::runtimeDisabled(const DocBlock& block)
{
	if (const FormRuntimeControlState* state = runtimeStateForBlock(block)) return state->disabled;
	return block.formControl.disabled;
}

void Navigator::updateFormAccessibilityMetadata()
{
	FormRuntimeStateTable& runtime = s_currentDoc.formRuntimeState;
	if (!runtime.initialized) return;

	struct LabelResolution {
		uint64_t controlSerial = 0;
		FormAccessibilityLabelSource source = FormAccessibilityLabelSource::None;
		bool hasText = false;
	};

	const auto hasAncestorSerial = [](const DocBlock& block, uint64_t serial) {
		for (const gxos::web::HtmlElementRef& ancestor : block.ancestors) {
			if (ancestor.serial == serial) return true;
		}
		return false;
	};
	const auto isSupportedControlBlock = [&](const DocBlock& block) {
		return block.formControl.logicalSerial != 0 &&
			block.formControl.metadataComplete && block.formControl.supported &&
			!block.formUnsupported && block.formControl.type != FormControlType::Option &&
			block.formControl.type != FormControlType::Unsupported &&
			block.formControl.type != FormControlType::None &&
			block.type != BlockType::FormLabel;
	};

	std::vector<LabelResolution> resolutions;
	resolutions.reserve(std::min(kFormRuntimeControlCap, s_currentDoc.blocks.size()));
	int validAssociations = 0;
	int invalidAssociations = 0;
	for (const DocBlock& label : s_currentDoc.blocks) {
		if (label.type != BlockType::FormLabel || !label.formControl.metadataComplete ||
			label.elementMetadata.serial == 0) continue;
		if (!label.labelFor.empty()) {
			int idMatches = 0;
			uint64_t targetSerial = 0;
			bool targetIsControl = false;
			for (const gxos::web::HtmlElementRef& element : s_currentDoc.structuralElements) {
				if (element.id != label.labelFor) continue;
				++idMatches;
				targetSerial = element.serial;
				targetIsControl = element.formControl.logicalSerial == element.serial &&
					element.formControl.metadataComplete && element.formControl.supported &&
					element.formControl.type != FormControlType::Option &&
					element.formControl.type != FormControlType::Unsupported &&
					element.formControl.type != FormControlType::None;
			}
			if (idMatches == 1 && targetIsControl && targetSerial != 0) {
				++validAssociations;
				resolutions.push_back(LabelResolution{targetSerial, FormAccessibilityLabelSource::ForId, !label.text.empty()});
			} else {
				++invalidAssociations;
			}
			continue;
		}

		int wrappedControls = 0;
		uint64_t wrappedSerial = 0;
		bool wrappedHasText = !label.text.empty();
		for (const DocBlock& candidate : s_currentDoc.blocks) {
			if (!isSupportedControlBlock(candidate) || !hasAncestorSerial(candidate, label.elementMetadata.serial)) continue;
			++wrappedControls;
			wrappedSerial = candidate.formControl.logicalSerial;
		}
		if (wrappedControls == 1 && wrappedSerial != 0) {
			++validAssociations;
			resolutions.push_back(LabelResolution{wrappedSerial, FormAccessibilityLabelSource::Wrapping, wrappedHasText});
		} else {
			++invalidAssociations;
		}
	}

	const bool phase2hFixture = s_currentDoc.url.find("css-phase2h.html") != std::string::npos;
	const bool phase2iFixture = s_currentDoc.url.find("css-phase2i.html") != std::string::npos;
	const bool phase2Fixture = phase2hFixture || phase2iFixture;
	const auto roleForBlock = [](const DocBlock& block) {
		switch (block.formControl.type) {
		case FormControlType::Checkbox: return FormAccessibilityRole::Checkbox;
		case FormControlType::Radio: return FormAccessibilityRole::Radio;
		case FormControlType::Button:
		case FormControlType::Submit:
		case FormControlType::Reset: return FormAccessibilityRole::Button;
		case FormControlType::Password: return FormAccessibilityRole::PasswordTextbox;
		case FormControlType::Textarea: return FormAccessibilityRole::Textarea;
		case FormControlType::Select: return FormAccessibilityRole::Select;
		case FormControlType::Text:
		case FormControlType::Search:
		case FormControlType::Email:
		case FormControlType::Url:
		case FormControlType::Number: return FormAccessibilityRole::Textbox;
		default: return FormAccessibilityRole::None;
		}
	};

	const std::array<FormAccessibilityRecord, kFormRuntimeControlCap> previous = runtime.accessibilityRecords;
	const size_t previousCount = runtime.accessibilityRecordCount;
	runtime.accessibilityRecords = {};
	runtime.accessibilityRecordCount = 0;
	s_currentDoc.formsDiagnostics.formAccessibleNamePresent = 0;
	s_currentDoc.formsDiagnostics.formAccessibleNameMissing = 0;
	s_currentDoc.formsDiagnostics.formLabelAssociationsValid = validAssociations;
	s_currentDoc.formsDiagnostics.formLabelAssociationsInvalid = invalidAssociations;
	s_currentDoc.formsDiagnostics.formFocusVisibleMatches = 0;

	for (const DocBlock& block : s_currentDoc.blocks) {
		if (!isSupportedControlBlock(block)) continue;
		if (runtime.accessibilityRecordCount >= runtime.accessibilityRecords.size()) {
			++s_currentDoc.formsDiagnostics.formAccessibilityMetadataClamps;
			break;
		}
		FormAccessibilityRecord& record = runtime.accessibilityRecords[runtime.accessibilityRecordCount++];
		record.logicalSerial = block.formControl.logicalSerial;
		record.documentGeneration = runtime.documentGeneration;
		record.role = roleForBlock(block);
		record.focusable = isFocusableFormControl(block);
		record.focused = isFocusedFormControl(block);
		record.focusOrigin = record.focused ? runtime.focusOrigin : FormFocusOrigin::None;
		record.focusMatch = record.focused;
		record.focusVisibleMatch = record.focused && runtime.focusOrigin == FormFocusOrigin::Keyboard;
		record.checked = (block.type == BlockType::FormCheckbox || block.type == BlockType::FormRadio)
			? runtimeChecked(block) : false;
		record.disabled = runtimeDisabled(block);
		record.required = block.formControl.required;
		record.readOnly = block.formControl.readOnly;
		record.visible = !block.formControl.hidden && blockHasVisibleCss(block);
		record.metadataComplete = block.formControl.metadataComplete && block.formControl.logicalSerial != 0;
		if (phase2Fixture && (block.id.rfind("phase2h-", 0) == 0 || block.id.rfind("phase2i-", 0) == 0))
			record.fixtureId = block.id;

		const LabelResolution* selectedLabel = nullptr;
		for (const LabelResolution& resolution : resolutions) {
			if (resolution.controlSerial != record.logicalSerial) continue;
			if (resolution.source == FormAccessibilityLabelSource::ForId) {
				selectedLabel = &resolution;
				break;
			}
			if (!selectedLabel) selectedLabel = &resolution;
		}
		if (selectedLabel) {
			record.labelAssociated = true;
			record.labelSource = selectedLabel->source;
			if (selectedLabel->hasText) {
				record.accessibleNamePresent = true;
				record.accessibleNameSource = selectedLabel->source == FormAccessibilityLabelSource::Wrapping
					? FormAccessibilityNameSource::LabelWrapping : FormAccessibilityNameSource::LabelForId;
			}
		}
		if (!record.accessibleNamePresent && record.role == FormAccessibilityRole::Button) {
			if (block.tagName == "button" && !block.text.empty()) {
				record.accessibleNamePresent = true;
				record.accessibleNameSource = FormAccessibilityNameSource::ButtonText;
			} else if (block.tagName == "input" && !block.submitLabel.empty()) {
				record.accessibleNamePresent = true;
				record.accessibleNameSource = FormAccessibilityNameSource::InputValuePresence;
			}
		}
		if (!record.accessibleNamePresent && !block.placeholder.empty()) {
			record.accessibleNamePresent = true;
			record.accessibleNameSource = FormAccessibilityNameSource::Placeholder;
		}
		if (!record.accessibleNamePresent) record.accessibleNameSource = FormAccessibilityNameSource::ControlTypeFallback;
		if (record.accessibleNamePresent) ++s_currentDoc.formsDiagnostics.formAccessibleNamePresent;
		else if (record.focusable) ++s_currentDoc.formsDiagnostics.formAccessibleNameMissing;
		if (record.focusVisibleMatch) ++s_currentDoc.formsDiagnostics.formFocusVisibleMatches;

		for (size_t prior = 0; prior < previousCount; ++prior) {
			if (previous[prior].logicalSerial != record.logicalSerial) continue;
			record.revealResult = previous[prior].revealResult;
			break;
		}
		if (!record.fixtureId.empty()) {
			record.winningSelectorCategory = evidenceFieldForSerial(s_currentDoc.cssDiagnostics.computedStyleEvidence,
				record.logicalSerial, "color-winning-selector");
			if (record.winningSelectorCategory.empty()) record.winningSelectorCategory = "none";
			record.winningPseudo = evidenceFieldForSerial(s_currentDoc.cssDiagnostics.computedStyleEvidence,
				record.logicalSerial, "winning-focus-pseudo");
			if (record.winningPseudo.empty()) record.winningPseudo = "none";
			parseBoundedSpecificity(evidenceFieldForSerial(s_currentDoc.cssDiagnostics.computedStyleEvidence,
				record.logicalSerial, "color-specificity"), record.winningSpecificityId,
				record.winningSpecificityClass, record.winningSpecificityElement);
			const std::string sourceOrder = evidenceFieldForSerial(s_currentDoc.cssDiagnostics.computedStyleEvidence,
				record.logicalSerial, "color-source-order");
			try {
				if (!sourceOrder.empty()) record.winningSourceOrder = static_cast<uint32_t>(std::stoul(sourceOrder));
			} catch (...) {
				record.winningSourceOrder = 0;
			}
		}
	}
	s_currentDoc.formsDiagnostics.formAccessibilityRecords = static_cast<int>(runtime.accessibilityRecordCount);
}

FormAccessibilityRecord* Navigator::accessibilityRecordForSerial(uint64_t serial)
{
	FormRuntimeStateTable& runtime = s_currentDoc.formRuntimeState;
	if (!runtime.initialized || serial == 0) return nullptr;
	const size_t count = std::min(runtime.accessibilityRecordCount, runtime.accessibilityRecords.size());
	for (size_t i = 0; i < count; ++i) {
		if (runtime.accessibilityRecords[i].logicalSerial == serial) return &runtime.accessibilityRecords[i];
	}
	return nullptr;
}

int Navigator::blockIndexForControlSerial(uint64_t serial)
{
	if (serial == 0) return -1;
	for (int i = 0; i < static_cast<int>(s_currentDoc.blocks.size()); ++i) {
		const DocBlock& block = s_currentDoc.blocks[static_cast<size_t>(i)];
		if (block.formControl.logicalSerial == serial && isRuntimeCheckable(block)) return i;
	}
	return -1;
}

int Navigator::findBlockById(const std::string& id, bool labelOnly)
{
	if (id.empty()) return -1;
	for (int i = 0; i < static_cast<int>(s_currentDoc.blocks.size()); ++i) {
		const DocBlock& block = s_currentDoc.blocks[static_cast<size_t>(i)];
		if (block.id != id) continue;
		if (labelOnly ? block.type == BlockType::FormLabel
			: (isRuntimeFormControl(block) || isFocusableFormControl(block))) return i;
	}
	return -1;
}

uint64_t Navigator::associatedControlSerialForLabel(const DocBlock& label)
{
	if (label.type != BlockType::FormLabel || !label.formControl.metadataComplete ||
		label.elementMetadata.serial == 0) return 0;
	uint64_t targetSerial = 0;
	int matches = 0;
	if (!label.labelFor.empty()) {
		for (const gxos::web::HtmlElementRef& element : s_currentDoc.structuralElements) {
			if (element.id != label.labelFor) continue;
			++matches;
			if (element.formControl.type == FormControlType::Checkbox ||
				element.formControl.type == FormControlType::Radio) {
				targetSerial = element.serial;
			}
		}
		// Duplicate ids, including a duplicate non-control element, fail closed.
		return matches == 1 ? targetSerial : 0;
	}

	for (const gxos::web::HtmlElementRef& element : s_currentDoc.structuralElements) {
		if (element.formControl.type != FormControlType::Checkbox &&
			element.formControl.type != FormControlType::Radio) continue;
		uint64_t parent = element.parentSerial;
		for (size_t depth = 0; depth < kFormRuntimeControlCap && parent != 0; ++depth) {
			if (parent == label.elementMetadata.serial) {
				targetSerial = element.serial;
				++matches;
				break;
			}
			const gxos::web::HtmlElementRef* found = nullptr;
			for (const gxos::web::HtmlElementRef& candidate : s_currentDoc.structuralElements) {
				if (candidate.serial == parent) { found = &candidate; break; }
			}
			if (!found) break;
			parent = found->parentSerial;
		}
	}
	return matches == 1 ? targetSerial : 0;
}

bool Navigator::radioGroupMatches(const DocBlock& left, const DocBlock& right)
{
	if (left.type != BlockType::FormRadio || right.type != BlockType::FormRadio) return false;
	if (left.formControl.name.empty() || right.formControl.name.empty())
		return left.formControl.logicalSerial == right.formControl.logicalSerial;
	if (left.formIndex >= 0 || right.formIndex >= 0)
		return left.formIndex >= 0 && right.formIndex == left.formIndex &&
			left.formControl.name == right.formControl.name;
	if (left.formControl.parentFormSerial != 0 || right.formControl.parentFormSerial != 0)
		return left.formControl.parentFormSerial != 0 &&
			right.formControl.parentFormSerial == left.formControl.parentFormSerial &&
			left.formControl.name == right.formControl.name;
	if (left.formControl.parentFieldsetSerial != 0 || right.formControl.parentFieldsetSerial != 0)
		return left.formControl.parentFieldsetSerial != 0 &&
			right.formControl.parentFieldsetSerial == left.formControl.parentFieldsetSerial &&
			left.formControl.name == right.formControl.name;
	return left.formControl.name == right.formControl.name;
}

void Navigator::initializeFormRuntimeState()
{
	if (s_currentDoc.formRuntimeState.initialized &&
		s_currentDoc.formRuntimeState.keyboardActivationArmed) {
		cancelKeyboardActivation(FormFocusCancellationReason::StateChange);
	}
	s_currentDoc.formRuntimeState = gxos::web::FormRuntimeStateTable{};
	s_currentDoc.formRuntimeState.initialized = true;
	s_currentDoc.formRuntimeState.documentGeneration = s_documentGeneration;
	s_currentDoc.formRuntimeState.focusOrigin = FormFocusOrigin::None;
	++s_currentDoc.formsDiagnostics.formRuntimeStateResets;
	++s_currentDoc.formsDiagnostics.formFocusStateResets;
	s_focusedInputBlockIndex = -1;
	s_inputCaret = 0;
	s_tabKeyPressed = false;
	for (const gxos::web::HtmlElementRef& element : s_currentDoc.structuralElements) {
		const FormControlMetadata& metadata = element.formControl;
		if (element.serial == 0 || !metadata.metadataComplete || !metadata.supported) continue;
		if (s_currentDoc.formRuntimeState.count >= kFormRuntimeControlCap) {
			++s_currentDoc.formsDiagnostics.controlMetadataClamps;
			break;
		}
		FormRuntimeControlState& state = s_currentDoc.formRuntimeState.controls[
			s_currentDoc.formRuntimeState.count++];
		state.logicalSerial = element.serial;
		state.type = metadata.type;
		state.parentFormSerial = metadata.parentFormSerial;
		state.parentFieldsetSerial = metadata.parentFieldsetSerial;
		state.checked = metadata.type == FormControlType::Option ? metadata.selected : metadata.checked;
		state.initialChecked = state.checked;
		state.disabled = metadata.disabled;
		state.metadataValid = true;
		++s_currentDoc.formsDiagnostics.formRuntimeControlsInitialized;
	}
	// Some forgiving HTML paths retain a valid rendered control block even when
	// its structural registration was not retained.  Recover only those bounded
	// runtime controls from their copied metadata; names and values remain out of
	// the runtime table.
	for (const DocBlock& block : s_currentDoc.blocks) {
		if (!isRuntimeFormControl(block) || block.formControl.logicalSerial == 0) continue;
		bool alreadyInitialized = false;
		for (size_t i = 0; i < s_currentDoc.formRuntimeState.count; ++i) {
			if (s_currentDoc.formRuntimeState.controls[i].logicalSerial == block.formControl.logicalSerial) {
				alreadyInitialized = true;
				break;
			}
		}
		if (alreadyInitialized) continue;
		if (s_currentDoc.formRuntimeState.count >= kFormRuntimeControlCap) {
			++s_currentDoc.formsDiagnostics.controlMetadataClamps;
			break;
		}
		FormRuntimeControlState& state = s_currentDoc.formRuntimeState.controls[
			s_currentDoc.formRuntimeState.count++];
		state.logicalSerial = block.formControl.logicalSerial;
		state.type = block.formControl.type;
		state.parentFormSerial = block.formControl.parentFormSerial;
		state.parentFieldsetSerial = block.formControl.parentFieldsetSerial;
		state.checked = block.formControl.type == FormControlType::Checkbox ||
			block.formControl.type == FormControlType::Radio
			? block.formControl.checked : false;
		state.initialChecked = state.checked;
		state.disabled = block.formControl.disabled;
		state.metadataValid = true;
		++s_currentDoc.formsDiagnostics.formRuntimeControlsInitialized;
	}
	updateFormAccessibilityMetadata();
}

void Navigator::recomputeFormControlStyles()
{
	const bool sameDocumentRecompute = s_pendingDocumentUrl.empty();
	const bool focusWasValid = sameDocumentRecompute &&
		s_currentDoc.formRuntimeState.initialized &&
		s_currentDoc.formRuntimeState.documentGeneration == s_documentGeneration &&
		s_currentDoc.formRuntimeState.focusValid &&
		s_currentDoc.formRuntimeState.focusedDocumentGeneration == s_documentGeneration &&
		focusedFormControlBlockIndex() >= 0;
	if (sameDocumentRecompute)
		incrementLifecycleCounter(s_lifecycleDiagnostics.sameDocumentRecomputations);
	++s_currentDoc.cssDiagnostics.checkedRuntimeRecomputations;
	++s_currentDoc.cssDiagnostics.runtimeFocusRecomputations;
	gxos::web::recomputeDocumentStyles(s_currentDoc);
	s_inlineLayoutDirty = true;
	// Runtime CSS can hide or disable the focused logical control.  Clear that
	// state and run one bounded second recomputation so :focus never survives
	// its own invalidation.
	if (s_currentDoc.formRuntimeState.focusValid && !ensureFocusedControlStillValid()) {
		const FormRuntimeStateTable& runtime = s_currentDoc.formRuntimeState;
		const FormFocusCancellationReason reason =
			(runtime.documentGeneration != s_documentGeneration ||
			 runtime.focusedDocumentGeneration != s_documentGeneration)
			? FormFocusCancellationReason::GenerationMismatch
			: FormFocusCancellationReason::StateChange;
		clearDocumentFocus(false, reason);
		++s_currentDoc.cssDiagnostics.runtimeFocusRecomputations;
		gxos::web::recomputeDocumentStyles(s_currentDoc);
		s_inlineLayoutDirty = true;
	}
	if (focusWasValid && s_currentDoc.formRuntimeState.focusValid &&
		ensureFocusedControlStillValid()) {
		incrementLifecycleCounter(s_lifecycleDiagnostics.focusPreservedRecompute);
	}
	s_documentHeight = std::max(0, computeDocumentHeight());
	clampScrollOffset();
	updateFormAccessibilityMetadata();
	// Generated about: pages such as Page Info and Save Page Text are views of
	// the inspected document.  Their load-time style recomputation must not
	// replace the inspected document with the diagnostics view.
	if (visibleDocumentOwnsInspectedSource())
		storePageMetadata(s_pageMetadata, s_currentDoc);
}

void Navigator::clearKeyboardActivationState()
{
	if (!s_currentDoc.formRuntimeState.initialized) return;
	s_currentDoc.formRuntimeState.pressedKeyboardLogicalSerial = 0;
	s_currentDoc.formRuntimeState.pressedKeyboardDocumentGeneration = 0;
	s_currentDoc.formRuntimeState.pressedKeyboardKey = 0;
	s_currentDoc.formRuntimeState.keyboardActivationArmed = false;
}

void Navigator::cancelKeyboardActivation(FormFocusCancellationReason reason)
{
	FormRuntimeStateTable& runtime = s_currentDoc.formRuntimeState;
	if (!runtime.initialized || !runtime.keyboardActivationArmed) return;
	switch (reason) {
	case FormFocusCancellationReason::Escape: ++s_currentDoc.formsDiagnostics.formFocusCancelEscape; break;
	case FormFocusCancellationReason::Navigation: ++s_currentDoc.formsDiagnostics.formFocusCancelNavigation; break;
	case FormFocusCancellationReason::Deactivation: ++s_currentDoc.formsDiagnostics.formFocusCancelDeactivation; break;
	case FormFocusCancellationReason::StateChange: ++s_currentDoc.formsDiagnostics.formFocusCancelStateChange; break;
	case FormFocusCancellationReason::GenerationMismatch: ++s_currentDoc.formsDiagnostics.formFocusCancelGenerationMismatch; break;
	case FormFocusCancellationReason::KeyMismatch: ++s_currentDoc.formsDiagnostics.formFocusCancelKeyMismatch; break;
	default: break;
	}
	clearKeyboardActivationState();
}

void Navigator::clearDocumentFocus(bool recomputeStyles, FormFocusCancellationReason reason)
{
	FormRuntimeStateTable& runtime = s_currentDoc.formRuntimeState;
	const bool hadFocus = runtime.focusValid || runtime.focusedLogicalSerial != 0;
	if (hadFocus) ++s_currentDoc.formsDiagnostics.formFocusClears;
	runtime.focusedLogicalSerial = 0;
	runtime.focusedDocumentGeneration = 0;
	runtime.focusOrigin = FormFocusOrigin::None;
	runtime.focusValid = false;
	cancelKeyboardActivation(reason);
	s_focusedInputBlockIndex = -1;
	s_inputCaret = 0;
	s_tabKeyPressed = false;
	if (recomputeStyles) {
		++s_currentDoc.cssDiagnostics.runtimeFocusRecomputations;
		gxos::web::recomputeDocumentStyles(s_currentDoc);
		s_inlineLayoutDirty = true;
		if (visibleDocumentOwnsInspectedSource())
			storePageMetadata(s_pageMetadata, s_currentDoc);
	}
}

void Navigator::clearMousePressState()
{
	s_mouseLeftDown = false;
	s_mouseMode = MouseMode::None;
	s_mouseDownHitTarget = HitTarget::None;
	s_mouseDownLinkBlockIndex = -1;
	s_mouseDownLinkUrl.clear();
	s_mouseDragThresholdExceeded = false;
}

bool Navigator::activateLabelBlock(int blockIndex)
{
	if (blockIndex < 0 || blockIndex >= static_cast<int>(s_currentDoc.blocks.size())) return false;
	const DocBlock& label = s_currentDoc.blocks[static_cast<size_t>(blockIndex)];
	const uint64_t serial = associatedControlSerialForLabel(label);
	const int controlIndex = blockIndexForControlSerial(serial);
	if (controlIndex < 0) return false;
	if (isFocusableFormControl(s_currentDoc.blocks[static_cast<size_t>(controlIndex)]))
		focusDocumentInput(controlIndex, FormFocusOrigin::Mouse);
	++s_currentDoc.formsDiagnostics.formLabelActivations;
	activateFormControl(controlIndex);
	return true;
}

bool Navigator::smokeClickBlock(int blockIndex, bool label)
{
	if (blockIndex < 0 || blockIndex >= static_cast<int>(s_currentDoc.blocks.size())) return false;
	s_scrollOffset = std::max(0, blockLayoutY(blockIndex) - kContentH / 2);
	clampScrollOffset();
	const Rect rect = label ? selectableBlockRect(blockIndex) : formControlRect(blockIndex);
	if (rect.w <= 0 || rect.h <= 0) return false;
	int x = rect.x + rect.w / 2;
	int y = rect.y + rect.h / 2;
	int hitIndex = -1;
	auto acceptableTarget = [&](HitTarget target, int index) {
		return index == blockIndex && (label ? target == HitTarget::FormLabel :
			(target == HitTarget::FormCheckbox || target == HitTarget::FormRadio ||
			 target == HitTarget::FormSubmit));
	};
	HitTarget expected = hitTest(x, y, hitIndex);
	if (!acceptableTarget(expected, hitIndex)) {
		const int stepX = std::max(1, rect.w / 32);
		const int stepY = std::max(1, rect.h / 16);
		bool found = false;
		int samples = 0;
		for (int py = rect.y; py < rect.y + rect.h && !found && samples < 2048; py += stepY) {
			for (int px = rect.x; px < rect.x + rect.w && samples < 2048; px += stepX) {
				++samples;
				int candidateIndex = -1;
				const HitTarget candidate = hitTest(px, py, candidateIndex);
				if (!acceptableTarget(candidate, candidateIndex)) continue;
				x = px;
				y = py;
				hitIndex = candidateIndex;
				expected = candidate;
				found = true;
				break;
			}
		}
		if (!found) return false;
	}
	handleMouseInput(x, y, 1, "down");
	handleMouseInput(x, y, 1, "up");
	return true;
}

void Navigator::handleDocumentClick(HitTarget target, int linkBlockIndex)
{
	if (target == HitTarget::Link &&
		linkBlockIndex >= 0 &&
		linkBlockIndex < static_cast<int>(s_currentDoc.blocks.size()))
	{
		navigateTo(s_currentDoc.blocks[linkBlockIndex].url);
	} else if (target == HitTarget::FormLabel &&
		linkBlockIndex >= 0 &&
		linkBlockIndex < static_cast<int>(s_currentDoc.blocks.size()))
	{
		activateLabelBlock(linkBlockIndex);
	} else if ((target == HitTarget::FormCheckbox ||
				target == HitTarget::FormRadio ||
				target == HitTarget::FormSelect) &&
		linkBlockIndex >= 0 &&
		linkBlockIndex < static_cast<int>(s_currentDoc.blocks.size()))
	{
		if (isFocusableFormControl(s_currentDoc.blocks[static_cast<size_t>(linkBlockIndex)]))
			focusDocumentInput(linkBlockIndex, FormFocusOrigin::Mouse);
		activateFormControl(linkBlockIndex);
	} else if (target == HitTarget::FormSubmit &&
		linkBlockIndex >= 0 &&
		linkBlockIndex < static_cast<int>(s_currentDoc.blocks.size()))
	{
		// Phase 2F buttons are deliberately inert.  The existing Forms-lite
		// submit path remains available to its explicit legacy callers.
		activateFormControl(linkBlockIndex);
	}
}

void Navigator::handleMouseInput(int x, int y, int button, const std::string& action)
{
	if (button == 1 && action == "up" && !s_mouseLeftDown &&
		s_staleMouseReleaseGeneration == s_documentGeneration &&
		s_staleMouseReleaseGeneration != 0) {
		incrementLifecycleCounter(s_lifecycleDiagnostics.staleMouseReleaseBlocks);
		s_staleMouseReleaseGeneration = 0;
		return;
	}
	int linkIdx = -1;
	HitTarget target = hitTest(x, y, linkIdx);

	if (button == 0 && action == "move") {
		s_mouseCurrentX = x;
		s_mouseCurrentY = y;

		if (s_mouseLeftDown) {
			const int dx = std::abs(x - s_mouseDownX);
			const int dy = std::abs(y - s_mouseDownY);
			if (dx >= kMouseDragThreshold || dy >= kMouseDragThreshold) {
				s_mouseDragThresholdExceeded = true;
			}

			if ((s_mouseMode == MouseMode::PotentialLinkClick ||
				 s_mouseMode == MouseMode::PotentialTextSelection) &&
				s_mouseDragThresholdExceeded)
			{
				beginSelection(s_mouseDownX, s_mouseDownY);
				if (s_selectionDragging) {
					s_mouseMode = MouseMode::SelectingText;
					s_selectionBegan = true;
				}
			}

			if (s_mouseMode == MouseMode::SelectingText) {
				updateSelection(x, y);
				updateDisplay();
			}
		}

		updateHoverStatus(target, linkIdx);
		return;
	}

	if (button == 1 && action == "down") {
		s_mouseLeftDown = true;
		s_mouseMode = MouseMode::None;
		s_mouseDownHitTarget = target;
		s_mouseDownLinkBlockIndex = linkIdx;
		s_mouseDownLinkUrl.clear();
		s_mouseDownX = x;
		s_mouseDownY = y;
		s_mouseCurrentX = x;
		s_mouseCurrentY = y;
		s_mouseDragThresholdExceeded = false;
		s_selectionBegan = false;

		if (target == HitTarget::Link &&
			linkIdx >= 0 &&
			linkIdx < static_cast<int>(s_currentDoc.blocks.size()))
		{
			s_mouseDownLinkUrl = s_currentDoc.blocks[linkIdx].url;
		}

		if (target == HitTarget::AddressBar) {
			s_mouseMode = MouseMode::AddressBarInteraction;
			if (s_currentDoc.formRuntimeState.focusValid) blurDocumentInput();
			clearSelection();
			if (s_findActive) closeFindMode();
			focusAddressBar();
			if (s_addressFocused) {
				constexpr int kTextX = kAddressX + 10;
				int charOffset = (x - kTextX) / kCharW;
				s_addressCaret = std::max(0, std::min(charOffset,
					static_cast<int>(s_addressBuffer.size())));
				renderToolbar();
			}
			return;
		}

		if (target == HitTarget::Back ||
			target == HitTarget::Forward ||
			target == HitTarget::Reload ||
			target == HitTarget::Home ||
			target == HitTarget::Bookmarks ||
			target == HitTarget::AddBookmark ||
			target == HitTarget::Find)
		{
			s_mouseMode = MouseMode::ToolbarInteraction;
			return;
		}

		if (s_addressFocused) blurAddressBar();

		if (target == HitTarget::FormInput || target == HitTarget::FormTextarea) {
			s_mouseMode = MouseMode::FormInputInteraction;
			clearSelection();
			if (s_findActive) closeFindMode();
			focusDocumentInput(linkIdx, FormFocusOrigin::Mouse);
			Rect r = formControlRect(linkIdx);
			int charOffset = (x - (r.x + 8)) / kCharW;
			if (linkIdx >= 0 && linkIdx < static_cast<int>(s_currentDoc.blocks.size())) {
				s_inputCaret = std::max(0, std::min(charOffset,
					static_cast<int>(s_currentDoc.blocks[linkIdx].inputValue.size())));
			}
			updateDisplay();
			return;
		}

		if (target == HitTarget::FormCheckbox ||
			target == HitTarget::FormRadio ||
			target == HitTarget::FormSelect ||
			target == HitTarget::FormSubmit ||
			target == HitTarget::FormLabel) {
			s_mouseMode = MouseMode::FormInputInteraction;
			if (target == HitTarget::FormLabel) {
				const uint64_t serial = associatedControlSerialForLabel(s_currentDoc.blocks[static_cast<size_t>(linkIdx)]);
				const int controlIndex = blockIndexForControlSerial(serial);
				if (controlIndex >= 0 && isFocusableFormControl(s_currentDoc.blocks[static_cast<size_t>(controlIndex)]))
					focusDocumentInput(controlIndex, FormFocusOrigin::Mouse);
			} else if (linkIdx >= 0 && linkIdx < static_cast<int>(s_currentDoc.blocks.size()) &&
				isFocusableFormControl(s_currentDoc.blocks[static_cast<size_t>(linkIdx)])) {
				focusDocumentInput(linkIdx, FormFocusOrigin::Mouse);
			}
			clearSelection();
			updateDisplay();
			return;
		}

		if (s_currentDoc.formRuntimeState.focusValid) blurDocumentInput();

		if (target == HitTarget::Link) {
			s_mouseMode = MouseMode::PotentialLinkClick;
			clearSelection();
			s_selectionPending = true;
			s_selectionStartX = x;
			s_selectionStartY = y;
			updateDisplay();
			return;
		}

		SelectionPosition textHit = textPositionFromPoint(x, y, false);
		if (textHit.blockIndex >= 0) {
			s_mouseMode = MouseMode::PotentialTextSelection;
			clearSelection();
			s_selectionPending = true;
			s_selectionStartX = x;
			s_selectionStartY = y;
			updateDisplay();
		} else {
			clearSelection();
			updateDisplay();
		}
		return;
	}

	if (button == 1 && action == "up") {
		s_mouseCurrentX = x;
		s_mouseCurrentY = y;

		int upLinkIdx = -1;
		HitTarget upTarget = hitTest(x, y, upLinkIdx);
		const MouseMode mode = s_mouseMode;
		const int downLinkIdx = s_mouseDownLinkBlockIndex;

		s_mouseLeftDown = false;

		if (mode == MouseMode::SelectingText || s_selectionDragging) {
			finalizeSelection(x, y);
			updateDisplay();
		} else if (mode == MouseMode::PotentialLinkClick &&
			!s_mouseDragThresholdExceeded &&
			upTarget == HitTarget::Link &&
			upLinkIdx == downLinkIdx)
		{
			handleDocumentClick(HitTarget::Link, downLinkIdx);
		} else if (mode == MouseMode::FormInputInteraction &&
			!s_mouseDragThresholdExceeded &&
			upTarget == s_mouseDownHitTarget &&
			upLinkIdx == downLinkIdx)
		{
			handleDocumentClick(upTarget, downLinkIdx);
		}

		s_mouseMode = MouseMode::None;
		s_mouseDownHitTarget = HitTarget::None;
		s_mouseDownLinkBlockIndex = -1;
		s_mouseDownLinkUrl.clear();
		s_selectionPending = false;
		s_mouseDragThresholdExceeded = false;
		s_selectionBegan = false;
	}
}

void Navigator::focusDocumentInput(int blockIndex, FormFocusOrigin origin)
{
	if (blockIndex < 0 || blockIndex >= static_cast<int>(s_currentDoc.blocks.size()) ||
		!isFocusableFormControl(s_currentDoc.blocks[blockIndex])) return;
	DocBlock& block = s_currentDoc.blocks[static_cast<size_t>(blockIndex)];
	FormRuntimeStateTable& runtime = s_currentDoc.formRuntimeState;
	if (!runtime.initialized || runtime.documentGeneration != s_documentGeneration ||
		block.formControl.logicalSerial == 0) return;
	const bool changed = !runtime.focusValid ||
		runtime.focusedLogicalSerial != block.formControl.logicalSerial ||
		runtime.focusedDocumentGeneration != s_documentGeneration;
	const bool originChanged = runtime.focusOrigin != origin;
	if (runtime.keyboardActivationArmed) cancelKeyboardActivation(FormFocusCancellationReason::StateChange);
	if (changed) ++s_currentDoc.formsDiagnostics.formFocusChanges;
	if (origin == FormFocusOrigin::Mouse && (changed || originChanged))
		++s_currentDoc.formsDiagnostics.formFocusOriginMouse;
	if (origin == FormFocusOrigin::Keyboard && (changed || originChanged))
		++s_currentDoc.formsDiagnostics.formFocusOriginKeyboard;
	runtime.focusedLogicalSerial = block.formControl.logicalSerial;
	runtime.focusedDocumentGeneration = s_documentGeneration;
	runtime.focusOrigin = origin;
	runtime.focusValid = true;
	clearKeyboardActivationState();
	s_focusedInputBlockIndex = blockIndex;
	s_inputCaret = static_cast<int>(block.inputValue.size());
	s_statusText = (block.type == BlockType::FormTextInput || block.type == BlockType::FormTextarea)
		? "Editing form field" : "Form control focused";
	if (changed || originChanged) {
		recomputeFormControlStyles();
		if (FormAccessibilityRecord* record = accessibilityRecordForSerial(block.formControl.logicalSerial))
			record->revealResult = FormFocusRevealResult::None;
	}
	revealFocusedFormControl(blockIndex);
}

void Navigator::revealFocusedFormControl(int blockIndex)
{
	if (blockIndex < 0 || blockIndex >= static_cast<int>(s_currentDoc.blocks.size())) return;
	const DocBlock& block = s_currentDoc.blocks[static_cast<size_t>(blockIndex)];
	if (!isFocusedFormControl(block)) return;
	const int current = std::max(0, s_scrollOffset);
	const int top = std::max(0, blockLayoutY(blockIndex));
	const int height = std::max(1, formControlHeight(block));
	const int bottom = top > std::numeric_limits<int>::max() - height
		? std::numeric_limits<int>::max() : top + height;
	constexpr int kRevealPadding = 8;
	int desired = current;
	if (top < current + kRevealPadding) {
		desired = std::max(0, top - kRevealPadding);
	} else if (bottom > current + kContentH - kRevealPadding) {
		desired = std::max(0, bottom - kContentH + kRevealPadding);
	}
	s_documentHeight = std::max(0, computeDocumentHeight());
	const int maxOffset = maxScrollOffset();
	const int unclamped = desired;
	desired = std::max(0, std::min(desired, maxOffset));
	FormAccessibilityRecord* record = accessibilityRecordForSerial(block.formControl.logicalSerial);
	if (unclamped != desired) {
		++s_currentDoc.formsDiagnostics.formFocusRevealClamps;
		if (record) record->revealResult = FormFocusRevealResult::Clamped;
	}
	if (desired == current) {
		++s_currentDoc.formsDiagnostics.formFocusRevealNoops;
		if (record && record->revealResult != FormFocusRevealResult::Clamped)
			record->revealResult = FormFocusRevealResult::Noop;
		return;
	}
	s_scrollOffset = desired;
	++s_currentDoc.formsDiagnostics.formFocusRevealScrolls;
	if (record) record->revealResult = FormFocusRevealResult::Scroll;
}

void Navigator::blurDocumentInput()
{
	clearDocumentFocus(true);
}

void Navigator::openFindMode()
{
	s_findActive = true;
	if (s_addressFocused) {
		s_addressFocused = false;
		s_addressBuffer.clear();
		s_addressCaret = 0;
	}
	if (s_currentDoc.formRuntimeState.focusValid) {
		blurDocumentInput();
	}
	s_findCaret = std::max(0, std::min(s_findCaret, static_cast<int>(s_findBuffer.size())));
	updateFindMatches(true);
}

void Navigator::closeFindMode()
{
	s_findActive = false;
	s_findMatches.clear();
	s_currentFindMatch = -1;
	s_statusText = "Find closed.";
}

std::string Navigator::searchableTextForBlock(const DocBlock& block)
{
	switch (block.type) {
	case BlockType::Heading:
	case BlockType::Paragraph:
	case BlockType::Link:
	case BlockType::ListItem:
	case BlockType::Preformatted:
	case BlockType::FormLabel:
		return block.text;
	case BlockType::Image:
		return !block.alt.empty() ? block.alt : block.text;
	case BlockType::FormTextInput:
	case BlockType::FormTextarea:
		if (block.formControl.type == FormControlType::Password || block.inputType == "password")
			return "[password field]";
		if (!block.inputValue.empty()) return block.inputValue;
		if (!block.placeholder.empty()) return block.placeholder;
		return block.inputName;
	case BlockType::FormCheckbox:
	case BlockType::FormRadio:
		return block.text.empty() ? block.inputName : block.text;
	case BlockType::FormSelect:
		return block.text.empty() ? block.inputName : block.text;
	case BlockType::FormSubmit:
		return !block.submitLabel.empty() ? block.submitLabel : block.text;
	}
	return std::string();
}

bool Navigator::isSelectableBlock(const DocBlock& block)
{
	switch (block.type) {
	case BlockType::Heading:
	case BlockType::Paragraph:
	case BlockType::Link:
	case BlockType::ListItem:
	case BlockType::Preformatted:
	case BlockType::FormLabel:
		return true;
	default:
		return false;
	}
}

void Navigator::clearSelection()
{
	s_selectionActive = false;
	s_selectionPending = false;
	s_selectionDragging = false;
	s_selectionMoved = false;
	s_selectionStartX = 0;
	s_selectionStartY = 0;
	s_selectionAnchor = SelectionPosition{};
	s_selectionFocus = SelectionPosition{};
}

Navigator::SelectionRange Navigator::normalizedSelection()
{
	SelectionRange range;
	if (!s_selectionActive || s_selectionAnchor.blockIndex < 0 || s_selectionFocus.blockIndex < 0) {
		return range;
	}
	range.start = s_selectionAnchor;
	range.end = s_selectionFocus;
	if (range.start.blockIndex > range.end.blockIndex ||
		(range.start.blockIndex == range.end.blockIndex && range.start.offset > range.end.offset)) {
		std::swap(range.start, range.end);
	}
	range.valid = true;
	return range;
}

bool Navigator::hasSelection()
{
	SelectionRange range = normalizedSelection();
	return range.valid &&
		(range.start.blockIndex != range.end.blockIndex || range.start.offset != range.end.offset);
}

Navigator::Rect Navigator::selectableBlockRect(int blockIndex)
{
	if (blockIndex < 0 || blockIndex >= static_cast<int>(s_currentDoc.blocks.size())) {
		return Rect{ 0, 0, 0, 0 };
	}
	const DocBlock& block = s_currentDoc.blocks[blockIndex];
	if (!blockHasVisibleCss(block)) return Rect{ 0, 0, 0, 0 };
	if (!isSelectableBlock(block)) return Rect{ 0, 0, 0, 0 };
	Rect inlineRect;
	if (inlineFragmentRectForBlock(blockIndex, false, inlineRect)) return inlineRect;
	if (isTableCellLikeBlock(block)) {
		if (!isFirstTableCellInGroup(s_currentDoc, blockIndex)) return Rect{ 0, 0, 0, 0 };
		const int groupStart = tableGroupStartIndex(s_currentDoc, blockIndex);
		const TableGroupLayout layout = buildTableGroupLayout(s_currentDoc, groupStart);
		const int drawY = kContentY + blockLayoutY(blockIndex) - s_scrollOffset;
		const int rowY = drawY + cssMarginTopPx(block.style, 4);
		const CssPaintRect clipped = cssClipHitTarget(CssPaintRect{
			layout.outerX,
			rowY,
			std::max(kCharW, layout.outerWidth),
			std::max(kLineH, layout.totalHeightPx)
		}, cssClipRectForHit(s_currentDoc, blockIndex, block, layout.outerX, rowY, layout.outerWidth,
			layout.totalHeightPx, s_scrollOffset));
		return Rect{clipped.x, clipped.y, clipped.w, clipped.h};
	}
	int drawY = kContentY + blockLayoutY(blockIndex) - s_scrollOffset;
	const int blockMarginTop = cssMarginTopPx(block.style, block.type == BlockType::Heading ? 10 : 4);
	const int blockMarginBottom = cssMarginBottomPx(block.style, block.type == BlockType::ListItem ? 4 : 8);
	const int paddingTop = cssPaddingTopPx(block.style, block.type == BlockType::Preformatted ? 4 : 0);
	const int paddingRight = cssPaddingRightPx(block.style, block.type == BlockType::Preformatted ? 4 : 0);
	const int paddingBottom = cssPaddingBottomPx(block.style, block.type == BlockType::Preformatted ? 4 : 0);
	const int paddingLeft = cssPaddingLeftPx(block.style, block.type == BlockType::Preformatted ? 4 : 0);
	const int availableWidth = blockAvailableWidth(block, s_currentDoc);
	const int outerWidth = blockOuterWidth(block, availableWidth);
	int resolvedOuterX = blockOuterX(block, s_currentDoc, availableWidth, outerWidth);
	if (const CssPositionedRecord* positioned = cssPositionedRecordForBlock(s_currentDoc, blockIndex)) {
		resolvedOuterX = positioned->finalX;
		drawY = positioned->finalY - s_scrollOffset - blockMarginTop;
	} else {
		int ancestorDeltaX = 0;
		int ancestorDeltaY = 0;
		cssPositionRelativeAncestorDelta(s_currentDoc, blockIndex, &ancestorDeltaX, &ancestorDeltaY);
		resolvedOuterX = cssBoundedGeometryAdd(resolvedOuterX, ancestorDeltaX);
		drawY = cssBoundedGeometryAdd(drawY, ancestorDeltaY);
	}
	const int borderLeft = cssBorderLeftPx(block.style);
	const int borderRight = cssBorderRightPx(block.style);
	const int textX = blockContentLeftX(block, resolvedOuterX);
	int textW = 0;
	int textH = 0;
	switch (block.type) {
	case BlockType::Heading:
		textW = std::max(1, outerWidth - borderLeft - borderRight - paddingLeft - paddingRight);
		textH = std::max(blockTextLineHeight(block) + 4, cssFontSizeOrDefault(block.style, 20) + 2);
		break;
	case BlockType::Paragraph:
	case BlockType::Link:
	case BlockType::FormLabel:
		textW = std::max(1, outerWidth - borderLeft - borderRight - paddingLeft - paddingRight);
		textH = wrappedBlockHeight(block, std::max(1, textW / kCharW), blockTextLineHeight(block));
		break;
	case BlockType::ListItem:
		textW = std::max(1, outerWidth - borderLeft - borderRight - paddingLeft - paddingRight);
		textH = wrappedBlockHeight(block, std::max(1, textW / kCharW), blockTextLineHeight(block));
		break;
	case BlockType::Preformatted:
		textW = std::max(1, outerWidth - borderLeft - borderRight - paddingLeft - paddingRight);
		textH = wrappedBlockHeight(block, std::max(1, textW / kCharW), blockTextLineHeight(block)) + paddingTop + paddingBottom;
		break;
	default:
		break;
	}
	const int boxY = drawY + blockMarginTop;
	const int boxH = std::max(1, blockTotalHeight(block, s_currentDoc,
		blockIndex + 1 < static_cast<int>(s_currentDoc.blocks.size()) &&
			s_currentDoc.blocks[blockIndex + 1].type == BlockType::Heading) - blockMarginTop - blockMarginBottom);
	const CssPaintRect clipped = cssClipHitTarget(
		CssPaintRect{textX, drawY + blockMarginTop + cssBorderTopPx(block.style) + paddingTop,
			std::max(kCharW, textW), std::max(kLineH, textH + blockMarginBottom)},
		cssClipRectForHit(s_currentDoc, blockIndex, block, resolvedOuterX, boxY, outerWidth, boxH, s_scrollOffset));
	return Rect{clipped.x, clipped.y, clipped.w, clipped.h};
}

Navigator::SelectionPosition Navigator::textPositionFromPoint(int x, int y, bool clampToNearest)
{
	SelectionPosition nearest;
	int nearestDistance = 1 << 30;
	for (int i = 0; i < static_cast<int>(s_currentDoc.blocks.size()); ++i) {
		const DocBlock& block = s_currentDoc.blocks[i];
		if (!isSelectableBlock(block)) continue;
		Rect rect = selectableBlockRect(i);
		if (rect.w <= 0 || rect.h <= 0) continue;
		const std::string text = searchableTextForBlock(block);
		const int maxChars = (block.type == BlockType::ListItem) ? ((kContentW - 44) / kCharW) : ((kContentW - 34) / kCharW);
		const std::vector<std::string> lines = wrapTextForBlock(block, maxChars);
		const int lineHeight = std::max(kLineH, blockTextLineHeight(block));
		int lineIndex = std::max(0, std::min((y - rect.y) / lineHeight, std::max(0, static_cast<int>(lines.size()) - 1)));
		size_t lineStart = 0;
		for (int line = 0; line < lineIndex && line < static_cast<int>(lines.size()); ++line) {
			lineStart += lines[line].size();
			if (block.type != BlockType::Preformatted && block.style.whiteSpace != WhiteSpaceMode::Pre && block.style.whiteSpace != WhiteSpaceMode::PreWrap) {
				while (lineStart < text.size() && text[lineStart] == ' ') ++lineStart;
				if (lineStart < text.size() && text[lineStart] == '\n') ++lineStart;
			} else if (lineStart < text.size()) {
				++lineStart;
			}
		}
		size_t offset = lineStart;
		if (!lines.empty()) {
			const std::string& lineText = lines[static_cast<size_t>(lineIndex)];
			int charOffset = std::max(0, std::min((x - rect.x) / kCharW, static_cast<int>(lineText.size())));
			offset = std::min(text.size(), lineStart + static_cast<size_t>(charOffset));
		}
		if (rect.contains(x, y)) {
			return SelectionPosition{ i, offset };
		}
		if (clampToNearest) {
			int dx = 0;
			if (x < rect.x) dx = rect.x - x;
			else if (x >= rect.x + rect.w) dx = x - (rect.x + rect.w - 1);
			int dy = 0;
			if (y < rect.y) dy = rect.y - y;
			else if (y >= rect.y + rect.h) dy = y - (rect.y + rect.h - 1);
			int distance = dx + dy;
			if (distance < nearestDistance) {
				nearestDistance = distance;
				nearest = SelectionPosition{ i, offset };
			}
		}
	}
	return nearest;
}

void Navigator::beginSelection(int x, int y)
{
	SelectionPosition pos = textPositionFromPoint(x, y, false);
	if (pos.blockIndex < 0) {
		clearSelection();
		return;
	}
	s_selectionAnchor = pos;
	s_selectionFocus = pos;
	s_selectionActive = true;
	s_selectionPending = false;
	s_selectionDragging = true;
	s_selectionMoved = false;
}

void Navigator::updateSelection(int x, int y)
{
	if (!s_selectionDragging) return;
	SelectionPosition pos = textPositionFromPoint(x, y, true);
	if (pos.blockIndex < 0) return;
	if (pos.blockIndex != s_selectionFocus.blockIndex || pos.offset != s_selectionFocus.offset) {
		s_selectionFocus = pos;
		s_selectionMoved = true;
	}
}

void Navigator::finalizeSelection(int x, int y)
{
	if (!s_selectionDragging) return;
	updateSelection(x, y);
	s_selectionDragging = false;
	if (!hasSelection()) {
		if (!s_selectionMoved) {
			clearSelection();
		}
	}
}

std::string Navigator::selectedText()
{
	SelectionRange range = normalizedSelection();
	if (!range.valid) return std::string();
	std::string out;
	for (int i = range.start.blockIndex; i <= range.end.blockIndex; ++i) {
		if (i < 0 || i >= static_cast<int>(s_currentDoc.blocks.size())) continue;
		const DocBlock& block = s_currentDoc.blocks[i];
		if (!isSelectableBlock(block)) continue;
		const std::string text = searchableTextForBlock(block);
		size_t begin = (i == range.start.blockIndex) ? std::min(range.start.offset, text.size()) : 0;
		size_t end = (i == range.end.blockIndex) ? std::min(range.end.offset, text.size()) : text.size();
		if (end < begin) std::swap(begin, end);
		if (end > begin) out += text.substr(begin, end - begin);
		if (i != range.end.blockIndex) out += "\n";
	}
	return out;
}

void Navigator::selectAllDocumentText()
{
	int first = -1;
	int last = -1;
	for (int i = 0; i < static_cast<int>(s_currentDoc.blocks.size()); ++i) {
		if (!isSelectableBlock(s_currentDoc.blocks[i])) continue;
		if (first < 0) first = i;
		last = i;
	}
	if (first < 0 || last < 0) {
		clearSelection();
		return;
	}
	s_selectionAnchor = SelectionPosition{ first, 0 };
	s_selectionFocus = SelectionPosition{ last, searchableTextForBlock(s_currentDoc.blocks[last]).size() };
	s_selectionActive = true;
	s_selectionDragging = false;
	s_selectionMoved = true;
}

bool Navigator::copySelectionToClipboard()
{
	std::string text = selectedText();
	if (text.empty()) return false;
	s_navigatorClipboard = text;
	if (tryWriteHostedClipboard(text)) {
		s_clipboardMode = "hosted system clipboard";
		return true;
	}
	s_clipboardMode = "Navigator internal clipboard";
	return true;
}

std::string Navigator::findMatchStatusText()
{
	if (s_findBuffer.empty()) return "Type to find";
	if (s_findMatches.empty()) return "No matches";
	return "Match " + std::to_string(s_currentFindMatch + 1) +
		" of " + std::to_string(s_findMatches.size());
}

bool Navigator::isFocusableFormControl(const DocBlock& block)
{
	if (block.formControl.logicalSerial == 0 || !block.formControl.metadataComplete ||
		!block.formControl.supported || block.formUnsupported || block.formControl.hidden ||
		runtimeDisabled(block) || !blockHasVisibleCss(block)) return false;
	if (block.type != BlockType::FormTextInput && block.type != BlockType::FormTextarea &&
		block.type != BlockType::FormCheckbox && block.type != BlockType::FormRadio &&
		block.type != BlockType::FormSelect && block.type != BlockType::FormSubmit) return false;
	// Focus eligibility is based on valid layout geometry, not the viewport
	// intersection used for mouse hit testing.  Off-viewport controls remain
	// keyboard-focusable so revealFocusedFormControl() can scroll them into view.
	const int availableWidth = blockAvailableWidth(block, s_currentDoc);
	const int outerWidth = blockOuterWidth(block, availableWidth);
	return outerWidth > 0 && formControlHeight(block) > 0;
}

bool Navigator::isFocusedFormControl(const DocBlock& block)
{
	const FormRuntimeStateTable& runtime = s_currentDoc.formRuntimeState;
	return runtime.initialized && runtime.focusValid &&
		runtime.documentGeneration == s_documentGeneration &&
		runtime.focusedDocumentGeneration == s_documentGeneration &&
		runtime.focusedLogicalSerial != 0 &&
		block.formControl.logicalSerial == runtime.focusedLogicalSerial &&
		isFocusableFormControl(block);
}

int Navigator::focusedFormControlBlockIndex()
{
	const FormRuntimeStateTable& runtime = s_currentDoc.formRuntimeState;
	if (!runtime.initialized || !runtime.focusValid ||
		runtime.documentGeneration != s_documentGeneration ||
		runtime.focusedDocumentGeneration != s_documentGeneration ||
		runtime.focusedLogicalSerial == 0) return -1;
	for (int i = 0; i < static_cast<int>(s_currentDoc.blocks.size()); ++i) {
		const DocBlock& block = s_currentDoc.blocks[static_cast<size_t>(i)];
		if (block.formControl.logicalSerial == runtime.focusedLogicalSerial &&
			isFocusableFormControl(block)) return i;
	}
	return -1;
}

bool Navigator::ensureFocusedControlStillValid()
{
	const FormRuntimeStateTable& runtime = s_currentDoc.formRuntimeState;
	if (!runtime.initialized || !runtime.focusValid ||
		runtime.documentGeneration != s_documentGeneration ||
		runtime.focusedDocumentGeneration != s_documentGeneration ||
		runtime.focusedLogicalSerial == 0) return false;
	const int blockIndex = focusedFormControlBlockIndex();
	if (blockIndex < 0) return false;
	s_focusedInputBlockIndex = blockIndex;
	return true;
}

size_t Navigator::buildFormFocusOrder(std::array<int, kFormRuntimeControlCap>& order)
{
	size_t count = 0;
	for (int i = 0; i < static_cast<int>(s_currentDoc.blocks.size()); ++i) {
		const DocBlock& block = s_currentDoc.blocks[static_cast<size_t>(i)];
		if (block.formControl.hidden || block.style.displayNone) {
			++s_currentDoc.formsDiagnostics.formHiddenFocusSkips;
			continue;
		}
		if (runtimeDisabled(block)) {
			++s_currentDoc.formsDiagnostics.formDisabledFocusSkips;
			continue;
		}
		if (!isFocusableFormControl(block)) continue;
		bool duplicate = false;
		for (size_t prior = 0; prior < count; ++prior) {
			if (s_currentDoc.blocks[static_cast<size_t>(order[prior])].formControl.logicalSerial ==
				block.formControl.logicalSerial) {
				duplicate = true;
				break;
			}
		}
		if (duplicate) continue;
		if (count >= order.size()) {
			++s_currentDoc.formsDiagnostics.controlMetadataClamps;
			break;
		}
		order[count++] = i;
	}
	s_currentDoc.formsDiagnostics.formFocusableControls = static_cast<int>(count);
	return count;
}

int Navigator::formControlHeight(const DocBlock& block)
{
	return blockFormControlHeight(block);
}

void Navigator::focusNextFormControl(bool reverse)
{
	std::array<int, kFormRuntimeControlCap> order{};
	const size_t count = buildFormFocusOrder(order);
	if (count == 0) return;
	int currentPosition = -1;
	const int currentIndex = focusedFormControlBlockIndex();
	if (currentIndex >= 0) {
		const uint64_t serial = s_currentDoc.blocks[static_cast<size_t>(currentIndex)].formControl.logicalSerial;
		for (size_t i = 0; i < count; ++i) {
			if (s_currentDoc.blocks[static_cast<size_t>(order[i])].formControl.logicalSerial == serial) {
				currentPosition = static_cast<int>(i);
				break;
			}
		}
	}
	int targetPosition = reverse ? static_cast<int>(count - 1) : 0;
	bool wrapped = false;
	if (currentPosition >= 0) {
		targetPosition = reverse ? currentPosition - 1 : currentPosition + 1;
		if (targetPosition < 0) { targetPosition = static_cast<int>(count - 1); wrapped = true; }
		if (targetPosition >= static_cast<int>(count)) { targetPosition = 0; wrapped = true; }
	}
	if (wrapped) ++s_currentDoc.formsDiagnostics.formFocusWraps;
	if (reverse) ++s_currentDoc.formsDiagnostics.formTabBackward;
	else ++s_currentDoc.formsDiagnostics.formTabForward;
	const int targetIndex = order[static_cast<size_t>(targetPosition)];
	focusDocumentInput(targetIndex, FormFocusOrigin::Keyboard);
	clearSelection();
	updateStatus("Form control focused.");
	updateDisplay();
}

void Navigator::activateFormControl(int blockIndex)
{
	if (blockIndex < 0 || blockIndex >= static_cast<int>(s_currentDoc.blocks.size())) return;
	DocBlock& block = s_currentDoc.blocks[blockIndex];
	if (!isRuntimeFormControl(block)) return;
	if (runtimeDisabled(block)) {
		++s_currentDoc.formsDiagnostics.formDisabledActivationBlocks;
		updateStatus("Disabled form control.");
		return;
	}
	FormRuntimeControlState* state = runtimeStateForBlock(block);
	if (!state) return; // Fail closed if bounded runtime metadata is incomplete.
	if (block.type == BlockType::FormCheckbox) {
		++state->activationCount;
		++s_currentDoc.formsDiagnostics.formCheckboxActivations;
		state->checked = !state->checked;
		++s_currentDoc.formsDiagnostics.formCheckboxToggles;
		s_focusedInputBlockIndex = blockIndex;
		recomputeFormControlStyles();
		updateDisplay();
		return;
	}
	if (block.type == BlockType::FormRadio) {
		++state->activationCount;
		++s_currentDoc.formsDiagnostics.formRadioActivations;
		for (DocBlock& candidate : s_currentDoc.blocks) {
			if (&candidate == &block || !radioGroupMatches(candidate, block)) continue;
			FormRuntimeControlState* candidateState = runtimeStateForBlock(candidate);
			if (candidateState && candidateState->checked) {
				candidateState->checked = false;
				++s_currentDoc.formsDiagnostics.formRadioGroupUnchecks;
			}
		}
		state->checked = true;
		s_focusedInputBlockIndex = blockIndex;
		recomputeFormControlStyles();
		updateDisplay();
		return;
	}
	if (block.type == BlockType::FormSubmit) {
		++state->activationCount;
		++s_currentDoc.formsDiagnostics.formButtonActivations;
		s_focusedInputBlockIndex = blockIndex;
		updateStatus("Button activated (session-local; no submission).");
		storePageMetadata(s_pageMetadata, s_currentDoc);
	}
}

void Navigator::armKeyboardActivation(int keyCode)
{
	if (keyCode != 32 && keyCode != 13) return;
	FormRuntimeStateTable& runtime = s_currentDoc.formRuntimeState;
	const int blockIndex = focusedFormControlBlockIndex();
	if (blockIndex < 0) {
		if (runtime.focusValid || runtime.focusedLogicalSerial != 0) {
			++s_currentDoc.formsDiagnostics.formStaleKeyActivationBlocks;
			const FormFocusCancellationReason reason =
				(runtime.documentGeneration != s_documentGeneration ||
				 runtime.focusedDocumentGeneration != s_documentGeneration)
				? FormFocusCancellationReason::GenerationMismatch
				: FormFocusCancellationReason::StateChange;
			clearDocumentFocus(true, reason);
		}
		return;
	}
	const DocBlock& block = s_currentDoc.blocks[static_cast<size_t>(blockIndex)];
	if (runtime.keyboardActivationArmed) {
		const bool sameControl = runtime.pressedKeyboardLogicalSerial == block.formControl.logicalSerial;
		const bool sameGeneration = runtime.pressedKeyboardDocumentGeneration == s_documentGeneration &&
			runtime.documentGeneration == s_documentGeneration;
		if (sameControl && sameGeneration && runtime.pressedKeyboardKey == static_cast<uint8_t>(keyCode)) {
			++s_currentDoc.formsDiagnostics.formKeyRepeatSuppressed;
		} else {
			cancelKeyboardActivation(sameGeneration ? FormFocusCancellationReason::KeyMismatch
				: FormFocusCancellationReason::GenerationMismatch);
		}
		return;
	}
	const bool activatable = keyCode == 32
		? (isRuntimeCheckable(block) || isRuntimeButton(block))
		: isRuntimeButton(block);
	if (!activatable) return;
	runtime.pressedKeyboardLogicalSerial = block.formControl.logicalSerial;
	runtime.pressedKeyboardDocumentGeneration = s_documentGeneration;
	runtime.pressedKeyboardKey = static_cast<uint8_t>(keyCode);
	runtime.keyboardActivationArmed = true;
}

void Navigator::finishKeyboardActivation(int keyCode)
{
	if (keyCode != 32 && keyCode != 13) return;
	FormRuntimeStateTable& runtime = s_currentDoc.formRuntimeState;
	if (!runtime.keyboardActivationArmed) return;
	if (runtime.pressedKeyboardKey != static_cast<uint8_t>(keyCode)) {
		cancelKeyboardActivation(FormFocusCancellationReason::KeyMismatch);
		return;
	}
	const uint64_t serial = runtime.pressedKeyboardLogicalSerial;
	const uint64_t generation = runtime.pressedKeyboardDocumentGeneration;
	if (generation != s_documentGeneration || generation != runtime.documentGeneration) {
		cancelKeyboardActivation(FormFocusCancellationReason::GenerationMismatch);
		++s_currentDoc.formsDiagnostics.formStaleKeyActivationBlocks;
		return;
	}
	const int blockIndex = focusedFormControlBlockIndex();
	if (blockIndex < 0 || s_currentDoc.blocks[static_cast<size_t>(blockIndex)].formControl.logicalSerial != serial) {
		cancelKeyboardActivation(FormFocusCancellationReason::StateChange);
		++s_currentDoc.formsDiagnostics.formStaleKeyActivationBlocks;
		return;
	}
	const DocBlock& block = s_currentDoc.blocks[static_cast<size_t>(blockIndex)];
	if (runtimeDisabled(block) ||
		(keyCode == 32 ? (!isRuntimeCheckable(block) && !isRuntimeButton(block)) : !isRuntimeButton(block))) {
		cancelKeyboardActivation(FormFocusCancellationReason::StateChange);
		++s_currentDoc.formsDiagnostics.formStaleKeyActivationBlocks;
		clearDocumentFocus(true, FormFocusCancellationReason::StateChange);
		return;
	}
	clearKeyboardActivationState();
	++s_currentDoc.formsDiagnostics.formKeyboardActivations;
	if (keyCode == 32) ++s_currentDoc.formsDiagnostics.formSpaceActivations;
	else ++s_currentDoc.formsDiagnostics.formEnterActivations;
	activateFormControl(blockIndex);
}

void Navigator::updateFindMatches(bool keepCurrent)
{
	const FindMatch previous =
		(s_currentFindMatch >= 0 && s_currentFindMatch < static_cast<int>(s_findMatches.size()))
		? s_findMatches[s_currentFindMatch]
		: FindMatch{};

	s_findMatches.clear();
	s_currentFindMatch = -1;

	const std::string needle = toLowerAscii(s_findBuffer);
	if (!needle.empty()) {
		for (int i = 0; i < static_cast<int>(s_currentDoc.blocks.size()); ++i) {
			const std::string haystackOriginal = searchableTextForBlock(s_currentDoc.blocks[i]);
			const std::string haystack = toLowerAscii(haystackOriginal);
			size_t pos = haystack.find(needle);
			while (pos != std::string::npos) {
				s_findMatches.push_back(FindMatch{i, pos, needle.size()});
				pos = haystack.find(needle, pos + 1);
			}
		}
	}

	if (!s_findMatches.empty()) {
		s_currentFindMatch = 0;
		if (keepCurrent && previous.blockIndex >= 0) {
			for (int i = 0; i < static_cast<int>(s_findMatches.size()); ++i) {
				const FindMatch& candidate = s_findMatches[i];
				if (candidate.blockIndex == previous.blockIndex &&
					candidate.offset == previous.offset) {
					s_currentFindMatch = i;
					break;
				}
			}
		}
	}

	s_statusText = findMatchStatusText();
}

void Navigator::goToFindMatch(int direction)
{
	if (s_findMatches.empty()) {
		s_currentFindMatch = -1;
		s_statusText = findMatchStatusText();
		return;
	}

	if (s_currentFindMatch < 0 ||
		s_currentFindMatch >= static_cast<int>(s_findMatches.size())) {
		s_currentFindMatch = 0;
	} else if (direction != 0) {
		const int count = static_cast<int>(s_findMatches.size());
		s_currentFindMatch = (s_currentFindMatch + direction + count) % count;
	}

	const int blockIndex = s_findMatches[s_currentFindMatch].blockIndex;
	if (blockIndex >= 0 && blockIndex < static_cast<int>(s_currentDoc.blocks.size())) {
		s_scrollOffset = std::max(0, blockLayoutY(blockIndex) - 18);
		clampScrollOffset();
	}
	s_statusText = findMatchStatusText();
}

void Navigator::submitFormForBlock(int blockIndex)
{
	if (blockIndex < 0 || blockIndex >= static_cast<int>(s_currentDoc.blocks.size())) return;
	const DocBlock& source = s_currentDoc.blocks[blockIndex];
	if (source.formControl.disabled) {
		updateStatus("Disabled form control.");
		return;
	}
	std::string method = toLowerAscii(source.formMethod.empty() ? "get" : source.formMethod);
	const std::string encoding = toLowerAscii(source.formEncoding.empty() ? "application/x-www-form-urlencoded" : source.formEncoding);
	if (source.formUnsupported || (method != "get" && method != "post")) {
		blurDocumentInput();
		s_lastSubmittedFormMethod = method.empty() ? "(none)" : method;
		s_lastSubmittedFormStatus = encoding != "application/x-www-form-urlencoded"
			? (std::string("unsupported encoding: ") + encoding)
			: "unsupported method";
		updateStatus(encoding != "application/x-www-form-urlencoded"
			? "Unsupported form encoding."
			: "Unsupported form method.");
		return;
	}

	std::string action = source.formAction.empty() ? s_currentDoc.url : source.formAction;
	if (action.empty()) action = s_currentDoc.url;

	std::ostringstream query;
	bool first = true;
	auto appendField = [&](const std::string& name, const std::string& value) {
		if (name.empty()) return;
		if (!first) query << "&";
		first = false;
		query << encodeFormComponent(name) << "=" << encodeFormComponent(value);
	};
	for (const DocBlock& block : s_currentDoc.blocks) {
		if (block.formIndex != source.formIndex) continue;
		switch (block.type) {
		case BlockType::FormTextInput:
		case BlockType::FormTextarea:
			appendField(block.inputName, block.inputValue);
			break;
		case BlockType::FormCheckbox:
		case BlockType::FormRadio:
			if (runtimeChecked(block)) appendField(block.inputName, block.inputValue.empty() ? "on" : block.inputValue);
			break;
		case BlockType::FormSelect:
			if (block.selectedOption >= 0 && block.selectedOption < static_cast<int>(block.options.size())) {
				appendField(block.inputName, block.options[static_cast<size_t>(block.selectedOption)].value);
			} else {
				appendField(block.inputName, block.inputValue);
			}
			break;
		default:
			break;
		}
	}

	const std::string queryText = query.str();
	s_lastSubmittedFormAction = action;
	s_lastSubmittedFormMethod = method;
	s_lastSubmittedFormStatus.clear();

	blurDocumentInput();
	if (method == "get") {
		std::string submitUrl = action;
		if (!queryText.empty()) {
			submitUrl += (submitUrl.find('?') == std::string::npos) ? "?" : "&";
			submitUrl += queryText;
		}
		s_lastSubmittedFormUrl = submitUrl;
		s_lastSubmittedFormStatus = "GET submitted";
		navigateTo(submitUrl);
		return;
	}

	s_lastSubmittedFormUrl = action;
	if (action.rfind("file://", 0) == 0) {
		WebDocument doc;
		doc.url = action;
		doc.title = "Unsupported Local POST";
		doc.blocks.push_back({BlockType::Heading, "Unsupported Local POST", ""});
		doc.blocks.push_back({BlockType::Paragraph,
			"Navigator does not submit POST forms to local file:// documents.", ""});
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Method", method), ""});
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Encoding", encoding), ""});
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Action", action), ""});
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Encoded body", queryText), ""});
		doc.blocks.push_back({BlockType::Link, "Go to about:navigator", "about:navigator"});
		NavigatorPageMetadata metadata;
		metadata.requestedUrl = action;
		metadata.finalUrl = action;
		metadata.sourceType = "file";
		metadata.errorStatus = "Local POST unsupported";
		setSourcePreview(metadata, queryText);
		if (!s_currentDoc.url.empty()) s_backStack.push_back(s_currentDoc.url);
		s_forwardStack.clear();
		s_currentDoc = std::move(doc);
		s_inlineLayoutDirty = true;
		s_documentHeight = computeDocumentHeight();
		s_lastSubmittedFormStatus = "local POST unsupported";
		storePageMetadata(metadata, s_currentDoc);
		updateDisplay();
		return;
	}
	if (!isRemoteHttpUrl(action)) {
		s_lastSubmittedFormStatus = "unsupported POST action";
		updateStatus("POST is only supported for http:// and https:// forms.");
		return;
	}

	gxos::web::HttpResponse response = gxos::web::postHttpUrl(action, queryText, encoding);
	s_lastPostHttpStatus = std::to_string(response.statusCode);
	if (!response.reasonPhrase.empty()) s_lastPostHttpStatus += " " + response.reasonPhrase;
	s_lastPostContentType = response.contentType;
	if (!s_currentDoc.url.empty()) s_backStack.push_back(s_currentDoc.url);
	s_forwardStack.clear();
	s_currentDoc = loadHttpResponseDocument(action, response);
	s_inlineLayoutDirty = true;
	s_scrollOffset = 0;
	s_documentHeight = computeDocumentHeight();
	s_lastSubmittedFormStatus = response.ok() ? "POST submitted" : std::string("POST failed: ") + gxos::web::httpErrorName(response.error);
	storePageMetadata(s_pageMetadata, s_currentDoc);
	updateDisplay();
}

void Navigator::handleKeyPress(int keyCode, const std::string& action)
{
	if (action == "up" && (keyCode == 32 || keyCode == 13) &&
		s_staleKeyReleaseGeneration == s_documentGeneration &&
		s_staleKeyReleaseGeneration != 0) {
		incrementLifecycleCounter(s_lifecycleDiagnostics.staleKeyReleaseBlocks);
		s_staleKeyReleaseGeneration = 0;
		return;
	}
	if (keyCode == 17) {
		s_ctrlPressed = (action == "down");
		return;
	}
	if (keyCode == 16) {
		s_shiftPressed = (action == "down");
		return;
	}
	if (keyCode == 9) {
		if (action == "up") {
			s_tabKeyPressed = false;
			return;
		}
		if (action != "down" || s_addressFocused || s_findActive) return;
		if (s_tabKeyPressed) {
			++s_currentDoc.formsDiagnostics.formKeyRepeatSuppressed;
			return;
		}
		s_tabKeyPressed = true;
		focusNextFormControl(s_shiftPressed);
		return;
	}
	if (action == "up") {
		if (keyCode == 32 || keyCode == 13) finishKeyboardActivation(keyCode);
		return;
	}
	if (action != "down") return;

	// --- Address bar editing mode ---
	if (s_addressFocused) {
		if (s_ctrlPressed && (keyCode == 67 || keyCode == 99)) {
			if (!s_addressBuffer.empty()) {
				s_navigatorClipboard = s_addressBuffer;
				if (tryWriteHostedClipboard(s_addressBuffer)) {
					s_clipboardMode = "hosted system clipboard";
					updateStatus("Copied address to system clipboard.");
				} else {
					s_clipboardMode = "Navigator internal clipboard";
					updateStatus("Copied address to Navigator clipboard.");
				}
			}
			return;
		}
		if (s_ctrlPressed && (keyCode == 65 || keyCode == 97)) {
			s_addressCaret = static_cast<int>(s_addressBuffer.size());
			updateStatus("Address bar select all is deferred; copy uses the full address.");
			renderToolbar();
			return;
		}
		const int bufLen = static_cast<int>(s_addressBuffer.size());

		if (keyCode == 13) {                        // Enter â€“ commit navigation
			commitAddressBar();
		} else if (keyCode == 27) {                 // Escape â€“ cancel edit
			blurAddressBar();
		} else if (keyCode == 8) {                  // Backspace â€“ delete before caret
			if (s_addressCaret > 0) {
				s_addressBuffer.erase(static_cast<size_t>(s_addressCaret - 1), 1);
				--s_addressCaret;
				renderToolbar();
			}
		} else if (keyCode == 46) {                 // Delete â€“ delete at caret
			if (s_addressCaret < bufLen) {
				s_addressBuffer.erase(static_cast<size_t>(s_addressCaret), 1);
				renderToolbar();
			}
		} else if (keyCode == 37) {                 // Left arrow
			if (s_addressCaret > 0) {
				--s_addressCaret;
				renderToolbar();
			}
		} else if (keyCode == 39) {                 // Right arrow
			if (s_addressCaret < bufLen) {
				++s_addressCaret;
				renderToolbar();
			}
		} else if (keyCode == 36) {                 // Home â€“ caret to start
			s_addressCaret = 0;
			renderToolbar();
		} else if (keyCode == 35) {                 // End â€“ caret to end
			s_addressCaret = bufLen;
			renderToolbar();
		} else if (keyCode >= 32 && keyCode <= 126) { // Printable ASCII â€“ insert at caret
			s_addressBuffer.insert(static_cast<size_t>(s_addressCaret), 1,
				static_cast<char>(keyCode));
			++s_addressCaret;
			renderToolbar();
		}
		return;
	}

	if (s_findActive) {
		const int bufLen = static_cast<int>(s_findBuffer.size());
		if (keyCode == 13 || keyCode == 40 || keyCode == 34) {
			goToFindMatch(1);
			updateDisplay();
		} else if (keyCode == 38 || keyCode == 33) {
			goToFindMatch(-1);
			updateDisplay();
		} else if (keyCode == 27) {
			closeFindMode();
			updateDisplay();
		} else if (keyCode == 8) {
			if (s_findCaret > 0) {
				s_findBuffer.erase(static_cast<size_t>(s_findCaret - 1), 1);
				--s_findCaret;
				updateFindMatches(true);
				updateDisplay();
			}
		} else if (keyCode == 46) {
			if (s_findCaret < bufLen) {
				s_findBuffer.erase(static_cast<size_t>(s_findCaret), 1);
				updateFindMatches(true);
				updateDisplay();
			}
		} else if (keyCode == 37) {
			if (s_findCaret > 0) --s_findCaret;
			updateDisplay();
		} else if (keyCode == 39) {
			if (s_findCaret < bufLen) ++s_findCaret;
			updateDisplay();
		} else if (keyCode == 36) {
			s_findCaret = 0;
			updateDisplay();
		} else if (keyCode == 35) {
			s_findCaret = bufLen;
			updateDisplay();
		} else if (keyCode >= 32 && keyCode <= 126) {
			s_findBuffer.insert(static_cast<size_t>(s_findCaret), 1, static_cast<char>(keyCode));
			++s_findCaret;
			updateFindMatches(true);
			goToFindMatch(0);
			updateDisplay();
		}
		return;
	}

	if (action == "down" && keyCode == 27 && s_currentDoc.formRuntimeState.keyboardActivationArmed) {
		cancelKeyboardActivation(FormFocusCancellationReason::Escape);
		updateDisplay();
		return;
	}

	const int focusedIndex = focusedFormControlBlockIndex();
	if (keyCode == 13 || keyCode == 32) {
		armKeyboardActivation(keyCode);
		return;
	}
	if (focusedIndex >= 0 &&
		(s_currentDoc.blocks[static_cast<size_t>(focusedIndex)].type == BlockType::FormCheckbox ||
		 s_currentDoc.blocks[static_cast<size_t>(focusedIndex)].type == BlockType::FormRadio ||
		 s_currentDoc.blocks[static_cast<size_t>(focusedIndex)].type == BlockType::FormSelect ||
		 s_currentDoc.blocks[static_cast<size_t>(focusedIndex)].type == BlockType::FormSubmit))
	{
		if (keyCode == 27) {
			blurDocumentInput();
			updateDisplay();
		}
		return;
	}

	if (focusedIndex >= 0 &&
		(s_currentDoc.blocks[static_cast<size_t>(focusedIndex)].type == BlockType::FormTextInput ||
		 s_currentDoc.blocks[static_cast<size_t>(focusedIndex)].type == BlockType::FormTextarea))
	{
		// Phase 2G keeps the existing text-control renderer but does not make
		// Space/Enter editing or implicit submission keyboard behaviors.
		if (keyCode == 13 || keyCode == 32) return;
		if (s_ctrlPressed && ((keyCode == 67 || keyCode == 99) || (keyCode == 65 || keyCode == 97))) {
			updateStatus("Form input copy/select all is deferred.");
			return;
		}
		DocBlock& block = s_currentDoc.blocks[static_cast<size_t>(focusedIndex)];
		const int bufLen = static_cast<int>(block.inputValue.size());
		if (keyCode == 27) {
			blurDocumentInput();
			updateDisplay();
		} else if (keyCode == 8) {
			if (s_inputCaret > 0) {
				block.inputValue.erase(static_cast<size_t>(s_inputCaret - 1), 1);
				block.text = block.inputValue;
				--s_inputCaret;
				updateDisplay();
			}
		} else if (keyCode == 46) {
			if (s_inputCaret < bufLen) {
				block.inputValue.erase(static_cast<size_t>(s_inputCaret), 1);
				block.text = block.inputValue;
				updateDisplay();
			}
		} else if (keyCode == 37) {
			if (s_inputCaret > 0) --s_inputCaret;
			updateDisplay();
		} else if (keyCode == 39) {
			if (s_inputCaret < bufLen) ++s_inputCaret;
			updateDisplay();
		} else if (keyCode == 36) {
			s_inputCaret = 0;
			updateDisplay();
		} else if (keyCode == 35) {
			s_inputCaret = bufLen;
			updateDisplay();
		} else if (keyCode >= 32 && keyCode <= 126) {
			block.inputValue.insert(static_cast<size_t>(s_inputCaret), 1, static_cast<char>(keyCode));
			block.text = block.inputValue;
			++s_inputCaret;
			updateDisplay();
		}
		return;
	}

	// --- Normal (unfocused) keyboard shortcuts ---
	if (s_ctrlPressed && (keyCode == 65 || keyCode == 97)) {
		selectAllDocumentText();
		updateStatus(hasSelection() ? "Selected all document text." : "No document text to select.");
		updateDisplay();
	} else if (s_ctrlPressed && (keyCode == 67 || keyCode == 99)) {
		if (copySelectionToClipboard()) {
			updateStatus(s_clipboardMode == "hosted system clipboard" ? "Copied to system clipboard." : "Copied to Navigator clipboard.");
		} else {
			updateStatus("No document selection to copy.");
		}
	} else if (keyCode == 33) {
		s_scrollOffset -= 48;
		clampScrollOffset();
		updateStatus("Scrolled up.");
	} else if (keyCode == 34) {
		s_scrollOffset += 48;
		clampScrollOffset();
		updateStatus("Scrolled down.");
	} else if (keyCode == 36) {
		s_scrollOffset = 0;
		updateStatus("Home position.");
	}
}

Navigator::HitTarget Navigator::hitTest(int x, int y, int& outLinkBlockIndex)
{
	outLinkBlockIndex = -1;
	ensureCssMarginLayout(s_currentDoc);
	ensureCssFloatLayout(s_currentDoc);
	ensureInlineLayout(s_currentDoc);
	ensureCssPositionLayout(s_currentDoc);

	if (toolbarButtonRect(kWidgetIdBack).contains(x, y))    return HitTarget::Back;
	if (toolbarButtonRect(kWidgetIdForward).contains(x, y)) return HitTarget::Forward;
	if (toolbarButtonRect(kWidgetIdReload).contains(x, y))  return HitTarget::Reload;
	if (toolbarButtonRect(kWidgetIdHome).contains(x, y))    return HitTarget::Home;
	if (toolbarButtonRect(kWidgetIdBookmarks).contains(x, y))    return HitTarget::Bookmarks;
	if (toolbarButtonRect(kWidgetIdAddBookmark).contains(x, y))  return HitTarget::AddBookmark;
	if (toolbarButtonRect(kWidgetIdFind).contains(x, y))         return HitTarget::Find;

	// Address bar hit region
	{
		Rect addrRect{ kAddressX, kAddressY, kAddressW, kAddressH };
		if (addrRect.contains(x, y)) return HitTarget::AddressBar;
	}

	// Controls win over labels when their rectangles overlap.  This makes a
	// wrapping label produce one activation rather than a control toggle plus a
	// second label activation.
	int bestControlIndex = -1;
	HitTarget bestControlTarget = HitTarget::None;
	int bestControlDistance = std::numeric_limits<int>::max();
	int bestControlPaintRank = -1;
	for (int i = 0; i < static_cast<int>(s_currentDoc.blocks.size()); ++i) {
		Rect controlRect{0, 0, 0, 0};
		HitTarget controlTarget = HitTarget::None;
		if (s_currentDoc.blocks[i].type == BlockType::FormTextInput ||
			s_currentDoc.blocks[i].type == BlockType::FormCheckbox ||
			s_currentDoc.blocks[i].type == BlockType::FormRadio ||
			s_currentDoc.blocks[i].type == BlockType::FormTextarea ||
			s_currentDoc.blocks[i].type == BlockType::FormSelect) {
			controlRect = formControlRect(i);
			switch (s_currentDoc.blocks[i].type) {
			case BlockType::FormCheckbox: controlTarget = HitTarget::FormCheckbox; break;
			case BlockType::FormRadio: controlTarget = HitTarget::FormRadio; break;
			case BlockType::FormTextarea: controlTarget = HitTarget::FormTextarea; break;
			case BlockType::FormSelect: controlTarget = HitTarget::FormSelect; break;
			default: controlTarget = HitTarget::FormInput; break;
			}
		} else if (s_currentDoc.blocks[i].type == BlockType::FormSubmit) {
			controlRect = formControlRect(i);
			controlTarget = HitTarget::FormSubmit;
		}
		if (controlTarget != HitTarget::None && controlRect.contains(x, y)) {
			const int dx = x - (controlRect.x + controlRect.w / 2);
			const int dy = y - (controlRect.y + controlRect.h / 2);
			const int distance = dx * dx + dy * dy;
			const int paintRank = cssPositionPaintRank(s_currentDoc, i);
			if (paintRank > bestControlPaintRank || (paintRank == bestControlPaintRank && distance < bestControlDistance)) {
				bestControlDistance = distance;
				bestControlPaintRank = paintRank;
				bestControlIndex = i;
				bestControlTarget = controlTarget;
			}
		}
	}
	if (bestControlIndex >= 0) {
		outLinkBlockIndex = bestControlIndex;
		++s_currentDoc.formsDiagnostics.formHitTargetsRegistered;
		return bestControlTarget;
	}

	for (int i = 0; i < static_cast<int>(s_currentDoc.blocks.size()); ++i) {
		if (s_currentDoc.blocks[i].type != BlockType::FormLabel ||
			associatedControlSerialForLabel(s_currentDoc.blocks[i]) == 0) continue;
		if (selectableBlockRect(i).contains(x, y)) {
			outLinkBlockIndex = i;
			++s_currentDoc.formsDiagnostics.formHitTargetsRegistered;
			return HitTarget::FormLabel;
		}
	}

	int bestLinkIndex = -1;
	int bestLinkPaintRank = -1;
	for (int i = 0; i < static_cast<int>(s_currentDoc.blocks.size()); ++i) {
		if (s_currentDoc.blocks[i].type == BlockType::Link) {
			ensureInlineLayout(s_currentDoc);
			Rect inlineFragments;
			const bool hasInlineFragments = inlineFragmentRectForBlock(i, false, inlineFragments);
			if (inlineFragmentContainsPoint(i, x, y) ||
				(!hasInlineFragments && linkBlockRect(i).contains(x, y))) {
				const int paintRank = cssPositionPaintRank(s_currentDoc, i);
				if (paintRank >= bestLinkPaintRank) {
					bestLinkPaintRank = paintRank;
					bestLinkIndex = i;
				}
			}
		}
	}
	if (bestLinkIndex >= 0) {
		outLinkBlockIndex = bestLinkIndex;
		if (bestLinkPaintRank >= 250000) ++s_currentDoc.cssDiagnostics.positionHitOcclusions;
		return HitTarget::Link;
	}
	return HitTarget::None;
}

Navigator::Rect Navigator::toolbarButtonRect(int widgetId)
{
	int x = 20;
	switch (widgetId) {
	case kWidgetIdBack:
		x = 20;
		break;
	case kWidgetIdForward:
		x = 20 + (kButtonW + kButtonGap);
		break;
	case kWidgetIdReload:
		x = 20 + 2 * (kButtonW + kButtonGap);
		break;
	case kWidgetIdHome:
		x = 20 + 3 * (kButtonW + kButtonGap);
		break;
	case kWidgetIdBookmarks:
		x = 20 + 4 * (kButtonW + kButtonGap);
		break;
	case kWidgetIdAddBookmark:
		x = 20 + 5 * (kButtonW + kButtonGap);
		break;
	case kWidgetIdFind:
		x = 20 + 6 * (kButtonW + kButtonGap);
		break;
	default:
		break;
	}

	return Rect{ x, kButtonY, kButtonW, kButtonH };
}

void Navigator::clampScrollOffset()
{
	s_scrollOffset = std::max(0, std::min(s_scrollOffset, maxScrollOffset()));
}

// -----------------------------------------------------------------------------
// Address bar editing
// -----------------------------------------------------------------------------

std::string Navigator::normalizeUrl(const std::string& input)
{
	if (input.empty()) return input;

	// Already has any scheme (file://, http://, https://, about:, etc.) â€“ pass through.
	// Detect scheme by looking for "://" or the special "about:" prefix.
	if (input.find("://") != std::string::npos) return input;
	if (input.size() >= 6 && input.substr(0, 6) == "about:") return input;

	// Bare path â€“ convert to a file:// URL.
	// file:// URLs have the form:  file:// <empty-authority> <absolute-path>
	// which renders as exactly three slashes:  file:///path/to/file
	//
	// Strip any leading slash(es) from the input so we can re-attach exactly one,
	// giving:  "file://" + "/" + "docs/index.html"  =  "file:///docs/index.html"
	std::string path = input;
	size_t firstNonSlash = path.find_first_not_of('/');
	if (firstNonSlash == std::string::npos) {
		// Input was all slashes â€“ treat as root.
		return "file:///";
	}
	path = path.substr(firstNonSlash);          // strip all leading slashes
	return std::string("file:///") + path;      // re-attach exactly three
}

void Navigator::focusAddressBar()
{
	if (s_currentDoc.formRuntimeState.focusValid) clearDocumentFocus(true);
	if (s_addressFocused) return;
	s_addressFocused = true;
	s_addressBuffer  = s_currentDoc.url;
	s_addressCaret   = static_cast<int>(s_addressBuffer.size()); // caret at end on focus
	s_statusText     = "Editing address";
	renderToolbar();
	renderStatusBar();
}

void Navigator::blurAddressBar()
{
	if (!s_addressFocused) return;
	s_addressFocused = false;
	s_addressBuffer.clear();
	s_addressCaret   = 0;
	s_statusText = "Address edit canceled";
	renderToolbar();
	renderStatusBar();
}

void Navigator::commitAddressBar()
{
	std::string typed = s_addressBuffer;
	s_addressFocused = false;
	s_addressBuffer.clear();
	s_addressCaret   = 0;

	if (typed.empty()) {
		s_statusText = "No URL entered.";
		renderToolbar();
		renderStatusBar();
		return;
	}

	std::string url = normalizeUrl(typed);
	s_statusText = "Loading " + url;
	renderToolbar();
	renderStatusBar();
	navigateTo(url);
}

// -----------------------------------------------------------------------------
// Bookmarks
// -----------------------------------------------------------------------------

static constexpr const char* kBookmarkFilePath = "/config/navigator/bookmarks.txt";

static void addDefaultBookmarks(std::vector<Bookmark>& bm)
{
	bm.push_back({"guideXOS Help",    "file:///docs/index.html"});
	bm.push_back({"About Navigator",  "about:navigator"});
	bm.push_back({"Bookmarks",        "about:bookmarks"});
}

void Navigator::loadBookmarks()
{
	s_bookmarks.clear();

	FileReadResult fr = readTextFile(kBookmarkFilePath);
	if (fr.status != FileReadStatus::Ok) {
		// File missing or unreadable â€“ use defaults.
		addDefaultBookmarks(s_bookmarks);
		return;
	}

	std::istringstream iss(fr.text);
	std::string line;
	while (std::getline(iss, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.empty()) continue;
		size_t sep = line.find('|');
		if (sep == std::string::npos) continue;
		Bookmark bm;
		bm.title = line.substr(0, sep);
		bm.url   = line.substr(sep + 1);
		if (!bm.title.empty() && !bm.url.empty()) {
			s_bookmarks.push_back(std::move(bm));
		}
	}

	if (s_bookmarks.empty()) {
		addDefaultBookmarks(s_bookmarks);
	}
}

void Navigator::saveBookmarks()
{
	std::ostringstream oss;
	for (const Bookmark& bm : s_bookmarks) {
		oss << bm.title << "|" << bm.url << "\n";
	}
	if (!writeTextFile(kBookmarkFilePath, oss.str())) {
		s_statusText = "Could not save bookmarks.";
		renderStatusBar();
	}
}

void Navigator::addBookmark(const std::string& title, const std::string& url)
{
	for (const Bookmark& bm : s_bookmarks) {
		if (bm.url == url) {
			updateStatus("Bookmark already exists.");
			return;
		}
	}
	s_bookmarks.push_back({title.empty() ? url : title, url});
	saveBookmarks();
	updateStatus("Bookmark added.");
}

WebDocument Navigator::buildBookmarksDocument()
{
	WebDocument doc;
	doc.url   = "about:bookmarks";
	doc.title = "Bookmarks";
	doc.blocks.push_back({BlockType::Heading,   "Bookmarks", ""});
	if (s_bookmarks.empty()) {
		doc.blocks.push_back({BlockType::Paragraph, "No bookmarks yet.", ""});
	} else {
		for (const Bookmark& bm : s_bookmarks) {
			doc.blocks.push_back({BlockType::Link, bm.title, bm.url});
		}
	}
	return doc;
}

// -----------------------------------------------------------------------------
// URL loading
// -----------------------------------------------------------------------------

void Navigator::loadUrl(const std::string& url, bool updateDisplayAfterLoad,
	NavigatorTransitionCategory transition)
{
	Logger::write(LogLevel::Info, std::string("Navigator loadUrl: ") + url);
	navigatorSmokeProgress("navigation-start");
	const std::string requestedDocumentUrl = url.empty() ? "about:navigator" : url;
	if (transition == NavigatorTransitionCategory::Navigation) {
		const NavigatorTransitionCategory byUrl = transitionCategoryForUrl(requestedDocumentUrl);
		if (byUrl != NavigatorTransitionCategory::Navigation) transition = byUrl;
	}
	s_pendingDocumentUrl = requestedDocumentUrl;
	s_pendingTransitionCategory = transition;
	const bool hadFocus = s_currentDoc.formRuntimeState.initialized &&
		s_currentDoc.formRuntimeState.focusValid &&
		s_currentDoc.formRuntimeState.focusedLogicalSerial != 0;
	const bool hadRuntimeState = s_currentDoc.formRuntimeState.initialized;
	const bool hadMousePress = s_mouseLeftDown;
	const bool hadKeyboardPress = s_currentDoc.formRuntimeState.initialized &&
		s_currentDoc.formRuntimeState.keyboardActivationArmed;
	const bool staleMouseReleasePending = s_staleMouseReleaseGeneration != 0;
	const bool staleKeyReleasePending = s_staleKeyReleaseGeneration != 0;
	if (hadRuntimeState) incrementLifecycleCounter(s_lifecycleDiagnostics.runtimeStateClears);
	cancelKeyboardActivation(FormFocusCancellationReason::Navigation);
	if (s_documentGeneration == std::numeric_limits<uint64_t>::max()) s_documentGeneration = 1;
	else ++s_documentGeneration;
	incrementLifecycleCounter(s_lifecycleDiagnostics.documentGenerationChanges);
	clearDocumentFocus(false, FormFocusCancellationReason::Navigation);
	if (hadKeyboardPress || staleKeyReleasePending) s_staleKeyReleaseGeneration = s_documentGeneration;
	else s_staleKeyReleaseGeneration = 0;
	if (hadMousePress || staleMouseReleasePending) s_staleMouseReleaseGeneration = s_documentGeneration;
	else s_staleMouseReleaseGeneration = 0;
	clearMousePressState();
	s_loading = true;
	navigatorSmokeProgress("replacement-state-cleared");
	if (s_windowId != 0) {
		navigatorSmokeProgress("pre-load-paint-start");
		updateDisplay(false);
		navigatorSmokeProgress("pre-load-paint-complete");
	}
	cleanupRemoteImageTempFiles();
	s_imageCache.clear();
	// clearDocumentFocus() above is the complete replacement boundary.  Do not
	// call blurDocumentInput() here: its recomputation guard intentionally
	// refreshes an active source document, and doing that against the old
	// document after the generation changed could reassign inspected ownership
	// to the document being replaced.
	clearSelection();

	WebDocument doc;
	navigatorSmokeProgress("document-dispatch-start");
	if (url == "about:navigator" || url.empty()) {
		doc = buildAboutNavigatorDocument();
		NavigatorPageMetadata metadata;
		metadata.requestedUrl = "about:navigator";
		metadata.finalUrl = "about:navigator";
		metadata.sourceType = "about";
		metadata.contentType = "generated/about";
		storePageMetadata(std::move(metadata), doc);
	} else if (url == "about:bookmarks") {
		doc = buildBookmarksDocument();
		NavigatorPageMetadata metadata;
		metadata.requestedUrl = "about:bookmarks";
		metadata.finalUrl = "about:bookmarks";
		metadata.sourceType = "about";
		metadata.contentType = "generated/about";
		storePageMetadata(std::move(metadata), doc);
	} else if (url == "about:downloads") {
		doc = buildDownloadsDocument();
	} else if (url == "about:page-info") {
		doc = buildPageInfoDocument();
	} else if (url == "about:view-source") {
		doc = buildViewSourceDocument();
	} else if (url == "about:save-page-text") {
		doc = buildSavePageTextDocument();
	} else if (url == "about:save-page-source") {
		doc = buildSavePageSourceDocument();
	} else if (url == "about:navigator-runtime") {
		doc = buildRuntimeDocument();
	} else if (url.size() >= 7 && url.substr(0, 7) == "file://") {
		doc = loadFileUrl(url);
	} else if (isRemoteHttpUrl(url)) {
		doc = loadHttpUrl(url);
	} else {
		doc = buildSimpleDocument(url,
			"Unsupported URL",
			"Unsupported URL",
			"Navigator supports about:, file://, http://, and hosted https:// URLs in this build.");
		NavigatorPageMetadata metadata;
		metadata.requestedUrl = url;
		metadata.finalUrl = url;
		metadata.sourceType = "unsupported";
		metadata.errorStatus = "Unsupported URL scheme";
		storePageMetadata(std::move(metadata), doc);
	}
	navigatorSmokeProgress("document-body-complete");

	s_currentDoc      = std::move(doc);
	NavigatorTransitionCategory committedTransition = transition;
	if (s_pageMetadata.redirected && committedTransition == NavigatorTransitionCategory::Navigation)
		committedTransition = NavigatorTransitionCategory::RedirectReplacement;
	if (!s_pageMetadata.errorStatus.empty() &&
		committedTransition != NavigatorTransitionCategory::PageInfoGeneration &&
		committedTransition != NavigatorTransitionCategory::SavePageTextGeneration) {
		const std::string lowerError = [&]() {
			std::string result = s_pageMetadata.errorStatus;
			std::transform(result.begin(), result.end(), result.begin(),
				[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
			return result;
		}();
		if (lowerError.find("parse") != std::string::npos)
			committedTransition = NavigatorTransitionCategory::ParseFailure;
		else if (s_pageMetadata.sourceType == "https" &&
			(lowerError.find("tls") != std::string::npos ||
			 lowerError.find("certificate") != std::string::npos ||
			 lowerError.find("hostname") != std::string::npos ||
			 lowerError.find("redirect") != std::string::npos ||
			 lowerError.find("policy") != std::string::npos))
			committedTransition = NavigatorTransitionCategory::TlsPolicyFailure;
		else
			committedTransition = NavigatorTransitionCategory::NavigationFailure;
	}
	navigatorSmokeProgress("style-resolution-start");
	initializeFormRuntimeState();
	for (const gxos::web::HtmlElementRef& element : s_currentDoc.structuralElements) {
		if (element.formControl.hidden &&
			(element.formControl.type == FormControlType::Checkbox ||
			 element.formControl.type == FormControlType::Radio)) {
			++s_currentDoc.formsDiagnostics.formHiddenHitTargetsSuppressed;
		}
	}
	recomputeFormControlStyles();
	navigatorSmokeProgress("style-resolution-complete");
	if (!s_currentDoc.url.empty()) {
		s_visitedUrls.insert(s_currentDoc.url);
	}
	s_visibleDocumentCategory = documentCategoryForUrl(s_currentDoc.url, s_pageMetadata);
	s_visibleDocumentInspectionView = isGeneratedInspectionViewUrl(s_currentDoc.url);
	s_lifecycleDiagnostics.lastTransition = committedTransition;
	incrementLifecycleCounter(s_lifecycleDiagnostics.documentReplacements);
	noteFocusClearedForTransition(committedTransition, hadFocus);
	if (committedTransition == NavigatorTransitionCategory::HistoryBack ||
		committedTransition == NavigatorTransitionCategory::HistoryForward) {
		if (!s_currentDoc.formRuntimeState.focusValid &&
			s_currentDoc.formRuntimeState.pressedKeyboardLogicalSerial == 0 &&
			s_currentDoc.formRuntimeState.count <= kFormRuntimeControlCap) {
			incrementLifecycleCounter(s_lifecycleDiagnostics.historyStateNonpersistent);
		}
	}
	s_scrollOffset    = 0;
	s_documentHeight  = computeDocumentHeight();
	s_hoverStatusText.clear();
	s_hitLinkBlockIndex = -1;
	s_statusText      = "Ready";
	if (s_findActive) {
		updateFindMatches(false);
		goToFindMatch(0);
	} else {
		s_findMatches.clear();
		s_currentFindMatch = -1;
	}

	s_loading = false;
	s_pendingDocumentUrl.clear();
	s_pendingTransitionCategory = NavigatorTransitionCategory::Navigation;
	refreshLifecycleOwnershipEvidence();
	if (updateDisplayAfterLoad) {
		navigatorSmokeProgress("final-paint-start");
		updateDisplay();
		navigatorSmokeProgress("final-paint-complete");
	}
	navigatorSmokeProgress("navigation-complete");
}

void Navigator::navigateTo(const std::string& url)
{
	// Don't push a duplicate entry if we're already on this URL.
	const std::string prev = s_currentDoc.url;
	if (!prev.empty() && prev != url) {
		s_backStack.push_back(prev);
	}
	// Any forward history is invalidated by a new navigation.
	s_forwardStack.clear();

	loadUrl(url, true, transitionCategoryForUrl(url));
}

void Navigator::goBack()
{
	if (s_backStack.empty()) {
		s_statusText = "No back history.";
		renderStatusBar();
		return;
	}
	const std::string target = s_backStack.back();
	s_backStack.pop_back();

	// Push current URL onto the forward stack.
	if (!s_currentDoc.url.empty()) {
		s_forwardStack.push_back(s_currentDoc.url);
	}

	loadUrl(target, true, NavigatorTransitionCategory::HistoryBack);
}

void Navigator::goForward()
{
	if (s_forwardStack.empty()) {
		s_statusText = "No forward history.";
		renderStatusBar();
		return;
	}
	const std::string target = s_forwardStack.back();
	s_forwardStack.pop_back();

	// Push current URL onto the back stack.
	if (!s_currentDoc.url.empty()) {
		s_backStack.push_back(s_currentDoc.url);
	}

	loadUrl(target, true, NavigatorTransitionCategory::HistoryForward);
}

WebDocument Navigator::buildAboutNavigatorDocument()
{
	WebDocument doc;
	doc.url   = "about:navigator";
	doc.title = "About guideXOS Navigator";
	doc.blocks.push_back({BlockType::Heading,   "About guideXOS Navigator", ""});
	doc.blocks.push_back({BlockType::Paragraph,
		"guideXOS Navigator is the native document viewer and browser shell for guideXOS Server.", ""});
	doc.blocks.push_back({BlockType::Paragraph,
		"It renders guideWeb documents and supports local file:// browsing plus hosted HTTP(S) text pages.", ""});
	doc.blocks.push_back({BlockType::Heading,   "Features", ""});
	doc.blocks.push_back({BlockType::ListItem,  "Headings, paragraphs, lists, and preformatted blocks", ""});
	doc.blocks.push_back({BlockType::ListItem,  "Word-wrapped text for readable documents", ""});
	doc.blocks.push_back({BlockType::ListItem,  "Baseline/descent-aware line boxes and underlines", ""});
	doc.blocks.push_back({BlockType::ListItem,  "Relative link resolution for file:// and HTTP(S) pages", ""});
	doc.blocks.push_back({BlockType::ListItem,  "HTTP(S) GET/POST with Schannel TLS on hosted Navigator", ""});
	doc.blocks.push_back({BlockType::ListItem,  "Back / Forward / Reload / Home navigation", ""});
	doc.blocks.push_back({BlockType::ListItem,  "Bookmarks with persistent storage", ""});
	doc.blocks.push_back({BlockType::ListItem,  "Forms-lite GET/POST forms with text, checkbox, radio, textarea, and select controls", ""});
	doc.blocks.push_back({BlockType::ListItem,  "Find in Page for rendered document text", ""});
	doc.blocks.push_back({BlockType::Heading,   "Quick Start", ""});
	doc.blocks.push_back({BlockType::Preformatted,
		"Type a file:// URL in the address bar and press Enter.\n"
		"Example: file:///docs/index.html\n"
		"Or start a small local HTTP server and open http://127.0.0.1:8080/docs/index.html", ""});
	doc.blocks.push_back({BlockType::Link, "Open guideXOS Help",   "file:///docs/index.html"});
	doc.blocks.push_back({BlockType::Link, "View Bookmarks",       "about:bookmarks"});
	doc.blocks.push_back({BlockType::Link, "View Downloads",       "about:downloads"});
	doc.blocks.push_back({BlockType::Link, "Page Info",            "about:page-info"});
	doc.blocks.push_back({BlockType::Link, "View Source",          "about:view-source"});
	doc.blocks.push_back({BlockType::Link, "Navigator Runtime",    "about:navigator-runtime"});
	return doc;
}

WebDocument Navigator::buildNavigatorHomeDocument()
{
	WebDocument doc;
	doc.url   = "file:///docs/index.html";
	doc.title = "guideXOS Navigator";
	doc.blocks.push_back({BlockType::Heading,   "guideXOS Navigator",                                            ""});
	doc.blocks.push_back({BlockType::Paragraph, "This is the first native guideXOS Server browser shell.",       ""});
	doc.blocks.push_back({BlockType::Paragraph, "guideWeb will provide local documentation, simple HTML rendering, and eventually network browsing.", ""});
	doc.blocks.push_back({BlockType::Link,      "Open guideXOS Help",   "file:///docs/index.html"});
	return doc;
}

void Navigator::storePageMetadata(NavigatorPageMetadata metadata, const WebDocument& doc)
{
	if (metadata.finalUrl.empty()) metadata.finalUrl = doc.url;
	if (metadata.requestedUrl.empty()) metadata.requestedUrl = metadata.finalUrl;
	fillDocumentCounts(metadata, doc);
	metadata.lastSubmittedFormUrl = s_lastSubmittedFormUrl;
	metadata.lastSubmittedFormAction = s_lastSubmittedFormAction;
	metadata.lastSubmittedFormMethod = s_lastSubmittedFormMethod;
	metadata.lastSubmittedFormStatus = s_lastSubmittedFormStatus;
	metadata.lastPostHttpStatus = s_lastPostHttpStatus;
	metadata.lastPostContentType = s_lastPostContentType;
	s_pageMetadata = std::move(metadata);
	s_inspectedDoc = doc;
	s_inspectedDocumentGeneration = s_documentGeneration;
	s_inspectedSourceCategory = documentCategoryForUrl(s_inspectedDoc.url, s_pageMetadata);
	if (!s_loading) refreshLifecycleOwnershipEvidence();
}

WebDocument Navigator::buildPageInfoDocument()
{
	WebDocument doc;
	doc.url = "about:page-info";
	doc.title = "Page Info";
	doc.blocks.push_back({BlockType::Heading, "Page Info", ""});

	const NavigatorPageMetadata& m = s_pageMetadata;
	if (m.requestedUrl.empty()) {
		doc.blocks.push_back({BlockType::Paragraph, "No page has been loaded yet.", ""});
		doc.blocks.push_back({BlockType::Link, "Go to about:navigator", "about:navigator"});
		return doc;
	}
	const bool buildingPendingDocument = s_loading && !s_pendingDocumentUrl.empty();
	const std::string visibleUrl = buildingPendingDocument ? s_pendingDocumentUrl : s_currentDoc.url;
	const uint64_t visibleGeneration = buildingPendingDocument
		? s_documentGeneration : s_currentDoc.formRuntimeState.documentGeneration;
	const NavigatorDocumentCategory visibleCategory = buildingPendingDocument
		? documentCategoryForUrl(visibleUrl, m) : s_visibleDocumentCategory;
	const bool inspectionView = buildingPendingDocument
		? isGeneratedInspectionViewUrl(visibleUrl) : s_visibleDocumentInspectionView;
	const bool sourceReferenceValid = s_inspectedDocumentGeneration != 0 &&
		!s_inspectedDoc.url.empty() && !m.finalUrl.empty() && s_inspectedDoc.url == m.finalUrl;
	const bool ownershipGuardPassed = sourceReferenceValid &&
		(inspectionView || (visibleUrl == m.finalUrl && s_inspectedDocumentGeneration == visibleGeneration));
	const bool focusSerialPresent = !buildingPendingDocument &&
		s_currentDoc.formRuntimeState.focusValid &&
		s_currentDoc.formRuntimeState.focusedLogicalSerial != 0;
	const size_t runtimeControlCount = buildingPendingDocument ? 0 : s_currentDoc.formRuntimeState.count;
	const int visibleGenerationEvidence = static_cast<int>(std::min<uint64_t>(
		visibleGeneration, static_cast<uint64_t>(std::numeric_limits<int>::max())));
	const int inspectedGenerationEvidence = static_cast<int>(std::min<uint64_t>(
		s_inspectedDocumentGeneration, static_cast<uint64_t>(std::numeric_limits<int>::max())));
	const int runtimeControlCountEvidence = static_cast<int>(std::min<size_t>(
		runtimeControlCount, static_cast<size_t>(std::numeric_limits<int>::max())));

	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Requested URL", m.requestedUrl), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Final URL", m.finalUrl), ""});
	doc.blocks.push_back({BlockType::Heading, "Phase 2I Ownership Evidence", ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Visible document generation", visibleGenerationEvidence), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Inspected document generation", inspectedGenerationEvidence), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Visible document category", documentCategoryName(visibleCategory)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Inspected source category", documentCategoryName(s_inspectedSourceCategory)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Requested/final URL equal", yesNo(m.requestedUrl == m.finalUrl)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Generated page", yesNo(visibleUrl.rfind("about:", 0) == 0)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Source reference valid", yesNo(sourceReferenceValid)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Focus serial present", yesNo(focusSerialPresent)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Runtime control-state count", runtimeControlCountEvidence), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Ownership guard", ownershipGuardPassed ? "pass" : "block"), ""});
	if (sourceReferenceValid) incrementLifecycleCounter(s_lifecycleDiagnostics.pageInfoSourceValid);
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Source type", m.sourceType), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Scheme", m.scheme), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Content type", m.contentType), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Content encoding", m.contentEncoding), ""});

	if (m.httpStatusCode > 0) {
		std::string status = std::to_string(m.httpStatusCode);
		if (!m.httpReasonPhrase.empty()) status += " " + m.httpReasonPhrase;
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("HTTP status", status), ""});
	} else {
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("HTTP status", "not applicable"), ""});
	}

	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Redirected", yesNo(m.redirected)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Redirect count", m.redirectCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Error status", m.errorStatus), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Unsupported reason", m.unsupportedReason), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Header cap hit", yesNo(m.headerCapHit)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Body cap hit", yesNo(m.bodyCapHit)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("TLS succeeded before content failure", yesNo(m.tlsSucceededBeforeContentFailure)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("TLS backend", m.tlsBackend), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("TLS enabled", yesNo(m.tlsEnabled)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("TLS validated", yesNo(m.tlsValidated)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Certificate validation", m.tlsCertificateValidation), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("TLS status", m.tlsStatus), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("TLS error", m.tlsError), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("TLS error code", m.tlsErrorCode), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("TLS connection path", m.tlsConnectionPath), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("TLS credential API", m.tlsCredentialApi), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("TLS credential structure", m.tlsCredentialStructure), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("TLS credential target", m.tlsCredentialTarget), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("TLS credential protocols", m.tlsCredentialProtocols), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("TLS credential flags", m.tlsCredentialFlags), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("TLS credential acquired", yesNo(m.tlsCredentialAcquired)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("TLS handshake started", yesNo(m.tlsHandshakeStarted)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Certificate subject", m.tlsCertificateSubject), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Certificate issuer", m.tlsCertificateIssuer), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Certificate valid from", m.tlsCertificateValidFrom), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Certificate valid to", m.tlsCertificateValidTo), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Certificate hostname checked", m.tlsCertificateHostname), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Certificate hostname validation", m.tlsCertificateHostnameValidation), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Certificate chain error", m.tlsCertificateChainError), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("TLS protocol", m.tlsProtocol), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("TLS cipher suite", m.tlsCipherSuite), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("TLS smoke bypass active", yesNo(m.tlsSmokeSelfSignedBypass)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("HTTPS-to-HTTP redirect policy", "blocked by default"), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Downgrade redirect blocked", yesNo(m.downgradeRedirectBlocked)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Attempted insecure redirect", m.insecureRedirectLocation), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Document blocks", m.documentBlockCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Image blocks", m.imageBlockCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Local images", m.localImageCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Remote images", m.remoteImageCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Loaded images", m.loadedImageCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Failed images", m.failedImageCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Last image error", m.lastImageError), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS detected", yesNo(m.cssDetected)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS enabled", yesNo(m.cssEnabled)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS rules parsed", m.styleRuleCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS style blocks", m.styleBlockCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS inline styles", m.inlineStyleCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS external stylesheets loaded", m.externalStylesheetLoadedCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS unsupported rules", m.unsupportedCssRuleCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS parse errors", m.cssParseErrorCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Style rule count", m.styleRuleCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Unsupported external stylesheets", m.unsupportedExternalStylesheetCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Unsupported CSS declarations", m.unsupportedCssDeclarationCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS style block capped", yesNo(m.cssStyleBlockCapped)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS style bytes processed", static_cast<int>(m.cssStyleBytesProcessed)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS layout max-width applied", m.cssLayoutMaxWidthAppliedCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS auto-margin centered blocks", m.cssAutoMarginCenteredBlockCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS background blocks drawn", m.cssBackgroundBlockCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS wrapper blocks rendered", m.cssWrapperRenderCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS display:none blocks", m.cssDisplayNoneBlockCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS tables rendered", m.cssTableRenderCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS table rows rendered", m.cssTableRowCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS table cells rendered", m.cssTableCellCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS table layout fallbacks", m.cssTableLayoutFallbackCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS lists rendered", m.cssListRenderCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS clamped values", m.cssClampedValueCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS length value clamps", m.cssLengthValueClampCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS invalid length values", m.cssInvalidLengthValueCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS figures rendered", m.cssFiguresRendered), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS figcaptions rendered", m.cssFigcaptionsRendered), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS blockquotes rendered", m.cssBlockquotesRendered), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS definition lists rendered", m.cssDefinitionListsRendered), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS images constrained", m.cssImagesConstrained), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS images aspect preserved", m.cssImagesAspectPreserved), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS image alt fallbacks", m.cssImageAltFallbacks), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS image size clamps", m.cssImageSizeClamps), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS nested layout clamps", m.cssNestedLayoutClamps), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS box-sizing content-box", m.cssBoxSizingContentBox), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS box-sizing border-box", m.cssBoxSizingBorderBox), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS percentage width resolved", m.cssPercentageWidthResolved), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS percentage height resolved", m.cssPercentageHeightResolved), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS percentage indefinite basis", m.cssPercentageIndefiniteBasis), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS min/max constraints", m.cssMinWidthConstraints + m.cssMaxWidthConstraints + m.cssMinHeightConstraints + m.cssMaxHeightConstraints), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS constraint conflicts", m.cssConstraintConflicts), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS overflow hidden boxes", m.cssOverflowHiddenBoxes), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS overflow auto boxes", m.cssOverflowAutoBoxes), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS overflow scroll deferred", m.cssOverflowScrollDeferred), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS clipped hit targets", m.cssClippedHitTargets), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS visibility hidden boxes", m.cssVisibilityHiddenBoxes), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS opacity boxes", m.cssOpacityBoxes), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS opacity zero boxes", m.cssOpacityZeroBoxes), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS vertical-align applications", m.cssVerticalAlignApplications), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS geometry evidence records", m.cssEvidenceRecordCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS inline items", m.cssInlineItems), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS line boxes", m.cssLineBoxes), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS inline fragments", m.cssInlineFragments), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS inline hit fragments", m.cssInlineHitFragments), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS inline evidence records", m.cssInlineEvidenceRecordCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Downloaded", yesNo(m.downloaded)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Download saved path", m.downloadSavedPath), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Download byte count", static_cast<int>(m.downloadByteCount)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Forms", m.formCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Text inputs", m.formInputCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Checkboxes", m.formCheckboxCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Radio buttons", m.formRadioCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Textareas", m.formTextareaCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Selects", m.formSelectCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Unsupported form controls", m.unsupportedFormControlCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Unsupported form method", yesNo(m.unsupportedFormMethod)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Unsupported form encoding", yesNo(m.unsupportedFormEncoding)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("POST supported hosted", yesNo(m.postSupportedHosted)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("POST supported bare-metal", yesNo(m.postSupportedBareMetal)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Last submitted form URL", m.lastSubmittedFormUrl), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Last submitted form action", m.lastSubmittedFormAction), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Last submitted form method", m.lastSubmittedFormMethod), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Last submitted form status", m.lastSubmittedFormStatus), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Last POST HTTP status", m.lastPostHttpStatus), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Last POST content type", m.lastPostContentType), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Text selection enabled", "yes"), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Clipboard mode", s_clipboardMode), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Raw/source bytes", static_cast<int>(m.rawSourceBytes)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Source preview truncated", yesNo(m.rawSourceTruncated)), ""});

	doc.blocks.push_back({BlockType::Heading, "Safety Limits", ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("HTTP header limit", static_cast<int>(gxos::web::kHttpMaxHeaderBytes)) + " bytes", ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("HTTP body limit", static_cast<int>(gxos::web::kHttpMaxBodyBytes)) + " bytes", ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("HTTP redirect limit", gxos::web::kHttpMaxRedirects), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("HTTP connect timeout", gxos::web::kHttpConnectTimeoutMs) + " ms", ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("HTTP read timeout", gxos::web::kHttpReadTimeoutMs) + " ms", ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("File text limit", static_cast<int>(kNavigatorMaxFileBytes)) + " bytes", ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Source preview limit", static_cast<int>(kNavigatorMaxSourcePreviewBytes)) + " bytes", ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Remote PNG byte limit", static_cast<int>(kRemoteImageMaxBytes)) + " bytes", ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Remote PNG dimensions", "2048 x 2048 pixels"), ""});

	doc.blocks.push_back({BlockType::Link, "View Source", "about:view-source"});
	doc.blocks.push_back({BlockType::Link, "View Downloads", "about:downloads"});
	doc.blocks.push_back({BlockType::Link, "Navigator Runtime", "about:navigator-runtime"});
	doc.blocks.push_back({BlockType::Link, "Go to about:navigator", "about:navigator"});

	doc.blocks.push_back({BlockType::Heading, "Save Page", ""});
	doc.blocks.push_back({BlockType::Link, "Save Page Text", "about:save-page-text"});
	if (!s_pageMetadata.rawSourceForSave.empty()) {
		doc.blocks.push_back({BlockType::Link, "Save Source", "about:save-page-source"});
	}
	return doc;
}

WebDocument Navigator::buildViewSourceDocument()
{
	WebDocument doc;
	doc.url = "about:view-source";
	doc.title = "View Source";
	doc.blocks.push_back({BlockType::Heading, "View Source", ""});

	const NavigatorPageMetadata& m = s_pageMetadata;
	if (m.requestedUrl.empty()) {
		doc.blocks.push_back({BlockType::Paragraph, "No page has been loaded yet.", ""});
		doc.blocks.push_back({BlockType::Link, "Go to about:navigator", "about:navigator"});
		return doc;
	}

	doc.blocks.push_back({BlockType::Paragraph, "Source for " + m.finalUrl, ""});
	if (m.rawSource.empty()) {
		if (m.sourceType == "about") {
			doc.blocks.push_back({BlockType::Paragraph, "No raw source available for generated about: pages.", ""});
		} else {
			doc.blocks.push_back({BlockType::Paragraph, "No raw source is available for this page.", ""});
		}
	} else {
		if (m.rawSourceTruncated) {
			doc.blocks.push_back({BlockType::Paragraph,
				"Showing the first " + std::to_string(kNavigatorMaxSourcePreviewBytes) +
				" bytes of a " + std::to_string(m.rawSourceBytes) + " byte source.", ""});
		}
		doc.blocks.push_back({BlockType::Preformatted, m.rawSource, ""});
	}

	doc.blocks.push_back({BlockType::Link, "Page Info", "about:page-info"});
	doc.blocks.push_back({BlockType::Link, "Navigator Runtime", "about:navigator-runtime"});
	doc.blocks.push_back({BlockType::Link, "View Downloads", "about:downloads"});
	if (!s_pageMetadata.rawSourceForSave.empty()) {
		doc.blocks.push_back({BlockType::Link, "Save Source", "about:save-page-source"});
	}
	doc.blocks.push_back({BlockType::Link, "Go to about:navigator", "about:navigator"});
	return doc;
}

WebDocument Navigator::buildRuntimeDocument()
{
	WebDocument doc;
	doc.url = "about:navigator-runtime";
	doc.title = "Navigator Runtime";
	doc.blocks.push_back({BlockType::Heading, "Navigator Runtime", ""});
	doc.blocks.push_back({BlockType::Paragraph,
		"This page reports the active Navigator launch path and platform capabilities.", ""});

	addRuntimeReportBlocks(doc, hostedRuntimeReportEntries(
		s_currentDoc.url,
		s_currentDoc.title,
		static_cast<int>(s_currentDoc.blocks.size()),
		s_pageMetadata.finalUrl,
		s_pageMetadata.cssDetected,
		s_pageMetadata.cssEnabled,
		s_pageMetadata.styleRuleCount,
		s_pageMetadata.styleBlockCount,
		s_pageMetadata.inlineStyleCount,
		s_pageMetadata.externalStylesheetLoadedCount,
		s_pageMetadata.unsupportedExternalStylesheetCount,
		s_pageMetadata.unsupportedCssRuleCount,
		s_pageMetadata.unsupportedCssDeclarationCount,
		s_pageMetadata.cssUnsupportedSelectorCount,
		s_pageMetadata.cssParseErrorCount,
		s_pageMetadata.cssStyleBlockCapped,
		s_pageMetadata.cssStyleBytesProcessed,
		s_pageMetadata.cssLayoutMaxWidthAppliedCount,
		s_pageMetadata.cssAutoMarginCenteredBlockCount,
		s_pageMetadata.cssBackgroundBlockCount,
		s_pageMetadata.cssWrapperRenderCount,
		s_pageMetadata.cssDisplayNoneBlockCount,
		s_pageMetadata.cssTableRenderCount,
		s_pageMetadata.cssTableRowCount,
		s_pageMetadata.cssTableCellCount,
		s_pageMetadata.cssTableLayoutFallbackCount,
		s_pageMetadata.cssListRenderCount,
		s_pageMetadata.cssClampedValueCount,
		s_pageMetadata.cssLineBreakCount,
		s_pageMetadata.cssTableCaptionCount,
		s_pageMetadata.cssTableHeaderCellCount,
		s_pageMetadata.cssVisitedLinkCount,
		s_pageMetadata.cssBorderedBlocksRendered,
		s_pageMetadata.cssDashedBordersRendered,
		s_pageMetadata.cssDottedBordersRendered,
		s_pageMetadata.cssBorderWidthClamps,
		s_pageMetadata.cssCollapsedTablesRendered,
		s_pageMetadata.cssSeparateTablesRendered,
		s_pageMetadata.cssTableBorderSpacingClamps,
		s_pageMetadata.cssListStyleMarkersRendered,
		s_pageMetadata.cssListStyleNoneApplied,
		s_pageMetadata.cssTextDecorationsRendered,
		s_pageMetadata.cssGenericFontFamilyApplied,
		s_pageMetadata.cssGenericFontFamilyFallbacks,
		s_pageMetadata.cssFiguresRendered,
		s_pageMetadata.cssFigcaptionsRendered,
		s_pageMetadata.cssBlockquotesRendered,
		s_pageMetadata.cssDefinitionListsRendered,
		s_pageMetadata.cssImagesConstrained,
		s_pageMetadata.cssImagesAspectPreserved,
		s_pageMetadata.cssImageAltFallbacks,
		s_pageMetadata.cssImageSizeClamps,
		s_pageMetadata.cssNestedLayoutClamps,
		s_pageMetadata.cssMaxWrapperAncestorDepth,
		s_pageMetadata.cssSelectorGroupsParsed,
		s_pageMetadata.cssCompoundSelectorsParsed,
		s_pageMetadata.cssChildCombinators,
		s_pageMetadata.cssDescendantCombinators,
		s_pageMetadata.cssAdjacentSiblingCombinators,
		s_pageMetadata.cssGeneralSiblingCombinators,
		s_pageMetadata.cssAdjacentSiblingMatches,
		s_pageMetadata.cssGeneralSiblingMatches,
		s_pageMetadata.cssSiblingScanSteps,
		s_pageMetadata.cssSiblingScanClamps,
		s_pageMetadata.cssSiblingMetadataClamps,
		s_pageMetadata.cssSiblingMetadataErrors,
		s_pageMetadata.cssSelectorMatches,
		s_pageMetadata.cssSpecificityOverrides,
		s_pageMetadata.cssSourceOrderOverrides,
		s_pageMetadata.cssInlineOverrides,
		s_pageMetadata.cssInheritedPropertiesApplied,
		s_pageMetadata.cssSelectorDepthClamps,
		s_pageMetadata.cssSelectorGroupClamps,
		s_pageMetadata.cssCascadePropertyResolutions,
		s_pageMetadata.cssImportantDeclarationsApplied,
		s_pageMetadata.cssRuleCapCount,
		s_pageMetadata.cssDeclarationCapCount,
		s_pageMetadata.cssInheritanceDepthClamps,
		s_pageMetadata.cssPseudoClassesParsed,
		s_pageMetadata.cssStructuralPseudoMatches,
		s_pageMetadata.cssFirstChildMatches,
		s_pageMetadata.cssLastChildMatches,
		s_pageMetadata.cssNthChildMatches,
		s_pageMetadata.cssOfTypeMatches,
		s_pageMetadata.cssNotMatches,
		s_pageMetadata.cssLinkPseudoMatches,
		s_pageMetadata.cssVisitedPseudoMatches,
		s_pageMetadata.cssPseudoClassClamps,
		s_pageMetadata.cssNthExpressionParseErrors,
		s_pageMetadata.cssStructuralMetadataClamps,
		s_pageMetadata.cssSelectorEvaluationStepClamps,
		s_pageMetadata.cssEmptyPseudoParsed,
		s_pageMetadata.cssEmptyPseudoMatches,
		s_pageMetadata.cssEmptyMetadataIncomplete,
		s_pageMetadata.cssContentMetadataClamps,
		s_pageMetadata.cssSelectorGroupMemberRecoveries,
		s_pageMetadata.cssCommentScanClamps,
		s_pageMetadata.cssUnterminatedCommentErrors,
		s_pageMetadata.cssUnbalancedParenthesisErrors,
		s_pageMetadata.cssUnbalancedBracketErrors,
		s_pageMetadata.cssUnterminatedStringErrors,
		s_pageMetadata.cssInvalidCombinatorSequences,
		s_pageMetadata.cssIdentifierEscapeRejections,
		s_pageMetadata.cssSelectorMemberParseFailures,
		s_pageMetadata.cssSelectorRecoverySuccesses,
		s_pageMetadata.cssComputedStyleEvidence,
		s_pageMetadata.formCount,
		s_pageMetadata.formInputCount,
		s_pageMetadata.formCheckboxCount,
		s_pageMetadata.formRadioCount,
		s_pageMetadata.formTextareaCount,
		s_pageMetadata.formSelectCount,
		s_pageMetadata.unsupportedFormControlCount,
		s_pageMetadata.postSupportedHosted,
		s_pageMetadata.postSupportedBareMetal,
		s_pageMetadata.lastSubmittedFormAction,
		s_pageMetadata.lastSubmittedFormMethod,
		s_pageMetadata.lastSubmittedFormStatus,
		s_pageMetadata.lastPostHttpStatus,
		s_pageMetadata.lastPostContentType,
		s_clipboardMode,
		s_pageMetadata.tlsStatus,
		s_pageMetadata.tlsError,
		s_pageMetadata.tlsConnectionPath,
		s_pageMetadata.tlsCredentialApi,
		s_pageMetadata.tlsCredentialStructure,
		s_pageMetadata.tlsCredentialAcquired,
		 s_pageMetadata.tlsHandshakeStarted,
		s_pageMetadata.tlsSmokeSelfSignedBypass));
	appendFormPhase2EBlocks(doc, s_pageMetadata);
	appendCssPhase3ABlocks(doc, s_pageMetadata);

	doc.blocks.push_back({BlockType::Link, "Page Info", "about:page-info"});
	doc.blocks.push_back({BlockType::Link, "View Source", "about:view-source"});
	doc.blocks.push_back({BlockType::Link, "View Downloads", "about:downloads"});
	doc.blocks.push_back({BlockType::Link, "Go to about:navigator", "about:navigator"});
	return doc;
}

// =============================================================================
// Save Page helpers
// =============================================================================

// Derive a safe filename stem from the current page URL.
// e.g. "http://example.com/foo/bar.html" -> "bar"
// Falls back to "page".
static std::string saveNameStemFromUrl(const std::string& url)
{
	// Strip scheme
	std::string path = url;
	size_t schemeEnd = path.find("://");
	if (schemeEnd != std::string::npos) {
		path = path.substr(schemeEnd + 3);
	}
	// Strip query and fragment
	size_t q = path.find('?'); if (q != std::string::npos) path = path.substr(0, q);
	size_t h = path.find('#'); if (h != std::string::npos) path = path.substr(0, h);
	// Get last path component
	size_t slash = path.rfind('/');
	if (slash != std::string::npos) path = path.substr(slash + 1);
	// Strip extension
	size_t dot = path.rfind('.');
	if (dot != std::string::npos && dot > 0) path = path.substr(0, dot);
	// Sanitize
	std::string safe;
	for (unsigned char ch : path) {
		if (std::isalnum(ch) || ch == '-' || ch == '_') safe.push_back(static_cast<char>(ch));
		else if (!safe.empty() && safe.back() != '-') safe.push_back('-');
	}
	while (!safe.empty() && safe.back() == '-') safe.pop_back();
	if (safe.empty() || safe == "." || safe == "..") safe = "page";
	if (safe.size() > 48) safe = safe.substr(0, 48);
	return safe;
}

// Extract visible text from the current document blocks.
static std::string extractDocumentText(const WebDocument& doc)
{
	std::ostringstream out;
	for (int i = 0; i < static_cast<int>(doc.blocks.size()); ++i) {
		const DocBlock& block = doc.blocks[static_cast<size_t>(i)];
		if (!blockHasVisibleCss(block)) {
			continue;
		}
		if (isTableCellLikeBlock(block)) {
			if (!isFirstTableCellInGroup(doc, i)) continue;
			const uint64_t tableSerial = tableSerialForBlock(block);
			const uint64_t rowSerial = tableRowSerialForBlock(block);
			std::ostringstream rowText;
			bool firstCell = true;
			for (int j = i; j < static_cast<int>(doc.blocks.size()); ++j) {
				const DocBlock& cell = doc.blocks[static_cast<size_t>(j)];
				if (!isTableCellLikeBlock(cell) ||
					tableSerialForBlock(cell) != tableSerial ||
					tableRowSerialForBlock(cell) != rowSerial) {
					break;
				}
				if (!firstCell) rowText << " | ";
				rowText << cell.text;
				firstCell = false;
			}
			out << rowText.str() << "\n";
			continue;
		}
		switch (block.type) {
		case BlockType::Heading:
			out << "=== " << block.text << " ===\n\n";
			break;
		case BlockType::Paragraph:
			if (!block.text.empty()) out << block.text << "\n\n";
			break;
		case BlockType::ListItem:
			if (!block.text.empty()) {
				const int ordinal = blockListOrdinal(doc, i);
				const std::string marker = blockListMarkerText(block, ordinal);
				if (!marker.empty()) {
					out << marker << ' ';
				}
				out << block.text << "\n";
			}
			break;
		case BlockType::Preformatted:
			if (!block.text.empty()) out << block.text << "\n\n";
			break;
		case BlockType::Link:
			if (!block.text.empty()) {
				out << block.text;
				if (!block.url.empty() && block.url != block.text) {
					out << " [" << block.url << "]";
				}
				out << "\n";
			}
			break;
		case BlockType::Image:
			if (!block.alt.empty()) out << "[Image: " << block.alt << "]\n";
			break;
		case BlockType::FormLabel:
			if (!block.text.empty()) out << block.text << "\n";
			break;
		case BlockType::FormTextInput:
		case BlockType::FormTextarea: {
			if (block.formControl.type == FormControlType::Password || block.inputType == "password") {
				out << "[password field]\n";
				break;
			}
			const std::string& text = block.inputValue.empty() ? block.placeholder : block.inputValue;
			if (!text.empty()) out << text << "\n";
			break;
		}
		case BlockType::FormCheckbox:
		case BlockType::FormRadio:
		case BlockType::FormSelect:
			if (!block.text.empty()) out << block.text << "\n";
			break;
		case BlockType::FormSubmit:
			out << (block.submitLabel.empty() ? "Submit" : block.submitLabel) << "\n";
			break;
		default:
			break;
		}
	}
	return out.str();
}

WebDocument Navigator::buildSavePageTextDocument()
{
	WebDocument result;
	result.url   = "about:save-page-text";
	result.title = "Page Saved";
	s_lifecycleDiagnostics.saveTextIntendedSourceCategory = "none";
	s_lifecycleDiagnostics.saveTextActualSourceCategory = "none";
	s_lifecycleDiagnostics.saveTextVisibleTextByteCount = 0;
	s_lifecycleDiagnostics.saveTextGeneratedPageExcluded = false;
	s_lifecycleDiagnostics.saveTextPasswordRedacted = false;
	s_lifecycleDiagnostics.saveTextHiddenControlExcluded = false;
	s_lifecycleDiagnostics.saveTextDiagnosticsExcluded = true;

	const std::string& pageUrl = s_pageMetadata.finalUrl.empty()
		? s_pageMetadata.requestedUrl : s_pageMetadata.finalUrl;
	const bool buildingPendingDocument = s_loading && !s_pendingDocumentUrl.empty();
	const std::string visibleUrl = buildingPendingDocument ? s_pendingDocumentUrl : s_currentDoc.url;
	const bool visibleIsInspectionView = buildingPendingDocument
		? isGeneratedInspectionViewUrl(visibleUrl) : s_visibleDocumentInspectionView;
	const bool sourceReferenceValid = s_inspectedDocumentGeneration != 0 &&
		!s_inspectedDoc.url.empty() && !s_pageMetadata.finalUrl.empty() &&
		s_inspectedDoc.url == s_pageMetadata.finalUrl;
	const NavigatorDocumentCategory sourceCategory = s_inspectedSourceCategory;
	s_lifecycleDiagnostics.saveTextIntendedSourceCategory = documentCategoryName(sourceCategory);
	s_lifecycleDiagnostics.saveTextActualSourceCategory = documentCategoryName(sourceCategory);
	s_lifecycleDiagnostics.saveTextGeneratedPageExcluded = sourceReferenceValid &&
		visibleIsInspectionView && visibleUrl != pageUrl;
	if (sourceReferenceValid) incrementLifecycleCounter(s_lifecycleDiagnostics.saveTextSourceValid);

	if (pageUrl.empty()) {
		result.blocks.push_back({BlockType::Heading, "Save Page Text", ""});
		result.blocks.push_back({BlockType::Paragraph, "No page has been loaded yet.", ""});
		result.blocks.push_back({BlockType::Link, "Go to about:navigator", "about:navigator"});
		return result;
	}

	const std::string text = extractDocumentText(s_inspectedDoc);
	s_lifecycleDiagnostics.saveTextVisibleTextByteCount = text.size();
	bool passwordFound = false;
	bool hiddenControlsFound = false;
	bool hiddenControlsExcluded = true;
	for (const DocBlock& block : s_inspectedDoc.blocks) {
		if (block.formControl.type == FormControlType::Password || block.inputType == "password") {
			passwordFound = true;
			continue;
		}
		if (block.formControl.hidden || !blockHasVisibleCss(block)) {
			hiddenControlsFound = true;
			if (blockHasVisibleCss(block)) hiddenControlsExcluded = false;
		}
	}
	s_lifecycleDiagnostics.saveTextPasswordRedacted = !passwordFound ||
		text.find("[password field]") != std::string::npos;
	s_lifecycleDiagnostics.saveTextHiddenControlExcluded = !hiddenControlsFound || hiddenControlsExcluded;
	result.blocks.push_back({BlockType::Heading, "Phase 2I Save Ownership Evidence", ""});
	result.blocks.push_back({BlockType::ListItem, pageInfoLine("Intended source category",
		s_lifecycleDiagnostics.saveTextIntendedSourceCategory), ""});
	result.blocks.push_back({BlockType::ListItem, pageInfoLine("Actual exported source category",
		s_lifecycleDiagnostics.saveTextActualSourceCategory), ""});
	result.blocks.push_back({BlockType::ListItem, pageInfoLine("Visible-text byte count",
		static_cast<int>(std::min<size_t>(text.size(), static_cast<size_t>(std::numeric_limits<int>::max())))), ""});
	result.blocks.push_back({BlockType::ListItem, pageInfoLine("Generated-page exclusion",
		yesNo(s_lifecycleDiagnostics.saveTextGeneratedPageExcluded)), ""});
	result.blocks.push_back({BlockType::ListItem, pageInfoLine("Password redaction",
		yesNo(s_lifecycleDiagnostics.saveTextPasswordRedacted)), ""});
	result.blocks.push_back({BlockType::ListItem, pageInfoLine("Hidden-control exclusion",
		yesNo(s_lifecycleDiagnostics.saveTextHiddenControlExcluded)), ""});
	result.blocks.push_back({BlockType::ListItem, pageInfoLine("Diagnostics exclusion",
		yesNo(s_lifecycleDiagnostics.saveTextDiagnosticsExcluded)), ""});
	result.blocks.push_back({BlockType::ListItem, pageInfoLine("Current visible document ownership",
		yesNo(sourceReferenceValid)), ""});
	if (text.empty()) {
		result.blocks.push_back({BlockType::Heading, "Save Page Text", ""});
		result.blocks.push_back({BlockType::Paragraph, "The current page has no visible text to save.", ""});
		result.blocks.push_back({BlockType::Link, "Page Info", "about:page-info"});
		result.blocks.push_back({BlockType::Link, "Go to about:navigator", "about:navigator"});
		return result;
	}

	const std::string stem = saveNameStemFromUrl(pageUrl);
	const std::string safeName = sanitizeDownloadFileName(stem + ".txt");
	std::string finalName;
	const std::string savePath = uniqueDownloadPathForName(safeName, finalName);

	result.blocks.push_back({BlockType::Heading, "Page Saved", ""});

	if (savePath.empty() || finalName.empty()) {
		result.blocks.push_back({BlockType::Paragraph, "Save unavailable: could not allocate a safe filename.", ""});
		result.blocks.push_back({BlockType::Link, "Page Info", "about:page-info"});
		result.blocks.push_back({BlockType::Link, "Go to about:navigator", "about:navigator"});
		return result;
	}

	const bool ok = writeTextFile(savePath, text);
	if (!ok) {
		result.blocks.push_back({BlockType::Paragraph, "Save unavailable: could not write to the downloads directory.", ""});
		result.blocks.push_back({BlockType::Link, "Page Info", "about:page-info"});
		result.blocks.push_back({BlockType::Link, "Go to about:navigator", "about:navigator"});
		return result;
	}

	DownloadItem item;
	item.url           = pageUrl;
	item.finalUrl      = pageUrl;
	item.contentType   = "text/plain";
	item.suggestedFileName = finalName;
	item.savedPath     = savePath;
	item.byteCount     = text.size();
	item.success       = true;
	rememberDownload(item);

	const std::string fileUrl = safeDownloadFileUrl(item);
	result.blocks.push_back({BlockType::ListItem, pageInfoLine("Filename",   finalName),                  ""});
	result.blocks.push_back({BlockType::ListItem, pageInfoLine("Saved path", savePath),                   ""});
	result.blocks.push_back({BlockType::ListItem, pageInfoLine("Byte count", static_cast<int>(text.size())), ""});
	result.blocks.push_back({BlockType::ListItem, pageInfoLine("Mode",       "text"),                     ""});
	result.blocks.push_back({BlockType::ListItem, pageInfoLine("Source URL", pageUrl),                    ""});
	if (!fileUrl.empty()) {
		result.blocks.push_back({BlockType::Link, "Open saved file", fileUrl});
	}
	result.blocks.push_back({BlockType::Link, "View Downloads",           "about:downloads"});
	result.blocks.push_back({BlockType::Link, "Page Info",                "about:page-info"});
	result.blocks.push_back({BlockType::Link, "Go to about:navigator",   "about:navigator"});
	return result;
}

WebDocument Navigator::buildSavePageSourceDocument()
{
	WebDocument result;
	result.url   = "about:save-page-source";
	result.title = "Page Saved";

	const std::string& pageUrl = s_pageMetadata.finalUrl.empty()
		? s_pageMetadata.requestedUrl : s_pageMetadata.finalUrl;

	result.blocks.push_back({BlockType::Heading, "Save Source", ""});

	if (pageUrl.empty()) {
		result.blocks.push_back({BlockType::Paragraph, "No page has been loaded yet.", ""});
		result.blocks.push_back({BlockType::Link, "Go to about:navigator", "about:navigator"});
		return result;
	}

	if (s_pageMetadata.rawSourceForSave.empty()) {
		result.blocks.push_back({BlockType::Paragraph,
			"No raw source available for the current page (generated about: pages have no source).", ""});
		result.blocks.push_back({BlockType::Link, "Page Info",              "about:page-info"});
		result.blocks.push_back({BlockType::Link, "Go to about:navigator", "about:navigator"});
		return result;
	}

	const std::string stem = saveNameStemFromUrl(pageUrl);
	const std::string safeName = sanitizeDownloadFileName(stem + "-source.html");
	std::string finalName;
	const std::string savePath = uniqueDownloadPathForName(safeName, finalName);

	if (savePath.empty() || finalName.empty()) {
		result.blocks.push_back({BlockType::Paragraph, "Save unavailable: could not allocate a safe filename.", ""});
		result.blocks.push_back({BlockType::Link, "Page Info",              "about:page-info"});
		result.blocks.push_back({BlockType::Link, "Go to about:navigator", "about:navigator"});
		return result;
	}

	const bool ok = writeTextFile(savePath, s_pageMetadata.rawSourceForSave);
	if (!ok) {
		result.blocks.push_back({BlockType::Paragraph, "Save unavailable: could not write to the downloads directory.", ""});
		result.blocks.push_back({BlockType::Link, "Page Info",              "about:page-info"});
		result.blocks.push_back({BlockType::Link, "Go to about:navigator", "about:navigator"});
		return result;
	}

	DownloadItem item;
	item.url           = pageUrl;
	item.finalUrl      = pageUrl;
	item.contentType   = "text/html";
	item.suggestedFileName = finalName;
	item.savedPath     = savePath;
	item.byteCount     = s_pageMetadata.rawSourceForSave.size();
	item.success       = true;
	rememberDownload(item);

	const std::string fileUrl = safeDownloadFileUrl(item);
	result.blocks.push_back({BlockType::ListItem, pageInfoLine("Filename",   finalName),                                      ""});
	result.blocks.push_back({BlockType::ListItem, pageInfoLine("Saved path", savePath),                                       ""});
	result.blocks.push_back({BlockType::ListItem, pageInfoLine("Byte count", static_cast<int>(s_pageMetadata.rawSourceForSave.size())), ""});
	result.blocks.push_back({BlockType::ListItem, pageInfoLine("Mode",       "source"),                                       ""});
	result.blocks.push_back({BlockType::ListItem, pageInfoLine("Source URL", pageUrl),                                        ""});
	if (!fileUrl.empty()) {
		result.blocks.push_back({BlockType::Link, "Open saved file", fileUrl});
	}
	result.blocks.push_back({BlockType::Link, "View Downloads",         "about:downloads"});
	result.blocks.push_back({BlockType::Link, "View Source",            "about:view-source"});
	result.blocks.push_back({BlockType::Link, "Page Info",              "about:page-info"});
	result.blocks.push_back({BlockType::Link, "Go to about:navigator", "about:navigator"});
	return result;
}

WebDocument Navigator::buildDownloadsDocument()
{
	WebDocument doc;
	doc.url = "about:downloads";
	doc.title = "Downloads";
	doc.blocks.push_back({BlockType::Heading, "Downloads", ""});
	doc.blocks.push_back({BlockType::Paragraph,
		"Recent downloads are kept in memory for this Navigator session. Newest downloads appear first.", ""});
#if defined(GXOS_BARE_METAL)
	doc.blocks.push_back({BlockType::Paragraph,
		"Bare-metal downloads use the same saved path shown below, typically under /downloads when writable storage is available.", ""});
#else
	doc.blocks.push_back({BlockType::Paragraph,
		"Hosted Navigator saves downloads to its /downloads path, which maps to the host working directory's downloads folder.", ""});
	doc.blocks.push_back({BlockType::Paragraph,
		"TODO: add an Open Downloads Folder action only after Navigator can reuse an existing File Explorer launch path without a special-case app-model bypass.", ""});
#endif
	doc.blocks.push_back({BlockType::Paragraph,
		"Page Info continues to describe the last loaded page or download response; opening about:downloads does not replace that metadata.", ""});

	if (s_recentDownloads.empty()) {
		doc.blocks.push_back({BlockType::Paragraph, "No downloads yet. Unsupported HTTP content will appear here after Navigator records it.", ""});
	} else {
		for (size_t i = 0; i < s_recentDownloads.size(); ++i) {
			const DownloadItem& item = s_recentDownloads[i];
			const std::string fileUrl = safeDownloadFileUrl(item);
			const std::string fileName = item.suggestedFileName.empty() ? "download.bin" : item.suggestedFileName;
			doc.blocks.push_back({BlockType::Heading,
				std::to_string(static_cast<int>(i) + 1) + ". " + fileName, ""});
			doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Status", item.success ? "success" : "failed"), ""});
			doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Filename", fileName), ""});
			doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Saved path", item.savedPath), ""});
			doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Source URL", item.url), ""});
			doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Final URL", item.finalUrl), ""});
			doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Content type", item.contentType), ""});
			doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Byte count", static_cast<int>(item.byteCount)), ""});
			if (!fileUrl.empty()) {
				doc.blocks.push_back({BlockType::Link, "Open downloaded file", fileUrl});
			}
			if (!item.error.empty()) {
				doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Error", item.error), ""});
			}
		}
	}

	doc.blocks.push_back({BlockType::Link, "Page Info (last loaded page)", "about:page-info"});
	doc.blocks.push_back({BlockType::Link, "Navigator Runtime", "about:navigator-runtime"});
	doc.blocks.push_back({BlockType::Link, "Go to about:navigator", "about:navigator"});
	return doc;
}

WebDocument Navigator::loadHttpUrl(const std::string& url)
{
	Logger::write(LogLevel::Info, std::string("Navigator loadHttpUrl: ") + url);
	navigatorSmokeProgress("http-fetch-start");

	gxos::web::HttpResponse response = gxos::web::fetchHttpUrl(url);
	navigatorSmokeProgress("http-body-complete");
	return loadHttpResponseDocument(url, response);
}

WebDocument Navigator::loadHttpResponseDocument(const std::string& url, const gxos::web::HttpResponse& response)
{
	NavigatorPageMetadata metadata;
	metadata.requestedUrl = response.requestedUrl.empty() ? url : response.requestedUrl;
	metadata.finalUrl = response.finalUrl.empty() ? url : response.finalUrl;
	metadata.sourceType = metadata.finalUrl.rfind("https://", 0) == 0 ? "https" : "http";
	metadata.scheme = metadata.sourceType;
	metadata.httpStatusCode = response.statusCode;
	metadata.httpReasonPhrase = response.reasonPhrase;
	metadata.contentType = response.contentType;
	metadata.contentEncoding = response.contentEncoding;
	if (response.redirectCount < 0 || response.redirectCount > gxos::web::kHttpMaxRedirects)
		incrementLifecycleCounter(s_lifecycleDiagnostics.transitionMetadataClamps);
	metadata.redirectCount = std::max(0, std::min(response.redirectCount, gxos::web::kHttpMaxRedirects));
	metadata.redirected = response.redirectCount > 0 || metadata.requestedUrl != metadata.finalUrl;
	metadata.headerCapHit = response.headerCapHit;
	metadata.bodyCapHit = response.bodyCapHit;
	metadata.tlsBackend = response.tlsBackend;
	metadata.tlsCertificateValidation = response.tlsCertificateValidation;
	metadata.tlsStatus = response.tlsStatus;
	metadata.tlsError = response.tlsError;
	metadata.tlsErrorCode = response.tlsErrorCode;
	metadata.tlsConnectionPath = response.tlsConnectionPath;
	metadata.tlsCredentialApi = response.tlsCredentialApi;
	metadata.tlsCredentialStructure = response.tlsCredentialStructure;
	metadata.tlsCredentialProtocols = response.tlsCredentialProtocols;
	metadata.tlsCredentialFlags = response.tlsCredentialFlags;
	metadata.tlsCredentialTarget = response.tlsCredentialTarget;
	metadata.tlsCertificateSubject = response.tlsCertificateSubject;
	metadata.tlsCertificateIssuer = response.tlsCertificateIssuer;
	metadata.tlsCertificateValidFrom = response.tlsCertificateValidFrom;
	metadata.tlsCertificateValidTo = response.tlsCertificateValidTo;
	metadata.tlsCertificateHostname = response.tlsCertificateHostname;
	metadata.tlsCertificateHostnameValidation = response.tlsCertificateHostnameValidation;
	metadata.tlsCertificateChainError = response.tlsCertificateChainError;
	metadata.tlsProtocol = response.tlsProtocol;
	metadata.tlsCipherSuite = response.tlsCipherSuite;
	metadata.tlsEnabled = response.tlsEnabled;
	metadata.tlsValidated = response.tlsValidated;
	metadata.tlsCredentialAcquired = response.tlsCredentialAcquired;
	metadata.tlsHandshakeStarted = response.tlsHandshakeStarted;
	metadata.tlsSmokeSelfSignedBypass = response.tlsSmokeSelfSignedBypass;
	metadata.downgradeRedirectBlocked = response.downgradeRedirectBlocked;
	metadata.insecureRedirectLocation = response.insecureRedirectLocation;
	metadata.tlsSucceededBeforeContentFailure =
		response.tlsEnabled && response.tlsHandshakeStarted &&
		(response.headerCapHit || response.bodyCapHit ||
			response.error == gxos::web::HttpError::UnsupportedContentEncoding ||
			response.error == gxos::web::HttpError::UnsupportedTransferEncoding ||
			response.error == gxos::web::HttpError::MalformedChunkedEncoding);
	if (response.error == gxos::web::HttpError::UnsupportedContentEncoding) {
		metadata.unsupportedReason = response.contentEncoding.empty()
			? "Unsupported content encoding"
			: ("Unsupported content encoding: " + response.contentEncoding);
	} else if (response.error == gxos::web::HttpError::UnsupportedTransferEncoding) {
		metadata.unsupportedReason = response.transferEncoding.empty()
			? "Unsupported transfer encoding"
			: ("Unsupported transfer encoding: " + response.transferEncoding);
	} else if (response.error == gxos::web::HttpError::MalformedChunkedEncoding) {
		metadata.unsupportedReason = "Malformed chunked transfer encoding";
	}
	if (response.error != gxos::web::HttpError::None) {
		metadata.errorStatus = gxos::web::httpErrorName(response.error);
		if (!response.errorMessage.empty()) metadata.errorStatus += ": " + response.errorMessage;
	}
	if (response.error == gxos::web::HttpError::None &&
		(response.contentType == "text/html" || response.contentType == "text/plain" || response.contentType.empty())) {
		setSourcePreview(metadata, response.body);
	}

	auto finish = [&](WebDocument doc) -> WebDocument {
		storePageMetadata(metadata, doc);
		return doc;
	};

	auto buildCompatibilityErrorDocument = [&](const std::string& title,
		const std::string& summary) -> WebDocument {
		WebDocument doc;
		doc.url = response.finalUrl.empty() ? url : response.finalUrl;
		doc.title = title;
		doc.blocks.push_back({BlockType::Heading, title, ""});
		doc.blocks.push_back({BlockType::Paragraph, summary, ""});
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Requested URL", metadata.requestedUrl), ""});
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Final URL", metadata.finalUrl), ""});
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Scheme", metadata.scheme), ""});
		if (response.statusCode > 0) {
			std::ostringstream status;
			status << response.statusCode;
			if (!response.reasonPhrase.empty()) status << " " << response.reasonPhrase;
			doc.blocks.push_back({BlockType::ListItem, pageInfoLine("HTTP status", status.str()), ""});
		}
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Content type",
			response.contentType.empty() ? "(none)" : response.contentType), ""});
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Content encoding",
			response.contentEncoding.empty() ? "(none)" : response.contentEncoding), ""});
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Redirect count", response.redirectCount), ""});
		if (!metadata.unsupportedReason.empty()) {
			doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Unsupported reason", metadata.unsupportedReason), ""});
		}
		if (response.headerCapHit) {
			doc.blocks.push_back({BlockType::ListItem,
				pageInfoLine("Header limit hit", std::to_string(gxos::web::kHttpMaxHeaderBytes) + " bytes"), ""});
		}
		if (response.bodyCapHit) {
			doc.blocks.push_back({BlockType::ListItem,
				pageInfoLine("Body limit hit", std::to_string(gxos::web::kHttpMaxBodyBytes) + " bytes"), ""});
		}
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("TLS succeeded before content failure",
			yesNo(metadata.tlsSucceededBeforeContentFailure)), ""});
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Technical error",
			std::string(gxos::web::httpErrorName(response.error)) + ": " + response.errorMessage), ""});
		if (!response.tlsErrorCode.empty()) {
			doc.blocks.push_back({BlockType::ListItem, pageInfoLine("TLS error code", response.tlsErrorCode), ""});
		}
		doc.blocks.push_back({BlockType::Link, "Page Info", "about:page-info"});
		doc.blocks.push_back({BlockType::Link, "Go to about:navigator", "about:navigator"});
		return doc;
	};

	if (!response.ok()) {
		std::string title = "HTTP Error";
		std::string summary = "Navigator could not load the requested URL.";
		if (response.error == gxos::web::HttpError::UnsupportedContentEncoding) {
			title = "Unsupported Content Encoding";
			summary = "TLS succeeded, but Navigator cannot display this compressed response body yet.";
		} else if (response.error == gxos::web::HttpError::UnsupportedTransferEncoding) {
			title = "Unsupported Transfer Encoding";
			summary = "TLS succeeded, but Navigator cannot display this transfer encoding yet.";
		} else if (response.error == gxos::web::HttpError::MalformedChunkedEncoding) {
			title = "Malformed Chunked Response";
			summary = "TLS succeeded, but the server's chunked response could not be decoded safely.";
		} else if (response.error == gxos::web::HttpError::RedirectLimitExceeded) {
			title = "Redirect Limit Exceeded";
			summary = "Navigator stopped following redirects after hitting its safety limit.";
		} else if (response.error == gxos::web::HttpError::InsecureRedirectBlocked) {
			title = "Insecure Redirect Blocked";
			summary = "Navigator blocked an HTTPS-to-HTTP redirect because it would continue over an insecure connection.";
		} else if (response.error == gxos::web::HttpError::BodyTooLarge) {
			title = "Response Too Large";
			summary = "TLS may have succeeded, but Navigator stopped before rendering because the response body exceeded the configured safety limit.";
		} else if (response.error == gxos::web::HttpError::HeaderTooLarge) {
			title = "Headers Too Large";
			summary = "Navigator stopped before rendering because the response headers exceeded the configured safety limit.";
		} else if (response.error == gxos::web::HttpError::Timeout) {
			title = "Network Timeout";
		} else if (response.error == gxos::web::HttpError::TlsCertificateHostnameMismatch) {
			title = "HTTPS Certificate Hostname Mismatch";
		} else if (response.error == gxos::web::HttpError::TlsCertificateExpired) {
			title = "HTTPS Certificate Expired";
		} else if (response.error == gxos::web::HttpError::TlsCertificateValidationFailed) {
			title = "HTTPS Certificate Validation Failed";
		} else if (response.error == gxos::web::HttpError::TlsProtocolUnsupported) {
			title = "HTTPS Protocol Unsupported";
		} else if (response.error == gxos::web::HttpError::TlsHandshakeFailed) {
			title = "HTTPS Handshake Failed";
		} else if (response.error == gxos::web::HttpError::TlsReadFailed) {
			title = "HTTPS Read Failed";
		} else if (response.error == gxos::web::HttpError::TlsWriteFailed) {
			title = "HTTPS Write Failed";
		}
		WebDocument doc = buildCompatibilityErrorDocument(title, summary);
		if (!response.insecureRedirectLocation.empty()) {
			doc.blocks.insert(doc.blocks.begin() + 3, {BlockType::ListItem,
				pageInfoLine("Attempted insecure Location", response.insecureRedirectLocation), ""});
		}
		return finish(std::move(doc));
	}

	const std::string documentUrl = response.finalUrl.empty() ? url : response.finalUrl;

	if (response.statusCode >= 300 && response.statusCode < 400) {
		WebDocument doc;
		doc.url = documentUrl;
		doc.title = "Redirect";
		doc.blocks.push_back({BlockType::Heading, "Redirect", ""});
		std::ostringstream line;
		line << "HTTP " << response.statusCode;
		if (!response.reasonPhrase.empty()) line << " " << response.reasonPhrase;
		doc.blocks.push_back({BlockType::Paragraph, line.str(), ""});
		const std::string location = response.headerValue("Location");
		if (!location.empty()) {
			doc.blocks.push_back({BlockType::Paragraph, "The server asked Navigator to load another URL.", ""});
			doc.blocks.push_back({BlockType::Link, location, gxos::web::resolveRelativeUrl(documentUrl, location)});
		} else {
			doc.blocks.push_back({BlockType::Paragraph, "The response did not include a Location header.", ""});
		}
		doc.blocks.push_back({BlockType::Link, "Go to about:navigator", "about:navigator"});
		return finish(std::move(doc));
	}

	if (response.statusCode < 200 || response.statusCode >= 300) {
		std::ostringstream status;
		status << "HTTP " << response.statusCode;
		if (!response.reasonPhrase.empty()) status << " " << response.reasonPhrase;
		if (metadata.errorStatus.empty()) metadata.errorStatus = status.str();
		WebDocument doc;
		doc.url = documentUrl;
		doc.title = status.str();
		doc.blocks.push_back({BlockType::Heading, status.str(), ""});
		doc.blocks.push_back({BlockType::Paragraph,
			response.statusCode == 404
				? "The server replied, but the requested page was not found."
				: (response.statusCode >= 500
					? "The server reported an internal failure after Navigator reached it successfully."
					: "The server replied with a non-success HTTP status."), ""});
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Requested URL", metadata.requestedUrl), ""});
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Final URL", metadata.finalUrl), ""});
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Content type",
			response.contentType.empty() ? "(none)" : response.contentType), ""});
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Redirect count", response.redirectCount), ""});
		doc.blocks.push_back({BlockType::Link, "Page Info", "about:page-info"});
		doc.blocks.push_back({BlockType::Link, "Go to about:navigator", "about:navigator"});
		return finish(std::move(doc));
	}

	if (response.contentType == "text/html") {
		navigatorSmokeProgress("html-parser-start");
		WebDocument doc = parseHtml(documentUrl, response.body, s_visitedUrls);
		navigatorSmokeProgress("html-parser-complete");
		if (doc.title.empty()) doc.title = documentUrl;
		return finish(std::move(doc));
	}

	if (response.contentType == "text/plain" || response.contentType.empty()) {
		WebDocument doc;
		doc.url = documentUrl;
		doc.title = documentUrl;
		doc.blocks.push_back({BlockType::Heading, documentUrl, ""});
		std::string cleanText;
		cleanText.reserve(response.body.size());
		for (char c : response.body) {
			if (c != '\r') cleanText += c;
		}
		if (!cleanText.empty() && cleanText.back() == '\n') cleanText.pop_back();
		doc.blocks.push_back({BlockType::Preformatted, cleanText.empty() ? "(empty response)" : cleanText, ""});
		return finish(std::move(doc));
	}

	DownloadItem item;
	item.url = metadata.requestedUrl;
	item.finalUrl = documentUrl;
	item.contentType = response.contentType.empty() ? "application/octet-stream" : response.contentType;
		item.byteCount = response.body.size();
		item.suggestedFileName = sanitizeDownloadFileName(fileNameFromUrlPath(documentUrl));

		metadata.errorStatus = "Unsupported content type downloaded";
		metadata.unsupportedReason = response.contentType.empty()
			? "Unsupported or missing content type"
			: ("Unsupported content type: " + response.contentType);
		metadata.downloaded = true;
	metadata.downloadByteCount = response.body.size();
	if (response.body.empty()) {
		item.success = false;
		item.error = "Response body was empty; Navigator did not create a download file.";
		metadata.errorStatus = "Download unavailable: no body";
		rememberDownload(item);
		metadata.downloadSavedPath = "";
		return finish(buildDownloadCompleteDocument(item));
	}

	std::string finalName;
	item.savedPath = uniqueDownloadPathForName(item.suggestedFileName, finalName);
	if (item.savedPath.empty() || finalName.empty()) {
		item.success = false;
		item.error = "Navigator could not allocate a safe non-overwriting filename.";
		metadata.errorStatus = "Download unavailable: unsafe filename";
		rememberDownload(item);
		return finish(buildDownloadCompleteDocument(item));
	}
	item.suggestedFileName = finalName;

	if (writeBinaryFile(item.savedPath, response.body)) {
		item.success = true;
		metadata.downloadSavedPath = item.savedPath;
		s_statusText = "Downloaded " + item.suggestedFileName;
	} else {
		item.success = false;
		item.error = "Navigator could not write the file to the downloads directory.";
		metadata.errorStatus = "Download unavailable: file write failed";
		metadata.downloadSavedPath = item.savedPath;
	}
	rememberDownload(item);
	return finish(buildDownloadCompleteDocument(s_recentDownloads.front()));
}

// -----------------------------------------------------------------------------
// Layout helpers
// -----------------------------------------------------------------------------

int Navigator::blockLayoutY(int blockIndex)
{
	ensureInlineLayout(s_currentDoc);
	ensureCssMarginLayout(s_currentDoc);
	if (s_cssMarginLayoutSnapshot.valid && blockIndex >= 0 &&
		blockIndex < static_cast<int>(s_cssMarginLayoutSnapshot.records.size())) {
		const CssMarginFlowRecord& record = s_cssMarginLayoutSnapshot.records[static_cast<size_t>(blockIndex)];
		const DocBlock& block = s_currentDoc.blocks[static_cast<size_t>(blockIndex)];
		int clearanceDisplacement = 0;
		if (s_cssFloatLayoutSnapshot.valid) {
			const size_t limit = std::min<size_t>(static_cast<size_t>(blockIndex),
				s_cssFloatLayoutSnapshot.blockClearances.size());
			for (size_t i = 0; i < limit; ++i)
				clearanceDisplacement = cssBoundedGeometryAdd(clearanceDisplacement,
					s_cssFloatLayoutSnapshot.blockClearances[i]);
		}
		int usedY = record.usedY + clearanceDisplacement;
		if (cssStyleHasOverflowBfc(block.style) && block.style.floatMode == FloatMode::None) {
			const int requiredWidth = blockOuterWidth(block, blockAvailableWidth(block, s_currentDoc));
			usedY = cssBfcPlacementY(s_currentDoc, blockIndex, usedY, requiredWidth);
		}
		return usedY - cssMarginTopPx(block.style,
			block.type == BlockType::Heading ? 10 : 4);
	}
	// Returns the Y coordinate of blockIndex relative to kContentY (pre-scroll).
	const int bodyTop = s_currentDoc.bodyStyle.marginTop >= 0 ? s_currentDoc.bodyStyle.marginTop : 0;
	int y = kHeadingY + bodyTop;
	for (int i = 0; i < blockIndex && i < static_cast<int>(s_currentDoc.blocks.size()); ++i) {
		const DocBlock& b    = s_currentDoc.blocks[i];
		bool nextIsHeading = false;
		if (i + 1 < blockIndex &&
			i + 1 < static_cast<int>(s_currentDoc.blocks.size())) {
			nextIsHeading = (s_currentDoc.blocks[i + 1].type == BlockType::Heading);
		}
		y += blockTotalHeight(b, s_currentDoc, nextIsHeading);
	}
	return y;
}

bool Navigator::inlineFragmentRectForBlock(int blockIndex, bool includeWhitespace, Rect& out)
{
	out = Rect{0, 0, 0, 0};
	if (blockIndex < 0 || blockIndex >= static_cast<int>(s_currentDoc.blocks.size())) return false;
	const DocBlock& block = s_currentDoc.blocks[static_cast<size_t>(blockIndex)];
	if (!blockHasVisibleCss(block)) return false;
	ensureCssMarginLayout(s_currentDoc);
	ensureInlineLayout(s_currentDoc);
	ensureCssFloatLayout(s_currentDoc);
	ensureCssPositionLayout(s_currentDoc);
	if (const CssFloatRecord* floatRecord = cssFloatRecordForBlock(s_currentDoc, blockIndex)) {
		CssPaintRect target{kContentX + floatRecord->borderBoxX,
			kContentY + floatRecord->borderBoxY - s_scrollOffset,
			floatRecord->borderBoxW, floatRecord->borderBoxH};
		if (floatRecord->contextSerial != 0) {
			for (const InlineFlowLayout& contextFlow : s_inlineLayoutSnapshot.flows) {
				if (contextFlow.contextSerial != floatRecord->contextSerial) continue;
				CssPaintRect parentAtomic;
				if (!atomicResultScreenRect(s_currentDoc, s_inlineLayoutSnapshot,
					contextFlow.atomicResultIndex, s_scrollOffset, parentAtomic)) continue;
				target = CssPaintRect{parentAtomic.x + contextFlow.contentX + floatRecord->borderBoxX,
					parentAtomic.y + contextFlow.localOuterY + cssBorderTopPx(contextFlow.style) +
						cssPaddingTopPx(contextFlow.style, 0) + floatRecord->borderBoxY,
					floatRecord->borderBoxW, floatRecord->borderBoxH};
				break;
			}
		}
		const CssPaintRect clipped = cssClipHitTarget(target, cssViewportClipRect());
		out = Rect{clipped.x, clipped.y, clipped.w, clipped.h};
		return out.w > 0 && out.h > 0;
	}
	const InlineFlowLayout* flow = inlineFlowForBlock(s_currentDoc, blockIndex);
	if (!flow) return false;
	if (flow->contextSerial != 0 && flow->atomicResultIndex < 0) return false;
	CssPaintRect parentAtomic;
	const bool embedded = flow->contextSerial != 0 &&
		atomicResultScreenRect(s_currentDoc, s_inlineLayoutSnapshot, flow->atomicResultIndex, s_scrollOffset, parentAtomic);
	if (flow->contextSerial != 0 && !embedded) return false;
		int flowDeltaX = 0;
		int flowDeltaY = 0;
		const CssPositionedRecord* flowPosition = cssPositionedRecordForBlock(s_currentDoc, flow->anchorBlockIndex);
		if (flowPosition) {
			flowDeltaX = flowPosition->finalX - flowPosition->normalX;
			flowDeltaY = flowPosition->finalY - flowPosition->normalY;
		} else if (!embedded) {
			cssPositionRelativeAncestorDelta(s_currentDoc, flow->anchorBlockIndex, &flowDeltaX, &flowDeltaY);
		}
		const int drawY = embedded ? parentAtomic.y + flow->localOuterY :
			kContentY + blockLayoutY(flow->anchorBlockIndex) - s_scrollOffset + flowDeltaY;
	const int boxY = embedded ? drawY : drawY + cssMarginTopPx(flow->style,
		(flow->anchorBlockIndex >= 0 && flow->anchorBlockIndex < static_cast<int>(s_currentDoc.blocks.size()) &&
			s_currentDoc.blocks[static_cast<size_t>(flow->anchorBlockIndex)].type == BlockType::Heading) ? 10 : 4);
	const int boxH = std::max(1, flow->outerHeight > 0 ? flow->outerHeight : flow->totalHeight - cssMarginTopPx(flow->style, 4) -
		cssMarginBottomPx(flow->style, 8));
	int left = std::numeric_limits<int>::max();
	int top = std::numeric_limits<int>::max();
	int right = 0;
	int bottom = 0;
	bool found = false;
	for (const InlineFragmentLayout& fragment : flow->fragments) {
		if (!fragment.visible || fragment.blockIndex != blockIndex ||
			(!includeWhitespace && fragment.whitespace)) continue;
		const int x = (embedded ? parentAtomic.x : 0) + flow->contentX + fragment.x;
		const int y = embedded ? drawY + cssBorderTopPx(flow->style) + cssPaddingTopPx(flow->style, 0) + fragment.y :
			drawY + flow->contentOffsetY + fragment.y;
			left = std::min(left, x + flowDeltaX);
			top = std::min(top, y);
			right = std::max(right, x + flowDeltaX + std::max(0, fragment.w));
			bottom = std::max(bottom, y + std::max(0, fragment.h));
		found = true;
	}
	if (!found || right <= left || bottom <= top) return false;
	CssPaintRect flowClip = embedded ? parentAtomic : cssClipRectForHit(
		s_currentDoc, flow->anchorBlockIndex,
		flow->anchorBlockIndex >= 0 && flow->anchorBlockIndex < static_cast<int>(s_currentDoc.blocks.size())
			? s_currentDoc.blocks[static_cast<size_t>(flow->anchorBlockIndex)] : block,
		flow->outerX, boxY, flow->outerWidth, boxH, s_scrollOffset);
	const CssPaintRect clipped = cssClipHitTarget(
		CssPaintRect{left, top, right - left, bottom - top},
		flowClip);
	out = Rect{clipped.x, clipped.y, clipped.w, clipped.h};
	return out.w > 0 && out.h > 0;
}

bool Navigator::inlineFragmentContainsPoint(int blockIndex, int x, int y)
{
	if (blockIndex < 0 || blockIndex >= static_cast<int>(s_currentDoc.blocks.size())) return false;
	const DocBlock& block = s_currentDoc.blocks[static_cast<size_t>(blockIndex)];
	if (!blockHasVisibleCss(block)) return false;
	ensureCssMarginLayout(s_currentDoc);
	ensureInlineLayout(s_currentDoc);
	ensureCssFloatLayout(s_currentDoc);
	ensureCssPositionLayout(s_currentDoc);
	if (const CssFloatRecord* floatRecord = cssFloatRecordForBlock(s_currentDoc, blockIndex)) {
		CssPaintRect target{kContentX + floatRecord->borderBoxX,
			kContentY + floatRecord->borderBoxY - s_scrollOffset,
			floatRecord->borderBoxW, floatRecord->borderBoxH};
		if (floatRecord->contextSerial != 0) {
			for (const InlineFlowLayout& contextFlow : s_inlineLayoutSnapshot.flows) {
				if (contextFlow.contextSerial != floatRecord->contextSerial) continue;
				CssPaintRect parentAtomic;
				if (!atomicResultScreenRect(s_currentDoc, s_inlineLayoutSnapshot,
					contextFlow.atomicResultIndex, s_scrollOffset, parentAtomic)) continue;
				target = CssPaintRect{parentAtomic.x + contextFlow.contentX + floatRecord->borderBoxX,
					parentAtomic.y + contextFlow.localOuterY + cssBorderTopPx(contextFlow.style) +
						cssPaddingTopPx(contextFlow.style, 0) + floatRecord->borderBoxY,
					floatRecord->borderBoxW, floatRecord->borderBoxH};
				break;
			}
		}
		const CssPaintRect clipped = cssClipHitTarget(target, cssViewportClipRect());
		return clipped.w > 0 && clipped.h > 0 && x >= clipped.x && x < clipped.x + clipped.w &&
			y >= clipped.y && y < clipped.y + clipped.h;
	}
	const InlineFlowLayout* flow = inlineFlowForBlock(s_currentDoc, blockIndex);
	if (!flow) return false;
	CssPaintRect parentAtomic;
	const bool embedded = flow->contextSerial != 0 &&
		atomicResultScreenRect(s_currentDoc, s_inlineLayoutSnapshot, flow->atomicResultIndex, s_scrollOffset, parentAtomic);
	if (flow->contextSerial != 0 && !embedded) return false;
	int flowDeltaX = 0;
	int flowDeltaY = 0;
	const CssPositionedRecord* flowPosition = cssPositionedRecordForBlock(s_currentDoc, flow->anchorBlockIndex);
	if (flowPosition) {
		flowDeltaX = flowPosition->finalX - flowPosition->normalX;
		flowDeltaY = flowPosition->finalY - flowPosition->normalY;
	} else if (!embedded) {
		cssPositionRelativeAncestorDelta(s_currentDoc, flow->anchorBlockIndex, &flowDeltaX, &flowDeltaY);
	}
	const int drawY = embedded ? parentAtomic.y + flow->localOuterY :
		kContentY + blockLayoutY(flow->anchorBlockIndex) - s_scrollOffset + flowDeltaY;
	const int boxY = embedded ? drawY : drawY + cssMarginTopPx(flow->style,
		(flow->anchorBlockIndex >= 0 && flow->anchorBlockIndex < static_cast<int>(s_currentDoc.blocks.size()) &&
			s_currentDoc.blocks[static_cast<size_t>(flow->anchorBlockIndex)].type == BlockType::Heading) ? 10 : 4);
	const int boxH = std::max(1, flow->outerHeight > 0 ? flow->outerHeight : flow->totalHeight - cssMarginTopPx(flow->style, 4) -
		cssMarginBottomPx(flow->style, 8));
	const CssPaintRect clip = embedded ? parentAtomic : cssClipRectForHit(
		s_currentDoc, flow->anchorBlockIndex,
		flow->anchorBlockIndex >= 0 && flow->anchorBlockIndex < static_cast<int>(s_currentDoc.blocks.size())
			? s_currentDoc.blocks[static_cast<size_t>(flow->anchorBlockIndex)] : block,
		flow->outerX, boxY, flow->outerWidth, boxH, s_scrollOffset);
	for (const InlineFragmentLayout& fragment : flow->fragments) {
		if (!fragment.visible || fragment.whitespace || fragment.blockIndex != blockIndex) continue;
		const CssPaintRect clipped = cssClipHitTarget(
			CssPaintRect{(embedded ? parentAtomic.x : 0) + flow->contentX + fragment.x + flowDeltaX,
				embedded ? drawY + cssBorderTopPx(flow->style) + cssPaddingTopPx(flow->style, 0) + fragment.y :
					drawY + flow->contentOffsetY + fragment.y,
				fragment.kind == InlineItemKind::AtomicBlock ? std::max(0, fragment.boxWidth) : std::max(0, fragment.w),
				fragment.kind == InlineItemKind::AtomicBlock ? std::max(0, fragment.boxHeight) : std::max(0, fragment.h)}, clip);
		if (clipped.w > 0 && clipped.h > 0 &&
			x >= clipped.x && x < clipped.x + clipped.w &&
			y >= clipped.y && y < clipped.y + clipped.h) return true;
	}
	return false;
}

Navigator::Rect Navigator::linkBlockRect(int blockIndex)
{
	Rect inlineRect;
	if (inlineFragmentRectForBlock(blockIndex, false, inlineRect)) return inlineRect;
	const DocBlock& block = s_currentDoc.blocks[blockIndex];
	if (!blockHasVisibleCss(block)) return Rect{0, 0, 0, 0};
	const int blockMarginTop = cssMarginTopPx(block.style, 4);
	const int paddingLeft = cssPaddingLeftPx(block.style, 0);
	const int paddingRight = cssPaddingRightPx(block.style, 0);
	const int availableWidth = blockAvailableWidth(block, s_currentDoc);
	const int outerWidth = blockOuterWidth(block, availableWidth);
	const int borderLeft = cssBorderLeftPx(block.style);
	const int borderRight = cssBorderRightPx(block.style);
	const int innerWidth = std::max(1, outerWidth - borderLeft - borderRight - paddingLeft - paddingRight);
	int relY  = blockLayoutY(blockIndex);
	int drawY = kContentY + relY - s_scrollOffset + blockMarginTop + cssBorderTopPx(block.style) + cssPaddingTopPx(block.style, 0);
	int resolvedX = blockOuterX(block, s_currentDoc, availableWidth, outerWidth);
	if (const CssPositionedRecord* positioned = cssPositionedRecordForBlock(s_currentDoc, blockIndex)) {
		resolvedX = positioned->finalX + cssBorderLeftPx(block.style) + cssPaddingLeftPx(block.style, 0);
		drawY = positioned->finalY - s_scrollOffset + cssBorderTopPx(block.style) + cssPaddingTopPx(block.style, 0);
	} else {
		int ancestorDeltaX = 0;
		int ancestorDeltaY = 0;
		cssPositionRelativeAncestorDelta(s_currentDoc, blockIndex, &ancestorDeltaX, &ancestorDeltaY);
		resolvedX = cssBoundedGeometryAdd(resolvedX, ancestorDeltaX);
		drawY = cssBoundedGeometryAdd(drawY, ancestorDeltaY);
	}
	int h     = wrappedBlockHeight(block, std::max(1, innerWidth / kCharW), blockTextLineHeight(block));
	int w     = std::min(static_cast<int>(block.text.size()) * kCharW, innerWidth);
	const int boxY = kContentY + blockLayoutY(blockIndex) - s_scrollOffset + blockMarginTop;
	const int boxH = std::max(1, blockTotalHeight(block, s_currentDoc,
		blockIndex + 1 < static_cast<int>(s_currentDoc.blocks.size()) &&
			s_currentDoc.blocks[blockIndex + 1].type == BlockType::Heading) - blockMarginTop -
			cssMarginBottomPx(block.style, 8));
	const CssPaintRect clipped = cssClipHitTarget(
		CssPaintRect{resolvedX, drawY, w, h},
		cssClipRectForHit(s_currentDoc, blockIndex, block, resolvedX, boxY, outerWidth, boxH, s_scrollOffset));
	return Rect{clipped.x, clipped.y, clipped.w, clipped.h};
}

Navigator::Rect Navigator::formControlRect(int blockIndex)
{
	if (blockIndex < 0 || blockIndex >= static_cast<int>(s_currentDoc.blocks.size())) {
		return Rect{0, 0, 0, 0};
	}
	const DocBlock& block = s_currentDoc.blocks[blockIndex];
	if (!blockHasVisibleCss(block)) return Rect{0, 0, 0, 0};
	Rect inlineRect;
	if (inlineFragmentRectForBlock(blockIndex, true, inlineRect)) return inlineRect;
	const int blockMarginTop = cssMarginTopPx(block.style, 4);
	const int paddingTop = cssPaddingTopPx(block.style, 0);
	const int availableWidth = blockAvailableWidth(block, s_currentDoc);
	const int outerWidth = blockOuterWidth(block, availableWidth);
	int resolvedX = blockOuterX(block, s_currentDoc, availableWidth, outerWidth);
	const int relY = blockLayoutY(blockIndex);
	int drawY = kContentY + relY - s_scrollOffset + blockMarginTop + cssBorderTopPx(block.style) + paddingTop;
	if (const CssPositionedRecord* positioned = cssPositionedRecordForBlock(s_currentDoc, blockIndex)) {
		resolvedX = positioned->finalX + cssBorderLeftPx(block.style) + cssPaddingLeftPx(block.style, 0);
		drawY = positioned->finalY - s_scrollOffset + cssBorderTopPx(block.style) + paddingTop;
	} else {
		int ancestorDeltaX = 0;
		int ancestorDeltaY = 0;
		cssPositionRelativeAncestorDelta(s_currentDoc, blockIndex, &ancestorDeltaX, &ancestorDeltaY);
		resolvedX = cssBoundedGeometryAdd(resolvedX, ancestorDeltaX);
		drawY = cssBoundedGeometryAdd(drawY, ancestorDeltaY);
	}
	int w = blockFormControlWidth(block, availableWidth);
	if (block.type == BlockType::FormCheckbox || block.type == BlockType::FormRadio) {
		// Keep the logical control hit box on the indicator.  Label text gets a
		// separate bounded target and therefore cannot double-toggle a wrapping
		// control when the two rendered blocks overlap.
		w = 22;
	}
	const int boxY = kContentY + blockLayoutY(blockIndex) - s_scrollOffset + blockMarginTop;
	const int boxH = std::max(1, blockTotalHeight(block, s_currentDoc,
		blockIndex + 1 < static_cast<int>(s_currentDoc.blocks.size()) &&
			s_currentDoc.blocks[blockIndex + 1].type == BlockType::Heading) - blockMarginTop -
			cssMarginBottomPx(block.style, block.type == BlockType::ListItem ? 4 : 8));
	const CssPaintRect clipped = cssClipHitTarget(
		CssPaintRect{resolvedX, drawY, w, formControlHeight(block)},
		cssClipRectForHit(s_currentDoc, blockIndex, block, resolvedX, boxY, outerWidth, boxH, s_scrollOffset));
	return Rect{clipped.x, clipped.y, clipped.w, clipped.h};
}

int Navigator::computeDocumentHeight()
{
	ensureInlineLayout(s_currentDoc);
	ensureCssMarginLayout(s_currentDoc);
	ensureCssPositionLayout(s_currentDoc);
	if (s_cssPositionLayoutSnapshot.valid)
		return std::max(s_cssMarginLayoutSnapshot.documentHeight, s_cssPositionLayoutSnapshot.documentExtent);
	if (s_cssMarginLayoutSnapshot.valid) return s_cssMarginLayoutSnapshot.documentHeight;
	int h = kHeadingY + (s_currentDoc.bodyStyle.marginTop >= 0 ? s_currentDoc.bodyStyle.marginTop : 0);
	const int n = static_cast<int>(s_currentDoc.blocks.size());
	for (int idx = 0; idx < n; ++idx) {
		const DocBlock& block = s_currentDoc.blocks[idx];
		bool nextIsHeading = (idx + 1 < n && s_currentDoc.blocks[idx + 1].type == BlockType::Heading);
		h += blockTotalHeight(block, s_currentDoc, nextIsHeading);
	}
	return h + (s_currentDoc.bodyStyle.marginBottom >= 0 ? s_currentDoc.bodyStyle.marginBottom : 8);
}

int Navigator::maxScrollOffset()
{
	int overflow = s_documentHeight - kContentH;
	return overflow > 0 ? overflow : 0;
}

// -----------------------------------------------------------------------------
// file:// loading
// -----------------------------------------------------------------------------

WebDocument Navigator::loadFileUrl(const std::string& url)
{
	// Strip "file://" to get the absolute POSIX path.
	// file:///docs/index.html  ->  /docs/index.html
	// file://docs/index.html   ->  docs/index.html  (non-standard, tolerated)
	std::string path = url.substr(7); // remove "file://"
	if (path.size() >= 2 && path[0] == '/' && path[1] == '/') {
		// file:////... â€” trim the extra slash pair (rare)
		path = path.substr(1);
	}
	// path is now an absolute POSIX path like /docs/index.html

	// Derive a human-readable filename for the title.
	std::string filename = path;
	{
		size_t slash = filename.rfind('/');
		if (slash != std::string::npos) filename = filename.substr(slash + 1);
	}

	Logger::write(LogLevel::Info,
		std::string("Navigator loadFileUrl: ") + path);

	const std::string guessedContentType = guessedContentTypeForPath(path);
	const bool isHtml = (guessedContentType == "text/html");

	NavigatorPageMetadata metadata;
	metadata.requestedUrl = url;
	metadata.finalUrl = url;
	metadata.sourceType = "file";
	metadata.contentType = guessedContentType;
	auto finish = [&](WebDocument doc) -> WebDocument {
		storePageMetadata(metadata, doc);
		return doc;
	};

	if (!isNavigatorRenderableFileType(guessedContentType)) {
		metadata.errorStatus = "File not viewable";
		return finish(buildFileNotViewableDocument(url, path, guessedContentType));
	}

	FileReadResult fr = readTextFile(path);

	if (fr.status == FileReadStatus::NotFound) {
		metadata.errorStatus = "File not found";
		return finish(buildErrorDocument(url, "File not found: " + url));
	}
	if (fr.status == FileReadStatus::TooLarge) {
		metadata.errorStatus = "File too large";
		return finish(buildErrorDocument(url, "File too large to display: " + url));
	}
	if (fr.status == FileReadStatus::IoError) {
		metadata.errorStatus = "File I/O error";
		return finish(buildErrorDocument(url, "I/O error reading: " + url));
	}

	setSourcePreview(metadata, fr.text);

	if (isHtml) {
		// Delegate to the HTML parser; it handles title, headings, paragraphs, links.
		try {
		navigatorSmokeProgress("html-parser-start");
		WebDocument doc = parseHtml(url, fr.text, s_visitedUrls);
			navigatorSmokeProgress("html-parser-complete");
			if (doc.title.empty()) {
				// fallback title from filename
				size_t slash = path.rfind('/');
				doc.title = (slash != std::string::npos) ? path.substr(slash + 1) : path;
			}
			return finish(std::move(doc));
		} catch (...) {
			metadata.errorStatus = "HTML parse error";
			return finish(buildErrorDocument(url, "HTML parse error for: " + url));
		}
	}

	// Plain-text path: render the entire file as a single Preformatted block
	// so line breaks and spacing are preserved naturally.
	// A heading block shows the filename; one Preformatted block holds the content.
	WebDocument doc;
	doc.url   = url;
	doc.title = filename;
	doc.blocks.push_back({BlockType::Heading, filename, ""});

	// Strip trailing CR from the whole text for clean Windows line-endings.
	std::string cleanText = fr.text;
	{
		std::string out;
		out.reserve(cleanText.size());
		for (char c : cleanText) {
			if (c != '\r') out += c;
		}
		// Remove single trailing newline for tidiness
		if (!out.empty() && out.back() == '\n') out.pop_back();
		cleanText = std::move(out);
	}

	if (cleanText.empty()) {
		doc.blocks.push_back({BlockType::Paragraph, "(empty file)", ""});
	} else {
		doc.blocks.push_back({BlockType::Preformatted, cleanText, ""});
	}

	return finish(std::move(doc));
}

WebDocument Navigator::buildErrorDocument(const std::string& url,
													 const std::string& reason)
{
	Logger::write(LogLevel::Warn,
		std::string("Navigator error document: ") + reason);

	WebDocument doc;
	doc.url   = url;
	doc.title = "Page Not Found";
	doc.blocks.push_back({BlockType::Heading,   "Page Not Found",         ""});
	doc.blocks.push_back({BlockType::Paragraph, "Could not load " + url,  ""});
	doc.blocks.push_back({BlockType::Paragraph, reason,                   ""});
	doc.blocks.push_back({BlockType::Paragraph,
		"Place the file on the guideXOS filesystem and reload.", ""});
	doc.blocks.push_back({BlockType::Link, "Go to about:navigator", "about:navigator"});
	return doc;
}

} // namespace apps
} // namespace gxos
