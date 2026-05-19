#pragma once

#include "process.h"
#include <cstdint>
#include <string>

namespace gxos {
namespace apps {

class Navigator {
public:
	static uint64_t Launch();

private:
	struct Rect {
		int x;
		int y;
		int w;
		int h;

		bool contains(int px, int py) const {
			return px >= x && px < x + w && py >= y && py < y + h;
		}
	};

	enum class HitTarget : uint8_t {
		None = 0,
		Back,
		Forward,
		Reload,
		Home,
		HelpLink
	};

	static int main(int argc, char** argv);
	static void updateDisplay();
	static void renderToolbar();
	static void renderDocument();
	static void renderStatusBar();
	static void updateStatus(const std::string& status);
	static void updateHoverStatus(HitTarget target);
	static void handleToolbarAction(int widgetId);
	static void handleDocumentClick(HitTarget target);
	static void handleKeyPress(int keyCode, const std::string& action);
	static HitTarget hitTest(int x, int y);
	static Rect toolbarButtonRect(int widgetId);
	static Rect helpLinkRect();
	static int maxScrollOffset();
	static void clampScrollOffset();

	static uint64_t s_windowId;
	static int s_scrollOffset;
	static std::string s_addressText;
	static std::string s_statusText;
	static std::string s_hoverStatusText;
};

} // namespace apps
} // namespace gxos
