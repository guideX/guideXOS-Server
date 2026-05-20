#pragma once

#include "process.h"
#include "guide_web_document.h"   // BlockType, DocBlock, WebDocument (gxos::web)
#include <cstdint>
#include <string>
#include <vector>

namespace gxos {
namespace apps {

// =============================================================================
// Document model – provided by the reusable guideWeb layer (guide_web_document.h).
// Pull the types into this namespace so Navigator code is unaffected.
// =============================================================================

using gxos::web::BlockType;
using gxos::web::DocBlock;
using gxos::web::WebDocument;

// =============================================================================
// Bookmark – a named navigation target persisted by Navigator
// =============================================================================

struct Bookmark {
	std::string title;
	std::string url;
};

// =============================================================================
// Navigator – first-class guideXOS app
// =============================================================================

class Navigator {
public:
	static uint64_t Launch();

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
		Link,   // any Link block; s_hitLinkBlockIndex carries the index
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
	// Load a file:// URL and convert the raw text to a WebDocument.
	// Returns an error document if the file cannot be read.
	static WebDocument loadFileUrl(const std::string& url);
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
	static void handleKeyPress(int keyCode, const std::string& action);

	// -------------------------------------------------------------------------
	// Hit testing & layout helpers
	// -------------------------------------------------------------------------
	static HitTarget hitTest(int x, int y, int& outLinkBlockIndex);
	static Rect      toolbarButtonRect(int widgetId);
	static int       blockLayoutY(int blockIndex);  // Y relative to kContentY
	static Rect      linkBlockRect(int blockIndex); // absolute screen rect
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
	// Navigation history – scheme-agnostic URL stacks.
	static std::vector<std::string> s_backStack;
	static std::vector<std::string> s_forwardStack;
	// Persistent bookmark list.
	static std::vector<Bookmark>    s_bookmarks;
};

} // namespace apps
} // namespace gxos
