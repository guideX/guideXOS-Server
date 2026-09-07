//
// Bounded bare-metal NativeElf development application registration.
//

#include "native_elf_development_app_model.h"

namespace kernel {
namespace native_elf {
namespace NativeElfDevelopmentAppModel {
namespace {

static Registration s_registration = {};
static bool s_active = false;

static uint32_t text_length(const char* text, uint32_t capacity)
{
    if (!text) return 0;
    uint32_t length = 0;
    while (length < capacity && text[length] != '\0') ++length;
    return length;
}

static bool copy_text(char* destination, uint32_t capacity, const char* source)
{
    if (!destination || capacity == 0 || !source) return false;
    uint32_t i = 0;
    while (i + 1 < capacity && source[i] != '\0') {
        destination[i] = source[i];
        ++i;
    }
    if (source[i] != '\0') {
        destination[0] = '\0';
        return false;
    }
    destination[i] = '\0';
    return true;
}

static bool equal_text(const char* left, const char* right)
{
    if (!left || !right) return left == right;
    uint32_t i = 0;
    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) return false;
        ++i;
    }
    return left[i] == right[i];
}

static bool safe_relative_path(const char* value)
{
    if (!value || value[0] == '\0' || value[0] == '/' || value[0] == '\\' || value[1] == ':') return false;
    const uint32_t length = text_length(value, GX_DEVELOPMENT_RUN_MAX_PATH_BYTES);
    if (length == 0 || length >= GX_DEVELOPMENT_RUN_MAX_PATH_BYTES) return false;
    uint32_t segmentStart = 0;
    for (uint32_t i = 0; i <= length; ++i) {
        if (i < length && value[i] != '/' && value[i] != '\\') {
            if (static_cast<unsigned char>(value[i]) < 0x20) return false;
            continue;
        }
        const uint32_t segmentLength = i - segmentStart;
        if (segmentLength == 0 ||
            (segmentLength == 1 && value[segmentStart] == '.') ||
            (segmentLength == 2 && value[segmentStart] == '.' && value[segmentStart + 1] == '.')) return false;
        segmentStart = i + 1;
    }
    return true;
}

static bool safe_application_id(const char* value)
{
    static const char kDevelopmentPrefix[] = "dev.guidexos.";
    if (!value || value[0] == '\0') return false;
    const uint32_t length = text_length(value, GX_DEVELOPMENT_RUN_MAX_APP_ID_BYTES);
    const uint32_t prefixLength = static_cast<uint32_t>(sizeof(kDevelopmentPrefix) - 1U);
    if (length < prefixLength || length >= GX_DEVELOPMENT_RUN_MAX_APP_ID_BYTES) return false;
    for (uint32_t i = 0; i < prefixLength; ++i)
        if (value[i] != kDevelopmentPrefix[i]) return false;
    for (uint32_t i = prefixLength; i < length; ++i) {
        const char c = value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_')) return false;
    }
    return true;
}

static bool safe_absolute_root(const char* value)
{
    if (!value || value[0] != '/' || value[1] == '\0' || value[1] == '/' || value[1] == '\\') return false;
    const uint32_t length = text_length(value, GX_DEVELOPMENT_RUN_MAX_PROJECT_ROOT_BYTES);
    if (length == 0 || length >= GX_DEVELOPMENT_RUN_MAX_PROJECT_ROOT_BYTES) return false;
    uint32_t segmentStart = 1;
    for (uint32_t i = 1; i <= length; ++i) {
        if (i < length && value[i] != '/' && value[i] != '\\') {
            if (static_cast<unsigned char>(value[i]) < 0x20) return false;
            continue;
        }
        const uint32_t segmentLength = i - segmentStart;
        if (segmentLength == 0 ||
            (segmentLength == 1 && value[segmentStart] == '.') ||
            (segmentLength == 2 && value[segmentStart] == '.' && value[segmentStart + 1] == '.')) return false;
        segmentStart = i + 1;
    }
    return true;
}

static bool safe_hash(const char* value)
{
    if (!value || text_length(value, GX_DEVELOPMENT_RUN_MAX_SHA256_BYTES) != 64) return false;
    for (uint32_t i = 0; i < 64; ++i) {
        const char c = value[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) return false;
    }
    return value[64] == '\0';
}

static bool same_identity(gx_development_run_handle handle,
                          uint64_t generation,
                          const char* applicationId)
{
    return s_active && handle != 0 && generation != 0 &&
        s_registration.handle == handle && s_registration.generation == generation &&
        equal_text(s_registration.applicationId, applicationId);
}

} // namespace

RegistrationResult register_temporary(const RegistrationRequest& request,
                                      Registration* outRegistration)
{
    if (s_active) {
        return equal_text(s_registration.applicationId, request.applicationId)
            ? RegistrationResult::DeploymentAlreadyActive
            : RegistrationResult::ApplicationIdInUse;
    }
    if (request.handle == 0 || request.generation == 0 || request.artifactSize == 0 ||
        !safe_application_id(request.applicationId) ||
        !copy_text(s_registration.displayName, sizeof(s_registration.displayName), request.displayName) ||
        s_registration.displayName[0] == '\0' ||
        !safe_absolute_root(request.projectRoot) || !safe_relative_path(request.artifactPath) ||
        !safe_hash(request.artifactSha256)) {
        s_registration = {};
        return RegistrationResult::Invalid;
    }
    s_registration = {};
    s_registration.handle = request.handle;
    s_registration.generation = request.generation;
    s_registration.artifactSize = request.artifactSize;
    if (!copy_text(s_registration.applicationId, sizeof(s_registration.applicationId), request.applicationId) ||
        !copy_text(s_registration.displayName, sizeof(s_registration.displayName), request.displayName) ||
        !copy_text(s_registration.projectRoot, sizeof(s_registration.projectRoot), request.projectRoot) ||
        !copy_text(s_registration.artifactPath, sizeof(s_registration.artifactPath), request.artifactPath) ||
        !copy_text(s_registration.artifactSha256, sizeof(s_registration.artifactSha256), request.artifactSha256)) {
        s_registration = {};
        return RegistrationResult::Invalid;
    }
    s_active = true;
    if (outRegistration) *outRegistration = s_registration;
    return RegistrationResult::Registered;
}

bool resolve_temporary(gx_development_run_handle handle,
                       uint64_t generation,
                       const char* applicationId,
                       Registration* outRegistration)
{
    if (!outRegistration || !same_identity(handle, generation, applicationId)) return false;
    *outRegistration = s_registration;
    return true;
}

bool unregister_temporary(gx_development_run_handle handle,
                         uint64_t generation,
                         const char* applicationId)
{
    if (!same_identity(handle, generation, applicationId)) return false;
    s_registration = {};
    s_active = false;
    return true;
}

bool has_active_registration()
{
    return s_active;
}

} // namespace NativeElfDevelopmentAppModel
} // namespace native_elf
} // namespace kernel
