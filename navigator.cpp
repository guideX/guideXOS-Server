#include "navigator.h"

#include "gui_protocol.h"
#include "ipc_bus.h"
#include "logger.h"
#include <algorithm>
#include <sstream>

namespace gxos {
namespace apps {

using namespace gxos::gui;

uint64_t Navigator::s_windowId = 0;
int Navigator::s_scrollOffset = 0;
std::string Navigator::s_addressText = "file:///docs/index.html";
std::string Navigator::s_statusText = "Ready.";
std::string Navigator::s_hoverStatusText;

namespace {
	constexpr int kWindowW = 920;
	constexpr int kWindowH = 640;
	constexpr int kToolbarH = 64;
	constexpr int kStatusBarH = 24;
	constexpr int kButtonY = 12;
	constexpr int kButtonW = 72;
	constexpr int kButtonH = 26;
	constexpr int kButtonGap = 10;
	constexpr int kAddressX = 338;
	constexpr int kAddressY = 12;
	constexpr int kAddressH = 26;
	constexpr int kAddressW = 920 - kAddressX - 20;
	constexpr int kContentX = 24;
	constexpr int kContentY = kToolbarH + 18;
	constexpr int kContentW = 920 - 48;
	constexpr int kContentH = 640 - kToolbarH - kStatusBarH - 24;
	constexpr int kHeadingY = 24;
	constexpr int kParagraphY = 64;
	constexpr int kParagraph2Y = 82;
	constexpr int kLinkY = 122;
	constexpr int kDocumentHeight = 180;

	constexpr int kWidgetIdBack = 1;
	constexpr int kWidgetIdForward = 2;
	constexpr int kWidgetIdReload = 3;
	constexpr int kWidgetIdHome = 4;

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

	void addButton(uint64_t windowId, int id, int x, int y, int w, int h, const std::string& text)
	{
		publish(MsgType::MT_WidgetAdd, packWidgetAdd(windowId, 1, id, x, y, w, h, text));
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
	s_windowId = 0;
	s_scrollOffset = 0;
	s_addressText = "file:///docs/index.html";
	s_statusText = "Ready.";
	s_hoverStatusText.clear();

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
				HitTarget target = hitTest(x, y);
				if (button == 0 && action == "move") {
					updateHoverStatus(target);
				} else if (button == 1 && action == "down") {
					if (target == HitTarget::HelpLink) handleDocumentClick(target);
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

	publish(MsgType::MT_SetTitle, std::to_string(s_windowId) + "|guideXOS Navigator");
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

	drawTextAt(s_windowId, 20, 46, "Toolbar");
	drawRect(s_windowId, kAddressX, kAddressY, kAddressW, kAddressH, 18, 22, 30);
	drawRect(s_windowId, kAddressX, kAddressY, kAddressW, 1, 110, 120, 142);
	drawRect(s_windowId, kAddressX, kAddressY + kAddressH - 1, kAddressW, 1, 70, 78, 96);
	drawTextAt(s_windowId, kAddressX + 10, kAddressY + 7, s_addressText);
}

void Navigator::renderDocument()
{
	clampScrollOffset();

	drawRect(s_windowId, kContentX, kToolbarH + 6, kContentW, kContentH, 245, 247, 250);
	drawRect(s_windowId, kContentX, kToolbarH + 6, kContentW, 1, 186, 192, 204);
	drawRect(s_windowId, kContentX + kContentW - 12, kToolbarH + 6, 8, kContentH, 229, 232, 238);

	int offsetY = -s_scrollOffset;
	drawTextAt(s_windowId, kContentX + 18, kContentY + kHeadingY + offsetY, "guideXOS Navigator");
	drawTextAt(s_windowId, kContentX + 18, kContentY + kParagraphY + offsetY, "This is the first native guideXOS Server browser shell.");
	drawTextAt(s_windowId, kContentX + 18, kContentY + kParagraph2Y + offsetY, "HTML, CSS, JavaScript, and network loading will arrive in later steps.");

	Rect link = helpLinkRect();
	drawRect(s_windowId, link.x - 2, link.y + 12, link.w, 1, 55, 110, 210);
	drawTextAt(s_windowId, link.x, link.y, "Open guideXOS Help");

	if (maxScrollOffset() > 0) {
		int trackY = kToolbarH + 10;
		int trackH = kContentH - 8;
		int thumbH = std::max(22, (trackH * kContentH) / kDocumentHeight);
		int thumbY = trackY + ((trackH - thumbH) * s_scrollOffset) / maxScrollOffset();
		drawRect(s_windowId, kContentX + kContentW - 10, trackY, 6, trackH, 216, 220, 228);
		drawRect(s_windowId, kContentX + kContentW - 10, thumbY, 6, thumbH, 130, 138, 156);
	} else {
		// TODO: add richer scrolling once the compositor/window framework has a shared safe mouse-wheel pattern.
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

void Navigator::updateHoverStatus(HitTarget target)
{
	std::string next;
	switch (target) {
	case HitTarget::Back:
		next = "Back button";
		break;
	case HitTarget::Forward:
		next = "Forward button";
		break;
	case HitTarget::Reload:
		next = "Reload button";
		break;
	case HitTarget::Home:
		next = "Home button";
		break;
	case HitTarget::HelpLink:
		next = "Link: Open guideXOS Help";
		break;
	default:
		break;
	}

	if (next != s_hoverStatusText) {
		s_hoverStatusText = next;
		renderStatusBar();
	}
}

void Navigator::handleToolbarAction(int widgetId)
{
	switch (widgetId) {
	case kWidgetIdBack:
		updateStatus("Back is not implemented yet.");
		break;
	case kWidgetIdForward:
		updateStatus("Forward is not implemented yet.");
		break;
	case kWidgetIdReload:
		updateStatus("Reload requested for file:///docs/index.html.");
		break;
	case kWidgetIdHome:
		s_addressText = "file:///docs/index.html";
		updateStatus("Home opened: file:///docs/index.html.");
		break;
	default:
		break;
	}
}

void Navigator::handleDocumentClick(HitTarget target)
{
	if (target == HitTarget::HelpLink) {
		updateStatus("Open guideXOS Help is a placeholder for future guideWeb/guideNet integration.");
	}
}

void Navigator::handleKeyPress(int keyCode, const std::string& action)
{
	if (action != "down") return;

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

Navigator::HitTarget Navigator::hitTest(int x, int y)
{
	if (toolbarButtonRect(kWidgetIdBack).contains(x, y)) return HitTarget::Back;
	if (toolbarButtonRect(kWidgetIdForward).contains(x, y)) return HitTarget::Forward;
	if (toolbarButtonRect(kWidgetIdReload).contains(x, y)) return HitTarget::Reload;
	if (toolbarButtonRect(kWidgetIdHome).contains(x, y)) return HitTarget::Home;
	if (helpLinkRect().contains(x, y)) return HitTarget::HelpLink;
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
	default:
		break;
	}

	return Rect{ x, kButtonY, kButtonW, kButtonH };
}

Navigator::Rect Navigator::helpLinkRect()
{
	int offsetY = -s_scrollOffset;
	return Rect{ kContentX + 18, kContentY + kLinkY + offsetY, 150, 16 };
}

int Navigator::maxScrollOffset()
{
	int overflow = kDocumentHeight - kContentH;
	return overflow > 0 ? overflow : 0;
}

void Navigator::clampScrollOffset()
{
	s_scrollOffset = std::max(0, std::min(s_scrollOffset, maxScrollOffset()));
}

} // namespace apps
} // namespace gxos
