#include "include/kernel/aarch64_studio_v05_abi_adapter.h"

#include <stddef.h>
#include <stdint.h>

#ifndef GXOS_STUDIO_V05_ABI_HEADER
#error GXOS_STUDIO_V05_ABI_HEADER must name the pinned v0.5 SDK abi.h
#endif
#ifndef GXOS_STUDIO_V05_APP_HEADER
#error GXOS_STUDIO_V05_APP_HEADER must name the pinned v0.5 SDK app.h
#endif

/* Include the pinned v0.5 declarations in their own namespace. The context
 * definition is repeated from its app.h contract below to avoid declaring a
 * second C-linkage gx_main symbol in this translation unit. */
#pragma push_macro("GX_DEVELOPMENT_RUN_API_VERSION")
#undef GX_DEVELOPMENT_RUN_API_VERSION
#define gx_app_context gx_v05_app_context
#define gx_host_calls gx_v05_host_calls
namespace studio_v05 {
#include GXOS_STUDIO_V05_ABI_HEADER
#define gx_main gx_v05_entry_contract_declaration
#include GXOS_STUDIO_V05_APP_HEADER
#undef gx_main
#undef gx_app_context
#undef gx_host_calls
typedef gx_v05_app_context gx_app_context;
typedef gx_v05_host_calls gx_host_calls;
}
#pragma pop_macro("GX_DEVELOPMENT_RUN_API_VERSION")

static_assert(sizeof(gx_host_calls) == 400, "AARCH64 ABI must remain 400 bytes");
static_assert(sizeof(studio_v05::gx_host_calls) == 464, "v0.5 ABI must remain 464 bytes");
static_assert(offsetof(gx_host_calls, guiVersion) == 240, "AARCH64 GUI field moved");
static_assert(offsetof(gx_host_calls, bare_metal_build_project_start) == 288,
              "AARCH64 Build callback moved");
static_assert(offsetof(studio_v05::gx_host_calls, bare_metal_build_project_start) == 240,
              "v0.5 Build callback moved");
static_assert(offsetof(studio_v05::gx_host_calls, draw_text) == 32 &&
              offsetof(studio_v05::gx_host_calls, draw_rect) == 40 &&
              offsetof(studio_v05::gx_host_calls, request_window_ex) == 88 &&
              offsetof(studio_v05::gx_host_calls, bare_metal_file_stat) == 264,
              "v0.5 shared and file callback slots moved");
static_assert(offsetof(studio_v05::gx_host_calls, native_window_destroy) == 368,
              "v0.5 NativeWindow callback moved");
static_assert(offsetof(studio_v05::gx_host_calls, get_document_activation_path) == 448,
              "v0.5 activation callback moved");
static_assert(offsetof(studio_v05::gx_host_calls, bare_metal_build_project_cancel) == 456,
              "v0.5 Build-cancel callback moved");
static_assert(sizeof(gx_app_context) == sizeof(studio_v05::gx_app_context),
              "Application context size must match before raw entry dispatch");
static_assert(offsetof(gx_app_context, host) == offsetof(studio_v05::gx_app_context, host) &&
              offsetof(gx_app_context, userData) == offsetof(studio_v05::gx_app_context, userData),
              "Application context field layout changed");
static_assert(sizeof(gx_event) == sizeof(studio_v05::gx_event) &&
              offsetof(gx_event, param4) == offsetof(studio_v05::gx_event, param4),
              "Event records require explicit field conversion");
static_assert(sizeof(gx_build_request) == sizeof(studio_v05::gx_build_request),
              "Build request layout changed; update fieldwise conversion");
static_assert(offsetof(gx_build_request, configuration) ==
              offsetof(studio_v05::gx_build_request, configuration),
              "Build request field order changed");
static_assert(GX_BUILD_MAX_OUTPUT_LINES == 32u && GX_BUILD_MAX_OUTPUT_LINE_BYTES == 256u &&
              GX_BUILD_MAX_ARTIFACT_PATH_BYTES == 160u && GX_BUILD_MAX_SHA256_BYTES == 65u,
              "Build output capacities require bounded conversion updates");
static_assert(sizeof(gx_file_info) == sizeof(studio_v05::gx_file_info) &&
              offsetof(gx_file_info, size) == offsetof(studio_v05::gx_file_info, size) &&
              sizeof(gx_file_entry) == sizeof(studio_v05::gx_file_entry) &&
              offsetof(gx_file_entry, size) == offsetof(studio_v05::gx_file_entry, size) &&
              offsetof(gx_file_entry, name) == offsetof(studio_v05::gx_file_entry, name),
              "File records require fieldwise conversion");
static_assert(offsetof(gx_development_run_request, projectRoot) ==
              offsetof(studio_v05::gx_development_run_request, projectRoot) &&
              offsetof(gx_development_run_request, artifactSize) ==
              offsetof(studio_v05::gx_development_run_request, artifactSize) &&
              offsetof(gx_development_run_request, artifactArchitecture) ==
              offsetof(studio_v05::gx_development_run_request, artifactArchitecture),
              "Run request shared field order changed");
static_assert(offsetof(gx_development_run_request, artifactAbi) ==
              offsetof(studio_v05::gx_development_run_request, artifactAbi),
              "Run request shared prefix changed");
static_assert(offsetof(gx_development_run_snapshot, output) ==
              offsetof(studio_v05::gx_development_run_snapshot, output),
              "Run snapshot shared prefix changed");
static_assert(offsetof(gx_development_run_snapshot, capabilities) ==
              offsetof(studio_v05::gx_development_run_snapshot, closeRequested),
              "Run snapshot extension boundary changed");
static_assert(GX_DEVELOPMENT_RUN_MAX_OUTPUT_LINES == 16u &&
              GX_DEVELOPMENT_RUN_MAX_OUTPUT_LINE_BYTES == 256u &&
              GX_DEVELOPMENT_RUN_MAX_APP_ID_BYTES == 96u &&
              GX_DEVELOPMENT_RUN_MAX_DISPLAY_NAME_BYTES == 96u &&
              GX_DEVELOPMENT_RUN_MAX_SHA256_BYTES == 65u &&
              GX_DEVELOPMENT_RUN_MAX_ERROR_BYTES == 128u,
              "Run snapshot capacities require bounded conversion updates");

namespace kernel {
namespace studio_abi {
namespace {

static const uint32_t kMaxStringBytes = 256;
static const uint32_t kMaxTransferBytes = 4u * 1024u * 1024u;
static const uint32_t kMaxListedEntries = 64;

struct V05AdapterState {
    gx_app_context* aarch64Context;
    gx_host_calls* aarch64Host;
    gui_render::HostBinding* renderBinding;
    UserMemoryRange userMemory;
    studio_v05::gx_app_context context;
    studio_v05::gx_host_calls host;
    uint32_t active;
    uint32_t observedEventMask;
    uint32_t observedKeyEventCount;
};

static_assert(sizeof(V05AdapterState::host) == 464,
              "Adapter state must contain one complete v0.5 table");

static V05AdapterState* s_activeAdapter = nullptr;

static uint32_t append_decimal(char* output, uint32_t capacity,
                               uint32_t offset, uint32_t value)
{
    char digits[10];
    uint32_t count = 0;
    do { digits[count++] = static_cast<char>('0' + value % 10u); value /= 10u; }
    while (value != 0 && count < sizeof(digits));
    while (count > 0 && offset + 1 < capacity) output[offset++] = digits[--count];
    output[offset] = '\0';
    return offset;
}

static void log_key_event(V05AdapterState* adapter, const gx_event& event)
{
    if (!adapter || !adapter->aarch64Host || !adapter->aarch64Host->log) return;
    char message[128] = "ABI3_STUDIO_KEY_EVENT sequence=";
    uint32_t offset = 0;
    while (message[offset] != '\0') ++offset;
    offset = append_decimal(message, sizeof(message), offset,
                                     ++adapter->observedKeyEventCount);
    const char* keyLabel = " key=";
    while (*keyLabel && offset + 1 < sizeof(message)) message[offset++] = *keyLabel++;
    offset = append_decimal(message, sizeof(message), offset,
                            static_cast<uint32_t>(event.param1));
    const char* actionLabel = " action=";
    while (*actionLabel && offset + 1 < sizeof(message)) message[offset++] = *actionLabel++;
    const char* action = event.param2 == GX_KEY_ACTION_DOWN ? "down" : "up";
    while (*action && offset + 1 < sizeof(message)) message[offset++] = *action++;
    const char* modifierLabel = " modifiers=";
    while (*modifierLabel && offset + 1 < sizeof(message)) message[offset++] = *modifierLabel++;
    (void)append_decimal(message, sizeof(message), offset,
                         static_cast<uint32_t>(event.param3));
    adapter->aarch64Host->log(adapter->aarch64Context, message);
}

static bool range_contains(uint64_t base, uint64_t bytes,
                           const void* pointer, uint64_t length)
{
    if (!pointer || bytes == 0 || length == 0) return false;
    const uintptr_t begin = reinterpret_cast<uintptr_t>(pointer);
    if (base > UINTPTR_MAX || length > UINTPTR_MAX - begin ||
        bytes > UINTPTR_MAX - static_cast<uintptr_t>(base)) return false;
    const uintptr_t end = begin + static_cast<uintptr_t>(length);
    const uintptr_t rangeBegin = static_cast<uintptr_t>(base);
    const uintptr_t rangeEnd = rangeBegin + static_cast<uintptr_t>(bytes);
    return begin >= rangeBegin && end <= rangeEnd;
}

static bool valid_range(const UserMemoryRange& memory)
{
    return memory.imageBytes != 0 && memory.stackBytes != 0 &&
        memory.imageBase <= UINTPTR_MAX && memory.stackBase <= UINTPTR_MAX &&
        memory.imageBytes <= UINTPTR_MAX - static_cast<uintptr_t>(memory.imageBase) &&
        memory.stackBytes <= UINTPTR_MAX - static_cast<uintptr_t>(memory.stackBase);
}

static bool app_range(const V05AdapterState* adapter, const void* pointer, uint64_t bytes)
{
    if (!adapter || !pointer || bytes == 0) return false;
    return range_contains(adapter->userMemory.imageBase, adapter->userMemory.imageBytes, pointer, bytes) ||
        range_contains(adapter->userMemory.stackBase, adapter->userMemory.stackBytes, pointer, bytes);
}

template <typename T>
static bool app_object(const V05AdapterState* adapter, const T* pointer)
{
    return pointer && (reinterpret_cast<uintptr_t>(pointer) % alignof(T)) == 0 &&
        app_range(adapter, pointer, sizeof(T));
}

static bool copy_app_string(const V05AdapterState* adapter, const char* source,
                            char* destination, uint32_t capacity)
{
    if (!adapter || !source || !destination || capacity == 0) return false;
    const uint32_t limit = capacity < kMaxStringBytes ? capacity : kMaxStringBytes;
    for (uint32_t i = 0; i < limit; ++i) {
        const uintptr_t address = reinterpret_cast<uintptr_t>(source);
        if (i > UINTPTR_MAX - address ||
            !app_range(adapter, reinterpret_cast<const void*>(address + i), 1)) return false;
        const char value = source[i];
        destination[i] = value;
        if (value == '\0') return true;
    }
    destination[capacity - 1] = '\0';
    return false;
}

static bool optional_app_string(const V05AdapterState* adapter, const char* source,
                                char* destination, uint32_t capacity,
                                const char** output)
{
    if (!destination || capacity == 0 || !output) return false;
    if (!source) {
        destination[0] = '\0';
        *output = nullptr;
        return true;
    }
    if (!copy_app_string(adapter, source, destination, capacity)) return false;
    *output = destination;
    return true;
}

static studio_v05::gx_result to_v05_result(gx_result result)
{
    switch (result) {
    case GX_OK: return studio_v05::GX_OK;
    case GX_ERROR_NOT_IMPLEMENTED: return studio_v05::GX_ERROR_NOT_IMPLEMENTED;
    case GX_ERROR_INVALID_ARGUMENT: return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    case GX_ERROR_UNSUPPORTED: return studio_v05::GX_ERROR_UNSUPPORTED;
    case GX_ERROR_FAILED: return studio_v05::GX_ERROR_FAILED;
    case GX_ERROR_PERMISSION_DENIED: return studio_v05::GX_ERROR_PERMISSION_DENIED;
    case GX_ERROR_INTERNAL: return studio_v05::GX_ERROR_INTERNAL;
    case GX_ERROR_TIMEOUT: return studio_v05::GX_ERROR_TIMEOUT;
    case GX_ERROR_BUSY: return studio_v05::GX_ERROR_BUSY;
    case GX_ERROR_QUOTA:
        /* -9 means quota in AARCH64 but not-found in v0.5. Never leak it. */
        return studio_v05::GX_ERROR_FAILED;
    default:
        return result < 0 ? studio_v05::GX_ERROR_FAILED : static_cast<studio_v05::gx_result>(result);
    }
}

static gx_result to_aarch64_result(studio_v05::gx_result result)
{
    switch (result) {
    case studio_v05::GX_OK: return GX_OK;
    case studio_v05::GX_ERROR_NOT_IMPLEMENTED: return GX_ERROR_NOT_IMPLEMENTED;
    case studio_v05::GX_ERROR_INVALID_ARGUMENT: return GX_ERROR_INVALID_ARGUMENT;
    case studio_v05::GX_ERROR_UNSUPPORTED: return GX_ERROR_UNSUPPORTED;
    case studio_v05::GX_ERROR_FAILED: return GX_ERROR_FAILED;
    case studio_v05::GX_ERROR_PERMISSION_DENIED: return GX_ERROR_PERMISSION_DENIED;
    case studio_v05::GX_ERROR_INTERNAL: return GX_ERROR_INTERNAL;
    case studio_v05::GX_ERROR_TIMEOUT: return GX_ERROR_TIMEOUT;
    case studio_v05::GX_ERROR_BUSY: return GX_ERROR_BUSY;
    /* v0.5's -9 is NOT_FOUND. AARCH64's -9 is QUOTA. */
    case studio_v05::GX_ERROR_NOT_FOUND:
    case studio_v05::GX_ERROR_NOT_MOUNTED:
    case studio_v05::GX_ERROR_IO:
        return GX_ERROR_FAILED;
    default:
        return result < 0 ? GX_ERROR_FAILED : static_cast<gx_result>(result);
    }
}

static V05AdapterState* state_for(studio_v05::gx_app_context* context)
{
    V05AdapterState* adapter = s_activeAdapter;
    /* Compare before dereferencing: callers may pass an arbitrary context. */
    if (!adapter || context != &adapter->context || adapter->active == 0) return nullptr;
    return context->size == sizeof(*context) && context->apiVersion == 0 &&
        context->host == &adapter->host && context->userData == adapter ? adapter : nullptr;
}

static studio_v05::gx_result GX_CALL host_log(studio_v05::gx_app_context* context,
                                               const char* message)
{
    V05AdapterState* adapter = state_for(context);
    char bounded[kMaxStringBytes];
    if (!adapter) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    if (!adapter->aarch64Host->log) return studio_v05::GX_ERROR_UNSUPPORTED;
    if (!copy_app_string(adapter, message, bounded, sizeof(bounded)))
        return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    return to_v05_result(adapter->aarch64Host->log(adapter->aarch64Context, bounded));
}

static uint32_t GX_CALL host_get_api_version(studio_v05::gx_app_context* context)
{
    V05AdapterState* adapter = state_for(context);
    return adapter && adapter->aarch64Host->get_api_version
        ? adapter->aarch64Host->get_api_version(adapter->aarch64Context) : UINT32_MAX;
}

static studio_v05::gx_result GX_CALL host_request_window(
    studio_v05::gx_app_context* context, const char* title, int width, int height,
    studio_v05::gx_handle* outWindow)
{
    V05AdapterState* adapter = state_for(context);
    char bounded[kMaxStringBytes];
    if (!adapter) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    if (!adapter->aarch64Host->request_window) return studio_v05::GX_ERROR_UNSUPPORTED;
    if (!outWindow || !app_object(adapter, outWindow) ||
        !copy_app_string(adapter, title, bounded, sizeof(bounded)))
        return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    gx_handle handle = 0;
    const gx_result result = adapter->aarch64Host->request_window(
        adapter->aarch64Context, bounded, width, height, &handle);
    if (result == GX_OK) *outWindow = handle;
    return to_v05_result(result);
}

static studio_v05::gx_result GX_CALL host_request_window_ex(
    studio_v05::gx_app_context* context, const char* title, int width, int height,
    uint32_t flags, studio_v05::gx_handle* outWindow)
{
    V05AdapterState* adapter = state_for(context);
    char bounded[kMaxStringBytes];
    if (!adapter) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    if (!adapter->aarch64Host->request_window_ex) return studio_v05::GX_ERROR_UNSUPPORTED;
    if (!outWindow || !app_object(adapter, outWindow) ||
        !copy_app_string(adapter, title, bounded, sizeof(bounded)))
        return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    gx_handle handle = 0;
    const gx_result result = adapter->aarch64Host->request_window_ex(
        adapter->aarch64Context, bounded, width, height, flags, &handle);
    if (result == GX_OK) *outWindow = handle;
    return to_v05_result(result);
}

static studio_v05::gx_result GX_CALL host_draw_rect(
    studio_v05::gx_app_context* context, studio_v05::gx_handle window,
    int x, int y, int width, int height, uint32_t color)
{
    V05AdapterState* adapter = state_for(context);
    if (!adapter) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    if (!adapter->renderBinding || !gui_render::binding_valid(adapter->renderBinding, adapter->aarch64Context))
        return studio_v05::GX_ERROR_UNSUPPORTED;
    if (!window || width <= 0 || height <= 0) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    gui_render::Surface surface{};
    if (!adapter->renderBinding->resolveSurface(adapter->renderBinding->owner, window, &surface) ||
        !gui_render::surface_valid(surface)) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    const gx_result result = gui_render::draw_rect(&surface, x, y, width, height, color);
    if (result == GX_OK)
        adapter->renderBinding->invalidateSurface(adapter->renderBinding->owner, window);
    return to_v05_result(result);
}

static studio_v05::gx_result GX_CALL host_draw_text(
    studio_v05::gx_app_context* context, studio_v05::gx_handle window,
    int x, int y, const char* text)
{
    V05AdapterState* adapter = state_for(context);
    char bounded[kMaxStringBytes];
    if (!adapter) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    if (!adapter->renderBinding || !gui_render::binding_valid(adapter->renderBinding, adapter->aarch64Context))
        return studio_v05::GX_ERROR_UNSUPPORTED;
    if (!window || !copy_app_string(adapter, text, bounded, sizeof(bounded)))
        return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    gui_render::Surface surface{};
    if (!adapter->renderBinding->resolveSurface(adapter->renderBinding->owner, window, &surface) ||
        !gui_render::surface_valid(surface)) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    const gx_result result = gui_render::draw_text(&surface, x, y, bounded);
    if (result == GX_OK)
        adapter->renderBinding->invalidateSurface(adapter->renderBinding->owner, window);
    return to_v05_result(result);
}

static studio_v05::gx_result GX_CALL host_poll_event(
    studio_v05::gx_app_context* context, studio_v05::gx_event* outEvent, int timeoutMs)
{
    V05AdapterState* adapter = state_for(context);
    if (!adapter) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    if (!adapter->aarch64Host->poll_event) return studio_v05::GX_ERROR_UNSUPPORTED;
    if (!app_object(adapter, outEvent))
        return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    gx_event event{};
    const gx_result result = adapter->aarch64Host->poll_event(
        adapter->aarch64Context, &event, timeoutMs);
    if (result != GX_OK) return to_v05_result(result);
    outEvent->size = sizeof(*outEvent);
    outEvent->type = event.type <= GX_EVENT_WINDOW_PAINT
        ? static_cast<studio_v05::gx_event_type>(event.type) : studio_v05::GX_EVENT_NONE;
    outEvent->window = event.window;
    outEvent->param1 = event.param1;
    outEvent->param2 = event.param2;
    outEvent->param3 = event.param3;
    outEvent->param4 = event.param4;
    if (event.type > GX_EVENT_NONE && event.type <= GX_EVENT_WINDOW_PAINT) {
        const uint32_t eventBit = UINT32_C(1) << event.type;
        if ((adapter->observedEventMask & eventBit) == 0) {
            adapter->observedEventMask |= eventBit;
            if (event.type == GX_EVENT_MOUSE && adapter->aarch64Host->log)
                adapter->aarch64Host->log(adapter->aarch64Context,
                    "ABI3 guest observed Studio mouse event through v0.5 poll_event");
            else if (event.type == GX_EVENT_KEY && adapter->aarch64Host->log)
                adapter->aarch64Host->log(adapter->aarch64Context,
                    "ABI3 guest observed Studio key event through v0.5 poll_event");
            else if (event.type == GX_EVENT_WINDOW_FOCUS && adapter->aarch64Host->log)
                adapter->aarch64Host->log(adapter->aarch64Context,
                    "ABI3 guest observed Studio focus event through v0.5 poll_event");
            else if (event.type == GX_EVENT_WINDOW_CLOSE && adapter->aarch64Host->log)
                adapter->aarch64Host->log(adapter->aarch64Context,
                    "ABI3 guest observed Studio close event through v0.5 poll_event");
        }
        if (event.type == GX_EVENT_KEY) log_key_event(adapter, event);
    }
    return studio_v05::GX_OK;
}

static studio_v05::gx_result GX_CALL host_exit(studio_v05::gx_app_context* context,
                                                studio_v05::gx_result exitCode)
{
    V05AdapterState* adapter = state_for(context);
    if (!adapter || !adapter->aarch64Host->exit) return studio_v05::GX_ERROR_UNSUPPORTED;
    return to_v05_result(adapter->aarch64Host->exit(adapter->aarch64Context,
                                                     to_aarch64_result(exitCode)));
}

static studio_v05::gx_result GX_CALL host_native_window_destroy(
    studio_v05::gx_app_context* context, studio_v05::gx_handle window)
{
    V05AdapterState* adapter = state_for(context);
    if (!adapter) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    if (!adapter->aarch64Host->window_destroy) return studio_v05::GX_ERROR_UNSUPPORTED;
    if (!window) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    return to_v05_result(adapter->aarch64Host->window_destroy(adapter->aarch64Context, window));
}

static bool app_path(V05AdapterState* adapter, const char* path, char (&bounded)[kMaxStringBytes])
{
    return copy_app_string(adapter, path, bounded, sizeof(bounded));
}

static studio_v05::gx_result GX_CALL host_bare_file_stat(
    studio_v05::gx_app_context* context, const char* path, studio_v05::gx_file_info* outInfo)
{
    V05AdapterState* adapter = state_for(context);
    char bounded[kMaxStringBytes];
    if (!adapter) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    if (!adapter->aarch64Host->bare_metal_file_stat) return studio_v05::GX_ERROR_UNSUPPORTED;
    if (!app_object(adapter, outInfo) || !app_path(adapter, path, bounded))
        return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    gx_file_info info{};
    const gx_result result = adapter->aarch64Host->bare_metal_file_stat(
        adapter->aarch64Context, bounded, &info);
    if (result == kV05FileStatNotFoundBridge) return studio_v05::GX_ERROR_NOT_FOUND;
    if (result == kV05FileStatMountUnavailableBridge) return studio_v05::GX_ERROR_NOT_MOUNTED;
    if (result == GX_OK) {
        outInfo->type = info.type;
        outInfo->reserved = info.reserved;
        outInfo->size = info.size;
    }
    return to_v05_result(result);
}

static studio_v05::gx_result GX_CALL host_bare_file_read(
    studio_v05::gx_app_context* context, const char* path, void* buffer,
    uint32_t bufferSize, uint32_t* outBytesRead)
{
    V05AdapterState* adapter = state_for(context);
    char bounded[kMaxStringBytes];
    if (!adapter) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    if (!adapter->aarch64Host->bare_metal_file_read_workspace) return studio_v05::GX_ERROR_UNSUPPORTED;
    if (!app_object(adapter, outBytesRead) ||
        bufferSize > kMaxTransferBytes || (bufferSize && !app_range(adapter, buffer, bufferSize)) ||
        !app_path(adapter, path, bounded)) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    uint32_t bytesRead = 0;
    const gx_result result = adapter->aarch64Host->bare_metal_file_read_workspace(
        adapter->aarch64Context, bounded, buffer, bufferSize, &bytesRead);
    if (result == GX_OK) {
        if (bytesRead > bufferSize) return studio_v05::GX_ERROR_INTERNAL;
        *outBytesRead = bytesRead;
    }
    return to_v05_result(result);
}

static studio_v05::gx_result GX_CALL host_bare_file_list(
    studio_v05::gx_app_context* context, const char* path,
    studio_v05::gx_file_entry* entries, uint32_t capacity,
    uint32_t* outCount, uint32_t* outTruncated)
{
    V05AdapterState* adapter = state_for(context);
    char bounded[kMaxStringBytes];
    if (!adapter) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    if (!adapter->aarch64Host->bare_metal_file_list) return studio_v05::GX_ERROR_UNSUPPORTED;
    if (capacity > kMaxListedEntries ||
        (capacity && (!entries ||
            (reinterpret_cast<uintptr_t>(entries) % alignof(studio_v05::gx_file_entry)) != 0 ||
            !app_range(adapter, entries,
            static_cast<uint64_t>(capacity) * sizeof(*entries)))) ||
        !app_object(adapter, outCount) || !app_object(adapter, outTruncated) ||
        !app_path(adapter, path, bounded)) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    gx_file_entry temporary[kMaxListedEntries]{};
    uint32_t count = 0;
    uint32_t truncated = 0;
    const gx_result result = adapter->aarch64Host->bare_metal_file_list(
        adapter->aarch64Context, bounded, temporary, capacity, &count, &truncated);
    if (result != GX_OK) return to_v05_result(result);
    if (count > capacity) return studio_v05::GX_ERROR_FAILED;
    for (uint32_t i = 0; i < count; ++i) {
        entries[i].type = temporary[i].type;
        entries[i].reserved = temporary[i].reserved;
        entries[i].size = temporary[i].size;
        for (uint32_t j = 0; j < sizeof(entries[i].name); ++j)
            entries[i].name[j] = temporary[i].name[j];
    }
    *outCount = count;
    *outTruncated = truncated;
    return studio_v05::GX_OK;
}

static studio_v05::gx_result GX_CALL host_bare_file_write(
    studio_v05::gx_app_context* context, const char* path, const void* buffer,
    uint32_t bufferSize, uint32_t* outBytesWritten)
{
    V05AdapterState* adapter = state_for(context);
    char bounded[kMaxStringBytes];
    if (!adapter) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    if (!adapter->aarch64Host->bare_metal_file_write_all) return studio_v05::GX_ERROR_UNSUPPORTED;
    if (!app_object(adapter, outBytesWritten) ||
        bufferSize > kMaxTransferBytes || (bufferSize && !app_range(adapter, buffer, bufferSize)) ||
        !app_path(adapter, path, bounded)) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    uint32_t bytesWritten = 0;
    const gx_result result = adapter->aarch64Host->bare_metal_file_write_all(
        adapter->aarch64Context, bounded, buffer, bufferSize, &bytesWritten);
    if (result == GX_OK) {
        if (bytesWritten > bufferSize) return studio_v05::GX_ERROR_INTERNAL;
        *outBytesWritten = bytesWritten;
    }
    return to_v05_result(result);
}

static studio_v05::gx_result GX_CALL host_bare_file_create_directory(
    studio_v05::gx_app_context* context, const char* path)
{
    V05AdapterState* adapter = state_for(context);
    char bounded[kMaxStringBytes];
    if (!adapter) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    if (!adapter->aarch64Host->bare_metal_file_create_directory) return studio_v05::GX_ERROR_UNSUPPORTED;
    if (!app_path(adapter, path, bounded)) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    return to_v05_result(adapter->aarch64Host->bare_metal_file_create_directory(
        adapter->aarch64Context, bounded));
}

static studio_v05::gx_result GX_CALL host_bare_file_remove(
    studio_v05::gx_app_context* context, const char* path)
{
    V05AdapterState* adapter = state_for(context);
    char bounded[kMaxStringBytes];
    if (!adapter) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    if (!adapter->aarch64Host->bare_metal_file_remove) return studio_v05::GX_ERROR_UNSUPPORTED;
    if (!app_path(adapter, path, bounded)) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    return to_v05_result(adapter->aarch64Host->bare_metal_file_remove(
        adapter->aarch64Context, bounded));
}

static bool convert_build_request(V05AdapterState* adapter,
                                  const studio_v05::gx_build_request* source,
                                  gx_build_request* destination,
                                  char strings[8][kMaxStringBytes])
{
    if (!source || !destination || !app_object(adapter, source) ||
        source->size < sizeof(*source) || source->version != GX_BUILD_API_VERSION) return false;
    const char* input[8] = { source->projectRoot, source->projectId, source->projectKind,
        source->targetProfile, source->buildSystem, source->buildScript,
        source->expectedArtifact, source->configuration };
    const char** output[8] = { &destination->projectRoot, &destination->projectId,
        &destination->projectKind, &destination->targetProfile, &destination->buildSystem,
        &destination->buildScript, &destination->expectedArtifact, &destination->configuration };
    for (uint32_t i = 0; i < 8; ++i)
        if (!optional_app_string(adapter, input[i], strings[i], kMaxStringBytes, output[i])) return false;
    destination->size = sizeof(*destination);
    destination->version = GX_BUILD_API_VERSION;
    return true;
}

static void copy_build_snapshot(const gx_build_snapshot& source,
                                studio_v05::gx_build_snapshot* destination)
{
    destination->size = sizeof(*destination);
    destination->version = source.version;
    destination->handle = source.handle;
    destination->state = source.state;
    destination->processExitCode = source.processExitCode;
    destination->errorCode = source.errorCode;
    destination->warningCount = source.warningCount;
    destination->errorCount = source.errorCount;
    destination->outputCount = source.outputCount > GX_BUILD_MAX_OUTPUT_LINES
        ? GX_BUILD_MAX_OUTPUT_LINES : source.outputCount;
    destination->outputTruncated = source.outputTruncated;
    destination->elapsedMilliseconds = source.elapsedMilliseconds;
    destination->artifactSize = source.artifactSize;
    destination->artifactValid = source.artifactValid;
    destination->artifactEntryPoint = source.artifactEntryPoint;
    for (uint32_t i = 0; i < sizeof(destination->artifactPath); ++i)
        destination->artifactPath[i] = source.artifactPath[i];
    for (uint32_t i = 0; i < sizeof(destination->artifactSha256); ++i)
        destination->artifactSha256[i] = source.artifactSha256[i];
    for (uint32_t i = 0; i < sizeof(destination->artifactArchitecture); ++i)
        destination->artifactArchitecture[i] = source.artifactArchitecture[i];
    for (uint32_t i = 0; i < sizeof(destination->errorMessage); ++i)
        destination->errorMessage[i] = source.errorMessage[i];
    for (uint32_t i = 0; i < destination->outputCount; ++i) {
        destination->output[i].stream = source.output[i].stream;
        for (uint32_t j = 0; j < sizeof(destination->output[i].text); ++j)
            destination->output[i].text[j] = source.output[i].text[j];
    }
    destination->sourceFileCount = source.sourceFileCount;
    destination->compiledModuleCount = source.compiledModuleCount;
    destination->cachedModuleCount = source.cachedModuleCount;
    destination->linkedModuleCount = source.linkedModuleCount;
}

static studio_v05::gx_result GX_CALL host_bare_build_start(
    studio_v05::gx_app_context* context,
    const studio_v05::gx_build_request* request,
    studio_v05::gx_build_handle* outHandle)
{
    V05AdapterState* adapter = state_for(context);
    gx_build_request converted{};
    char strings[8][kMaxStringBytes]{};
    if (!adapter) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    if (!adapter->aarch64Host->bare_metal_build_project_start) return studio_v05::GX_ERROR_UNSUPPORTED;
    if (!app_object(adapter, outHandle) || !convert_build_request(adapter, request, &converted, strings))
        return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    gx_build_handle handle = 0;
    const gx_result result = adapter->aarch64Host->bare_metal_build_project_start(
        adapter->aarch64Context, &converted, &handle);
    if (result == GX_OK) *outHandle = handle;
    return to_v05_result(result);
}

static studio_v05::gx_result GX_CALL host_bare_build_poll(
    studio_v05::gx_app_context* context, studio_v05::gx_build_handle handle,
    studio_v05::gx_build_snapshot* outSnapshot)
{
    V05AdapterState* adapter = state_for(context);
    if (!adapter) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    if (!adapter->aarch64Host->bare_metal_build_project_poll) return studio_v05::GX_ERROR_UNSUPPORTED;
    if (!handle || !app_object(adapter, outSnapshot) ||
        outSnapshot->size < sizeof(*outSnapshot) || outSnapshot->version != GX_BUILD_API_VERSION)
        return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    gx_build_snapshot snapshot{};
    snapshot.size = sizeof(snapshot);
    snapshot.version = GX_BUILD_API_VERSION;
    const gx_result result = adapter->aarch64Host->bare_metal_build_project_poll(
        adapter->aarch64Context, handle, &snapshot);
    if (result == GX_OK) copy_build_snapshot(snapshot, outSnapshot);
    return to_v05_result(result);
}

static studio_v05::gx_result GX_CALL host_bare_build_release(
    studio_v05::gx_app_context* context, studio_v05::gx_build_handle handle)
{
    V05AdapterState* adapter = state_for(context);
    if (!adapter) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    if (!adapter->aarch64Host->bare_metal_build_project_release) return studio_v05::GX_ERROR_UNSUPPORTED;
    if (!handle) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    return to_v05_result(adapter->aarch64Host->bare_metal_build_project_release(
        adapter->aarch64Context, handle));
}

static bool convert_run_request(V05AdapterState* adapter,
                                const studio_v05::gx_development_run_request* source,
                                gx_development_run_request* destination,
                                char strings[9][kMaxStringBytes])
{
    if (!source || !destination || !app_object(adapter, source) ||
        source->size < sizeof(*source) || source->version != 1u) return false;
    if (source->flags & GX_DEVELOPMENT_RUN_FLAG_DEBUG_CONTROLLED) return false;
    if (source->debugSourcePath || source->debugSourceLine || source->debugSourceColumn ||
        source->debugSourceCondition) return false;
    const char* input[7] = { source->projectRoot, source->projectId, source->projectKind,
        source->targetProfile, source->manifestPath, source->artifactPath, source->artifactSha256 };
    const char** output[7] = { &destination->projectRoot, &destination->projectId,
        &destination->projectKind, &destination->targetProfile, &destination->manifestPath,
        &destination->artifactPath, &destination->artifactSha256 };
    for (uint32_t i = 0; i < 7; ++i)
        if (!optional_app_string(adapter, input[i], strings[i], kMaxStringBytes, output[i])) return false;
    destination->size = sizeof(*destination);
    destination->version = 2u; /* AARCH64 Run API v2; SDK v0.5 is Run API v1. */
    destination->flags = source->flags;
    destination->reserved = source->reserved;
    destination->artifactSize = source->artifactSize;
    const char* architecture = nullptr;
    const char* abi = nullptr;
    if (!optional_app_string(adapter, source->artifactArchitecture, strings[7], kMaxStringBytes, &architecture) ||
        !optional_app_string(adapter, source->artifactAbi, strings[8], kMaxStringBytes, &abi)) return false;
    destination->artifactArchitecture = architecture;
    destination->artifactAbi = abi;
    return true;
}

static uint32_t to_v05_run_error(uint32_t error)
{
    /* AARCH64's stable 1..32 values match v0.5. Its 33/34 tail predates the
     * v0.5 array-bounds insertion, so move those named errors by one slot. */
    if (error == GX_DEVELOPMENT_RUN_ERROR_INVALID_POINTER_DEREFERENCE)
        return studio_v05::GX_DEVELOPMENT_RUN_ERROR_INVALID_POINTER_DEREFERENCE;
    if (error == GX_DEVELOPMENT_RUN_ERROR_POINTER_OUT_OF_BOUNDS)
        return studio_v05::GX_DEVELOPMENT_RUN_ERROR_POINTER_OUT_OF_BOUNDS;
    return error;
}

static void copy_run_snapshot(const gx_development_run_snapshot& source,
                              studio_v05::gx_development_run_snapshot* destination)
{
    destination->size = sizeof(*destination);
    destination->version = 1u;
    destination->handle = source.handle;
    destination->state = source.state;
    destination->errorCode = to_v05_run_error(source.errorCode);
    destination->processId = source.processId;
    destination->nativeRuntimeId = source.nativeRuntimeId;
    destination->windowCount = source.windowCount;
    destination->createdWindowCount = source.createdWindowCount;
    destination->exitCode = source.exitCode;
    destination->cleanupComplete = source.cleanupComplete;
    for (uint32_t i = 0; i < sizeof(destination->applicationId); ++i)
        destination->applicationId[i] = source.applicationId[i];
    for (uint32_t i = 0; i < sizeof(destination->displayName); ++i)
        destination->displayName[i] = source.displayName[i];
    for (uint32_t i = 0; i < sizeof(destination->artifactSha256); ++i)
        destination->artifactSha256[i] = source.artifactSha256[i];
    for (uint32_t i = 0; i < sizeof(destination->errorMessage); ++i)
        destination->errorMessage[i] = source.errorMessage[i];
    destination->reservedOutputAlignment = source.reservedOutputAlignment;
    destination->outputCount = source.outputCount > GX_DEVELOPMENT_RUN_MAX_OUTPUT_LINES
        ? GX_DEVELOPMENT_RUN_MAX_OUTPUT_LINES : source.outputCount;
    destination->outputTruncated = source.outputTruncated;
    for (uint32_t i = 0; i < destination->outputCount; ++i)
        for (uint32_t j = 0; j < sizeof(destination->output[i].text); ++j)
            destination->output[i].text[j] = source.output[i].text[j];
    destination->closeRequested = 0;
    destination->cancellationRequested = 0;
    destination->generation = 0;
}

static studio_v05::gx_result GX_CALL host_bare_run_prepare(
    studio_v05::gx_app_context* context,
    const studio_v05::gx_development_run_request* request,
    studio_v05::gx_development_run_handle* outHandle,
    studio_v05::gx_development_run_snapshot* outSnapshot)
{
    V05AdapterState* adapter = state_for(context);
    gx_development_run_request converted{};
    char strings[9][kMaxStringBytes]{};
    if (!adapter) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    if (!adapter->aarch64Host->bare_metal_development_run_prepare) return studio_v05::GX_ERROR_UNSUPPORTED;
    if (!app_object(adapter, outHandle) || !app_object(adapter, outSnapshot) ||
        !request || !app_object(adapter, request) || outSnapshot->size < sizeof(*outSnapshot) ||
        outSnapshot->version != 1u) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    if ((request->flags & GX_DEVELOPMENT_RUN_FLAG_DEBUG_CONTROLLED) || request->debugSourcePath ||
        request->debugSourceLine || request->debugSourceColumn || request->debugSourceCondition)
        return studio_v05::GX_ERROR_UNSUPPORTED;
    if (!convert_run_request(adapter, request, &converted, strings))
        return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    gx_development_run_handle handle = 0;
    gx_development_run_snapshot snapshot{};
    snapshot.size = sizeof(snapshot);
    snapshot.version = 2u;
    const gx_result result = adapter->aarch64Host->bare_metal_development_run_prepare(
        adapter->aarch64Context, &converted, &handle, &snapshot);
    if (result == GX_OK) {
        *outHandle = handle;
        copy_run_snapshot(snapshot, outSnapshot);
    }
    return to_v05_result(result);
}

static studio_v05::gx_result GX_CALL host_bare_run_start(
    studio_v05::gx_app_context* context, studio_v05::gx_development_run_handle handle)
{
    V05AdapterState* adapter = state_for(context);
    if (!adapter) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    if (!adapter->aarch64Host->bare_metal_development_run_start) return studio_v05::GX_ERROR_UNSUPPORTED;
    if (!handle) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    return to_v05_result(adapter->aarch64Host->bare_metal_development_run_start(
        adapter->aarch64Context, handle));
}

static studio_v05::gx_result GX_CALL host_bare_run_poll(
    studio_v05::gx_app_context* context, studio_v05::gx_development_run_handle handle,
    studio_v05::gx_development_run_snapshot* outSnapshot)
{
    V05AdapterState* adapter = state_for(context);
    if (!adapter) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    if (!adapter->aarch64Host->bare_metal_development_run_poll) return studio_v05::GX_ERROR_UNSUPPORTED;
    if (!handle || !app_object(adapter, outSnapshot) ||
        outSnapshot->size < sizeof(*outSnapshot) || outSnapshot->version != 1u)
        return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    gx_development_run_snapshot snapshot{};
    snapshot.size = sizeof(snapshot);
    snapshot.version = 2u;
    const gx_result result = adapter->aarch64Host->bare_metal_development_run_poll(
        adapter->aarch64Context, handle, &snapshot);
    if (result == GX_OK) copy_run_snapshot(snapshot, outSnapshot);
    return to_v05_result(result);
}

static studio_v05::gx_result GX_CALL host_bare_run_request_close(
    studio_v05::gx_app_context* context, studio_v05::gx_development_run_handle handle)
{
    V05AdapterState* adapter = state_for(context);
    if (!adapter) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    if (!adapter->aarch64Host->bare_metal_development_run_request_close) return studio_v05::GX_ERROR_UNSUPPORTED;
    if (!handle) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    return to_v05_result(adapter->aarch64Host->bare_metal_development_run_request_close(
        adapter->aarch64Context, handle));
}

static studio_v05::gx_result GX_CALL host_bare_run_release(
    studio_v05::gx_app_context* context, studio_v05::gx_development_run_handle handle)
{
    V05AdapterState* adapter = state_for(context);
    if (!adapter) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    if (!adapter->aarch64Host->bare_metal_development_run_release) return studio_v05::GX_ERROR_UNSUPPORTED;
    if (!handle) return studio_v05::GX_ERROR_INVALID_ARGUMENT;
    return to_v05_result(adapter->aarch64Host->bare_metal_development_run_release(
        adapter->aarch64Context, handle));
}

static void initialize_table(V05AdapterState* adapter)
{
    adapter->host = {};
    adapter->host.size = sizeof(adapter->host);
    adapter->host.version = 0;
    adapter->host.log = host_log;
    adapter->host.get_api_version = host_get_api_version;
    adapter->host.request_window = host_request_window;
    adapter->host.draw_text = host_draw_text;
    adapter->host.draw_rect = host_draw_rect;
    adapter->host.poll_event = host_poll_event;
    adapter->host.exit = host_exit;
    adapter->host.request_window_ex = host_request_window_ex;
    adapter->host.native_window_destroy = host_native_window_destroy;
    adapter->host.bare_metal_build_project_start = host_bare_build_start;
    adapter->host.bare_metal_build_project_poll = host_bare_build_poll;
    adapter->host.bare_metal_build_project_release = host_bare_build_release;
    adapter->host.bare_metal_file_stat = host_bare_file_stat;
    adapter->host.bare_metal_file_read_workspace = host_bare_file_read;
    adapter->host.bare_metal_file_list = host_bare_file_list;
    adapter->host.bare_metal_file_write_all = host_bare_file_write;
    adapter->host.bare_metal_file_create_directory = host_bare_file_create_directory;
    adapter->host.bare_metal_file_remove = host_bare_file_remove;
    adapter->host.bare_metal_development_run_prepare = host_bare_run_prepare;
    adapter->host.bare_metal_development_run_start = host_bare_run_start;
    adapter->host.bare_metal_development_run_poll = host_bare_run_poll;
    adapter->host.bare_metal_development_run_request_close = host_bare_run_request_close;
    adapter->host.bare_metal_development_run_release = host_bare_run_release;
    /* Deliberately absent: hosted filesystem/Build/Run, wait-for-close,
     * native-window create/set/run, debugger, activation and Build cancel. */
    adapter->context.size = sizeof(adapter->context);
    adapter->context.apiVersion = 0;
    adapter->context.host = &adapter->host;
    adapter->context.userData = adapter;
}

} // namespace

Profile profile_from_identity(const char* identity)
{
    if (!identity) return Profile::Unknown;
    uint32_t i = 0;
    while (identity[i] && i < 64u) ++i;
    if (i == 64u) return Profile::Unknown;
    const char* aarch64 = GX_ABI_NAME;
    uint32_t j = 0;
    while (aarch64[j] && identity[j] == aarch64[j]) ++j;
    if (aarch64[j] == '\0' && identity[j] == '\0') return Profile::Aarch64400;
    j = 0;
    while (kV05StudioAbiIdentity[j] && identity[j] == kV05StudioAbiIdentity[j]) ++j;
    if (kV05StudioAbiIdentity[j] == '\0' && identity[j] == '\0') return Profile::StudioV05_464;
    return Profile::Unknown;
}

bool identity_supported(const char* identity)
{
    return profile_from_identity(identity) != Profile::Unknown;
}

V05Adapter* create_v05_adapter(gx_app_context* aarch64Context,
                               gx_host_calls* aarch64Host,
                               gui_render::HostBinding* renderBinding,
                               const UserMemoryRange& userMemory)
{
    if (!aarch64Context || !aarch64Host || !renderBinding || s_activeAdapter ||
        aarch64Context->host != aarch64Host || !gui_render::binding_valid(renderBinding, aarch64Context) ||
        !valid_range(userMemory)) return nullptr;
    V05AdapterState* adapter = new V05AdapterState();
    if (!adapter) return nullptr;
    adapter->aarch64Context = aarch64Context;
    adapter->aarch64Host = aarch64Host;
    adapter->renderBinding = renderBinding;
    adapter->userMemory = userMemory;
    adapter->active = 1;
    initialize_table(adapter);
    s_activeAdapter = adapter;
    return reinterpret_cast<V05Adapter*>(adapter);
}

void* v05_context(V05Adapter* opaque)
{
    V05AdapterState* adapter = reinterpret_cast<V05AdapterState*>(opaque);
    return adapter && adapter->active ? &adapter->context : nullptr;
}

void* v05_host_table(V05Adapter* opaque)
{
    V05AdapterState* adapter = reinterpret_cast<V05AdapterState*>(opaque);
    return adapter && adapter->active ? &adapter->host : nullptr;
}

void deactivate_v05_adapter(V05Adapter* opaque)
{
    V05AdapterState* adapter = reinterpret_cast<V05AdapterState*>(opaque);
    if (adapter) adapter->active = 0;
}

void destroy_v05_adapter(V05Adapter* opaque)
{
    V05AdapterState* adapter = reinterpret_cast<V05AdapterState*>(opaque);
    if (!adapter) return;
    adapter->active = 0;
    if (s_activeAdapter == adapter) s_activeAdapter = nullptr;
    delete adapter;
}

} // namespace studio_abi
} // namespace kernel
