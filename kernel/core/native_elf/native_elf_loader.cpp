//
// Bare-metal NativeElf loader and diagnostic execution route.
//

#include "native_elf_loader.h"

#include "native_elf_executor.h"
#include "native_elf_scheduler.h"
#include "../compiler/compiler_build_service.h"
#include "native_elf_run_service.h"
#include "arch/amd64.h"
#include "kernel/desktop_font.h"
#include "kernel/desktop.h"
#include "kernel/framebuffer.h"
#include "kernel/input_manager.h"
#include "kernel/kernel_app.h"
#include "kernel/kernel_compositor.h"
#include "kernel/pit.h"
#include "kernel/ps2keyboard.h"
#include "kernel/serial_debug.h"
#include "kernel/vfs.h"

namespace kernel {
namespace native_elf {
namespace {

static const uint64_t PTE_PRESENT = 1ULL << 0;
static const uint64_t PTE_WRITABLE = 1ULL << 1;
static const uint64_t PTE_LARGE = 1ULL << 7;
static const uint64_t PTE_NX = 1ULL << 63;
static const uint64_t PTE_ADDRESS_MASK = 0x000FFFFFFFFFF000ULL;

static NativeElfExecutionContext s_context = {};
static bool s_contextConfigured = false;
static bool s_nxEnabled = false;
static uint8_t s_file[NATIVE_APP_MAX_ELF_FILE_BYTES];
static NativeAppExecutionContext s_appRuntime = {};
static char s_bareBuildStrings[8][768] = {};
static char s_bareRunStrings[9][768] = {};
static const uint64_t NESTED_APPLICATION_STACK_BASE =
    APPLICATION_STACK_BASE - APPLICATION_STACK_SIZE;
static const uint64_t NESTED_SERVICE_STACK_BASE =
    NESTED_APPLICATION_STACK_BASE - APPLICATION_STACK_SIZE;
static bool s_nestedExecutionActive = false;
static uint8_t s_parentImage[guidexos::native_elf::MAX_MAPPED_BYTES] = {};
static uint64_t s_parentImageSize = 0;
static uint64_t s_parentImagePtes[guidexos::native_elf::MAX_MAPPED_BYTES /
                                  guidexos::native_elf::PAGE_SIZE] = {};
static uint32_t s_parentImagePteCount = 0;
static NativeAppExecutionContext s_parentRuntime = {};
static uint64_t s_developmentGeneration = 0;
static char s_developmentApplicationId[GX_DEVELOPMENT_RUN_MAX_APP_ID_BYTES] = {};

static bool s_guiApplicationCreated = false;
static bool s_guiWindowCreated = false;
static bool s_guiRendered = false;
static bool s_guiClosedNormally = false;
static bool s_guiCloseLifecycleRequested = false;
static bool s_guiForcedCleanup = false;
static bool s_guiAutomationClose = false;
static bool s_guiRenderAnnounced = false;
static uint32_t s_guiWindowId = 0;
static uint32_t s_guiRenderCount = 0;
static uint32_t s_guiPumpCount = 0;
static uint64_t s_guiGeneration = 0;
static uint64_t s_guiContentHash = 0;
static uint64_t s_guiLastDestroyedWindow = 0;
static char s_guiContent[256] = {};

static uint64_t fnv1a_text(const char* text)
{
    uint64_t hash = 1469598103934665603ULL;
    if (!text) return hash;
    for (uint32_t i = 0; i < 255 && text[i] != '\0'; ++i) {
        hash ^= static_cast<uint8_t>(text[i]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

static void copy_bounded_text(char* destination, uint32_t capacity, const char* source)
{
    if (!destination || capacity == 0) return;
    uint32_t i = 0;
    if (source) while (i + 1 < capacity && source[i] != '\0') {
        destination[i] = source[i];
        ++i;
    }
    destination[i] = '\0';
}

class NativeElfGuiApplication final : public app::KernelApp {
public:
    NativeElfGuiApplication() : m_label(-1) {
        copy_bounded_text(m_name, sizeof(m_name),
                          s_developmentApplicationId[0] ? s_developmentApplicationId : "NativeElf Development App");
    }

    bool init() override { return false; }

    bool initialize(int width, int height, const char* title)
    {
        if (!createWindow(width, height, title)) return false;
        m_label = addLabel(16, 30, width - 32, 32, "");
        if (m_label < 0) {
            requestClose();
            return false;
        }
        return true;
    }

    void shutdown() override
    {
        if (s_guiCloseLifecycleRequested) s_guiClosedNormally = true;
    }

    void onWindowClose() override
    {
        // The close request reached the application-owned lifecycle before
        // KernelApp releases its compositor window.
        if (!s_guiForcedCleanup) s_guiCloseLifecycleRequested = true;
    }

    void draw(uint32_t, uint32_t, uint32_t, uint32_t) override
    {
        if (s_guiRenderCount != 0xFFFFFFFFU) ++s_guiRenderCount;
        s_guiRendered = true;
        s_guiContentHash = fnv1a_text(m_content);
        if (!s_guiRenderAnnounced) {
            serial::puts("NativeElf: compositor_render window=");
            serial::put_hex32(s_guiWindowId);
            serial::puts(" text=");
            serial::puts(m_content);
            serial::puts(" hash=fnv1a64:");
            serial::put_hex64(s_guiContentHash);
            serial::putc('\n');
            s_guiRenderAnnounced = true;
        }
    }

    bool setContent(const char* text)
    {
        if (!text || m_label < 0) return false;
        copy_bounded_text(m_content, sizeof(m_content), text);
        copy_bounded_text(s_guiContent, sizeof(s_guiContent), m_content);
        setWidgetText(m_label, m_content);
        s_guiContentHash = fnv1a_text(m_content);
        return true;
    }

private:
    int m_label;
    char m_content[256] = {};
};

static NativeElfGuiApplication* s_guiApplication = nullptr;

static void reset_gui_runtime()
{
    s_guiApplicationCreated = false;
    s_guiWindowCreated = false;
    s_guiRendered = false;
    s_guiClosedNormally = false;
    s_guiCloseLifecycleRequested = false;
    s_guiForcedCleanup = false;
    s_guiRenderAnnounced = false;
    s_guiWindowId = 0;
    s_guiRenderCount = 0;
    s_guiPumpCount = 0;
    s_guiGeneration = 0;
    s_guiContentHash = fnv1a_text("");
    s_guiLastDestroyedWindow = 0;
    s_guiContent[0] = '\0';
}

static void destroy_native_gui()
{
    if (s_guiApplication) {
        if (s_guiApplication->getWindow()) {
            s_guiCloseLifecycleRequested = false;
            s_guiForcedCleanup = true;
            s_guiApplication->requestClose();
            s_guiForcedCleanup = false;
        }
        delete s_guiApplication;
        s_guiApplication = nullptr;
    }
    if (s_guiWindowId != 0) s_guiLastDestroyedWindow = s_guiWindowId;
    s_guiWindowCreated = false;
}

static void copy_gui_proof(NativeElfRunReport* report)
{
    if (!report) return;
    report->guiApplicationCreated = s_guiApplicationCreated;
    report->guiWindowCreated = s_guiWindowCreated || s_guiWindowId != 0;
    report->guiRendered = s_guiRendered;
    report->guiClosedNormally = s_guiClosedNormally;
    report->guiWindowId = s_guiWindowId;
    report->guiRenderCount = s_guiRenderCount;
    report->guiPumpCount = s_guiPumpCount;
    report->guiGeneration = s_guiGeneration;
    report->guiContentHash = s_guiContentHash;
    copy_bounded_text(report->guiContent, sizeof(report->guiContent), s_guiContent);
}

struct NestedInvocation {
    const char* path;
    int32_t* returnValue;
    NativeElfRunReport* report;
    bool success;
};

static NestedInvocation s_nestedInvocation = {};

static uint64_t read_stack_pointer()
{
#if defined(__GNUC__) || defined(__clang__)
    uint64_t value = 0;
    asm volatile ("mov %%rsp, %0" : "=r"(value));
    return value;
#else
    return 0;
#endif
}

static gx_result GX_CALL host_log(gx_app_context* context, const char* message)
{
    if (s_appRuntime.state != NativeAppExecutionState::Running ||
        !context || context != &s_appRuntime.appContext ||
        context->host != &s_appRuntime.hostCalls ||
        context->userData != &s_appRuntime) {
        return GX_ERROR_PERMISSION_DENIED;
    }

    uint32_t maximumReadableBytes = 0;
    if (!native_app_log_pointer_range(reinterpret_cast<uint64_t>(message),
                                      s_appRuntime.readOnlyDataBase,
                                      s_appRuntime.readOnlyDataSize,
                                      &maximumReadableBytes)) {
        return GX_ERROR_INVALID_ARGUMENT;
    }

    uint32_t length = 0;
    while (length < maximumReadableBytes && message[length] != '\0') ++length;
    if (length == maximumReadableBytes) return GX_ERROR_INVALID_ARGUMENT;

    serial::puts("NativeElf host log: ");
    serial::puts(message);
    serial::putc('\n');
    s_appRuntime.hostLogObserved = true;
    s_appRuntime.hostLogBytes = length;
    if (s_appRuntime.hostLogCount < NATIVE_APP_MAX_LOG_LINES) {
        for (uint32_t i = 0; i <= length && i < NATIVE_APP_MAX_LOG_LINE_BYTES; ++i)
            s_appRuntime.hostLog[s_appRuntime.hostLogCount][i] = message[i];
        ++s_appRuntime.hostLogCount;
    } else {
        s_appRuntime.hostLogTruncated = true;
    }
    return GX_OK;
}

static uint32_t GX_CALL host_get_api_version(gx_app_context* context)
{
    if (s_appRuntime.state != NativeAppExecutionState::Running ||
        !context || context != &s_appRuntime.appContext ||
        context->host != &s_appRuntime.hostCalls ||
        context->userData != &s_appRuntime) return 0;
    return GX_API_VERSION;
}

static bool app_pointer_range(const void* pointer, uint64_t bytes)
{
    const uint64_t address = reinterpret_cast<uint64_t>(pointer);
    if (address == 0 || bytes == 0 || address > ~static_cast<uint64_t>(0) - bytes) return false;
    return native_app_pointer_in_range(address, s_appRuntime.imageBase, s_appRuntime.imageSize) ||
           native_app_pointer_in_range(address, s_appRuntime.stackBase, s_appRuntime.stackSize);
}

static bool app_string(const char* pointer, char* output, uint32_t capacity)
{
    if (!pointer || !output || capacity < 2 || !app_pointer_range(pointer, 1)) return false;
    for (uint32_t i = 0; i + 1 < capacity; ++i) {
        if (!app_pointer_range(pointer + i, 1)) return false;
        output[i] = pointer[i];
        if (output[i] == '\0') return true;
    }
    output[0] = '\0';
    return false;
}

static bool app_context_valid(gx_app_context* context)
{
    return s_appRuntime.state == NativeAppExecutionState::Running && context &&
        context == &s_appRuntime.appContext && context->host == &s_appRuntime.hostCalls &&
        context->userData == &s_appRuntime;
}

static gx_result GX_CALL host_bare_file_stat(gx_app_context* context, const char* path, gx_file_info* output)
{
    if (!app_context_valid(context) || !output || !app_pointer_range(output, sizeof(*output))) return GX_ERROR_PERMISSION_DENIED;
    char localPath[vfs::VFS_MAX_PATH] = {};
    if (!app_string(path, localPath, sizeof(localPath))) return GX_ERROR_INVALID_ARGUMENT;
    vfs::FileInfo info = {};
    if (vfs::stat(localPath, &info) != vfs::VFS_OK) return GX_ERROR_FAILED;
    output->type = info.type == vfs::FILE_TYPE_DIRECTORY ? GX_FILE_TYPE_DIRECTORY :
        (info.type == vfs::FILE_TYPE_REGULAR ? GX_FILE_TYPE_REGULAR : GX_FILE_TYPE_UNKNOWN);
    output->reserved = 0;
    output->size = info.size;
    return GX_OK;
}

static gx_result GX_CALL host_bare_file_read_workspace(gx_app_context* context, const char* path,
                                                         void* buffer, uint32_t bufferSize, uint32_t* outBytes)
{
    if (!app_context_valid(context) || !outBytes || !app_pointer_range(outBytes, sizeof(*outBytes)) ||
        (bufferSize != 0 && (!buffer || !app_pointer_range(buffer, bufferSize)))) return GX_ERROR_PERMISSION_DENIED;
    *outBytes = 0;
    char localPath[vfs::VFS_MAX_PATH] = {};
    if (!app_string(path, localPath, sizeof(localPath))) return GX_ERROR_INVALID_ARGUMENT;
    const int32_t bytes = bufferSize == 0 ? 0 : vfs::read_file(localPath, buffer, bufferSize);
    if (bytes < 0) return GX_ERROR_FAILED;
    *outBytes = static_cast<uint32_t>(bytes);
    return GX_OK;
}

static gx_result GX_CALL host_bare_file_list(gx_app_context* context, const char* path,
                                               gx_file_entry* output, uint32_t capacity,
                                               uint32_t* outCount, uint32_t* outTruncated)
{
    if (capacity > 128) capacity = 128;
    const uint64_t outputBytes = static_cast<uint64_t>(capacity) * sizeof(*output);
    if (!app_context_valid(context) || !outCount || !app_pointer_range(outCount, sizeof(*outCount)) ||
        (outTruncated && !app_pointer_range(outTruncated, sizeof(*outTruncated))) ||
        (capacity != 0 && (!output || !app_pointer_range(output, outputBytes)))) return GX_ERROR_PERMISSION_DENIED;
    *outCount = 0;
    if (outTruncated) *outTruncated = 0;
    char localPath[vfs::VFS_MAX_PATH] = {};
    if (!app_string(path, localPath, sizeof(localPath))) return GX_ERROR_INVALID_ARGUMENT;
    const uint8_t iterator = vfs::opendir(localPath);
    if (iterator == 0xFF) return GX_ERROR_FAILED;
    vfs::DirEntry entry = {};
    while (vfs::readdir(iterator, &entry)) {
        if (*outCount < capacity) {
            gx_file_entry& destination = output[*outCount];
            destination = {};
            destination.type = entry.type == vfs::FILE_TYPE_DIRECTORY ? GX_FILE_TYPE_DIRECTORY :
                (entry.type == vfs::FILE_TYPE_REGULAR ? GX_FILE_TYPE_REGULAR : GX_FILE_TYPE_UNKNOWN);
            destination.size = entry.size;
            uint32_t i = 0;
            for (; i + 1 < sizeof(destination.name) && entry.name[i] != '\0'; ++i) destination.name[i] = entry.name[i];
            destination.name[i] = '\0';
            ++(*outCount);
        } else if (outTruncated) *outTruncated = 1;
    }
    vfs::closedir(iterator);
    return GX_OK;
}

static gx_result GX_CALL host_bare_file_write_all(gx_app_context* context, const char* path,
                                                   const void* buffer, uint32_t bufferSize, uint32_t* outBytes)
{
    if (!app_context_valid(context) || !outBytes || !app_pointer_range(outBytes, sizeof(*outBytes)) ||
        (bufferSize != 0 && (!buffer || !app_pointer_range(buffer, bufferSize)))) return GX_ERROR_PERMISSION_DENIED;
    *outBytes = 0;
    char localPath[vfs::VFS_MAX_PATH] = {};
    if (!app_string(path, localPath, sizeof(localPath))) return GX_ERROR_INVALID_ARGUMENT;
    const int32_t bytes = vfs::write_file(localPath, buffer, bufferSize);
    if (bytes < 0) return GX_ERROR_FAILED;
    *outBytes = static_cast<uint32_t>(bytes);
    return GX_OK;
}

static gx_result GX_CALL host_bare_file_create_directory(gx_app_context* context, const char* path)
{
    if (!app_context_valid(context)) return GX_ERROR_PERMISSION_DENIED;
    char localPath[vfs::VFS_MAX_PATH] = {};
    if (!app_string(path, localPath, sizeof(localPath))) return GX_ERROR_INVALID_ARGUMENT;
    return vfs::mkdir(localPath) == vfs::VFS_OK ? GX_OK : GX_ERROR_FAILED;
}

static gx_result GX_CALL host_bare_file_remove(gx_app_context* context, const char* path)
{
    if (!app_context_valid(context)) return GX_ERROR_PERMISSION_DENIED;
    char localPath[vfs::VFS_MAX_PATH] = {};
    if (!app_string(path, localPath, sizeof(localPath))) return GX_ERROR_INVALID_ARGUMENT;
    vfs::FileInfo info = {};
    if (vfs::stat(localPath, &info) != vfs::VFS_OK) return GX_ERROR_FAILED;
    const vfs::Status result = info.type == vfs::FILE_TYPE_DIRECTORY ? vfs::rmdir(localPath) : vfs::unlink(localPath);
    return result == vfs::VFS_OK ? GX_OK : GX_ERROR_FAILED;
}

static bool native_window_valid(gx_app_context* context, gx_handle window)
{
    return app_context_valid(context) && s_guiApplication &&
        s_guiApplication->getWindow() && s_guiWindowCreated &&
        window == static_cast<gx_handle>(s_guiWindowId) &&
        s_guiApplication->getState() == app::AppState::Running;
}

static gx_result GX_CALL host_bare_request_window(gx_app_context* context, const char*, int width, int height, gx_handle* output)
{
    if (!app_context_valid(context) || !output || !app_pointer_range(output, sizeof(*output)) || width <= 0 || height <= 0) return GX_ERROR_INVALID_ARGUMENT;
    *output = 1;
    return framebuffer::is_available() ? GX_OK : GX_ERROR_UNSUPPORTED;
}

static gx_result GX_CALL host_bare_request_window_ex(gx_app_context* context, const char* title, int width, int height, uint32_t, gx_handle* output)
{
    return host_bare_request_window(context, title, width, height, output);
}

static gx_result GX_CALL host_bare_draw_text(gx_app_context* context, gx_handle window, int x, int y, const char* text)
{
    if (!native_window_valid(context, window) || !text) return GX_ERROR_INVALID_ARGUMENT;
    char localText[256] = {};
    if (!app_string(text, localText, sizeof(localText))) return GX_ERROR_INVALID_ARGUMENT;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    desktop::draw_text(static_cast<uint32_t>(x), static_cast<uint32_t>(y), localText, 0x00F0F5FFu, 1);
    return GX_OK;
}

static gx_result GX_CALL host_bare_draw_rect(gx_app_context* context, gx_handle window, int x, int y, int width, int height, uint32_t color)
{
    if (!native_window_valid(context, window) || width <= 0 || height <= 0) return GX_ERROR_INVALID_ARGUMENT;
    if (x < 0) { width += x; x = 0; } if (y < 0) { height += y; y = 0; }
    if (width <= 0 || height <= 0) return GX_OK;
    framebuffer::fill_rect(static_cast<uint32_t>(x), static_cast<uint32_t>(y), static_cast<uint32_t>(width), static_cast<uint32_t>(height), color);
    return GX_OK;
}

static gx_result GX_CALL host_bare_present_frame(gx_app_context* context, gx_handle window, int, int, int, int, uint32_t, uint32_t, const void*, uint32_t)
{
    if (!native_window_valid(context, window)) return GX_ERROR_INVALID_ARGUMENT;
    framebuffer::present();
    return GX_OK;
}

static gx_result GX_CALL host_bare_poll_event(gx_app_context* context, gx_event* output, int)
{
    if (!app_context_valid(context) || !output || !app_pointer_range(output, sizeof(*output))) return GX_ERROR_INVALID_ARGUMENT;
    *output = {};
    output->size = sizeof(*output);
    output->window = 1;
    input::poll();
    if (ps2keyboard::has_key()) {
        output->type = GX_EVENT_KEY;
        output->param1 = static_cast<int>(ps2keyboard::get_key());
        output->param2 = GX_KEY_ACTION_DOWN;
        output->param3 = (ps2keyboard::is_shift_down() ? GX_KEY_MOD_SHIFT : 0) |
                         (ps2keyboard::is_ctrl_down() ? GX_KEY_MOD_CTRL : 0) |
                         (ps2keyboard::is_alt_down() ? GX_KEY_MOD_ALT : 0);
        return GX_OK;
    }
    if (input::mouse_dirty()) {
        output->type = GX_EVENT_MOUSE;
        output->param1 = input::mouse_x();
        output->param2 = input::mouse_y();
        output->param3 = GX_MOUSE_PACK(input::mouse_buttons() ? GX_MOUSE_BUTTON_LEFT : GX_MOUSE_BUTTON_NONE, GX_MOUSE_ACTION_MOVE);
        input::mouse_clear_dirty();
        return GX_OK;
    }
    framebuffer::present();
    return GX_ERROR_TIMEOUT;
}

static gx_result GX_CALL host_bare_wait_for_close(gx_app_context* context, gx_handle window, int)
{
    return native_window_valid(context, window) ? GX_ERROR_TIMEOUT : GX_ERROR_INVALID_ARGUMENT;
}

static gx_result GX_CALL host_bare_exit(gx_app_context* context, gx_result exitCode)
{
    return app_context_valid(context) ? exitCode : GX_ERROR_PERMISSION_DENIED;
}

static uint64_t GX_CALL host_bare_get_ticks_ms(gx_app_context* context)
{
    return app_context_valid(context) ? pit::ticks() * 10ULL : 0;
}

static void put_decimal_u64(uint64_t value);

static gx_handle GX_CALL host_native_window_create(gx_app_context* context,
                                                     int width,
                                                     int height,
                                                     const char* title)
{
    if (!app_context_valid(context) || s_guiApplication || width < 200 || height < 120 ||
        width > 1024 || height > 768 || !title) return 0;

    char localTitle[64] = {};
    if (!app_string(title, localTitle, sizeof(localTitle))) return 0;
    reset_gui_runtime();
    s_guiApplication = new NativeElfGuiApplication();
    if (!s_guiApplication || !s_guiApplication->initialize(width, height, localTitle)) {
        delete s_guiApplication;
        s_guiApplication = nullptr;
        return 0;
    }
    s_guiApplicationCreated = true;
    s_guiWindowCreated = true;
    s_guiWindowId = s_guiApplication->getWindow()->id;
    s_guiGeneration = s_developmentGeneration;
    serial::puts("NativeElf: app_create generation=");
    serial::put_hex64(s_guiGeneration);
    serial::puts(" id=");
    serial::puts(s_developmentApplicationId);
    serial::putc('\n');
    serial::puts("NativeElf: window_create id=");
    serial::put_hex32(s_guiWindowId);
    serial::puts(" width=");
    put_decimal_u64(static_cast<uint32_t>(width));
    serial::puts(" height=");
    put_decimal_u64(static_cast<uint32_t>(height));
    serial::putc('\n');
    return static_cast<gx_handle>(s_guiWindowId);
}

static gx_result GX_CALL host_native_window_set_text(gx_app_context* context,
                                                      gx_handle window,
                                                      const char* text)
{
    if (!native_window_valid(context, window) || !text) return GX_ERROR_INVALID_ARGUMENT;
    char localText[256] = {};
    if (!app_string(text, localText, sizeof(localText))) return GX_ERROR_INVALID_ARGUMENT;
    return s_guiApplication->setContent(localText) ? GX_OK : GX_ERROR_FAILED;
}

static gx_result GX_CALL host_native_window_destroy(gx_app_context* context,
                                                     gx_handle window)
{
    if (!app_context_valid(context)) return GX_ERROR_PERMISSION_DENIED;
    if (!s_guiWindowCreated && window == static_cast<gx_handle>(s_guiLastDestroyedWindow)) return GX_OK;
    if (!native_window_valid(context, window)) return GX_ERROR_INVALID_ARGUMENT;
    destroy_native_gui();
    return GX_OK;
}

static gx_result GX_CALL host_native_window_run(gx_app_context* context,
                                                 gx_handle window)
{
    if (!native_window_valid(context, window)) return GX_ERROR_INVALID_ARGUMENT;

    // This is the same desktop/compositor path used by the kernel's main
    // loop.  An asynchronous Run owner suspends at each bounded pump; the
    // legacy direct-loader route retains its synchronous lifetime.
    desktop::draw();
    while (native_window_valid(context, window)) {
        desktop::cooperative_yield();
        desktop::tick();
        if (s_guiPumpCount != 0xFFFFFFFFU) ++s_guiPumpCount;

        if (s_guiAutomationClose && s_guiRendered && s_guiPumpCount >= 2) {
            s_guiCloseLifecycleRequested = true;
            serial::puts("NativeElf: close_request id=");
            serial::put_hex32(s_guiWindowId);
            serial::putc('\n');
            if (!compositor::KernelCompositor::requestCloseWindow(s_guiWindowId)) {
                return GX_ERROR_FAILED;
            }
            // KernelApp has completed the normal close callback and released
            // the compositor window. Keep the generation's handle tombstone
            // so a repeated destroy is an idempotent, bounded operation.
            s_guiLastDestroyedWindow = s_guiWindowId;
            s_guiWindowCreated = false;
        }
        if (!native_window_valid(context, window)) break;
        // A service-owned asynchronous run must return the CPU to its owner
        // after each bounded pump.  The legacy direct-loader route has no
        // owner, so it retains the interrupt-driven halt behavior.
        if (!NativeElfRunService::native_elf_scheduler_yield()) arch::halt();
    }
    serial::puts(s_guiClosedNormally ? "NativeElf: close_complete normal\n"
                                     : "NativeElf: close_complete abnormal\n");
    return s_guiClosedNormally ? GX_OK : GX_ERROR_FAILED;
}

static gx_result GX_CALL host_bare_build_start(gx_app_context* context, const gx_build_request* request, gx_build_handle* output)
{
    if (!app_context_valid(context) || !request || !output || !app_pointer_range(request, sizeof(*request)) || !app_pointer_range(output, sizeof(*output))) return GX_ERROR_PERMISSION_DENIED;
    const char* input[8] = { request->projectRoot, request->projectId, request->projectKind, request->targetProfile,
                             request->buildSystem, request->buildScript, request->expectedArtifact, request->configuration };
    for (uint32_t i = 0; i < 8; ++i) if (!app_string(input[i], s_bareBuildStrings[i], sizeof(s_bareBuildStrings[i]))) return GX_ERROR_INVALID_ARGUMENT;
    gx_build_request copied = *request;
    copied.projectRoot = s_bareBuildStrings[0];
    copied.projectId = s_bareBuildStrings[1];
    copied.projectKind = s_bareBuildStrings[2];
    copied.targetProfile = s_bareBuildStrings[3];
    copied.buildSystem = s_bareBuildStrings[4];
    copied.buildScript = s_bareBuildStrings[5];
    copied.expectedArtifact = s_bareBuildStrings[6];
    copied.configuration = s_bareBuildStrings[7];
    return compiler::BareMetalBuildService::start(&copied, output);
}

static gx_result GX_CALL host_bare_build_poll(gx_app_context* context, gx_build_handle handle, gx_build_snapshot* output)
{
    if (!app_context_valid(context) || !output || !app_pointer_range(output, sizeof(*output))) return GX_ERROR_PERMISSION_DENIED;
    return compiler::BareMetalBuildService::poll(handle, output);
}

static gx_result GX_CALL host_bare_build_release(gx_app_context* context, gx_build_handle handle)
{
    if (!app_context_valid(context)) return GX_ERROR_PERMISSION_DENIED;
    return compiler::BareMetalBuildService::release(handle);
}

static void copy_bytes(uint8_t* destination, const uint8_t* source, uint64_t count);
static void put_decimal_u64(uint64_t value);

static bool copy_run_snapshot_to_app(const gx_development_run_snapshot& source,
                                     gx_development_run_snapshot* destination)
{
    if (!destination || !app_pointer_range(destination, sizeof(uint32_t))) return false;
    const uint32_t requested = destination->size;
    if (requested < static_cast<uint32_t>(offsetof(gx_development_run_snapshot, outputCount))) return false;
    const uint32_t bytes = requested < sizeof(source) ? requested : static_cast<uint32_t>(sizeof(source));
    if (!app_pointer_range(destination, bytes)) return false;
    copy_bytes(reinterpret_cast<uint8_t*>(destination),
               reinterpret_cast<const uint8_t*>(&source), bytes);
    return true;
}

static gx_result GX_CALL host_bare_run_prepare(
    gx_app_context* context,
    const gx_development_run_request* request,
    gx_development_run_handle* outputHandle,
    gx_development_run_snapshot* outputSnapshot)
{
    if (!app_context_valid(context) || !request || !outputHandle || !outputSnapshot ||
        !app_pointer_range(request, sizeof(*request)) ||
        !app_pointer_range(outputHandle, sizeof(*outputHandle)) ||
        !app_pointer_range(outputSnapshot, sizeof(uint32_t))) return GX_ERROR_PERMISSION_DENIED;
    if (request->size < sizeof(*request) || request->version != GX_DEVELOPMENT_RUN_API_VERSION) return GX_ERROR_INVALID_ARGUMENT;
    const char* values[9] = { request->projectRoot, request->projectId, request->projectKind,
                              request->targetProfile, request->manifestPath, request->artifactPath,
                              request->artifactSha256, request->artifactArchitecture, request->artifactAbi };
    for (uint32_t i = 0; i < 9; ++i) {
        if (!app_string(values[i], s_bareRunStrings[i], sizeof(s_bareRunStrings[i]))) return GX_ERROR_INVALID_ARGUMENT;
    }
    gx_development_run_request copied = request[0];
    copied.projectRoot = s_bareRunStrings[0];
    copied.projectId = s_bareRunStrings[1];
    copied.projectKind = s_bareRunStrings[2];
    copied.targetProfile = s_bareRunStrings[3];
    copied.manifestPath = s_bareRunStrings[4];
    copied.artifactPath = s_bareRunStrings[5];
    copied.artifactSha256 = s_bareRunStrings[6];
    copied.artifactArchitecture = s_bareRunStrings[7];
    copied.artifactAbi = s_bareRunStrings[8];

    gx_development_run_snapshot local = {};
    local.size = sizeof(local);
    local.version = GX_DEVELOPMENT_RUN_API_VERSION;
    const gx_result result = NativeElfRunService::prepare(copied, outputHandle, &local);
    if (!copy_run_snapshot_to_app(local, outputSnapshot)) return GX_ERROR_PERMISSION_DENIED;
    return result;
}

static gx_result GX_CALL host_bare_run_start(gx_app_context* context,
                                              gx_development_run_handle handle)
{
    if (!app_context_valid(context)) return GX_ERROR_PERMISSION_DENIED;
    return NativeElfRunService::start(handle);
}

static gx_result GX_CALL host_bare_run_poll(gx_app_context* context,
                                             gx_development_run_handle handle,
                                             gx_development_run_snapshot* outputSnapshot)
{
    if (!app_context_valid(context) || !outputSnapshot ||
        !app_pointer_range(outputSnapshot, sizeof(uint32_t))) return GX_ERROR_PERMISSION_DENIED;
    gx_development_run_snapshot local = {};
    local.size = sizeof(local);
    local.version = GX_DEVELOPMENT_RUN_API_VERSION;
    const gx_result result = NativeElfRunService::poll(handle, &local);
    if (!copy_run_snapshot_to_app(local, outputSnapshot)) return GX_ERROR_PERMISSION_DENIED;
    return result;
}

static gx_result GX_CALL host_bare_run_request_close(gx_app_context* context,
                                                      gx_development_run_handle handle)
{
    if (!app_context_valid(context)) return GX_ERROR_PERMISSION_DENIED;
    return NativeElfRunService::request_close(handle);
}

static gx_result GX_CALL host_bare_run_cancel(gx_app_context* context,
                                               gx_development_run_handle handle)
{
    if (!app_context_valid(context)) return GX_ERROR_PERMISSION_DENIED;
    return NativeElfRunService::cancel(handle);
}

static gx_result GX_CALL host_bare_run_release(gx_app_context* context,
                                                gx_development_run_handle handle)
{
    if (!app_context_valid(context)) return GX_ERROR_PERMISSION_DENIED;
    return NativeElfRunService::release(handle);
}

static void clear_report(NativeElfRunReport* report)
{
    if (!report) return;
    *report = {};
    // A validation failure before image/stack mapping has no runtime cleanup
    // left outstanding. Paths that acquire resources overwrite this field
    // from teardown_application before returning.
    report->teardownComplete = true;
    report->error = "NativeElf execution did not start";
}

static bool fail_report(NativeElfRunReport* report, const char* error)
{
    if (report) {
        report->success = false;
        report->error = error;
    }
    serial::puts("ELF Loader: ");
    serial::puts(error ? error : "unknown failure");
    serial::putc('\n');
    return false;
}

static bool set_page_permissions(uint64_t start,
                                 uint64_t end,
                                 bool writable,
                                 bool executable);
static void clear_bytes(uint8_t* bytes, uint64_t count);

static bool find_read_only_data(const NativeElfValidationResult& validation,
                                uint64_t* dataBase,
                                uint64_t* dataBytes)
{
    if (!dataBase || !dataBytes) return false;
    *dataBase = 0;
    *dataBytes = 0;
    for (uint32_t i = 0; i < validation.loadCount; ++i) {
        const NativeElfLoad& load = validation.loads[i];
        if ((load.flags & PF_R) != 0 && (load.flags & PF_W) == 0 &&
            (load.flags & PF_X) == 0 && load.fileSize != 0) {
            *dataBase = load.virtualAddress;
            *dataBytes = load.fileSize;
            return true;
        }
    }
    return true;
}

static bool prepare_application_stack(uint64_t stackBase, NativeElfRunReport* report)
{
    NativeAppStackLayout stack = {};
    if (!calculate_application_stack_layout(stackBase,
                                            APPLICATION_STACK_SIZE,
                                            &stack)) {
        return fail_report(report, "application stack layout is invalid");
    }
    if (!set_page_permissions(stack.base, stack.top, true, false)) {
        return fail_report(report, "application stack is not safely mapped by kernel page tables");
    }
    clear_bytes(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(stack.base)), stack.size);
    s_appRuntime.stackBase = stack.base;
    s_appRuntime.stackSize = stack.size;
    s_appRuntime.stackTop = stack.top;
    if (report) {
        report->applicationStackBase = stack.base;
        report->applicationStackTop = stack.top;
    }
    return true;
}

static void initialize_app_context()
{
    s_appRuntime.hostCalls = {};
    s_appRuntime.hostCalls.size = sizeof(gx_host_calls);
    s_appRuntime.hostCalls.version = GX_API_VERSION;
    s_appRuntime.hostCalls.log = host_log;
    s_appRuntime.hostCalls.get_api_version = host_get_api_version;
    s_appRuntime.hostCalls.request_window = host_bare_request_window;
    s_appRuntime.hostCalls.request_window_ex = host_bare_request_window_ex;
    s_appRuntime.hostCalls.draw_text = host_bare_draw_text;
    s_appRuntime.hostCalls.draw_rect = host_bare_draw_rect;
    s_appRuntime.hostCalls.wait_for_close = host_bare_wait_for_close;
    s_appRuntime.hostCalls.poll_event = host_bare_poll_event;
    s_appRuntime.hostCalls.exit = host_bare_exit;
    s_appRuntime.hostCalls.present_frame = host_bare_present_frame;
    s_appRuntime.hostCalls.get_ticks_ms = host_bare_get_ticks_ms;
    s_appRuntime.hostCalls.bare_metal_build_project_start = host_bare_build_start;
    s_appRuntime.hostCalls.bare_metal_build_project_poll = host_bare_build_poll;
    s_appRuntime.hostCalls.bare_metal_build_project_release = host_bare_build_release;
    s_appRuntime.hostCalls.bare_metal_file_stat = host_bare_file_stat;
    s_appRuntime.hostCalls.bare_metal_file_read_workspace = host_bare_file_read_workspace;
    s_appRuntime.hostCalls.bare_metal_file_list = host_bare_file_list;
    s_appRuntime.hostCalls.bare_metal_file_write_all = host_bare_file_write_all;
    s_appRuntime.hostCalls.bare_metal_file_create_directory = host_bare_file_create_directory;
    s_appRuntime.hostCalls.bare_metal_file_remove = host_bare_file_remove;
    s_appRuntime.hostCalls.bare_metal_development_run_prepare = host_bare_run_prepare;
    s_appRuntime.hostCalls.bare_metal_development_run_start = host_bare_run_start;
    s_appRuntime.hostCalls.bare_metal_development_run_poll = host_bare_run_poll;
    s_appRuntime.hostCalls.bare_metal_development_run_request_close = host_bare_run_request_close;
    s_appRuntime.hostCalls.bare_metal_development_run_release = host_bare_run_release;
    s_appRuntime.hostCalls.native_window_create = host_native_window_create;
    s_appRuntime.hostCalls.native_window_set_text = host_native_window_set_text;
    s_appRuntime.hostCalls.native_window_destroy = host_native_window_destroy;
    s_appRuntime.hostCalls.native_window_run = host_native_window_run;
    s_appRuntime.hostCalls.bare_metal_development_run_cancel = host_bare_run_cancel;

    s_appRuntime.appContext = {};
    s_appRuntime.appContext.size = sizeof(gx_app_context);
    s_appRuntime.appContext.apiVersion = GX_API_VERSION;
    s_appRuntime.appContext.host = &s_appRuntime.hostCalls;
    s_appRuntime.appContext.userData = &s_appRuntime;
}

static bool teardown_application(NativeElfRunReport* report)
{
    bool clean = true;
    copy_gui_proof(report);
    destroy_native_gui();
    if (s_appRuntime.imageBase != 0 && s_appRuntime.imageSize != 0) {
        uint64_t imageEnd = 0;
        if (s_appRuntime.imageBase > ~static_cast<uint64_t>(0) - s_appRuntime.imageSize) {
            clean = false;
        } else {
            imageEnd = s_appRuntime.imageBase + s_appRuntime.imageSize;
            if (!set_page_permissions(s_appRuntime.imageBase, imageEnd, true, false)) clean = false;
            clear_bytes(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(s_appRuntime.imageBase)),
                        s_appRuntime.imageSize);
            if (!set_page_permissions(s_appRuntime.imageBase, imageEnd, false, false)) clean = false;
        }
    }

    if (s_appRuntime.stackBase != 0 && s_appRuntime.stackSize != 0) {
        const uint64_t stackEnd = s_appRuntime.stackBase + s_appRuntime.stackSize;
        clear_bytes(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(s_appRuntime.stackBase)),
                    s_appRuntime.stackSize);
        if (!set_page_permissions(s_appRuntime.stackBase, stackEnd, false, false)) clean = false;
    }

    const int32_t preservedResult = s_appRuntime.result;
    s_appRuntime.appContext = {};
    s_appRuntime.hostCalls = {};
    s_appRuntime.imageBase = 0;
    s_appRuntime.imageSize = 0;
    s_appRuntime.entryPoint = 0;
    s_appRuntime.readOnlyDataBase = 0;
    s_appRuntime.readOnlyDataSize = 0;
    s_appRuntime.stackBase = 0;
    s_appRuntime.stackSize = 0;
    s_appRuntime.stackTop = 0;
    s_appRuntime.runtimeStatus = NativeRuntimeStatus::None;
    s_appRuntime.runtimeCallDepth = 0;
    s_appRuntime.result = preservedResult;
    s_appRuntime.state = clean ? NativeAppExecutionState::Cleaned : NativeAppExecutionState::Failed;
    s_appRuntime.error = clean ? "" : "NativeElf teardown could not restore page permissions";
    if (report) {
        report->teardownComplete = clean;
        report->finalState = s_appRuntime.state;
    }
    return clean;
}

static void put_decimal_u64(uint64_t value)
{
    char digits[20];
    uint32_t count = 0;
    if (value == 0) {
        serial::putc('0');
        return;
    }
    while (value != 0 && count < sizeof(digits)) {
        digits[count++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    }
    while (count != 0) serial::putc(digits[--count]);
}

static void put_decimal_i32(int32_t value)
{
    if (value < 0) {
        serial::putc('-');
        const uint32_t magnitude = static_cast<uint32_t>(-(value + 1)) + 1U;
        put_decimal_u64(magnitude);
    } else {
        put_decimal_u64(static_cast<uint32_t>(value));
    }
}

static uint64_t align_down(uint64_t value)
{
    return value & ~(static_cast<uint64_t>(guidexos::native_elf::PAGE_SIZE) - 1ULL);
}

static bool align_up(uint64_t value, uint64_t* result)
{
    if (!result) return false;
    const uint64_t mask = static_cast<uint64_t>(guidexos::native_elf::PAGE_SIZE) - 1ULL;
    if (value > ~static_cast<uint64_t>(0) - mask) return false;
    *result = (value + mask) & ~mask;
    return true;
}

static bool find_pte(uint64_t virtualAddress, volatile uint64_t** outPte)
{
    if (!outPte || !s_contextConfigured || s_context.pageTableRoot == 0 ||
        (s_context.pageTableRoot & (guidexos::native_elf::PAGE_SIZE - 1U)) != 0) {
        return false;
    }

    volatile uint64_t* table = reinterpret_cast<volatile uint64_t*>(
        static_cast<uintptr_t>(s_context.pageTableRoot));
    const uint64_t indices[4] = {
        (virtualAddress >> 39) & 0x1FFULL,
        (virtualAddress >> 30) & 0x1FFULL,
        (virtualAddress >> 21) & 0x1FFULL,
        (virtualAddress >> 12) & 0x1FFULL
    };

    for (uint32_t level = 0; level < 3; ++level) {
        const uint64_t entry = table[indices[level]];
        if ((entry & PTE_PRESENT) == 0 || (entry & PTE_LARGE) != 0) return false;
        table = reinterpret_cast<volatile uint64_t*>(
            static_cast<uintptr_t>(entry & PTE_ADDRESS_MASK));
    }

    volatile uint64_t* pte = &table[indices[3]];
    const uint64_t entry = *pte;
    if ((entry & PTE_PRESENT) == 0 || (entry & PTE_LARGE) != 0) return false;
    if ((entry & PTE_ADDRESS_MASK) != (virtualAddress & PTE_ADDRESS_MASK)) return false;
    *outPte = pte;
    return true;
}

static bool set_page_permissions(uint64_t start,
                                  uint64_t end,
                                  bool writable,
                                  bool executable)
{
    if (start >= end || (start & (guidexos::native_elf::PAGE_SIZE - 1U)) != 0 ||
        (end & (guidexos::native_elf::PAGE_SIZE - 1U)) != 0) return false;

    for (uint64_t page = start; page < end; page += guidexos::native_elf::PAGE_SIZE) {
        volatile uint64_t* pte = nullptr;
        if (!find_pte(page, &pte)) return false;
        uint64_t entry = *pte;
        if (writable) entry |= PTE_WRITABLE;
        else entry &= ~PTE_WRITABLE;
        if (s_nxEnabled) {
            if (executable) entry &= ~PTE_NX;
            else entry |= PTE_NX;
        }
        *pte = entry;
        arch::amd64::invlpg(reinterpret_cast<void*>(static_cast<uintptr_t>(page)));
    }
    return true;
}

static void clear_bytes(uint8_t* bytes, uint64_t count)
{
    for (uint64_t i = 0; i < count; ++i) bytes[i] = 0;
}

static void copy_bytes(uint8_t* destination, const uint8_t* source, uint64_t count)
{
    for (uint64_t i = 0; i < count; ++i) destination[i] = source[i];
}

static bool prepare_page_tables(const NativeElfValidationResult& validation,
                                NativeElfRunReport* report)
{
    uint64_t mappedEnd = 0;
    if (validation.imageBase > ~static_cast<uint64_t>(0) - validation.mappedBytes) {
        return fail_report(report, "mapped image end overflows");
    }
    mappedEnd = validation.imageBase + validation.mappedBytes;

    // The reusable window is writable but non-executable during population.
    // This is the only temporary write permission transition needed here.
    if (!set_page_permissions(validation.imageBase, mappedEnd, true, false)) {
        return fail_report(report, "application pages are not safely mapped by the kernel page tables");
    }

    uint8_t* imageDestination = reinterpret_cast<uint8_t*>(
        static_cast<uintptr_t>(validation.imageBase));
    clear_bytes(imageDestination, validation.mappedBytes);

    for (uint32_t i = 0; i < validation.loadCount; ++i) {
        const NativeElfLoad& load = validation.loads[i];
        uint8_t* destination = reinterpret_cast<uint8_t*>(
            static_cast<uintptr_t>(load.virtualAddress));
        copy_bytes(destination, s_file + load.fileOffset, load.fileSize);

        uint64_t loadEnd = 0;
        if (load.virtualAddress > ~static_cast<uint64_t>(0) - load.memorySize) {
            return fail_report(report, "PT_LOAD end overflows during mapping");
        }
        loadEnd = load.virtualAddress + load.memorySize;
        uint64_t alignedEnd = 0;
        if (!align_up(loadEnd, &alignedEnd)) {
            return fail_report(report, "PT_LOAD page range overflows during permission setup");
        }
        if (!set_page_permissions(align_down(load.virtualAddress), alignedEnd,
                                  (load.flags & PF_W) != 0,
                                  (load.flags & PF_X) != 0)) {
            return fail_report(report, "PT_LOAD permissions could not be installed");
        }
    }
    return true;
}

} // namespace

bool configure_execution_context(const NativeElfExecutionContext& context)
{
    if (context.pageTableRoot == 0 || context.regionBase != guidexos::native_elf::IMAGE_BASE ||
        context.regionSize < guidexos::native_elf::REGION_SIZE ||
        (context.regionBase & (guidexos::native_elf::PAGE_SIZE - 1U)) != 0 ||
        (context.regionSize & (guidexos::native_elf::PAGE_SIZE - 1U)) != 0) {
        s_contextConfigured = false;
        return false;
    }
    s_context = context;
    s_contextConfigured = true;
    s_nxEnabled = arch::amd64::enable_nx();
    return true;
}

bool execution_context_configured()
{
    return s_contextConfigured;
}

bool configure_development_identity(uint64_t generation, const char* applicationId)
{
    if (generation == 0 || !applicationId) return false;
    uint32_t i = 0;
    for (; i + 1 < GX_DEVELOPMENT_RUN_MAX_APP_ID_BYTES && applicationId[i] != '\0'; ++i) {}
    if (applicationId[i] != '\0') return false;
    s_developmentGeneration = generation;
    copy_bounded_text(s_developmentApplicationId,
                      sizeof(s_developmentApplicationId), applicationId);
    return true;
}

void clear_development_identity()
{
    s_developmentGeneration = 0;
    s_developmentApplicationId[0] = '\0';
}

void set_gui_automation_close(bool enabled)
{
    s_guiAutomationClose = enabled;
}

bool native_elf_gui_runtime_snapshot(NativeElfGuiRuntimeSnapshot* output)
{
    if (!output) return false;
    *output = {};
    output->active = s_guiApplication && s_guiApplication->getWindow() != nullptr;
    output->applicationCreated = s_guiApplicationCreated;
    output->windowCreated = s_guiWindowCreated || s_guiWindowId != 0;
    output->rendered = s_guiRendered;
    output->closedNormally = s_guiClosedNormally;
    output->windowId = s_guiWindowId;
    output->renderCount = s_guiRenderCount;
    output->pumpCount = s_guiPumpCount;
    output->generation = s_guiGeneration;
    output->contentHash = s_guiContentHash;
    copy_bounded_text(output->content, sizeof(output->content), s_guiContent);
    return true;
}

bool request_native_elf_gui_close(uint64_t generation)
{
    if (generation == 0 || generation != s_guiGeneration || !s_guiApplication) return false;
    if (s_guiClosedNormally || !s_guiApplication->getWindow()) return s_guiClosedNormally;
    if (s_guiWindowId == 0) return false;

    s_guiCloseLifecycleRequested = true;
    serial::puts("NativeElf: external_close_request id=");
    serial::put_hex32(s_guiWindowId);
    serial::putc('\n');
    if (!compositor::KernelCompositor::requestCloseWindow(s_guiWindowId)) return false;
    s_guiLastDestroyedWindow = s_guiWindowId;
    s_guiWindowCreated = false;
    return true;
}

static bool run_file_internal(const char* path,
                              int32_t* returnValue,
                              NativeElfRunReport* report,
                              uint64_t stackBase)
{
    clear_report(report);
    if (returnValue) *returnValue = 0;
    if (!s_contextConfigured) return fail_report(report, "execution context is not configured");
    if (!path || path[0] == '\0') return fail_report(report, "ELF path is empty");
    if (!returnValue) return fail_report(report, "return-value output is null");

    // A previous invocation must have completed its cleanup before the single
    // reusable application context can be prepared again.
    if (s_appRuntime.state == NativeAppExecutionState::Running ||
        s_appRuntime.state == NativeAppExecutionState::Loaded ||
        s_appRuntime.state == NativeAppExecutionState::Prepared) {
        return fail_report(report, "NativeElf application context is still active");
    }
    s_appRuntime = {};
    s_appRuntime.state = NativeAppExecutionState::Empty;
    reset_gui_runtime();

    serial::puts("ELF Loader: file=");
    serial::puts(path);
    serial::putc('\n');

    vfs::FileInfo info = {};
    if (vfs::stat(path, &info) != vfs::VFS_OK || info.type != vfs::FILE_TYPE_REGULAR) {
        return fail_report(report, "ELF file is not a regular VFS file");
    }
    if (info.size == 0 || info.size > NATIVE_APP_MAX_ELF_FILE_BYTES) {
        return fail_report(report, "ELF file exceeds loader bounds");
    }
    const uint32_t fileBytes = static_cast<uint32_t>(info.size);
    const int32_t readBytes = vfs::read_file(path, s_file, fileBytes);
    if (readBytes < 0 || static_cast<uint32_t>(readBytes) != fileBytes) {
        return fail_report(report, "ELF file read was incomplete");
    }

    NativeElfValidationPolicy policy = default_validation_policy();
    policy.regionBase = s_context.regionBase;
    policy.regionSize = s_context.regionSize;
    policy.maxFileBytes = NATIVE_APP_MAX_ELF_FILE_BYTES;
    NativeElfValidationResult validation = {};
    if (!validate_native_elf(s_file, fileBytes, policy, &validation)) {
        return fail_report(report, validation.error);
    }
    if (report) {
        report->imageBase = validation.imageBase;
        report->entryPoint = validation.entryPoint;
        report->mappedBytes = validation.mappedBytes;
        report->nxEnabled = s_nxEnabled;
    }

    s_appRuntime.imageBase = validation.imageBase;
    s_appRuntime.imageSize = validation.mappedBytes;
    s_appRuntime.entryPoint = validation.entryPoint;
    (void)find_read_only_data(validation, &s_appRuntime.readOnlyDataBase,
                              &s_appRuntime.readOnlyDataSize);
    s_appRuntime.state = NativeAppExecutionState::Loaded;
    if (report) {
        report->readOnlyDataBase = s_appRuntime.readOnlyDataBase;
        report->readOnlyDataBytes = s_appRuntime.readOnlyDataSize;
    }

    serial::puts("ELF Loader: validation PASS\n");
    serial::puts("ELF Loader: image_base=0x");
    serial::put_hex64(validation.imageBase);
    serial::putc('\n');
    serial::puts("ELF Loader: entry=0x");
    serial::put_hex64(validation.entryPoint);
    serial::putc('\n');
    serial::puts("ELF Loader: mapped_bytes=");
    put_decimal_u64(validation.mappedBytes);
    serial::puts(" nx=");
    serial::puts(s_nxEnabled ? "enabled\n" : "unavailable\n");

    if (!prepare_page_tables(validation, report)) {
        s_appRuntime.state = NativeAppExecutionState::Failed;
        (void)teardown_application(report);
        return false;
    }
    // The normal public route has one fixed application stack. Keep this
    // internal boundary fail-safe if a damaged caller frame supplies an
    // unexpected value; nested execution supplies its separate stack.
    const uint64_t safeStackBase = stackBase == NESTED_APPLICATION_STACK_BASE
        ? NESTED_APPLICATION_STACK_BASE : APPLICATION_STACK_BASE;
    if (!prepare_application_stack(safeStackBase, report)) {
        s_appRuntime.state = NativeAppExecutionState::Failed;
        (void)teardown_application(report);
        return false;
    }
    initialize_app_context();
    s_appRuntime.state = NativeAppExecutionState::Prepared;
    if (report) report->appContextValid =
        s_appRuntime.appContext.size == sizeof(gx_app_context) &&
        s_appRuntime.appContext.apiVersion == GX_API_VERSION &&
        s_appRuntime.appContext.host == &s_appRuntime.hostCalls &&
        s_appRuntime.appContext.host->log != nullptr &&
        s_appRuntime.appContext.userData == &s_appRuntime;

    serial::puts("ELF Loader: dedicated application stack base=0x");
    serial::put_hex64(s_appRuntime.stackBase);
    serial::puts(" top=0x");
    serial::put_hex64(s_appRuntime.stackTop);
    serial::putc('\n');
    serial::puts("ELF Loader: invoking gx_main with gx_app_context\n");
    s_appRuntime.kernelRspBefore = read_stack_pointer();
    s_appRuntime.state = NativeAppExecutionState::Running;
    NativeElfTrampolineResult trampoline = {};
    const bool invoked = invoke_native_entry_on_stack(validation.entryPoint,
                                                      &s_appRuntime.appContext,
                                                      s_appRuntime.stackTop,
                                                      &trampoline);
    s_appRuntime.kernelRspAfter = read_stack_pointer();
    s_appRuntime.applicationRsp = trampoline.applicationRsp;
    if (!invoked) {
        s_appRuntime.state = NativeAppExecutionState::Failed;
        if (report) {
            report->kernelRspBefore = s_appRuntime.kernelRspBefore;
            report->kernelRspAfter = s_appRuntime.kernelRspAfter;
            report->applicationRsp = s_appRuntime.applicationRsp;
        }
        (void)teardown_application(report);
        return fail_report(report, "NativeElf stack trampoline invocation failed");
    }
    s_appRuntime.result = trampoline.returnValue;
    s_appRuntime.runtimeStatus = trampoline.runtimeStatus;
    s_appRuntime.runtimeCallDepth = trampoline.runtimeCallDepth;
    *returnValue = s_appRuntime.result;
    s_appRuntime.state = NativeAppExecutionState::Returned;
    if (report) {
        report->returnValue = s_appRuntime.result;
        report->kernelRspBefore = s_appRuntime.kernelRspBefore;
        report->kernelRspAfter = s_appRuntime.kernelRspAfter;
        report->applicationRsp = s_appRuntime.applicationRsp;
        report->hostLogObserved = s_appRuntime.hostLogObserved;
        report->hostLogBytes = s_appRuntime.hostLogBytes;
        report->hostLogCount = s_appRuntime.hostLogCount;
        report->hostLogTruncated = s_appRuntime.hostLogTruncated;
        report->runtimeStatus = s_appRuntime.runtimeStatus;
        report->runtimeCallDepth = s_appRuntime.runtimeCallDepth;
        for (uint32_t i = 0; i < s_appRuntime.hostLogCount && i < NATIVE_APP_MAX_LOG_LINES; ++i)
            copy_bytes(reinterpret_cast<uint8_t*>(report->hostLog[i]),
                       reinterpret_cast<const uint8_t*>(s_appRuntime.hostLog[i]),
                       NATIVE_APP_MAX_LOG_LINE_BYTES);
        report->dedicatedStackUsed =
            native_app_pointer_in_range(s_appRuntime.applicationRsp,
                                        s_appRuntime.stackBase,
                                        s_appRuntime.stackSize) &&
            s_appRuntime.kernelRspBefore == s_appRuntime.kernelRspAfter;
    }
    serial::puts("ELF Loader: gx_main returned ");
    put_decimal_i32(s_appRuntime.result);
    serial::putc('\n');
    const bool runtimeSucceeded = s_appRuntime.runtimeStatus == NativeRuntimeStatus::None;
    if (!runtimeSucceeded) {
        serial::puts(s_appRuntime.runtimeStatus == NativeRuntimeStatus::ArrayBoundsExceeded
            ? "ELF Loader: Application terminated: array index out of bounds.\n"
            : (s_appRuntime.runtimeStatus == NativeRuntimeStatus::InvalidPointerDereference
                ? "ELF Loader: Application terminated: invalid pointer dereference.\n"
                : (s_appRuntime.runtimeStatus == NativeRuntimeStatus::PointerOutOfBounds
                    ? "ELF Loader: Application terminated: pointer arithmetic moved outside its object.\n"
                    : "ELF Loader: Application terminated: recursive call depth limit exceeded.\n")));
    }
    const bool teardownComplete = teardown_application(report);
    if (report) {
        report->success = teardownComplete && runtimeSucceeded;
        report->error = !teardownComplete ? s_appRuntime.error :
            (runtimeSucceeded ? "" :
             (s_appRuntime.runtimeStatus == NativeRuntimeStatus::ArrayBoundsExceeded
                  ? "ELF Loader: Application terminated: array index out of bounds."
                  : (s_appRuntime.runtimeStatus == NativeRuntimeStatus::InvalidPointerDereference
                      ? "ELF Loader: Application terminated: invalid pointer dereference."
                      : (s_appRuntime.runtimeStatus == NativeRuntimeStatus::PointerOutOfBounds
                          ? "ELF Loader: Application terminated: pointer arithmetic moved outside its object."
                          : "ELF Loader: Application terminated: recursive call depth limit exceeded."))));
    }
    serial::puts(teardownComplete ? "ELF Loader: teardown PASS\n"
                                  : "ELF Loader: teardown FAIL\n");
    return teardownComplete && runtimeSucceeded;
}

bool run_file(const char* path, int32_t* returnValue, NativeElfRunReport* report)
{
    return run_file_internal(path, returnValue, report, APPLICATION_STACK_BASE);
}

static int32_t GX_CALL nested_service_entry(void*)
{
    // The parent NativeElf image and context have already been snapshotted by
    // run_file_nested. The child therefore gets the same loader contract on a
    // separate stack while this kernel callback remains outside the image.
    s_appRuntime = {};
    s_appRuntime.state = NativeAppExecutionState::Empty;
    s_nestedInvocation.success = run_file_internal(
        s_nestedInvocation.path,
        s_nestedInvocation.returnValue,
        s_nestedInvocation.report,
        NESTED_APPLICATION_STACK_BASE);
    return s_nestedInvocation.success ? 1 : 0;
}

bool run_file_nested(const char* path, int32_t* returnValue, NativeElfRunReport* report)
{
    clear_report(report);
    if (returnValue) *returnValue = 0;
    if (!s_contextConfigured || !path || path[0] == '\0' || !returnValue) {
        return fail_report(report, "nested NativeElf request is invalid");
    }
    if (s_nestedExecutionActive || s_appRuntime.state != NativeAppExecutionState::Running) {
        return fail_report(report, "nested NativeElf runtime is not available");
    }
    if (s_appRuntime.imageBase != guidexos::native_elf::IMAGE_BASE ||
        s_appRuntime.imageSize == 0 ||
        s_appRuntime.imageSize > guidexos::native_elf::MAX_MAPPED_BYTES ||
        (s_appRuntime.imageSize & (guidexos::native_elf::PAGE_SIZE - 1ULL)) != 0) {
        return fail_report(report, "active NativeElf image cannot be suspended safely");
    }

    NativeAppStackLayout serviceStack = {};
    if (!calculate_application_stack_layout(NESTED_SERVICE_STACK_BASE,
                                            APPLICATION_STACK_SIZE,
                                            &serviceStack) ||
        !set_page_permissions(serviceStack.base, serviceStack.top, true, false)) {
        return fail_report(report, "nested NativeElf service stack is unavailable");
    }
    clear_bytes(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(serviceStack.base)),
                serviceStack.size);

    s_parentImageSize = s_appRuntime.imageSize;
    copy_bytes(s_parentImage,
               reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(s_appRuntime.imageBase)),
               s_parentImageSize);
    s_parentImagePteCount = static_cast<uint32_t>(
        s_parentImageSize / guidexos::native_elf::PAGE_SIZE);
    if (s_parentImagePteCount > sizeof(s_parentImagePtes) / sizeof(s_parentImagePtes[0])) {
        (void)set_page_permissions(serviceStack.base, serviceStack.top, false, false);
        return fail_report(report, "nested NativeElf image page count exceeds bounds");
    }
    for (uint32_t i = 0; i < s_parentImagePteCount; ++i) {
        volatile uint64_t* pte = nullptr;
        if (!find_pte(s_appRuntime.imageBase +
                          static_cast<uint64_t>(i) * guidexos::native_elf::PAGE_SIZE,
                      &pte)) {
            (void)set_page_permissions(serviceStack.base, serviceStack.top, false, false);
            return fail_report(report, "parent NativeElf page table cannot be suspended");
        }
        s_parentImagePtes[i] = *pte;
    }
    s_parentRuntime = s_appRuntime;
    s_nestedInvocation.path = path;
    s_nestedInvocation.returnValue = returnValue;
    s_nestedInvocation.report = report;
    s_nestedInvocation.success = false;
    s_nestedExecutionActive = true;

    NativeElfTrampolineResult trampoline = {};
    const bool invoked = invoke_native_entry_on_stack(
        reinterpret_cast<uint64_t>(&nested_service_entry),
        &s_nestedInvocation,
        serviceStack.top,
        &trampoline);

    bool restored = true;
    const uint64_t parentEnd = guidexos::native_elf::IMAGE_BASE + s_parentImageSize;
    if (!set_page_permissions(guidexos::native_elf::IMAGE_BASE, parentEnd, true, false)) {
        restored = false;
    } else {
        copy_bytes(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(guidexos::native_elf::IMAGE_BASE)),
                   s_parentImage, s_parentImageSize);
        for (uint32_t i = 0; i < s_parentImagePteCount; ++i) {
            volatile uint64_t* pte = nullptr;
            const uint64_t page = guidexos::native_elf::IMAGE_BASE +
                static_cast<uint64_t>(i) * guidexos::native_elf::PAGE_SIZE;
            if (!find_pte(page, &pte)) {
                restored = false;
                continue;
            }
            *pte = s_parentImagePtes[i];
            arch::amd64::invlpg(reinterpret_cast<void*>(static_cast<uintptr_t>(page)));
        }
    }
    clear_bytes(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(serviceStack.base)),
                serviceStack.size);
    if (!set_page_permissions(serviceStack.base, serviceStack.top, false, false)) restored = false;

    s_appRuntime = s_parentRuntime;
    s_parentRuntime = {};
    s_parentImageSize = 0;
    s_parentImagePteCount = 0;
    s_nestedExecutionActive = false;
    s_nestedInvocation.path = nullptr;
    s_nestedInvocation.returnValue = nullptr;
    s_nestedInvocation.report = nullptr;

    if (!invoked || !restored) {
        return fail_report(report, !invoked
            ? "nested NativeElf trampoline invocation failed"
            : "nested NativeElf parent restoration failed");
    }
    return s_nestedInvocation.success;
}

bool native_elf_execution_active()
{
    return s_appRuntime.state == NativeAppExecutionState::Running;
}

const NativeAppExecutionContext* native_elf_runtime_context()
{
    return &s_appRuntime;
}

bool native_elf_host_call_validation_smoke()
{
    NativeAppStackLayout stack = {};
    uint32_t maximumReadableBytes = 0;
    const uint64_t dataBase = guidexos::native_elf::IMAGE_BASE + 0x2000ULL;
    const bool stackBounds = calculate_application_stack_layout(
        APPLICATION_STACK_BASE, APPLICATION_STACK_SIZE, &stack) &&
        stack.top == APPLICATION_STACK_BASE + APPLICATION_STACK_SIZE &&
        (stack.top & (APPLICATION_STACK_ALIGNMENT - 1ULL)) == 0;
    const bool nullRejected = !native_app_log_pointer_range(0, dataBase, 256, &maximumReadableBytes);
    const bool beforeRejected = !native_app_log_pointer_range(dataBase - 1, dataBase, 256, &maximumReadableBytes);
    const bool afterRejected = !native_app_log_pointer_range(dataBase + 256, dataBase, 256, &maximumReadableBytes);
    const bool boundedAtEnd = native_app_log_pointer_range(dataBase + 254, dataBase, 256,
                                                           &maximumReadableBytes) &&
                              maximumReadableBytes == 2;
    const bool inactiveContextRejected = host_log(nullptr, nullptr) == GX_ERROR_PERMISSION_DENIED;
    return stackBounds && nullRejected && beforeRejected && afterRejected &&
           boundedAtEnd && inactiveContextRejected;
}

} // namespace native_elf
} // namespace kernel
