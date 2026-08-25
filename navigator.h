#pragma once

#include "process.h"
#include "guide_web_document.h"   // BlockType, DocBlock, WebDocument (gxos::web)
#include "navigator_resource_diagnostics.h"
#include "navigator_resource_scheduler.h"
#include "navigator_javascript/navigator_script_host.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gxos {
namespace web {
struct HttpResponse;
}
namespace apps {

// =============================================================================
// Document model – provided by the reusable guideWeb layer (guide_web_document.h).
// Pull the types into this namespace so Navigator code is unaffected.
// =============================================================================

using gxos::web::BlockType;
using gxos::web::DocBlock;
using gxos::web::WebStyle;
using gxos::web::WebDocument;

// =============================================================================
// Bookmark – a named navigation target persisted by Navigator
// =============================================================================

struct Bookmark {
	std::string title;
	std::string url;
};

struct DownloadItem {
	std::string url;
	std::string finalUrl;
	std::string suggestedFileName;
	std::string contentType;
	std::string savedPath;
	size_t      byteCount = 0;
	bool        success = false;
	std::string error;
};

// One bounded record is retained per image reference in the current document.
// URLs are represented by a short hash in the public diagnostics surface; the
// resource cache still owns the full URL for the duration of the document.
struct NavigatorResourceTelemetry {
	int         ordinal = 0;
	uint32_t    schedulerOrdinal = 0;
	int         blockIndex = -1;
	int         blockY = -1;
	int         blockTop = -1;
	int         blockBottom = -1;
	int         displayWidth = 0;
	int         displayHeight = 0;
	int         viewportTop = 0;
	int         viewportBottom = -1;
	int         distanceFromViewport = -1;
	int         viewportRelation = static_cast<int>(NavigatorResourceViewportRelation::Unknown);
	bool        likelyVisible = false;
	std::string urlHash;
	std::string originHost;
	bool        sameOrigin = false;
	int         responseStatusCode = 0;
	std::string contentType;
	std::string contentEncoding;
	size_t      encodedBodyBytes = 0;
	size_t      decodedBodyBytes = 0;
	uint32_t    imageWidth = 0;
	uint32_t    imageHeight = 0;
	uint64_t    decodedRgbaBytes = 0;
	uint64_t    activeBytesBefore = 0;
	uint64_t    budgetHeadroomBefore = 0;
	uint64_t    displayPixelBytes = 0;
	int         redirectCount = 0;
	uint32_t    priority = 0;
	uint32_t    priorityBeforeViewport = 0;
	bool        admittedDueToViewportPriority = false;
	NavigatorResourceSchedulerState schedulerState = NavigatorResourceSchedulerState::Empty;
	uint64_t    budgetRequestedBytes = 0;
	uint64_t    budgetAcceptedBytes = 0;
	int         sharedResourceId = -1;
	bool        duplicate = false;
	NavigatorResourceClassification classification = NavigatorResourceClassification::OtherFailure;
	std::string reason;
};

struct NavigatorResourceAggregateCounters {
	int totalResourceReferences = 0;
	int attempted = 0;
	int loaded = 0;
	int failed = 0;
	int skipped = 0;
	int pngReferences = 0;
	int pngLoads = 0;
	int jpegReferences = 0;
	int jpegLoads = 0;
	int svgReferences = 0;
	int svgFailures = 0;
	int webpReferences = 0;
	int webpFailures = 0;
	int avifReferences = 0;
	int avifFailures = 0;
	int gifReferences = 0;
	int gifFailures = 0;
	int redirects = 0;
	int http4xx = 0;
	int http5xx = 0;
	int sizeBoundFailures = 0;
	int decodeFailures = 0;
	int networkTlsFailures = 0;
	int unsupportedMime = 0;
	int duplicateSkips = 0;
	int resourceLimitSkips = 0;
	int duplicateResourceUrls = 0;
	int duplicateNetworkFetches = 0;
	int duplicateDecodedImages = 0;
	uint32_t referencesDiscovered = 0;
	uint32_t uniqueReferences = 0;
	uint32_t duplicateReferences = 0;
	uint32_t schedulerCandidates = 0;
	uint32_t pending = 0;
	uint32_t fetchStarted = 0;
	uint32_t fetchCompleted = 0;
	uint32_t decodeStarted = 0;
	uint32_t decoded = 0;
	uint32_t attached = 0;
	uint32_t budgetDenied = 0;
	uint32_t resourceCapDenied = 0;
	uint32_t unsupportedSkipped = 0;
	uint32_t referencesCapacityDenied = 0;
	uint32_t released = 0;
	uint64_t activeCount = 0;
	uint64_t activeBytes = 0;
	uint64_t peakActiveBytes = 0;
	uint64_t currentEncodedResourceBytes = 0;
	uint64_t peakEncodedResourceBytes = 0;
	uint64_t peakTemporaryDecodeBytes = 0;
	uint64_t releasedDecodedBytes = 0;
	uint64_t deniedAllocationBytes = 0;
	uint64_t totalLoadedDecodedBytes = 0;
	uint64_t totalDeniedRequestedBytes = 0;
	int32_t viewportTop = 0;
	int32_t viewportBottom = -1;
	int32_t viewportWidth = 0;
	int32_t viewportHeight = 0;
	int32_t initialScrollOffset = 0;
	int32_t preloadMargin = 0;
	uint32_t visibleReferences = 0;
	uint32_t nearReferences = 0;
	uint32_t farReferences = 0;
	uint32_t unknownViewportReferences = 0;
	uint32_t visibleLoaded = 0;
	uint32_t visibleBudgetDenied = 0;
	uint32_t nearLoaded = 0;
	uint32_t nearBudgetDenied = 0;
	uint32_t farLoaded = 0;
	uint32_t farBudgetDenied = 0;
	uint32_t visiblePriorityAdmissions = 0;
	uint32_t offscreenBudgetDenied = 0;
	uint64_t decodedBytesVisible = 0;
	uint64_t decodedBytesNear = 0;
	uint64_t decodedBytesFar = 0;
};

struct NavigatorPageMetadata {
	std::string requestedUrl;
	std::string finalUrl;
	std::string sourceType;
	int         httpStatusCode = 0;
	std::string httpReasonPhrase;
	std::string contentType;
	std::string contentEncoding;
	std::string responseFraming;
	bool        contentLengthPresent = false;
	size_t      contentLength = 0;
	size_t      encodedBodyBytes = 0;
	size_t      decodedBodyBytes = 0;
	size_t      documentSegmentCount = 0;
	size_t      documentStorageBytes = 0;
	size_t      documentStorageCapacity = 0;
	size_t      documentHistoryBytes = 0;
	bool        documentStorageAllocationFailed = false;
	bool        truncatedResponse = false;
	bool        redirected = false;
	int         redirectCount = 0;
	std::string errorStatus;
	std::string unsupportedReason;
	bool        headerCapHit = false;
	bool        bodyCapHit = false;
	bool        tlsSucceededBeforeContentFailure = false;
	std::string scheme;
	std::string tlsBackend;
	std::string tlsCertificateValidation;
	std::string tlsStatus;
	std::string tlsError;
	std::string tlsErrorCode;
	std::string tlsConnectionPath;
	std::string tlsCredentialApi;
	std::string tlsCredentialStructure;
	std::string tlsCredentialProtocols;
	std::string tlsCredentialFlags;
	std::string tlsCredentialTarget;
	std::string tlsCertificateSubject;
	std::string tlsCertificateIssuer;
	std::string tlsCertificateValidFrom;
	std::string tlsCertificateValidTo;
	std::string tlsCertificateHostname;
	std::string tlsCertificateHostnameValidation;
	std::string tlsCertificateChainError;
	std::string tlsProtocol;
	std::string tlsCipherSuite;
	bool        tlsEnabled = false;
	bool        tlsValidated = false;
	bool        tlsCredentialAcquired = false;
	bool        tlsHandshakeStarted = false;
		bool        tlsSmokeSelfSignedBypass = false;
		bool        downgradeRedirectBlocked = false;
		std::string insecureRedirectLocation;
		std::string rawSource;
	std::string rawSourceForSave;
	size_t      rawSourceBytes = 0;
	bool        rawSourceTruncated = false;
	int         documentBlockCount = 0;
		int         imageBlockCount = 0;
		int         loadedImageCount = 0;
		int         failedImageCount = 0;
		int         remoteImageCount = 0;
		int         localImageCount = 0;
		int         jpegImageReferenceCount = 0;
		int         jpegImageAttemptCount = 0;
		int         jpegImageLoadCount = 0;
		int         pngImageLoadCount = 0;
		int         unsupportedImageCount = 0;
		std::string lastImageError;
		NavigatorResourceAggregateCounters resourceCounters;
		std::vector<NavigatorResourceTelemetry> resourceTelemetry;
		std::array<int, 64> resourceClassificationCounts{};
		size_t activeImageResources = 0;
		size_t activeImageBytes = 0;
		size_t peakActiveImageBytes = 0;
		size_t decodedImageBudgetBytes = static_cast<size_t>(kNavigatorDecodedImageBudgetBytes);
		size_t budgetDeniedBytes = 0;
		size_t peakEncodedResourceBytes = 0;
		size_t releasedDecodedBytes = 0;
		NavigatorResourceSchedulerStats resourceScheduler;
		size_t releasedImageResources = 0;
		size_t allocatedImageBytes = 0;
		size_t releasedImageBytes = 0;
		bool        cssEnabled = false;
		bool        cssDetected = false;
		int         styleRuleCount = 0;
		int         styleBlockCount = 0;
		int         inlineStyleCount = 0;
		int         externalStylesheetLoadedCount = 0;
		int         unsupportedExternalStylesheetCount = 0;
		int         unsupportedCssRuleCount = 0;
		int         unsupportedCssDeclarationCount = 0;
		int         cssUnsupportedSelectorCount = 0;
		int         cssParseErrorCount = 0;
		bool        cssStyleBlockCapped = false;
		size_t      cssStyleBytesProcessed = 0;
		int         cssLayoutMaxWidthAppliedCount = 0;
		int         cssAutoMarginCenteredBlockCount = 0;
		int         cssBackgroundBlockCount = 0;
		int         cssWrapperRenderCount = 0;
		int         cssDisplayNoneBlockCount = 0;
		int         cssTableRenderCount = 0;
		int         cssTableRowCount = 0;
		int         cssTableCellCount = 0;
		int         cssTableLayoutFallbackCount = 0;
		int         cssTableLogicalColumnCount = 0;
		int         cssTableDataCellCountPhase8B = 0;
		int         cssTableColspanCellCount = 0;
		int         cssTableMaximumColspan = 1;
		int         cssTableWrappedCellCount = 0;
		int         cssTableWideCount = 0;
		int         cssTableMalformedFallbackCount = 0;
		int         cssTableRowspanDeferredCount = 0;
		int         cssTableRowspanCellCount = 0;
		int         cssTableMaximumRowspan = 1;
		int         cssTableOccupiedGridSkips = 0;
		int         cssTableRowspanHeightAdjustments = 0;
		int         cssTableCombinedSpanCount = 0;
		int         cssTableResolvedVerticalEdgeCount = 0;
		int         cssTableResolvedHorizontalEdgeCount = 0;
		int         cssTableSuppressedInteriorSpanEdgeCount = 0;
		int         cssTableBorderConflictCount = 0;
		int         cssTableLinkHitTestEvidence = 0;
		int         cssTableGeometryClamps = 0;
		std::string cssTableGeometryEvidence;
		int         cssListRenderCount = 0;
		int         cssClampedValueCount = 0;
		int         cssLengthValueClampCount = 0;
		int         cssInvalidLengthValueCount = 0;
		int         cssLineBreakCount = 0;
		int         cssTableCaptionCount = 0;
		int         cssTableHeaderCellCount = 0;
		int         cssVisitedLinkCount = 0;
		int         cssBorderedBlocksRendered = 0;
		int         cssDashedBordersRendered = 0;
		int         cssDottedBordersRendered = 0;
		int         cssBorderWidthClamps = 0;
		int         cssCollapsedTablesRendered = 0;
		int         cssSeparateTablesRendered = 0;
		int         cssTableBorderSpacingClamps = 0;
		int         cssListStyleMarkersRendered = 0;
		int         cssListStyleNoneApplied = 0;
		int         cssTextDecorationsRendered = 0;
		int         cssGenericFontFamilyApplied = 0;
		int         cssGenericFontFamilyFallbacks = 0;
		int         cssFiguresRendered = 0;
		int         cssFigcaptionsRendered = 0;
		int         cssBlockquotesRendered = 0;
		int         cssDefinitionListsRendered = 0;
		int         cssImagesConstrained = 0;
		int         cssImagesAspectPreserved = 0;
		int         cssImageAltFallbacks = 0;
		int         cssImageSizeClamps = 0;
		int         cssNestedLayoutClamps = 0;
		int         cssMaxWrapperAncestorDepth = 0;
		int         cssSelectorGroupsParsed = 0;
		int         cssCompoundSelectorsParsed = 0;
		int         cssChildCombinators = 0;
		int         cssDescendantCombinators = 0;
		int         cssAdjacentSiblingCombinators = 0;
		int         cssGeneralSiblingCombinators = 0;
		int         cssAdjacentSiblingMatches = 0;
		int         cssGeneralSiblingMatches = 0;
		int         cssSiblingScanSteps = 0;
		int         cssSiblingScanClamps = 0;
		int         cssSiblingMetadataClamps = 0;
		int         cssSiblingMetadataErrors = 0;
		int         cssSelectorMatches = 0;
		int         cssSpecificityOverrides = 0;
		int         cssSourceOrderOverrides = 0;
		int         cssInlineOverrides = 0;
		int         cssInheritedPropertiesApplied = 0;
		int         cssSelectorDepthClamps = 0;
		int         cssSelectorGroupClamps = 0;
		int         cssCascadePropertyResolutions = 0;
		int         cssImportantDeclarationsApplied = 0;
		int         cssRuleCapCount = 0;
		int         cssDeclarationCapCount = 0;
		int         cssInheritanceDepthClamps = 0;
		int         cssPseudoClassesParsed = 0;
		int         cssStructuralPseudoMatches = 0;
		int         cssFirstChildMatches = 0;
		int         cssLastChildMatches = 0;
		int         cssNthChildMatches = 0;
		int         cssOfTypeMatches = 0;
		int         cssNotMatches = 0;
		int         cssLinkPseudoMatches = 0;
		int         cssVisitedPseudoMatches = 0;
		int         cssPseudoClassClamps = 0;
		int         cssNthExpressionParseErrors = 0;
		int         cssStructuralMetadataClamps = 0;
		int         cssSelectorEvaluationStepClamps = 0;
		int         cssEmptyPseudoParsed = 0;
		int         cssEmptyPseudoMatches = 0;
		int         cssEmptyMetadataIncomplete = 0;
		int         cssContentMetadataClamps = 0;
		int         cssSelectorGroupMemberRecoveries = 0;
		int         cssCommentScanClamps = 0;
		int         cssUnterminatedCommentErrors = 0;
		int         cssUnbalancedParenthesisErrors = 0;
		int         cssUnbalancedBracketErrors = 0;
		int         cssUnterminatedStringErrors = 0;
		int         cssInvalidCombinatorSequences = 0;
		int         cssIdentifierEscapeRejections = 0;
		int         cssSelectorMemberParseFailures = 0;
		int         cssSelectorRecoverySuccesses = 0;
		int         cssCheckedPseudoParsed = 0;
		int         cssCheckedPseudoMatches = 0;
		int         cssDisabledPseudoParsed = 0;
		int         cssDisabledPseudoMatches = 0;
		int         cssEnabledPseudoParsed = 0;
		int         cssEnabledPseudoMatches = 0;
		int         cssRequiredPseudoParsed = 0;
		int         cssRequiredPseudoMatches = 0;
		int         cssReadonlyPseudoParsed = 0;
		int         cssReadonlyPseudoMatches = 0;
		int         cssReadwritePseudoParsed = 0;
		int         cssReadwritePseudoMatches = 0;
		int         cssFocusPseudoParsed = 0;
		int         cssFocusPseudoMatches = 0;
		int         cssFocusVisiblePseudoParsed = 0;
		int         cssFocusVisiblePseudoMatches = 0;
		int         cssRuntimeFocusRecomputations = 0;
		std::string cssComputedStyleEvidence;
		// Phase 3A bounded box/clip diagnostics.  These are counters rather than
		// per-node logs so runtime evidence stays small on large documents.
		int         cssBoxSizingContentBox = 0;
		int         cssBoxSizingBorderBox = 0;
		int         cssWidthAutoResolutions = 0;
		int         cssHeightAutoResolutions = 0;
		int         cssPercentageWidthResolved = 0;
		int         cssPercentageHeightResolved = 0;
		int         cssPercentageIndefiniteBasis = 0;
		int         cssPercentageCycleClamps = 0;
		int         cssMinWidthConstraints = 0;
		int         cssMaxWidthConstraints = 0;
		int         cssMinHeightConstraints = 0;
		int         cssMaxHeightConstraints = 0;
		int         cssConstraintConflicts = 0;
		int         cssOverflowVisibleBoxes = 0;
		int         cssOverflowHiddenBoxes = 0;
		int         cssOverflowAutoBoxes = 0;
		int         cssOverflowScrollBoxes = 0;
		int         cssOverflowScrollDeferred = 0;
		int         cssActiveScrollContainers = 0;
		int         cssClippedDescendants = 0;
		int         cssLocalScrollOperations = 0;
		int         cssLocalScrollWheelOperations = 0;
		int         cssNestedScrollContainers = 0;
		int         cssScrollClamps = 0;
		int         cssScrollContentExtentRecords = 0;
		int         cssLocalScrollHitTestEvidence = 0;
		std::string cssScrollEvidence;
		int         cssScrollbarVerticalVisibleCount = 0;
		int         cssScrollbarHorizontalVisibleCount = 0;
		int         cssScrollbarAutoHiddenCount = 0;
		int         cssScrollbarScrollModeZeroRangeCount = 0;
		int         cssScrollbarThumbDragOperations = 0;
		int         cssScrollbarTrackClickOperations = 0;
		int         cssScrollbarNestedOperations = 0;
		int         cssScrollbarHitTestInterceptions = 0;
		int         cssScrollbarExtentNeutralRecords = 0;
		int         cssScrollbarVisibilityIterations = 0;
		int         cssScrollbarVisibilityIterationClamps = 0;
		std::string cssScrollbarEvidence;
		int         cssClipIntersections = 0;
		int         cssClipDepthClamps = 0;
		int         cssClippedHitTargets = 0;
		int         cssVisibilityHiddenBoxes = 0;
		int         cssOpacityBoxes = 0;
		int         cssOpacityZeroBoxes = 0;
		int         cssVerticalAlignApplications = 0;
		int         cssBoxGeometryClamps = 0;
		int         cssLayoutPasses = 0;
		int         cssLayoutRecomputations = 0;
		int         cssClipRecordCount = 0;
		int         cssHitTargetsBeforeClipping = 0;
		int         cssHitTargetsAfterClipping = 0;
		int         cssEvidenceRecordCount = 0;
		int         cssOpacityImageApproximation = 0;
		std::string cssGeometryEvidence;
		// Phase 3B bounded inline-flow diagnostics/evidence.
		int         cssInlineItems = 0;
		int         cssInlineTextRuns = 0;
		int         cssInlineWhitespaceRuns = 0;
		int         cssInlineForcedBreaks = 0;
		int         cssLineBoxes = 0;
		int         cssLineWraps = 0;
		int         cssWhitespaceCollapses = 0;
		int         cssLeadingSpaceSuppressions = 0;
		int         cssTrailingSpaceSuppressions = 0;
		int         cssReplacedInlineItems = 0;
		int         cssControlInlineItems = 0;
		int         cssVerticalAlignAdjustments = 0;
		int         cssLineHeightClamps = 0;
		int         cssBaselineIterationClamps = 0;
		int         cssInlineFragments = 0;
		int         cssInlineFragmentClamps = 0;
		int         cssInlineHitFragments = 0;
		int         cssDescenderSafeLines = 0;
		int         cssInlineBlockItems = 0;
		int         cssInlineNestingClamps = 0;
		int         cssInlineWrapScanClamps = 0;
		int         cssInlineEvidenceRecordCount = 0;
		int         cssAtomicFormattingContexts = 0;
		int         cssAtomicContextDepthMax = 0;
		int         cssAtomicContextDepthClamps = 0;
		int         cssAtomicContextsDocument = 0;
		int         cssAtomicLayoutOperations = 0;
		int         cssAtomicLayoutOperationClamps = 0;
		int         cssInlineBlockAutoWidths = 0;
		int         cssInlineBlockExplicitWidths = 0;
		int         cssInlineBlockShrinkToFit = 0;
		int         cssInlineBlockPreferredMinClamps = 0;
		int         cssInlineBlockPreferredWidthClamps = 0;
		int         cssInlineBlockBaselineFromLine = 0;
		int         cssInlineBlockBaselineFallback = 0;
		int         cssInlineBlockNested = 0;
		int         cssInlineBlockWraps = 0;
		int         cssInlineBlockHitTargets = 0;
		int         cssInlineBlockOverflowClips = 0;
		int         cssAtomicContextIncomplete = 0;
		std::string cssInlineEvidence;
		int         cssAtomicEvidenceRecordCount = 0;
		std::string cssAtomicEvidence;
		// Bounded Flexbox diagnostics and fixture evidence.
		int         cssFlexContainers = 0;
		int         cssInlineFlexContainers = 0;
		int         cssFlexItems = 0;
		int         cssFlexAnonymousItems = 0;
		int         cssFlexNestedContainers = 0;
		int         cssFlexNestedMultilineContainers = 0;
		int         cssFlexColumnWrappedContainers = 0;
		int         cssFlexLines = 0;
		int         cssFlexWrappedContainers = 0;
		int         cssFlexWrapReverseContainers = 0;
		int         cssFlexAlignContentContainers = 0;
		int         cssFlexStretchedLines = 0;
		int         cssFlexWrapUnsupported = 0;
		int         cssFlexAbsoluteExcluded = 0;
		int         cssFlexDisplayNoneExcluded = 0;
		int         cssFlexOrderSortItems = 0;
		int         cssFlexBaseSizeQueries = 0;
		int         cssFlexIntrinsicQueries = 0;
		int         cssFlexAutomaticMinimumApplied = 0;
		int         cssFlexAutomaticMinimumZero = 0;
		int         cssFlexGrowIterations = 0;
		int         cssFlexShrinkIterations = 0;
		int         cssFlexFreezeIterations = 0;
		int         cssFlexCrossSizePasses = 0;
		int         cssFlexBaselineItems = 0;
		int         cssFlexAutoMarginAbsorptions = 0;
		int         cssFlexGapClamps = 0;
		int         cssFlexGeometryClamps = 0;
		int         cssFlexDepthClamps = 0;
		int         cssFlexOperationClamps = 0;
		int         cssFlexUnsupportedDeclarations = 0;
		int         cssFlexEvidenceRecords = 0;
		std::string cssFlexEvidence;
		// Phase 3D bounded margin-collapse/BFC diagnostics and fixture evidence.
		int         cssMarginCollapseSets = 0;
		int         cssMarginCollapseParticipants = 0;
		int         cssMarginCollapseSibling = 0;
		int         cssMarginCollapseParentTop = 0;
		int         cssMarginCollapseParentBottom = 0;
		int         cssMarginCollapseEmpty = 0;
		int         cssMarginCollapsePositiveOnly = 0;
		int         cssMarginCollapseNegativeOnly = 0;
		int         cssMarginCollapseMixed = 0;
		int         cssMarginCollapseBlockedBorder = 0;
		int         cssMarginCollapseBlockedPadding = 0;
		int         cssMarginCollapseBlockedBfc = 0;
		int         cssMarginCollapseBlockedHeight = 0;
		int         cssMarginCollapseBlockedContent = 0;
		int         cssMarginCollapseDepthClamps = 0;
		int         cssMarginGeometryClamps = 0;
		int         cssBfcRoot = 0;
		int         cssBfcInlineBlock = 0;
		int         cssBfcOverflow = 0;
		int         cssBfcAtomic = 0;
		int         cssMarginCollapseEvidenceRecords = 0;
		std::string cssMarginCollapseEvidence;
		// Phase 3E bounded float/clear diagnostics and fixture evidence.
		int         cssFloatLeft = 0;
		int         cssFloatRight = 0;
		int         cssFloatBlockifications = 0;
		int         cssFloatRecords = 0;
		int         cssFloatPlacementAttempts = 0;
		int         cssFloatPlacementDownshifts = 0;
		int         cssFloatSideBySide = 0;
		int         cssFloatWidthOverflows = 0;
		int         cssFloatLineExclusions = 0;
		int         cssFloatZeroWidthLineAdvances = 0;
		int         cssFloatBfcAvoidances = 0;
		int         cssFloatBfcDownshifts = 0;
		int         cssClearLeft = 0;
		int         cssClearRight = 0;
		int         cssClearBoth = 0;
		int         cssClearanceApplied = 0;
		int         cssFloatContainmentBoundaries = 0;
		int         cssFloatScopeSuppressions = 0;
		int         cssFloatHeightContainments = 0;
		int         cssBfcFloatContainments = 0;
		int         cssBfcFloatHeightExtensions = 0;
		int         cssBfcFloatHeightNoops = 0;
		int         cssBfcFloatAvoidanceAttempts = 0;
		int         cssBfcFloatAvoidanceFits = 0;
		int         cssBfcFloatAvoidanceDownshifts = 0;
		int         cssBfcFloatTooWide = 0;
		int         cssNestedFloatContexts = 0;
		int         cssNestedFloatDepthClamps = 0;
		int         cssFloatInsideInlineBlock = 0;
		int         cssFloatInsideFloat = 0;
		int         cssFloatListCases = 0;
		int         cssFloatTableCellCases = 0;
		int         cssFloatTableAvoidanceCases = 0;
		int         cssFloatedTableUnsupported = 0;
		int         cssFloatDocumentExtentExtensions = 0;
		int         cssFloatGeometryClamps = 0;
		int         cssFloatPlacementAttemptClamps = 0;
		int         cssFloatExclusionScanClamps = 0;
		int         cssFloatBfcDepthClamps = 0;
		int         cssFloatEvidenceRecords = 0;
		std::string cssFloatEvidence;
		// Phase 3G bounded positioning diagnostics and fixture evidence.
		int         cssPositionStatic = 0;
		int         cssPositionRelative = 0;
		int         cssPositionAbsolute = 0;
		int         cssPositionFixed = 0;
		int         cssPositionSticky = 0;
		int         cssPositionUnsupportedFixed = 0;
		int         cssPositionUnsupportedSticky = 0;
		int         cssStickyElementCount = 0;
		int         cssStickyRootCount = 0;
		int         cssStickyLocalScrollCount = 0;
		int         cssStickyConstrainedCount = 0;
		int         cssStickyReleaseCount = 0;
		int         cssStickyHorizontalCount = 0;
		int         cssStickyFlexCount = 0;
		int         cssStickyPositionedDescendantCount = 0;
		int         cssStickyHyperlinkHitTestEvidence = 0;
		std::string cssStickyEvidence;
		int         cssRelativeOffsets = 0;
		int         cssRelativePercentageOffsets = 0;
		int         cssAbsoluteBoxes = 0;
		int         cssAbsoluteBlockifications = 0;
		int         cssPositionedContainingBlocks = 0;
		int         cssPositionRootFallbacks = 0;
		int         cssPositionAncestryClamps = 0;
		int         cssAbsoluteStaticPositionUses = 0;
		int         cssAbsoluteShrinkToFit = 0;
		int         cssAbsoluteOutOfFlow = 0;
		int         cssFixedViewportRecords = 0;
		int         cssFixedAbsoluteDescendants = 0;
		int         cssFixedFlexExclusions = 0;
		int         cssFixedHitTestRecords = 0;
		int         cssFixedStackingRecords = 0;
		int         cssFixedExtentExclusions = 0;
		int         cssPositionDocumentExtentExtensions = 0;
		int         cssZIndexAuto = 0;
		int         cssZIndexNegative = 0;
		int         cssZIndexZero = 0;
		int         cssZIndexPositive = 0;
		int         cssPositionHitOcclusions = 0;
		int         cssPositionGeometryClamps = 0;
		int         cssPositionUnsupportedTable = 0;
		int         cssPositionStackingOwners = 0;
		int         cssPositionStackingDepthMax = 0;
		int         cssPositionStackingDepthClamps = 0;
		int         cssPositionNestedZRecords = 0;
		int         cssPositionNegativeZRecords = 0;
		int         cssPositionPositiveZRecords = 0;
		int         cssPositionEqualZSourceOrders = 0;
		int         cssPositionInlineFragmentOwners = 0;
		int         cssPositionInlineFragmentsShifted = 0;
		int         cssPositionInlineAncestryClamps = 0;
		int         cssPositionInlineContainingBlocks = 0;
		int         cssPositionInlineContainingBlockIncomplete = 0;
		int         cssPositionStaticSnapshots = 0;
		int         cssPositionStaticSnapshotFallbacks = 0;
		int         cssPositionLifecycleResets = 0;
		int         cssPositionedEvidenceRecords = 0;
		std::string cssPositionedEvidence;
	bool        downloaded = false;
	std::string downloadSavedPath;
	size_t      downloadByteCount = 0;
	int         formCount = 0;
	int         formInputCount = 0;
	int         formCheckboxCount = 0;
	int         formRadioCount = 0;
	int         formTextareaCount = 0;
	int         formSelectCount = 0;
		int         unsupportedFormControlCount = 0;
		int         htmlFormsParsed = 0;
		int         htmlFieldsetsParsed = 0;
		int         htmlLabelsParsed = 0;
		int         htmlInputsParsed = 0;
		int         htmlButtonsParsed = 0;
		int         htmlTextareasParsed = 0;
		int         htmlSelectsParsed = 0;
		int         htmlOptionsParsed = 0;
		int         htmlHiddenControls = 0;
		int         controlMetadataClamps = 0;
		int         controlTextTruncations = 0;
		int         formControlsRendered = 0;
		int         formControlsUnsupported = 0;
		int         formInteractionsDeferred = 0;
		int         formRuntimeControlsInitialized = 0;
		int         formCheckboxActivations = 0;
		int         formCheckboxToggles = 0;
		int         formRadioActivations = 0;
		int         formRadioGroupUnchecks = 0;
		int         formLabelActivations = 0;
		int         formButtonActivations = 0;
		int         formDisabledActivationBlocks = 0;
		int         formHiddenHitTargetsSuppressed = 0;
		int         formDuplicateActivationSuppressed = 0;
		int         formRuntimeStateResets = 0;
		int         formHitTargetsRegistered = 0;
		int         formHitTargetClamps = 0;
		int         formFocusableControls = 0;
		int         formFocusChanges = 0;
		int         formFocusClears = 0;
		int         formFocusWraps = 0;
		int         formTabForward = 0;
		int         formTabBackward = 0;
		int         formKeyboardActivations = 0;
		int         formSpaceActivations = 0;
		int         formEnterActivations = 0;
		int         formKeyRepeatSuppressed = 0;
		int         formStaleKeyActivationBlocks = 0;
		int         formDisabledFocusSkips = 0;
		int         formHiddenFocusSkips = 0;
		int         formFocusStateResets = 0;
		std::string formFocusOrigin;
		uint64_t    formFocusGeneration = 0;
		uint64_t    formFocusedLogicalSerial = 0;
		int         cssCheckedRuntimeRecomputations = 0;
		std::string formInteractionMode = "session_local_non_submitting";
	bool        unsupportedFormMethod = false;
	bool        unsupportedFormEncoding = false;
	bool        postSupportedHosted = true;
	bool        postSupportedBareMetal = false;
	std::string lastSubmittedFormUrl;
	std::string lastSubmittedFormAction;
	std::string lastSubmittedFormMethod;
	std::string lastSubmittedFormStatus;
	std::string lastPostHttpStatus;
	std::string lastPostContentType;
};

// Phase 2I keeps lifecycle evidence deliberately smaller than the parsed
// document model.  These enums are used only for bounded diagnostics and
// deterministic smoke assertions; they are not a navigation/session model.
enum class NavigatorDocumentCategory : uint8_t {
	None = 0,
	LocalFile,
	Http,
	Https,
	GeneratedAbout,
	Error,
	Unsupported,
};

enum class NavigatorTransitionCategory : uint8_t {
	InitialNavigation = 0,
	Navigation,
	SameDocumentRecomputation,
	Reload,
	HistoryBack,
	HistoryForward,
	RedirectReplacement,
	LocalFileNavigation,
	GeneratedAboutNavigation,
	PageInfoGeneration,
	SavePageTextGeneration,
	NavigationFailure,
	ParseFailure,
	TlsPolicyFailure,
	AbortedNavigation,
	WindowDocumentTeardown,
};

struct NavigatorLifecycleDiagnostics {
	uint64_t documentGenerationChanges = 0;
	uint64_t sameDocumentRecomputations = 0;
	uint64_t documentReplacements = 0;
	uint64_t focusPreservedRecompute = 0;
	uint64_t focusClearedReload = 0;
	uint64_t focusClearedHistory = 0;
	uint64_t focusClearedRedirect = 0;
	uint64_t focusClearedGeneratedPage = 0;
	uint64_t focusClearedNavigationFailure = 0;
	uint64_t runtimeStateClears = 0;
	uint64_t staleMouseReleaseBlocks = 0;
	uint64_t staleKeyReleaseBlocks = 0;
	uint64_t inspectedDocumentGuardPass = 0;
	uint64_t inspectedDocumentGuardBlock = 0;
	uint64_t pageInfoSourceValid = 0;
	uint64_t saveTextSourceValid = 0;
	uint64_t historyStateNonpersistent = 0;
	uint64_t transitionMetadataClamps = 0;

	uint64_t visibleDocumentGeneration = 0;
	uint64_t inspectedDocumentGeneration = 0;
	NavigatorDocumentCategory visibleDocumentCategory = NavigatorDocumentCategory::None;
	NavigatorDocumentCategory inspectedSourceCategory = NavigatorDocumentCategory::None;
	NavigatorTransitionCategory lastTransition = NavigatorTransitionCategory::InitialNavigation;
	bool requestedFinalUrlEqual = true;
	bool visibleDocumentGenerated = false;
	bool visibleDocumentInspectionView = false;
	bool sourceReferenceValid = false;
	bool ownershipGuardPassed = false;

	std::string saveTextIntendedSourceCategory = "none";
	std::string saveTextActualSourceCategory = "none";
	uint64_t saveTextVisibleTextByteCount = 0;
	bool saveTextGeneratedPageExcluded = false;
	bool saveTextPasswordRedacted = false;
	bool saveTextHiddenControlExcluded = false;
	bool saveTextDiagnosticsExcluded = true;
};

// =============================================================================
// Navigator – first-class guideXOS app
//
// This hosted/compositor implementation is the authoritative full Navigator
// path for the guideXOS app model. Keep portable document behavior in guideWeb
// or small adapters, and keep platform-only transport/rendering details here.
// The bare-metal NavigatorApp in kernel_apps.* is a thin capability-limited
// adapter and should not grow a divergent full browser implementation.
// =============================================================================

class Navigator {
public:
	static uint64_t Launch();
	static bool SmokeNavigateTo(const std::string& url);
	static bool SmokeNavigateToQuiet(const std::string& url);
	static bool SmokeNavigateToWithHistory(const std::string& url);
	static bool SmokeSubmitFirstForm(const std::string& value);
	static int SmokeFindInPage(const std::string& query);
	static bool SmokeClickFirstLink();
	static bool SmokeHitLinkById(const std::string& id);
	static bool SmokeLinkGeometryById(const std::string& id,
		int& outPaintX, int& outPaintY, int& outPaintW, int& outPaintH,
		int& outFinalX, int& outFinalY, int& outFinalW, int& outFinalH,
		int& outClipX, int& outClipY, int& outClipW, int& outClipH);
	static bool SmokeTableGeometryById(const std::string& id,
		int& outX, int& outY, int& outW, int& outH,
		int& outRows, int& outColumns);
	static bool SmokeTableCellGeometryById(const std::string& id,
		int& outX, int& outY, int& outW, int& outH,
		int& outRow, int& outColumn, int& outRowSpan, int& outColSpan);
	static bool SmokeBlockGeometryById(const std::string& id,
		int& outX, int& outY, int& outW, int& outH);
	static bool SmokeHitLinkAt(int x, int y, const std::string& id);
	static std::string SmokeHitTargetIdAt(int x, int y);
	static void SmokeSetScrollOffset(int offset);
	static int SmokeScrollOffset();
	static bool SmokeSetElementScrollOffsetById(const std::string& id, int offsetX, int offsetY);
	static int SmokeElementScrollOffsetYById(const std::string& id);
	static int SmokeElementMaxScrollYById(const std::string& id);
	static bool SmokeElementScrollbarGeometryById(const std::string& id, bool horizontal,
		bool thumb, int& outX, int& outY, int& outW, int& outH);
	static int SmokeElementScrollOffsetXById(const std::string& id);
	static int SmokeElementMaxScrollXById(const std::string& id);
	static bool SmokePointerInput(int x, int y, int button, const std::string& action);
	static bool SmokeDragFirstLinkSelectsWithoutNavigation();
	static std::string SmokeRuntimeReport();
	static std::string SmokePageDiagnostics();
	static std::string SmokeLifecycleReport();
	static std::string SmokeCurrentUrl();
	static int SmokeCurrentBlockCount();
	static std::string SmokeCurrentDocumentText();
	static uint64_t SmokeDocumentLayoutRevision();
	static bool SmokeDocumentDirty();
	static size_t SmokeJavaScriptHandlerCount();
	static size_t SmokeJavaScriptListenerCount();
	static std::string SmokeJavaScriptLastError();
	static std::string SmokeCurrentLinkUrl(const std::string& text);
	static bool SmokeClickFormControlById(const std::string& id);
	static bool SmokeClickFormLabelById(const std::string& id);
	static bool SmokeFormControlCheckedById(const std::string& id);
	static bool SmokeFormControlDisabledById(const std::string& id);
	static bool SmokeFormHitTargetById(const std::string& id);
	static int SmokeFormActivationCountById(const std::string& id);
	static bool SmokeFormMouseSafetyById(const std::string& id);
	static bool SmokeFocusFormControlById(const std::string& id, bool keyboardOrigin = true);
	static bool SmokeFormControlFocusedById(const std::string& id);
	static std::string SmokeFocusedFormControlId();
	static std::string SmokeFormFocusOrigin();
	static int SmokeFormFocusableCount();
	static bool SmokeKeyPress(int keyCode, const std::string& action);
	static bool SmokeSetFormControlDisabledById(const std::string& id, bool disabled);
	static bool SmokeSetFormControlHiddenById(const std::string& id, bool hidden);
	static void SmokeDeactivateWindow();
	static bool SmokeForceFormFocusGenerationMismatch();
	static int SmokeFormControlInputLengthById(const std::string& id);
	static void SmokeFocusAddressBar();
	static bool SmokeReloadCurrentDocument();
	static bool SmokeGoBack();
	static bool SmokeGoForward();
	static bool SmokeMouseDownFormControlById(const std::string& id);
	static bool SmokeMouseUp();
	// Returns the widget IDs registered with the compositor toolbar.
	// Used by hosted smoke to verify the full modern toolbar (7 buttons) is
	// present and that the old stale four-button placeholder is not active.
	static std::vector<int> SmokeToolbarWidgetIds();

	static std::vector<int> s_registeredWidgetIds;

private:
	// -------------------------------------------------------------------------
	// Layout helper
	// -------------------------------------------------------------------------
	struct Rect {
		int x, y, w, h;
		bool contains(int px, int py) const {
			return px >= x && px < x + w && py >= y && py < y + h;
		}
	};

	struct FindMatch {
		int blockIndex = -1;
		size_t offset = 0;
		size_t length = 0;
	};

	struct SelectionPosition {
		int blockIndex = -1;
		size_t offset = 0;
	};

	struct SelectionRange {
		SelectionPosition start;
		SelectionPosition end;
		bool valid = false;
	};

	// -------------------------------------------------------------------------
	// Input hit-testing
	// -------------------------------------------------------------------------
	enum class HitTarget : uint8_t {
		None = 0,
		Back,
		Forward,
		Reload,
		Home,
		Bookmarks,
		AddBookmark,
		Find,
		AddressBar,
		Link,   // any Link block; s_hitLinkBlockIndex carries the index
		FormInput,
		FormCheckbox,
		FormRadio,
		FormTextarea,
		FormSelect,
		FormSubmit,
		FormLabel,
		ElementScrollbar,
	};

	enum class MouseMode : uint8_t {
		None = 0,
		PotentialLinkClick,
		PotentialTextSelection,
		SelectingText,
		FormInputInteraction,
		AddressBarInteraction,
		ToolbarInteraction,
		ElementScrollbarInteraction,
	};

	enum class ScrollbarAxis : uint8_t {
		None = 0,
		Vertical,
		Horizontal,
	};

	// -------------------------------------------------------------------------
	// Entry point / event loop
	// -------------------------------------------------------------------------
	static int  main(int argc, char** argv);

	// -------------------------------------------------------------------------
	// URL loading – the central dispatch point
	// -------------------------------------------------------------------------

	// loadUrl() is the raw document-loading engine.  It fetches and renders
	// the document but does NOT modify history.  All callers that represent
	// user navigation (links, Home, Back, Forward) go through the helpers below.
	static void loadUrl(const std::string& url, bool updateDisplayAfterLoad = true,
		NavigatorTransitionCategory transition = NavigatorTransitionCategory::Navigation);

	// navigateTo() – normal forward navigation (link clicks, Home).
	//   Pushes the current URL onto the back stack, clears the forward stack,
	//   then calls loadUrl().
	static void navigateTo(const std::string& url);

	// goBack() / goForward() – history traversal.
	//   Move current URL to the opposite stack then call loadUrl().
	//   Show a status message and do nothing if the respective stack is empty.
	static void goBack();
	static void goForward();

	static WebDocument buildNavigatorHomeDocument();
	static WebDocument buildAboutNavigatorDocument();
	static WebDocument buildPageInfoDocument();
	static WebDocument buildViewSourceDocument();
	static WebDocument buildRuntimeDocument();
	static WebDocument buildDownloadsDocument();
	// Load a file:// URL and convert the raw text to a WebDocument.
	// Returns an error document if the file cannot be read.
	static WebDocument loadFileUrl(const std::string& url);
	// Load an http:// URL and convert the response body to a WebDocument.
	static WebDocument loadHttpUrl(const std::string& url);
	static WebDocument loadHttpResponseDocument(const std::string& url, const gxos::web::HttpResponse& response);
	// Build a "Page Not Found" error document for the given URL.
	static WebDocument buildErrorDocument(const std::string& url, const std::string& reason);

	// -------------------------------------------------------------------------
	// Bookmark management
	// -------------------------------------------------------------------------
	static void        loadBookmarks();
	static void        saveBookmarks();
	static void        addBookmark(const std::string& title, const std::string& url);
	static WebDocument buildBookmarksDocument();
	static WebDocument buildSavePageTextDocument();
	static WebDocument buildSavePageSourceDocument();

	// -------------------------------------------------------------------------
	// Rendering
	// -------------------------------------------------------------------------
	static void updateDisplay(bool renderDocumentContent = true);
	static void renderToolbar();
	static void renderDocument();
	static void renderStatusBar();
	static void updateStatus(const std::string& status);
	static void updateHoverStatus(HitTarget target, int linkBlockIndex);

	// -------------------------------------------------------------------------
	// Input handling
	// -------------------------------------------------------------------------
	static void handleToolbarAction(int widgetId);
	static void handleDocumentClick(HitTarget target, int linkBlockIndex);
	static void handleMouseInput(int x, int y, int button, const std::string& action);
	static bool resetJavaScriptRealmForNavigation();
	static void executeJavaScriptDocumentScripts();
	static bool dispatchJavaScriptClick(int blockIndex);
	static void recordJavaScriptError(const std::string& phase,
		gxos::javascript::RuntimeErrorCode error);
	static void handleKeyPress(int keyCode, const std::string& action);
	static void focusDocumentInput(int blockIndex,
		gxos::web::FormFocusOrigin origin = gxos::web::FormFocusOrigin::ProgrammaticInternalSmoke);
	static void blurDocumentInput();
	static void submitFormForBlock(int blockIndex);
	static void focusNextFormControl(bool reverse);
	static bool isFocusableFormControl(const DocBlock& block);
	static bool isFocusedFormControl(const DocBlock& block);
	static int focusedFormControlBlockIndex();
	static size_t buildFormFocusOrder(std::array<int, gxos::web::kFormRuntimeControlCap>& order);
	static void clearKeyboardActivationState();
	static void cancelKeyboardActivation(gxos::web::FormFocusCancellationReason reason);
	static void armKeyboardActivation(int keyCode);
	static void finishKeyboardActivation(int keyCode);
	static void clearDocumentFocus(bool recomputeStyles = true,
		gxos::web::FormFocusCancellationReason reason = gxos::web::FormFocusCancellationReason::StateChange);
	static bool ensureFocusedControlStillValid();
	static void revealFocusedFormControl(int blockIndex);
	static int formControlHeight(const DocBlock& block);
	static void activateFormControl(int blockIndex);
	static void initializeFormRuntimeState();
	static void updateFormAccessibilityMetadata();
	static gxos::web::FormAccessibilityRecord* accessibilityRecordForSerial(uint64_t serial);
	static void recomputeFormControlStyles();
	static void clearMousePressState();
	static bool isRuntimeFormControl(const DocBlock& block);
	static bool isRuntimeCheckable(const DocBlock& block);
	static bool isRuntimeButton(const DocBlock& block);
	static bool runtimeChecked(const DocBlock& block);
	static bool runtimeDisabled(const DocBlock& block);
	static gxos::web::FormRuntimeControlState* runtimeStateForBlock(DocBlock& block);
	static const gxos::web::FormRuntimeControlState* runtimeStateForBlock(const DocBlock& block);
	static int findBlockById(const std::string& id, bool labelOnly);
	static uint64_t associatedControlSerialForLabel(const DocBlock& label);
	static int blockIndexForControlSerial(uint64_t serial);
	static bool radioGroupMatches(const DocBlock& left, const DocBlock& right);
	static bool activateLabelBlock(int blockIndex);
	static bool smokeClickBlock(int blockIndex, bool label);
	static void openFindMode();
	static void closeFindMode();
	static void updateFindMatches(bool keepCurrent);
	static void goToFindMatch(int direction);
	static std::string findMatchStatusText();
	static std::string searchableTextForBlock(const DocBlock& block);
	static bool isSelectableBlock(const DocBlock& block);
	static void clearSelection();
	static void beginSelection(int x, int y);
	static void updateSelection(int x, int y);
	static void finalizeSelection(int x, int y);
	static bool hasSelection();
	static SelectionRange normalizedSelection();
	static SelectionPosition textPositionFromPoint(int x, int y, bool clampToNearest);
	static std::string selectedText();
	static void selectAllDocumentText();
	static bool copySelectionToClipboard();

	// -------------------------------------------------------------------------
	// Address bar editing
	// -------------------------------------------------------------------------
	static void focusAddressBar();   // begin editing – copies current URL into buffer
	static void blurAddressBar();    // cancel editing – restores current URL
	static void commitAddressBar();  // navigate to typed URL, then blur
	static std::string normalizeUrl(const std::string& input); // scheme normalizer
	static void storePageMetadata(NavigatorPageMetadata metadata, const WebDocument& doc);
	static NavigatorTransitionCategory transitionCategoryForUrl(const std::string& url);
	static NavigatorDocumentCategory documentCategoryForUrl(const std::string& url,
		const NavigatorPageMetadata& metadata);
	static const char* documentCategoryName(NavigatorDocumentCategory category);
	static const char* transitionCategoryName(NavigatorTransitionCategory category);
	static bool isGeneratedInspectionViewUrl(const std::string& url);
	static bool visibleDocumentOwnsInspectedSource();
	static void refreshLifecycleOwnershipEvidence();
	static void noteFocusClearedForTransition(NavigatorTransitionCategory transition, bool hadFocus);

	// -------------------------------------------------------------------------
	// Hit testing & layout helpers
	// -------------------------------------------------------------------------
	static HitTarget hitTest(int x, int y, int& outLinkBlockIndex);
	static Rect      toolbarButtonRect(int widgetId);
	static int       blockLayoutY(int blockIndex);  // Y relative to kContentY
	static Rect      linkBlockRect(int blockIndex); // absolute screen rect
	static Rect      formControlRect(int blockIndex);
	static Rect      selectableBlockRect(int blockIndex);
	static bool      inlineFragmentRectForBlock(int blockIndex, bool includeWhitespace, Rect& out);
	static bool      inlineFragmentContainsPoint(int blockIndex, int x, int y);
	static int       computeDocumentHeight();
	static int       maxScrollOffset();
	static void      clampScrollOffset();
	static void      clearScrollbarDragState();

	// -------------------------------------------------------------------------
	// State
	// -------------------------------------------------------------------------
	static uint64_t             s_windowId;
	static int                  s_scrollOffset;
	static int                  s_documentHeight;   // computed by loadUrl()
	static std::string          s_statusText;
	static std::string          s_hoverStatusText;
	static int                  s_hitLinkBlockIndex; // index of the link under the cursor
	static WebDocument          s_currentDoc;
	static gxos::javascript::RuntimeContext s_scriptRuntime;
	static gxos::javascript::NavigatorScriptHostAdapter s_scriptHostAdapter;
	static std::string          s_lastJavaScriptError;
	static WebDocument          s_inspectedDoc;
	static NavigatorPageMetadata s_pageMetadata;
	static NavigatorLifecycleDiagnostics s_lifecycleDiagnostics;
	static NavigatorDocumentCategory s_visibleDocumentCategory;
	static NavigatorDocumentCategory s_inspectedSourceCategory;
	static bool s_visibleDocumentInspectionView;
	static uint64_t s_inspectedDocumentGeneration;
	static std::string s_pendingDocumentUrl;
	static NavigatorTransitionCategory s_pendingTransitionCategory;
	static uint64_t s_staleMouseReleaseGeneration;
	static uint64_t s_staleKeyReleaseGeneration;
	// Navigation history – scheme-agnostic URL stacks.
	static std::vector<std::string> s_backStack;
	static std::vector<std::string> s_forwardStack;
	// Persistent bookmark list.
	static std::vector<Bookmark>    s_bookmarks;
	// Address bar editing state.
	static bool        s_addressFocused;   // true while user is typing
	static std::string s_addressBuffer;    // the editable text
	static int         s_addressCaret;     // insertion point index into s_addressBuffer
	static int         s_focusedInputBlockIndex;
	static int         s_inputCaret;
	static uint64_t    s_documentGeneration;
	static bool        s_tabKeyPressed;
	static std::string s_lastSubmittedFormUrl;
	static std::string s_lastSubmittedFormAction;
	static std::string s_lastSubmittedFormMethod;
	static std::string s_lastSubmittedFormStatus;
	static std::string s_lastPostHttpStatus;
	static std::string s_lastPostContentType;
	static bool        s_findActive;
	static bool        s_loading;
	static std::string s_findBuffer;
	static int         s_findCaret;
	static std::vector<FindMatch> s_findMatches;
	static int         s_currentFindMatch;
	static bool        s_ctrlPressed;
	static bool        s_shiftPressed;
	static bool        s_mouseLeftDown;
	static MouseMode   s_mouseMode;
	static HitTarget   s_mouseDownHitTarget;
	static int         s_mouseDownLinkBlockIndex;
	static std::string s_mouseDownLinkUrl;
	static int         s_mouseDownX;
	static int         s_mouseDownY;
	static int         s_mouseCurrentX;
	static int         s_mouseCurrentY;
	static bool        s_mouseDragThresholdExceeded;
	static uint64_t    s_scrollbarDragSerial;
	static ScrollbarAxis s_scrollbarDragAxis;
	static int         s_scrollbarDragGrabOffset;
	static uint64_t    s_hitScrollbarSerial;
	static ScrollbarAxis s_hitScrollbarAxis;
	static bool        s_hitScrollbarThumb;
	static bool        s_selectionBegan;
	static bool        s_selectionActive;
	static bool        s_selectionPending;
	static bool        s_selectionDragging;
	static bool        s_selectionMoved;
	static int         s_selectionStartX;
	static int         s_selectionStartY;
	static SelectionPosition s_selectionAnchor;
	static SelectionPosition s_selectionFocus;
	static std::string s_navigatorClipboard;
	static std::string s_clipboardMode;
};

} // namespace apps
} // namespace gxos
