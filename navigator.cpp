#include "navigator.h"

#include "gui_protocol.h"
#include "ipc_bus.h"
#include "logger.h"
#include "navigator_file_io.h"
#include "navigator_html_parser.h"
#include "png_loader.h"
#include <algorithm>
#include <cctype>
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
std::vector<std::string> Navigator::s_backStack;
std::vector<std::string> Navigator::s_forwardStack;
std::vector<Bookmark>    Navigator::s_bookmarks;
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
		int naturalW = 0;
		int naturalH = 0;
		std::string filePath;
		std::string drawPath;
		std::string message;
	};

	static std::unordered_map<std::string, ImageInfo> s_imageCache;

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

	static std::string filePathFromUrl(const std::string& url)
	{
		if (url.rfind("file://", 0) != 0) return "";
		std::string path = url.substr(7);
		if (path.size() >= 2 && path[0] == '/' && path[1] == '/') path = path.substr(1);
		return path;
	}

	static bool readPngDimensions(const std::vector<uint8_t>& bytes, int& width, int& height)
	{
		static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
		if (bytes.size() < 24) return false;
		for (int i = 0; i < 8; ++i) {
			if (bytes[static_cast<size_t>(i)] != sig[i]) return false;
		}
		if (bytes[12] != 'I' || bytes[13] != 'H' || bytes[14] != 'D' || bytes[15] != 'R') return false;
		auto be32 = [&bytes](size_t pos) -> uint32_t {
			return (static_cast<uint32_t>(bytes[pos]) << 24) |
			       (static_cast<uint32_t>(bytes[pos + 1]) << 16) |
			       (static_cast<uint32_t>(bytes[pos + 2]) << 8) |
			       static_cast<uint32_t>(bytes[pos + 3]);
		};
		uint32_t w = be32(16);
		uint32_t h = be32(20);
		if (w == 0 || h == 0 || w > 4096 || h > 4096) return false;
		width = static_cast<int>(w);
		height = static_cast<int>(h);
		return true;
	}

	static const ImageInfo& imageInfoForBlock(const DocBlock& block)
	{
		const std::string key = block.url.empty() ? block.src : block.url;
		auto found = s_imageCache.find(key);
		if (found != s_imageCache.end()) return found->second;

		ImageInfo info;
		info.attempted = true;
		if (block.url.rfind("file://", 0) != 0) {
			info.unsupported = true;
			info.message = "[unsupported image]";
			auto inserted = s_imageCache.emplace(key, std::move(info));
			return inserted.first->second;
		}

		info.filePath = filePathFromUrl(block.url);
		if (!endsWithIgnoreCase(info.filePath, ".png")) {
			info.unsupported = true;
			info.message = "[unsupported image]";
			auto inserted = s_imageCache.emplace(key, std::move(info));
			return inserted.first->second;
		}

		BinaryReadResult br = readBinaryFile(info.filePath);
		if (br.status == FileReadStatus::NotFound) {
			info.message = "[missing image]";
		} else if (br.status == FileReadStatus::TooLarge) {
			info.tooLarge = true;
			info.message = "[image too large]";
		} else if (br.status == FileReadStatus::IoError) {
			info.message = "[image read error]";
		} else {
			int headerW = 0;
			int headerH = 0;
			if (!readPngDimensions(br.bytes, headerW, headerH)) {
				info.message = "[unsupported image]";
			} else {
				gxos::gui::ImagePtr decoded = gxos::gui::PngLoader::LoadFromMemory(br.bytes, info.filePath);
				if (decoded && decoded->isValid()) {
					info.ok = true;
					info.naturalW = decoded->Width;
					info.naturalH = decoded->Height;
					info.drawPath = imageLoaderPathForFile(info.filePath);
				} else {
					info.message = "[missing image]";
				}
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
	drawRect(s_windowId, kContentX, kToolbarH + 6, kContentW, kContentH, 245, 247, 250);
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

		// Skip blocks fully above or below the visible viewport
		int blockH = 0;
		bool nextIsHeading = (blockIndex + 1 < static_cast<int>(s_currentDoc.blocks.size()) &&
			s_currentDoc.blocks[blockIndex + 1].type == BlockType::Heading);
		constexpr int kPreGapIfNextHeading = 10;
		switch (block.type) {
		case BlockType::Heading:      blockH = kLineH + 4; break;
		case BlockType::Paragraph:    blockH = wrappedBlockHeight(block.text, kWrapCols)        + (nextIsHeading ? kPreGapIfNextHeading : 0); break;
		case BlockType::Link:         blockH = wrappedBlockHeight(block.text, kWrapCols)        + (nextIsHeading ? kPreGapIfNextHeading : 0); break;
		case BlockType::ListItem:     blockH = wrappedBlockHeight(block.text, kListWrapCols)    + (nextIsHeading ? kPreGapIfNextHeading : 0); break;
		case BlockType::Preformatted: blockH = wrappedBlockHeight(block.text, kPreWrapCols, true); break;
		case BlockType::Image: {
			int imageW = 0;
			int imageH = 0;
			imageDisplaySize(block, imageW, imageH);
			blockH = imageH + 4;
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
			drawRect(s_windowId, kContentX + kIndent, drawY + kLineH,
				kContentW - kIndent - 16, 2, 80, 140, 220);
			drawTextAt(s_windowId, kContentX + kIndent, drawY, block.text);
			break;

		case BlockType::Paragraph: {
			auto lines = wrapText(block.text, kWrapCols);
			int lineY = drawY;
			for (const std::string& ln : lines) {
				drawTextAt(s_windowId, kContentX + kIndent, lineY, ln);
				lineY += kLineH;
			}
			break;
		}

		case BlockType::ListItem: {
			// Dash bullet + indented wrapped text
			drawTextAt(s_windowId, kContentX + kIndent, drawY, "-");
			auto lines = wrapText(block.text, kListWrapCols);
			int lineY = drawY;
			for (const std::string& ln : lines) {
				drawTextAt(s_windowId, kContentX + kListIndent, lineY, ln);
				lineY += kLineH;
			}
			break;
		}

		case BlockType::Preformatted: {
			// Light background box for the pre block
			int preH = wrappedBlockHeight(block.text, kPreWrapCols, true);
			drawRect(s_windowId, kContentX + kPreIndent - 4, drawY - 2,
				kContentW - kPreIndent - 8, preH + 4, 230, 232, 238);
			// Draw each line preserving exact content
			auto lines = splitPreLines(block.text);
			int lineY = drawY;
			for (const std::string& ln : lines) {
				drawTextAt(s_windowId, kContentX + kPreIndent, lineY, ln);
				lineY += kLineH;
			}
			break;
		}

		case BlockType::Link: {
			// Full wrapped link block: underline + blue text
			// The entire bounding rect is clickable (TODO: per-line hit testing).
			auto lines = wrapText(block.text, kWrapCols);
			int lineY = drawY;
			for (const std::string& ln : lines) {
				// Underline under each line
				int lineW = static_cast<int>(ln.size()) * kCharW;
				drawRect(s_windowId, kContentX + kIndent, lineY + kLineH - 1,
					lineW, 1, 55, 110, 210);
				drawTextAt(s_windowId, kContentX + kIndent, lineY, ln);
				lineY += kLineH;
			}
			break;
		}

		case BlockType::Image: {
			int imageW = 0;
			int imageH = 0;
			imageDisplaySize(block, imageW, imageH);
			const ImageInfo& info = imageInfoForBlock(block);
			const int imageX = kContentX + kIndent;
			const int viewportTop = kContentY;
			const int viewportBottom = kToolbarH + 6 + kContentH;
			if (drawY >= viewportTop && drawY + imageH <= viewportBottom) {
				if (info.ok) {
					drawImage(s_windowId, imageX, drawY, imageW, imageH, info.drawPath);
				} else {
					drawRect(s_windowId, imageX, drawY, imageW, imageH, 232, 236, 242);
					drawRect(s_windowId, imageX, drawY, imageW, 1, 145, 153, 168);
					drawRect(s_windowId, imageX, drawY + imageH - 1, imageW, 1, 145, 153, 168);
					drawRect(s_windowId, imageX, drawY, 1, imageH, 145, 153, 168);
					drawRect(s_windowId, imageX + imageW - 1, drawY, 1, imageH, 145, 153, 168);
					drawTextAt(s_windowId, imageX + 10, drawY + std::max(8, (imageH - kLineH) / 2),
						imagePlaceholderText(block, info));
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
	s_imageCache.clear();

	WebDocument doc;
	if (url == "about:navigator" || url.empty()) {
		doc = buildAboutNavigatorDocument();
	} else if (url == "about:bookmarks") {
		doc = buildBookmarksDocument();
	} else if (url.size() >= 7 && url.substr(0, 7) == "file://") {
		doc = loadFileUrl(url);
	} else {
		// Unrecognised scheme: fall back to home placeholder.
		doc = buildNavigatorHomeDocument();
		doc.url = url;
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
		"It renders guideWeb documents and supports local file:// browsing.", ""});
	doc.blocks.push_back({BlockType::Heading,   "Features", ""});
	doc.blocks.push_back({BlockType::ListItem,  "Headings, paragraphs, lists, and preformatted blocks", ""});
	doc.blocks.push_back({BlockType::ListItem,  "Word-wrapped text for readable documents", ""});
	doc.blocks.push_back({BlockType::ListItem,  "Relative link resolution for file:// pages", ""});
	doc.blocks.push_back({BlockType::ListItem,  "Back / Forward / Reload / Home navigation", ""});
	doc.blocks.push_back({BlockType::ListItem,  "Bookmarks with persistent storage", ""});
	doc.blocks.push_back({BlockType::Heading,   "Quick Start", ""});
	doc.blocks.push_back({BlockType::Preformatted,
		"Type a file:// URL in the address bar and press Enter.\n"
		"Example: file:///docs/index.html", ""});
	doc.blocks.push_back({BlockType::Link, "Open guideXOS Help",   "file:///docs/index.html"});
	doc.blocks.push_back({BlockType::Link, "View Bookmarks",       "about:bookmarks"});
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

// -----------------------------------------------------------------------------
// Layout helpers
// -----------------------------------------------------------------------------

int Navigator::blockLayoutY(int blockIndex)
{
	// Returns the Y coordinate of blockIndex relative to kContentY (pre-scroll).
	// Spacing constants
	constexpr int kHeadingGap    = 14;  // space after a Heading block
	constexpr int kHeadingPreGap = 10;  // extra space BEFORE a Heading (except the first)
	constexpr int kBlockGap      = 8;   // space after other blocks
	constexpr int kPreWrapCols   = (kContentW - 34) / kCharW;
	const     int kWrapCols      = (kContentW - 34) / kCharW;
	const     int kListWrapCols  = (kContentW - 44) / kCharW;

	int y = kHeadingY;
	for (int i = 0; i < blockIndex && i < static_cast<int>(s_currentDoc.blocks.size()); ++i) {
		const DocBlock& b    = s_currentDoc.blocks[i];
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
			h = (kLineH + 4) + kHeadingGap;
			break;
		case BlockType::Paragraph:
			h = wrappedBlockHeight(b.text, kWrapCols) + kBlockGap + (nextIsHeading ? kHeadingPreGap : 0);
			break;
		case BlockType::Link:
			h = wrappedBlockHeight(b.text, kWrapCols) + kBlockGap + (nextIsHeading ? kHeadingPreGap : 0);
			break;
		case BlockType::ListItem:
			h = wrappedBlockHeight(b.text, kListWrapCols) + kBlockGap + (nextIsHeading ? kHeadingPreGap : 0);
			break;
		case BlockType::Preformatted:
			h = wrappedBlockHeight(b.text, kPreWrapCols, true) + 8 + kBlockGap + (nextIsHeading ? kHeadingPreGap : 0);
			break;
		case BlockType::Image: {
			int imageW = 0;
			int imageH = 0;
			imageDisplaySize(b, imageW, imageH);
			h = imageH + 4 + kBlockGap + (nextIsHeading ? kHeadingPreGap : 0);
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
	int relY  = blockLayoutY(blockIndex);
	int drawY = kContentY + relY - s_scrollOffset;
	int h     = wrappedBlockHeight(s_currentDoc.blocks[blockIndex].text, kWrapCols);
	int w     = std::min(
		static_cast<int>(s_currentDoc.blocks[blockIndex].text.size()) * kCharW,
		kContentW - 34);
	return Rect{ kContentX + 18, drawY, w, h };
}

int Navigator::computeDocumentHeight()
{
	constexpr int kHeadingGap    = 14;
	constexpr int kHeadingPreGap = 10;
	constexpr int kBlockGap      = 8;
	constexpr int kPreWrapCols   = (kContentW - 34) / kCharW;
	const     int kWrapCols      = (kContentW - 34) / kCharW;
	const     int kListWrapCols  = (kContentW - 44) / kCharW;

	int h = kHeadingY;
	const int n = static_cast<int>(s_currentDoc.blocks.size());
	for (int idx = 0; idx < n; ++idx) {
		const DocBlock& block = s_currentDoc.blocks[idx];
		bool nextIsHeading = (idx + 1 < n &&
			s_currentDoc.blocks[idx + 1].type == BlockType::Heading);
		switch (block.type) {
		case BlockType::Heading:
			h += (kLineH + 4) + kHeadingGap;
			break;
		case BlockType::Paragraph:
			h += wrappedBlockHeight(block.text, kWrapCols) + kBlockGap + (nextIsHeading ? kHeadingPreGap : 0);
			break;
		case BlockType::Link:
			h += wrappedBlockHeight(block.text, kWrapCols) + kBlockGap + (nextIsHeading ? kHeadingPreGap : 0);
			break;
		case BlockType::ListItem:
			h += wrappedBlockHeight(block.text, kListWrapCols) + kBlockGap + (nextIsHeading ? kHeadingPreGap : 0);
			break;
		case BlockType::Preformatted:
			h += wrappedBlockHeight(block.text, kPreWrapCols, true) + 8 + kBlockGap + (nextIsHeading ? kHeadingPreGap : 0);
			break;
		case BlockType::Image: {
			int imageW = 0;
			int imageH = 0;
			imageDisplaySize(block, imageW, imageH);
			h += imageH + 4 + kBlockGap + (nextIsHeading ? kHeadingPreGap : 0);
			break;
		}
		}
	}
	return h + kBlockGap;  // trailing padding
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

	FileReadResult fr = readTextFile(path);

	if (fr.status == FileReadStatus::NotFound) {
		return buildErrorDocument(url, "File not found: " + url);
	}
	if (fr.status == FileReadStatus::TooLarge) {
		return buildErrorDocument(url, "File too large to display: " + url);
	}
	if (fr.status == FileReadStatus::IoError) {
		return buildErrorDocument(url, "I/O error reading: " + url);
	}

	// Decide whether to invoke the HTML parser or the plain-text path.
	bool isHtml = false;
	{
		// Check the last 5 chars of the path for .html / .htm (case-insensitive).
		std::string ext;
		size_t dot = path.rfind('.');
		if (dot != std::string::npos) {
			ext = path.substr(dot);
			for (char& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		}
		isHtml = (ext == ".html" || ext == ".htm");
	}

	if (isHtml) {
		// Delegate to the HTML parser; it handles title, headings, paragraphs, links.
		try {
			WebDocument doc = parseHtml(url, fr.text);
			if (doc.title.empty()) {
				// fallback title from filename
				size_t slash = path.rfind('/');
				doc.title = (slash != std::string::npos) ? path.substr(slash + 1) : path;
			}
			return doc;
		} catch (...) {
			return buildErrorDocument(url, "HTML parse error for: " + url);
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

	return doc;
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
