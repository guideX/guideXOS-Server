#pragma once

#include "process.h"
#include "guide_web_document.h"   // BlockType, DocBlock, WebDocument (gxos::web)
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

struct NavigatorPageMetadata {
	std::string requestedUrl;
	std::string finalUrl;
	std::string sourceType;
	int         httpStatusCode = 0;
	std::string httpReasonPhrase;
	std::string contentType;
	std::string contentEncoding;
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
		std::string lastImageError;
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
		int         cssListRenderCount = 0;
		int         cssClampedValueCount = 0;
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
		int         cssOverflowHiddenBoxes = 0;
		int         cssOverflowAutoBoxes = 0;
		int         cssOverflowScrollDeferred = 0;
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
	static bool SmokeDragFirstLinkSelectsWithoutNavigation();
	static std::string SmokeRuntimeReport();
	static std::string SmokeLifecycleReport();
	static std::string SmokeCurrentUrl();
	static int SmokeCurrentBlockCount();
	static std::string SmokeCurrentDocumentText();
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
	};

	enum class MouseMode : uint8_t {
		None = 0,
		PotentialLinkClick,
		PotentialTextSelection,
		SelectingText,
		FormInputInteraction,
		AddressBarInteraction,
		ToolbarInteraction,
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
	static int       computeDocumentHeight();
	static int       maxScrollOffset();
	static void      clampScrollOffset();

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
