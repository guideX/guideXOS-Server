// ARM64 NativeElf architecture boundary.  The containing translation unit
// provides the normal bare-metal App Model namespace and ABI/VFS includes.

static const uint64_t kArm64PageSize = UINT64_C(0x1000);
static const uint64_t kArm64PreferredBase = UINT64_C(0x50000000);
static const uint32_t kArm64MaxPackages = 8;
static const uint32_t kArm64MaxManifestBytes = 8192;
static const uint32_t kArm64MaxProgramHeaders = 32;
static const uint32_t kArm64MaxImageBytes = 16u * 1024u * 1024u;
static const uint32_t kArm64ApplicationStackPages = 16;
static const uint64_t kArm64LowCanary = UINT64_C(0x504841534553544B);
static const uint64_t kArm64HighCanary = UINT64_C(0x41524D363453544B);
static const uint16_t kArm64ElfTypeExec = 2;
static const uint16_t kArm64ElfMachineAmd64 = 62;
static const uint16_t kArm64ElfMachine = 183;
static const uint32_t kArm64ElfLoad = 1;

extern "C" void phase3_serial_print(const char* text);
extern "C" void phase3_serial_dec(uint64_t value);
extern "C" uint8_t phase9_filesystem_vfs_active();
extern "C" uint8_t phase2_mmu_set_range_permissions(uint64_t base, uint64_t size,
                                                       uint8_t executable, uint8_t writable);
extern "C" gx_result gxos_native_call_on_stack(
    gx_result (GX_CALL *entry)(gx_app_context*), gx_app_context* context, uint8_t* stackTop);

struct Arm64Package {
    bool valid;
    char directory[64];
    char root[128];
    char id[80];
    char displayName[80];
    char icon[64];
    char executable[160];
    char entryPoint[48];
    char abi[64];
    char architecture[16];
    uint64_t executableBytes;
    bool startMenuVisible;
};

struct Arm64Elf64Header {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
};

struct Arm64Elf64ProgramHeader {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
};

static_assert(sizeof(Arm64Elf64Header) == 64, "ARM64 ELF header layout changed");
static_assert(sizeof(Arm64Elf64ProgramHeader) == 56, "ARM64 ELF program header layout changed");

struct Arm64Runtime;
class Arm64GuiOwner;

struct Arm64Runtime {
    Arm64Package* package;
    Arm64GuiOwner* guiOwner;
    gx_app_context context;
    gx_host_calls host;
    gx_result (GX_CALL *entry)(gx_app_context*);
    uint8_t* image;
    uint64_t imageBase;
    uint64_t imageBytes;
    uint64_t imagePages;
    uint8_t* stack;
    uint64_t stackBase;
    uint64_t stackBytes;
    uint64_t stackPages;
#if defined(GXOS_AARCH64_PHASE9)
    uint64_t preferredBase;
#endif
    bool entered;
    bool verbose;
#if defined(GXOS_AARCH64_PHASE8)
    uint64_t generation;
    gx_handle guiWindow;
    uint32_t guiWidgetCount;
    uint8_t guiCreated;
    uint8_t guiFocusPassed;
    uint8_t guiTextPassed;
    uint8_t guiButtonPassed;
    uint8_t guiClosePassed;
    uint8_t guiDragPassed;
    uint8_t reservedGuiFlags[2];
    bool guiAutoClose;
    bool guiAutoCloseIssued;
    bool guiPollEntered;
    bool guiPumpTraceDone;
    bool guiPumpObservedInput;
    int32_t guiInitialX;
    int32_t guiInitialY;
    uint64_t guiInitialHash;
    uint64_t guiTextHash;
    uint64_t guiButtonHash;
    uint64_t guiDragHash;
#endif
};

static Arm64Package s_arm64Packages[kArm64MaxPackages];
static uint32_t s_arm64PackageCount = 0;
static char s_arm64Manifest[kArm64MaxManifestBytes];
static bool s_arm64Discovered = false;
static bool s_arm64LaunchInProgress = false;
static bool s_arm64LastWrongArchitecture = false;
static uint32_t s_arm64SuccessfulLaunches = 0;
#if defined(GXOS_AARCH64_PHASE8)
static uint64_t s_arm64GuiGeneration = 0;
static uint32_t s_arm64GuiLaunches = 0;
#endif

static void arm64_print(const char* text) { phase3_serial_print(text ? text : ""); }

#if defined(GXOS_AARCH64_PHASE8)
static const uint64_t kArm64GuiHandleMagic = UINT64_C(0x4758000000000000);
static const uint32_t kArm64GuiWindowHandleKind = 1;
static const uint32_t kArm64GuiWidgetHandleKind = 2;
static const uint32_t kArm64GuiEventCapacity = 32;
static const uint32_t kArm64GuiMaxDimension = 640;

static bool arm64_equal(const char* left, const char* right);

static gx_handle arm64_gui_handle(const Arm64Runtime* runtime, uint32_t kind, uint32_t id)
{
    if (!runtime || runtime->generation == 0 || id == 0) return 0;
    return kArm64GuiHandleMagic |
        ((runtime->generation & UINT64_C(0xffff)) << 32) |
        ((static_cast<uint64_t>(kind) & UINT64_C(0xf)) << 28) |
        (static_cast<uint64_t>(id) & UINT64_C(0x0fffffff));
}

static bool arm64_gui_string(const char* value)
{
    if (!value) return false;
    for (uint32_t i = 0; i < GX_ABI_MAX_STRING_BYTES; ++i) {
        if (value[i] == '\0') return true;
    }
    return false;
}

static bool arm64_gui_copy_string(char* destination, uint32_t capacity, const char* value)
{
    if (!destination || capacity == 0 || !arm64_gui_string(value)) return false;
    uint32_t i = 0;
    while (value[i] && i + 1 < capacity) {
        destination[i] = value[i];
        ++i;
    }
    destination[i] = '\0';
    return value[i] == '\0';
}

class Arm64GuiOwner final : public app::KernelApp {
public:
    explicit Arm64GuiOwner(Arm64Runtime* runtime)
        : m_runtime(runtime), m_eventHead(0), m_eventTail(0), m_eventCount(0),
          m_focusedWidget(-1), m_closed(false), m_widgetHandles{} {}

    ~Arm64GuiOwner() override = default;

    bool init() override
    {
        m_state = app::AppState::Running;
        return true;
    }

    void shutdown() override
    {
        m_closed = true;
    }

    void draw(uint32_t, uint32_t, uint32_t, uint32_t) override {}

    bool create(const char* title, int32_t width, int32_t height, uint32_t flags);
    gx_handle create_widget(uint32_t type, int32_t x, int32_t y, int32_t width,
                            int32_t height, const char* text);
    bool set_widget_text(gx_handle handle, const char* text);
    bool set_widget_value(gx_handle handle, int32_t value);
    bool destroy_window(gx_handle handle);
    bool pop_event(gx_event* event);
    bool closed() const { return m_closed || !m_window; }
    app::KernelWindow* window() const { return m_window; }
    gx_handle window_handle() const { return m_runtime ? m_runtime->guiWindow : 0; }
    int initial_x() const { return m_runtime ? m_runtime->guiInitialX : 0; }
    int initial_y() const { return m_runtime ? m_runtime->guiInitialY : 0; }

    void onMouseMove(int x, int y) override
    {
        queue_event(GX_EVENT_MOUSE, window_handle(), x, y, GX_MOUSE_PACK(GX_MOUSE_BUTTON_NONE, GX_MOUSE_ACTION_MOVE));
    }

    void onMouseDown(int x, int y, uint8_t button) override
    {
        queue_event(GX_EVENT_MOUSE, window_handle(), x, y,
                    GX_MOUSE_PACK(button, GX_MOUSE_ACTION_DOWN));
    }

    void onMouseUp(int x, int y, uint8_t button) override
    {
        queue_event(GX_EVENT_MOUSE, window_handle(), x, y,
                    GX_MOUSE_PACK(button, GX_MOUSE_ACTION_UP));
    }

    void onKeyDown(uint32_t key) override
    {
        queue_event(GX_EVENT_KEY, window_handle(), static_cast<int32_t>(key),
                    GX_KEY_ACTION_DOWN, 0);
    }

    void onWidgetClick(int widgetId) override
    {
        if (!m_window || widgetId < 0 || widgetId >= m_window->widgetCount) return;
        app::Widget* widget = getWidget(widgetId);
        if (!widget || !widget->enabled || !widget->visible) return;
        if (widget->type == app::WidgetType::TextBox) {
            m_focusedWidget = widgetId;
            if (m_runtime) {
                arm64_print("[guideXOS] phase8 desktop input: textbox focus callback\n");
            }
        } else if (widget->type == app::WidgetType::Button && m_runtime) {
            m_runtime->guiButtonPassed = 1;
            arm64_print("[guideXOS] phase8 desktop input: button callback\n");
        }
        queue_event(GX_EVENT_WIDGET, widget_handle(widgetId), widgetId, 0, 0);
    }

    void onKeyChar(char c) override
    {
        if (!m_window || m_focusedWidget < 0) return;
        app::Widget* widget = getWidget(m_focusedWidget);
        if (!widget || widget->type != app::WidgetType::TextBox ||
            !widget->enabled || !widget->visible) return;
        uint32_t length = 0;
        while (length < sizeof(widget->text) && widget->text[length]) ++length;
        if (length + 1 >= sizeof(widget->text)) return;
        widget->text[length] = c;
        widget->text[length + 1] = '\0';
        if (m_runtime && !m_runtime->guiTextPassed) {
            arm64_print("[guideXOS] phase8 desktop input: key callback\n");
        }
        invalidate();
        queue_event(GX_EVENT_TEXT_INPUT, widget_handle(m_focusedWidget), static_cast<uint8_t>(c), 0, 0);
        if (arm64_equal(widget->text, "arm64") && m_runtime && !m_runtime->guiTextPassed) {
            desktop::draw();
            m_runtime->guiTextPassed = 1;
            m_runtime->guiTextHash = framebuffer::verification_hash(
                static_cast<uint32_t>(m_window->x), static_cast<uint32_t>(m_window->y),
                static_cast<uint32_t>(m_window->w), static_cast<uint32_t>(m_window->h));
        }
    }

    void onWindowClose() override
    {
        if (m_runtime && !m_runtime->guiClosePassed) {
            m_runtime->guiClosePassed = 1;
            queue_event(GX_EVENT_WINDOW_CLOSE, window_handle(), 0, 0, 0);
        }
        m_closed = true;
    }

    void onWindowFocus() override
    {
        if (m_runtime && m_runtime->guiWindow != 0) {
            m_runtime->guiFocusPassed = 1;
            queue_event(GX_EVENT_WINDOW_FOCUS, window_handle(), 0, 0, 0);
        }
    }

    void onWindowBlur() override
    {
        if (m_runtime && m_runtime->guiWindow != 0)
            queue_event(GX_EVENT_WINDOW_BLUR, window_handle(), 0, 0, 0);
    }

private:
    gx_handle widget_handle(int widgetId) const
    {
        return widgetId >= 0 && widgetId < app::KernelWindow::MAX_WIDGETS
            ? m_widgetHandles[widgetId] : 0;
    }

    void queue_event(gx_event_type type, gx_handle object, int32_t p1, int32_t p2, int32_t p3)
    {
        if (m_eventCount >= kArm64GuiEventCapacity) {
            // A close request is a lifecycle event and must not be lost behind
            // stale pointer motion.  Drop the oldest queued event only for a
            // close; all other events use bounded drop-newest semantics.
            if (type != GX_EVENT_WINDOW_CLOSE) return;
            m_eventHead = (m_eventHead + 1) % kArm64GuiEventCapacity;
            --m_eventCount;
        }
        gx_event& event = m_events[m_eventTail];
        event.size = sizeof(gx_event);
        event.type = type;
        event.window = object;
        event.param1 = p1;
        event.param2 = p2;
        event.param3 = p3;
        event.param4 = 0;
        m_eventTail = (m_eventTail + 1) % kArm64GuiEventCapacity;
        ++m_eventCount;
    }

    Arm64Runtime* m_runtime;
    gx_event m_events[kArm64GuiEventCapacity];
    uint32_t m_eventHead;
    uint32_t m_eventTail;
    uint32_t m_eventCount;
    int m_focusedWidget;
    bool m_closed;
    gx_handle m_widgetHandles[app::KernelWindow::MAX_WIDGETS];
};
#endif

static bool arm64_add(uint64_t left, uint64_t right, uint64_t* result)
{
    if (!result || right > UINT64_MAX - left) return false;
    *result = left + right;
    return true;
}

static bool arm64_mul(uint64_t left, uint64_t right, uint64_t* result)
{
    if (!result || (left != 0 && right > UINT64_MAX / left)) return false;
    *result = left * right;
    return true;
}

static uint64_t arm64_align_down(uint64_t value)
{
    return value & ~(kArm64PageSize - 1);
}

static bool arm64_align_up(uint64_t value, uint64_t* result)
{
    if (!result || value > UINT64_MAX - (kArm64PageSize - 1)) return false;
    *result = (value + kArm64PageSize - 1) & ~(kArm64PageSize - 1);
    return true;
}

static uint32_t arm64_length(const char* text)
{
    uint32_t result = 0;
    if (text) while (text[result]) ++result;
    return result;
}

static bool arm64_equal(const char* left, const char* right)
{
    if (!left || !right) return false;
    uint32_t i = 0;
    while (left[i] && right[i]) {
        char a = left[i]; char b = right[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + ('a' - 'A'));
        if (a != b) return false;
        ++i;
    }
    return left[i] == '\0' && right[i] == '\0';
}

#if defined(GXOS_AARCH64_PHASE8)
bool Arm64GuiOwner::create(const char* title, int32_t width, int32_t height, uint32_t flags)
{
    if (!m_runtime || m_window || !arm64_gui_string(title) || width <= 0 || height <= 0 ||
        width > static_cast<int32_t>(kArm64GuiMaxDimension) ||
        height > static_cast<int32_t>(kArm64GuiMaxDimension)) return false;
    const uint32_t screenWidth = framebuffer::get_width();
    const uint32_t screenHeight = framebuffer::get_height();
    if (screenWidth < static_cast<uint32_t>(width) || screenHeight < static_cast<uint32_t>(height)) return false;

    app::KernelWindow* window = new app::KernelWindow();
    if (!window || !arm64_gui_copy_string(window->title, sizeof(window->title), title)) {
        delete window;
        return false;
    }
    window->w = width;
    window->h = height;
    window->x = static_cast<int>((screenWidth - static_cast<uint32_t>(width)) / 2u);
    window->y = static_cast<int>((screenHeight - static_cast<uint32_t>(height)) / 2u);
    window->flags = app::WF_VISIBLE | app::WF_TITLEBAR | app::WF_CLOSABLE;
    if (flags & GX_WINDOW_FLAG_RESIZABLE) window->flags |= app::WF_RESIZABLE;
    window->owner = this;
    m_window = window;
    if (!compositor::KernelCompositor::registerWindow(m_window)) {
        m_window = nullptr;
        delete window;
        return false;
    }

    // Registration follows the common compositor contract and momentarily
    // focuses a new window.  Clear it here so Phase 8 can prove focus via a
    // real virtio-tablet click, exactly as a normal desktop launch would.
    m_runtime->guiWindow = arm64_gui_handle(m_runtime, kArm64GuiWindowHandleKind, m_window->id);
    m_runtime->guiInitialX = m_window->x;
    m_runtime->guiInitialY = m_window->y;
    m_runtime->guiCreated = 1;
    compositor::KernelCompositor::setFocus(0);
    arm64_print("[guideXOS] NativeElf GUI window: created\n");
    return init();
}

gx_handle Arm64GuiOwner::create_widget(uint32_t type, int32_t x, int32_t y, int32_t width,
                                       int32_t height, const char* text)
{
    if (!m_runtime || !m_window || !arm64_gui_string(text) || x < 0 || y < 0 || width <= 0 || height <= 0 ||
        width > static_cast<int32_t>(kArm64GuiMaxDimension) || height > static_cast<int32_t>(kArm64GuiMaxDimension)) return 0;
    const int32_t clientHeight = m_window->h - compositor::TITLEBAR_HEIGHT;
    if (x > m_window->w || y > clientHeight || width > m_window->w - x || height > clientHeight - y) return 0;
    int id = -1;
    switch (type) {
        case GX_WIDGET_LABEL: id = addLabel(x, y, width, height, text); break;
        case GX_WIDGET_BUTTON: id = addButton(x, y, width, height, text); break;
        case GX_WIDGET_TEXTBOX: id = addTextBox(x, y, width, height, text); break;
        default: return 0;
    }
    if (id < 0 || id >= app::KernelWindow::MAX_WIDGETS) return 0;
    const gx_handle handle = arm64_gui_handle(m_runtime, kArm64GuiWidgetHandleKind,
                                                static_cast<uint32_t>(id + 1));
    m_widgetHandles[id] = handle;
    ++m_runtime->guiWidgetCount;
    if (m_runtime->guiWidgetCount >= 5 && m_runtime->guiInitialHash == 0) {
        desktop::draw();
        m_runtime->guiInitialHash = framebuffer::verification_hash(
            static_cast<uint32_t>(m_window->x), static_cast<uint32_t>(m_window->y),
            static_cast<uint32_t>(m_window->w), static_cast<uint32_t>(m_window->h));
    }
    return handle;
}

bool Arm64GuiOwner::set_widget_text(gx_handle handle, const char* text)
{
    if (!m_runtime || !m_window || !arm64_gui_string(text)) return false;
    for (int i = 0; i < m_window->widgetCount; ++i) {
        if (m_widgetHandles[i] != handle) continue;
        setWidgetText(i, text);
        desktop::draw();
        if (m_runtime->guiButtonPassed && m_runtime->guiButtonHash == 0) {
            m_runtime->guiButtonHash = framebuffer::verification_hash(
                static_cast<uint32_t>(m_window->x), static_cast<uint32_t>(m_window->y),
                static_cast<uint32_t>(m_window->w), static_cast<uint32_t>(m_window->h));
        }
        return true;
    }
    return false;
}

bool Arm64GuiOwner::set_widget_value(gx_handle handle, int32_t value)
{
    if (!m_runtime || !m_window) return false;
    for (int i = 0; i < m_window->widgetCount; ++i) {
        if (m_widgetHandles[i] != handle) continue;
        setWidgetValue(i, value);
        return true;
    }
    return false;
}

bool Arm64GuiOwner::destroy_window(gx_handle handle)
{
    if (!m_runtime || !m_window || handle == 0 || handle != m_runtime->guiWindow) return false;
    requestClose();
    return true;
}

bool Arm64GuiOwner::pop_event(gx_event* event)
{
    if (!event || m_eventCount == 0) return false;
    *event = m_events[m_eventHead];
    m_eventHead = (m_eventHead + 1) % kArm64GuiEventCapacity;
    --m_eventCount;
    return true;
}
#endif

static bool arm64_copy(char* destination, uint32_t capacity, const char* source)
{
    if (!destination || capacity == 0 || !source) return false;
    uint32_t i = 0;
    while (source[i] && i + 1 < capacity) { destination[i] = source[i]; ++i; }
    destination[i] = '\0';
    return source[i] == '\0';
}

static const char* arm64_find(const char* text, const char* needle)
{
    if (!text || !needle || !needle[0]) return text;
    const uint32_t n = arm64_length(needle);
    for (const char* cursor = text; *cursor; ++cursor) {
        uint32_t i = 0;
        while (i < n && cursor[i] == needle[i]) ++i;
        if (i == n) return cursor;
    }
    return nullptr;
}

static bool arm64_json_between(const char* begin, const char* end, const char* key,
                               char* output, uint32_t capacity)
{
    if (!begin || !end || begin >= end || !key || !output || capacity == 0) return false;
    const uint32_t keyLength = arm64_length(key);
    for (const char* cursor = begin; cursor + keyLength < end; ++cursor) {
        uint32_t i = 0;
        while (i < keyLength && cursor[i] == key[i]) ++i;
        if (i != keyLength) continue;
        cursor += keyLength;
        while (cursor < end && *cursor != ':') ++cursor;
        if (cursor >= end) return false;
        ++cursor;
        while (cursor < end && *cursor != '"') ++cursor;
        if (cursor >= end) return false;
        ++cursor;
        uint32_t count = 0;
        while (cursor + count < end && cursor[count] != '"') ++count;
        if (cursor + count >= end || count + 1 > capacity) return false;
        for (uint32_t j = 0; j < count; ++j) output[j] = cursor[j];
        output[count] = '\0';
        return true;
    }
    return false;
}

static bool arm64_json(const char* json, const char* key, char* output, uint32_t capacity)
{
    return json && arm64_json_between(json, json + arm64_length(json), key, output, capacity);
}

static bool arm64_package_path(const Arm64Package* package, const char* relative,
                               char* output, uint32_t capacity)
{
    if (!package || !relative || !relative[0] || relative[0] == '/') return false;
    const uint32_t rootLength = arm64_length(package->root);
    const uint32_t relativeLength = arm64_length(relative);
    if (rootLength == 0 || rootLength >= sizeof(package->root) || relativeLength == 0 ||
        relativeLength >= sizeof(package->executable) || rootLength + 1u + relativeLength + 1u > capacity) return false;
    uint32_t componentLength = 0;
    for (uint32_t i = 0; i < relativeLength; ++i) {
        const unsigned char character = static_cast<unsigned char>(relative[i]);
        if (character < 0x20 || character == 0x7f || relative[i] == '\\' || relative[i] == ':') return false;
        if (relative[i] == '/') {
            if (componentLength == 0 || componentLength == 1 && relative[i - componentLength] == '.' ||
                componentLength == 2 && relative[i - componentLength] == '.' && relative[i - componentLength + 1] == '.') return false;
            componentLength = 0;
            continue;
        }
        ++componentLength;
    }
    if (componentLength == 0 || componentLength == 1 && relative[relativeLength - 1] == '.' ||
        componentLength == 2 && relative[relativeLength - 2] == '.' && relative[relativeLength - 1] == '.') return false;
    for (uint32_t i = 0; i < rootLength; ++i) output[i] = package->root[i];
    output[rootLength] = '/';
    for (uint32_t i = 0; i < relativeLength; ++i) output[rootLength + 1 + i] = relative[i];
    output[rootLength + 1 + relativeLength] = '\0';
    return true;
}

static bool arm64_read_at(const char* path, uint64_t offset, void* buffer, uint32_t bytes)
{
    if (!path || !buffer || bytes == 0) return false;
    const uint8_t handle = vfs::open(path, vfs::OPEN_READ);
    if (handle == 0xff) return false;
    bool okay = vfs::seek(handle, static_cast<int64_t>(offset), vfs::SEEK_SET) == vfs::VFS_OK;
    uint32_t total = 0;
    while (okay && total < bytes) {
        const int32_t amount = vfs::read(handle, static_cast<uint8_t*>(buffer) + total, bytes - total);
        if (amount <= 0) { okay = false; break; }
        total += static_cast<uint32_t>(amount);
    }
    vfs::close(handle);
    return okay && total == bytes;
}

static bool arm64_read_at_for_runtime(const Arm64Runtime* runtime, const char* path,
                                      uint64_t offset, void* buffer, uint32_t bytes)
{
#if defined(GXOS_AARCH64_PHASE9)
    static const uint32_t kPhase9CacheBytes = 64u * 1024u;
    struct ImageCache {
        const Arm64Package* package;
        uint32_t bytes;
        uint8_t data[kPhase9CacheBytes];
    };
    static ImageCache caches[2]{};
    if (!runtime || !runtime->package || runtime->package->executableBytes > kPhase9CacheBytes)
        return arm64_read_at(path, offset, buffer, bytes);
    const uint32_t slot = arm64_equal(runtime->package->id, "com.guidexos.phase9.appb") ? 1u : 0u;
    ImageCache& cache = caches[slot];
    if (cache.package != runtime->package) {
        if (!arm64_read_at(path, 0, cache.data,
                           static_cast<uint32_t>(runtime->package->executableBytes))) {
            cache.package = nullptr;
            cache.bytes = 0;
            return false;
        }
        cache.package = runtime->package;
        cache.bytes = static_cast<uint32_t>(runtime->package->executableBytes);
    }
    if (offset > cache.bytes || bytes > cache.bytes - offset) return false;
    for (uint32_t i = 0; i < bytes; ++i)
        static_cast<uint8_t*>(buffer)[i] = cache.data[offset + i];
    return true;
#else
    (void)runtime;
    return arm64_read_at(path, offset, buffer, bytes);
#endif
}

static bool arm64_parse_package(const char* directory, Arm64Package* package)
{
    if (!directory || !package) return false;
    for (uint32_t i = 0; i < sizeof(Arm64Package); ++i) reinterpret_cast<uint8_t*>(package)[i] = 0;
    if (!arm64_copy(package->directory, sizeof(package->directory), directory)) return false;
    package->root[0] = '/'; package->root[1] = 'A'; package->root[2] = 'p';
    package->root[3] = 'p'; package->root[4] = 's'; package->root[5] = '/';
    if (!arm64_copy(package->root + 6, sizeof(package->root) - 6, directory)) return false;
    char manifestPath[256];
    if (!arm64_package_path(package, "app.json", manifestPath, sizeof(manifestPath))) return false;
    const int32_t manifestBytes = vfs::read_file(manifestPath, s_arm64Manifest,
                                                 kArm64MaxManifestBytes - 1);
    if (manifestBytes <= 0) return false;
    s_arm64Manifest[manifestBytes] = '\0';
    char kind[32]; char runtime[32];
    if (!arm64_json(s_arm64Manifest, "\"kind\"", kind, sizeof(kind)) ||
        !arm64_equal(kind, "NativeElf") ||
        !arm64_json(s_arm64Manifest, "\"id\"", package->id, sizeof(package->id)) ||
        !arm64_json(s_arm64Manifest, "\"displayName\"", package->displayName, sizeof(package->displayName))) return false;

    const char* entries = arm64_find(s_arm64Manifest, "\"entries\"");
    const char* cursor = entries;
    bool selected = false;
    const char* currentArchitecture = gxos::apps::NativeArchitectureToString(
        gxos::apps::CurrentNativeArchitecture());
    while (cursor && *cursor) {
        const char* archKey = arm64_find(cursor, "\"architecture\"");
        if (!archKey) break;
        const char* objectEnd = arm64_find(archKey, "}");
        if (!objectEnd) break;
        char architecture[16];
        if (arm64_json_between(archKey, objectEnd, "\"architecture\"", architecture, sizeof(architecture)) &&
            arm64_equal(architecture, currentArchitecture)) {
            if (!arm64_json_between(archKey, objectEnd, "\"path\"", package->executable, sizeof(package->executable)) ||
                !arm64_json_between(archKey, objectEnd, "\"entryPoint\"", package->entryPoint, sizeof(package->entryPoint)) ||
                !arm64_json_between(archKey, objectEnd, "\"abi\"", package->abi, sizeof(package->abi))) return false;
            arm64_copy(package->architecture, sizeof(package->architecture), currentArchitecture);
            selected = true;
            break;
        }
        cursor = objectEnd + 1;
    }
    if (!selected || !arm64_equal(package->entryPoint, "gx_main") ||
        !arm64_equal(package->abi, GX_ABI_NAME) ||
        !arm64_json(s_arm64Manifest, "\"runtime\"", runtime, sizeof(runtime)) ||
        !arm64_equal(runtime, "native-elf")) return false;
    char executablePath[256];
    if (!arm64_package_path(package, package->executable, executablePath, sizeof(executablePath))) return false;
    vfs::FileInfo info{};
    if (vfs::stat(executablePath, &info) != vfs::VFS_OK || info.type != vfs::FILE_TYPE_REGULAR ||
        info.size == 0 || info.size > UINT32_MAX) return false;
    package->executableBytes = info.size;
    package->valid = true;
#if defined(GXOS_AARCH64_PHASE10)
    arm64_print("[guideXOS] package architecture: "); arm64_print(package->architecture); arm64_print("\n");
    arm64_print("[guideXOS] selected payload: "); arm64_print(package->executable); arm64_print("\n");
#endif
    arm64_print("[guideXOS] App discovery: ARM64 proof app found package=");
    arm64_print(package->directory); arm64_print(" id="); arm64_print(package->id);
    arm64_print(" manifest="); arm64_print(manifestPath); arm64_print(" executable=");
    arm64_print(executablePath); arm64_print("\n");
    return true;
}

void discover()
{
    s_arm64PackageCount = 0;
    s_arm64Discovered = true;
    arm64_print("[guideXOS] App Model: initialized\n");
    const uint8_t iterator = vfs::opendir("/Apps");
    if (iterator == 0xff) {
        s_arm64Discovered = false;
        arm64_print("[guideXOS] App discovery: /Apps missing\n");
        return;
    }
    char candidates[kArm64MaxPackages][64]{};
    uint32_t candidateCount = 0;
    vfs::DirEntry entry{};
    while (candidateCount < kArm64MaxPackages && vfs::readdir(iterator, &entry)) {
        if (entry.type != vfs::FILE_TYPE_DIRECTORY || entry.name[0] == '.') continue;
        arm64_copy(candidates[candidateCount], sizeof(candidates[candidateCount]), entry.name);
        ++candidateCount;
    }
    vfs::closedir(iterator);
    for (uint32_t i = 0; i < candidateCount && s_arm64PackageCount < kArm64MaxPackages; ++i) {
        if (arm64_parse_package(candidates[i], &s_arm64Packages[s_arm64PackageCount])) ++s_arm64PackageCount;
    }
    arm64_print("[guideXOS] App discovery count="); phase3_serial_dec(s_arm64PackageCount); arm64_print("\n");
}

static Arm64Package* arm64_find_package(const char* name)
{
    if (!name) return nullptr;
    for (uint32_t i = 0; i < s_arm64PackageCount; ++i) {
        if (arm64_equal(name, s_arm64Packages[i].directory) ||
            arm64_equal(name, s_arm64Packages[i].id) ||
            arm64_equal(name, s_arm64Packages[i].displayName)) return &s_arm64Packages[i];
    }
    return nullptr;
}

bool is_available(const char* name)
{
    if (!s_arm64Discovered) discover();
    return arm64_find_package(name) != nullptr;
}

const PackageInfo* lookup_package(const char* name)
{
    if (!s_arm64Discovered) discover();
    return reinterpret_cast<const PackageInfo*>(arm64_find_package(name));
}

uint32_t package_count()
{
    if (!s_arm64Discovered) discover();
    return s_arm64PackageCount;
}

const PackageInfo* package_at(uint32_t index)
{
    if (!s_arm64Discovered) discover();
    return index < s_arm64PackageCount ? reinterpret_cast<const PackageInfo*>(&s_arm64Packages[index]) : nullptr;
}

static void arm64_sync_instruction_cache(const uint8_t* start, uint64_t bytes)
{
    if (!start || bytes == 0) return;
    uint64_t ctr = 0;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    uint64_t line = UINT64_C(4) << (ctr & 0xf);
    if (line < 16) line = 16;
    const uint64_t begin = reinterpret_cast<uintptr_t>(start) & ~(line - 1);
    uint64_t end = 0;
    if (!arm64_add(reinterpret_cast<uintptr_t>(start), bytes + line - 1, &end)) return;
    end &= ~(line - 1);
    for (uint64_t address = begin; address < end; address += line)
        __asm__ volatile("dc cvau, %0" : : "r"(address) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");
    for (uint64_t address = begin; address < end; address += line)
        __asm__ volatile("ic ivau, %0" : : "r"(address) : "memory");
    __asm__ volatile("dsb sy\n isb" ::: "memory");
}

static void arm64_load_failure(const char* reason)
{
    arm64_print("[guideXOS] NativeElf load rejected: "); arm64_print(reason); arm64_print("\n");
}

static bool arm64_load_image(Arm64Runtime* runtime)
{
    char path[256];
    if (!arm64_package_path(runtime->package, runtime->package->executable, path, sizeof(path))) return false;
    Arm64Elf64Header header{};
    if (!arm64_read_at_for_runtime(runtime, path, 0, &header, sizeof(header))) { arm64_load_failure("header-read"); return false; }
    if (header.machine != kArm64ElfMachine) {
        if (header.machine == kArm64ElfMachineAmd64) arm64_print("[guideXOS] NativeElf wrong architecture: rejected\n");
        else arm64_load_failure("unsupported-machine");
        s_arm64LastWrongArchitecture = header.machine == kArm64ElfMachineAmd64;
        return false;
    }
#if defined(GXOS_AARCH64_PHASE10)
    if (runtime->verbose) {
        arm64_print("[guideXOS] selected ELF machine: EM_AARCH64\n");
    }
#endif
    uint64_t phBytes = 0;
    if (header.ident[0] != 0x7f || header.ident[1] != 'E' || header.ident[2] != 'L' || header.ident[3] != 'F' ||
        header.ident[4] != 2 || header.ident[5] != 1 || header.ident[6] != 1 || header.type != kArm64ElfTypeExec ||
        header.version != 1 || header.ehsize != sizeof(Arm64Elf64Header) ||
        header.phentsize != sizeof(Arm64Elf64ProgramHeader) || header.phnum == 0 ||
        header.phnum > kArm64MaxProgramHeaders || !arm64_mul(header.phnum, header.phentsize, &phBytes) ||
        header.phoff > runtime->package->executableBytes || phBytes > runtime->package->executableBytes - header.phoff) {
        arm64_load_failure("ELF64-structural-validation"); return false;
    }
    Arm64Elf64ProgramHeader headers[kArm64MaxProgramHeaders]{};
    uint64_t minimum = UINT64_MAX; uint64_t maximum = 0; uint32_t loadCount = 0;
    bool entryExecutable = false;
    for (uint16_t i = 0; i < header.phnum; ++i) {
        uint64_t offset = 0;
        if (!arm64_mul(i, header.phentsize, &offset) || !arm64_add(header.phoff, offset, &offset) ||
            !arm64_read_at_for_runtime(runtime, path, offset, &headers[i], sizeof(headers[i]))) {
            arm64_load_failure("program-header-bounds"); return false;
        }
        const Arm64Elf64ProgramHeader& program = headers[i];
        if (program.type != kArm64ElfLoad || program.memsz == 0) continue;
        uint64_t fileEnd = 0; uint64_t segmentEnd = 0;
        if (program.filesz > program.memsz || !arm64_add(program.offset, program.filesz, &fileEnd) ||
            fileEnd > runtime->package->executableBytes || !arm64_add(program.vaddr, program.memsz, &segmentEnd) ||
            (program.align > 1 && ((program.align & (program.align - 1)) != 0 ||
             (program.vaddr & (program.align - 1)) != (program.offset & (program.align - 1)))) ||
            (program.flags & 3u) == 3u) {
            arm64_load_failure("segment-bounds-or-permissions"); return false;
        }
        for (uint16_t prior = 0; prior < i; ++prior) {
            if (headers[prior].type != kArm64ElfLoad || headers[prior].memsz == 0) continue;
            uint64_t priorEnd = 0;
            if (!arm64_add(headers[prior].vaddr, headers[prior].memsz, &priorEnd)) return false;
            if (program.vaddr < priorEnd && headers[prior].vaddr < segmentEnd) {
                arm64_load_failure("overlapping-load-ranges"); return false;
            }
        }
        const uint64_t start = arm64_align_down(program.vaddr);
        uint64_t end = 0;
        if (!arm64_align_up(segmentEnd, &end)) { arm64_load_failure("segment-alignment-overflow"); return false; }
        if (start < minimum) minimum = start;
        if (end > maximum) maximum = end;
        if ((program.flags & 1u) && header.entry >= program.vaddr && header.entry < segmentEnd) entryExecutable = true;
        ++loadCount;
    }
    const uint64_t preferredBase =
#if defined(GXOS_AARCH64_PHASE9)
        runtime->preferredBase != 0 ? runtime->preferredBase : kArm64PreferredBase;
#else
        kArm64PreferredBase;
#endif
    if (loadCount == 0 || minimum != preferredBase || maximum <= minimum ||
        maximum - minimum > kArm64MaxImageBytes || header.entry < minimum || header.entry >= maximum ||
        !entryExecutable) {
        arm64_load_failure(minimum != preferredBase ? "fixed-address-contract" : "load-layout"); return false;
    }
    const uint64_t imageBytes = maximum - minimum;
    const uint64_t pages = imageBytes / kArm64PageSize;
    if (!kernel::memory::allocate_pages_at(minimum, pages)) { arm64_load_failure("preferred-address-unavailable"); return false; }
    runtime->image = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(minimum));
    runtime->imageBase = minimum; runtime->imageBytes = imageBytes; runtime->imagePages = pages;
    for (uint64_t i = 0; i < imageBytes; ++i) runtime->image[i] = 0;
    for (uint16_t i = 0; i < header.phnum; ++i) {
        const Arm64Elf64ProgramHeader& program = headers[i];
        if (program.type != kArm64ElfLoad || program.memsz == 0) continue;
        uint8_t* destination = runtime->image + (program.vaddr - minimum);
        uint64_t copied = 0;
        while (copied < program.filesz) {
            const uint64_t remaining = program.filesz - copied;
            const uint32_t chunk = remaining > 65536 ? 65536 : static_cast<uint32_t>(remaining);
            if (!arm64_read_at_for_runtime(runtime, path, program.offset + copied, destination + copied, chunk)) {
                kernel::memory::release_pages(runtime->imageBase, runtime->imagePages); runtime->image = nullptr;
                arm64_load_failure("segment-copy"); return false;
            }
            copied += chunk;
        }
        uint64_t mappedEnd = 0;
        const uint64_t mappedStart = arm64_align_down(program.vaddr);
        if (!arm64_align_up(program.vaddr + program.memsz, &mappedEnd) ||
            !phase2_mmu_set_range_permissions(mappedStart, mappedEnd - mappedStart,
                                              (program.flags & 1u) ? 1 : 0,
                                              (program.flags & 2u) ? 1 : 0)) {
            kernel::memory::release_pages(runtime->imageBase, runtime->imagePages); runtime->image = nullptr;
            arm64_load_failure("segment-permissions"); return false;
        }
        if (program.flags & 1u) arm64_sync_instruction_cache(
            runtime->image + (mappedStart - minimum), mappedEnd - mappedStart);
    }
    runtime->entry = reinterpret_cast<gx_result (GX_CALL *)(gx_app_context*)>(static_cast<uintptr_t>(header.entry));
    if (runtime->verbose) {
        arm64_print("[guideXOS] NativeElf architecture: ARM64\n");
        arm64_print("[guideXOS] NativeElf ELF validation: PASS type=ET_EXEC machine=EM_AARCH64 relocations=none\n");
        arm64_print("[guideXOS] NativeElf segments: loaded permissions=RX/RW/RO\n");
        arm64_print("[guideXOS] NativeElf icache sync: PASS\n");
    }
    return true;
}

static gx_result GX_CALL arm64_host_log(gx_app_context* context, const char* message)
{
    Arm64Runtime* runtime = context ? static_cast<Arm64Runtime*>(context->userData) : nullptr;
    if (runtime) runtime->entered = true;
    if (!runtime || runtime->verbose || arm64_equal(message, "gx_main entered")) {
        arm64_print("[phase5-app] "); arm64_print(message ? message : ""); arm64_print("\n");
    }
    return GX_OK;
}

static uint32_t GX_CALL arm64_host_api_version(gx_app_context*) { return GX_API_VERSION; }
static gx_result GX_CALL arm64_host_exit(gx_app_context*, gx_result code) { return code; }

static void arm64_initialize_host(Arm64Runtime* runtime)
{
    uint8_t* bytes = reinterpret_cast<uint8_t*>(&runtime->host);
    for (uint32_t i = 0; i < sizeof(runtime->host); ++i) bytes[i] = 0;
    runtime->host.size = sizeof(runtime->host);
    runtime->host.version = GX_API_VERSION;
    runtime->host.guiVersion = GX_GUI_ABI_VERSION;
    runtime->host.log = arm64_host_log;
    runtime->host.get_api_version = arm64_host_api_version;
    runtime->host.exit = arm64_host_exit;
}

static bool arm64_run_package(Arm64Package* package, bool verbose)
{
    Arm64Runtime runtime{};
    runtime.package = package;
    runtime.verbose = verbose;
    const uint64_t pagesBefore = kernel::memory::allocated_pages();
    arm64_initialize_host(&runtime);
    runtime.context.size = sizeof(runtime.context);
    runtime.context.apiVersion = GX_API_VERSION;
    runtime.context.host = &runtime.host;
    runtime.context.userData = &runtime;
    if (!arm64_load_image(&runtime)) return false;
    if (!kernel::memory::allocate_pages(kArm64ApplicationStackPages, &runtime.stackBase)) {
        kernel::memory::release_pages(runtime.imageBase, runtime.imagePages); runtime.image = nullptr;
        arm64_load_failure("application-stack-allocation"); return false;
    }
    runtime.stackPages = kArm64ApplicationStackPages;
    runtime.stackBytes = runtime.stackPages * kArm64PageSize;
    runtime.stack = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(runtime.stackBase));
    *reinterpret_cast<uint64_t*>(runtime.stack) = kArm64LowCanary;
    *reinterpret_cast<uint64_t*>(runtime.stack + runtime.stackBytes - sizeof(uint64_t)) = kArm64HighCanary;
    if (!phase2_mmu_set_range_permissions(runtime.stackBase, runtime.stackBytes, 0, 1)) {
        kernel::memory::release_pages(runtime.stackBase, runtime.stackPages);
        kernel::memory::release_pages(runtime.imageBase, runtime.imagePages); runtime.image = nullptr;
        arm64_load_failure("application-stack-permissions"); return false;
    }
    if (runtime.verbose) {
        arm64_print("[guideXOS] NativeElf application stack: OK bytes="); phase3_serial_dec(runtime.stackBytes);
        arm64_print(" aligned=1 non-executable=1\n");
    }
    const gx_result result = gxos_native_call_on_stack(
        runtime.entry, &runtime.context, runtime.stack + runtime.stackBytes - 16);
    if (runtime.verbose) {
        arm64_print("[guideXOS] NativeElf ARM64 return: "); phase3_serial_dec(static_cast<uint64_t>(result)); arm64_print("\n");
    }
    const bool canaries = *reinterpret_cast<uint64_t*>(runtime.stack) == kArm64LowCanary &&
        *reinterpret_cast<uint64_t*>(runtime.stack + runtime.stackBytes - sizeof(uint64_t)) == kArm64HighCanary;
    const bool returned = result == 42 && runtime.entered && canaries;
    const bool imagePermissionsRestored = phase2_mmu_set_range_permissions(
        runtime.imageBase, runtime.imageBytes, 0, 1) != 0;
    const bool stackReleased = kernel::memory::release_pages(runtime.stackBase, runtime.stackPages);
    const bool imageReleased = kernel::memory::release_pages(runtime.imageBase, runtime.imagePages);
    runtime.stack = nullptr; runtime.image = nullptr;
    const bool noLeak = kernel::memory::allocated_pages() == pagesBefore;
    if (runtime.verbose) {
        arm64_print("[guideXOS] NativeElf cleanup: ");
        arm64_print(imagePermissionsRestored && stackReleased && imageReleased && noLeak ? "PASS\n" : "FAIL\n");
    }
    return returned && imagePermissionsRestored && stackReleased && imageReleased && noLeak;
}

#if defined(GXOS_AARCH64_PHASE8)
static Arm64Runtime* arm64_gui_runtime(gx_app_context* context)
{
    if (!context) return nullptr;
    Arm64Runtime* runtime = static_cast<Arm64Runtime*>(context->userData);
    return runtime && context == &runtime->context && context->host == &runtime->host
        ? runtime : nullptr;
}

static bool arm64_gui_window_valid(Arm64Runtime* runtime, gx_handle handle)
{
    return runtime && runtime->guiOwner && runtime->guiOwner->window() &&
           runtime->guiWindow != 0 && handle == runtime->guiWindow;
}

static int arm64_gui_widget_id(Arm64Runtime* runtime, gx_handle handle)
{
    if (!runtime || !runtime->guiOwner || !runtime->guiOwner->window() || handle == 0) return -1;
    for (int i = 0; i < runtime->guiOwner->window()->widgetCount; ++i) {
        // A widget handle is only meaningful inside this runtime generation;
        // the owner table is the final type/lifetime check.
        gx_handle expected = arm64_gui_handle(runtime, kArm64GuiWidgetHandleKind,
                                                static_cast<uint32_t>(i + 1));
        if (expected == handle) return i;
    }
    return -1;
}

static gx_result GX_CALL arm64_gui_host_log(gx_app_context* context, const char* message)
{
    Arm64Runtime* runtime = arm64_gui_runtime(context);
    if (runtime) runtime->entered = true;
    if (!runtime || !arm64_gui_string(message)) return GX_ERROR_INVALID_ARGUMENT;
    arm64_print("[phase8-app] ");
    arm64_print(message);
    arm64_print("\n");
    return GX_OK;
}

static uint32_t GX_CALL arm64_gui_host_api_version(gx_app_context* context)
{
    return arm64_gui_runtime(context) ? GX_API_VERSION : 0xffffffffu;
}

static gx_result GX_CALL arm64_gui_host_exit(gx_app_context* context, gx_result code)
{
    Arm64Runtime* runtime = arm64_gui_runtime(context);
    return runtime ? code : GX_ERROR_INVALID_ARGUMENT;
}

static gx_result GX_CALL arm64_gui_host_request_window_ex(gx_app_context* context, const char* title,
                                                           int32_t width, int32_t height, uint32_t flags,
                                                           gx_handle* outWindow)
{
    Arm64Runtime* runtime = arm64_gui_runtime(context);
    if (!runtime || !outWindow || runtime->guiOwner || !arm64_gui_string(title) || width <= 0 || height <= 0 ||
        width > static_cast<int32_t>(kArm64GuiMaxDimension) || height > static_cast<int32_t>(kArm64GuiMaxDimension)) {
        return GX_ERROR_INVALID_ARGUMENT;
    }
    runtime->guiOwner = new Arm64GuiOwner(runtime);
    if (!runtime->guiOwner || !runtime->guiOwner->create(title, width, height, flags)) {
        delete runtime->guiOwner;
        runtime->guiOwner = nullptr;
        return GX_ERROR_FAILED;
    }
    *outWindow = runtime->guiWindow;
    return GX_OK;
}

static gx_result GX_CALL arm64_gui_host_request_window(gx_app_context* context, const char* title,
                                                        int32_t width, int32_t height, gx_handle* outWindow)
{
    return arm64_gui_host_request_window_ex(context, title, width, height,
                                             GX_WINDOW_FLAG_FIXED_SIZE, outWindow);
}

static gx_result GX_CALL arm64_gui_host_window_destroy(gx_app_context* context, gx_handle window)
{
    Arm64Runtime* runtime = arm64_gui_runtime(context);
    if (!arm64_gui_window_valid(runtime, window)) return GX_ERROR_INVALID_ARGUMENT;
    return runtime->guiOwner->destroy_window(window) ? GX_OK : GX_ERROR_FAILED;
}

static gx_result GX_CALL arm64_gui_host_widget_create(gx_app_context* context, gx_handle window,
                                                       uint32_t type, int32_t x, int32_t y,
                                                       int32_t width, int32_t height, const char* text,
                                                       gx_handle* outWidget)
{
    Arm64Runtime* runtime = arm64_gui_runtime(context);
    if (!arm64_gui_window_valid(runtime, window) || !outWidget || !arm64_gui_string(text) ||
        type == GX_WIDGET_NONE || type > GX_WIDGET_TEXTBOX) return GX_ERROR_INVALID_ARGUMENT;
    const gx_handle handle = runtime->guiOwner->create_widget(type, x, y, width, height, text);
    if (handle == 0) return GX_ERROR_UNSUPPORTED;
    *outWidget = handle;
    return GX_OK;
}

static gx_result GX_CALL arm64_gui_host_widget_set_text(gx_app_context* context, gx_handle widget,
                                                         const char* text)
{
    Arm64Runtime* runtime = arm64_gui_runtime(context);
    if (!runtime || arm64_gui_widget_id(runtime, widget) < 0 || !arm64_gui_string(text))
        return GX_ERROR_INVALID_ARGUMENT;
    return runtime->guiOwner->set_widget_text(widget, text) ? GX_OK : GX_ERROR_FAILED;
}

static gx_result GX_CALL arm64_gui_host_widget_set_value(gx_app_context* context, gx_handle widget,
                                                          int32_t value)
{
    Arm64Runtime* runtime = arm64_gui_runtime(context);
    if (!runtime || arm64_gui_widget_id(runtime, widget) < 0)
        return GX_ERROR_INVALID_ARGUMENT;
    return runtime->guiOwner->set_widget_value(widget, value) ? GX_OK : GX_ERROR_FAILED;
}

static void arm64_gui_detect_drag(Arm64Runtime* runtime)
{
    if (!runtime || !runtime->guiOwner || !runtime->guiOwner->window() || runtime->guiDragPassed ||
        compositor::KernelCompositor::isButtonPressActive()) return;
    app::KernelWindow* window = runtime->guiOwner->window();
    if (window->x == runtime->guiInitialX && window->y == runtime->guiInitialY) return;
    runtime->guiDragPassed = 1;
    runtime->guiDragHash = framebuffer::verification_hash(
        static_cast<uint32_t>(window->x), static_cast<uint32_t>(window->y),
        static_cast<uint32_t>(window->w), static_cast<uint32_t>(window->h));
    arm64_print("[guideXOS] application window drag: PASS initial=");
    phase3_serial_dec(static_cast<uint64_t>(runtime->guiInitialX));
    arm64_print(",");
    phase3_serial_dec(static_cast<uint64_t>(runtime->guiInitialY));
    arm64_print(" final=");
    phase3_serial_dec(static_cast<uint64_t>(window->x));
    arm64_print(",");
    phase3_serial_dec(static_cast<uint64_t>(window->y));
    arm64_print(" delta=");
    phase3_serial_dec(static_cast<uint64_t>(window->x - runtime->guiInitialX));
    arm64_print(",");
    phase3_serial_dec(static_cast<uint64_t>(window->y - runtime->guiInitialY));
    arm64_print("\n");
}

static gx_result GX_CALL arm64_gui_host_poll_event(gx_app_context* context, gx_event* outEvent,
                                                    int32_t timeoutMs)
{
    Arm64Runtime* runtime = arm64_gui_runtime(context);
    if (!runtime || !outEvent || timeoutMs < 0) return GX_ERROR_INVALID_ARGUMENT;
    if (!runtime->guiPollEntered) {
        runtime->guiPollEntered = true;
        arm64_print("[guideXOS] phase8 app event loop: polling\n");
    }
    outEvent->size = sizeof(gx_event);
    outEvent->type = GX_EVENT_NONE;
    outEvent->window = runtime->guiWindow;
    outEvent->param1 = outEvent->param2 = outEvent->param3 = outEvent->param4 = 0;
    const uint64_t start = phase3_timer_count();
    const uint64_t deadline = start + static_cast<uint64_t>((timeoutMs + 9) / 10);
    for (;;) {
        if (runtime->guiOwner && runtime->guiOwner->pop_event(outEvent)) return GX_OK;
        if (!runtime->guiOwner) return GX_ERROR_FAILED;
        if (!runtime->guiPumpTraceDone) {
            arm64_print("[guideXOS] phase8 input bridge: calling desktop pump\n");
        }
        // NativeElf runs on a private application stack, so it stays in the
        // scheduler/UI context while it pumps the common input queue.  The
        // Phase-8 graphics worker deliberately does not call this stateful
        // pump concurrently; timer preemption still keeps that worker and
        // the rest of the scheduler alive.
        desktop::cooperative_yield();
        if (!runtime->guiPumpTraceDone) {
            runtime->guiPumpTraceDone = true;
            arm64_print("[guideXOS] phase8 input bridge: desktop pump returned devices=");
            phase3_serial_dec(virtio_input::active_device_count());
            arm64_print(" events=");
            phase3_serial_dec(virtio_input::hardware_events_received());
            arm64_print("\n");
        }
        if (!runtime->guiPumpObservedInput &&
            virtio_input::hardware_events_received() != 0) {
            runtime->guiPumpObservedInput = true;
            arm64_print("[guideXOS] phase8 input bridge: hardware delivered events=");
            phase3_serial_dec(virtio_input::hardware_events_received());
            arm64_print(" pointer=");
            phase3_serial_dec(virtio_input::hardware_pointer_events());
            arm64_print(" buttons=");
            phase3_serial_dec(virtio_input::hardware_button_events());
            arm64_print(" keyboard=");
            phase3_serial_dec(virtio_input::hardware_keyboard_events());
            arm64_print("\n");
        }
        arm64_gui_detect_drag(runtime);
        if (runtime->guiAutoClose && !runtime->guiAutoCloseIssued && runtime->guiOwner->window() &&
            phase3_timer_count() - start >= 3) {
            runtime->guiAutoCloseIssued = true;
            compositor::KernelCompositor::requestCloseWindow(runtime->guiOwner->window()->id);
        }
        if (runtime->guiOwner->pop_event(outEvent)) return GX_OK;
        if (runtime->guiOwner->closed()) {
            outEvent->type = GX_EVENT_WINDOW_CLOSE;
            outEvent->window = runtime->guiWindow;
            return GX_OK;
        }
        if (timeoutMs == 0 || phase3_timer_count() >= deadline) return GX_ERROR_TIMEOUT;
        // NativeElf is executing on its private stack, so use the normal
        // interrupt-backed idle instruction rather than switching a common
        // scheduler context from inside the trampoline.  Timer preemption
        // wakes this wait and keeps the other kernel tasks runnable.
        kernel::arch::idle();
    }
}

static gx_result GX_CALL arm64_gui_host_wait_for_close(gx_app_context* context, gx_handle window,
                                                        int32_t timeoutMs)
{
    Arm64Runtime* runtime = arm64_gui_runtime(context);
    if (!arm64_gui_window_valid(runtime, window)) return GX_ERROR_INVALID_ARGUMENT;
    for (;;) {
        gx_event event{};
        const gx_result result = arm64_gui_host_poll_event(context, &event, timeoutMs);
        if (result != GX_OK) return result;
        if (event.type == GX_EVENT_WINDOW_CLOSE) return GX_OK;
    }
}

static void arm64_gui_initialize_host(Arm64Runtime* runtime)
{
    uint8_t* bytes = reinterpret_cast<uint8_t*>(&runtime->host);
    for (uint32_t i = 0; i < sizeof(runtime->host); ++i) bytes[i] = 0;
    runtime->host.size = sizeof(runtime->host);
    runtime->host.version = GX_API_VERSION;
    runtime->host.guiVersion = GX_GUI_ABI_VERSION;
    runtime->host.log = arm64_gui_host_log;
    runtime->host.get_api_version = arm64_gui_host_api_version;
    runtime->host.request_window = arm64_gui_host_request_window;
    runtime->host.request_window_ex = arm64_gui_host_request_window_ex;
    runtime->host.wait_for_close = arm64_gui_host_wait_for_close;
    runtime->host.poll_event = arm64_gui_host_poll_event;
    runtime->host.exit = arm64_gui_host_exit;
    runtime->host.window_destroy = arm64_gui_host_window_destroy;
    runtime->host.widget_create = arm64_gui_host_widget_create;
    runtime->host.widget_set_text = arm64_gui_host_widget_set_text;
    runtime->host.widget_set_value = arm64_gui_host_widget_set_value;
}

static bool arm64_gui_run_package(Arm64Package* package, bool verbose)
{
    Arm64Runtime runtime{};
    runtime.package = package;
    runtime.verbose = verbose;
    runtime.generation = ++s_arm64GuiGeneration;
    runtime.guiAutoClose = s_arm64GuiLaunches >= 2;
    const uint64_t pagesBefore = kernel::memory::allocated_pages();
    arm64_gui_initialize_host(&runtime);
    runtime.context.size = sizeof(runtime.context);
    runtime.context.apiVersion = GX_API_VERSION;
    runtime.context.host = &runtime.host;
    runtime.context.userData = &runtime;
    if (!arm64_load_image(&runtime)) return false;
    if (!kernel::memory::allocate_pages(kArm64ApplicationStackPages, &runtime.stackBase)) {
        kernel::memory::release_pages(runtime.imageBase, runtime.imagePages);
        runtime.image = nullptr;
        arm64_load_failure("application-stack-allocation");
        return false;
    }
    runtime.stackPages = kArm64ApplicationStackPages;
    runtime.stackBytes = runtime.stackPages * kArm64PageSize;
    runtime.stack = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(runtime.stackBase));
    *reinterpret_cast<uint64_t*>(runtime.stack) = kArm64LowCanary;
    *reinterpret_cast<uint64_t*>(runtime.stack + runtime.stackBytes - sizeof(uint64_t)) = kArm64HighCanary;
    if (!phase2_mmu_set_range_permissions(runtime.stackBase, runtime.stackBytes, 0, 1)) {
        kernel::memory::release_pages(runtime.stackBase, runtime.stackPages);
        kernel::memory::release_pages(runtime.imageBase, runtime.imagePages);
        runtime.image = nullptr;
        return false;
    }
    const gx_result result = gxos_native_call_on_stack(
        runtime.entry, &runtime.context, runtime.stack + runtime.stackBytes - 16);
    const bool canaries = *reinterpret_cast<uint64_t*>(runtime.stack) == kArm64LowCanary &&
        *reinterpret_cast<uint64_t*>(runtime.stack + runtime.stackBytes - sizeof(uint64_t)) == kArm64HighCanary;
    const bool guiProof = result == 42 && runtime.entered && runtime.guiCreated &&
        runtime.guiWidgetCount == 5 && runtime.guiClosePassed && runtime.guiInitialHash != 0 &&
        (runtime.guiAutoClose || (runtime.guiFocusPassed && runtime.guiTextPassed &&
                                  runtime.guiButtonPassed && runtime.guiDragPassed));

    if (result == 42 && runtime.entered) arm64_print("[guideXOS] NativeElf GUI return: 42\n");
    if (!runtime.guiAutoClose && runtime.guiFocusPassed)
        arm64_print("[guideXOS] application focus routing: PASS\n");
    if (!runtime.guiAutoClose && runtime.guiTextPassed && runtime.guiButtonPassed)
        arm64_print("[guideXOS] application input routing: PASS\n");
    if (!runtime.guiAutoClose && runtime.guiInitialHash != 0 && runtime.guiTextHash != 0 &&
        runtime.guiButtonHash != 0 && runtime.guiDragHash != 0) {
        arm64_print("[guideXOS] framebuffer GUI regions: PASS initial=0x");
        phase3_serial_hex(runtime.guiInitialHash);
        arm64_print(" text=0x");
        phase3_serial_hex(runtime.guiTextHash);
        arm64_print(" button=0x");
        phase3_serial_hex(runtime.guiButtonHash);
        arm64_print(" drag=0x");
        phase3_serial_hex(runtime.guiDragHash);
        arm64_print("\n");
    }

    if (runtime.guiOwner) {
        if (runtime.guiOwner->window()) runtime.guiOwner->requestClose();
        delete runtime.guiOwner;
        runtime.guiOwner = nullptr;
    }
    const bool compositorClean = compositor::KernelCompositor::getWindowCount() == 0;
    const bool imagePermissionsRestored = phase2_mmu_set_range_permissions(
        runtime.imageBase, runtime.imageBytes, 0, 1) != 0;
    const bool stackReleased = kernel::memory::release_pages(runtime.stackBase, runtime.stackPages);
    const bool imageReleased = kernel::memory::release_pages(runtime.imageBase, runtime.imagePages);
    runtime.stack = nullptr;
    runtime.image = nullptr;
    const bool noLeak = kernel::memory::allocated_pages() == pagesBefore;
    const bool cleanup = compositorClean && imagePermissionsRestored && stackReleased && imageReleased && noLeak;
    if (cleanup) {
        arm64_print("[guideXOS] NativeElf GUI cleanup: PASS windows=0 controls=0 mappings=0 allocator-delta-pages=0\n");
    }
    return guiProof && canaries && cleanup;
}
#endif

#if defined(GXOS_AARCH64_PHASE9)

/* -------------------------------------------------------------------------
 * Phase-9 concurrent NativeElf adapter
 * -------------------------------------------------------------------------
 * This is deliberately a thin ARM64 boundary.  Identity, lifecycle, quota,
 * handle, queue, and wait/wake policy live in the common ApplicationRuntime;
 * this section supplies only ELF mapping, AAPCS64 entry, and KernelWindow
 * translation. */

static const application::ApplicationQuotas kPhase9Quotas = {
    3, 16, application::ApplicationEventService::kCapacity, 4096, 32, 2048
};
static const uint32_t kPhase9MaxWindows = 3;
static const uint32_t kPhase9MaxWidgets = 32;

struct Phase9Runtime;

class Phase9GuiOwner final : public app::KernelApp {
public:
    explicit Phase9GuiOwner(Phase9Runtime* runtime);
    ~Phase9GuiOwner() override;

    bool init() override;
    void shutdown() override;
    void draw(uint32_t, uint32_t, uint32_t, uint32_t) override {}
    void requestClose() override;

    bool create_window(const char* title, int32_t width, int32_t height, uint32_t flags,
                       gx_handle* outWindow);
    gx_handle create_widget(gx_handle window, uint32_t type, int32_t x, int32_t y,
                            int32_t width, int32_t height, const char* text);
    bool set_widget_text(gx_handle widget, const char* text);
    bool set_widget_value(gx_handle widget, int32_t value);
    bool destroy_window(gx_handle window);
    bool pop_event(gx_event* event);
    void request_auto_close();
    bool validate_window(gx_handle handle) const;
    bool validate_widget(gx_handle handle) const;
    gx_handle first_window_handle() const { return m_windowHandles[0]; }
    gx_handle first_widget_handle() const { return m_widgetHandles[0]; }
    uint32_t window_count() const { return m_windowCount; }
    uint32_t widget_count() const { return m_widgetCount; }
    bool close_seen() const { return m_closeSeen; }
    bool has_windows() const { return m_windowCount != 0; }

    void onMouseMove(int x, int y) override;
    void onMouseDown(int x, int y, uint8_t button) override;
    void onMouseUp(int x, int y, uint8_t button) override;
    void onKeyDown(uint32_t key) override;
    void onWidgetClick(int widgetId) override;
    void onKeyChar(char c) override;
    void onWindowClose() override;
    void onWindowFocus() override;
    void onWindowBlur() override;

private:
    struct WidgetSlot {
        uint16_t generation;
        uint16_t windowIndex;
        uint16_t localId;
        uint16_t stringBytes;
        bool live;
    };

    static bool copy_string(char* destination, uint32_t capacity, const char* value);
    int window_index(gx_handle handle) const;
    int widget_slot(gx_handle handle) const;
    int focused_window_index() const;
    gx_handle widget_handle(int windowIndex, int localId) const;
    void queue(gx_event_type type, gx_handle object, int32_t p1, int32_t p2,
               int32_t p3, bool lifecycle = false);
    void close_window_index(int index);
    void update_current_window(app::KernelWindow* window);

    Phase9Runtime* m_runtime;
    app::KernelWindow* m_windows[kPhase9MaxWindows];
    gx_handle m_windowHandles[kPhase9MaxWindows];
    uint16_t m_windowGenerations[kPhase9MaxWindows];
    WidgetSlot m_widgets[kPhase9MaxWidgets];
    gx_handle m_widgetHandles[kPhase9MaxWidgets];
    uint32_t m_windowCount;
    uint32_t m_widgetCount;
    int m_focusedWidget;
    int m_closingWindow;
    bool m_closeSeen;
};

struct Phase9Runtime {
    Arm64Runtime loader;
    application::ApplicationRuntime common;
    Phase9GuiOwner* owner;
    bool autoClose;
    bool closeRequested;
    bool isAppA;
    bool isPhase10;
    bool entered;
    uint64_t startTick;
    uint32_t buttonClicks;
    gx_handle firstWindowHandle;
    gx_handle firstWidgetHandle;
    uint32_t peakWindowCount;
    uint32_t peakWidgetCount;
    bool staleWindowRejected;
    bool staleWidgetRejected;
};

static Phase9Runtime* s_phase9AppA = nullptr;
static Phase9Runtime* s_phase9AppB = nullptr;
static uint64_t s_phase9Generation = 0;
static uint32_t s_phase9RuntimeSequence = 0;
static uint32_t s_phase9AppALaunches = 0;
static bool s_phase9AppAInitialDone = false;
static bool s_phase9AppADurabilityDone = false;
static uint64_t s_phase9AppADurabilityTick = 0;
/* The QMP harness exercises App B before App A teardown; this short fallback
 * keeps headless boots deterministic after the durability proof completes. */
static const uint64_t kPhase9AppBGraceTicks = 200;
static bool s_phase9AppBSurvived = false;
static bool s_phase9AllComplete = false;
static gx_handle s_phase9OldAWindow = 0;
static gx_handle s_phase9OldAWidget = 0;
static uint64_t s_phase9AppAWaits = 0;
static uint64_t s_phase9AppAWakes = 0;
static uint64_t s_phase9AppBWaits = 0;
static uint64_t s_phase9AppBWakes = 0;
static bool s_phase9WaitWakeLogged = false;
static bool s_phase9AppAButtonLogged = false;
static bool s_phase9AppBButtonLogged = false;
static volatile uint8_t s_phase9LoaderBusy = 0;
static volatile uint32_t s_phase9VfsUsers = 0;
static bool s_phase9AppAImagePrimed = false;
static bool s_phase9AppBImagePrimed = false;
#if defined(GXOS_AARCH64_PHASE10)
static uint32_t s_phase10Launches = 0;
static bool s_phase10WaitWakeLogged = false;
static bool s_phase10WaitLogged = false;
static bool s_phase10AllComplete = false;
#endif

bool phase9_vfs_exclusive() { return s_phase9VfsUsers != 0; }

void phase9_vfs_enter()
{
    while (phase9_filesystem_vfs_active()) scheduler::note_execution();
    const arch::interrupt_state_t state = arch::irq_save();
    ++s_phase9VfsUsers;
    arch::irq_restore(state);
}

void phase9_vfs_exit()
{
    const arch::interrupt_state_t state = arch::irq_save();
    if (s_phase9VfsUsers != 0) --s_phase9VfsUsers;
    arch::irq_restore(state);
}

static void phase9_acquire_loader()
{
    for (;;) {
        const arch::interrupt_state_t state = arch::irq_save();
        if (!s_phase9LoaderBusy) {
            s_phase9LoaderBusy = 1;
            arch::irq_restore(state);
            return;
        }
        arch::irq_restore(state);
        arch::idle();
    }
}

static void phase9_release_loader()
{
    const arch::interrupt_state_t state = arch::irq_save();
    s_phase9LoaderBusy = 0;
    arch::irq_restore(state);
}

static bool phase9_string_valid(const char* value)
{
    return arm64_gui_string(value);
}

bool Phase9GuiOwner::copy_string(char* destination, uint32_t capacity, const char* value)
{
    if (!destination || capacity == 0 || !phase9_string_valid(value)) return false;
    uint32_t i = 0;
    while (value[i] && i + 1u < capacity) { destination[i] = value[i]; ++i; }
    destination[i] = '\0';
    return value[i] == '\0';
}

Phase9GuiOwner::Phase9GuiOwner(Phase9Runtime* runtime)
    : m_runtime(runtime), m_windows{}, m_windowHandles{}, m_windowGenerations{},
      m_widgets{}, m_widgetHandles{}, m_windowCount(0), m_widgetCount(0),
      m_focusedWidget(-1), m_closingWindow(-1), m_closeSeen(false) {
    for (uint32_t i = 0; i < kPhase9MaxWindows; ++i) m_windowGenerations[i] = 1;
    for (uint32_t i = 0; i < kPhase9MaxWidgets; ++i) m_widgets[i].generation = 1;
}

Phase9GuiOwner::~Phase9GuiOwner()
{
    for (int i = static_cast<int>(kPhase9MaxWindows) - 1; i >= 0; --i) {
        if (!m_windows[i]) continue;
        for (uint32_t widget = 0; widget < kPhase9MaxWidgets; ++widget) {
            if (!m_widgets[widget].live || m_widgets[widget].windowIndex != static_cast<uint16_t>(i)) continue;
            if (m_runtime) {
                m_runtime->common.release_widgets();
                m_runtime->common.release_strings(m_widgets[widget].stringBytes);
            }
            m_widgets[widget].live = false;
            ++m_widgets[widget].generation;
            if (m_widgetCount != 0) --m_widgetCount;
        }
        compositor::KernelCompositor::unregisterWindow(m_windows[i]);
        delete m_windows[i];
        m_windows[i] = nullptr;
        if (m_runtime) m_runtime->common.release_windows();
    }
    m_windowCount = 0;
    m_window = nullptr;
}

bool Phase9GuiOwner::init()
{
    m_state = app::AppState::Running;
    return true;
}

void Phase9GuiOwner::shutdown()
{
    m_state = app::AppState::Terminated;
}

void Phase9GuiOwner::update_current_window(app::KernelWindow* window)
{
    m_window = window;
}

int Phase9GuiOwner::window_index(gx_handle handle) const
{
    for (uint32_t i = 0; i < kPhase9MaxWindows; ++i) {
        if (m_windows[i] && m_windowHandles[i] == handle &&
            application::handle_matches(handle, m_runtime->common.runtime_id(),
                                        m_runtime->common.generation(), application::HandleKind::Window,
                                        m_windowGenerations[i], static_cast<uint16_t>(i + 1))) return static_cast<int>(i);
    }
    return -1;
}

int Phase9GuiOwner::widget_slot(gx_handle handle) const
{
    for (uint32_t i = 0; i < kPhase9MaxWidgets; ++i) {
        if (m_widgets[i].live && m_widgetHandles[i] == handle &&
            application::handle_matches(handle, m_runtime->common.runtime_id(),
                                        m_runtime->common.generation(), application::HandleKind::Widget,
                                        m_widgets[i].generation, static_cast<uint16_t>(i + 1))) return static_cast<int>(i);
    }
    return -1;
}

int Phase9GuiOwner::focused_window_index() const
{
    app::KernelWindow* focused = compositor::KernelCompositor::getFocusedWindow();
    if (!focused) return -1;
    for (uint32_t i = 0; i < kPhase9MaxWindows; ++i) if (m_windows[i] == focused) return static_cast<int>(i);
    return -1;
}

gx_handle Phase9GuiOwner::widget_handle(int windowIndex, int localId) const
{
    for (uint32_t i = 0; i < kPhase9MaxWidgets; ++i) {
        if (m_widgets[i].live && m_widgets[i].windowIndex == static_cast<uint16_t>(windowIndex) &&
            m_widgets[i].localId == static_cast<uint16_t>(localId)) return m_widgetHandles[i];
    }
    return 0;
}

void Phase9GuiOwner::queue(gx_event_type type, gx_handle object, int32_t p1, int32_t p2,
                           int32_t p3, bool lifecycle)
{
    if (!m_runtime || !m_runtime->common.events().valid()) return;
    gx_event value{};
    value.size = sizeof(gx_event);
    value.type = type;
    value.window = object;
    value.param1 = p1;
    value.param2 = p2;
    value.param3 = p3;
    value.param4 = 0;
    m_runtime->common.events().enqueue(value, lifecycle);
    m_runtime->common.note_event_high_water(m_runtime->common.events().high_water());
}

bool Phase9GuiOwner::create_window(const char* title, int32_t width, int32_t height,
                                   uint32_t flags, gx_handle* outWindow)
{
    if (!m_runtime || !outWindow || !m_runtime->common.can_create_objects() ||
        !phase9_string_valid(title) || width <= 0 || height <= 0 || width > 640 || height > 640 ||
        m_windowCount >= kPhase9MaxWindows || !m_runtime->common.reserve_windows()) return false;
    app::KernelWindow* window = new app::KernelWindow();
    if (!window || !copy_string(window->title, sizeof(window->title), title)) {
        delete window;
        m_runtime->common.release_windows();
        return false;
    }
    const uint32_t screenW = framebuffer::get_width();
    const uint32_t screenH = framebuffer::get_height();
    if (static_cast<uint32_t>(width) >= screenW || static_cast<uint32_t>(height) + compositor::TITLEBAR_HEIGHT >= screenH) {
        delete window;
        m_runtime->common.release_windows();
        return false;
    }
    const bool appA = m_runtime->isAppA;
    const uint32_t ordinal = m_windowCount;
    if (appA && ordinal == 0) { window->x = 55; window->y = 95; }
    else if (appA) { window->x = 410; window->y = 135; }
    else { window->x = 90; window->y = 350; }
    if (window->x + width > static_cast<int>(screenW)) window->x = static_cast<int>(screenW) - width - 4;
    if (window->y + height + compositor::TITLEBAR_HEIGHT > static_cast<int>(screenH)) window->y = 40;
    window->w = width;
    window->h = height;
    window->flags = app::WF_VISIBLE | app::WF_TITLEBAR | app::WF_CLOSABLE;
    if (flags & GX_WINDOW_FLAG_RESIZABLE) window->flags |= app::WF_RESIZABLE;
    window->owner = this;
    uint32_t slot = kPhase9MaxWindows;
    for (uint32_t i = 0; i < kPhase9MaxWindows; ++i) if (!m_windows[i]) { slot = i; break; }
    if (slot == kPhase9MaxWindows || !compositor::KernelCompositor::registerWindow(window)) {
        delete window;
        m_runtime->common.release_windows();
        return false;
    }
    m_windows[slot] = window;
    m_windowHandles[slot] = application::make_handle(
        m_runtime->common.runtime_id(), m_runtime->common.generation(), application::HandleKind::Window,
        m_windowGenerations[slot], static_cast<uint16_t>(slot + 1));
    ++m_windowCount;
    update_current_window(window);
    if (!init()) return false;
    *outWindow = m_windowHandles[slot];
    if (s_phase9OldAWindow != 0 && m_runtime->isAppA && s_phase9AppALaunches > 0)
        m_runtime->staleWindowRejected = !validate_window(s_phase9OldAWindow);
    return true;
}

gx_handle Phase9GuiOwner::create_widget(gx_handle windowHandle, uint32_t type, int32_t x, int32_t y,
                                        int32_t width, int32_t height, const char* text)
{
    const int wi = window_index(windowHandle);
    if (wi < 0 || !m_runtime || !m_runtime->common.can_create_objects() || !phase9_string_valid(text) ||
        x < 0 || y < 0 || width <= 0 || height <= 0 || width > 640 || height > 640) return 0;
    app::KernelWindow* target = m_windows[wi];
    const int32_t clientHeight = target->h - compositor::TITLEBAR_HEIGHT;
    if (x > target->w || y > clientHeight || width > target->w - x || height > clientHeight - y) return 0;
    uint32_t length = arm64_length(text);
    if (length >= 64) return 0;
    if (!m_runtime->common.reserve_widgets()) return 0;
    if (!m_runtime->common.reserve_strings(length + 1u)) {
        m_runtime->common.release_widgets();
        return 0;
    }
    uint32_t slot = kPhase9MaxWidgets;
    for (uint32_t i = 0; i < kPhase9MaxWidgets; ++i) if (!m_widgets[i].live) { slot = i; break; }
    if (slot == kPhase9MaxWidgets) {
        m_runtime->common.release_widgets(); m_runtime->common.release_strings(length + 1u); return 0;
    }
    app::KernelWindow* saved = m_window;
    m_window = target;
    int local = -1;
    if (type == GX_WIDGET_LABEL) local = addLabel(x, y, width, height, text);
    else if (type == GX_WIDGET_BUTTON) local = addButton(x, y, width, height, text);
    else if (type == GX_WIDGET_TEXTBOX) local = addTextBox(x, y, width, height, text);
    m_window = saved;
    if (local < 0) {
        m_runtime->common.release_widgets(); m_runtime->common.release_strings(length + 1u); return 0;
    }
    m_widgets[slot] = { m_widgets[slot].generation, static_cast<uint16_t>(wi), static_cast<uint16_t>(local),
                        static_cast<uint16_t>(length + 1u), true };
    m_widgetHandles[slot] = application::make_handle(
        m_runtime->common.runtime_id(), m_runtime->common.generation(), application::HandleKind::Widget,
        m_widgets[slot].generation, static_cast<uint16_t>(slot + 1));
    ++m_widgetCount;
    if (s_phase9OldAWidget != 0 && m_runtime->isAppA && s_phase9AppALaunches > 0)
        m_runtime->staleWidgetRejected = !validate_widget(s_phase9OldAWidget);
    return m_widgetHandles[slot];
}

bool Phase9GuiOwner::set_widget_text(gx_handle handle, const char* text)
{
    const int slot = widget_slot(handle);
    if (slot < 0 || !phase9_string_valid(text)) return false;
    const uint32_t length = arm64_length(text);
    if (length >= 64) return false;
    app::KernelWindow* target = m_windows[m_widgets[slot].windowIndex];
    app::KernelWindow* saved = m_window;
    m_window = target;
    setWidgetText(m_widgets[slot].localId, text);
    m_window = saved;
    const uint32_t newBytes = length + 1u;
    if (newBytes > m_widgets[slot].stringBytes) {
        if (!m_runtime->common.reserve_strings(newBytes - m_widgets[slot].stringBytes)) return false;
    } else {
        m_runtime->common.release_strings(m_widgets[slot].stringBytes - newBytes);
    }
    m_widgets[slot].stringBytes = static_cast<uint16_t>(newBytes);
    return true;
}

bool Phase9GuiOwner::set_widget_value(gx_handle handle, int32_t value)
{
    const int slot = widget_slot(handle);
    if (slot < 0) return false;
    app::KernelWindow* saved = m_window;
    m_window = m_windows[m_widgets[slot].windowIndex];
    setWidgetValue(m_widgets[slot].localId, value);
    m_window = saved;
    return true;
}

void Phase9GuiOwner::close_window_index(int index)
{
    if (index < 0 || index >= static_cast<int>(kPhase9MaxWindows) || !m_windows[index]) return;
    app::KernelWindow* target = m_windows[index];
    app::KernelWindow* saved = m_window;
    m_window = target;
    m_closingWindow = index;
    KernelApp::requestClose();
    m_closingWindow = -1;
    m_windows[index] = nullptr;
    m_windowHandles[index] = 0;
    ++m_windowGenerations[index];
    m_runtime->common.release_windows();
    for (uint32_t i = 0; i < kPhase9MaxWidgets; ++i) {
        if (m_widgets[i].live && m_widgets[i].windowIndex == static_cast<uint16_t>(index)) {
            m_runtime->common.release_widgets();
            m_runtime->common.release_strings(m_widgets[i].stringBytes);
            m_widgets[i].live = false;
            ++m_widgets[i].generation;
            --m_widgetCount;
        }
    }
    m_window = saved && saved != target ? saved : nullptr;
    if (!m_window) for (uint32_t i = 0; i < kPhase9MaxWindows; ++i) if (m_windows[i]) { m_window = m_windows[i]; break; }
    if (m_windowCount != 0) --m_windowCount;
    if (m_windowCount != 0 && m_runtime->common.state() == application::LifecycleState::Closing)
        m_runtime->common.transition(application::LifecycleState::Running);
}

void Phase9GuiOwner::requestClose()
{
    const int focused = focused_window_index();
    close_window_index(focused >= 0 ? focused : (m_windowCount ? 0 : -1));
}

bool Phase9GuiOwner::destroy_window(gx_handle handle)
{
    const int index = window_index(handle);
    if (index < 0) return false;
    close_window_index(index);
    return true;
}

void Phase9GuiOwner::request_auto_close()
{
    if (m_windowCount != 0) close_window_index(0);
}

bool Phase9GuiOwner::validate_window(gx_handle handle) const { return window_index(handle) >= 0; }
bool Phase9GuiOwner::validate_widget(gx_handle handle) const { return widget_slot(handle) >= 0; }
bool Phase9GuiOwner::pop_event(gx_event* event) { return m_runtime && m_runtime->common.events().try_pop(event); }

void Phase9GuiOwner::onMouseMove(int x, int y) { const int i = focused_window_index(); if (i >= 0) queue(GX_EVENT_MOUSE, m_windowHandles[i], x, y, GX_MOUSE_PACK(0, GX_MOUSE_ACTION_MOVE)); }
void Phase9GuiOwner::onMouseDown(int x, int y, uint8_t button) { const int i = focused_window_index(); if (i >= 0) queue(GX_EVENT_MOUSE, m_windowHandles[i], x, y, GX_MOUSE_PACK(button, GX_MOUSE_ACTION_DOWN)); }
void Phase9GuiOwner::onMouseUp(int x, int y, uint8_t button) { const int i = focused_window_index(); if (i >= 0) queue(GX_EVENT_MOUSE, m_windowHandles[i], x, y, GX_MOUSE_PACK(button, GX_MOUSE_ACTION_UP)); }
void Phase9GuiOwner::onKeyDown(uint32_t key) { const int i = focused_window_index(); if (i >= 0) queue(GX_EVENT_KEY, m_windowHandles[i], static_cast<int32_t>(key), GX_KEY_ACTION_DOWN, 0); }

void Phase9GuiOwner::onWidgetClick(int widgetId)
{
    const int wi = focused_window_index();
    if (wi < 0 || !m_windows[wi] || widgetId < 0 || widgetId >= m_windows[wi]->widgetCount) return;
    app::Widget* widget = &m_windows[wi]->widgets[widgetId];
    if (widget->type == app::WidgetType::TextBox) m_focusedWidget = widgetId;
    if (widget->type == app::WidgetType::Button && m_runtime) ++m_runtime->buttonClicks;
    queue(GX_EVENT_WIDGET, widget_handle(wi, widgetId), widgetId, 0, 0);
}

void Phase9GuiOwner::onKeyChar(char c)
{
    const int wi = focused_window_index();
    if (wi < 0 || m_focusedWidget < 0 || !m_windows[wi] || m_focusedWidget >= m_windows[wi]->widgetCount) return;
    app::Widget* widget = &m_windows[wi]->widgets[m_focusedWidget];
    if (widget->type != app::WidgetType::TextBox || !widget->enabled || !widget->visible) return;
    uint32_t length = arm64_length(widget->text);
    if (length + 1u >= sizeof(widget->text)) return;
    widget->text[length] = c; widget->text[length + 1u] = '\0';
    queue(GX_EVENT_TEXT_INPUT, widget_handle(wi, m_focusedWidget), static_cast<uint8_t>(c), 0, 0);
}

void Phase9GuiOwner::onWindowClose()
{
    const int index = m_closingWindow >= 0 ? m_closingWindow : focused_window_index();
    if (index >= 0 && index < static_cast<int>(kPhase9MaxWindows)) {
        m_closeSeen = true;
        if (m_windowCount == 1) m_runtime->common.begin_closing();
        queue(GX_EVENT_WINDOW_CLOSE, m_windowHandles[index], 0, 0, 0, true);
    }
}

void Phase9GuiOwner::onWindowFocus()
{
    const int index = focused_window_index();
    if (index >= 0) queue(GX_EVENT_WINDOW_FOCUS, m_windowHandles[index], 0, 0, 0);
}

void Phase9GuiOwner::onWindowBlur()
{
    const int index = focused_window_index();
    if (index >= 0) queue(GX_EVENT_WINDOW_BLUR, m_windowHandles[index], 0, 0, 0);
}

static Phase9Runtime* phase9_runtime(gx_app_context* context)
{
    if (!context) return nullptr;
    Phase9Runtime* runtime = static_cast<Phase9Runtime*>(context->userData);
    return runtime && context == &runtime->loader.context && context->host == &runtime->loader.host
        ? runtime : nullptr;
}

static gx_result GX_CALL phase9_log(gx_app_context* context, const char* message)
{
    Phase9Runtime* runtime = phase9_runtime(context);
    if (!runtime || !phase9_string_valid(message)) return GX_ERROR_INVALID_ARGUMENT;
    runtime->entered = true;
    const arch::interrupt_state_t state = arch::irq_save();
    arm64_print(runtime->isPhase10 ? "[phase10-app] " : (runtime->isAppA ? "[phase9-app-a] " : "[phase9-app-b] ")); arm64_print(message); arm64_print("\n");
    if (runtime->isAppA && arm64_equal(message, "focus/input isolation: PASS value=appa"))
        arm64_print("[guideXOS] cross-app focus isolation: PASS\n");
    if (!runtime->isAppA && arm64_equal(message, "focus/input isolation: PASS value=appb"))
        arm64_print("[guideXOS] cross-app input isolation: PASS\n");
    if (arm64_equal(message, "button: PASS count=1")) {
        if (runtime->isAppA) s_phase9AppAButtonLogged = true; else s_phase9AppBButtonLogged = true;
        if (s_phase9AppAButtonLogged && s_phase9AppBButtonLogged)
            arm64_print("[guideXOS] cross-app control routing: PASS\n");
    }
    arch::irq_restore(state);
    return GX_OK;
}

static uint32_t GX_CALL phase9_api_version(gx_app_context* context) { return phase9_runtime(context) ? GX_API_VERSION : UINT32_MAX; }
static gx_result GX_CALL phase9_exit(gx_app_context* context, gx_result code) { return phase9_runtime(context) ? code : GX_ERROR_INVALID_ARGUMENT; }

static gx_result GX_CALL phase9_request_window(gx_app_context* context, const char* title, int32_t width,
                                                int32_t height, uint32_t flags, gx_handle* outWindow)
{
    Phase9Runtime* runtime = phase9_runtime(context);
    if (!runtime || !runtime->owner || !outWindow) return GX_ERROR_INVALID_ARGUMENT;
    if (runtime->common.resources().windows >= runtime->common.quotas().windows) return GX_ERROR_QUOTA;
    const bool created = runtime->owner->create_window(title, width, height, flags, outWindow);
    return created ? GX_OK : GX_ERROR_FAILED;
}

static gx_result GX_CALL phase9_request_window_ex(gx_app_context* context, const char* title, int32_t width,
                                                   int32_t height, uint32_t flags, gx_handle* outWindow)
{
    return phase9_request_window(context, title, width, height, flags, outWindow);
}

static gx_result GX_CALL phase9_request_window_legacy(gx_app_context* context, const char* title,
                                                       int32_t width, int32_t height, gx_handle* outWindow)
{
    return phase9_request_window_ex(context, title, width, height, GX_WINDOW_FLAG_FIXED_SIZE, outWindow);
}

static gx_result GX_CALL phase9_window_destroy(gx_app_context* context, gx_handle window)
{
    Phase9Runtime* runtime = phase9_runtime(context);
    if (!runtime || !runtime->owner || !runtime->owner->validate_window(window)) return GX_ERROR_INVALID_ARGUMENT;
    return runtime->owner->destroy_window(window) ? GX_OK : GX_ERROR_FAILED;
}

static gx_result GX_CALL phase9_widget_create(gx_app_context* context, gx_handle window, uint32_t type,
                                               int32_t x, int32_t y, int32_t width, int32_t height,
                                               const char* text, gx_handle* outWidget)
{
    Phase9Runtime* runtime = phase9_runtime(context);
    if (!runtime || !runtime->owner || !outWidget) return GX_ERROR_INVALID_ARGUMENT;
    if (runtime->common.resources().widgets >= runtime->common.quotas().widgets) return GX_ERROR_QUOTA;
    gx_handle handle = runtime->owner->create_widget(window, type, x, y, width, height, text);
    if (!handle) return phase9_string_valid(text) && arm64_length(text) >= 64 ? GX_ERROR_QUOTA : GX_ERROR_INVALID_ARGUMENT;
    *outWidget = handle;
    return GX_OK;
}

static gx_result GX_CALL phase9_widget_set_text(gx_app_context* context, gx_handle widget, const char* text)
{
    Phase9Runtime* runtime = phase9_runtime(context);
    if (!runtime || !runtime->owner || !runtime->owner->validate_widget(widget)) return GX_ERROR_INVALID_ARGUMENT;
    if (!phase9_string_valid(text) || arm64_length(text) >= 64) return GX_ERROR_QUOTA;
    return runtime->owner->set_widget_text(widget, text) ? GX_OK : GX_ERROR_FAILED;
}

static gx_result GX_CALL phase9_widget_set_value(gx_app_context* context, gx_handle widget, int32_t value)
{
    Phase9Runtime* runtime = phase9_runtime(context);
    if (!runtime || !runtime->owner || !runtime->owner->validate_widget(widget)) return GX_ERROR_INVALID_ARGUMENT;
    return runtime->owner->set_widget_value(widget, value) ? GX_OK : GX_ERROR_FAILED;
}

static gx_result GX_CALL phase9_poll_event(gx_app_context* context, gx_event* event, int32_t timeoutMs)
{
    Phase9Runtime* runtime = phase9_runtime(context);
    if (!runtime || !runtime->owner || !event || timeoutMs < 0) return GX_ERROR_INVALID_ARGUMENT;
    if (runtime->owner->pop_event(event)) return GX_OK;
    event->size = sizeof(gx_event); event->type = GX_EVENT_NONE; event->window = 0;
    return GX_ERROR_TIMEOUT;
}

static gx_result GX_CALL phase9_wait_event(gx_app_context* context, gx_event* event)
{
    Phase9Runtime* runtime = phase9_runtime(context);
    if (!runtime || !runtime->owner || !event) return GX_ERROR_INVALID_ARGUMENT;
    /* Repeated durability launches use the same application-owned close
     * callback as the interactive path.  Requesting it at the wait boundary
     * keeps the bounded proof independent of monitor-task scheduling while
     * still delivering a normal queued lifecycle event through wait/wake. */
    if (runtime->autoClose && runtime->owner->has_windows())
        runtime->owner->request_auto_close();
    if (runtime->common.state() == application::LifecycleState::Running)
        runtime->common.transition(application::LifecycleState::Waiting);
#if defined(GXOS_AARCH64_PHASE10)
    if (runtime->isPhase10 && !s_phase10WaitLogged) {
        s_phase10WaitLogged = true;
        arm64_print("[guideXOS] multiarch app waiting: PASS\n");
    }
#endif
    const gx_result result = runtime->common.events().wait(event);
    if (runtime->common.state() == application::LifecycleState::Waiting)
        runtime->common.transition(application::LifecycleState::Running);
    if (runtime->isAppA) { s_phase9AppAWaits = runtime->common.events().waits(); s_phase9AppAWakes = runtime->common.events().wakes(); }
    else { s_phase9AppBWaits = runtime->common.events().waits(); s_phase9AppBWakes = runtime->common.events().wakes(); }
#if defined(GXOS_AARCH64_PHASE10)
    if (result == GX_OK && runtime->common.events().wakes() != 0 && runtime->isPhase10 && !s_phase10WaitWakeLogged) {
        s_phase10WaitWakeLogged = true;
        arm64_print("[guideXOS] multiarch app wait/wake: PASS\n");
    }
#endif
    if (result == GX_OK && runtime->common.events().wakes() != 0 && !runtime->isPhase10 && !s_phase9WaitWakeLogged) {
        s_phase9WaitWakeLogged = true;
        arm64_print("[guideXOS] application wait/wake: PASS\n");
    }
    return result;
}

static void phase9_initialize_host(Phase9Runtime* runtime)
{
    uint8_t* bytes = reinterpret_cast<uint8_t*>(&runtime->loader.host);
    for (uint32_t i = 0; i < sizeof(runtime->loader.host); ++i) bytes[i] = 0;
    runtime->loader.host.size = sizeof(runtime->loader.host);
    runtime->loader.host.version = GX_API_VERSION;
    runtime->loader.host.guiVersion = GX_GUI_ABI_VERSION;
    runtime->loader.host.log = phase9_log;
    runtime->loader.host.get_api_version = phase9_api_version;
    runtime->loader.host.request_window = phase9_request_window_legacy;
    runtime->loader.host.request_window_ex = phase9_request_window_ex;
    runtime->loader.host.window_destroy = phase9_window_destroy;
    runtime->loader.host.widget_create = phase9_widget_create;
    runtime->loader.host.widget_set_text = phase9_widget_set_text;
    runtime->loader.host.widget_set_value = phase9_widget_set_value;
    runtime->loader.host.poll_event = phase9_poll_event;
    runtime->loader.host.wait_event = phase9_wait_event;
    runtime->loader.host.exit = phase9_exit;
}

static bool phase9_load_and_run(Phase9Runtime* runtime)
{
    phase9_initialize_host(runtime);
    runtime->loader.context.size = sizeof(runtime->loader.context);
    runtime->loader.context.apiVersion = GX_API_VERSION;
    runtime->loader.context.host = &runtime->loader.host;
    runtime->loader.context.userData = runtime;
    const bool needsImageVfs = runtime->isAppA ? !s_phase9AppAImagePrimed : !s_phase9AppBImagePrimed;
    if (needsImageVfs) phase9_vfs_enter();
    phase9_acquire_loader();
    const bool imageLoaded = arm64_load_image(&runtime->loader);
    phase9_release_loader();
    if (needsImageVfs) phase9_vfs_exit();
    if (imageLoaded && needsImageVfs) {
        if (runtime->isAppA) s_phase9AppAImagePrimed = true;
        else s_phase9AppBImagePrimed = true;
    }
    if (!imageLoaded) return false;
    if (!runtime->common.reserve_pages(static_cast<uint32_t>(runtime->loader.imagePages))) {
        kernel::memory::release_pages(runtime->loader.imageBase, runtime->loader.imagePages);
        runtime->loader.image = nullptr;
        return false;
    }
    if (!kernel::memory::allocate_pages(kArm64ApplicationStackPages, &runtime->loader.stackBase) ||
        !runtime->common.reserve_stack_pages(kArm64ApplicationStackPages)) {
        if (runtime->loader.stackBase) kernel::memory::release_pages(runtime->loader.stackBase, kArm64ApplicationStackPages);
        if (runtime->common.resources().stackPages != 0)
            runtime->common.release_stack_pages(runtime->common.resources().stackPages);
        kernel::memory::release_pages(runtime->loader.imageBase, runtime->loader.imagePages);
        runtime->common.release_pages(static_cast<uint32_t>(runtime->loader.imagePages));
        if (runtime->owner) { delete runtime->owner; runtime->owner = nullptr; }
        runtime->common.cleanup();
        runtime->loader.image = nullptr;
        return false;
    }
    runtime->loader.stackPages = kArm64ApplicationStackPages;
    runtime->loader.stackBytes = runtime->loader.stackPages * kArm64PageSize;
    runtime->loader.stack = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(runtime->loader.stackBase));
    *reinterpret_cast<uint64_t*>(runtime->loader.stack) = kArm64LowCanary;
    *reinterpret_cast<uint64_t*>(runtime->loader.stack + runtime->loader.stackBytes - sizeof(uint64_t)) = kArm64HighCanary;
    if (!phase2_mmu_set_range_permissions(runtime->loader.stackBase, runtime->loader.stackBytes, 0, 1)) {
        kernel::memory::release_pages(runtime->loader.stackBase, runtime->loader.stackPages);
        kernel::memory::release_pages(runtime->loader.imageBase, runtime->loader.imagePages);
        runtime->common.release_stack_pages(runtime->loader.stackPages);
        runtime->common.release_pages(static_cast<uint32_t>(runtime->loader.imagePages));
        if (runtime->owner) { delete runtime->owner; runtime->owner = nullptr; }
        runtime->common.cleanup();
        runtime->loader.stack = nullptr; runtime->loader.image = nullptr;
        return false;
    }
    runtime->common.transition(application::LifecycleState::Running);
    const gx_result result = gxos_native_call_on_stack(
        runtime->loader.entry, &runtime->loader.context,
        runtime->loader.stack + runtime->loader.stackBytes - 16);
    /* Give the other runtime and the monitor one interrupt boundary before
     * tearing down this runtime's compositor objects and mappings. */
    kernel::arch::idle();
    const bool canaries = *reinterpret_cast<uint64_t*>(runtime->loader.stack) == kArm64LowCanary &&
        *reinterpret_cast<uint64_t*>(runtime->loader.stack + runtime->loader.stackBytes - sizeof(uint64_t)) == kArm64HighCanary;
    if (runtime->owner) {
        runtime->firstWindowHandle = runtime->owner->first_window_handle();
        runtime->firstWidgetHandle = runtime->owner->first_widget_handle();
        runtime->peakWindowCount = runtime->owner->window_count();
        runtime->peakWidgetCount = runtime->owner->widget_count();
    }
    if (runtime->owner) {
        const arch::interrupt_state_t cleanupState = arch::irq_save();
        runtime->owner->shutdown();
        delete runtime->owner;
        runtime->owner = nullptr;
        arch::irq_restore(cleanupState);
    }
    const bool imagePermissionsRestored = phase2_mmu_set_range_permissions(
        runtime->loader.imageBase, runtime->loader.imageBytes, 0, 1) != 0;
    const bool stackReleased = kernel::memory::release_pages(runtime->loader.stackBase, runtime->loader.stackPages);
    const bool imageReleased = kernel::memory::release_pages(runtime->loader.imageBase, runtime->loader.imagePages);
    runtime->common.release_pages(static_cast<uint32_t>(runtime->loader.imagePages));
    runtime->common.release_stack_pages(runtime->loader.stackPages);
    runtime->loader.stack = nullptr; runtime->loader.image = nullptr;
    const bool noLeak = runtime->common.resources().mappedPages == 0 &&
        runtime->common.resources().stackPages == 0 &&
        runtime->common.resources().windows == 0 &&
        runtime->common.resources().widgets == 0;
    runtime->common.mark_exited();
    runtime->common.cleanup();
    const bool cleanup = result == 42 && runtime->entered && canaries && imagePermissionsRestored && stackReleased && imageReleased && noLeak;
    if (cleanup) arm64_print("[guideXOS] NativeElf GUI cleanup: PASS mappings=0 windows=0 widgets=0\n");
    return cleanup;
}

bool phase9_run_application(const char* appName, bool autoClose)
{
    if (!s_arm64Discovered) {
        phase9_vfs_enter();
        phase9_acquire_loader();
        if (!s_arm64Discovered) discover();
        phase9_release_loader();
        phase9_vfs_exit();
    }
    Arm64Package* package = arm64_find_package(appName);
    if (!package || s_phase9AppA == nullptr && s_phase9AppB == nullptr && s_arm64LaunchInProgress) return false;
    Phase9Runtime runtime{};
    runtime.loader.package = package;
    runtime.loader.verbose = false;
    runtime.loader.preferredBase = arm64_equal(package->id, "com.guidexos.phase9.appb") ? UINT64_C(0x52000000) : kArm64PreferredBase;
    const uint8_t runtimeId = static_cast<uint8_t>((++s_phase9RuntimeSequence % 255u) + 1u);
    const uint16_t generation = static_cast<uint16_t>((++s_phase9Generation & 0xffffu) ? (s_phase9Generation & 0xffffu) : 1u);
    if (!runtime.common.initialize(package->id, "AARCH64", runtimeId, generation,
                              kPhase9Quotas)) return false;
    runtime.owner = new Phase9GuiOwner(&runtime);
    if (!runtime.owner) {
        runtime.common.cleanup();
        return false;
    }
    runtime.autoClose = autoClose;
    runtime.closeRequested = false;
    runtime.isAppA = arm64_equal(package->id, "com.guidexos.phase9.appa");
    runtime.startTick = phase3_timer_count();
    runtime.loader.context.userData = &runtime;
    if (runtime.isAppA && s_phase9AppALaunches == 0) {
        arm64_print("[guideXOS] application runtime: initialized\n");
        arm64_print("[guideXOS] application event service: initialized\n");
    }
    if (runtime.isAppA) s_phase9AppA = &runtime; else s_phase9AppB = &runtime;
    if (s_phase9AppA && s_phase9AppB && s_phase9AppALaunches == 0)
        arm64_print("[guideXOS] concurrent NativeElf runtimes: PASS\n");
    const bool result = phase9_load_and_run(&runtime);
    if (runtime.isAppA) {
        ++s_phase9AppALaunches;
        if (!s_phase9AppAInitialDone) {
            s_phase9AppAInitialDone = result;
            s_phase9OldAWindow = runtime.firstWindowHandle;
            s_phase9OldAWidget = runtime.firstWidgetHandle;
            s_phase9AppBSurvived = s_phase9AppB != nullptr;
            if (result && s_phase9AppBSurvived) arm64_print("[guideXOS] cross-app cleanup isolation: PASS\n");
        }
        if (s_phase9AppALaunches == 2 && result) arm64_print("[guideXOS] application generation relaunch: PASS\n");
        if (s_phase9AppALaunches >= 51) {
            s_phase9AppADurabilityDone = result;
            if (result) {
                s_phase9AppADurabilityTick = phase3_timer_count();
                arm64_print("[guideXOS] runtime lifecycle durability: PASS launches=51\n");
            }
        }
        s_phase9AppA = nullptr;
    } else {
        s_phase9AppB = nullptr;
    }
    return result;
}

void phase9_service()
{
    const uint64_t now = phase3_timer_count();
    Phase9GuiOwner* appBOwner = nullptr;
    const bool closeMayBeNeeded =
       (s_phase9AppB && s_phase9AppADurabilityDone && s_phase9AppADurabilityTick != 0 &&
         now - s_phase9AppADurabilityTick >= kPhase9AppBGraceTicks && !s_phase9AppB->closeRequested &&
         s_phase9AppB->owner);
    if (closeMayBeNeeded) {
        const arch::interrupt_state_t state = arch::irq_save();
        Phase9Runtime* appB = s_phase9AppB;
        if (appB && s_phase9AppADurabilityDone && s_phase9AppADurabilityTick != 0 &&
            now - s_phase9AppADurabilityTick >= kPhase9AppBGraceTicks && !appB->closeRequested && appB->owner) {
            appB->closeRequested = true;
            appBOwner = appB->owner;
        }
        if (appBOwner) appBOwner->request_auto_close();
        arch::irq_restore(state);
    }
    s_phase9AllComplete = s_phase9AppADurabilityDone && s_phase9AppB == nullptr;
}

bool phase9_all_complete() { return s_phase9AllComplete; }
bool phase9_app_a_image_primed() { return s_phase9AppAImagePrimed; }
bool phase9_app_b_survived_a_close() { return s_phase9AppBSurvived; }
uint32_t phase9_app_a_launches() { return s_phase9AppALaunches; }
uint64_t phase9_app_a_waits() { return s_phase9AppAWaits; }
uint64_t phase9_app_a_wakes() { return s_phase9AppAWakes; }
uint64_t phase9_app_b_waits() { return s_phase9AppBWaits; }
uint64_t phase9_app_b_wakes() { return s_phase9AppBWakes; }

#if defined(GXOS_AARCH64_PHASE10)
bool phase10_run_application(const char* appName, bool autoClose)
{
    if (s_arm64LaunchInProgress) return false;
    if (!s_arm64Discovered) {
        phase9_vfs_enter();
        phase9_acquire_loader();
        if (!s_arm64Discovered) discover();
        phase9_release_loader();
        phase9_vfs_exit();
    }
    Arm64Package* package = arm64_find_package(appName);
    if (!package || !arm64_equal(package->id, "com.guidexos.phase10.multiarchproof")) return false;

    s_arm64LaunchInProgress = true;
    arm64_print("[guideXOS] App Model package: "); arm64_print(package->id); arm64_print("\n");
    arm64_print("[guideXOS] native architecture: "); arm64_print(package->architecture); arm64_print("\n");
    arm64_print("[guideXOS] selected payload: "); arm64_print(package->executable); arm64_print("\n");

    Phase9Runtime runtime{};
    runtime.loader.package = package;
    runtime.loader.verbose = true;
    runtime.loader.preferredBase = kArm64PreferredBase;
    const uint8_t runtimeId = static_cast<uint8_t>((++s_phase9RuntimeSequence % 255u) + 1u);
    const uint16_t generation = static_cast<uint16_t>((++s_phase9Generation & 0xffffu) ?
        (s_phase9Generation & 0xffffu) : 1u);
    if (!runtime.common.initialize(package->id, package->architecture, runtimeId, generation,
                                   kPhase9Quotas)) {
        s_arm64LaunchInProgress = false;
        return false;
    }
    runtime.owner = new Phase9GuiOwner(&runtime);
    if (!runtime.owner) {
        runtime.common.cleanup();
        s_arm64LaunchInProgress = false;
        return false;
    }
    runtime.autoClose = autoClose;
    runtime.closeRequested = false;
    runtime.isAppA = true;
    runtime.isPhase10 = true;
    runtime.startTick = phase3_timer_count();
    runtime.loader.context.userData = &runtime;
    const bool result = phase9_load_and_run(&runtime);
    s_arm64LaunchInProgress = false;
    if (!result) return false;

    ++s_phase10Launches;
    arm64_print("[guideXOS] multiarch app cleanup: PASS\n");
    if (s_phase10Launches == 2) arm64_print("[guideXOS] multiarch package relaunch: PASS\n");
    return true;
}

bool phase10_all_complete() { return s_phase10AllComplete || s_phase10Launches >= 26; }
uint32_t phase10_launches() { return s_phase10Launches; }
#else
bool phase10_run_application(const char*, bool) { return false; }
bool phase10_all_complete() { return false; }
uint32_t phase10_launches() { return 0; }
#endif

#endif

bool launch(const char* name)
{
    s_arm64LastWrongArchitecture = false;
    if (s_arm64LaunchInProgress) return false;
    if (!s_arm64Discovered) discover();
    Arm64Package* package = arm64_find_package(name);
    if (!package) return false;
    s_arm64LaunchInProgress = true;
    bool result = false;
#if defined(GXOS_AARCH64_PHASE8)
    result = arm64_gui_run_package(package, s_arm64GuiLaunches == 0);
    if (result && arm64_equal(package->id, "com.guidexos.phase8.arm64guiproof")) ++s_arm64GuiLaunches;
#else
    result = arm64_run_package(package, s_arm64SuccessfulLaunches == 0);
    if (result && arm64_equal(package->id, "com.guidexos.phase5.arm64proof")) ++s_arm64SuccessfulLaunches;
#endif
    s_arm64LaunchInProgress = false;
    return result;
}

bool last_launch_rejected_wrong_architecture() { return s_arm64LastWrongArchitecture; }
