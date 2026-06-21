#include "navigator.h"

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

uint64_t           Navigator::s_windowId        = 0;
int                Navigator::s_scrollOffset    = 0;
int                Navigator::s_documentHeight  = 0;
std::string        Navigator::s_statusText      = "Ready";
std::string        Navigator::s_hoverStatusText;
int                Navigator::s_hitLinkBlockIndex = -1;
WebDocument        Navigator::s_currentDoc;
WebDocument        Navigator::s_inspectedDoc;
NavigatorPageMetadata Navigator::s_pageMetadata;
std::vector<std::string> Navigator::s_backStack;
std::vector<std::string> Navigator::s_forwardStack;
std::vector<Bookmark>    Navigator::s_bookmarks;
static std::vector<DownloadItem> s_recentDownloads;
bool        Navigator::s_addressFocused = false;
std::string Navigator::s_addressBuffer;
int         Navigator::s_addressCaret   = 0;
int         Navigator::s_focusedInputBlockIndex = -1;
int         Navigator::s_inputCaret = 0;
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

static std::string extractDocumentText(const WebDocument& doc);

namespace {
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
		std::ostringstream oss;
		oss << windowId << "|" << x << "|" << y << "|" << w << "|" << h << "|" << r << "|" << g << "|" << b;
		publish(MsgType::MT_DrawRect, oss.str());
	}

	void drawTextAt(uint64_t windowId, int x, int y, const std::string& text)
	{
		publish(MsgType::MT_DrawTextAt, packDrawTextAt(windowId, x, y, text));
	}

	void drawTextAtColored(uint64_t windowId, int x, int y, const std::string& text, int r, int g, int b)
	{
		publish(MsgType::MT_DrawTextAtColor, packDrawTextAtColor(windowId, x, y,
			static_cast<uint8_t>(std::max(0, std::min(255, r))),
			static_cast<uint8_t>(std::max(0, std::min(255, g))),
			static_cast<uint8_t>(std::max(0, std::min(255, b))),
			text));
	}

	void drawTextAtStyled(uint64_t windowId, int x, int y, const std::string& text, const WebStyle& style)
	{
		if (style.hasColor) {
			int r = static_cast<int>((style.color >> 16) & 0xFFu);
			int g = static_cast<int>((style.color >> 8) & 0xFFu);
			int b = static_cast<int>(style.color & 0xFFu);
			if (style.italic) {
				r = std::max(0, r - 12);
				g = std::max(0, g - 12);
				b = std::max(0, b - 12);
			}
			if (style.bold) {
				drawTextAtColored(windowId, x + 1, y, text, r, g, b);
			}
			if (style.italic) {
				drawTextAtColored(windowId, x, y + 1, text, r, g, b);
			}
			drawTextAtColored(windowId, x, y, text, r, g, b);
			return;
		}
		if (style.bold) {
			drawTextAt(windowId, x + 1, y, text);
		}
		if (style.italic) {
			drawTextAt(windowId, x, y + 1, text);
		}
		drawTextAt(windowId, x, y, text);
	}

	void drawImage(uint64_t windowId, int x, int y, int w, int h, const std::string& path)
	{
		publish(MsgType::MT_DrawImage, packDrawImage(windowId, x, y, w, h, path));
	}

	void drawAnimatedImage(uint64_t windowId, int x, int y, int w, int h, const std::string& pathPattern)
	{
		publish(MsgType::MT_DrawImageAnimated, packDrawImage(windowId, x, y, w, h, pathPattern));
	}

	void addButton(uint64_t windowId, int id, int x, int y, int w, int h, const std::string& text, const std::string& iconPath = {})
	{
		publish(MsgType::MT_WidgetAdd, packWidgetAdd(windowId, 1, id, x, y, w, h, text));
		if (!iconPath.empty()) publish(MsgType::MT_WidgetSetIcon, packWidgetSetIcon(windowId, id, iconPath));
		// Track registered widget IDs for smoke/diagnostic access.
		auto& ids = Navigator::s_registeredWidgetIds;
		if (std::find(ids.begin(), ids.end(), id) == ids.end())
			ids.push_back(id);
	}

	int chromeLineHeight()
	{
		return SystemFont::MeasureHeight(FontRole::Default);
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

	// Number of pixel rows occupied by a block (based on wrapped line count).
	// wrapCols: max chars per line for the block type.
	static int wrappedBlockHeight(const std::string& text, int wrapCols, bool isPre = false, int lineHeight = kLineH)
	{
		if (isPre) {
			return static_cast<int>(splitPreLines(text).size()) * lineHeight;
		}
		int lines = static_cast<int>(wrapText(text, wrapCols).size());
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
	static void imageDisplaySize(const DocBlock& block, int& outW, int& outH);
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
	static int blockOuterWidth(const DocBlock& block, int availableWidth);
	static int blockOuterX(const DocBlock& block, const WebDocument& doc, int availableWidth, int outerWidth);
	static int blockWrapWidth(const DocBlock& block, int outerWidth);
	static int blockTextLineHeight(const DocBlock& block);
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
		int borderBottom = 0;
		int lineHeight = 0;
		std::vector<int> columnWidthsChars;
		std::vector<TableRowLayout> rows;
		bool fallbackUsed = false;
	};

	static bool isFirstTableCellInGroup(const WebDocument& doc, int index);
	static int tableGroupStartIndex(const WebDocument& doc, int index);
	static TableGroupLayout buildTableGroupLayout(const WebDocument& doc, int startIndex);

	static void fillDocumentCounts(NavigatorPageMetadata& metadata, const WebDocument& doc)
	{
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
		metadata.cssLineBreakCount = doc.cssDiagnostics.lineBreakCount;
		metadata.cssTableCaptionCount = 0;
		metadata.cssTableHeaderCellCount = 0;
		metadata.cssVisitedLinkCount = 0;
		metadata.formCount = doc.formsDiagnostics.formCount;
		metadata.formInputCount = doc.formsDiagnostics.textInputCount;
		metadata.formCheckboxCount = doc.formsDiagnostics.checkboxCount;
		metadata.formRadioCount = doc.formsDiagnostics.radioCount;
		metadata.formTextareaCount = doc.formsDiagnostics.textareaCount;
		metadata.formSelectCount = doc.formsDiagnostics.selectCount;
		metadata.unsupportedFormControlCount = doc.formsDiagnostics.unsupportedControlCount;
		metadata.unsupportedFormMethod = doc.formsDiagnostics.hasUnsupportedMethod;
		metadata.unsupportedFormEncoding = doc.formsDiagnostics.hasUnsupportedEncoding;
		metadata.postSupportedHosted = true;
		metadata.postSupportedBareMetal = false;

		std::vector<uint64_t> seenTableSerials;
		std::vector<uint64_t> seenTableRowSerials;
		for (size_t i = 0; i < doc.blocks.size(); ++i) {
			const DocBlock& block = doc.blocks[i];
			if (block.style.displayNone) {
				++metadata.cssDisplayNoneBlockCount;
				continue;
			}
			if (blockHasWrapperAncestor(block)) {
				++metadata.cssWrapperRenderCount;
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
			if (block.type == BlockType::ListItem) {
				++metadata.cssListRenderCount;
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
			const ImageInfo& info = imageInfoForBlock(block);
			if (info.ok) {
				++metadata.loadedImageCount;
			} else {
				++metadata.failedImageCount;
				if (metadata.lastImageError.empty()) {
					metadata.lastImageError = info.errorDetail.empty() ? info.message : info.errorDetail;
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
		return style.marginTop != -1 ? (style.marginTop == -2 ? 0 : style.marginTop) : fallbackValue;
	}

	static int cssMarginBottomPx(const WebStyle& style, int fallbackValue)
	{
		return style.marginBottom != -1 ? (style.marginBottom == -2 ? 0 : style.marginBottom) : fallbackValue;
	}

	static int cssMarginLeftPx(const WebStyle& style, int fallbackValue)
	{
		return style.marginLeft != -1 ? (style.marginLeft == -2 ? 0 : style.marginLeft) : fallbackValue;
	}

	static int cssMarginRightPx(const WebStyle& style, int fallbackValue)
	{
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

	static int cssWidthPx(const WebStyle& style, int availableWidth, int fallbackValue)
	{
		if (style.widthPercent >= 0) {
			return std::max(0, availableWidth * style.widthPercent / 100);
		}
		if (style.width > 0) return style.width;
		return fallbackValue;
	}

	static int cssMaxWidthPx(const WebStyle& style, int availableWidth, int fallbackValue)
	{
		int value = fallbackValue;
		if (style.maxWidthPercent >= 0) {
			value = std::max(0, availableWidth * style.maxWidthPercent / 100);
		} else if (style.maxWidth > 0) {
			value = style.maxWidth;
		}
		return value;
	}

	static int blockIndentForType(BlockType type)
	{
		if (type == BlockType::ListItem) return kDocumentListIndent;
		if (type == BlockType::Preformatted) return kDocumentPreIndent;
		return kDocumentIndent;
	}

	static int cssBorderTopPx(const WebStyle& style)
	{
		return style.hasBorderTop ? std::max(1, style.borderTopWidth) : 0;
	}

	static int cssBorderBottomPx(const WebStyle& style)
	{
		return style.hasBorderBottom ? std::max(1, style.borderBottomWidth) : 0;
	}

	static bool cssListStyleNone(const WebStyle& style)
	{
		return style.listStyleNone;
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

	static int blockListTextInsetPx(const DocBlock& block)
	{
		if (cssListStyleNone(block.style)) return 0;
		return blockIsOrderedListItem(block) ? 4 * kCharW : 2 * kCharW;
	}

	static std::string blockListMarkerText(const DocBlock& block, int ordinal)
	{
		if (cssListStyleNone(block.style)) return "";
		if (blockIsOrderedListItem(block)) {
			return std::to_string(std::max(1, ordinal)) + ".";
		}
		return "-";
	}

	static int blockListOrdinal(const WebDocument& doc, int blockIndex)
	{
		if (blockIndex < 0 || blockIndex >= static_cast<int>(doc.blocks.size())) return 1;
		const DocBlock& block = doc.blocks[static_cast<size_t>(blockIndex)];
		if (!blockIsOrderedListItem(block)) return 1;
		const std::string signature = blockListContainerSignature(block);
		int ordinal = 0;
		for (int i = 0; i <= blockIndex && i < static_cast<int>(doc.blocks.size()); ++i) {
			const DocBlock& candidate = doc.blocks[static_cast<size_t>(i)];
			if (candidate.type != BlockType::ListItem) continue;
			if (!blockIsOrderedListItem(candidate)) continue;
			if (blockListContainerSignature(candidate) == signature) {
				++ordinal;
			}
		}
		return std::max(1, ordinal);
	}

	static bool isWrapperTagName(const std::string& tagName)
	{
		const std::string tag = toLowerAscii(tagName);
		return tag == "main" || tag == "article" || tag == "nav" || tag == "aside" ||
			tag == "header" || tag == "footer" || tag == "section" || tag == "div";
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
		layout.borderBottom = cssBorderBottomPx(first.style);
		layout.lineHeight = blockTextLineHeight(first);

		int i = startIndex;
		while (i < static_cast<int>(doc.blocks.size())) {
			const DocBlock& block = doc.blocks[static_cast<size_t>(i)];
			if (!isTableCellLikeBlock(block) || tableSerialForBlock(block) != layout.tableSerial) break;
			TableRowLayout row;
			row.rowSerial = tableRowSerialForBlock(block);
			row.headerRow = false;
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
				cellLayout.lines = wrapText(cell.text, cellLayout.contentWidthChars);
				row.headerRow = row.headerRow || toLowerAscii(cell.tagName) == "th" || cell.style.bold;
				row.cells.push_back(std::move(cellLayout));
				++j;
			}
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

		const int separatorChars = columnCount > 0 ? (3 * (columnCount - 1)) : 0;
		const int availableChars = std::max(8, (layout.outerWidth - layout.paddingLeft - layout.paddingRight) / kCharW);
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
				cell.lines = wrapText(cell.block->text, contentWidth);
				maxLines = std::max(maxLines, static_cast<int>(cell.lines.size()));
			}
			row.heightPx = std::max(layout.lineHeight + 4, maxLines * layout.lineHeight + layout.paddingTop + layout.paddingBottom);
		}

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
		const int baseWidth = kContentW - blockIndentForType(block.type) - kDocumentRightPad
			- blockBodyMarginLeft(doc) - blockBodyMarginRight(doc)
			- cssMarginLeftPx(block.style, 0) - cssMarginRightPx(block.style, 0);
		return std::max(1, baseWidth);
	}

	static int blockOuterWidth(const DocBlock& block, int availableWidth)
	{
		int outerWidth = cssWidthPx(block.style, availableWidth, availableWidth);
		outerWidth = std::min(outerWidth, cssMaxWidthPx(block.style, availableWidth, outerWidth));
		return std::max(1, std::min(outerWidth, availableWidth));
	}

	static int blockOuterX(const DocBlock& block, const WebDocument& doc, int availableWidth, int outerWidth)
	{
		const int baseX = kContentX + blockIndentForType(block.type) + blockBodyMarginLeft(doc);
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
		return std::max(1, outerWidth - paddingLeft - paddingRight);
	}

	static int blockTextLineHeight(const DocBlock& block)
	{
		const int fontSize = cssFontSizeOrDefault(block.style, kLineH);
		const int lineHeight = cssLineHeightOrDefault(block.style, fontSize + 4);
		return std::max(fontSize + 4, std::max(kLineH, lineHeight));
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
		if (block.type == BlockType::FormTextarea) {
			int rows = block.visibleRows > 0 ? block.visibleRows : 4;
			rows = std::max(kTextareaMinRows, std::min(kTextareaMaxRows, rows));
			return std::max(kFormControlH, rows * kLineH + 10);
		}
		return kFormControlH;
	}

	static int blockTotalHeight(const DocBlock& block, const WebDocument& doc, bool nextIsHeading)
	{
		if (block.style.displayNone) return 0;
		if (isTableCellLikeBlock(block)) {
			if (!isFirstTableCellInGroup(doc, static_cast<int>(&block - &doc.blocks.front()))) {
				return 0;
			}
			const int blockIndex = static_cast<int>(&block - &doc.blocks.front());
			const int groupStart = tableGroupStartIndex(doc, blockIndex);
			const TableGroupLayout layout = buildTableGroupLayout(doc, groupStart);
			const uint64_t rowSerial = tableRowSerialForBlock(block);
			int rowHeight = layout.lineHeight + 4;
			for (const TableRowLayout& row : layout.rows) {
				if (row.rowSerial == rowSerial) {
					rowHeight = row.heightPx;
					break;
				}
			}
			const int blockMarginTop = cssMarginTopPx(block.style, 4);
			const int blockMarginBottom = cssMarginBottomPx(block.style, 8);
			int total = blockMarginTop + cssBorderTopPx(block.style) + rowHeight + cssBorderBottomPx(block.style) + blockMarginBottom;
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
		const int blockMarginLeft = cssMarginLeftPx(block.style, 0);
		const int blockMarginRight = cssMarginRightPx(block.style, 0);
		const int paddingTop = cssPaddingTopPx(block.style, block.type == BlockType::Preformatted ? 4 : 0);
		const int paddingRight = cssPaddingRightPx(block.style, block.type == BlockType::Preformatted ? 4 : 0);
		const int paddingBottom = cssPaddingBottomPx(block.style, block.type == BlockType::Preformatted ? 4 : 0);
		const int paddingLeft = cssPaddingLeftPx(block.style, block.type == BlockType::Preformatted ? 4 : 0);
		const int borderTop = cssBorderTopPx(block.style);
		const int borderBottom = cssBorderBottomPx(block.style);
		const int bodyMarginLeft = blockBodyMarginLeft(doc);
		const int bodyMarginRight = blockBodyMarginRight(doc);
		const int availableWidth = std::max(1, kContentW - blockIndentForType(block.type) - kDocumentRightPad
			- bodyMarginLeft - bodyMarginRight - blockMarginLeft - blockMarginRight);
		const int outerWidth = blockOuterWidth(block, availableWidth);
		const int innerWidth = std::max(1, outerWidth - paddingLeft - paddingRight);
		const int listInset = block.type == BlockType::ListItem ? blockListTextInsetPx(block) : 0;
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
			contentH = wrappedBlockHeight(block.text, wrapCols, false, lineHeight);
			break;
		case BlockType::ListItem:
			contentH = wrappedBlockHeight(block.text, wrapCols, false, lineHeight);
			break;
		case BlockType::Preformatted:
			contentH = wrappedBlockHeight(block.text, wrapCols, true, lineHeight);
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
			imageDisplaySize(block, imageW, imageH);
			contentH = imageH;
			break;
		}
		}
		int total = blockMarginTop + borderTop + paddingTop + contentH + paddingBottom + borderBottom + blockMarginBottom;
		if (nextIsHeading) total += kPreGapIfNextHeading;
		return total;
	}

	static void drawBlockBox(uint64_t windowId, int x, int y, int w, int h, const WebStyle& style)
	{
		if (w <= 0 || h <= 0) return;
		if (style.hasBackgroundColor) {
			int r = 245, g = 247, b = 250;
			colorChannels(style.backgroundColor, r, g, b);
			drawRect(windowId, x, y, w, h, r, g, b);
		}
		if (style.hasBorderTop && style.borderTopWidth > 0) {
			int r = 24, g = 28, b = 36;
			colorChannels(style.borderTopColor, r, g, b);
			drawRect(windowId, x, y, w, std::max(1, style.borderTopWidth), r, g, b);
		}
		if (style.hasBorderBottom && style.borderBottomWidth > 0) {
			int r = 24, g = 28, b = 36;
			colorChannels(style.borderBottomColor, r, g, b);
			drawRect(windowId, x, y + std::max(0, h - std::max(1, style.borderBottomWidth)), w,
				std::max(1, style.borderBottomWidth), r, g, b);
		}
	}

	static bool blockHasVisibleCss(const DocBlock& block)
	{
		return !block.style.displayNone;
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
			{"Capabilities", "External stylesheets", "unsupported"},

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

	static void imageDisplaySize(const DocBlock& block, int& outW, int& outH)
	{
		constexpr int kImageMaxW = kContentW - 36;
		constexpr int kImageMaxH = kContentH - 20;
		const ImageInfo& info = imageInfoForBlock(block);
		int naturalW = info.ok ? info.naturalW : 220;
		int naturalH = info.ok ? info.naturalH : 64;
		if (naturalW <= 0) naturalW = 220;
		if (naturalH <= 0) naturalH = 64;

		int drawW = block.width > 0 ? block.width : naturalW;
		int drawH = block.height > 0 ? block.height : naturalH;
		if (block.width > 0 && block.height <= 0) {
			drawH = std::max(1, (drawW * naturalH) / naturalW);
		} else if (block.height > 0 && block.width <= 0) {
			drawW = std::max(1, (drawH * naturalW) / naturalH);
		}
		if (block.style.width > 0) {
			drawW = block.style.width;
			if (block.height <= 0) {
				drawH = std::max(1, (drawW * naturalH) / naturalW);
			}
		} else if (block.style.widthPercent >= 0) {
			drawW = std::max(1, kImageMaxW * block.style.widthPercent / 100);
			if (block.height <= 0) {
				drawH = std::max(1, (drawW * naturalH) / naturalW);
			}
		}
		if (block.style.maxWidth > 0 && drawW > block.style.maxWidth) {
			drawW = block.style.maxWidth;
			if (block.height <= 0) {
				drawH = std::max(1, (drawW * naturalH) / naturalW);
			}
		}
		if (block.style.maxWidthPercent >= 0) {
			const int cssMaxW = std::max(1, kImageMaxW * block.style.maxWidthPercent / 100);
			if (drawW > cssMaxW) {
				drawW = cssMaxW;
				if (block.height <= 0) {
					drawH = std::max(1, (drawW * naturalH) / naturalW);
				}
			}
		}

		if (drawW > kImageMaxW) {
			drawH = std::max(1, (drawH * kImageMaxW) / drawW);
			drawW = kImageMaxW;
		}
		if (drawH > kImageMaxH) {
			// TODO: replace nearest-neighbor compositor scaling with higher-quality scaling.
			drawW = std::max(1, (drawW * kImageMaxH) / drawH);
			drawH = kImageMaxH;
		}
		outW = std::max(1, drawW);
		outH = std::max(1, drawH);
	}

	static std::string imagePlaceholderText(const DocBlock& block, const ImageInfo& info)
	{
		if (!block.alt.empty()) return block.alt;
		if (!block.text.empty()) return block.text;
		return info.message.empty() ? "[missing image]" : info.message;
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
	loadUrl(url);
	return s_currentDoc.url == url;
}

bool Navigator::SmokeNavigateToQuiet(const std::string& url)
{
	if (s_windowId == 0) return false;
	loadUrl(url, false);
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
		const int x = r.x + std::min(std::max(2, r.w / 4), std::max(2, r.w - 2));
		const int y = r.y + std::min(8, std::max(1, r.h - 1));
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
		const int x1 = r.x + std::min(std::max(2, r.w / 4), std::max(2, r.w - 2));
		const int y1 = r.y + std::min(8, std::max(1, r.h - 1));
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
	const std::string inspected = s_pageMetadata.finalUrl.empty() ? "" : s_pageMetadata.finalUrl;
	return formatRuntimeReport(hostedRuntimeReportEntries(
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

std::string Navigator::SmokeCurrentLinkUrl(const std::string& text)
{
	for (const DocBlock& block : s_currentDoc.blocks) {
		if (block.type == BlockType::Link && block.text == text) {
			return block.url;
		}
	}
	return "";
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
	loadUrl("about:navigator");

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

void Navigator::updateDisplay()
{
	if (s_windowId == 0) return;

	// Window title tracks the current document title.
	const std::string winTitle = s_currentDoc.title.empty()
		? "guideXOS Navigator"
		: s_currentDoc.title + " - guideXOS Navigator";
	publish(MsgType::MT_SetTitle, std::to_string(s_windowId) + "|" + winTitle);
	publish(MsgType::MT_DrawText, std::to_string(s_windowId) + "|\f");

	drawRect(s_windowId, 0, 0, kWindowW, kWindowH, 25, 29, 38);
	renderToolbar();
	renderDocument();
	renderStatusBar();
}

void Navigator::renderToolbar()
{
	static const char* kIconRoot = "assets/Images/NuoveXT/PNG/32/";
	drawRect(s_windowId, 0, 0, kWindowW, kToolbarH, 42, 46, 58);
	drawRect(s_windowId, 0, kToolbarH - 1, kWindowW, 1, 78, 86, 108);

	addButton(s_windowId, kWidgetIdBack, 20, kButtonY, kButtonW, kButtonH, "Back", std::string(kIconRoot) + "above_thearrow_10194.png");
	addButton(s_windowId, kWidgetIdForward, 20 + (kButtonW + kButtonGap), kButtonY, kButtonW, kButtonH, "Next", std::string(kIconRoot) + "Next_arrow_10211.png");
	addButton(s_windowId, kWidgetIdReload, 20 + 2 * (kButtonW + kButtonGap), kButtonY, kButtonW, kButtonH, "Reload", std::string(kIconRoot) + "refresh_arrow_10190.png");
	addButton(s_windowId, kWidgetIdHome, 20 + 3 * (kButtonW + kButtonGap), kButtonY, kButtonW, kButtonH, "Home", std::string(kIconRoot) + "gohome_action_ir_10235.png");
	addButton(s_windowId, kWidgetIdBookmarks, 20 + 4 * (kButtonW + kButtonGap), kButtonY, kButtonW, kButtonH, "Marks", std::string(kIconRoot) + "markers_list_add_favorites_10275.png");
	addButton(s_windowId, kWidgetIdAddBookmark, 20 + 5 * (kButtonW + kButtonGap), kButtonY, kButtonW, kButtonH, "Add", std::string(kIconRoot) + "edit_add_10261.png");
	addButton(s_windowId, kWidgetIdFind, 20 + 6 * (kButtonW + kButtonGap), kButtonY, kButtonW, kButtonH, "Find");

	drawRect(s_windowId, kAddressX, kAddressY, kAddressW, kAddressH, 18, 22, 30);
	if (s_addressFocused) {
		// Focused: bright blue border on all four sides
		drawRect(s_windowId, kAddressX,                 kAddressY,                 kAddressW, 1, 80, 140, 220);
		drawRect(s_windowId, kAddressX,                 kAddressY + kAddressH - 1, kAddressW, 1, 80, 140, 220);
		drawRect(s_windowId, kAddressX,                 kAddressY,                 1, kAddressH, 80, 140, 220);
		drawRect(s_windowId, kAddressX + kAddressW - 1, kAddressY,                 1, kAddressH, 80, 140, 220);

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
		drawTextAt(s_windowId, kTextX, kTextY, s_addressBuffer);
		// Draw a 1-px wide caret bar on top.
		drawRect(s_windowId, caretX, kAddressY + 4, 1, kAddressH - 8, 200, 220, 255);
		(void)before; (void)after; // reserved for future proportional split-draw
	} else {
		// Normal: subtle top/bottom border
		drawRect(s_windowId, kAddressX, kAddressY,                 kAddressW, 1, 110, 120, 142);
		drawRect(s_windowId, kAddressX, kAddressY + kAddressH - 1, kAddressW, 1,  70,  78,  96);
		drawTextAt(s_windowId, kAddressX + 10, centeredChromeTextY(kAddressY, kAddressH), s_currentDoc.url);
	}
	if (s_loading) {
		drawAnimatedImage(s_windowId, kAddressX + kAddressW - 24, kAddressY + 2, 22, 22,
			"assets/Images/SurfThrobber/PNG/surfer_{frame}.png");
	}
}

void Navigator::renderDocument()
{
	clampScrollOffset();

	// Content area background
	int pageBgR = 245;
	int pageBgG = 247;
	int pageBgB = 250;
	if (s_currentDoc.bodyStyle.hasBackgroundColor) {
		colorChannels(s_currentDoc.bodyStyle.backgroundColor, pageBgR, pageBgG, pageBgB);
	}
	drawRect(s_windowId, kContentX, kToolbarH + 6, kContentW, kContentH, pageBgR, pageBgG, pageBgB);
	drawRect(s_windowId, kContentX, kToolbarH + 6, kContentW, 1, 186, 192, 204);
	// Scroll-track slot
	drawRect(s_windowId, kContentX + kContentW - 12, kToolbarH + 6, 8, kContentH, 229, 232, 238);

	int blockIndex = 0;
	for (const DocBlock& block : s_currentDoc.blocks) {
		if (!blockHasVisibleCss(block)) {
			++blockIndex;
			continue;
		}
		int relY  = blockLayoutY(blockIndex);
		int drawY = kContentY + relY - s_scrollOffset;
		const int blockMarginTop = cssMarginTopPx(block.style, block.type == BlockType::Heading ? 10 : 4);
		const int blockMarginBottom = cssMarginBottomPx(block.style, block.type == BlockType::ListItem ? 4 : 8);
		const int blockMarginLeft = cssMarginLeftPx(block.style, 0);
		const int blockMarginRight = cssMarginRightPx(block.style, 0);
		const int paddingTop = cssPaddingTopPx(block.style, block.type == BlockType::Preformatted ? 4 : 0);
		const int paddingRight = cssPaddingRightPx(block.style, block.type == BlockType::Preformatted ? 4 : 0);
		const int paddingBottom = cssPaddingBottomPx(block.style, block.type == BlockType::Preformatted ? 4 : 0);
		const int paddingLeft = cssPaddingLeftPx(block.style, block.type == BlockType::Preformatted ? 4 : 0);
		const int borderTop = cssBorderTopPx(block.style);
		const int borderBottom = cssBorderBottomPx(block.style);
		const int lineHeight = blockTextLineHeight(block);
		const int bodyMarginLeft = blockBodyMarginLeft(s_currentDoc);
		const int bodyMarginRight = blockBodyMarginRight(s_currentDoc);
		const int availableWidth = std::max(1, kContentW - blockIndentForType(block.type) - kDocumentRightPad
			- bodyMarginLeft - bodyMarginRight - blockMarginLeft - blockMarginRight);
		const int outerWidth = blockOuterWidth(block, availableWidth);
		const int outerX = blockOuterX(block, s_currentDoc, availableWidth, outerWidth);
		const int innerWidth = std::max(1, outerWidth - paddingLeft - paddingRight);
		const int listInset = block.type == BlockType::ListItem ? blockListTextInsetPx(block) : 0;
		const int wrapCols = std::max(1, std::max(1, innerWidth - listInset) / kCharW);
		const int listWrapCols = wrapCols;
		const int preWrapCols = std::max(1, innerWidth / kCharW);
		const int headingFontSize = cssFontSizeOrDefault(block.style, block.tagName == "h1" ? 24 : (block.tagName == "h2" ? 20 : (block.tagName == "h3" ? 18 : 20)));

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
			const int rowBlockH = row.heightPx + cssBorderTopPx(block.style) + cssBorderBottomPx(block.style);
			if (drawY + rowBlockH < kContentY || drawY > kContentY + kContentH) {
				++blockIndex;
				continue;
			}
			if (s_findActive &&
				s_currentFindMatch >= 0 &&
				s_currentFindMatch < static_cast<int>(s_findMatches.size()) &&
				s_findMatches[s_currentFindMatch].blockIndex == blockIndex)
			{
				drawRect(s_windowId, kContentX + 10, drawY + std::max(0, blockMarginTop - 2),
					kContentW - 28, std::max(kLineH + 4, rowBlockH - std::max(0, blockMarginTop)),
					255, 244, 168);
			}
			SelectionRange selection = normalizedSelection();
			if (selection.valid && blockIndex >= selection.start.blockIndex && blockIndex <= selection.end.blockIndex && isSelectableBlock(block)) {
				Rect selectionRect = selectableBlockRect(blockIndex);
				if (selectionRect.w > 0 && selectionRect.h > 0) {
					drawRect(s_windowId,
						selectionRect.x - 2,
						selectionRect.y - 1,
						std::min(selectionRect.w + 4, kContentX + kContentW - 18 - (selectionRect.x - 2)),
						selectionRect.h,
						96, 146, 224);
				}
			}
			const int boxY = drawY + blockMarginTop;
			drawBlockBox(s_windowId, layout.outerX, boxY, layout.outerWidth, row.heightPx, block.style);
			const size_t lastCol = layout.columnWidthsChars.empty() ? 0 : layout.columnWidthsChars.size() - 1;
			const int rowTextY = drawY + blockMarginTop + cssBorderTopPx(block.style) + layout.paddingTop;
			const int rowTextBottom = rowTextY + row.heightPx - layout.paddingTop - layout.paddingBottom;
			int cellX = layout.outerX + layout.paddingLeft;
			int separatorX = cellX;
			for (size_t col = 0; col < row.cells.size(); ++col) {
				const TableCellLayout& cell = row.cells[col];
				const int colWidthChars = layout.columnWidthsChars[std::min(col, lastCol)];
				const int cellW = std::max(1, colWidthChars * kCharW);
				const int cellRight = cellX + cellW;
				WebStyle cellStyle = cell.block->style;
				if (row.headerRow) cellStyle.bold = true;
				if (!cell.block->url.empty()) {
					cellStyle.underline = true;
					if (!cellStyle.hasColor) {
						cellStyle.hasColor = true;
						cellStyle.color = s_visitedUrls.find(cell.block->url) != s_visitedUrls.end()
							? 0xFF6B46C1u
							: 0xFF1E5CB8u;
					}
				}
				if (cellStyle.hasBackgroundColor || cellStyle.hasBorderTop || cellStyle.hasBorderBottom) {
					drawBlockBox(s_windowId, cellX, boxY, cellW, row.heightPx, cellStyle);
				}
				int lineY = rowTextY;
				for (size_t lineIndex = 0; lineIndex < cell.lines.size(); ++lineIndex) {
					const std::string& ln = cell.lines[lineIndex];
					const int lineW = static_cast<int>(ln.size()) * kCharW;
					const int paddingLeft = std::max(1, cell.padLeftChars * kCharW);
					const int paddingRight = std::max(1, cell.padRightChars * kCharW);
					const int innerWidth = std::max(1, cellW - paddingLeft - paddingRight);
					int textX = cellX + paddingLeft;
					if (cellStyle.textAlign == TextAlign::Center) {
						textX = cellX + paddingLeft + std::max(0, (innerWidth - lineW) / 2);
					} else if (cellStyle.textAlign == TextAlign::Right) {
						textX = cellRight - paddingRight - std::min(innerWidth, lineW);
					}
					drawTextAtStyled(s_windowId, textX, lineY, ln, cellStyle);
					lineY += layout.lineHeight;
				}
				if (col < row.cells.size() - 1) {
					drawRect(s_windowId, cellRight - 1, boxY, 1, row.heightPx, 209, 214, 223);
				}
				cellX += cellW;
				separatorX = cellRight;
			}
			if (!row.cells.empty()) {
				drawRect(s_windowId, layout.outerX + layout.paddingLeft, rowTextBottom, std::max(1, separatorX - (layout.outerX + layout.paddingLeft)), 1, 209, 214, 223);
			}
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
			drawBlockBox(s_windowId, outerX, boxY, outerWidth, std::max(1, hrH - blockMarginTop - std::max(4, blockMarginBottom)), block.style);
			++blockIndex;
			continue;
		}

		// Skip blocks fully above or below the visible viewport
		int blockH = 0;
		bool nextIsHeading = (blockIndex + 1 < static_cast<int>(s_currentDoc.blocks.size()) &&
			s_currentDoc.blocks[blockIndex + 1].type == BlockType::Heading);
		constexpr int kPreGapIfNextHeading = 10;
		switch (block.type) {
		case BlockType::Heading:      blockH = blockMarginTop + borderTop + paddingTop + std::max(lineHeight + 4, headingFontSize + 2) + paddingBottom + borderBottom + std::max(4, blockMarginBottom); break;
		case BlockType::Paragraph:    blockH = blockMarginTop + borderTop + paddingTop + wrappedBlockHeight(block.text, wrapCols, false, lineHeight) + paddingBottom + borderBottom + std::max(4, blockMarginBottom) + (nextIsHeading ? kPreGapIfNextHeading : 0); break;
		case BlockType::Link:         blockH = blockMarginTop + borderTop + paddingTop + wrappedBlockHeight(block.text, wrapCols, false, lineHeight) + paddingBottom + borderBottom + std::max(4, blockMarginBottom) + (nextIsHeading ? kPreGapIfNextHeading : 0); break;
		case BlockType::ListItem:     blockH = blockMarginTop + borderTop + paddingTop + wrappedBlockHeight(block.text, listWrapCols, false, lineHeight) + paddingBottom + borderBottom + std::max(4, blockMarginBottom) + (nextIsHeading ? kPreGapIfNextHeading : 0); break;
		case BlockType::Preformatted: blockH = blockMarginTop + borderTop + paddingTop + wrappedBlockHeight(block.text, preWrapCols, true, lineHeight) + paddingBottom + borderBottom + std::max(4, blockMarginBottom) + (nextIsHeading ? kPreGapIfNextHeading : 0); break;
		case BlockType::FormTextInput:
		case BlockType::FormCheckbox:
		case BlockType::FormRadio:
		case BlockType::FormTextarea:
		case BlockType::FormSelect:
		case BlockType::FormSubmit:   blockH = blockMarginTop + borderTop + paddingTop + formControlHeight(block) + paddingBottom + borderBottom + std::max(6, blockMarginBottom); break;
		case BlockType::Image: {
			int imageW = 0;
			int imageH = 0;
			imageDisplaySize(block, imageW, imageH);
			blockH = blockMarginTop + borderTop + paddingTop + imageH + paddingBottom + borderBottom + std::max(4, blockMarginBottom);
			break;
		}
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
			drawRect(s_windowId, kContentX + 10, drawY + std::max(0, blockMarginTop - 2),
				kContentW - 28, std::max(kLineH + 4, blockH - std::max(0, blockMarginTop)),
				255, 244, 168);
		}

		SelectionRange selection = normalizedSelection();
		if (selection.valid && blockIndex >= selection.start.blockIndex && blockIndex <= selection.end.blockIndex && isSelectableBlock(block)) {
			Rect selectionRect = selectableBlockRect(blockIndex);
			if (selectionRect.w > 0 && selectionRect.h > 0) {
				drawRect(s_windowId,
					selectionRect.x - 2,
					selectionRect.y - 1,
					std::min(selectionRect.w + 4, kContentX + kContentW - 18 - (selectionRect.x - 2)),
					selectionRect.h,
					96, 146, 224);
			}
		}

		const int boxY = drawY + blockMarginTop;
		const int boxH = std::max(1, blockH - blockMarginTop - std::max(4, blockMarginBottom));
		drawBlockBox(s_windowId, outerX, boxY, outerWidth, boxH, block.style);

		switch (block.type) {
		case BlockType::Heading:
			// Slightly larger heading: draw a subtle accent bar then the text
			drawRect(s_windowId, outerX + paddingLeft, boxY + borderTop + paddingTop + std::max(lineHeight, headingFontSize - 4),
				std::max(1, innerWidth), 2, 80, 140, 220);
			drawTextAtStyled(s_windowId, blockTextX(block, outerX + paddingLeft, innerWidth, std::min(static_cast<int>(block.text.size()) * kCharW, innerWidth)), drawY + blockMarginTop + borderTop + paddingTop, block.text, block.style);
			if (block.style.bold) {
				drawTextAtStyled(s_windowId, blockTextX(block, outerX + paddingLeft + 1, innerWidth, std::min(static_cast<int>(block.text.size()) * kCharW, innerWidth)), drawY + blockMarginTop + borderTop + paddingTop, block.text, block.style);
			}
			break;

		case BlockType::Paragraph: {
			auto lines = wrapText(block.text, wrapCols);
			int lineY = drawY + blockMarginTop + borderTop + paddingTop;
			for (const std::string& ln : lines) {
				const int lineW = static_cast<int>(ln.size()) * kCharW;
				drawTextAtStyled(s_windowId, blockTextX(block, outerX + paddingLeft, innerWidth, lineW), lineY, ln, block.style);
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
				drawTextAtStyled(s_windowId, outerX + paddingLeft, drawY + blockMarginTop + borderTop + paddingTop, marker, block.style);
			}
			const int textInset = blockListTextInsetPx(block);
			auto lines = wrapText(block.text, listWrapCols);
			int lineY = drawY + blockMarginTop + borderTop + paddingTop;
			for (const std::string& ln : lines) {
				const int lineW = static_cast<int>(ln.size()) * kCharW;
				drawTextAtStyled(s_windowId, blockTextX(block, outerX + paddingLeft + textInset, std::max(1, innerWidth - textInset), lineW), lineY, ln, block.style);
				lineY += lineHeight;
			}
			break;
		}

		case BlockType::Preformatted: {
			// Draw each line preserving exact content
			auto lines = splitPreLines(block.text);
			int lineY = drawY + blockMarginTop + borderTop + paddingTop;
			for (const std::string& ln : lines) {
				drawTextAtStyled(s_windowId, outerX + paddingLeft, lineY, ln, block.style);
				lineY += lineHeight;
			}
			break;
		}

		case BlockType::Link: {
			// Full wrapped link block: underline + blue text
			// The entire bounding rect is clickable (TODO: per-line hit testing).
			auto lines = wrapText(block.text, wrapCols);
			int lineY = drawY + blockMarginTop + borderTop + paddingTop;
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
				// Underline under each line
				int lineW = static_cast<int>(ln.size()) * kCharW;
				if (block.style.underline) {
					drawRect(s_windowId, blockTextX(block, outerX + paddingLeft, innerWidth, lineW), lineY + lineHeight - 1,
						lineW, 1, linkR, linkG, linkB);
				}
				drawTextAtStyled(s_windowId, blockTextX(block, outerX + paddingLeft, innerWidth, lineW), lineY, ln, linkStyle);
				lineY += lineHeight;
			}
			break;
		}

		case BlockType::Image: {
			int imageW = 0;
			int imageH = 0;
			imageDisplaySize(block, imageW, imageH);
			const ImageInfo& info = imageInfoForBlock(block);
			const int imageX = outerX + paddingLeft;
			const int viewportTop = kContentY;
			const int viewportBottom = kToolbarH + 6 + kContentH;
			if (boxY + borderTop + paddingTop >= viewportTop && boxY + borderTop + paddingTop + imageH <= viewportBottom) {
				if (info.ok) {
					drawImage(s_windowId, imageX, boxY + borderTop + paddingTop, imageW, imageH, info.drawPath);
				} else {
					drawRect(s_windowId, imageX, boxY + borderTop + paddingTop, imageW, imageH, 232, 236, 242);
					drawRect(s_windowId, imageX, boxY + borderTop + paddingTop, imageW, 1, 145, 153, 168);
					drawRect(s_windowId, imageX, boxY + borderTop + paddingTop + imageH - 1, imageW, 1, 145, 153, 168);
					drawRect(s_windowId, imageX, boxY + borderTop + paddingTop, 1, imageH, 145, 153, 168);
					drawRect(s_windowId, imageX + imageW - 1, boxY + borderTop + paddingTop, 1, imageH, 145, 153, 168);
					drawTextAtStyled(s_windowId, imageX + 10, boxY + borderTop + paddingTop + std::max(8, (imageH - lineHeight) / 2),
						imagePlaceholderText(block, info), block.style);
				}
			}
			break;
		}

		case BlockType::FormTextInput: {
			const int inputX = outerX + paddingLeft;
			const int inputY = boxY + borderTop + paddingTop;
			const bool focused = (blockIndex == s_focusedInputBlockIndex);
			drawRect(s_windowId, inputX, inputY, kFormInputW, kFormControlH, 250, 252, 255);
			drawRect(s_windowId, inputX, inputY, kFormInputW, 1, focused ? 54 : 148, focused ? 118 : 156, focused ? 210 : 170);
			drawRect(s_windowId, inputX, inputY + kFormControlH - 1, kFormInputW, 1, focused ? 54 : 148, focused ? 118 : 156, focused ? 210 : 170);
			drawRect(s_windowId, inputX, inputY, 1, kFormControlH, focused ? 54 : 148, focused ? 118 : 156, focused ? 210 : 170);
			drawRect(s_windowId, inputX + kFormInputW - 1, inputY, 1, kFormControlH, focused ? 54 : 148, focused ? 118 : 156, focused ? 210 : 170);
			std::string text = block.inputValue;
			bool placeholder = text.empty() && !block.placeholder.empty();
			if (placeholder) text = block.placeholder;
			const int maxChars = (kFormInputW - 16) / kCharW;
			if (static_cast<int>(text.size()) > maxChars) {
				text = text.substr(text.size() - static_cast<size_t>(maxChars));
			}
			if (placeholder) {
				drawTextAtColored(s_windowId, inputX + 8, centeredChromeTextY(inputY, kFormControlH), text, 128, 136, 150);
			} else {
				drawTextAtColored(s_windowId, inputX + 8, centeredChromeTextY(inputY, kFormControlH), text, 35, 45, 60);
			}
			if (focused) {
				int caretPos = std::max(0, std::min(s_inputCaret, static_cast<int>(block.inputValue.size())));
				int visibleCaret = std::min(caretPos, maxChars);
				drawRect(s_windowId, inputX + 8 + visibleCaret * kCharW, inputY + 5, 1, kFormControlH - 10, 35, 85, 170);
			}
			break;
		}

		case BlockType::FormTextarea: {
			const int inputX = outerX + paddingLeft;
			const int inputY = boxY + borderTop + paddingTop;
			const int inputH = formControlHeight(block);
			const bool focused = (blockIndex == s_focusedInputBlockIndex);
			drawRect(s_windowId, inputX, inputY, kFormInputW, inputH, 250, 252, 255);
			drawRect(s_windowId, inputX, inputY, kFormInputW, 1, focused ? 54 : 148, focused ? 118 : 156, focused ? 210 : 170);
			drawRect(s_windowId, inputX, inputY + inputH - 1, kFormInputW, 1, focused ? 54 : 148, focused ? 118 : 156, focused ? 210 : 170);
			drawRect(s_windowId, inputX, inputY, 1, inputH, focused ? 54 : 148, focused ? 118 : 156, focused ? 210 : 170);
			drawRect(s_windowId, inputX + kFormInputW - 1, inputY, 1, inputH, focused ? 54 : 148, focused ? 118 : 156, focused ? 210 : 170);
			const bool placeholder = block.inputValue.empty() && !block.placeholder.empty();
			const std::string rawText = placeholder ? block.placeholder : block.inputValue;
			const int maxChars = (kFormInputW - 16) / kCharW;
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
				drawTextAtColored(s_windowId, inputX + 8, lineY, lineText,
					placeholder ? 128 : 35,
					placeholder ? 136 : 45,
					placeholder ? 150 : 60);
				lineY += lineHeight;
			}
			if (focused && !placeholder) {
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
					drawRect(s_windowId, inputX + 8 + visibleColumn * kCharW, caretY, 1, lineHeight - 2, 35, 85, 170);
				}
			}
			break;
		}

		case BlockType::FormCheckbox:
		case BlockType::FormRadio: {
			const int controlX = outerX + paddingLeft;
			const int controlY = boxY + borderTop + paddingTop;
			const bool focused = (blockIndex == s_focusedInputBlockIndex);
			const int box = 14;
			const int boxY = controlY + (kFormControlH - box) / 2;
			drawRect(s_windowId, controlX, boxY, box, box, 248, 250, 254);
			drawRect(s_windowId, controlX, boxY, box, 1, focused ? 54 : 110, focused ? 118 : 118, focused ? 210 : 132);
			drawRect(s_windowId, controlX, boxY + box - 1, box, 1, focused ? 54 : 110, focused ? 118 : 118, focused ? 210 : 132);
			drawRect(s_windowId, controlX, boxY, 1, box, focused ? 54 : 110, focused ? 118 : 118, focused ? 210 : 132);
			drawRect(s_windowId, controlX + box - 1, boxY, 1, box, focused ? 54 : 110, focused ? 118 : 118, focused ? 210 : 132);
			if (block.checked) {
				if (block.type == BlockType::FormRadio) {
					drawRect(s_windowId, controlX + 4, boxY + 4, box - 8, box - 8, 45, 94, 170);
				} else {
					drawTextAtColored(s_windowId, controlX + 3, boxY - 2, "x", 35, 85, 170);
				}
			}
			std::string label = block.text.empty() ? block.inputName : block.text;
			drawTextAtColored(s_windowId, controlX + box + 8, centeredChromeTextY(controlY, kFormControlH), label, 35, 45, 60);
			break;
		}

		case BlockType::FormSelect: {
			const int selectX = outerX + paddingLeft;
			const int selectY = boxY + borderTop + paddingTop;
			const bool focused = (blockIndex == s_focusedInputBlockIndex);
			drawRect(s_windowId, selectX, selectY, kFormInputW, kFormControlH, 250, 252, 255);
			drawRect(s_windowId, selectX, selectY, kFormInputW, 1, focused ? 54 : 148, focused ? 118 : 156, focused ? 210 : 170);
			drawRect(s_windowId, selectX, selectY + kFormControlH - 1, kFormInputW, 1, focused ? 54 : 148, focused ? 118 : 156, focused ? 210 : 170);
			drawRect(s_windowId, selectX, selectY, 1, kFormControlH, focused ? 54 : 148, focused ? 118 : 156, focused ? 210 : 170);
			drawRect(s_windowId, selectX + kFormInputW - 1, selectY, 1, kFormControlH, focused ? 54 : 148, focused ? 118 : 156, focused ? 210 : 170);
			std::string label = block.text.empty() ? "(select)" : block.text;
			drawTextAtColored(s_windowId, selectX + 8, centeredChromeTextY(selectY, kFormControlH), label, 35, 45, 60);
			drawTextAtColored(s_windowId, selectX + kFormInputW - 20, centeredChromeTextY(selectY, kFormControlH), "v", 70, 78, 96);
			break;
		}

		case BlockType::FormSubmit: {
			const int buttonX = outerX + paddingLeft;
			const int buttonY = boxY + borderTop + paddingTop;
			const bool focused = (blockIndex == s_focusedInputBlockIndex);
			const bool disabled = block.formUnsupported;
			drawRect(s_windowId, buttonX, buttonY, kFormSubmitW, kFormControlH, disabled ? 184 : 65, disabled ? 188 : 112, disabled ? 196 : 190);
			drawRect(s_windowId, buttonX, buttonY, kFormSubmitW, 1, focused ? 54 : (disabled ? 128 : 38), focused ? 118 : (disabled ? 132 : 78), focused ? 210 : (disabled ? 142 : 150));
			drawRect(s_windowId, buttonX, buttonY + kFormControlH - 1, kFormSubmitW, 1, focused ? 54 : (disabled ? 128 : 38), focused ? 118 : (disabled ? 132 : 78), focused ? 210 : (disabled ? 142 : 150));
			drawRect(s_windowId, buttonX, buttonY, 1, kFormControlH, focused ? 54 : (disabled ? 128 : 38), focused ? 118 : (disabled ? 132 : 78), focused ? 210 : (disabled ? 142 : 150));
			drawRect(s_windowId, buttonX + kFormSubmitW - 1, buttonY, 1, kFormControlH, focused ? 54 : (disabled ? 128 : 38), focused ? 118 : (disabled ? 132 : 78), focused ? 210 : (disabled ? 142 : 150));
			std::string label = block.submitLabel.empty() ? "Submit" : block.submitLabel;
			int labelMax = (kFormSubmitW - 14) / kCharW;
			if (static_cast<int>(label.size()) > labelMax) label = label.substr(0, static_cast<size_t>(labelMax));
			drawTextAtColored(s_windowId, buttonX + 10, centeredChromeTextY(buttonY, kFormControlH), label, disabled ? 76 : 255, disabled ? 80 : 255, disabled ? 88 : 255);
			break;
		}
		}
		++blockIndex;
	}

	// Scroll thumb
	int maxScroll = maxScrollOffset();
	if (maxScroll > 0) {
		int trackY = kToolbarH + 10;
		int trackH = kContentH - 8;
		int thumbH = std::max(22, (trackH * kContentH) / s_documentHeight);
		int thumbY = trackY + ((trackH - thumbH) * s_scrollOffset) / maxScroll;
		drawRect(s_windowId, kContentX + kContentW - 10, trackY, 6, trackH, 216, 220, 228);
		drawRect(s_windowId, kContentX + kContentW - 10, thumbY, 6, thumbH, 130, 138, 156);
	}
}

void Navigator::renderStatusBar()
{
	drawRect(s_windowId, 0, kWindowH - kStatusBarH, kWindowW, kStatusBarH, 36, 40, 50);
	drawRect(s_windowId, 0, kWindowH - kStatusBarH, kWindowW, 1, 78, 86, 108);

	if (s_findActive) {
		drawRect(s_windowId, 8, kWindowH - kStatusBarH + 4, 420, kStatusBarH - 8, 18, 22, 30);
		drawRect(s_windowId, 8, kWindowH - kStatusBarH + 4, 420, 1, 80, 140, 220);
		drawRect(s_windowId, 8, kWindowH - kStatusBarH + kStatusBarH - 5, 420, 1, 80, 140, 220);
		const int findTextY = centeredChromeTextY(kWindowH - kStatusBarH + 4, kStatusBarH - 8);
		std::string shown = s_findBuffer;
		const int maxChars = 28;
		if (static_cast<int>(shown.size()) > maxChars) {
			shown = shown.substr(shown.size() - static_cast<size_t>(maxChars));
		}
		drawTextAt(s_windowId, 16, findTextY, "Find: " + shown);
		int caretPos = std::min(s_findCaret, maxChars);
		drawRect(s_windowId, 64 + caretPos * kCharW, kWindowH - kStatusBarH + 6, 1, kStatusBarH - 12, 200, 220, 255);
		drawTextAt(s_windowId, 440, centeredChromeTextY(kWindowH - kStatusBarH, kStatusBarH), findMatchStatusText() + "   Enter/Down: next   Up: prev   Esc: close");
		return;
	}

	const std::string& status = s_hoverStatusText.empty() ? s_statusText : s_hoverStatusText;
	std::string shown = status;
	if (hasSelection()) {
		const std::string text = selectedText();
		shown += (shown.empty() ? "" : "   ") + std::string("Selection: ") + std::to_string(text.size()) + " chars";
	}
	drawTextAt(s_windowId, 12, centeredChromeTextY(kWindowH - kStatusBarH, kStatusBarH), shown);
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
	case HitTarget::FormSelect:  next = "Cycle select option"; break;
	case HitTarget::FormSubmit:  next = "Submit form"; break;
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
	if (s_focusedInputBlockIndex >= 0) {
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
		loadUrl(s_currentDoc.url);
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

void Navigator::handleDocumentClick(HitTarget target, int linkBlockIndex)
{
	if (target == HitTarget::Link &&
		linkBlockIndex >= 0 &&
		linkBlockIndex < static_cast<int>(s_currentDoc.blocks.size()))
	{
		navigateTo(s_currentDoc.blocks[linkBlockIndex].url);
	} else if ((target == HitTarget::FormCheckbox ||
				target == HitTarget::FormRadio ||
				target == HitTarget::FormSelect) &&
		linkBlockIndex >= 0 &&
		linkBlockIndex < static_cast<int>(s_currentDoc.blocks.size()))
	{
		activateFormControl(linkBlockIndex);
	} else if (target == HitTarget::FormSubmit &&
		linkBlockIndex >= 0 &&
		linkBlockIndex < static_cast<int>(s_currentDoc.blocks.size()))
	{
		submitFormForBlock(linkBlockIndex);
	}
}

void Navigator::handleMouseInput(int x, int y, int button, const std::string& action)
{
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
			if (s_focusedInputBlockIndex >= 0) blurDocumentInput();
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
			focusDocumentInput(linkIdx);
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
			target == HitTarget::FormSubmit) {
			s_mouseMode = MouseMode::FormInputInteraction;
			if (s_focusedInputBlockIndex >= 0) blurDocumentInput();
			if (target != HitTarget::FormSubmit) s_focusedInputBlockIndex = linkIdx;
			clearSelection();
			updateDisplay();
			return;
		}

		if (s_focusedInputBlockIndex >= 0) blurDocumentInput();

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

void Navigator::focusDocumentInput(int blockIndex)
{
	if (blockIndex < 0 || blockIndex >= static_cast<int>(s_currentDoc.blocks.size()) ||
		!isFocusableFormControl(s_currentDoc.blocks[blockIndex])) {
		return;
	}
	s_focusedInputBlockIndex = blockIndex;
	s_inputCaret = static_cast<int>(s_currentDoc.blocks[blockIndex].inputValue.size());
	s_statusText = (s_currentDoc.blocks[blockIndex].type == BlockType::FormTextInput ||
		s_currentDoc.blocks[blockIndex].type == BlockType::FormTextarea)
		? "Editing form field"
		: "Form control focused";
}

void Navigator::blurDocumentInput()
{
	s_focusedInputBlockIndex = -1;
	s_inputCaret = 0;
}

void Navigator::openFindMode()
{
	s_findActive = true;
	if (s_addressFocused) {
		s_addressFocused = false;
		s_addressBuffer.clear();
		s_addressCaret = 0;
	}
	if (s_focusedInputBlockIndex >= 0) {
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
		return block.text;
	case BlockType::Image:
		return !block.alt.empty() ? block.alt : block.text;
	case BlockType::FormTextInput:
	case BlockType::FormTextarea:
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
	if (isTableCellLikeBlock(block)) {
		if (!isFirstTableCellInGroup(s_currentDoc, blockIndex)) return Rect{ 0, 0, 0, 0 };
		const int groupStart = tableGroupStartIndex(s_currentDoc, blockIndex);
		const TableGroupLayout layout = buildTableGroupLayout(s_currentDoc, groupStart);
		const uint64_t rowSerial = tableRowSerialForBlock(block);
		for (const TableRowLayout& row : layout.rows) {
			if (row.rowSerial != rowSerial) continue;
			const int drawY = kContentY + blockLayoutY(blockIndex) - s_scrollOffset;
			const int rowY = drawY + cssMarginTopPx(block.style, 4) + cssBorderTopPx(block.style);
			return Rect{
				layout.outerX + layout.paddingLeft,
				rowY + layout.paddingTop,
				std::max(kCharW, (layout.outerWidth - layout.paddingLeft - layout.paddingRight)),
				std::max(kLineH, row.heightPx)
			};
		}
		return Rect{ 0, 0, 0, 0 };
	}
	const int drawY = kContentY + blockLayoutY(blockIndex) - s_scrollOffset;
	const int bodyMarginLeft = blockBodyMarginLeft(s_currentDoc);
	const int bodyMarginRight = blockBodyMarginRight(s_currentDoc);
	const int blockMarginTop = cssMarginTopPx(block.style, block.type == BlockType::Heading ? 10 : 4);
	const int blockMarginBottom = cssMarginBottomPx(block.style, block.type == BlockType::ListItem ? 4 : 8);
	const int blockMarginLeft = cssMarginLeftPx(block.style, 0);
	const int blockMarginRight = cssMarginRightPx(block.style, 0);
	const int paddingTop = cssPaddingTopPx(block.style, block.type == BlockType::Preformatted ? 4 : 0);
	const int paddingRight = cssPaddingRightPx(block.style, block.type == BlockType::Preformatted ? 4 : 0);
	const int paddingBottom = cssPaddingBottomPx(block.style, block.type == BlockType::Preformatted ? 4 : 0);
	const int paddingLeft = cssPaddingLeftPx(block.style, block.type == BlockType::Preformatted ? 4 : 0);
	const int availableWidth = std::max(1, kContentW - blockIndentForType(block.type) - kDocumentRightPad
		- bodyMarginLeft - bodyMarginRight - blockMarginLeft - blockMarginRight);
	const int outerWidth = blockOuterWidth(block, availableWidth);
	const int outerX = blockOuterX(block, s_currentDoc, availableWidth, outerWidth);
	const int textX = outerX + paddingLeft;
	int textW = 0;
	int textH = 0;
	switch (block.type) {
	case BlockType::Heading:
		textW = std::max(1, outerWidth - paddingLeft - paddingRight);
		textH = std::max(blockTextLineHeight(block) + 4, cssFontSizeOrDefault(block.style, 20) + 2);
		break;
	case BlockType::Paragraph:
	case BlockType::Link:
		textW = std::max(1, outerWidth - paddingLeft - paddingRight);
		textH = wrappedBlockHeight(block.text, std::max(1, textW / kCharW), false, blockTextLineHeight(block));
		break;
	case BlockType::ListItem:
		textW = std::max(1, outerWidth - paddingLeft - paddingRight);
		textH = wrappedBlockHeight(block.text, std::max(1, textW / kCharW), false, blockTextLineHeight(block));
		break;
	case BlockType::Preformatted:
		textW = std::max(1, outerWidth - paddingLeft - paddingRight);
		textH = wrappedBlockHeight(block.text, std::max(1, textW / kCharW), true, blockTextLineHeight(block)) + paddingTop + paddingBottom;
		break;
	default:
		break;
	}
	return Rect{ textX, drawY + blockMarginTop + cssBorderTopPx(block.style) + paddingTop, std::max(kCharW, textW), std::max(kLineH, textH + blockMarginBottom) };
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
		const std::vector<std::string> lines = (block.type == BlockType::Preformatted) ? splitPreLines(text) : wrapText(text, maxChars);
		int lineIndex = std::max(0, std::min((y - rect.y) / kLineH, std::max(0, static_cast<int>(lines.size()) - 1)));
		size_t lineStart = 0;
		for (int line = 0; line < lineIndex && line < static_cast<int>(lines.size()); ++line) {
			lineStart += lines[line].size();
			if (block.type != BlockType::Preformatted) {
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
	return block.type == BlockType::FormTextInput ||
		block.type == BlockType::FormTextarea ||
		block.type == BlockType::FormCheckbox ||
		block.type == BlockType::FormRadio ||
		block.type == BlockType::FormSelect ||
		block.type == BlockType::FormSubmit;
}

int Navigator::formControlHeight(const DocBlock& block)
{
	if (block.type == BlockType::FormTextarea) {
		int rows = block.visibleRows > 0 ? block.visibleRows : 4;
		rows = std::max(kTextareaMinRows, std::min(kTextareaMaxRows, rows));
		return std::max(kFormControlH, rows * kLineH + 10);
	}
	return kFormControlH;
}

void Navigator::focusNextFormControl(bool reverse)
{
	const int count = static_cast<int>(s_currentDoc.blocks.size());
	if (count <= 0) return;
	int start = s_focusedInputBlockIndex;
	if (start < 0 || start >= count) start = reverse ? 0 : count - 1;
	for (int step = 1; step <= count; ++step) {
		int idx = reverse ? (start - step + count) % count : (start + step) % count;
		if (!isFocusableFormControl(s_currentDoc.blocks[idx])) continue;
		focusDocumentInput(idx);
		s_scrollOffset = std::max(0, blockLayoutY(idx) - 24);
		clampScrollOffset();
		clearSelection();
		updateStatus("Form control focused.");
		updateDisplay();
		return;
	}
}

void Navigator::activateFormControl(int blockIndex)
{
	if (blockIndex < 0 || blockIndex >= static_cast<int>(s_currentDoc.blocks.size())) return;
	DocBlock& block = s_currentDoc.blocks[blockIndex];
	if (block.type == BlockType::FormCheckbox) {
		block.checked = !block.checked;
		s_focusedInputBlockIndex = blockIndex;
		updateDisplay();
		return;
	}
	if (block.type == BlockType::FormRadio) {
		for (DocBlock& candidate : s_currentDoc.blocks) {
			if (candidate.type == BlockType::FormRadio &&
				candidate.formIndex == block.formIndex &&
				candidate.inputName == block.inputName) {
				candidate.checked = false;
			}
		}
		block.checked = true;
		s_focusedInputBlockIndex = blockIndex;
		updateDisplay();
		return;
	}
	if (block.type == BlockType::FormSelect) {
		if (!block.options.empty()) {
			int next = block.selectedOption < 0 ? 0 : block.selectedOption + 1;
			if (next >= static_cast<int>(block.options.size())) next = 0;
			block.selectedOption = next;
			const gxos::web::FormOption& option = block.options[static_cast<size_t>(next)];
			block.inputValue = option.value;
			block.text = option.text;
		}
		s_focusedInputBlockIndex = blockIndex;
		updateDisplay();
		return;
	}
	if (block.type == BlockType::FormSubmit) {
		submitFormForBlock(blockIndex);
	}
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
			if (block.checked) appendField(block.inputName, block.inputValue.empty() ? "on" : block.inputValue);
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
	s_scrollOffset = 0;
	s_documentHeight = computeDocumentHeight();
	s_lastSubmittedFormStatus = response.ok() ? "POST submitted" : std::string("POST failed: ") + gxos::web::httpErrorName(response.error);
	storePageMetadata(s_pageMetadata, s_currentDoc);
	updateDisplay();
}

void Navigator::handleKeyPress(int keyCode, const std::string& action)
{
	if (keyCode == 17) {
		s_ctrlPressed = (action == "down");
		return;
	}
	if (keyCode == 16) {
		s_shiftPressed = (action == "down");
		return;
	}
	if (action != "down") return;

	if (keyCode == 9 && !s_addressFocused && !s_findActive) {
		focusNextFormControl(s_shiftPressed);
		return;
	}

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

	if (s_focusedInputBlockIndex >= 0 &&
		s_focusedInputBlockIndex < static_cast<int>(s_currentDoc.blocks.size()) &&
		(s_currentDoc.blocks[s_focusedInputBlockIndex].type == BlockType::FormCheckbox ||
		 s_currentDoc.blocks[s_focusedInputBlockIndex].type == BlockType::FormRadio ||
		 s_currentDoc.blocks[s_focusedInputBlockIndex].type == BlockType::FormSelect ||
		 s_currentDoc.blocks[s_focusedInputBlockIndex].type == BlockType::FormSubmit))
	{
		if (keyCode == 13 || keyCode == 32) {
			activateFormControl(s_focusedInputBlockIndex);
		} else if (keyCode == 27) {
			blurDocumentInput();
			updateDisplay();
		}
		return;
	}

	if (s_focusedInputBlockIndex >= 0 &&
		s_focusedInputBlockIndex < static_cast<int>(s_currentDoc.blocks.size()) &&
		(s_currentDoc.blocks[s_focusedInputBlockIndex].type == BlockType::FormTextInput ||
		 s_currentDoc.blocks[s_focusedInputBlockIndex].type == BlockType::FormTextarea))
	{
		if (s_ctrlPressed && ((keyCode == 67 || keyCode == 99) || (keyCode == 65 || keyCode == 97))) {
			updateStatus("Form input copy/select all is deferred.");
			return;
		}
		DocBlock& block = s_currentDoc.blocks[s_focusedInputBlockIndex];
		const int bufLen = static_cast<int>(block.inputValue.size());
		if (keyCode == 13) {
			if (block.type == BlockType::FormTextarea) {
				block.inputValue.insert(static_cast<size_t>(s_inputCaret), 1, '\n');
				block.text = block.inputValue;
				++s_inputCaret;
				updateDisplay();
			} else {
				submitFormForBlock(s_focusedInputBlockIndex);
			}
		} else if (keyCode == 27) {
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

	for (int i = 0; i < static_cast<int>(s_currentDoc.blocks.size()); ++i) {
		if (s_currentDoc.blocks[i].type == BlockType::FormTextInput ||
			s_currentDoc.blocks[i].type == BlockType::FormCheckbox ||
			s_currentDoc.blocks[i].type == BlockType::FormRadio ||
			s_currentDoc.blocks[i].type == BlockType::FormTextarea ||
			s_currentDoc.blocks[i].type == BlockType::FormSelect) {
			if (formControlRect(i).contains(x, y)) {
				outLinkBlockIndex = i;
				switch (s_currentDoc.blocks[i].type) {
				case BlockType::FormCheckbox: return HitTarget::FormCheckbox;
				case BlockType::FormRadio: return HitTarget::FormRadio;
				case BlockType::FormTextarea: return HitTarget::FormTextarea;
				case BlockType::FormSelect: return HitTarget::FormSelect;
				default: return HitTarget::FormInput;
				}
			}
		} else if (s_currentDoc.blocks[i].type == BlockType::FormSubmit) {
			if (formControlRect(i).contains(x, y)) {
				outLinkBlockIndex = i;
				return HitTarget::FormSubmit;
			}
		} else if (s_currentDoc.blocks[i].type == BlockType::Link) {
			if (linkBlockRect(i).contains(x, y)) {
				outLinkBlockIndex = i;
				return HitTarget::Link;
			}
		}
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

void Navigator::loadUrl(const std::string& url, bool updateDisplayAfterLoad)
{
	Logger::write(LogLevel::Info, std::string("Navigator loadUrl: ") + url);
	s_loading = true;
	if (s_windowId != 0) updateDisplay();
	cleanupRemoteImageTempFiles();
	s_imageCache.clear();
	blurDocumentInput();
	clearSelection();

	WebDocument doc;
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

	s_currentDoc      = std::move(doc);
	if (!s_currentDoc.url.empty()) {
		s_visitedUrls.insert(s_currentDoc.url);
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
	if (updateDisplayAfterLoad) {
		updateDisplay();
	}
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

	loadUrl(url);
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

	loadUrl(target);
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

	loadUrl(target);
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

	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Requested URL", m.requestedUrl), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Final URL", m.finalUrl), ""});
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
		case BlockType::FormTextInput:
		case BlockType::FormTextarea: {
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

	const std::string& pageUrl = s_pageMetadata.finalUrl.empty()
		? s_pageMetadata.requestedUrl : s_pageMetadata.finalUrl;

	if (pageUrl.empty()) {
		result.blocks.push_back({BlockType::Heading, "Save Page Text", ""});
		result.blocks.push_back({BlockType::Paragraph, "No page has been loaded yet.", ""});
		result.blocks.push_back({BlockType::Link, "Go to about:navigator", "about:navigator"});
		return result;
	}

	const std::string text = extractDocumentText(s_inspectedDoc);
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

	gxos::web::HttpResponse response = gxos::web::fetchHttpUrl(url);
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
	metadata.redirectCount = response.redirectCount;
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
		WebDocument doc = parseHtml(documentUrl, response.body);
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

Navigator::Rect Navigator::linkBlockRect(int blockIndex)
{
	// The entire wrapped link height is clickable.
	// TODO: per-line hit testing when proportional text measurement is available.
	const DocBlock& block = s_currentDoc.blocks[blockIndex];
	if (!blockHasVisibleCss(block)) return Rect{0, 0, 0, 0};
	const int bodyMarginLeft = blockBodyMarginLeft(s_currentDoc);
	const int bodyMarginRight = blockBodyMarginRight(s_currentDoc);
	const int blockMarginTop = cssMarginTopPx(block.style, 4);
	const int blockMarginLeft = cssMarginLeftPx(block.style, 0);
	const int blockMarginRight = cssMarginRightPx(block.style, 0);
	const int paddingLeft = cssPaddingLeftPx(block.style, 0);
	const int paddingRight = cssPaddingRightPx(block.style, 0);
	const int availableWidth = std::max(1, kContentW - blockIndentForType(block.type) - kDocumentRightPad
		- bodyMarginLeft - bodyMarginRight - blockMarginLeft - blockMarginRight);
	const int outerWidth = blockOuterWidth(block, availableWidth);
	const int outerX = blockOuterX(block, s_currentDoc, availableWidth, outerWidth);
	const int innerWidth = std::max(1, outerWidth - paddingLeft - paddingRight);
	int relY  = blockLayoutY(blockIndex);
	int drawY = kContentY + relY - s_scrollOffset + blockMarginTop + cssBorderTopPx(block.style) + cssPaddingTopPx(block.style, 0);
	int h     = wrappedBlockHeight(block.text, std::max(1, innerWidth / kCharW), false, blockTextLineHeight(block));
	int w     = std::min(static_cast<int>(block.text.size()) * kCharW, innerWidth);
	return Rect{ outerX + paddingLeft, drawY, w, h };
}

Navigator::Rect Navigator::formControlRect(int blockIndex)
{
	if (blockIndex < 0 || blockIndex >= static_cast<int>(s_currentDoc.blocks.size())) {
		return Rect{0, 0, 0, 0};
	}
	const DocBlock& block = s_currentDoc.blocks[blockIndex];
	if (!blockHasVisibleCss(block)) return Rect{0, 0, 0, 0};
	const int bodyMarginLeft = blockBodyMarginLeft(s_currentDoc);
	const int bodyMarginRight = blockBodyMarginRight(s_currentDoc);
	const int blockMarginLeft = cssMarginLeftPx(block.style, 0);
	const int blockMarginRight = cssMarginRightPx(block.style, 0);
	const int blockMarginTop = cssMarginTopPx(block.style, 4);
	const int paddingLeft = cssPaddingLeftPx(block.style, 0);
	const int paddingTop = cssPaddingTopPx(block.style, 0);
	const int availableWidth = std::max(1, kContentW - blockIndentForType(block.type) - kDocumentRightPad
		- bodyMarginLeft - bodyMarginRight - blockMarginLeft - blockMarginRight);
	const int outerWidth = blockOuterWidth(block, availableWidth);
	const int outerX = blockOuterX(block, s_currentDoc, availableWidth, outerWidth);
	const int relY = blockLayoutY(blockIndex);
	const int drawY = kContentY + relY - s_scrollOffset + blockMarginTop + cssBorderTopPx(block.style) + paddingTop;
	int w = kFormInputW;
	if (block.type == BlockType::FormSubmit) w = kFormSubmitW;
	else if (block.type == BlockType::FormCheckbox || block.type == BlockType::FormRadio) w = 260;
	return Rect{ outerX + paddingLeft, drawY, w, formControlHeight(block) };
}

int Navigator::computeDocumentHeight()
{
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
			WebDocument doc = parseHtml(url, fr.text);
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
