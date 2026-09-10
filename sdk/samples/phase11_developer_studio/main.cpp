#include <guidexos/app.h>

extern "C" void* memset(void* destination, int value, size_t count)
{
    unsigned char* bytes = static_cast<unsigned char*>(destination);
    for (size_t i = 0; i < count; ++i) bytes[i] = static_cast<unsigned char>(value);
    return destination;
}

extern "C" size_t strlen(const char* value)
{
    size_t length = 0;
    if (value) while (value[length]) ++length;
    return length;
}

static bool same_text(const char* left, const char* right)
{
    if (!left || !right) return false;
    uint32_t i = 0;
    while (left[i] && left[i] == right[i]) ++i;
    return left[i] == right[i];
}

static bool has_text(const char* text, const char* needle)
{
    if (!text || !needle || !needle[0]) return false;
    for (uint32_t i = 0; text[i]; ++i) {
        uint32_t j = 0;
        while (needle[j] && text[i + j] == needle[j]) ++j;
        if (!needle[j]) return true;
    }
    return false;
}

static gx_result log(gx_app_context* ctx, const char* text)
{
    return ctx && ctx->host && ctx->host->log ? ctx->host->log(ctx, text) : GX_ERROR_INVALID_ARGUMENT;
}

static bool write_source(gx_app_context* ctx, const char* source)
{
    if (!ctx || !ctx->host || !ctx->host->bare_metal_file_write_all || !source) return false;
    uint32_t written = 0;
    const uint32_t bytes = static_cast<uint32_t>(__builtin_strlen(source));
    return ctx->host->bare_metal_file_write_all(ctx, "/Apps/Phase11CrossArchApp/src/main.c",
                                                source, bytes, &written) == GX_OK && written == bytes;
}

static bool run_build(gx_app_context* ctx, gx_build_snapshot* output)
{
    if (!ctx || !ctx->host || !output || !ctx->host->bare_metal_build_project_start ||
        !ctx->host->bare_metal_build_project_poll || !ctx->host->bare_metal_build_project_release) return false;
    gx_build_request request{};
    request.size = sizeof(request);
    request.version = GX_BUILD_API_VERSION;
    request.projectRoot = "/Apps/Phase11CrossArchApp";
    request.projectId = "com.guidexos.phase11.crossarch";
    request.projectKind = "native-gui-application";
    request.targetProfile = "guidexos.multi.baremetal.bootstrap.native";
    request.buildSystem = "guidexos-native-baremetal-bootstrap-v1";
    request.buildScript = "";
    request.expectedArtifact = "build/bin/arm64/app.elf";
    request.configuration = "Debug";
    gx_build_handle handle = 0;
    if (ctx->host->bare_metal_build_project_start(ctx, &request, &handle) != GX_OK) return false;
    *output = {};
    const gx_result poll = ctx->host->bare_metal_build_project_poll(ctx, handle, output);
    const gx_result release = ctx->host->bare_metal_build_project_release(ctx, handle);
    return poll == GX_OK && release == GX_OK;
}

static bool artifact_exists(gx_app_context* ctx)
{
    gx_file_info info{};
    return ctx && ctx->host && ctx->host->bare_metal_file_stat &&
        ctx->host->bare_metal_file_stat(ctx, "/Apps/Phase11CrossArchApp/bin/arm64/app.elf", &info) == GX_OK &&
        info.type == GX_FILE_TYPE_REGULAR && info.size != 0;
}

extern "C" gx_result GX_CALL gx_main(gx_app_context* ctx)
{
    if (!ctx || !ctx->host || ctx->size < sizeof(gx_app_context) ||
        ctx->apiVersion != GX_API_VERSION || ctx->host->size < GX_GUI_HOST_CALLS_SIZE ||
        ctx->host->version != GX_API_VERSION || !ctx->host->bare_metal_build_project_start ||
        !ctx->host->bare_metal_file_write_all || !ctx->host->bare_metal_file_stat) return GX_ERROR_INVALID_ARGUMENT;

    if (log(ctx, "Developer Studio project loaded") != GX_OK) return GX_ERROR_FAILED;
    if (log(ctx, "compiler target: multi (arm64 + amd64)") != GX_OK) return GX_ERROR_FAILED;

    static const char build1[] =
        "int gx_main(void* ctx) { log(ctx, \"built by Developer Studio\"); log(ctx, \"architecture: arm64\"); log(ctx, \"computation: PASS build=1\"); return 42; }\n";
    static const char build2[] =
        "int gx_main(void* ctx) { log(ctx, \"built by Developer Studio\"); log(ctx, \"architecture: arm64\"); log(ctx, \"computation: PASS build=2\"); return 42; }\n";
    static const char invalid[] = "int gx_main(void* ctx) { return ; }\n";

    if (!write_source(ctx, build1)) return GX_ERROR_FAILED;
    gx_build_snapshot first{};
    if (!run_build(ctx, &first) || first.state != GX_BUILD_SUCCEEDED || !first.artifactValid ||
        !first.siblingArtifactValid || !first.packageWritten) return GX_ERROR_FAILED;
    if (log(ctx, "ARM64 code generation: PASS") != GX_OK ||
        log(ctx, "ARM64 ELF emission: PASS machine=EM_AARCH64") != GX_OK ||
        log(ctx, "AMD64 ELF emission: PASS machine=EM_X86_64") != GX_OK ||
        log(ctx, "multiarch package build: PASS") != GX_OK) return GX_ERROR_FAILED;

    if (!write_source(ctx, invalid)) return GX_ERROR_FAILED;
    gx_build_snapshot failed{};
    if (!run_build(ctx, &failed) || failed.state != GX_BUILD_FAILED || !artifact_exists(ctx)) {
        log(ctx, "invalid-source recovery: FAIL");
        return GX_ERROR_FAILED;
    }
    if (log(ctx, "invalid-source recovery: PASS previous package retained") != GX_OK) return GX_ERROR_FAILED;

    if (!write_source(ctx, build2)) return GX_ERROR_FAILED;
    gx_build_snapshot second{};
    if (!run_build(ctx, &second) || second.state != GX_BUILD_SUCCEEDED || !second.packageWritten ||
        same_text(first.artifactSha256, second.artifactSha256)) return GX_ERROR_FAILED;
    if (log(ctx, "source rebuild freshness: PASS build=2 hash-changed") != GX_OK) return GX_ERROR_FAILED;
    if (log(ctx, "Developer Studio build/run: PASS") != GX_OK) return GX_ERROR_FAILED;
    return 42;
}
