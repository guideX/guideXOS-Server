#include "include/kernel/native_elf_baremetal.h"
#include "include/kernel/native_elf_fault.h"

#include "include/kernel/arch.h"
#include "include/kernel/desktop.h"
#include "include/kernel/framebuffer.h"
#include "include/kernel/input_manager.h"
#include "include/kernel/kernel_app.h"
#include "include/kernel/kernel_compositor.h"
#include "include/kernel/file_clipboard.h"
#include "include/kernel/pit.h"
#include "include/kernel/ps2keyboard.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/vfs.h"

#include "bitmap_font.h"
#include "sdk/include/guidexos/abi.h"
#include "sdk/include/guidexos/app.h"

namespace kernel {
namespace native_elf {

#if defined(ARCH_AMD64)

static const uint32_t kPageSize = 0x1000u;
static const uint32_t kMaxPackages = 8u;
static const uint32_t kMaxManifestBytes = 8192u;
static const uint32_t kReadChunkBytes = 65536u;
static const uint32_t kRuntimeStackBytes = 128u * 1024u;
// Developer Studio's signed amd64 ELF has a large, valid PT_LOAD .bss region.
// Keep the loader bound below the amd64 heap capacity while retaining the
// original conservative bound on other architectures.
#if defined(__x86_64__)
static const uint32_t kMaxLoadedImageBytes = 512u * 1024u * 1024u;
#else
static const uint32_t kMaxLoadedImageBytes = 32u * 1024u * 1024u;
#endif
static const uint32_t kMaxFrameBytes = 16u * 1024u * 1024u;

using Package = PackageInfo;

struct Runtime;

enum class NativeAbiOperation : uint32_t {
    None = 0,
    GetTicks,
    ReadResource,
    CreateWindow,
    PresentFrame,
    PollEvent,
    CloseWindow,
    RequestExit
};

class NativeWindowOwner final : public app::KernelApp {
public:
    explicit NativeWindowOwner(Runtime* runtime)
        : m_runtime(runtime), m_allocation(nullptr), m_pixels(nullptr), m_pixelBytes(0), m_width(0), m_height(0),
          m_strideBytes(0), m_surface(nullptr), m_surfaceBytes(0), m_pendingEventValue(), m_pendingEvent(false), m_closed(false) {
        m_name[0] = '\0';
    }

    ~NativeWindowOwner() override {
        release_pixels();
    }

    bool init() override { return true; }

    void shutdown() override {
        m_closed = true;
        release_pixels();
    }

    void onMouseMove(int x, int y) override {
        queue_mouse_event(x, y, GX_MOUSE_BUTTON_NONE, GX_MOUSE_ACTION_MOVE, 0);
    }

    void onMouseDown(int x, int y, uint8_t button) override {
        queue_mouse_event(x, y, mouse_button_for_abi(button), GX_MOUSE_ACTION_DOWN, 0);
    }

    void onMouseUp(int x, int y, uint8_t button) override {
        queue_mouse_event(x, y, mouse_button_for_abi(button), GX_MOUSE_ACTION_UP, 0);
    }

    void onMouseWheel(int x, int y, int wheelDelta) override {
        queue_mouse_event(x, y, GX_MOUSE_BUTTON_NONE, GX_MOUSE_ACTION_WHEEL, wheelDelta);
    }

    void draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) override {
        framebuffer::fill_rect(x, y, w, h, 0xFF000000u);
        const uint32_t* source = m_surface ? m_surface : m_pixels;
        if (!source || m_width == 0 || m_height == 0) return;

        const uint32_t drawX = w > m_width ? x + (w - m_width) / 2u : x;
        const uint32_t drawY = h > m_height ? y + (h - m_height) / 2u : y;
        const uint32_t visibleW = w < m_width ? w : m_width;
        const uint32_t visibleH = h < m_height ? h : m_height;
        uint32_t* output = framebuffer::get_back_buffer();
        const uint32_t pitchWords = framebuffer::get_pitch() / 4u;
        if (!output || pitchWords == 0) return;

        const uint32_t sourceStrideWords = m_surface ? m_width : m_strideBytes / 4u;
        for (uint32_t row = 0; row < visibleH; ++row) {
            for (uint32_t col = 0; col < visibleW; ++col) {
                const uint32_t src = source[row * sourceStrideWords + col];
                output[(drawY + row) * pitchWords + drawX + col] = 0xFF000000u | (src & 0x00FFFFFFu);
            }
        }
    }

    bool create(const char* title, int width, int height, uint32_t flags) {
        if (!title || width <= 0 || height <= 0 || m_window) return false;
        m_window = new app::KernelWindow();
        if (!m_window) return false;
        m_window->owner = this;
        m_window->w = width;
        m_window->h = height;
        m_window->flags = app::WF_VISIBLE | app::WF_TITLEBAR | app::WF_CLOSABLE;
        if (flags & GX_WINDOW_FLAG_RESIZABLE) m_window->flags |= app::WF_RESIZABLE;
        const uint32_t screenW = framebuffer::get_width();
        const uint32_t screenH = framebuffer::get_height();
        if (static_cast<uint32_t>(width) > screenW ||
            static_cast<uint32_t>(height) + compositor::TITLEBAR_HEIGHT > screenH) {
            delete m_window;
            m_window = nullptr;
            return false;
        }
        m_window->x = screenW > static_cast<uint32_t>(width) ?
            static_cast<int>((screenW - static_cast<uint32_t>(width)) / 2u) : 0;
        m_window->y = screenH > static_cast<uint32_t>(height) ?
            static_cast<int>((screenH - static_cast<uint32_t>(height)) / 2u) : 0;
        m_width = static_cast<uint32_t>(width);
        m_height = static_cast<uint32_t>(height);
        setTitle(title);
        if (!compositor::KernelCompositor::registerWindow(m_window)) {
            delete m_window;
            m_window = nullptr;
            return false;
        }
        m_state = app::AppState::Running;
        m_closed = false;
        desktop_request_redraw();
        return true;
    }

    gx_result draw_text(int x, int y, const char* text) {
        if (!text || !ensure_surface()) return GX_ERROR_INVALID_ARGUMENT;
        if (text[0] == '\f' && text[1] == '\0') {
            clear_surface(0xFF000000u);
        } else {
            gxos::gui::BitmapFont::DrawStringToBuffer(
                m_surface, static_cast<int>(m_width * sizeof(uint32_t)),
                static_cast<int>(m_width), static_cast<int>(m_height),
                x, y, text, -1, 0xFFFFFFFFu);
        }
        invalidate();
        return GX_OK;
    }

    gx_result draw_rect(int x, int y, int width, int height, uint32_t color) {
        if (width <= 0 || height <= 0 || !ensure_surface()) return GX_ERROR_INVALID_ARGUMENT;
        const int left = x < 0 ? 0 : x;
        const int top = y < 0 ? 0 : y;
        const int right = x > static_cast<int>(m_width) ? static_cast<int>(m_width) :
            (x > 0x7FFFFFFF - width ? static_cast<int>(m_width) : x + width);
        const int bottom = y > static_cast<int>(m_height) ? static_cast<int>(m_height) :
            (y > 0x7FFFFFFF - height ? static_cast<int>(m_height) : y + height);
        if (left < right && top < bottom) {
            const uint32_t pixel = 0xFF000000u | (color & 0x00FFFFFFu);
            for (int row = top; row < bottom; ++row) {
                for (int col = left; col < right; ++col) {
                    m_surface[static_cast<uint32_t>(row) * m_width + static_cast<uint32_t>(col)] = pixel;
                }
            }
        }
        invalidate();
        return GX_OK;
    }

    bool present(const void* pixels, uint32_t width, uint32_t height, uint32_t strideBytes) {
        if (!pixels || width == 0 || height == 0 || strideBytes < width * 4u ||
            (strideBytes & 3u) != 0u || height > 0xFFFFFFFFu / strideBytes) return false;
        release_pixels();
        const uint64_t bytes64 = static_cast<uint64_t>(strideBytes) * height;
        if (bytes64 > kMaxFrameBytes || (bytes64 & 3u) != 0u) return false;
        const uint32_t bytes = static_cast<uint32_t>(bytes64);
        const uint32_t words = bytes / 4u;
        if (words > 0xFFFFFFFFu - 2u) return false;
        m_allocation = new uint32_t[words + 2u];
        if (!m_allocation) return false;
        m_allocation[0] = 0xC0DEC0DEu;
        m_allocation[words + 1u] = 0xFACEB00Cu;
        m_pixels = m_allocation + 1u;
        const uint8_t* source = static_cast<const uint8_t*>(pixels);
        uint8_t* destination = reinterpret_cast<uint8_t*>(m_pixels);
        for (uint32_t i = 0; i < bytes; ++i) destination[i] = source[i];
        m_pixelBytes = bytes;
        m_width = width;
        m_height = height;
        m_strideBytes = strideBytes;
        invalidate();
        if (!guards_intact()) return false;
        return true;
    }

    void release_pixels() {
        if (m_allocation && !guards_intact()) {
            serial::puts("[NATIVE-ELF] retained-frame guard CORRUPT\n");
            record_frame_guard_failure();
        }
        if (m_allocation) delete[] m_allocation;
        m_allocation = nullptr;
        m_pixels = nullptr;
        m_pixelBytes = 0;
        m_width = 0;
        m_height = 0;
        m_strideBytes = 0;
        if (m_surface) delete[] m_surface;
        m_surface = nullptr;
        m_surfaceBytes = 0;
    }

    void clear_surface(uint32_t color) {
        if (!m_surface) return;
        const uint32_t pixels = m_width * m_height;
        for (uint32_t i = 0; i < pixels; ++i) m_surface[i] = color;
    }

    bool ensure_surface() {
        if (m_surface) return true;
        if (m_width == 0 || m_height == 0 || m_width > 0xFFFFFFFFu / m_height) return false;
        const uint64_t bytes64 = static_cast<uint64_t>(m_width) * m_height * sizeof(uint32_t);
        if (bytes64 == 0 || bytes64 > kMaxFrameBytes) return false;
        m_surface = new uint32_t[static_cast<uint32_t>(bytes64 / sizeof(uint32_t))];
        if (!m_surface) return false;
        m_surfaceBytes = static_cast<uint32_t>(bytes64);
        clear_surface(0xFF000000u);
        return true;
    }

    bool closed() const { return m_closed || m_window == nullptr; }
    app::KernelWindow* window() const { return m_window; }

    bool take_pending_event(gx_event* event) {
        if (!event || !m_pendingEvent) return false;
        *event = m_pendingEventValue;
        m_pendingEvent = false;
        return true;
    }

private:
    static int mouse_button_for_abi(uint8_t button) {
        if (button & 0x01u) return GX_MOUSE_BUTTON_LEFT;
        if (button & 0x02u) return GX_MOUSE_BUTTON_RIGHT;
        if (button & 0x04u) return GX_MOUSE_BUTTON_MIDDLE;
        return GX_MOUSE_BUTTON_NONE;
    }

    void queue_mouse_event(int x, int y, int button, int action, int wheelDelta) {
        if (m_closed || !m_window) return;
        m_pendingEventValue.size = sizeof(gx_event);
        m_pendingEventValue.type = GX_EVENT_MOUSE;
        m_pendingEventValue.window = m_window->id;
        m_pendingEventValue.param1 = x;
        m_pendingEventValue.param2 = y;
        m_pendingEventValue.param3 = GX_MOUSE_PACK(button, action);
        m_pendingEventValue.param4 = wheelDelta;
        m_pendingEvent = true;
        if (action != GX_MOUSE_ACTION_MOVE) {
            serial::puts("[NATIVE-ELF] mouse event app=");
            serial::puts(m_window->title);
            serial::puts(" action=");
            serial::puts(action == GX_MOUSE_ACTION_DOWN ? "down" :
                         action == GX_MOUSE_ACTION_UP ? "up" : "wheel");
            serial::puts(" button=0x");
            serial::put_hex32(static_cast<uint32_t>(button));
            serial::putc('\n');
        }
    }

    void record_frame_guard_failure();

    bool guards_intact() const {
        if (!m_allocation || !m_pixels || m_pixelBytes == 0) return true;
        const uint32_t words = m_pixelBytes / 4u;
        return m_allocation[0] == 0xC0DEC0DEu && m_allocation[words + 1u] == 0xFACEB00Cu;
    }

    Runtime* m_runtime;
    uint32_t* m_allocation;
    uint32_t* m_pixels;
    uint32_t m_pixelBytes;
    uint32_t m_width;
    uint32_t m_height;
    uint32_t m_strideBytes;
    uint32_t* m_surface;
    uint32_t m_surfaceBytes;
    gx_event m_pendingEventValue;
    bool m_pendingEvent;
    bool m_closed;
};

struct Runtime {
    Package* package;
    NativeWindowOwner* owner;
    gx_app_context context;
    gx_host_calls host;
    uint8_t* image;
    uint64_t imageBytes;
    gx_result (GX_CALL *entry)(gx_app_context*);
    uint8_t* stack;
    bool exitRequested;
    gx_result exitCode;
    bool lastFocus;
    bool focusKnown;
    uint32_t readLogCount;
    uint32_t tickLogCount;
    uint32_t frameGuardFailures;
    NativeAbiOperation currentAbiOperation;
    NativeAbiOperation lastCompletedAbiOperation;
    uint64_t abiCallCount;
    uint64_t appFrameSequence;
    bool faulted;
    uint64_t loadMinimum;
    uint64_t loadMaximum;
    uint64_t executableMinimum;
    uint64_t executableMaximum;
    uint64_t faultVector;
    uint64_t faultErrorCode;
    uint64_t faultRip;
    uint64_t faultRsp;
    uint64_t faultRbp;
    uint64_t faultCr2;
};

void NativeWindowOwner::record_frame_guard_failure() {
    if (m_runtime) ++m_runtime->frameGuardFailures;
}

static Package s_packages[kMaxPackages];
static uint32_t s_packageCount = 0;
static char s_manifest[kMaxManifestBytes];
static bool s_discovered = false;
static bool s_launchInProgress = false;
static uint64_t s_runtimeSequence = 0;
static Runtime* s_activeRuntime = nullptr;

extern "C" uint64_t gxos_native_fault_recovery_rsp = 0;
extern "C" uint64_t gxos_native_fault_recovery_rip = 0;

extern "C" void gxos_native_set_fault_recovery(uint64_t stack, uint64_t instruction) {
    gxos_native_fault_recovery_rsp = stack;
    gxos_native_fault_recovery_rip = instruction;
}

extern "C" gx_result gxos_native_call_on_stack(
    gx_result (GX_CALL *entry)(gx_app_context*), gx_app_context* context, uint8_t* stackTop);
extern "C" void gxos_native_call_on_stack_end();

static bool text_equal(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb + ('a' - 'A'));
        if (ca != cb) return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static uint32_t text_length(const char* value) {
    uint32_t length = 0;
    if (value) while (value[length]) ++length;
    return length;
}

static void serial_put_u32_decimal(uint32_t value) {
    char digits[11];
    uint32_t count = 0;
    do {
        digits[count++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && count < sizeof(digits));
    while (count > 0u) serial::putc(digits[--count]);
}

static bool text_copy(char* destination, uint32_t capacity, const char* source) {
    if (!destination || capacity == 0 || !source) return false;
    uint32_t i = 0;
    while (source[i] && i + 1u < capacity) {
        destination[i] = source[i];
        ++i;
    }
    destination[i] = '\0';
    return source[i] == '\0';
}

static const char* find_text(const char* text, const char* needle) {
    if (!text || !needle || !needle[0]) return text;
    const uint32_t needleLength = text_length(needle);
    for (const char* current = text; *current; ++current) {
        uint32_t i = 0;
        while (i < needleLength && current[i] == needle[i]) ++i;
        if (i == needleLength) return current;
    }
    return nullptr;
}

static bool json_string(const char* json, const char* key, char* output, uint32_t capacity) {
    const char* cursor = json;
    while ((cursor = find_text(cursor, key)) != nullptr) {
        cursor += text_length(key);
        while (*cursor && *cursor != ':') ++cursor;
        if (!*cursor) return false;
        ++cursor;
        while (*cursor && *cursor != '"') ++cursor;
        if (!*cursor) return false;
        ++cursor;
        uint32_t length = 0;
        while (cursor[length] && cursor[length] != '"') ++length;
        if (length + 1u > capacity) return false;
        for (uint32_t i = 0; i < length; ++i) output[i] = cursor[i];
        output[length] = '\0';
        return true;
    }
    return false;
}

static void log_package_line(const char* prefix, const Package* package) {
    serial::puts("[NATIVE-ELF] ");
    serial::puts(prefix);
    if (package) {
        serial::puts(" package=");
        serial::puts(package->directory);
        serial::puts(" id=");
        serial::puts(package->id);
        serial::puts(" displayName=");
        serial::puts(package->displayName);
    }
    serial::putc('\n');
}

static void log_runtime(Runtime* runtime, const char* message) {
    serial::puts("[NATIVE-ELF] runtime=");
    serial::put_hex64(s_runtimeSequence);
    if (runtime && runtime->package) {
        serial::puts(" app=");
        serial::puts(runtime->package->displayName);
    }
    serial::puts(" ");
    serial::puts(message ? message : "");
    serial::putc('\n');
}

static const char* abi_operation_name(NativeAbiOperation operation) {
    switch (operation) {
    case NativeAbiOperation::GetTicks: return "GetTicks";
    case NativeAbiOperation::ReadResource: return "ReadResource";
    case NativeAbiOperation::CreateWindow: return "CreateWindow";
    case NativeAbiOperation::PresentFrame: return "PresentFrame";
    case NativeAbiOperation::PollEvent: return "PollEvent";
    case NativeAbiOperation::CloseWindow: return "CloseWindow";
    case NativeAbiOperation::RequestExit: return "RequestExit";
    case NativeAbiOperation::None: break;
    }
    return "None";
}

static const char* native_exception_name(uint64_t vector) {
    switch (vector) {
        case 0: return "DE divide-error";
        case 1: return "DB debug";
        case 2: return "NMI";
        case 3: return "BP breakpoint";
        case 4: return "OF overflow";
        case 5: return "BR bounds";
        case 6: return "UD invalid-opcode";
        case 7: return "NM device-not-available";
        case 8: return "DF double-fault";
        case 10: return "TS invalid-TSS";
        case 11: return "NP segment-not-present";
        case 12: return "SS stack-segment";
        case 13: return "GP general-protection";
        case 14: return "PF page-fault";
        case 16: return "MF x87";
        case 17: return "AC alignment-check";
        case 18: return "MC machine-check";
        case 19: return "XM SIMD";
        case 20: return "VE virtualization";
        case 21: return "CP control-protection";
        default: return "unexpected-exception";
    }
}

static void abi_begin(Runtime* runtime, NativeAbiOperation operation) {
    if (!runtime) return;
    runtime->currentAbiOperation = operation;
    ++runtime->abiCallCount;
}

static void abi_complete(Runtime* runtime, NativeAbiOperation operation) {
    if (!runtime) return;
    runtime->lastCompletedAbiOperation = operation;
    runtime->currentAbiOperation = NativeAbiOperation::None;
}

static gx_result abi_result(Runtime* runtime, NativeAbiOperation operation, gx_result result) {
    abi_complete(runtime, operation);
    return result;
}

static void log_breadcrumb(Runtime* runtime, const char* marker) {
    serial::puts("PACBM ");
    serial::puts(marker ? marker : "UNKNOWN");
    serial::puts(" runtime=");
    serial::put_hex64(runtime ? s_runtimeSequence : 0);
    serial::puts(" window=0x");
    serial::put_hex64(runtime && runtime->owner && runtime->owner->window() ?
        runtime->owner->window()->id : 0);
    serial::putc('\n');
}

static bool package_path(const Package* package, const char* relative, char* output, uint32_t capacity) {
    if (!package || !relative || !relative[0] || relative[0] == '/') return false;
    const char* component = relative;
    while (*component) {
        const char* end = component;
        while (*end && *end != '/') ++end;
        if ((end - component) == 2 && component[0] == '.' && component[1] == '.') return false;
        component = *end ? end + 1 : end;
    }
    const uint32_t rootLength = text_length(package->root);
    const uint32_t relativeLength = text_length(relative);
    if (rootLength + 1u + relativeLength + 1u > capacity) return false;
    for (uint32_t i = 0; i < rootLength; ++i) output[i] = package->root[i];
    output[rootLength] = '/';
    for (uint32_t i = 0; i < relativeLength; ++i) {
        if (relative[i] == '\\') return false;
        output[rootLength + 1u + i] = relative[i];
    }
    output[rootLength + 1u + relativeLength] = '\0';
    return true;
}

static bool read_at(const char* path, uint64_t offset, void* buffer, uint32_t length, uint32_t* outRead) {
    if (outRead) *outRead = 0;
    if (!path || !buffer || length == 0) return false;
    const uint8_t handle = vfs::open(path, vfs::OPEN_READ);
    if (handle == 0xFF) return false;
    bool okay = vfs::seek(handle, static_cast<int64_t>(offset), vfs::SEEK_SET) == vfs::VFS_OK;
    uint32_t total = 0;
    while (okay && total < length) {
        const int32_t amount = vfs::read(handle, static_cast<uint8_t*>(buffer) + total, length - total);
        if (amount <= 0) break;
        total += static_cast<uint32_t>(amount);
    }
    vfs::close(handle);
    if (outRead) *outRead = total;
    return okay && total == length;
}

static bool read_file_exact(const char* path, void* buffer, uint32_t length) {
    uint32_t amount = 0;
    return read_at(path, 0, buffer, length, &amount) && amount == length;
}

static bool parse_package(const char* directory, Package* package) {
    if (!directory || !package) return false;
    for (uint32_t i = 0; i < sizeof(Package); ++i) reinterpret_cast<uint8_t*>(package)[i] = 0;
    package->valid = false;
    if (!text_copy(package->directory, sizeof(package->directory), directory)) return false;
    package->root[0] = '/';
    package->root[1] = 'A'; package->root[2] = 'p'; package->root[3] = 'p'; package->root[4] = 's';
    package->root[5] = '/';
    if (!text_copy(package->root + 6, sizeof(package->root) - 6u, directory)) return false;

    char manifestPath[256];
    if (!package_path(package, "app.json", manifestPath, sizeof(manifestPath))) return false;
    const int32_t count = vfs::read_file(manifestPath, s_manifest, kMaxManifestBytes - 1u);
    if (count <= 0) return false;
    s_manifest[count] = '\0';
    char kind[32];
    char runtime[32];
    char architecture[32];
    package->startMenuVisible = true;
    if (!json_string(s_manifest, "\"kind\"", kind, sizeof(kind)) ||
        !text_equal(kind, "NativeElf") ||
        !json_string(s_manifest, "\"runtime\"", runtime, sizeof(runtime)) ||
        !text_equal(runtime, "native-elf")) return false;
    if (!json_string(s_manifest, "\"id\"", package->id, sizeof(package->id)) ||
        !json_string(s_manifest, "\"displayName\"", package->displayName, sizeof(package->displayName)) ||
        !json_string(s_manifest, "\"architecture\"", architecture, sizeof(architecture)) ||
        !text_equal(architecture, "amd64") ||
        !json_string(s_manifest, "\"path\"", package->executable, sizeof(package->executable)) ||
        !json_string(s_manifest, "\"entryPoint\"", package->entryPoint, sizeof(package->entryPoint)) ||
         !json_string(s_manifest, "\"abi\"", package->abi, sizeof(package->abi))) return false;
    (void)json_string(s_manifest, "\"icon\"", package->icon, sizeof(package->icon));
    char startMenuVisible[16];
    if (json_string(s_manifest, "\"appearsInStartMenu\"", startMenuVisible, sizeof(startMenuVisible))) {
        package->startMenuVisible = !text_equal(startMenuVisible, "false");
    }
    if (!text_equal(package->entryPoint, "gx_main") || !text_equal(package->abi, GX_ABI_NAME)) return false;
    char executablePath[256];
    if (!package_path(package, package->executable, executablePath, sizeof(executablePath))) return false;
    vfs::FileInfo info;
    if (vfs::stat(executablePath, &info) != vfs::VFS_OK || info.type != vfs::FILE_TYPE_REGULAR) return false;
    package->executableBytes = info.size;
    package->valid = true;
    log_package_line("App Model package discovered", package);
    log_breadcrumb(nullptr, "01 DISCOVERED");
    serial::puts("[NATIVE-ELF] kind=NativeElf arch=amd64 entryPoint=");
    serial::puts(package->entryPoint);
    serial::puts(" abi=");
    serial::puts(package->abi);
    serial::puts(" executable=");
    serial::puts(executablePath);
    serial::puts(" bytes=0x");
    serial::put_hex64(package->executableBytes);
    serial::puts(" resources=package-relative VFS\n");
    return true;
}

void discover() {
    s_packageCount = 0;
    s_discovered = true;
    serial::puts("[NATIVE-ELF] App Model discovery root=/Apps source=bare-metal-VFS\n");
    const uint8_t iterator = vfs::opendir("/Apps");
    if (iterator == 0xFF) {
        // VFS can be mounted after early desktop probes.  Do not permanently
        // cache a negative result from a not-yet-mounted package root.
        s_discovered = false;
        serial::puts("[NATIVE-ELF] discovery result=NO_APPS_DIRECTORY\n");
        return;
    }
    vfs::DirEntry entry;
    while (s_packageCount < kMaxPackages && vfs::readdir(iterator, &entry)) {
        if (entry.type != vfs::FILE_TYPE_DIRECTORY || entry.name[0] == '.') continue;
        if (parse_package(entry.name, &s_packages[s_packageCount])) ++s_packageCount;
    }
    vfs::closedir(iterator);
    serial::puts("[NATIVE-ELF] discovery result packages=0x");
    serial::put_hex32(s_packageCount);
    serial::putc('\n');
}

static Package* find_package(const char* appName) {
    if (!appName) return nullptr;
    for (uint32_t i = 0; i < s_packageCount; ++i) {
        Package* package = &s_packages[i];
        if (text_equal(appName, package->displayName) || text_equal(appName, package->id) ||
            text_equal(appName, package->directory) ||
            (text_equal(appName, "PacMan") && text_equal(package->directory, "PacMan"))) return package;
    }
    return nullptr;
}

bool is_available(const char* appName) {
    if (!s_discovered) discover();
    return find_package(appName) != nullptr;
}

const PackageInfo* lookup_package(const char* appName) {
    if (!s_discovered) discover();
    return find_package(appName);
}

uint32_t package_count() {
    if (!s_discovered) discover();
    return s_packageCount;
}

const PackageInfo* package_at(uint32_t index) {
    if (!s_discovered) discover();
    return index < s_packageCount ? &s_packages[index] : nullptr;
}

struct Elf64Header {
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

struct Elf64ProgramHeader {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
};

static const uint32_t kElfLoad = 1u;

static uint64_t align_down(uint64_t value) { return value & ~(static_cast<uint64_t>(kPageSize) - 1u); }
static uint64_t align_up(uint64_t value) {
    return (value + kPageSize - 1u) & ~(static_cast<uint64_t>(kPageSize) - 1u);
}

static uint32_t read_u32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8u) |
        (static_cast<uint32_t>(bytes[2]) << 16u) |
        (static_cast<uint32_t>(bytes[3]) << 24u);
}

static uint64_t read_u64(const uint8_t* bytes) {
    uint64_t value = 0;
    for (uint32_t byte = 0; byte < sizeof(uint64_t); ++byte) {
        value |= static_cast<uint64_t>(bytes[byte]) << (byte * 8u);
    }
    return value;
}

static void write_u32(uint8_t* bytes, uint32_t value) {
    for (uint32_t byte = 0; byte < sizeof(uint32_t); ++byte) {
        bytes[byte] = static_cast<uint8_t>(value >> (byte * 8u));
    }
}

static void write_u64(uint8_t* bytes, uint64_t value) {
    for (uint32_t byte = 0; byte < sizeof(uint64_t); ++byte) {
        bytes[byte] = static_cast<uint8_t>(value >> (byte * 8u));
    }
}

static bool opcode_has_modrm(uint8_t opcode) {
    switch (opcode) {
        case 0x00u: case 0x01u: case 0x02u: case 0x03u:
        case 0x08u: case 0x09u: case 0x0Au: case 0x0Bu:
        case 0x10u: case 0x11u: case 0x12u: case 0x13u:
        case 0x18u: case 0x19u: case 0x1Au: case 0x1Bu:
        case 0x20u: case 0x21u: case 0x22u: case 0x23u:
        case 0x28u: case 0x29u: case 0x2Au: case 0x2Bu:
        case 0x30u: case 0x31u: case 0x32u: case 0x33u:
        case 0x38u: case 0x39u: case 0x3Au: case 0x3Bu:
        case 0x62u: case 0x63u: case 0x69u: case 0x6Bu:
        case 0x80u: case 0x81u: case 0x82u: case 0x83u:
        case 0x84u: case 0x85u: case 0x86u: case 0x87u:
        case 0x88u: case 0x89u: case 0x8Au: case 0x8Bu:
        case 0x8Cu: case 0x8Du: case 0x8Eu: case 0x8Fu:
        case 0xC0u: case 0xC1u: case 0xC6u: case 0xC7u:
        case 0xD0u: case 0xD1u: case 0xD2u: case 0xD3u:
        case 0xD8u: case 0xD9u: case 0xDAu: case 0xDBu:
        case 0xDCu: case 0xDDu: case 0xDEu: case 0xDFu:
        case 0xF6u: case 0xF7u: case 0xFEu: case 0xFFu:
            return true;
        default:
            return false;
    }
}

static bool two_byte_opcode_has_modrm(uint8_t opcode) {
    if ((opcode >= 0x10u && opcode <= 0x17u) ||
        (opcode >= 0x28u && opcode <= 0x2Fu) ||
        (opcode >= 0x40u && opcode <= 0x4Fu) ||
        (opcode >= 0x50u && opcode <= 0x6Fu) ||
        (opcode >= 0x70u && opcode <= 0x7Fu) ||
        (opcode >= 0x90u && opcode <= 0x9Fu)) return true;
    switch (opcode) {
        case 0xA3u: case 0xA4u: case 0xA5u: case 0xABu: case 0xAFu:
        case 0xB0u: case 0xB1u: case 0xB3u: case 0xB6u: case 0xB7u:
        case 0xBAu: case 0xBBu: case 0xBCu: case 0xBDu: case 0xBEu: case 0xBFu:
        case 0xC0u: case 0xC1u: case 0xC7u:
        case 0xD0u: case 0xD1u: case 0xD2u: case 0xD3u:
        case 0xE6u: case 0xE7u:
            return true;
        default:
            return false;
    }
}

static bool absolute_disp32_offset(const uint8_t* bytes, uint64_t offset, uint64_t end,
                                   uint64_t* displacementOffset) {
    uint64_t cursor = offset;
    uint32_t prefixCount = 0;
    while (cursor < end && prefixCount < 6u) {
        const uint8_t prefix = bytes[cursor];
        if (prefix == 0xF0u || prefix == 0xF2u || prefix == 0xF3u ||
            prefix == 0x2Eu || prefix == 0x36u || prefix == 0x3Eu ||
            prefix == 0x26u || prefix == 0x64u || prefix == 0x65u ||
            prefix == 0x66u || prefix == 0x67u ||
            (prefix >= 0x40u && prefix <= 0x4Fu)) {
            ++cursor;
            ++prefixCount;
            continue;
        }
        break;
    }
    if (cursor >= end) return false;

    const uint8_t opcode = bytes[cursor++];
    if (opcode == 0x0Fu) {
        if (cursor >= end) return false;
        const uint8_t extended = bytes[cursor++];
        if (extended == 0x38u || extended == 0x3Au) {
            if (cursor >= end) return false;
            ++cursor;
        } else if (!two_byte_opcode_has_modrm(extended)) {
            return false;
        }
    } else if (!opcode_has_modrm(opcode)) {
        return false;
    }

    if (cursor + 6u > end) return false;
    const uint8_t modrm = bytes[cursor];
    // A mod=00, r/m=100 ModRM selects a SIB byte.  Base=101 means the
    // following disp32 is absolute; the index may be none (0x25) or an
    // indexed jump-table form such as 0xC5 ([rax*8 + disp32]).
    if ((modrm & 0xC7u) != 0x04u || (bytes[cursor + 1u] & 0x07u) != 0x05u) return false;
    *displacementOffset = cursor + 2u;
    return true;
}

static bool rebase_absolute_disp32(Runtime* runtime, uint64_t minimum, uint64_t maximum,
                                   uint64_t delta, uint64_t displacementOffset) {
    const uint64_t value = read_u32(runtime->image + displacementOffset);
    if (value < minimum || value >= maximum || value + delta > 0xFFFFFFFFull) return false;
    write_u32(runtime->image + displacementOffset, static_cast<uint32_t>(value + delta));
    return true;
}

static uint64_t rebase_static_absolute_words(Runtime* runtime, uint64_t minimum, uint64_t maximum,
                                              uint64_t executableMinimum, uint64_t executableMaximum,
                                              uint64_t writableMinimum, uint64_t writableMaximum,
                                              uint64_t readOnlyMinimum, uint64_t readOnlyMaximum) {
    // The production package is intentionally static ET_EXEC and has no
    // dynamic relocation table.  Its small-model x86-64 code uses three
    // forms of absolute address: aligned data pointers, movabs imm64 values,
    // and SIB disp32 fields.  Rebase only those instruction encodings and
    // aligned data words; RIP-relative code remains valid under one delta.
    const uint64_t delta = reinterpret_cast<uintptr_t>(runtime->image) - minimum;
    uint64_t count = 0;
    const uint64_t executableStart = executableMinimum - minimum;
    const uint64_t executableEnd = executableMaximum - minimum;

    // REX.W + MOV r64, imm64 (48..4F B8..BF imm64).  The immediate begins at
    // an intentionally unaligned offset in several gx_main call sites.
    for (uint64_t offset = executableStart; offset + 10u <= executableEnd; ++offset) {
        const uint8_t rex = runtime->image[offset];
        const uint8_t opcode = runtime->image[offset + 1u];
        if ((rex & 0xF8u) != 0x48u || opcode < 0xB8u || opcode > 0xBFu) continue;
        const uint64_t value = read_u64(runtime->image + offset + 2u);
        if (value < minimum || value >= maximum) continue;
        write_u64(runtime->image + offset + 2u, value + delta);
        ++count;
    }

    // Absolute disp32 memory operands use a no-base SIB (04/0C/14/... 25)
    // after an optional x86 prefix and opcode.  This covers the static ELF's
    // direct global loads/stores without treating arbitrary instruction words
    // as pointers.
    uint64_t lastDisplacementOffset = ~0ull;
    for (uint64_t offset = executableStart; offset < executableEnd; ++offset) {
        uint64_t displacementOffset = 0;
        if (!absolute_disp32_offset(runtime->image, offset, executableEnd, &displacementOffset)) continue;
        if (displacementOffset == lastDisplacementOffset) continue;
        lastDisplacementOffset = displacementOffset;
        if (rebase_absolute_disp32(runtime, minimum, maximum, delta, displacementOffset)) ++count;
    }

    // Static pointer tables are naturally eight-byte aligned in writable or
    // read-only PT_LOAD data.  Do not scan executable bytes as arbitrary
    // words: large code-model immediates and instruction operands can look
    // like addresses and corrupt otherwise valid instructions.
    if (writableMaximum > writableMinimum) {
        const uint64_t writableStart = writableMinimum - minimum;
        const uint64_t writableEnd = writableMaximum - minimum;
        for (uint64_t offset = writableStart; offset + sizeof(uint64_t) <= writableEnd; offset += sizeof(uint64_t)) {
            const uint64_t value = read_u64(runtime->image + offset);
            if (value < minimum || value >= maximum) continue;
            write_u64(runtime->image + offset, value + delta);
            ++count;
        }
    }
    if (readOnlyMaximum > readOnlyMinimum) {
        const uint64_t readOnlyStart = readOnlyMinimum - minimum;
        const uint64_t readOnlyEnd = readOnlyMaximum - minimum;
        for (uint64_t offset = readOnlyStart; offset + sizeof(uint64_t) <= readOnlyEnd; offset += sizeof(uint64_t)) {
            const uint64_t value = read_u64(runtime->image + offset);
            if (value < minimum || value >= maximum) continue;
            write_u64(runtime->image + offset, value + delta);
            ++count;
        }
    }
    return count;
}

static bool load_image(Runtime* runtime) {
    char path[256];
    if (!package_path(runtime->package, runtime->package->executable, path, sizeof(path))) return false;
    Elf64Header header;
    if (!read_file_exact(path, &header, sizeof(header))) {
        log_runtime(runtime, "load FAIL=elf-header-read");
        return false;
    }
    if (header.ident[0] != 0x7F || header.ident[1] != 'E' || header.ident[2] != 'L' || header.ident[3] != 'F' ||
        header.ident[4] != 2 || header.ident[5] != 1 || header.type != 2 || header.machine != 62 ||
        header.phentsize != sizeof(Elf64ProgramHeader) || header.phnum == 0 || header.phnum > 32u ||
        header.entry == 0) {
        log_runtime(runtime, "load FAIL=elf-header-validation");
        return false;
    }

    uint64_t minimum = ~0ull;
    uint64_t maximum = 0;
    uint64_t executableMinimum = ~0ull;
    uint64_t executableMaximum = 0;
    uint64_t writableMinimum = ~0ull;
    uint64_t writableMaximum = 0;
    uint64_t readOnlyMinimum = ~0ull;
    uint64_t readOnlyMaximum = 0;
    uint32_t loadCount = 0;
    for (uint16_t i = 0; i < header.phnum; ++i) {
        Elf64ProgramHeader program;
        const uint64_t offset = header.phoff + static_cast<uint64_t>(i) * header.phentsize;
        if (offset > runtime->package->executableBytes || !read_at(path, offset, &program, sizeof(program), nullptr)) {
            log_runtime(runtime, "load FAIL=program-header-read");
            return false;
        }
        if (program.type != kElfLoad) continue;
        if (program.filesz > program.memsz || program.offset + program.filesz > runtime->package->executableBytes ||
            program.vaddr + program.memsz < program.vaddr) {
            log_runtime(runtime, "load FAIL=segment-bounds");
            return false;
        }
        const uint64_t start = align_down(program.vaddr);
        const uint64_t end = align_up(program.vaddr + program.memsz);
        if (start < minimum) minimum = start;
        if (end > maximum) maximum = end;
        if ((program.flags & 1u) != 0u) {
            if (start < executableMinimum) executableMinimum = start;
            if (end > executableMaximum) executableMaximum = end;
        }
        if ((program.flags & 2u) != 0u) {
            if (start < writableMinimum) writableMinimum = start;
            if (end > writableMaximum) writableMaximum = end;
        }
        if ((program.flags & 3u) == 0u) {
            if (start < readOnlyMinimum) readOnlyMinimum = start;
            if (end > readOnlyMaximum) readOnlyMaximum = end;
        }
        ++loadCount;
    }
    if (loadCount == 0 || maximum <= minimum || maximum - minimum > kMaxLoadedImageBytes ||
        header.entry < minimum || header.entry >= maximum || executableMaximum <= executableMinimum) {
        log_runtime(runtime, "load FAIL=load-layout");
        return false;
    }

    const uint64_t imageBytes = maximum - minimum;
    runtime->image = new uint8_t[static_cast<uint32_t>(imageBytes)];
    if (!runtime->image) {
        log_runtime(runtime, "load FAIL=kernel-heap-image-allocation");
        return false;
    }
    runtime->imageBytes = imageBytes;
    runtime->loadMinimum = minimum;
    runtime->loadMaximum = maximum;
    runtime->executableMinimum = executableMinimum;
    runtime->executableMaximum = executableMaximum;
    runtime->entry = reinterpret_cast<gx_result (GX_CALL *)(gx_app_context*)>(
        runtime->image + (header.entry - minimum));
    for (uint64_t i = 0; i < imageBytes; ++i) runtime->image[i] = 0;

    for (uint16_t i = 0; i < header.phnum; ++i) {
        Elf64ProgramHeader program;
        const uint64_t offset = header.phoff + static_cast<uint64_t>(i) * header.phentsize;
        if (!read_at(path, offset, &program, sizeof(program), nullptr) || program.type != kElfLoad) continue;
        uint64_t copied = 0;
        while (copied < program.filesz) {
            uint32_t chunk = static_cast<uint32_t>(program.filesz - copied);
            if (chunk > kReadChunkBytes) chunk = kReadChunkBytes;
            uint32_t amount = 0;
            if (!read_at(path, program.offset + copied,
                         runtime->image + (program.vaddr - minimum) + copied, chunk, &amount) || amount != chunk) {
                log_runtime(runtime, "load FAIL=segment-copy");
                return false;
            }
            copied += chunk;
        }
    }

    const uint64_t rebasedWords = rebase_static_absolute_words(runtime, minimum, maximum,
        executableMinimum, executableMaximum, writableMinimum, writableMaximum,
        readOnlyMinimum, readOnlyMaximum);

    serial::puts("[NATIVE-ELF] runtime="); serial::put_hex64(s_runtimeSequence);
    serial::puts(" load PASS format=ELF64 machine=amd64 segments=0x"); serial::put_hex32(loadCount);
    serial::puts(" source="); serial::puts(path);
    serial::puts(" imageBytes=0x"); serial::put_hex64(imageBytes);
    serial::puts(" minVaddr=0x"); serial::put_hex64(minimum);
    serial::puts(" maxVaddr=0x"); serial::put_hex64(maximum);
    serial::puts(" relocatedBase=0x"); serial::put_hex64(reinterpret_cast<uintptr_t>(runtime->image));
    serial::puts(" rebasedAbsoluteWords=0x"); serial::put_hex64(rebasedWords);
    serial::puts(" entry=0x"); serial::put_hex64(reinterpret_cast<uintptr_t>(runtime->image + (header.entry - minimum)));
    serial::puts(" stack=app-owned\n");
    log_breadcrumb(runtime, "02 ELF_VALIDATED");
    return true;
}

static Runtime* runtime_from(gx_app_context* context) {
    return context ? static_cast<Runtime*>(context->userData) : nullptr;
}

static gx_result GX_CALL host_log(gx_app_context* context, const char* message) {
    Runtime* runtime = runtime_from(context);
    if (message && message[0] == 'P' && message[1] == 'A' && message[2] == 'C' &&
        message[3] == 'B' && message[4] == 'M' && message[5] == ' ') {
        log_breadcrumb(runtime, message + 6);
        return GX_OK;
    }
    serial::puts("[NATIVE-ELF] app=");
    serial::puts(runtime && runtime->package ? runtime->package->displayName : "(none)");
    serial::puts(" ");
    serial::puts(message ? message : "");
    serial::putc('\n');
    return GX_OK;
}

static uint32_t GX_CALL host_get_api_version(gx_app_context*) { return GX_API_VERSION; }

static gx_result GX_CALL host_request_window_ex(gx_app_context* context, const char* title,
                                                 int width, int height, uint32_t flags, gx_handle* outWindow) {
    Runtime* runtime = runtime_from(context);
    if (!runtime || !outWindow || !title || runtime->owner) return GX_ERROR_INVALID_ARGUMENT;
    abi_begin(runtime, NativeAbiOperation::CreateWindow);
    if (width <= 0 || height <= 0 || width > 1280 || height > 760) {
        abi_complete(runtime, NativeAbiOperation::CreateWindow);
        return GX_ERROR_UNSUPPORTED;
    }
    runtime->owner = new NativeWindowOwner(runtime);
    if (!runtime->owner || !runtime->owner->create(title, width, height, flags)) {
        if (runtime->owner) delete runtime->owner;
        runtime->owner = nullptr;
        abi_complete(runtime, NativeAbiOperation::CreateWindow);
        return GX_ERROR_FAILED;
    }
    *outWindow = runtime->owner->window()->id;
    serial::puts("[NATIVE-ELF] window PASS app="); serial::puts(runtime->package->displayName);
    serial::puts(" title="); serial::puts(title);
    serial::puts(" size="); serial_put_u32_decimal(static_cast<uint32_t>(width)); serial::putc('x');
    serial_put_u32_decimal(static_cast<uint32_t>(height)); serial::puts(" compositor\n");
    abi_complete(runtime, NativeAbiOperation::CreateWindow);
    return GX_OK;
}

static gx_result GX_CALL host_request_window(gx_app_context* context, const char* title,
                                             int width, int height, gx_handle* outWindow) {
    return host_request_window_ex(context, title, width, height, GX_WINDOW_FLAG_FIXED_SIZE, outWindow);
}

static gx_result GX_CALL host_draw_text(gx_app_context* context, gx_handle window, int x, int y, const char* text) {
    Runtime* runtime = runtime_from(context);
    if (!runtime || !runtime->owner || !runtime->owner->window() ||
        window != runtime->owner->window()->id || !text) return GX_ERROR_INVALID_ARGUMENT;
    return runtime->owner->draw_text(x, y, text);
}

static gx_result GX_CALL host_draw_rect(gx_app_context* context, gx_handle window, int x, int y,
                                        int width, int height, uint32_t color) {
    Runtime* runtime = runtime_from(context);
    if (!runtime || !runtime->owner || !runtime->owner->window() ||
        window != runtime->owner->window()->id) return GX_ERROR_INVALID_ARGUMENT;
    return runtime->owner->draw_rect(x, y, width, height, color);
}

static void pump_desktop(Runtime* runtime) {
    input::poll();
    const int8_t wheel = input::mouse_scroll_y();
    if (input::mouse_dirty()) {
        input::mouse_clear_dirty();
        desktop::handle_mouse(input::mouse_x(), input::mouse_y(), input::mouse_buttons());
    }
    if (wheel != 0) desktop::handle_mouse_wheel(input::mouse_x(), input::mouse_y(), wheel);
    desktop::draw();
    desktop::draw_cursor(input::mouse_x(), input::mouse_y());
    (void)runtime;
}

static gx_result GX_CALL host_poll_event(gx_app_context* context, gx_event* outEvent, int timeoutMs) {
    Runtime* runtime = runtime_from(context);
    if (!runtime || !outEvent) return GX_ERROR_INVALID_ARGUMENT;
    abi_begin(runtime, NativeAbiOperation::PollEvent);
    outEvent->size = sizeof(gx_event);
    outEvent->type = GX_EVENT_NONE;
    outEvent->window = runtime->owner && runtime->owner->window() ? runtime->owner->window()->id : 0;
    outEvent->param1 = outEvent->param2 = outEvent->param3 = outEvent->param4 = 0;
    const uint64_t start = pit::ticks();
    const uint64_t deadline = start + (timeoutMs > 0 ? static_cast<uint64_t>((timeoutMs + 9) / 10) : 0);
    for (;;) {
        if (!runtime->owner || runtime->owner->closed()) {
            outEvent->type = GX_EVENT_WINDOW_CLOSE;
            return abi_result(runtime, NativeAbiOperation::PollEvent, GX_OK);
        }

        // Pump the compositor while polling so retained native-app surfaces
        // and window/input ownership stay live between application events.
        pump_desktop(runtime);

        const bool focused = compositor::KernelCompositor::getFocusedWindow() == runtime->owner->window();
        if (!runtime->focusKnown || focused != runtime->lastFocus) {
            runtime->focusKnown = true;
            runtime->lastFocus = focused;
            outEvent->type = focused ? GX_EVENT_WINDOW_FOCUS : GX_EVENT_WINDOW_BLUR;
            return abi_result(runtime, NativeAbiOperation::PollEvent, GX_OK);
        }

        if (runtime->owner->take_pending_event(outEvent)) {
            return abi_result(runtime, NativeAbiOperation::PollEvent, GX_OK);
        }

        ps2keyboard::KeyEvent keyEvent;
        if (ps2keyboard::get_event(&keyEvent)) {
            serial::puts("[NATIVE-ELF] input event key=0x"); serial::put_hex32(keyEvent.key);
            serial::puts(" action=");
            serial::puts(keyEvent.action == ps2keyboard::KeyAction::Down ? "down\n" : "up\n");
            outEvent->type = GX_EVENT_KEY;
            outEvent->param1 = static_cast<int>(keyEvent.key == ps2keyboard::KEY_EVENT_LEFT ? GX_KEY_LEFT :
                keyEvent.key == ps2keyboard::KEY_EVENT_UP ? GX_KEY_UP :
                keyEvent.key == ps2keyboard::KEY_EVENT_RIGHT ? GX_KEY_RIGHT :
                keyEvent.key == ps2keyboard::KEY_EVENT_DOWN ? GX_KEY_DOWN : keyEvent.key);
            outEvent->param2 = keyEvent.action == ps2keyboard::KeyAction::Down ? GX_KEY_ACTION_DOWN : GX_KEY_ACTION_UP;
            outEvent->param3 = (ps2keyboard::is_shift_down() ? GX_KEY_MOD_SHIFT : 0) |
                (ps2keyboard::is_ctrl_down() ? GX_KEY_MOD_CTRL : 0) |
                (ps2keyboard::is_alt_down() ? GX_KEY_MOD_ALT : 0);
            return abi_result(runtime, NativeAbiOperation::PollEvent, GX_OK);
        }

        if (timeoutMs <= 0 || pit::ticks() >= deadline) {
            return abi_result(runtime, NativeAbiOperation::PollEvent, GX_ERROR_TIMEOUT);
        }
        arch::halt();
    }
}

static gx_result GX_CALL host_wait_for_close(gx_app_context* context, gx_handle window, int timeoutMs) {
    Runtime* runtime = runtime_from(context);
    if (!runtime || !runtime->owner || window != runtime->owner->window()->id) return GX_ERROR_INVALID_ARGUMENT;
    for (;;) {
        gx_event event;
        const gx_result result = host_poll_event(context, &event, timeoutMs);
        if (result != GX_OK) return result;
        if (event.type == GX_EVENT_WINDOW_CLOSE) return GX_OK;
    }
}

static gx_result GX_CALL host_exit(gx_app_context* context, gx_result exitCode) {
    Runtime* runtime = runtime_from(context);
    if (!runtime) return GX_ERROR_INVALID_ARGUMENT;
    abi_begin(runtime, NativeAbiOperation::RequestExit);
    log_breadcrumb(runtime, "20 EXIT_REQUESTED");
    runtime->exitRequested = true;
    runtime->exitCode = exitCode;
    return abi_result(runtime, NativeAbiOperation::RequestExit, GX_OK);
}

static gx_result GX_CALL host_file_exists(gx_app_context* context, const char* relative, uint32_t* outExists) {
    Runtime* runtime = runtime_from(context);
    if (!runtime || !outExists) return GX_ERROR_INVALID_ARGUMENT;
    char path[256];
    if (!package_path(runtime->package, relative, path, sizeof(path))) return GX_ERROR_PERMISSION_DENIED;
    *outExists = vfs::exists(path) ? 1u : 0u;
    return GX_OK;
}

static gx_result GX_CALL host_file_read(gx_app_context* context, const char* relative, uint64_t offset,
                                        void* buffer, uint32_t bufferSize, uint32_t* outBytesRead) {
    Runtime* runtime = runtime_from(context);
    if (outBytesRead) *outBytesRead = 0;
    if (!runtime || !relative || !buffer || !outBytesRead || bufferSize == 0) return GX_ERROR_INVALID_ARGUMENT;
    abi_begin(runtime, NativeAbiOperation::ReadResource);
    char path[256];
    if (!package_path(runtime->package, relative, path, sizeof(path))) {
        return abi_result(runtime, NativeAbiOperation::ReadResource, GX_ERROR_PERMISSION_DENIED);
    }
    const uint8_t handle = vfs::open(path, vfs::OPEN_READ);
    if (handle == 0xFF) return abi_result(runtime, NativeAbiOperation::ReadResource, GX_ERROR_FAILED);
    const int64_t size = vfs::file_size(handle);
    if (size < 0 || offset > static_cast<uint64_t>(size) ||
        vfs::seek(handle, static_cast<int64_t>(offset), vfs::SEEK_SET) != vfs::VFS_OK) {
        vfs::close(handle);
        return abi_result(runtime, NativeAbiOperation::ReadResource, GX_ERROR_FAILED);
    }
    uint32_t requested = bufferSize;
    if (requested > kReadChunkBytes) requested = kReadChunkBytes;
    const int32_t amount = vfs::read(handle, buffer, requested);
    vfs::close(handle);
    if (amount < 0) return abi_result(runtime, NativeAbiOperation::ReadResource, GX_ERROR_FAILED);
    *outBytesRead = static_cast<uint32_t>(amount);
    if (runtime->readLogCount < 80u) {
        ++runtime->readLogCount;
        serial::puts("[NATIVE-ELF] file_read app="); serial::puts(runtime->package->displayName);
        serial::puts(" relative="); serial::puts(relative);
        serial::puts(" offset=0x"); serial::put_hex64(offset);
        serial::puts(" requested=0x"); serial::put_hex32(requested);
        serial::puts(" returned=0x"); serial::put_hex32(static_cast<uint32_t>(amount));
        serial::puts(" size=0x"); serial::put_hex64(static_cast<uint64_t>(size));
        serial::puts(" path="); serial::puts(path); serial::putc('\n');
    }
    return abi_result(runtime, NativeAbiOperation::ReadResource, GX_OK);
}

static gx_result GX_CALL host_file_read_all(gx_app_context* context, const char* relative,
                                            void* buffer, uint32_t bufferSize, uint32_t* outBytesRead) {
    if (!outBytesRead) return GX_ERROR_INVALID_ARGUMENT;
    *outBytesRead = 0;
    uint64_t offset = 0;
    uint8_t* output = static_cast<uint8_t*>(buffer);
    while (offset < bufferSize) {
        uint32_t amount = 0;
        const gx_result result = host_file_read(context, relative, offset, output + offset,
            bufferSize - static_cast<uint32_t>(offset), &amount);
        if (result != GX_OK) return result;
        if (amount == 0) break;
        offset += amount;
        *outBytesRead = static_cast<uint32_t>(offset);
        if (amount < kReadChunkBytes) break;
    }
    return GX_OK;
}

static gx_result GX_CALL host_present_frame(gx_app_context* context, gx_handle window, int x, int y,
                                            int width, int height, uint32_t strideBytes, uint32_t pixelFormat,
                                            const void* pixels, uint32_t pixelBytes) {
    Runtime* runtime = runtime_from(context);
    if (!runtime) return GX_ERROR_INVALID_ARGUMENT;
    abi_begin(runtime, NativeAbiOperation::PresentFrame);
    const uint64_t requiredBytes = static_cast<uint64_t>(strideBytes) *
        static_cast<uint64_t>(height > 0 ? height : 0);
    if (!runtime->owner || !runtime->owner->window() ||
        window != runtime->owner->window()->id || !pixels || x != 0 || y != 0 ||
        width != 448 || height != 553 || strideBytes != 1792u ||
        pixelFormat != GX_PIXEL_FORMAT_XRGB8888 || requiredBytes > 0xFFFFFFFFull ||
        pixelBytes < static_cast<uint32_t>(requiredBytes) || pixelBytes > kMaxFrameBytes) {
        return abi_result(runtime, NativeAbiOperation::PresentFrame, GX_ERROR_INVALID_ARGUMENT);
    }
    ++runtime->appFrameSequence;
    serial::puts("[NATIVE-ELF] frame copy begin bytes=0x"); serial::put_hex32(pixelBytes); serial::putc('\n');
    if (!runtime->owner->present(pixels, static_cast<uint32_t>(width), static_cast<uint32_t>(height), strideBytes)) {
        return abi_result(runtime, NativeAbiOperation::PresentFrame, GX_ERROR_FAILED);
    }
    pump_desktop(runtime);
    serial::puts("[NATIVE-ELF] frame copy complete\n");
    if (runtime->owner->closed()) {
        return abi_result(runtime, NativeAbiOperation::PresentFrame, GX_ERROR_FAILED);
    }
    serial::puts("[NATIVE-ELF] frame PASS app="); serial::puts(runtime->package->displayName);
    serial::puts(" window=0x"); serial::put_hex64(window);
    serial::puts(" size=448x553 stride=1792 bytes=990976 pixelFormat=XRGB8888\n");
    return abi_result(runtime, NativeAbiOperation::PresentFrame, GX_OK);
}

static uint64_t GX_CALL host_get_ticks_ms(gx_app_context* context) {
    Runtime* runtime = runtime_from(context);
    if (runtime) abi_begin(runtime, NativeAbiOperation::GetTicks);
    const uint64_t value = pit::ticks() * 10u;
    if (runtime && runtime->tickLogCount < 3u) {
        ++runtime->tickLogCount;
        serial::puts("[NATIVE-ELF] ticks_ms app="); serial::puts(runtime->package->displayName);
        serial::puts(" value=0x"); serial::put_hex64(value); serial::puts(" source=PIT-100Hz\n");
    }
    if (runtime) abi_complete(runtime, NativeAbiOperation::GetTicks);
    return value;
}

static bool address_in_range(uint64_t address, uint64_t begin, uint64_t end) {
    return begin != 0 && end > begin && address >= begin && address < end;
}

static const char* exception_code_region(Runtime* runtime, uint64_t rip) {
    if (runtime && address_in_range(rip, reinterpret_cast<uintptr_t>(runtime->image),
                                    reinterpret_cast<uintptr_t>(runtime->image) + runtime->imageBytes)) {
        return "PacMan ELF text/data image";
    }
    if (address_in_range(rip, reinterpret_cast<uintptr_t>(&gxos_native_call_on_stack),
                         reinterpret_cast<uintptr_t>(&gxos_native_call_on_stack_end))) {
        return "Native ELF trampoline";
    }
    if (runtime && runtime->currentAbiOperation != NativeAbiOperation::None) {
        return "runtime ABI dispatcher/window/frame path";
    }
    if (rip >= 0x100000ull && rip < 0x2BF000ull) return "kernel text (best-effort range)";
    return "outside known ranges";
}

extern "C" uint64_t gxos_native_exception_dispatch(const NativeExceptionFrame* frame) {
    Runtime* runtime = s_activeRuntime;
    if (!frame) {
        serial::puts("[NATIVE-ELF-FAULT] missing exception frame; kernel halted\n");
        for (;;) arch::halt();
    }

    const uint64_t interruptedRsp = reinterpret_cast<uintptr_t>(frame) + sizeof(NativeExceptionFrame);
    uint16_t ss = 0;
    asm volatile ("mov %%ss, %0" : "=r"(ss));
    const uint64_t cr2 = arch::read_cr2();
    const uint64_t cr3 = arch::read_cr3();
    const bool stackInRuntime = runtime && address_in_range(interruptedRsp,
        reinterpret_cast<uintptr_t>(runtime->stack),
        reinterpret_cast<uintptr_t>(runtime->stack) + kRuntimeStackBytes);
    const bool ripInApp = runtime && address_in_range(frame->rip,
        reinterpret_cast<uintptr_t>(runtime->image),
        reinterpret_cast<uintptr_t>(runtime->image) + runtime->imageBytes);

    serial::puts("[NATIVE-ELF-FAULT] exception="); serial::puts(native_exception_name(frame->vector));
    serial::puts(" vector=0x"); serial::put_hex64(frame->vector);
    serial::puts(" name="); serial::puts(native_exception_name(frame->vector));
    serial::puts(" error=0x"); serial::put_hex64(frame->errorCode);
    serial::puts(" RIP=0x"); serial::put_hex64(frame->rip);
    serial::puts(" RSP=0x"); serial::put_hex64(interruptedRsp);
    serial::puts(" RBP=0x"); serial::put_hex64(frame->rbp);
    serial::puts(" RFLAGS=0x"); serial::put_hex64(frame->rflags);
    serial::puts(" CS=0x"); serial::put_hex64(frame->cs);
    serial::puts(" SS=0x"); serial::put_hex64(ss);
    serial::puts(" CR2=0x"); serial::put_hex64(cr2);
    serial::puts(" CR3=0x"); serial::put_hex64(cr3);
    serial::putc('\n');
    serial::puts("[KERNEL-EXCEPTION] vector=0x"); serial::put_hex64(frame->vector);
    serial::puts(" name="); serial::puts(native_exception_name(frame->vector));
    serial::puts(" error=0x"); serial::put_hex64(frame->errorCode);
    serial::puts(" RIP=0x"); serial::put_hex64(frame->rip);
    serial::puts(" RSP=0x"); serial::put_hex64(interruptedRsp);
    serial::puts(" RBP=0x"); serial::put_hex64(frame->rbp);
    serial::puts(" CR2=0x"); serial::put_hex64(cr2);
    serial::puts(" operation="); serial::puts(runtime
        ? abi_operation_name(runtime->currentAbiOperation) : "none");
    serial::putc('\n');
    file_clipboard::trace_exception_context();
    serial::puts("[NATIVE-ELF-FAULT] RAX=0x"); serial::put_hex64(frame->rax);
    serial::puts(" RBX=0x"); serial::put_hex64(frame->rbx);
    serial::puts(" RCX=0x"); serial::put_hex64(frame->rcx);
    serial::puts(" RDX=0x"); serial::put_hex64(frame->rdx);
    serial::puts(" RSI=0x"); serial::put_hex64(frame->rsi);
    serial::puts(" RDI=0x"); serial::put_hex64(frame->rdi);
    serial::puts(" R8=0x"); serial::put_hex64(frame->r8);
    serial::puts(" R9=0x"); serial::put_hex64(frame->r9);
    serial::puts(" R10=0x"); serial::put_hex64(frame->r10);
    serial::puts(" R11=0x"); serial::put_hex64(frame->r11);
    serial::puts(" R12=0x"); serial::put_hex64(frame->r12);
    serial::puts(" R13=0x"); serial::put_hex64(frame->r13);
    serial::puts(" R14=0x"); serial::put_hex64(frame->r14);
    serial::puts(" R15=0x"); serial::put_hex64(frame->r15);
    serial::putc('\n');

    serial::puts("[NATIVE-ELF-FAULT] region=");
    serial::puts(exception_code_region(runtime, frame->rip));
    serial::puts(" symbol=unavailable appRip="); serial::puts(ripInApp ? "1" : "0");
    serial::puts(" stackRip="); serial::puts(stackInRuntime ? "1" : "0");
    serial::putc('\n');

    if (runtime) {
        runtime->faulted = true;
        runtime->faultVector = frame->vector;
        runtime->faultErrorCode = frame->errorCode;
        runtime->faultRip = frame->rip;
        runtime->faultRsp = interruptedRsp;
        runtime->faultRbp = frame->rbp;
        runtime->faultCr2 = cr2;
        serial::puts("[NATIVE-ELF-FAULT] app="); serial::puts(runtime->package ? runtime->package->displayName : "(none)");
        serial::puts(" runtime=0x"); serial::put_hex64(s_runtimeSequence);
        serial::puts(" window=0x"); serial::put_hex64(runtime->owner && runtime->owner->window() ?
            runtime->owner->window()->id : 0);
        serial::puts(" currentAbi="); serial::puts(abi_operation_name(runtime->currentAbiOperation));
        serial::puts(" lastAbi="); serial::puts(abi_operation_name(runtime->lastCompletedAbiOperation));
        serial::puts(" abiCalls=0x"); serial::put_hex64(runtime->abiCallCount);
        serial::puts(" appFrames=0x"); serial::put_hex64(runtime->appFrameSequence);
        serial::puts(" stackRange=0x"); serial::put_hex64(reinterpret_cast<uintptr_t>(runtime->stack));
        serial::puts("..0x"); serial::put_hex64(reinterpret_cast<uintptr_t>(runtime->stack) + kRuntimeStackBytes);
        serial::puts(" elfRange=0x"); serial::put_hex64(reinterpret_cast<uintptr_t>(runtime->image));
        serial::puts("..0x"); serial::put_hex64(reinterpret_cast<uintptr_t>(runtime->image) + runtime->imageBytes);
        serial::puts(" loadVaddr=0x"); serial::put_hex64(runtime->loadMinimum);
        serial::puts("..0x"); serial::put_hex64(runtime->loadMaximum);
        serial::puts(" execVaddr=0x"); serial::put_hex64(runtime->executableMinimum);
        serial::puts("..0x"); serial::put_hex64(runtime->executableMaximum);
        serial::puts(" frameGuardFailures=0x"); serial::put_hex32(runtime->frameGuardFailures);
        serial::putc('\n');
    }

    if (stackInRuntime) {
        serial::puts("[NATIVE-ELF-FAULT] stackDump");
        const uint64_t* words = reinterpret_cast<const uint64_t*>(interruptedRsp);
        for (uint32_t i = 0; i < 16u && address_in_range(
            reinterpret_cast<uintptr_t>(words + i),
            reinterpret_cast<uintptr_t>(runtime->stack),
            reinterpret_cast<uintptr_t>(runtime->stack) + kRuntimeStackBytes); ++i) {
            serial::puts(" ["); serial::put_hex32(i); serial::puts("]=0x"); serial::put_hex64(words[i]);
        }
        serial::putc('\n');
    }

    // Recover only a fault that occurred while an active Native ELF owns the
    // app stack.  A fault with no active runtime remains a kernel fault and is
    // deliberately halted rather than attempting unsafe global recovery.
    if (runtime && (ripInApp || stackInRuntime)) {
        serial::puts("[NATIVE-ELF-FAULT] containment=runtime-faulted recovery=return-to-desktop\n");
        return 1;
    }
    serial::puts("[KERNEL-FAULT] panic=unhandled-exception containment=kernel-fault halted=1\n");
    for (;;) arch::halt();
}

static void initialize_host_table(Runtime* runtime) {
    uint8_t* bytes = reinterpret_cast<uint8_t*>(&runtime->host);
    for (uint32_t i = 0; i < sizeof(runtime->host); ++i) bytes[i] = 0;
    runtime->host.size = sizeof(runtime->host);
    runtime->host.version = GX_API_VERSION;
    runtime->host.log = host_log;
    runtime->host.get_api_version = host_get_api_version;
    runtime->host.request_window = host_request_window;
    runtime->host.draw_text = host_draw_text;
    runtime->host.draw_rect = host_draw_rect;
    runtime->host.wait_for_close = host_wait_for_close;
    runtime->host.poll_event = host_poll_event;
    runtime->host.exit = host_exit;
    runtime->host.file_read_all = host_file_read_all;
    runtime->host.file_exists = host_file_exists;
    runtime->host.request_window_ex = host_request_window_ex;
    runtime->host.file_read = host_file_read;
    runtime->host.present_frame = host_present_frame;
    runtime->host.get_ticks_ms = host_get_ticks_ms;
}

static bool run_package(Package* package) {
    Runtime runtime{};
    runtime.package = package;
    runtime.exitCode = GX_OK;
    ++s_runtimeSequence;
    initialize_host_table(&runtime);
    runtime.context.size = sizeof(runtime.context);
    runtime.context.apiVersion = GX_API_VERSION;
    runtime.context.host = &runtime.host;
    runtime.context.userData = &runtime;
    log_breadcrumb(&runtime, "03 RUNTIME_CREATED");
    log_runtime(&runtime, "launch begin path=package-relative runtime=native-elf abi=guidexos-c-abi-v1");
    if (!load_image(&runtime)) {
        delete[] runtime.image;
        runtime.image = nullptr;
        return false;
    }
    runtime.stack = new uint8_t[kRuntimeStackBytes];
    if (!runtime.stack) {
        delete[] runtime.image;
        runtime.image = nullptr;
        log_runtime(&runtime, "launch FAIL=app-stack-allocation");
        return false;
    }
    serial::puts("[NATIVE-ELF] runtime="); serial::put_hex64(s_runtimeSequence);
    serial::puts(" stack allocated bytes=0x"); serial::put_hex32(kRuntimeStackBytes);
    serial::puts(" range=0x"); serial::put_hex64(reinterpret_cast<uintptr_t>(runtime.stack));
    serial::puts("..0x"); serial::put_hex64(reinterpret_cast<uintptr_t>(runtime.stack + kRuntimeStackBytes));
    serial::putc('\n');

    s_activeRuntime = &runtime;
    log_breadcrumb(&runtime, "04 GX_MAIN_ENTER");
    gx_result result = gxos_native_call_on_stack(runtime.entry, &runtime.context,
        runtime.stack + kRuntimeStackBytes);
    log_breadcrumb(&runtime, "21 GX_MAIN_RETURN");
    serial::puts("[NATIVE-ELF] runtime="); serial::put_hex64(s_runtimeSequence);
    serial::puts(" exit result=0x"); serial::put_hex32(static_cast<uint32_t>(result));
    serial::puts(" requested="); serial::puts(runtime.exitRequested ? "1" : "0");
    serial::puts(" fault="); serial::puts(runtime.faulted ? "native-exception" : "none");
    if (runtime.faulted) {
        serial::puts(" vector=0x"); serial::put_hex64(runtime.faultVector);
        serial::puts(" error=0x"); serial::put_hex64(runtime.faultErrorCode);
        serial::puts(" RIP=0x"); serial::put_hex64(runtime.faultRip);
        serial::puts(" RSP=0x"); serial::put_hex64(runtime.faultRsp);
        serial::puts(" RBP=0x"); serial::put_hex64(runtime.faultRbp);
        serial::puts(" CR2=0x"); serial::put_hex64(runtime.faultCr2);
    }
    serial::puts(" currentAbi="); serial::puts(abi_operation_name(runtime.currentAbiOperation));
    serial::puts(" lastAbi="); serial::puts(abi_operation_name(runtime.lastCompletedAbiOperation));
    serial::puts(" abiCalls=0x"); serial::put_hex64(runtime.abiCallCount);
    serial::puts(" frames=0x"); serial::put_hex64(runtime.appFrameSequence);
    serial::putc('\n');

    log_breadcrumb(&runtime, "22 RUNTIME_CLEANUP");
    if (runtime.owner) {
        abi_begin(&runtime, NativeAbiOperation::CloseWindow);
        if (runtime.owner->window()) runtime.owner->requestClose();
        delete runtime.owner;
        runtime.owner = nullptr;
        abi_complete(&runtime, NativeAbiOperation::CloseWindow);
    }
    delete[] runtime.stack;
    delete[] runtime.image;
    runtime.stack = nullptr;
    runtime.image = nullptr;
    s_activeRuntime = nullptr;
    gxos_native_set_fault_recovery(0, 0);
    log_runtime(&runtime, result == GX_OK ? "lifecycle PASS window/resource cleanup complete" : "lifecycle FAIL app-returned-error");
    return result == GX_OK;
}

bool launch(const char* appName) {
    if (s_launchInProgress) return false;
    if (!s_discovered) discover();
    Package* package = find_package(appName);
    if (!package) return false;
    s_launchInProgress = true;
    const bool result = run_package(package);
    s_launchInProgress = false;
    return result;
}

#else

void discover() {}
bool launch(const char*) { return false; }
bool is_available(const char*) { return false; }
const PackageInfo* lookup_package(const char*) { return nullptr; }
uint32_t package_count() { return 0; }
const PackageInfo* package_at(uint32_t) { return nullptr; }

#endif

} // namespace native_elf
} // namespace kernel
