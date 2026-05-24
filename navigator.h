#pragma once

#include "process.h"
#include "guide_web_document.h"   // BlockType, DocBlock, WebDocument (gxos::web)
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
	bool        redirected = false;
	int         redirectCount = 0;
	std::string errorStatus;
	std::string rawSource;
	size_t      rawSourceBytes = 0;
	bool        rawSourceTruncated = false;
	int         documentBlockCount = 0;
	int         imageBlockCount = 0;
	int         loadedImageCount = 0;
	int         failedImageCount = 0;
	int         remoteImageCount = 0;
	int         localImageCount = 0;
	std::string lastImageError;
	bool        cssDetected = false;
	int         styleRuleCount = 0;
	int         unsupportedExternalStylesheetCount = 0;
	int         unsupportedCssDeclarationCount = 0;
	bool        cssStyleBlockCapped = false;
	size_t      cssStyleBytesProcessed = 0;
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
	bool        unsupportedFormMethod = false;
	bool        unsupportedFormEncoding = false;
	bool        postSupportedHosted = true;
	bool        postSupportedBareMetal = false;
	std::string lastSubmittedFormUrl;
	std::string lastSubmittedFormMethod;
	std::string lastSubmittedFormStatus;
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
	static bool SmokeSubmitFirstForm(const std::string& value);
	static int SmokeFindInPage(const std::string& query);
	static bool SmokeClickFirstLink();
	static bool SmokeDragFirstLinkSelectsWithoutNavigation();
	static std::string SmokeRuntimeReport();
	static std::string SmokeCurrentUrl();
	static int SmokeCurrentBlockCount();

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
	static void loadUrl(const std::string& url);

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

	// -------------------------------------------------------------------------
	// Rendering
	// -------------------------------------------------------------------------
	static void updateDisplay();
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
	static void focusDocumentInput(int blockIndex);
	static void blurDocumentInput();
	static void submitFormForBlock(int blockIndex);
	static void focusNextFormControl(bool reverse);
	static bool isFocusableFormControl(const DocBlock& block);
	static int formControlHeight(const DocBlock& block);
	static void activateFormControl(int blockIndex);
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
	static NavigatorPageMetadata s_pageMetadata;
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
	static std::string s_lastSubmittedFormUrl;
	static std::string s_lastSubmittedFormMethod;
	static std::string s_lastSubmittedFormStatus;
	static bool        s_findActive;
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
