#pragma once

#include "types.h"
#include "build.h"
#include "development_run.h"
#include "development_debug.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GX_API_VERSION 0u
#define GX_ABI_NAME "guidexos-c-abi-v1"

#if defined(__x86_64__)
#define GX_CALL __attribute__((ms_abi))
#else
#define GX_CALL
#endif

typedef struct gx_app_context gx_app_context;

typedef enum gx_event_type {
    GX_EVENT_NONE = 0,
    GX_EVENT_WINDOW_CLOSE = 1,
    GX_EVENT_WINDOW_FOCUS = 2,
    GX_EVENT_WINDOW_BLUR = 3,
    GX_EVENT_KEY = 4,
    GX_EVENT_MOUSE = 5,
    GX_EVENT_WINDOW_PAINT = 6
} gx_event_type;

enum {
    GX_KEY_ACTION_UP = 0,
    GX_KEY_ACTION_DOWN = 1,
    GX_KEY_MOD_SHIFT = 1,
    GX_KEY_MOD_CTRL = 2,
    GX_KEY_MOD_ALT = 4
};

/* Keyboard values use the guideXOS virtual-key contract shared by hosted and
 * Native ELF input translation. */
enum {
    GX_KEY_LEFT = 37,
    GX_KEY_UP = 38,
    GX_KEY_RIGHT = 39,
    GX_KEY_DOWN = 40
};

enum {
    GX_MOUSE_ACTION_MOVE = 0,
    GX_MOUSE_ACTION_DOWN = 1,
    GX_MOUSE_ACTION_UP = 2,
    GX_MOUSE_ACTION_DOUBLE_CLICK = 3,
    GX_MOUSE_ACTION_WHEEL = 4,
    GX_MOUSE_BUTTON_NONE = 0,
    GX_MOUSE_BUTTON_LEFT = 1,
    GX_MOUSE_BUTTON_RIGHT = 2,
    GX_MOUSE_BUTTON_MIDDLE = 3
};

#define GX_MOUSE_PACK(button, action) ((((button) & 0xFFFF) << 16) | ((action) & 0xFFFF))
#define GX_MOUSE_ACTION(value) ((value) & 0xFFFF)
#define GX_MOUSE_BUTTON(value) (((value) >> 16) & 0xFFFF)

typedef struct gx_event {
    uint32_t size;
    gx_event_type type;
    gx_handle window;
    int param1;
    int param2;
    int param3;
    int param4;
} gx_event;

enum {
    GX_FILE_TYPE_UNKNOWN = 0,
    GX_FILE_TYPE_REGULAR = 1,
    GX_FILE_TYPE_DIRECTORY = 2
};

typedef struct gx_file_info {
    uint32_t type;
    uint32_t reserved;
    uint64_t size;
} gx_file_info;

typedef struct gx_file_entry {
    uint32_t type;
    uint32_t reserved;
    uint64_t size;
    char name[128];
} gx_file_entry;

typedef struct gx_host_calls {
    uint32_t size;
    uint32_t version;
    gx_result (GX_CALL *log)(gx_app_context* ctx, const char* message);
    uint32_t (GX_CALL *get_api_version)(gx_app_context* ctx);
    gx_result (GX_CALL *request_window)(gx_app_context* ctx, const char* title, int width, int height, gx_handle* outWindow);
    gx_result (GX_CALL *draw_text)(gx_app_context* ctx, gx_handle window, int x, int y, const char* text);
    gx_result (GX_CALL *draw_rect)(gx_app_context* ctx, gx_handle window, int x, int y, int width, int height, uint32_t color);
    gx_result (GX_CALL *wait_for_close)(gx_app_context* ctx, gx_handle window, int timeoutMs);
    gx_result (GX_CALL *poll_event)(gx_app_context* ctx, gx_event* outEvent, int timeoutMs);
    gx_result (GX_CALL *exit)(gx_app_context* ctx, gx_result exitCode);
    gx_result (GX_CALL *file_read_all)(gx_app_context* ctx, const char* path, void* buffer, uint32_t bufferSize, uint32_t* outBytesRead);
    gx_result (GX_CALL *file_exists)(gx_app_context* ctx, const char* path, uint32_t* outExists);
    /* v1 extensions are appended so existing v1 tables keep their layout. */
    gx_result (GX_CALL *request_window_ex)(gx_app_context* ctx, const char* title, int width, int height, uint32_t flags, gx_handle* outWindow);
    gx_result (GX_CALL *file_read)(gx_app_context* ctx, const char* path, uint64_t offset, void* buffer, uint32_t bufferSize, uint32_t* outBytesRead);
    gx_result (GX_CALL *present_frame)(gx_app_context* ctx, gx_handle window, int x, int y, int width, int height, uint32_t strideBytes, uint32_t pixelFormat, const void* pixels, uint32_t pixelBytes);
    /* Monotonic milliseconds since the hosted runtime's process-local epoch.
     * The uint64_t value wraps after 2^64 milliseconds. */
    uint64_t (GX_CALL *get_ticks_ms)(gx_app_context* ctx);
    /* Hosted-development workspace extensions. Paths are explicit absolute
     * UTF-8 host paths; the Server rejects traversal/device/symlink paths but
     * does not know an application's selected workspace root. Callers must
     * enforce that root boundary. These slots are appended for ABI stability
     * and are not a bare-metal/VFS path contract. */
    gx_result (GX_CALL *file_stat)(gx_app_context* ctx, const char* path, gx_file_info* outInfo);
    gx_result (GX_CALL *file_read_workspace)(gx_app_context* ctx, const char* path, void* buffer, uint32_t bufferSize, uint32_t* outBytesRead);
    gx_result (GX_CALL *file_list)(gx_app_context* ctx, const char* path, gx_file_entry* entries, uint32_t capacity, uint32_t* outCount, uint32_t* outTruncated);
    gx_result (GX_CALL *file_write_all)(gx_app_context* ctx, const char* path, const void* buffer, uint32_t bufferSize, uint32_t* outBytesWritten);
    gx_result (GX_CALL *file_create_directory)(gx_app_context* ctx, const char* path);
    gx_result (GX_CALL *file_remove)(gx_app_context* ctx, const char* path);
    /* Hosted-development build service. These slots are append-only and are
     * not a production or bare-metal build API. */
    gx_result (GX_CALL *build_project_start)(gx_app_context* ctx, const gx_build_request* request, gx_build_handle* outHandle);
    gx_result (GX_CALL *build_project_poll)(gx_app_context* ctx, gx_build_handle handle, gx_build_snapshot* outSnapshot);
    gx_result (GX_CALL *build_project_release)(gx_app_context* ctx, gx_build_handle handle);
    /* Hosted-development Run Project calls. These slots are append-only and
     * are not a generic package registration or arbitrary ELF launch API. */
    gx_result (GX_CALL *development_run_prepare)(gx_app_context* ctx, const gx_development_run_request* request, gx_development_run_handle* outHandle, gx_development_run_snapshot* outSnapshot);
    gx_result (GX_CALL *development_run_start)(gx_app_context* ctx, gx_development_run_handle handle);
    gx_result (GX_CALL *development_run_poll)(gx_app_context* ctx, gx_development_run_handle handle, gx_development_run_snapshot* outSnapshot);
    gx_result (GX_CALL *development_run_request_close)(gx_app_context* ctx, gx_development_run_handle handle);
    gx_result (GX_CALL *development_run_release)(gx_app_context* ctx, gx_development_run_handle handle);
    /* Hosted-development software breakpoint operations. This is appended to
     * preserve every existing host-call slot and is not a bare-metal ABI. */
    gx_result (GX_CALL *development_debug)(gx_app_context* ctx, const gx_development_debug_request* request, gx_development_debug_snapshot* outSnapshot);
    /* Bare-metal Developer Studio build service. These slots are appended
     * after every existing v1 slot. A NativeElf runtime advertises them only
     * when the kernel-side VFS compiler is available; callers must gate every
     * dereference by host-call-table size. */
    gx_result (GX_CALL *bare_metal_build_project_start)(gx_app_context* ctx, const gx_build_request* request, gx_build_handle* outHandle);
    gx_result (GX_CALL *bare_metal_build_project_poll)(gx_app_context* ctx, gx_build_handle handle, gx_build_snapshot* outSnapshot);
    gx_result (GX_CALL *bare_metal_build_project_release)(gx_app_context* ctx, gx_build_handle handle);
    /* Bare-metal VFS workspace operations. These are distinct from the
     * hosted absolute-path workspace extensions above. */
    gx_result (GX_CALL *bare_metal_file_stat)(gx_app_context* ctx, const char* path, gx_file_info* outInfo);
    gx_result (GX_CALL *bare_metal_file_read_workspace)(gx_app_context* ctx, const char* path, void* buffer, uint32_t bufferSize, uint32_t* outBytesRead);
    gx_result (GX_CALL *bare_metal_file_list)(gx_app_context* ctx, const char* path, gx_file_entry* entries, uint32_t capacity, uint32_t* outCount, uint32_t* outTruncated);
    gx_result (GX_CALL *bare_metal_file_write_all)(gx_app_context* ctx, const char* path, const void* buffer, uint32_t bufferSize, uint32_t* outBytesWritten);
    gx_result (GX_CALL *bare_metal_file_create_directory)(gx_app_context* ctx, const char* path);
    gx_result (GX_CALL *bare_metal_file_remove)(gx_app_context* ctx, const char* path);
} gx_host_calls;

#ifdef __cplusplus
}
#endif
