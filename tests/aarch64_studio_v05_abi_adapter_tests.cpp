#include "kernel/aarch64_studio_v05_abi_adapter.h"

#include <limits.h>
#include <stdint.h>
#include <cstdio>
#include <string.h>

#ifndef GXOS_STUDIO_V05_ABI_HEADER
#error GXOS_STUDIO_V05_ABI_HEADER must point at the pinned v0.5 SDK
#endif
#ifndef GXOS_STUDIO_V05_APP_HEADER
#error GXOS_STUDIO_V05_APP_HEADER must point at the pinned v0.5 SDK
#endif

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

static_assert(sizeof(gx_host_calls) == 400, "AARCH64 ABI changed");
static_assert(sizeof(studio_v05::gx_host_calls) == 464, "v0.5 ABI changed");
static_assert(offsetof(gx_host_calls, guiVersion) == 240, "AARCH64 conflict slot moved");
static_assert(offsetof(studio_v05::gx_host_calls, bare_metal_build_project_start) == 240,
              "v0.5 Build slot moved");
static_assert(offsetof(studio_v05::gx_host_calls, native_window_destroy) == 368,
              "v0.5 NativeWindow slot moved");
static_assert(offsetof(studio_v05::gx_host_calls, get_document_activation_path) == 448,
              "v0.5 activation slot moved");
static_assert(offsetof(studio_v05::gx_host_calls, bare_metal_build_project_cancel) == 456,
              "v0.5 Build cancel slot moved");

namespace {

static unsigned g_checks = 0;
static unsigned g_failures = 0;
#define CHECK(expression) do { ++g_checks; if (!(expression)) { ++g_failures; \
    std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expression); } } while (0)

struct HostHarness {
    gx_app_context context;
    gx_host_calls host;
    kernel::gui_render::SurfaceRegistry surfaces;
    kernel::gui_render::HostBinding binding;
    uint32_t invalidations;
    uint32_t calls[32];
    char lastText[256];
    gx_handle lastHandle;
    int lastX;
    int lastY;
    int lastWidth;
    int lastHeight;
    uint32_t lastFlags;
    gx_result lastExitCode;
    uint32_t listOverflow;
    uint32_t byteCountOverflow;
    gx_development_run_request lastRunRequest;
    char lastRunProjectId[256];
    char lastRunArtifactAbi[256];
};

static HostHarness* harness(gx_app_context* context)
{
    if (!context || !context->userData) return nullptr;
    kernel::gui_render::HostBinding* binding =
        static_cast<kernel::gui_render::HostBinding*>(context->userData);
    return static_cast<HostHarness*>(binding->owner);
}

static bool resolve_surface(void* owner, gx_handle window, kernel::gui_render::Surface* out)
{
    return static_cast<HostHarness*>(owner)->surfaces.resolve(window, out);
}

static void invalidate_surface(void* owner, gx_handle)
{
    ++static_cast<HostHarness*>(owner)->invalidations;
}

static gx_result GX_CALL mock_log(gx_app_context* context, const char* text)
{
    HostHarness* h = harness(context); ++h->calls[0];
    size_t n = strlen(text); if (n >= sizeof(h->lastText)) n = sizeof(h->lastText) - 1;
    memcpy(h->lastText, text, n); h->lastText[n] = '\0'; return GX_OK;
}

static uint32_t GX_CALL mock_api_version(gx_app_context*) { return 77; }

static gx_result GX_CALL mock_request_window(gx_app_context* context, const char* title,
                                               int, int, gx_handle* out)
{
    HostHarness* h = harness(context); ++h->calls[1];
    strcpy(h->lastText, title); *out = 0x123; return GX_OK;
}

static gx_result GX_CALL mock_request_window_ex(gx_app_context* context, const char* title,
                                                  int, int, uint32_t flags, gx_handle* out)
{
    HostHarness* h = harness(context); ++h->calls[2];
    strcpy(h->lastText, title); h->lastFlags = flags; *out = 0x456; return GX_OK;
}

static gx_result GX_CALL mock_poll_event(gx_app_context* context, gx_event* out, int timeout)
{
    HostHarness* h = harness(context); ++h->calls[3]; h->lastX = timeout;
    *out = {}; out->size = sizeof(*out); out->type = GX_EVENT_KEY; out->window = 0x123;
    out->param1 = 11; out->param2 = 12; out->param3 = 13; out->param4 = 14; return GX_OK;
}

static gx_result GX_CALL mock_exit(gx_app_context* context, gx_result code)
{
    HostHarness* h = harness(context); ++h->calls[4]; h->lastExitCode = code; return GX_OK;
}

static gx_result GX_CALL mock_window_destroy(gx_app_context* context, gx_handle window)
{
    HostHarness* h = harness(context); ++h->calls[5]; h->lastHandle = window; return GX_OK;
}

static gx_result GX_CALL mock_file_stat(gx_app_context* context, const char* path, gx_file_info* out)
{
    HostHarness* h = harness(context); ++h->calls[6]; strcpy(h->lastText, path);
    out->type = GX_FILE_TYPE_REGULAR; out->reserved = 0; out->size = 9; return GX_OK;
}

static gx_result GX_CALL mock_file_read(gx_app_context* context, const char* path, void* buffer,
                                          uint32_t capacity, uint32_t* bytes)
{
    HostHarness* h = harness(context); ++h->calls[7]; strcpy(h->lastText, path);
    if (h->byteCountOverflow) { *bytes = capacity + 1; return GX_OK; }
    const uint32_t count = capacity < 3 ? capacity : 3;
    if (count) memcpy(buffer, "abc", count);
    *bytes = count;
    return GX_OK;
}

static gx_result GX_CALL mock_file_list(gx_app_context* context, const char* path,
                                          gx_file_entry* entries, uint32_t capacity,
                                          uint32_t* count, uint32_t* truncated)
{
    HostHarness* h = harness(context); ++h->calls[8]; strcpy(h->lastText, path);
    if (h->listOverflow) { *count = capacity + 1; *truncated = 1; return GX_OK; }
    if (capacity) {
        entries[0] = {}; entries[0].type = GX_FILE_TYPE_REGULAR; entries[0].size = 9;
        strcpy(entries[0].name, "file.txt"); *count = 1; *truncated = 0;
    } else { *count = 0; *truncated = 0; }
    return GX_OK;
}

static gx_result GX_CALL mock_file_write(gx_app_context* context, const char* path,
                                           const void* buffer, uint32_t capacity, uint32_t* bytes)
{
    HostHarness* h = harness(context); ++h->calls[9]; strcpy(h->lastText, path);
    if (capacity) { const char* text = static_cast<const char*>(buffer); h->lastX = text[0]; }
    *bytes = capacity; return GX_OK;
}

static gx_result GX_CALL mock_file_mkdir(gx_app_context* context, const char* path)
{
    HostHarness* h = harness(context); ++h->calls[10]; strcpy(h->lastText, path); return GX_OK;
}

static gx_result GX_CALL mock_file_remove(gx_app_context* context, const char* path)
{
    HostHarness* h = harness(context); ++h->calls[11]; strcpy(h->lastText, path); return GX_OK;
}

static gx_result GX_CALL mock_build_start(gx_app_context* context, const gx_build_request* request,
                                            gx_build_handle* out)
{
    HostHarness* h = harness(context); ++h->calls[12];
    strcpy(h->lastText, request->projectId); *out = 0x44; return GX_OK;
}

static gx_result GX_CALL mock_build_poll(gx_app_context* context, gx_build_handle handle,
                                           gx_build_snapshot* out)
{
    HostHarness* h = harness(context); ++h->calls[13]; h->lastHandle = handle;
    out->state = GX_BUILD_SUCCEEDED; out->sourceFileCount = 3; out->compiledModuleCount = 2;
    out->outputCount = 1; out->output[0].stream = 1; strcpy(out->output[0].text, "built");
    out->artifactValid = 1; out->artifactSize = 123; strcpy(out->artifactPath, "bin/app.elf");
    return GX_OK;
}

static gx_result GX_CALL mock_build_release(gx_app_context* context, gx_build_handle handle)
{
    HostHarness* h = harness(context); ++h->calls[14]; h->lastHandle = handle; return GX_OK;
}

static gx_result GX_CALL mock_run_prepare(gx_app_context* context,
                                            const gx_development_run_request* request,
                                            gx_development_run_handle* outHandle,
                                            gx_development_run_snapshot* out)
{
    HostHarness* h = harness(context); ++h->calls[15]; h->lastRunRequest = *request;
    strcpy(h->lastRunProjectId, request->projectId);
    strcpy(h->lastRunArtifactAbi, request->artifactAbi);
    *outHandle = 0x55; out->handle = 0x55; out->state = GX_DEVELOPMENT_RUN_FAILED;
    out->errorCode = GX_DEVELOPMENT_RUN_ERROR_INVALID_POINTER_DEREFERENCE;
    out->outputCount = 1; strcpy(out->output[0].text, "run prepared"); out->capabilities = 0xffffffff;
    out->reservedV2 = 0xffffffff; return GX_OK;
}

static gx_result GX_CALL mock_run_start(gx_app_context* context, gx_development_run_handle handle)
{
    HostHarness* h = harness(context); ++h->calls[16]; h->lastHandle = handle; return GX_OK;
}

static gx_result GX_CALL mock_run_poll(gx_app_context* context, gx_development_run_handle handle,
                                        gx_development_run_snapshot* out)
{
    HostHarness* h = harness(context); ++h->calls[17]; h->lastHandle = handle;
    out->state = GX_DEVELOPMENT_RUN_COMPLETED; out->errorCode = GX_DEVELOPMENT_RUN_ERROR_POINTER_OUT_OF_BOUNDS;
    out->outputCount = 1; strcpy(out->output[0].text, "run polled"); return GX_OK;
}

static gx_result GX_CALL mock_run_close(gx_app_context* context, gx_development_run_handle handle)
{
    HostHarness* h = harness(context); ++h->calls[18]; h->lastHandle = handle; return GX_OK;
}

static gx_result GX_CALL mock_run_release(gx_app_context* context, gx_development_run_handle handle)
{
    HostHarness* h = harness(context); ++h->calls[19]; h->lastHandle = handle; return GX_OK;
}

static void initialize(HostHarness* h)
{
    h->context = {}; h->host = {}; h->binding = {};
    h->host.size = sizeof(h->host); h->host.version = GX_API_VERSION;
    h->host.log = mock_log; h->host.get_api_version = mock_api_version;
    h->host.request_window = mock_request_window; h->host.request_window_ex = mock_request_window_ex;
    h->host.poll_event = mock_poll_event; h->host.exit = mock_exit; h->host.window_destroy = mock_window_destroy;
    h->host.bare_metal_file_stat = mock_file_stat; h->host.bare_metal_file_read_workspace = mock_file_read;
    h->host.bare_metal_file_list = mock_file_list; h->host.bare_metal_file_write_all = mock_file_write;
    h->host.bare_metal_file_create_directory = mock_file_mkdir; h->host.bare_metal_file_remove = mock_file_remove;
    h->host.bare_metal_build_project_start = mock_build_start;
    h->host.bare_metal_build_project_poll = mock_build_poll;
    h->host.bare_metal_build_project_release = mock_build_release;
    h->host.bare_metal_development_run_prepare = mock_run_prepare;
    h->host.bare_metal_development_run_start = mock_run_start;
    h->host.bare_metal_development_run_poll = mock_run_poll;
    h->host.bare_metal_development_run_request_close = mock_run_close;
    h->host.bare_metal_development_run_release = mock_run_release;
    h->context.size = sizeof(h->context); h->context.apiVersion = GX_API_VERSION;
    h->context.host = &h->host; h->context.userData = &h->binding;
    h->binding.runtime = h; h->binding.context = &h->context; h->binding.host = &h->host;
    h->binding.owner = h; h->binding.resolveSurface = resolve_surface;
    h->binding.invalidateSurface = invalidate_surface; h->binding.active = 1;
}

} // namespace

int main()
{
    CHECK(kernel::studio_abi::profile_from_identity(GX_ABI_NAME) == kernel::studio_abi::Profile::Aarch64400);
    CHECK(kernel::studio_abi::profile_from_identity(kernel::studio_abi::kV05StudioAbiIdentity) ==
          kernel::studio_abi::Profile::StudioV05_464);
    CHECK(kernel::studio_abi::identity_supported(GX_ABI_NAME));
    CHECK(kernel::studio_abi::identity_supported(kernel::studio_abi::kV05StudioAbiIdentity));
    CHECK(kernel::studio_abi::profile_from_identity(nullptr) == kernel::studio_abi::Profile::Unknown);
    CHECK(kernel::studio_abi::profile_from_identity("guidexos-c-abi-v2") == kernel::studio_abi::Profile::Unknown);
    char overlongIdentity[66]; memset(overlongIdentity, 'x', sizeof(overlongIdentity)); overlongIdentity[65] = '\0';
    CHECK(kernel::studio_abi::profile_from_identity(overlongIdentity) == kernel::studio_abi::Profile::Unknown);

    HostHarness h{}; initialize(&h);
    CHECK(h.surfaces.create(0x123, 16, 16));
    uintptr_t stackMarker = 0;
    const uintptr_t stackCenter = reinterpret_cast<uintptr_t>(&stackMarker);
    const kernel::studio_abi::UserMemoryRange memory = {
        UINT64_C(0x100000000000), 1,
        static_cast<uint64_t>(stackCenter - 1024u * 1024u), 2u * 1024u * 1024u
    };
    CHECK(kernel::gui_render::binding_valid(&h.binding, &h.context));
    kernel::studio_abi::V05Adapter* adapter = kernel::studio_abi::create_v05_adapter(
        &h.context, &h.host, &h.binding, memory);
    CHECK(adapter != nullptr);
    CHECK(kernel::studio_abi::create_v05_adapter(&h.context, &h.host, &h.binding, memory) == nullptr);
    studio_v05::gx_app_context* context = static_cast<studio_v05::gx_app_context*>(
        kernel::studio_abi::v05_context(adapter));
    studio_v05::gx_host_calls* host = static_cast<studio_v05::gx_host_calls*>(
        kernel::studio_abi::v05_host_table(adapter));
    CHECK(context && host);
    CHECK(context->size == sizeof(*context) && context->apiVersion == 0);
    CHECK(context->host == host && context->userData != nullptr);
    CHECK(host->size == 464 && host->version == 0);
    CHECK(host->log && host->get_api_version && host->request_window && host->draw_text && host->draw_rect);
    CHECK(host->poll_event && host->exit && host->request_window_ex && host->native_window_destroy);
    CHECK(host->bare_metal_build_project_start && host->bare_metal_build_project_poll && host->bare_metal_build_project_release);
    CHECK(host->bare_metal_file_stat && host->bare_metal_file_read_workspace && host->bare_metal_file_list);
    CHECK(host->bare_metal_file_write_all && host->bare_metal_file_create_directory && host->bare_metal_file_remove);
    CHECK(host->bare_metal_development_run_prepare && host->bare_metal_development_run_start &&
          host->bare_metal_development_run_poll && host->bare_metal_development_run_request_close &&
          host->bare_metal_development_run_release);
    CHECK(!host->wait_for_close && !host->file_read_all && !host->file_exists && !host->present_frame);
    CHECK(!host->file_stat && !host->file_read_workspace && !host->file_list && !host->file_write_all);
    CHECK(!host->build_project_start && !host->development_run_prepare && !host->development_debug);
    CHECK(!host->native_window_create && !host->native_window_set_text && !host->native_window_run);
    CHECK(!host->bare_metal_development_run_cancel && !host->bare_metal_development_debug);
    CHECK(!host->get_document_activation_path && !host->bare_metal_build_project_cancel);
    CHECK(host->get_api_version(context) == 77);

    char message[] = "adapter log";
    CHECK(host->log(context, message) == studio_v05::GX_OK && h.calls[0] == 1);
    CHECK(strcmp(h.lastText, message) == 0);
    CHECK(host->log(reinterpret_cast<studio_v05::gx_app_context*>(1), message) == studio_v05::GX_ERROR_INVALID_ARGUMENT);
    CHECK(host->log(context, reinterpret_cast<const char*>(1)) == studio_v05::GX_ERROR_INVALID_ARGUMENT);
    char longText[256]; memset(longText, 'x', sizeof(longText));
    CHECK(host->log(context, longText) == studio_v05::GX_ERROR_INVALID_ARGUMENT);

    char title[] = "Studio"; studio_v05::gx_handle window = 0;
    CHECK(host->request_window(context, title, 640, 360, &window) == studio_v05::GX_OK && window == 0x123);
    CHECK(h.calls[1] == 1 && strcmp(h.lastText, title) == 0);
    CHECK(host->request_window_ex(context, title, 640, 360, 7, &window) == studio_v05::GX_OK && window == 0x456);
    CHECK(h.calls[2] == 1 && h.lastFlags == 7);
    CHECK(host->request_window(context, title, 10, 10, reinterpret_cast<studio_v05::gx_handle*>(1)) ==
          studio_v05::GX_ERROR_INVALID_ARGUMENT);
    CHECK(host->native_window_destroy(context, 0x123) == studio_v05::GX_OK && h.lastHandle == 0x123);
    CHECK(host->native_window_destroy(context, 0) == studio_v05::GX_ERROR_INVALID_ARGUMENT);

    CHECK(host->draw_rect(context, 0x123, -2, -2, 4, 4, 0x00112233) == studio_v05::GX_OK);
    kernel::gui_render::Surface surface{}; CHECK(h.surfaces.resolve(0x123, &surface));
    CHECK(surface.pixels[0] == 0xff112233 && surface.pixels[1] == 0xff112233 &&
          surface.pixels[surface.stridePixels] == 0xff112233 && surface.pixels[surface.stridePixels + 1] == 0xff112233);
    CHECK(h.invalidations == 1 && h.surfaces.guards_intact(0x123));
    char glyph[] = "A";
    CHECK(host->draw_text(context, 0x123, 3, 3, glyph) == studio_v05::GX_OK);
    CHECK(h.invalidations == 2 && h.surfaces.guards_intact(0x123));
    CHECK(host->draw_rect(context, 0x123, INT_MAX - 1, INT_MIN, 100, 4, 0) == studio_v05::GX_OK);
    CHECK(host->draw_rect(context, 0x999, 0, 0, 4, 4, 0) == studio_v05::GX_ERROR_INVALID_ARGUMENT);
    CHECK(host->draw_text(context, 0x123, 0, 0, reinterpret_cast<const char*>(1)) == studio_v05::GX_ERROR_INVALID_ARGUMENT);
    h.binding.active = 0;
    CHECK(host->draw_rect(context, 0x123, 0, 0, 2, 2, 0) == studio_v05::GX_ERROR_UNSUPPORTED);
    h.binding.active = 1;

    studio_v05::gx_event event{};
    CHECK(host->poll_event(context, &event, 19) == studio_v05::GX_OK);
    CHECK(h.calls[3] == 1 && h.lastX == 19 && event.size == sizeof(event));
    CHECK(event.type == studio_v05::GX_EVENT_KEY && event.window == 0x123 &&
          event.param1 == 11 && event.param2 == 12 && event.param3 == 13 && event.param4 == 14);
    CHECK(host->poll_event(context, reinterpret_cast<studio_v05::gx_event*>(1), 0) ==
          studio_v05::GX_ERROR_INVALID_ARGUMENT);
    CHECK(host->exit(context, studio_v05::GX_ERROR_NOT_FOUND) == studio_v05::GX_OK);
    CHECK(h.lastExitCode == GX_ERROR_FAILED);

    char path[] = "/Apps/DeveloperStudio/app.json";
    studio_v05::gx_file_info info{};
    CHECK(host->bare_metal_file_stat(context, path, &info) == studio_v05::GX_OK);
    CHECK(info.type == GX_FILE_TYPE_REGULAR && info.size == 9 && strcmp(h.lastText, path) == 0);
    char fileBytes[8]{}; uint32_t bytes = 0;
    CHECK(host->bare_metal_file_read_workspace(context, path, fileBytes, sizeof(fileBytes), &bytes) == studio_v05::GX_OK);
    CHECK(bytes == 3 && memcmp(fileBytes, "abc", 3) == 0);
    studio_v05::gx_file_entry entries[2]{}; uint32_t count = 0; uint32_t truncated = 0;
    CHECK(host->bare_metal_file_list(context, path, entries, 2, &count, &truncated) == studio_v05::GX_OK);
    CHECK(count == 1 && truncated == 0 && strcmp(entries[0].name, "file.txt") == 0 && entries[0].size == 9);
    char writeBytes[] = "write"; bytes = 0;
    CHECK(host->bare_metal_file_write_all(context, path, writeBytes, 5, &bytes) == studio_v05::GX_OK);
    CHECK(bytes == 5 && h.lastX == 'w');
    char directory[] = "/Apps/DeveloperStudio/new";
    CHECK(host->bare_metal_file_create_directory(context, directory) == studio_v05::GX_OK);
    CHECK(host->bare_metal_file_remove(context, path) == studio_v05::GX_OK);
    CHECK(host->bare_metal_file_stat(context, path, reinterpret_cast<studio_v05::gx_file_info*>(1)) ==
          studio_v05::GX_ERROR_INVALID_ARGUMENT);
    CHECK(host->bare_metal_file_stat(context, reinterpret_cast<const char*>(1), &info) ==
          studio_v05::GX_ERROR_INVALID_ARGUMENT);
    CHECK(host->bare_metal_file_read_workspace(context, path, reinterpret_cast<void*>(1), 4u * 1024u * 1024u + 1u, &bytes) ==
          studio_v05::GX_ERROR_INVALID_ARGUMENT);
    h.byteCountOverflow = 1;
    CHECK(host->bare_metal_file_read_workspace(context, path, fileBytes, 2, &bytes) == studio_v05::GX_ERROR_INTERNAL);
    h.byteCountOverflow = 0;
    h.listOverflow = 1;
    CHECK(host->bare_metal_file_list(context, path, entries, 2, &count, &truncated) == studio_v05::GX_ERROR_FAILED);
    h.listOverflow = 0;
    CHECK(host->bare_metal_file_list(context, path, entries, 65, &count, &truncated) ==
          studio_v05::GX_ERROR_INVALID_ARGUMENT);
    gx_result (GX_CALL *savedStat)(gx_app_context*, const char*, gx_file_info*) = h.host.bare_metal_file_stat;
    h.host.bare_metal_file_stat = nullptr;
    CHECK(host->bare_metal_file_stat(context, path, &info) == studio_v05::GX_ERROR_UNSUPPORTED);
    h.host.bare_metal_file_stat = savedStat;
    CHECK(host->bare_metal_file_stat(context, path, &info) == studio_v05::GX_OK);
    CHECK(h.calls[6] == 2);

    studio_v05::gx_build_request build{}; build.size = sizeof(build); build.version = GX_BUILD_API_VERSION;
    char projectRoot[] = "/Apps/DeveloperStudio"; char projectId[] = "com.guidexos.developerstudio";
    char projectKind[] = "native-gui-application"; char target[] = "guidexos.arm64.baremetal.bootstrap.native";
    char buildSystem[] = "guidexos-native-baremetal-bootstrap-v1"; char script[] = "build.ps1";
    char expected[] = "build/bin/arm64/app.elf"; char configuration[] = "Debug";
    build.projectRoot = projectRoot; build.projectId = projectId; build.projectKind = projectKind;
    build.targetProfile = target; build.buildSystem = buildSystem; build.buildScript = script;
    build.expectedArtifact = expected; build.configuration = configuration;
    studio_v05::gx_build_handle buildHandle = 0;
    CHECK(host->bare_metal_build_project_start(context, &build, &buildHandle) == studio_v05::GX_OK);
    CHECK(buildHandle == 0x44 && h.calls[12] == 1 && strcmp(h.lastText, projectId) == 0);
    studio_v05::gx_build_snapshot buildSnapshot{}; buildSnapshot.size = sizeof(buildSnapshot);
    buildSnapshot.version = GX_BUILD_API_VERSION;
    CHECK(host->bare_metal_build_project_poll(context, buildHandle, &buildSnapshot) == studio_v05::GX_OK);
    CHECK(buildSnapshot.state == GX_BUILD_SUCCEEDED && buildSnapshot.sourceFileCount == 3 &&
          buildSnapshot.compiledModuleCount == 2 && buildSnapshot.outputCount == 1);
    CHECK(strcmp(buildSnapshot.output[0].text, "built") == 0 &&
          strcmp(buildSnapshot.artifactPath, "bin/app.elf") == 0 && buildSnapshot.artifactSize == 123);
    CHECK(host->bare_metal_build_project_poll(context, buildHandle,
          reinterpret_cast<studio_v05::gx_build_snapshot*>(1)) == studio_v05::GX_ERROR_INVALID_ARGUMENT);
    buildSnapshot.size = sizeof(buildSnapshot) - 1;
    CHECK(host->bare_metal_build_project_poll(context, buildHandle, &buildSnapshot) ==
          studio_v05::GX_ERROR_INVALID_ARGUMENT);
    CHECK(host->bare_metal_build_project_release(context, buildHandle) == studio_v05::GX_OK);
    CHECK(h.calls[13] == 1 && h.calls[14] == 1);
    build.projectId = reinterpret_cast<const char*>(1);
    CHECK(host->bare_metal_build_project_start(context, &build, &buildHandle) ==
          studio_v05::GX_ERROR_INVALID_ARGUMENT);

    studio_v05::gx_development_run_request run{}; run.size = sizeof(run); run.version = 1;
    char runRoot[] = "/Apps/DeveloperStudio"; char runId[] = "com.guidexos.developerstudio";
    char runKind[] = "native-gui-application"; char runTarget[] = "guidexos.arm64.baremetal.bootstrap.native";
    char manifest[] = "/Apps/DeveloperStudio/app.json"; char artifact[] = "bin/arm64/developerstudio.elf";
    char digest[] = "abcdef"; char architecture[] = "arm64"; char abi[] = "guidexos-c-abi-v1-studio-v05-464";
    run.projectRoot = runRoot; run.projectId = runId; run.projectKind = runKind; run.targetProfile = runTarget;
    run.manifestPath = manifest; run.artifactPath = artifact; run.artifactSha256 = digest;
    run.artifactSize = 777; run.artifactArchitecture = architecture; run.artifactAbi = abi;
    studio_v05::gx_development_run_handle runHandle = 0;
    studio_v05::gx_development_run_snapshot runSnapshot{}; runSnapshot.size = sizeof(runSnapshot); runSnapshot.version = 1;
    CHECK(host->bare_metal_development_run_prepare(context, &run, &runHandle, &runSnapshot) == studio_v05::GX_OK);
    CHECK(runHandle == 0x55 && h.calls[15] == 1 && h.lastRunRequest.version == 2 &&
          h.lastRunRequest.size == sizeof(gx_development_run_request));
    CHECK(strcmp(h.lastRunProjectId, runId) == 0 && strcmp(h.lastRunArtifactAbi, abi) == 0 &&
          h.lastRunRequest.artifactSize == 777 && strcmp(runSnapshot.output[0].text, "run prepared") == 0);
    CHECK(runSnapshot.version == 1 && runSnapshot.errorCode == studio_v05::GX_DEVELOPMENT_RUN_ERROR_INVALID_POINTER_DEREFERENCE);
    CHECK(runSnapshot.closeRequested == 0 && runSnapshot.cancellationRequested == 0 && runSnapshot.generation == 0);
    CHECK(host->bare_metal_development_run_start(context, runHandle) == studio_v05::GX_OK);
    runSnapshot.size = sizeof(runSnapshot); runSnapshot.version = 1;
    CHECK(host->bare_metal_development_run_poll(context, runHandle, &runSnapshot) == studio_v05::GX_OK);
    CHECK(runSnapshot.state == GX_DEVELOPMENT_RUN_COMPLETED &&
          runSnapshot.errorCode == studio_v05::GX_DEVELOPMENT_RUN_ERROR_POINTER_OUT_OF_BOUNDS &&
          strcmp(runSnapshot.output[0].text, "run polled") == 0);
    CHECK(host->bare_metal_development_run_request_close(context, runHandle) == studio_v05::GX_OK);
    CHECK(host->bare_metal_development_run_release(context, runHandle) == studio_v05::GX_OK);
    CHECK(h.calls[16] == 1 && h.calls[17] == 1 && h.calls[18] == 1 && h.calls[19] == 1);
    run.flags = GX_DEVELOPMENT_RUN_FLAG_DEBUG_CONTROLLED;
    runSnapshot.size = sizeof(runSnapshot); runSnapshot.version = 1;
    CHECK(host->bare_metal_development_run_prepare(context, &run, &runHandle, &runSnapshot) ==
          studio_v05::GX_ERROR_UNSUPPORTED);
    run.flags = 0; run.debugSourcePath = projectRoot;
    CHECK(host->bare_metal_development_run_prepare(context, &run, &runHandle, &runSnapshot) ==
          studio_v05::GX_ERROR_UNSUPPORTED);
    run.debugSourcePath = nullptr;
    runSnapshot.size = sizeof(runSnapshot) - 1;
    CHECK(host->bare_metal_development_run_prepare(context, &run, &runHandle, &runSnapshot) ==
          studio_v05::GX_ERROR_INVALID_ARGUMENT);
    CHECK(host->bare_metal_development_run_poll(context, runHandle,
          reinterpret_cast<studio_v05::gx_development_run_snapshot*>(1)) == studio_v05::GX_ERROR_INVALID_ARGUMENT);

    kernel::studio_abi::deactivate_v05_adapter(adapter);
    CHECK(host->log(context, message) == studio_v05::GX_ERROR_INVALID_ARGUMENT);
    CHECK(kernel::studio_abi::v05_context(adapter) == nullptr && kernel::studio_abi::v05_host_table(adapter) == nullptr);
    kernel::studio_abi::destroy_v05_adapter(adapter);
    kernel::studio_abi::V05Adapter* recreated = kernel::studio_abi::create_v05_adapter(
        &h.context, &h.host, &h.binding, memory);
    CHECK(recreated != nullptr);
    kernel::studio_abi::V05Adapter* second = kernel::studio_abi::create_v05_adapter(
        &h.context, &h.host, &h.binding, memory);
    CHECK(second == nullptr);
    kernel::studio_abi::destroy_v05_adapter(recreated);

    std::printf("aarch64StudioV05AdapterChecks=%u/%u\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
