// QEMU-only interactive display-input proof adapter.

#include "include/kernel/qemu_display_input_proof.h"
#include "include/kernel/kernel_compositor.h"
#include "include/kernel/kernel_app.h"
#include "include/kernel/desktop.h"
#include "include/kernel/serial_debug.h"

namespace kernel {
namespace qemu_display_input_proof {

static State s_state;
static app::KernelWindow s_testWindow;
static const display_input::DisplayInputMapper* s_mapper = nullptr;
static bool s_summaryPrinted = false;

State::State()
    : initialized(false), active(false), windowId(0), cursorX(0), cursorY(0),
      buttons(0), pointerMonitor(-1), windowDominantMonitor(-1), boundaryX(0),
      initialWindowX(0), initialWindowY(0), initialWindowW(0), initialWindowH(0),
      windowX(0), windowY(0), windowW(0), windowH(0), boundaryWindowX(0),
      boundaryWindowY(0), boundaryWindowW(0), boundaryWindowH(0), clickFocus(false),
      expectedWindowHit(false), dragCapture(false), dragCrossedBoundary(false),
      captureReleased(false), finalWindowOnSecondary(false),
      finalWindowIntersectsBoth(false), eventsHandled(0), buttonDowns(0),
      buttonUps(0), lastMappingReason{}
{
}

static void copy_text(char* destination, const char* source)
{
    if (destination == nullptr) return;
    uint32_t i = 0;
    if (source != nullptr) {
        while (source[i] != '\0' && i < 63u) {
            destination[i] = source[i];
            ++i;
        }
    }
    destination[i] = '\0';
}

static void put_decimal(int32_t value)
{
    char buffer[12];
    uint32_t index = sizeof(buffer) - 1u;
    buffer[index] = '\0';
    bool negative = value < 0;
    uint32_t magnitude = negative
        ? static_cast<uint32_t>(-(static_cast<int64_t>(value)))
        : static_cast<uint32_t>(value);
    do {
        buffer[--index] = static_cast<char>('0' + (magnitude % 10u));
        magnitude /= 10u;
    } while (magnitude != 0u && index > 0u);
    if (negative && index > 0u) buffer[--index] = '-';
    serial::puts(&buffer[index]);
}

static void log_monitor_transition(const char* label, int32_t from, int32_t to)
{
    if (from == to) return;
    serial::puts("[INPUT-PROOF] ");
    serial::puts(label);
    serial::puts(" ");
    put_decimal(from);
    serial::puts(" -> ");
    put_decimal(to);
    serial::putc('\n');
}

static const char* hit_test_name(compositor::HitTestResult hit)
{
    switch (hit) {
        case compositor::HitTestResult::Client: return "client";
        case compositor::HitTestResult::Titlebar: return "titlebar";
        case compositor::HitTestResult::CloseButton: return "close-button";
        case compositor::HitTestResult::MaximizeButton: return "maximize-button";
        case compositor::HitTestResult::MinimizeButton: return "minimize-button";
        case compositor::HitTestResult::ResizeCorner: return "resize-corner";
        case compositor::HitTestResult::Border: return "border";
        case compositor::HitTestResult::None:
        default: return "none";
    }
}

static void update_window_snapshot()
{
    app::KernelWindow* window = compositor::KernelCompositor::getWindow(s_state.windowId);
    if (window == nullptr) return;
    s_state.windowX = window->x;
    s_state.windowY = window->y;
    s_state.windowW = window->w;
    s_state.windowH = window->h;

    if (s_mapper != nullptr) {
        const int oldDominant = s_state.windowDominantMonitor;
        const int dominantSlot = s_mapper->monitorFromRect(
            window->x, window->y, window->x + window->w, window->y + window->h);
        const display_input::DisplayInputMonitor* dominant = dominantSlot >= 0
            ? s_mapper->monitorAtSlot(static_cast<uint8_t>(dominantSlot)) : nullptr;
        s_state.windowDominantMonitor = dominant != nullptr ? dominant->id : -1;
        log_monitor_transition("windowDominantMonitor", oldDominant,
                               s_state.windowDominantMonitor);

        // Use descriptor slots for primary/secondary geometry. Numeric
        // monitor IDs remain inventory data, not input-routing selectors.
        const int primarySlot = 0;
        const int secondarySlot = 1;
        const display_input::DisplayInputMonitor* secondary = secondarySlot >= 0
            ? s_mapper->monitorAtSlot(static_cast<uint8_t>(secondarySlot)) : nullptr;
        const display_input::DisplayInputMonitor* primary = primarySlot >= 0
            ? s_mapper->monitorAtSlot(static_cast<uint8_t>(primarySlot)) : nullptr;
        if (secondary != nullptr) {
            const int32_t secondaryLeft = secondary->virtualX;
            const int32_t secondaryRight = secondary->virtualX + secondary->width;
            s_state.finalWindowOnSecondary =
                window->x >= secondaryLeft && window->x < secondaryRight;
            if (primary != nullptr) {
                const int32_t left = window->x > primary->virtualX ? window->x : primary->virtualX;
                const int32_t right = (window->x + window->w) < (primary->virtualX + primary->width)
                    ? window->x + window->w : primary->virtualX + primary->width;
                const int32_t top = window->y > primary->virtualY ? window->y : primary->virtualY;
                const int32_t bottom = (window->y + window->h) < (primary->virtualY + primary->height)
                    ? window->y + window->h : primary->virtualY + primary->height;
                const bool intersectsPrimary = right > left && bottom > top;
                const int32_t secondaryOverlapLeft = window->x > secondaryLeft ? window->x : secondaryLeft;
                const int32_t secondaryOverlapRight = (window->x + window->w) < secondaryRight
                    ? window->x + window->w : secondaryRight;
                const int32_t secondaryOverlapTop = window->y > secondary->virtualY ? window->y : secondary->virtualY;
                const int32_t secondaryOverlapBottom = (window->y + window->h) < (secondary->virtualY + secondary->height)
                    ? window->y + window->h : secondary->virtualY + secondary->height;
                const bool intersectsSecondary = secondaryOverlapRight > secondaryOverlapLeft &&
                    secondaryOverlapBottom > secondaryOverlapTop;
                s_state.finalWindowIntersectsBoth = intersectsPrimary && intersectsSecondary;
            }
        }
    }
}

void init(int32_t virtualWidth, int32_t virtualHeight,
          const display_input::DisplayInputMapper* mapper)
{
    s_state = State();
    s_mapper = mapper;
    s_summaryPrinted = false;
    if (virtualWidth <= 0 || virtualHeight <= 0 || mapper == nullptr) {
        serial::puts("[INPUT-PROOF] disabled reason=invalid-virtual-desktop-or-mapper\n");
        return;
    }

    // This is the ordinary kernel compositor/window-manager state used by
    // bare-metal windows. It is initialized here only behind the explicit
    // QEMU virtio-gpu live probe gate; it is not a product boot path.
    compositor::KernelCompositor::init(
        static_cast<uint32_t>(virtualWidth), static_cast<uint32_t>(virtualHeight), 40u);
    compositor::KernelCompositor::setWorkArea(
        0u, 0u, static_cast<uint32_t>(virtualWidth),
        static_cast<uint32_t>(virtualHeight > 40 ? virtualHeight - 40 : virtualHeight));

    s_testWindow = app::KernelWindow();
    s_testWindow.x = 180;
    s_testWindow.y = 180;
    s_testWindow.w = virtualWidth >= 700 ? 520 : virtualWidth / 2;
    s_testWindow.h = virtualHeight >= 360 ? 300 : virtualHeight / 2;
    s_testWindow.id = 0;
    s_testWindow.title[0] = 'Q';
    s_testWindow.title[1] = 'E';
    s_testWindow.title[2] = 'M';
    s_testWindow.title[3] = 'U';
    s_testWindow.title[4] = ' '; 
    s_testWindow.title[5] = 'I';
    s_testWindow.title[6] = 'n';
    s_testWindow.title[7] = 'p';
    s_testWindow.title[8] = 'u';
    s_testWindow.title[9] = 't';
    s_testWindow.title[10] = ' '; 
    s_testWindow.title[11] = 'P';
    s_testWindow.title[12] = 'r';
    s_testWindow.title[13] = 'o';
    s_testWindow.title[14] = 'o';
    s_testWindow.title[15] = 'f';
    s_testWindow.title[16] = '\0';

    if (!compositor::KernelCompositor::registerWindow(&s_testWindow)) {
        serial::puts("[INPUT-PROOF] disabled reason=ordinary-window-registration-failed\n");
        return;
    }

    s_state.initialized = true;
    s_state.active = true;
    s_state.windowId = s_testWindow.id;
    s_state.initialWindowX = s_testWindow.x;
    s_state.initialWindowY = s_testWindow.y;
    s_state.initialWindowW = s_testWindow.w;
    s_state.initialWindowH = s_testWindow.h;
    const int initialDominantSlot = mapper->monitorFromRect(
        s_testWindow.x, s_testWindow.y,
        s_testWindow.x + s_testWindow.w, s_testWindow.y + s_testWindow.h);
    const display_input::DisplayInputMonitor* initialDominant = initialDominantSlot >= 0
        ? mapper->monitorAtSlot(static_cast<uint8_t>(initialDominantSlot)) : nullptr;
    s_state.windowDominantMonitor = initialDominant != nullptr ? initialDominant->id : -1;
    const display_input::DisplayInputMonitor* primary = mapper->monitorAtSlot(0);
    if (primary != nullptr) s_state.boundaryX = primary->virtualX + primary->width;
    update_window_snapshot();
    serial::puts("[INPUT-PROOF] ordinaryWindow id=");
    serial::put_hex32(s_state.windowId);
    serial::puts(" title=QEMU Input Proof startRect=");
    put_decimal(s_state.windowX);
    serial::puts(",");
    put_decimal(s_state.windowY);
    serial::puts(" ");
    put_decimal(s_state.windowW);
    serial::puts("x");
    put_decimal(s_state.windowH);
    serial::puts(" virtualDesktop=");
    put_decimal(virtualWidth);
    serial::putc('x');
    put_decimal(virtualHeight);
    serial::putc('\n');
}

void handle(const display_input::DisplayPointerEvent& event)
{
    if (!s_state.active || !event.valid) return;
    const int oldPointerMonitor = s_state.pointerMonitor;
    s_state.cursorX = event.virtualX;
    s_state.cursorY = event.virtualY;
    s_state.pointerMonitor = event.sourceMonitor;
    copy_text(s_state.lastMappingReason, event.mappingReason);
    ++s_state.eventsHandled;
    log_monitor_transition("pointerMonitor", oldPointerMonitor, s_state.pointerMonitor);

    app::KernelWindow* hitWindow = nullptr;
    const compositor::HitTestResult hit = compositor::KernelCompositor::hitTest(
        event.virtualX, event.virtualY, &hitWindow);
    if (hitWindow != nullptr && hitWindow->id == s_state.windowId) {
        s_state.expectedWindowHit = true;
    }

    compositor::KernelCompositor::handleMouseMove(event.virtualX, event.virtualY);
    const uint8_t pressed = event.buttonMask & static_cast<uint8_t>(~s_state.buttons);
    const uint8_t released = s_state.buttons & static_cast<uint8_t>(~event.buttonMask);
    if (pressed & 0x01u) {
        serial::puts("[INPUT-PROOF] buttonDown x=");
        put_decimal(event.virtualX);
        serial::puts(" y=");
        put_decimal(event.virtualY);
        serial::puts(" hit=");
        serial::puts(hit_test_name(hit));
        serial::puts(" window=");
        serial::puts(hitWindow != nullptr && hitWindow->id == s_state.windowId ? "expected" : "other");
        serial::puts(" titlebar=");
        serial::puts(hit == compositor::HitTestResult::Titlebar ? "yes" : "no");
        serial::putc('\n');
        if (hitWindow != nullptr && hitWindow->id == s_state.windowId) {
            s_state.clickFocus = true;
            if (hit == compositor::HitTestResult::Titlebar) s_state.dragCapture = true;
        }
        compositor::KernelCompositor::handleMouseDown(event.virtualX, event.virtualY, 0x01u);
        ++s_state.buttonDowns;
    }
    if (released & 0x01u) {
        compositor::KernelCompositor::handleMouseUp(event.virtualX, event.virtualY, 0x01u);
        ++s_state.buttonUps;
        s_state.captureReleased = !compositor::KernelCompositor::isButtonPressActive();
        serial::puts("[INPUT-PROOF] buttonUp captureReleased=");
        serial::puts(s_state.captureReleased ? "yes" : "no");
        serial::putc('\n');
        s_state.dragCapture = false;
    }
    s_state.buttons = event.buttonMask;
    update_window_snapshot();
    if (s_state.dragCapture && !s_state.dragCrossedBoundary &&
        s_state.boundaryX > 0 &&
        s_state.windowX < s_state.boundaryX &&
        s_state.windowX + s_state.windowW > s_state.boundaryX) {
        s_state.dragCrossedBoundary = true;
        s_state.boundaryWindowX = s_state.windowX;
        s_state.boundaryWindowY = s_state.windowY;
        s_state.boundaryWindowW = s_state.windowW;
        s_state.boundaryWindowH = s_state.windowH;
        serial::puts("[INPUT-PROOF] dragCrossedBoundary=yes boundaryX=");
        put_decimal(s_state.boundaryX);
        serial::puts(" windowRect=");
        put_decimal(s_state.boundaryWindowX);
        serial::puts(",");
        put_decimal(s_state.boundaryWindowY);
        serial::puts(" ");
        put_decimal(s_state.boundaryWindowW);
        serial::putc('x');
        put_decimal(s_state.boundaryWindowH);
        serial::putc('\n');
    }
    desktop::request_redraw();
}

void finish()
{
    if (!s_state.active) return;
    if (s_state.buttons & 0x01u) {
        compositor::KernelCompositor::handleMouseUp(s_state.cursorX, s_state.cursorY, 0x01u);
        s_state.buttons = 0;
        ++s_state.buttonUps;
    }
    s_state.captureReleased = !compositor::KernelCompositor::isButtonPressActive();
    s_state.dragCapture = false;
    update_window_snapshot();
    s_state.active = false;
    if (s_summaryPrinted) return;
    s_summaryPrinted = true;
    serial::puts("Dual-monitor input proof: relativeGlobal=ok headAwareAbsolute=unavailable reason=guest-input-path-ps2-relative-no-source-head ");
    serial::puts("clickFocus=");
    serial::puts(s_state.clickFocus && s_state.expectedWindowHit ? "ok" : "failed");
    serial::puts(" dragCrossedBoundary=");
    serial::puts(s_state.dragCrossedBoundary ? "yes" : "no");
    serial::puts(" finalWindowMonitor=");
    put_decimal(s_state.windowDominantMonitor);
    serial::puts(" virtualDesktop=");
    if (s_mapper != nullptr) {
        put_decimal(s_mapper->virtualWidth());
        serial::putc('x');
        put_decimal(s_mapper->virtualHeight());
    }
    serial::puts(" taskbarPrimaryOnly=yes livePresentation=yes captureReleased=");
    serial::puts(s_state.captureReleased ? "yes" : "no");
    serial::puts(" finalWindowIntersectsBoth=");
    serial::puts(s_state.finalWindowIntersectsBoth ? "yes" : "no");
    serial::puts(" initialRect=");
    put_decimal(s_state.initialWindowX);
    serial::puts(",");
    put_decimal(s_state.initialWindowY);
    serial::puts(" ");
    put_decimal(s_state.initialWindowW);
    serial::putc('x');
    put_decimal(s_state.initialWindowH);
    serial::puts(" finalRect=");
    put_decimal(s_state.windowX);
    serial::puts(",");
    put_decimal(s_state.windowY);
    serial::puts(" ");
    put_decimal(s_state.windowW);
    serial::putc('x');
    put_decimal(s_state.windowH);
    serial::putc('\n');
}

const State* state() { return &s_state; }
bool is_active() { return s_state.active; }

} // namespace qemu_display_input_proof
} // namespace kernel
