#include "include/kernel/phase7_input_proof.h"

#include "include/kernel/kernel_compositor.h"
#include "include/kernel/serial_debug.h"

namespace kernel {
namespace phase7_input_proof {

namespace {

class InputProofApp final : public app::KernelApp {
public:
    InputProofApp() : m_textWidget(-1), m_textLength(0), m_focusPassed(false),
                      m_keyboardPassed(false), m_textPassed(false), m_focusArmed(false) {
        m_name[0] = 'P'; m_name[1] = 'h'; m_name[2] = 'a'; m_name[3] = 's';
        m_name[4] = 'e'; m_name[5] = '7'; m_name[6] = ' '; m_name[7] = 'I';
        m_name[8] = 'n'; m_name[9] = 'p'; m_name[10] = 'u'; m_name[11] = 't';
        m_name[12] = ' '; m_name[13] = 'P'; m_name[14] = 'r'; m_name[15] = 'o';
        m_name[16] = 'o'; m_name[17] = 'f'; m_name[18] = '\0';
    }

    bool init() override {
        m_window = new app::KernelWindow();
        if (!m_window) return false;
        m_window->owner = this;
        m_window->x = 140;
        m_window->y = 120;
        m_window->w = 420;
        m_window->h = 220;
        m_window->flags = app::WF_VISIBLE | app::WF_TITLEBAR | app::WF_CLOSABLE;
        const char title[] = "Input Routing Proof";
        for (uint32_t i = 0; i + 1 < sizeof(title) && i < app::MAX_TITLE_LEN; ++i) {
            m_window->title[i] = title[i];
            if (title[i] == '\0') break;
        }
        m_window->title[sizeof(title) - 1] = '\0';
        const char label[] = "Real QEMU input -> common compositor";
        addLabel(18, 18, 380, 24, label);
        m_textWidget = addTextBox(18, 72, 380, 34, "");
        if (m_textWidget < 0 || !compositor::KernelCompositor::registerWindow(m_window)) {
            delete m_window;
            m_window = nullptr;
            return false;
        }
        m_state = app::AppState::Running;
        // Registration focuses a new common window.  Deliberately clear it so
        // the acceptance click must exercise the normal focus path.
        compositor::KernelCompositor::setFocus(0);
        m_focusArmed = true;
        m_initialX = m_window->x;
        m_initialY = m_window->y;
        return true;
    }

    void shutdown() override {}

    void draw(uint32_t, uint32_t, uint32_t, uint32_t) override {}

    void onWindowFocus() override {
        if (!m_focusArmed || m_focusPassed) return;
        m_focusPassed = true;
        serial::puts("[guideXOS] focus routing: PASS\n");
    }

    void onKeyChar(char c) override {
        if (m_textLength >= 63) return;
        app::Widget* text = getWidget(m_textWidget);
        if (!text) return;
        m_text[m_textLength] = c;
        ++m_textLength;
        m_text[m_textLength] = '\0';
        text->text[m_textLength - 1] = c;
        text->text[m_textLength] = '\0';
        invalidate();
        const char expected[] = "guidexos";
        if (!m_keyboardPassed && m_textLength == sizeof(expected) - 1) {
            bool matches = true;
            for (uint32_t i = 0; i < sizeof(expected) - 1; ++i) {
                if (m_text[i] != expected[i]) matches = false;
            }
            if (matches) {
                m_keyboardPassed = true;
                serial::puts("[guideXOS] keyboard routing: PASS\n");
                m_textPassed = true;
                serial::puts("[guideXOS] text input: PASS value=guidexos\n");
            }
        }
    }

    bool focus_passed() const { return m_focusPassed; }
    bool keyboard_passed() const { return m_keyboardPassed; }
    bool text_passed() const { return m_textPassed; }
    int initial_x() const { return m_initialX; }
    int initial_y() const { return m_initialY; }

private:
    int m_textWidget;
    uint32_t m_textLength;
    bool m_focusPassed;
    bool m_keyboardPassed;
    bool m_textPassed;
    bool m_focusArmed;
    int m_initialX = 0;
    int m_initialY = 0;
    char m_text[64]{};
};

static InputProofApp* s_app = nullptr;

} // namespace

bool initialize()
{
    if (s_app) return true;
    s_app = new InputProofApp();
    if (!s_app || !s_app->init()) {
        delete s_app;
        s_app = nullptr;
        return false;
    }
    return true;
}

app::KernelWindow* window() { return s_app ? s_app->getWindow() : nullptr; }
int initial_x() { return s_app ? s_app->initial_x() : 0; }
int initial_y() { return s_app ? s_app->initial_y() : 0; }
bool keyboard_passed() { return s_app && s_app->keyboard_passed(); }
bool focus_passed() { return s_app && s_app->focus_passed(); }
bool text_passed() { return s_app && s_app->text_passed(); }

} // namespace phase7_input_proof
} // namespace kernel
