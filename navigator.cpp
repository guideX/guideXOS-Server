#include "navigator.h"

#include "gui_protocol.h"
#include "kernel/core/include/kernel/image_adapter.h"
#include "ipc_bus.h"
#include "guide_web_http.h"
#include "logger.h"
#include "navigator_file_io.h"
#include "navigator_html_parser.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace gxos {
namespace apps {

using namespace gxos::gui;

uint64_t           Navigator::s_windowId        = 0;
int                Navigator::s_scrollOffset    = 0;
int                Navigator::s_documentHeight  = 0;
std::string        Navigator::s_statusText      = "Ready";
std::string        Navigator::s_hoverStatusText;
int                Navigator::s_hitLinkBlockIndex = -1;
WebDocument        Navigator::s_currentDoc;
NavigatorPageMetadata Navigator::s_pageMetadata;
std::vector<std::string> Navigator::s_backStack;
std::vector<std::string> Navigator::s_forwardStack;
std::vector<Bookmark>    Navigator::s_bookmarks;
static std::vector<DownloadItem> s_recentDownloads;
bool        Navigator::s_addressFocused = false;
std::string Navigator::s_addressBuffer;
int         Navigator::s_addressCaret   = 0;

namespace {
	constexpr int kWindowW = 920;
	constexpr int kWindowH = 640;
	constexpr int kToolbarH = 64;
	constexpr int kStatusBarH = 24;
	constexpr int kButtonY = 12;
	constexpr int kButtonW = 72;
	constexpr int kButtonH = 26;
	constexpr int kButtonGap = 10;
	constexpr int kAddressX = 502;
	constexpr int kAddressY = 12;
	constexpr int kAddressH = 26;
	constexpr int kAddressW = 920 - kAddressX - 20;
	constexpr int kContentX = 24;
	constexpr int kContentY = kToolbarH + 18;
	constexpr int kContentW = 920 - 48;
	constexpr int kContentH = 640 - kToolbarH - kStatusBarH - 24;
	constexpr int kHeadingY = 24;
	constexpr size_t kNavigatorMaxSourcePreviewBytes = gxos::web::kHttpMaxBodyBytes;
	constexpr uint32_t kRemoteImageMaxBytes = 256u * 1024u;
	constexpr uint32_t kRemoteImageMaxWidth = 2048u;
	constexpr uint32_t kRemoteImageMaxHeight = 2048u;
	constexpr uint32_t kRemoteImageMaxPixels = 2048u * 2048u;

	constexpr int kWidgetIdBack = 1;
	constexpr int kWidgetIdForward = 2;
	constexpr int kWidgetIdReload = 3;
	constexpr int kWidgetIdHome = 4;
	constexpr int kWidgetIdBookmarks = 5;
	constexpr int kWidgetIdAddBookmark = 6;

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
		if (!style.hasColor) {
			drawTextAt(windowId, x, y, text);
			return;
		}
		int r = 220;
		int g = 220;
		int b = 220;
		r = static_cast<int>((style.color >> 16) & 0xFFu);
		g = static_cast<int>((style.color >> 8) & 0xFFu);
		b = static_cast<int>(style.color & 0xFFu);
		drawTextAtColored(windowId, x, y, text, r, g, b);
	}

	void drawImage(uint64_t windowId, int x, int y, int w, int h, const std::string& path)
	{
		publish(MsgType::MT_DrawImage, packDrawImage(windowId, x, y, w, h, path));
	}

	void addButton(uint64_t windowId, int id, int x, int y, int w, int h, const std::string& text)
	{
		publish(MsgType::MT_WidgetAdd, packWidgetAdd(windowId, 1, id, x, y, w, h, text));
	}

	// -----------------------------------------------------------------------
	// Word-wrap helpers
	//
	// All text is rendered with a fixed-width bitmap font.
	// kCharW is the approximate glyph advance in pixels.
	// TODO: replace with proportional text-measurement API when available.
	// -----------------------------------------------------------------------
	constexpr int kCharW    = 8;   // approximate character cell width in pixels
	constexpr int kLineH    = 16;  // single line height in pixels

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
	static int wrappedBlockHeight(const std::string& text, int wrapCols, bool isPre = false)
	{
		if (isPre) {
			return static_cast<int>(splitPreLines(text).size()) * kLineH;
		}
		int lines = static_cast<int>(wrapText(text, wrapCols).size());
		if (lines == 0) lines = 1;
		return lines * kLineH;
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
	static std::string filePathFromUrl(const std::string& url);
	static std::string pageInfoLine(const std::string& label, const std::string& value);
	static std::string pageInfoLine(const std::string& label, int value);
	static DocBlock makeBlock(BlockType type, const std::string& text, const std::string& url = "", const std::string& tagName = "");
	static int cssMarginOrDefault(const WebStyle& style, int fallbackValue);
	static int cssPaddingOrDefault(const WebStyle& style, int fallbackValue);
	static int cssFontSizeOrDefault(const WebStyle& style, int fallbackValue);
	static bool colorChannels(uint32_t color, int& r, int& g, int& b);

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

	static std::string fileNameFromUrlPath(const std::string& url)
	{
		std::string path;
		if (url.rfind("http://", 0) == 0) {
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

	static std::string uniqueDownloadPathForName(const std::string& safeName, std::string& outFinalName)
	{
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
		doc.url = item.finalUrl.empty() ? item.url : item.finalUrl;
		doc.title = item.success ? "Download Complete" : "Download Unavailable";
		doc.blocks.push_back({BlockType::Heading, doc.title, ""});
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Filename", item.suggestedFileName), ""});
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Source URL", item.url), ""});
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Final URL", item.finalUrl), ""});
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Content type", item.contentType), ""});
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Byte count", static_cast<int>(item.byteCount)), ""});
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Saved path", item.savedPath), ""});
		doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Status", item.success ? "success" : "failed"), ""});
		if (!item.error.empty()) {
			doc.blocks.push_back({BlockType::Paragraph, item.error, ""});
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
		metadata.rawSourceBytes = source.size();
		if (source.size() > kNavigatorMaxSourcePreviewBytes) {
			metadata.rawSource = source.substr(0, kNavigatorMaxSourcePreviewBytes);
			metadata.rawSourceTruncated = true;
		} else {
			metadata.rawSource = source;
			metadata.rawSourceTruncated = false;
		}
	}

	static void fillDocumentCounts(NavigatorPageMetadata& metadata, const WebDocument& doc)
	{
		metadata.documentBlockCount = static_cast<int>(doc.blocks.size());
		metadata.imageBlockCount = 0;
		metadata.loadedImageCount = 0;
		metadata.failedImageCount = 0;
		metadata.remoteImageCount = 0;
		metadata.localImageCount = 0;
		metadata.lastImageError.clear();
		metadata.cssDetected = doc.cssDiagnostics.cssDetected;
		metadata.styleRuleCount = doc.cssDiagnostics.styleRuleCount;
		metadata.unsupportedExternalStylesheetCount = doc.cssDiagnostics.unsupportedExternalStylesheetCount;
		metadata.unsupportedCssDeclarationCount = doc.cssDiagnostics.unsupportedDeclarationCount;
		metadata.cssStyleBlockCapped = doc.cssDiagnostics.styleBlockCapped;
		metadata.cssStyleBytesProcessed = doc.cssDiagnostics.styleBytesProcessed;

		for (const DocBlock& block : doc.blocks) {
			if (block.type != BlockType::Image) continue;
			++metadata.imageBlockCount;
			if (block.url.rfind("http://", 0) == 0) {
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

	static int cssPaddingOrDefault(const WebStyle& style, int fallbackValue)
	{
		return style.padding >= 0 ? style.padding : fallbackValue;
	}

	static int cssFontSizeOrDefault(const WebStyle& style, int fallbackValue)
	{
		return style.fontScaleOrSize > 0 ? style.fontScaleOrSize : fallbackValue;
	}

	static bool colorChannels(uint32_t color, int& r, int& g, int& b)
	{
		r = static_cast<int>((color >> 16) & 0xFFu);
		g = static_cast<int>((color >> 8) & 0xFFu);
		b = static_cast<int>(color & 0xFFu);
		return true;
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
		bool cssDetected)
	{
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
			{"Capabilities", "HTTP", "enabled for http:// via Winsock transport"},
			{"Capabilities", "Remote PNG", "enabled for http:// PNG images"},
			{"Capabilities", "Downloads", "enabled for unsupported http:// content within body limit"},
			{"Capabilities", "Temp files", "enabled for compositor image handoff"},
			{"Capabilities", "Bookmark persistence", "enabled"},
			{"Capabilities", "HTTPS/TLS", "unsupported"},
			{"Capabilities", "CSS-lite embedded <style>", "enabled"},
			{"Capabilities", "Hosted colored text primitive", "enabled"},
			{"Capabilities", "CSS text color visible", "enabled"},
			{"Capabilities", "External stylesheets", "unsupported"},

			{"Backends", "File backend", "navigator_file_io hosted/VFS adapter"},
			{"Backends", "HTTP backend", "guide_web_http hosted socket path"},
			{"Backends", "Image backend", "ImageAdapter + compositor drawImage path"},

			{"Current Document", "URL", currentUrl.empty() ? "(none)" : currentUrl},
			{"Current Document", "Title", currentTitle.empty() ? "(none)" : currentTitle},
			{"Current Document", "Block count", std::to_string(currentBlockCount)},
			{"Current Document", "Inspected page", inspectedUrl.empty() ? "(none)" : inspectedUrl},
			{"Current Document", "CSS diagnostics", cssDetected ? "css detected" : "no css detected"},
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

		if (block.url.rfind("http://", 0) == 0) {
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
	return ProcessTable::spawn(spec, {"navigator"});
}

bool Navigator::SmokeNavigateTo(const std::string& url)
{
	if (s_windowId == 0) return false;
	loadUrl(url);
	return s_currentDoc.url == url;
}

std::string Navigator::SmokeRuntimeReport()
{
	const std::string inspected = s_pageMetadata.finalUrl.empty() ? "" : s_pageMetadata.finalUrl;
	return formatRuntimeReport(hostedRuntimeReportEntries(
		s_currentDoc.url,
		s_currentDoc.title,
		static_cast<int>(s_currentDoc.blocks.size()),
		inspected,
		s_pageMetadata.cssDetected));
}

std::string Navigator::SmokeCurrentUrl()
{
	return s_currentDoc.url;
}

int Navigator::SmokeCurrentBlockCount()
{
	return static_cast<int>(s_currentDoc.blocks.size());
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
	s_recentDownloads.clear();
	s_addressFocused = false;
	s_addressBuffer.clear();
	s_addressCaret   = 0;

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
				int linkIdx = -1;
				HitTarget target = hitTest(x, y, linkIdx);
				if (button == 0 && action == "move") {
					updateHoverStatus(target, linkIdx);
				} else if (button == 1 && action == "down") {
					if (target == HitTarget::AddressBar) {
							focusAddressBar();
							// Set caret from click X position using the same fixed char width as rendering.
							// TODO: replace with proportional text measurement when available.
							if (s_addressFocused) {
								constexpr int kCharW = 8;
								constexpr int kTextX = kAddressX + 10;
								int charOffset = (x - kTextX) / kCharW;
								s_addressCaret = std::max(0, std::min(charOffset,
									static_cast<int>(s_addressBuffer.size())));
								renderToolbar();
							}
					} else {
						// Clicking anywhere outside the address bar blurs it.
						if (s_addressFocused) blurAddressBar();
						if (target == HitTarget::Link) handleDocumentClick(target, linkIdx);
					}
				}
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
	drawRect(s_windowId, 0, 0, kWindowW, kToolbarH, 42, 46, 58);
	drawRect(s_windowId, 0, kToolbarH - 1, kWindowW, 1, 78, 86, 108);

	addButton(s_windowId, kWidgetIdBack, 20, kButtonY, kButtonW, kButtonH, "Back");
	addButton(s_windowId, kWidgetIdForward, 20 + (kButtonW + kButtonGap), kButtonY, kButtonW, kButtonH, "Forward");
	addButton(s_windowId, kWidgetIdReload, 20 + 2 * (kButtonW + kButtonGap), kButtonY, kButtonW, kButtonH, "Reload");
	addButton(s_windowId, kWidgetIdHome, 20 + 3 * (kButtonW + kButtonGap), kButtonY, kButtonW, kButtonH, "Home");
	addButton(s_windowId, kWidgetIdBookmarks, 20 + 4 * (kButtonW + kButtonGap), kButtonY, kButtonW, kButtonH, "Bookmarks");
	addButton(s_windowId, kWidgetIdAddBookmark, 20 + 5 * (kButtonW + kButtonGap), kButtonY, kButtonW, kButtonH, "Add *");

	drawRect(s_windowId, kAddressX, kAddressY, kAddressW, kAddressH, 18, 22, 30);
	if (s_addressFocused) {
		// Focused: bright blue border on all four sides
		drawRect(s_windowId, kAddressX,                 kAddressY,                 kAddressW, 1, 80, 140, 220);
		drawRect(s_windowId, kAddressX,                 kAddressY + kAddressH - 1, kAddressW, 1, 80, 140, 220);
		drawRect(s_windowId, kAddressX,                 kAddressY,                 1, kAddressH, 80, 140, 220);
		drawRect(s_windowId, kAddressX + kAddressW - 1, kAddressY,                 1, kAddressH, 80, 140, 220);

		// Draw buffer text split at caret position so we can paint the caret in between.
		// The UI bitmap font has a fixed cell width; kCharW is the best approximation.
		// TODO: replace with proportional text-measurement API when available.
		constexpr int kCharW     = 8;  // approximate glyph advance in pixels
		constexpr int kTextX     = kAddressX + 10;
		constexpr int kTextY     = kAddressY + 7;

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
		drawTextAt(s_windowId, kAddressX + 10, kAddressY + 7, s_currentDoc.url);
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

	// Layout constants
	// Wrap columns: content width minus indent, divided by character cell width.
	constexpr int kIndent       = 18;
	constexpr int kListIndent   = 28;
	constexpr int kPreIndent    = 18;
	constexpr int kWrapW        = kContentW - kIndent - 16; // 16px right margin
	const     int kWrapCols     = kWrapW / kCharW;
	const     int kListWrapCols = (kContentW - kListIndent - 16) / kCharW;
	const     int kPreWrapCols  = (kContentW - kPreIndent - 16) / kCharW;

	int blockIndex = 0;
	for (const DocBlock& block : s_currentDoc.blocks) {
		int relY  = blockLayoutY(blockIndex);
		int drawY = kContentY + relY - s_scrollOffset;
		const int blockMarginTop = block.style.marginTop >= 0 ? block.style.marginTop : (block.type == BlockType::Heading ? 10 : 4);
		const int blockMarginBottom = block.style.marginBottom >= 0 ? block.style.marginBottom : 8;
		const int blockMarginLeft = block.style.marginLeft >= 0 ? block.style.marginLeft : 0;
		const int blockPadding = cssPaddingOrDefault(block.style, block.type == BlockType::Preformatted ? 4 : 0);
		const int bodyMarginLeft = s_currentDoc.bodyStyle.marginLeft >= 0 ? s_currentDoc.bodyStyle.marginLeft : 0;
		const int headingFontSize = cssFontSizeOrDefault(block.style, block.tagName == "h1" ? 24 : (block.tagName == "h2" ? 20 : (block.tagName == "h3" ? 18 : 20)));

		// Skip blocks fully above or below the visible viewport
		int blockH = 0;
		bool nextIsHeading = (blockIndex + 1 < static_cast<int>(s_currentDoc.blocks.size()) &&
			s_currentDoc.blocks[blockIndex + 1].type == BlockType::Heading);
		constexpr int kPreGapIfNextHeading = 10;
		switch (block.type) {
		case BlockType::Heading:      blockH = blockMarginTop + std::max(kLineH + 4, headingFontSize + 2) + std::max(4, blockMarginBottom); break;
		case BlockType::Paragraph:    blockH = blockMarginTop + wrappedBlockHeight(block.text, kWrapCols)        + std::max(4, blockMarginBottom) + (nextIsHeading ? kPreGapIfNextHeading : 0); break;
		case BlockType::Link:         blockH = blockMarginTop + wrappedBlockHeight(block.text, kWrapCols)        + std::max(4, blockMarginBottom) + (nextIsHeading ? kPreGapIfNextHeading : 0); break;
		case BlockType::ListItem:     blockH = blockMarginTop + wrappedBlockHeight(block.text, kListWrapCols)    + std::max(2, blockMarginBottom) + (nextIsHeading ? kPreGapIfNextHeading : 0); break;
		case BlockType::Preformatted: blockH = blockMarginTop + wrappedBlockHeight(block.text, kPreWrapCols, true) + blockPadding * 2 + std::max(4, blockMarginBottom); break;
		case BlockType::Image: {
			int imageW = 0;
			int imageH = 0;
			imageDisplaySize(block, imageW, imageH);
			blockH = blockMarginTop + imageH + std::max(4, blockMarginBottom);
			break;
		}
		}
		if (drawY + blockH < kContentY || drawY > kContentY + kContentH) {
			++blockIndex;
			continue;
		}

		switch (block.type) {
		case BlockType::Heading:
			// Slightly larger heading: draw a subtle accent bar then the text
			drawRect(s_windowId, kContentX + kIndent + bodyMarginLeft + blockMarginLeft, drawY + blockMarginTop + std::max(kLineH, headingFontSize - 4),
				kContentW - kIndent - 16, 2, 80, 140, 220);
			drawTextAtStyled(s_windowId, kContentX + kIndent + bodyMarginLeft + blockMarginLeft, drawY + blockMarginTop, block.text, block.style);
			if (block.style.bold) {
				drawTextAtStyled(s_windowId, kContentX + kIndent + bodyMarginLeft + blockMarginLeft + 1, drawY + blockMarginTop, block.text, block.style);
			}
			break;

		case BlockType::Paragraph: {
			auto lines = wrapText(block.text, kWrapCols);
			int lineY = drawY + blockMarginTop;
			for (const std::string& ln : lines) {
				drawTextAtStyled(s_windowId, kContentX + kIndent + bodyMarginLeft + blockMarginLeft, lineY, ln, block.style);
				lineY += kLineH;
			}
			break;
		}

		case BlockType::ListItem: {
			// Dash bullet + indented wrapped text
			drawTextAtStyled(s_windowId, kContentX + kIndent + bodyMarginLeft + blockMarginLeft, drawY + blockMarginTop, "-", block.style);
			auto lines = wrapText(block.text, kListWrapCols);
			int lineY = drawY + blockMarginTop;
			for (const std::string& ln : lines) {
				drawTextAtStyled(s_windowId, kContentX + kListIndent + bodyMarginLeft + blockMarginLeft, lineY, ln, block.style);
				lineY += kLineH;
			}
			break;
		}

		case BlockType::Preformatted: {
			// Light background box for the pre block
			int preH = wrappedBlockHeight(block.text, kPreWrapCols, true) + blockPadding * 2;
			int preR = 230;
			int preG = 232;
			int preB = 238;
			if (block.style.hasBackgroundColor) {
				colorChannels(block.style.backgroundColor, preR, preG, preB);
			}
			drawRect(s_windowId, kContentX + kPreIndent + bodyMarginLeft + blockMarginLeft - 4, drawY + blockMarginTop - 2,
				kContentW - kPreIndent - 8, preH + 4, preR, preG, preB);
			// Draw each line preserving exact content
			auto lines = splitPreLines(block.text);
			int lineY = drawY + blockMarginTop + blockPadding;
			for (const std::string& ln : lines) {
				drawTextAtStyled(s_windowId, kContentX + kPreIndent + bodyMarginLeft + blockMarginLeft, lineY, ln, block.style);
				lineY += kLineH;
			}
			break;
		}

		case BlockType::Link: {
			// Full wrapped link block: underline + blue text
			// The entire bounding rect is clickable (TODO: per-line hit testing).
			auto lines = wrapText(block.text, kWrapCols);
			int lineY = drawY + blockMarginTop;
			int linkR = 55;
			int linkG = 110;
			int linkB = 210;
			if (block.style.hasColor) {
				colorChannels(block.style.color, linkR, linkG, linkB);
			}
			for (const std::string& ln : lines) {
				// Underline under each line
				int lineW = static_cast<int>(ln.size()) * kCharW;
				if (block.style.underline) {
					drawRect(s_windowId, kContentX + kIndent + bodyMarginLeft + blockMarginLeft, lineY + kLineH - 1,
						lineW, 1, linkR, linkG, linkB);
				}
				drawTextAtColored(s_windowId, kContentX + kIndent + bodyMarginLeft + blockMarginLeft, lineY, ln, linkR, linkG, linkB);
				lineY += kLineH;
			}
			break;
		}

		case BlockType::Image: {
			int imageW = 0;
			int imageH = 0;
			imageDisplaySize(block, imageW, imageH);
			const ImageInfo& info = imageInfoForBlock(block);
			const int imageX = kContentX + kIndent + bodyMarginLeft + blockMarginLeft;
			const int viewportTop = kContentY;
			const int viewportBottom = kToolbarH + 6 + kContentH;
			if (drawY + blockMarginTop >= viewportTop && drawY + blockMarginTop + imageH <= viewportBottom) {
				if (info.ok) {
					drawImage(s_windowId, imageX, drawY + blockMarginTop, imageW, imageH, info.drawPath);
				} else {
					drawRect(s_windowId, imageX, drawY + blockMarginTop, imageW, imageH, 232, 236, 242);
					drawRect(s_windowId, imageX, drawY + blockMarginTop, imageW, 1, 145, 153, 168);
					drawRect(s_windowId, imageX, drawY + blockMarginTop + imageH - 1, imageW, 1, 145, 153, 168);
					drawRect(s_windowId, imageX, drawY + blockMarginTop, 1, imageH, 145, 153, 168);
					drawRect(s_windowId, imageX + imageW - 1, drawY + blockMarginTop, 1, imageH, 145, 153, 168);
					drawTextAtStyled(s_windowId, imageX + 10, drawY + blockMarginTop + std::max(8, (imageH - kLineH) / 2),
						imagePlaceholderText(block, info), block.style);
				}
			}
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

	const std::string& status = s_hoverStatusText.empty() ? s_statusText : s_hoverStatusText;
	drawTextAt(s_windowId, 12, kWindowH - kStatusBarH + 6, status);
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
	case HitTarget::AddressBar:  next = "Click to edit address"; break;
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
	}
}

void Navigator::handleKeyPress(int keyCode, const std::string& action)
{
	if (action != "down") return;

	// --- Address bar editing mode ---
	if (s_addressFocused) {
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

	// --- Normal (unfocused) keyboard shortcuts ---
	if (keyCode == 33) {
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

	// Address bar hit region
	{
		Rect addrRect{ kAddressX, kAddressY, kAddressW, kAddressH };
		if (addrRect.contains(x, y)) return HitTarget::AddressBar;
	}

	for (int i = 0; i < static_cast<int>(s_currentDoc.blocks.size()); ++i) {
		if (s_currentDoc.blocks[i].type == BlockType::Link) {
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

void Navigator::loadUrl(const std::string& url)
{
	Logger::write(LogLevel::Info, std::string("Navigator loadUrl: ") + url);
	cleanupRemoteImageTempFiles();
	s_imageCache.clear();

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
	} else if (url == "about:navigator-runtime") {
		doc = buildRuntimeDocument();
	} else if (url.size() >= 7 && url.substr(0, 7) == "file://") {
		doc = loadFileUrl(url);
	} else if (url.rfind("http://", 0) == 0) {
		doc = loadHttpUrl(url);
	} else if (url.rfind("https://", 0) == 0) {
		doc = buildSimpleDocument(url,
			"HTTPS Unsupported",
			"HTTPS Unsupported",
			"Navigator does not support HTTPS or TLS yet. Use a plain http:// URL for this milestone.");
		NavigatorPageMetadata metadata;
		metadata.requestedUrl = url;
		metadata.finalUrl = url;
		metadata.sourceType = "unsupported";
		metadata.errorStatus = "HTTPS/TLS unsupported";
		storePageMetadata(std::move(metadata), doc);
	} else {
		doc = buildSimpleDocument(url,
			"Unsupported URL",
			"Unsupported URL",
			"Navigator supports about:, file://, and basic http:// URLs in this build.");
		NavigatorPageMetadata metadata;
		metadata.requestedUrl = url;
		metadata.finalUrl = url;
		metadata.sourceType = "unsupported";
		metadata.errorStatus = "Unsupported URL scheme";
		storePageMetadata(std::move(metadata), doc);
	}

	s_currentDoc      = std::move(doc);
	s_scrollOffset    = 0;
	s_documentHeight  = computeDocumentHeight();
	s_hoverStatusText.clear();
	s_hitLinkBlockIndex = -1;
	s_statusText      = "Ready";

	updateDisplay();
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
		"It renders guideWeb documents and supports local file:// browsing plus basic plain HTTP text pages.", ""});
	doc.blocks.push_back({BlockType::Heading,   "Features", ""});
	doc.blocks.push_back({BlockType::ListItem,  "Headings, paragraphs, lists, and preformatted blocks", ""});
	doc.blocks.push_back({BlockType::ListItem,  "Word-wrapped text for readable documents", ""});
	doc.blocks.push_back({BlockType::ListItem,  "Relative link resolution for file:// and http:// pages", ""});
	doc.blocks.push_back({BlockType::ListItem,  "Basic http:// GET for text/html and text/plain pages", ""});
	doc.blocks.push_back({BlockType::ListItem,  "Back / Forward / Reload / Home navigation", ""});
	doc.blocks.push_back({BlockType::ListItem,  "Bookmarks with persistent storage", ""});
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
	s_pageMetadata = std::move(metadata);
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
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Content type", m.contentType), ""});

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
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Document blocks", m.documentBlockCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Image blocks", m.imageBlockCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Local images", m.localImageCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Remote images", m.remoteImageCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Loaded images", m.loadedImageCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Failed images", m.failedImageCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Last image error", m.lastImageError), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS detected", yesNo(m.cssDetected)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Style rule count", m.styleRuleCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Unsupported external stylesheets", m.unsupportedExternalStylesheetCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Unsupported CSS declarations", m.unsupportedCssDeclarationCount), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS style block capped", yesNo(m.cssStyleBlockCapped)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("CSS style bytes processed", static_cast<int>(m.cssStyleBytesProcessed)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Downloaded", yesNo(m.downloaded)), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Download saved path", m.downloadSavedPath), ""});
	doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Download byte count", static_cast<int>(m.downloadByteCount)), ""});
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
		s_pageMetadata.cssDetected));

	doc.blocks.push_back({BlockType::Link, "Page Info", "about:page-info"});
	doc.blocks.push_back({BlockType::Link, "View Source", "about:view-source"});
	doc.blocks.push_back({BlockType::Link, "View Downloads", "about:downloads"});
	doc.blocks.push_back({BlockType::Link, "Go to about:navigator", "about:navigator"});
	return doc;
}

WebDocument Navigator::buildDownloadsDocument()
{
	WebDocument doc;
	doc.url = "about:downloads";
	doc.title = "Downloads";
	doc.blocks.push_back({BlockType::Heading, "Downloads", ""});
	doc.blocks.push_back({BlockType::Paragraph,
		"Recent downloads are kept in memory for this Navigator session.", ""});

	if (s_recentDownloads.empty()) {
		doc.blocks.push_back({BlockType::Paragraph, "No downloads yet.", ""});
	} else {
		for (const DownloadItem& item : s_recentDownloads) {
			doc.blocks.push_back({BlockType::Heading,
				item.suggestedFileName.empty() ? "download.bin" : item.suggestedFileName, ""});
			doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Status", item.success ? "success" : "failed"), ""});
			doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Source URL", item.url), ""});
			doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Final URL", item.finalUrl), ""});
			doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Content type", item.contentType), ""});
			doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Byte count", static_cast<int>(item.byteCount)), ""});
			doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Saved path", item.savedPath), ""});
			if (!item.error.empty()) {
				doc.blocks.push_back({BlockType::ListItem, pageInfoLine("Error", item.error), ""});
			}
		}
	}

	doc.blocks.push_back({BlockType::Link, "Page Info", "about:page-info"});
	doc.blocks.push_back({BlockType::Link, "Navigator Runtime", "about:navigator-runtime"});
	doc.blocks.push_back({BlockType::Link, "Go to about:navigator", "about:navigator"});
	return doc;
}

WebDocument Navigator::loadHttpUrl(const std::string& url)
{
	Logger::write(LogLevel::Info, std::string("Navigator loadHttpUrl: ") + url);

	gxos::web::HttpResponse response = gxos::web::fetchHttpUrl(url);
	NavigatorPageMetadata metadata;
	metadata.requestedUrl = response.requestedUrl.empty() ? url : response.requestedUrl;
	metadata.finalUrl = response.finalUrl.empty() ? url : response.finalUrl;
	metadata.sourceType = "http";
	metadata.httpStatusCode = response.statusCode;
	metadata.httpReasonPhrase = response.reasonPhrase;
	metadata.contentType = response.contentType;
	metadata.redirectCount = response.redirectCount;
	metadata.redirected = response.redirectCount > 0 || metadata.requestedUrl != metadata.finalUrl;
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

	if (!response.ok()) {
		std::string title = "HTTP Error";
		if (response.error == gxos::web::HttpError::UnsupportedContentEncoding) {
			title = "Unsupported Content Encoding";
		} else if (response.error == gxos::web::HttpError::UnsupportedTransferEncoding) {
			title = "Unsupported Transfer Encoding";
		} else if (response.error == gxos::web::HttpError::MalformedChunkedEncoding) {
			title = "Malformed Chunked Response";
		} else if (response.error == gxos::web::HttpError::RedirectLimitExceeded) {
			title = "Redirect Limit Exceeded";
		} else if (response.error == gxos::web::HttpError::BodyTooLarge) {
			title = "Response Too Large";
		} else if (response.error == gxos::web::HttpError::Timeout) {
			title = "Network Timeout";
		}
		return finish(buildSimpleDocument(response.finalUrl.empty() ? url : response.finalUrl,
			title,
			title,
			std::string(gxos::web::httpErrorName(response.error)) + ": " + response.errorMessage));
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
		std::ostringstream reason;
		reason << "HTTP " << response.statusCode;
		if (!response.reasonPhrase.empty()) reason << " " << response.reasonPhrase;
		if (metadata.errorStatus.empty()) metadata.errorStatus = reason.str();
		return finish(buildErrorDocument(documentUrl, reason.str()));
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
	// Spacing constants
	constexpr int kHeadingPreGap = 10;  // extra space BEFORE a Heading (except the first)
	constexpr int kPreWrapCols   = (kContentW - 34) / kCharW;
	const     int kWrapCols      = (kContentW - 34) / kCharW;
	const     int kListWrapCols  = (kContentW - 44) / kCharW;

	int y = kHeadingY + (s_currentDoc.bodyStyle.marginTop >= 0 ? s_currentDoc.bodyStyle.marginTop : 0);
	for (int i = 0; i < blockIndex && i < static_cast<int>(s_currentDoc.blocks.size()); ++i) {
		const DocBlock& b    = s_currentDoc.blocks[i];
		const int marginTop = b.style.marginTop >= 0 ? b.style.marginTop : (b.type == BlockType::Heading ? 10 : 4);
		const int marginBottom = b.style.marginBottom >= 0 ? b.style.marginBottom : (b.type == BlockType::ListItem ? 4 : 8);
		const int padding = cssPaddingOrDefault(b.style, b.type == BlockType::Preformatted ? 4 : 0);
		const int headingHeight = std::max(kLineH + 4, cssFontSizeOrDefault(b.style, 20) + 2);
		// Apply pre-gap before the *next* block when the next block is a Heading
		// and the current block is not the first block.
		bool nextIsHeading = false;
		if (i + 1 < blockIndex &&
			i + 1 < static_cast<int>(s_currentDoc.blocks.size())) {
			nextIsHeading = (s_currentDoc.blocks[i + 1].type == BlockType::Heading);
		}
		int h = 0;
		switch (b.type) {
		case BlockType::Heading:
			h = marginTop + headingHeight + marginBottom;
			break;
		case BlockType::Paragraph:
			h = marginTop + wrappedBlockHeight(b.text, kWrapCols) + marginBottom + (nextIsHeading ? kHeadingPreGap : 0);
			break;
		case BlockType::Link:
			h = marginTop + wrappedBlockHeight(b.text, kWrapCols) + marginBottom + (nextIsHeading ? kHeadingPreGap : 0);
			break;
		case BlockType::ListItem:
			h = marginTop + wrappedBlockHeight(b.text, kListWrapCols) + marginBottom + (nextIsHeading ? kHeadingPreGap : 0);
			break;
		case BlockType::Preformatted:
			h = marginTop + wrappedBlockHeight(b.text, kPreWrapCols, true) + padding * 2 + marginBottom + (nextIsHeading ? kHeadingPreGap : 0);
			break;
		case BlockType::Image: {
			int imageW = 0;
			int imageH = 0;
			imageDisplaySize(b, imageW, imageH);
			h = marginTop + imageH + marginBottom + (nextIsHeading ? kHeadingPreGap : 0);
			break;
		}
		}
		y += h;
	}
	return y;
}

Navigator::Rect Navigator::linkBlockRect(int blockIndex)
{
	// The entire wrapped link height is clickable.
	// TODO: per-line hit testing when proportional text measurement is available.
	const int kWrapCols = (kContentW - 34) / kCharW;
	const DocBlock& block = s_currentDoc.blocks[blockIndex];
	const int bodyMarginLeft = s_currentDoc.bodyStyle.marginLeft >= 0 ? s_currentDoc.bodyStyle.marginLeft : 0;
	const int blockMarginLeft = block.style.marginLeft >= 0 ? block.style.marginLeft : 0;
	const int blockMarginTop = block.style.marginTop >= 0 ? block.style.marginTop : 4;
	int relY  = blockLayoutY(blockIndex);
	int drawY = kContentY + relY - s_scrollOffset + blockMarginTop;
	int h     = wrappedBlockHeight(block.text, kWrapCols);
	int w     = std::min(static_cast<int>(block.text.size()) * kCharW, kContentW - 34);
	return Rect{ kContentX + 18 + bodyMarginLeft + blockMarginLeft, drawY, w, h };
}

int Navigator::computeDocumentHeight()
{
	constexpr int kHeadingPreGap = 10;
	constexpr int kPreWrapCols   = (kContentW - 34) / kCharW;
	const     int kWrapCols      = (kContentW - 34) / kCharW;
	const     int kListWrapCols  = (kContentW - 44) / kCharW;

	int h = kHeadingY + (s_currentDoc.bodyStyle.marginTop >= 0 ? s_currentDoc.bodyStyle.marginTop : 0);
	const int n = static_cast<int>(s_currentDoc.blocks.size());
	for (int idx = 0; idx < n; ++idx) {
		const DocBlock& block = s_currentDoc.blocks[idx];
		const int marginTop = block.style.marginTop >= 0 ? block.style.marginTop : (block.type == BlockType::Heading ? 10 : 4);
		const int marginBottom = block.style.marginBottom >= 0 ? block.style.marginBottom : (block.type == BlockType::ListItem ? 4 : 8);
		const int padding = cssPaddingOrDefault(block.style, block.type == BlockType::Preformatted ? 4 : 0);
		const int headingFontSize = cssFontSizeOrDefault(block.style, block.tagName == "h1" ? 24 : (block.tagName == "h2" ? 20 : (block.tagName == "h3" ? 18 : 20)));
		const int headingHeight = std::max(kLineH + 4, headingFontSize + 2);
		bool nextIsHeading = (idx + 1 < n && s_currentDoc.blocks[idx + 1].type == BlockType::Heading);
		switch (block.type) {
		case BlockType::Heading:
			h += marginTop + headingHeight + marginBottom;
			break;
		case BlockType::Paragraph:
			h += marginTop + wrappedBlockHeight(block.text, kWrapCols) + marginBottom + (nextIsHeading ? kHeadingPreGap : 0);
			break;
		case BlockType::Link:
			h += marginTop + wrappedBlockHeight(block.text, kWrapCols) + marginBottom + (nextIsHeading ? kHeadingPreGap : 0);
			break;
		case BlockType::ListItem:
			h += marginTop + wrappedBlockHeight(block.text, kListWrapCols) + marginBottom + (nextIsHeading ? kHeadingPreGap : 0);
			break;
		case BlockType::Preformatted:
			h += marginTop + wrappedBlockHeight(block.text, kPreWrapCols, true) + padding * 2 + marginBottom + (nextIsHeading ? kHeadingPreGap : 0);
			break;
		case BlockType::Image: {
			int imageW = 0;
			int imageH = 0;
			imageDisplaySize(block, imageW, imageH);
			h += marginTop + imageH + marginBottom + (nextIsHeading ? kHeadingPreGap : 0);
			break;
		}
		}
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
