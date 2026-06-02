#include "gxos_tls_foundation.h"
#include "gxos_tls_prerequisites.h"

#if defined(GXOS_BARE_METAL)
#include "kernel/core/include/kernel/vfs.h"
#endif

#if defined(__has_include)
#if __has_include("third_party/mbedtls/include/mbedtls/version.h")
#define GXOS_TLS_MBEDTLS_SOURCE_PRESENT 1
#include "third_party/mbedtls/include/mbedtls/version.h"
#endif
#if __has_include("third_party/mbedtls/guidexos/mbedtls_config.h")
#define GXOS_TLS_MBEDTLS_CONFIG_PRESENT 1
#endif
#endif

#ifndef GXOS_TLS_MBEDTLS_SOURCE_PRESENT
#define GXOS_TLS_MBEDTLS_SOURCE_PRESENT 0
#endif

#ifndef GXOS_TLS_MBEDTLS_CONFIG_PRESENT
#define GXOS_TLS_MBEDTLS_CONFIG_PRESENT 0
#endif

namespace gxos {
namespace {

constexpr const char* kBareMetalCaBundlePath = "/certs/ca-bundle.pem";
constexpr const char* kHostedCaBundlePath = "(Windows trust store)";
constexpr const char* kBareMetalMbedTlsImportPath = "third_party/mbedtls";
constexpr const char* kBareMetalMbedTlsExpectedVersion = "official Mbed TLS 2.28.9 LTS source tree";
constexpr const char* kBareMetalConfigPath = "third_party/mbedtls/guidexos/mbedtls_config.h";

size_t token_length(const char* token)
{
    if (!token) return 0;
    size_t len = 0;
    while (token[len]) ++len;
    return len;
}

bool buffer_contains_token(const uint8_t* buffer, size_t buffer_len, const char* token)
{
    const size_t len = token_length(token);
    if (!buffer || len == 0 || buffer_len < len) return false;

    for (size_t i = 0; i + len <= buffer_len; ++i) {
        size_t matched = 0;
        while (matched < len &&
            buffer[i + matched] == static_cast<uint8_t>(token[matched])) {
            ++matched;
        }
        if (matched == len) return true;
    }
    return false;
}

size_t count_token_occurrences(const uint8_t* buffer, size_t buffer_len, const char* token)
{
    const size_t len = token_length(token);
    if (!buffer || len == 0 || buffer_len < len) return 0;

    size_t count = 0;
    for (size_t i = 0; i + len <= buffer_len; ++i) {
        size_t matched = 0;
        while (matched < len &&
            buffer[i + matched] == static_cast<uint8_t>(token[matched])) {
            ++matched;
        }
        if (matched == len) {
            ++count;
            i += len - 1;
        }
    }
    return count;
}

bool is_clock_ready(GxosClockStatus status)
{
    return status == GxosClockStatus::Plausible || status == GxosClockStatus::Verified;
}

const char* detected_mbedtls_version()
{
#if GXOS_TLS_MBEDTLS_SOURCE_PRESENT
#if defined(MBEDTLS_VERSION_STRING_FULL)
    return MBEDTLS_VERSION_STRING_FULL;
#elif defined(MBEDTLS_VERSION_STRING)
    return MBEDTLS_VERSION_STRING;
#else
    return "source-present (version macro unavailable)";
#endif
#else
    return "(not imported)";
#endif
}

GxosTlsMbedTlsImportInfo make_mbedtls_import_info()
{
#if defined(GXOS_BARE_METAL)
    if (!GXOS_TLS_MBEDTLS_SOURCE_PRESENT && !GXOS_TLS_MBEDTLS_CONFIG_PRESENT) {
        return {
            false,
            false,
            kBareMetalMbedTlsImportPath,
            kBareMetalMbedTlsExpectedVersion,
            "(not imported)",
            "Official Mbed TLS source and guideXOS bare-metal config are both missing."
        };
    }
    if (!GXOS_TLS_MBEDTLS_SOURCE_PRESENT) {
        return {
            false,
            true,
            kBareMetalMbedTlsImportPath,
            kBareMetalMbedTlsExpectedVersion,
            "(not imported)",
            "guideXOS bare-metal config is present, but the official Mbed TLS source tree has not been imported yet."
        };
    }
    if (!GXOS_TLS_MBEDTLS_CONFIG_PRESENT) {
        return {
            true,
            false,
            kBareMetalMbedTlsImportPath,
            kBareMetalMbedTlsExpectedVersion,
            detected_mbedtls_version(),
            "Official Mbed TLS headers were found, but the guideXOS bare-metal config is missing."
        };
    }
    return {
        true,
        true,
        kBareMetalMbedTlsImportPath,
        kBareMetalMbedTlsExpectedVersion,
        detected_mbedtls_version(),
        "Official Mbed TLS source and guideXOS config are present; Navigator handshake wiring remains gated."
    };
#else
    return {
        false,
        false,
        "(not applicable in hosted Schannel mode)",
        "(not applicable in hosted Schannel mode)",
        "Schannel hosted",
        "Hosted Navigator uses Schannel; Mbed TLS import scaffolding is bare-metal only."
    };
#endif
}

#if defined(GXOS_BARE_METAL)
struct BareMetalCaStoreState {
    bool attempted = false;
    GxosCaStoreInfo info{
        GxosCaStoreStatus::Missing,
        GxosCaParseStatus::NotAttempted,
        0,
        0,
        0,
        kBareMetalCaBundlePath,
        "Root CA bundle has not been checked yet."
    };
    uint8_t bytes[kGxosMaxCaStoreBytes];
};

BareMetalCaStoreState& ca_store_state()
{
    static BareMetalCaStoreState state;
    return state;
}
#endif

const char* readiness_blocker_for_ca_store(const GxosCaStoreInfo& info)
{
    switch (info.status) {
    case GxosCaStoreStatus::Missing:
        return "Root CA bundle is missing at /certs/ca-bundle.pem";
    case GxosCaStoreStatus::TooLarge:
        return "Root CA bundle exceeds the 512 KiB safety cap";
    case GxosCaStoreStatus::ReadError:
        return "Root CA bundle could not be read from the VFS";
    case GxosCaStoreStatus::ParseUnsupported:
        return "Root CA bundle parser is not wired yet";
    case GxosCaStoreStatus::Invalid:
        return "Root CA bundle contents are invalid";
    default:
        break;
    }

    switch (info.parseStatus) {
    case GxosCaParseStatus::SourceMissing:
        return "Official Mbed TLS source tree is missing at third_party/mbedtls";
    case GxosCaParseStatus::ConfigMissing:
        return "guideXOS Mbed TLS config is missing at third_party/mbedtls/guidexos/mbedtls_config.h";
    case GxosCaParseStatus::ParseError:
        return info.error ? info.error : "Root CA bundle could not be parsed";
    default:
        return nullptr;
    }
}

GxosTlsArenaInfo make_tls_arena_info()
{
#if defined(GXOS_BARE_METAL)
    return {
        GxosTlsArenaStatus::Ready,
        kGxosTlsArenaCapacityBytes,
        0,
        0,
        nullptr
    };
#else
    return {
        GxosTlsArenaStatus::NotApplicable,
        0,
        0,
        0,
        "Hosted Schannel path does not use the guideXOS TLS arena."
    };
#endif
}

GxosTlsHostnameValidationInfo make_hostname_validation_info()
{
#if defined(GXOS_BARE_METAL)
    const GxosTlsMbedTlsImportInfo importInfo = make_mbedtls_import_info();
    if (!importInfo.sourcePresent) {
        return {
            false,
            true,
            true,
            false,
            "scaffolded only; original URL host is retained for future SNI and certificate checks, but Mbed TLS source import is still missing"
        };
    }
    if (!importInfo.configPresent) {
        return {
            false,
            true,
            true,
            false,
            "scaffolded only; original URL host is retained, but the guideXOS Mbed TLS config is missing"
        };
    }
    return {
        false,
        true,
        true,
        false,
        "scaffolded only; SNI and original-host retention are planned, but certificate hostname verification is not active until handshake wiring lands"
    };
#else
    return {
        true,
        true,
        true,
        true,
        "enabled via Schannel using the original URL host for SNI and hostname validation"
    };
#endif
}

GxosTlsBackendInfo make_backend_info()
{
#if defined(GXOS_BARE_METAL)
    const GxosTlsMbedTlsImportInfo importInfo = make_mbedtls_import_info();
    const GxosTlsArenaInfo arenaInfo = make_tls_arena_info();

    if (!importInfo.sourcePresent) {
        return {
            GxosTlsBackendStatus::SourceMissing,
            "Mbed TLS bare-metal scaffold",
            importInfo.detectedVersion,
            "Vendored Mbed TLS source tree is missing at third_party/mbedtls; handshake support stays disabled."
        };
    }
    if (!importInfo.configPresent) {
        return {
            GxosTlsBackendStatus::Error,
            "Mbed TLS bare-metal scaffold",
            importInfo.detectedVersion,
            "guideXOS bare-metal Mbed TLS config is missing; handshake support stays disabled."
        };
    }
    if (gxos_random_quality() != GxosRandomQuality::Secure) {
        return {
            GxosTlsBackendStatus::RngUnavailable,
            "Mbed TLS bare-metal scaffold",
            importInfo.detectedVersion,
            "Secure RNG is unavailable; TLS cannot start."
        };
    }
    if (!is_clock_ready(gxos_wall_clock_status())) {
        return {
            GxosTlsBackendStatus::ClockUnavailable,
            "Mbed TLS bare-metal scaffold",
            importInfo.detectedVersion,
            "Wall clock is not plausible enough for certificate validation."
        };
    }
    if (arenaInfo.status != GxosTlsArenaStatus::Ready) {
        return {
            GxosTlsBackendStatus::ArenaUnavailable,
            "Mbed TLS bare-metal scaffold",
            importInfo.detectedVersion,
            arenaInfo.error ? arenaInfo.error : "TLS arena is unavailable."
        };
    }

    const GxosCaStoreInfo caInfo = gxos_ca_store_info();
    if (caInfo.status == GxosCaStoreStatus::Loaded && caInfo.parseStatus == GxosCaParseStatus::Parsed) {
        return {
            GxosTlsBackendStatus::CaParsed,
            "Mbed TLS bare-metal scaffold",
            importInfo.detectedVersion,
            "Root CA bundle is parsed, but Navigator handshake wiring remains disabled."
        };
    }
    if (caInfo.status == GxosCaStoreStatus::Loaded) {
        return {
            GxosTlsBackendStatus::CaLoadedNotParsed,
            "Mbed TLS bare-metal scaffold",
            importInfo.detectedVersion,
            caInfo.error ? caInfo.error : "Root CA bundle is loaded, but X.509 parsing is not complete."
        };
    }
    return {
        GxosTlsBackendStatus::BuildConfigured,
        "Mbed TLS bare-metal scaffold",
        importInfo.detectedVersion,
        caInfo.error ? caInfo.error : "Bare-metal TLS scaffolding is configured, but the root CA bundle is not ready."
    };
#else
    return {
        GxosTlsBackendStatus::ReadyForValidatedNavigation,
        "Schannel hosted",
        "Windows Schannel",
        nullptr
    };
#endif
}

const char* compute_tls_prerequisites_blocker()
{
    if (gxos_random_quality() != GxosRandomQuality::Secure) {
        return "Secure RNG is unavailable";
    }

    if (!is_clock_ready(gxos_wall_clock_status())) {
        return "Wall clock is not plausible enough for TLS validation";
    }

    const GxosTlsBackendInfo backend = gxos_tls_backend_info();
    if (!gxos_tls_backend_available()) {
        switch (backend.status) {
        case GxosTlsBackendStatus::SourceMissing:
        case GxosTlsBackendStatus::BuildConfigured:
        case GxosTlsBackendStatus::CaLoadedNotParsed:
        case GxosTlsBackendStatus::CaParsed:
        case GxosTlsBackendStatus::RngUnavailable:
        case GxosTlsBackendStatus::ClockUnavailable:
        case GxosTlsBackendStatus::ArenaUnavailable:
        case GxosTlsBackendStatus::Error:
            return backend.error ? backend.error : "TLS backend is not ready";
        case GxosTlsBackendStatus::Unavailable:
            return "TLS backend is unavailable";
        default:
            return "TLS backend is not ready";
        }
    }

    const GxosCaStoreInfo caStore = gxos_ca_store_info();
    if (caStore.status != GxosCaStoreStatus::Loaded || caStore.parseStatus != GxosCaParseStatus::Parsed) {
        return readiness_blocker_for_ca_store(caStore);
    }

    const GxosTlsHostnameValidationInfo hostnameInfo = gxos_tls_hostname_validation_info();
    if (!hostnameInfo.available) {
        return "Certificate hostname validation is not active yet";
    }

    if (!gxos_tls_certificate_validation_policy_enabled()) {
        return "Certificate validation policy is not enabled yet";
    }

    if (backend.status != GxosTlsBackendStatus::ReadyForValidatedNavigation) {
        return "TLS handshake is not wired into Navigator yet";
    }

    return nullptr;
}

} // namespace

const char* gxos_tls_backend_status_name(GxosTlsBackendStatus status)
{
    switch (status) {
    case GxosTlsBackendStatus::Unavailable: return "Unavailable";
    case GxosTlsBackendStatus::SourceMissing: return "SourceMissing";
    case GxosTlsBackendStatus::BuildConfigured: return "BuildConfigured";
    case GxosTlsBackendStatus::CaLoadedNotParsed: return "CaLoadedNotParsed";
    case GxosTlsBackendStatus::CaParsed: return "CaParsed";
    case GxosTlsBackendStatus::RngUnavailable: return "RngUnavailable";
    case GxosTlsBackendStatus::ClockUnavailable: return "ClockUnavailable";
    case GxosTlsBackendStatus::ArenaUnavailable: return "ArenaUnavailable";
    case GxosTlsBackendStatus::ReadyForLocalHandshake: return "ReadyForLocalHandshake";
    case GxosTlsBackendStatus::ReadyForValidatedNavigation: return "ReadyForValidatedNavigation";
    case GxosTlsBackendStatus::Error: return "Error";
    default: return "Unknown";
    }
}

bool gxos_tls_backend_available()
{
    const GxosTlsBackendStatus status = gxos_tls_backend_info().status;
    return status == GxosTlsBackendStatus::ReadyForLocalHandshake ||
        status == GxosTlsBackendStatus::ReadyForValidatedNavigation;
}

GxosTlsBackendInfo gxos_tls_backend_info()
{
    return make_backend_info();
}

GxosTlsMbedTlsImportInfo gxos_tls_mbedtls_import_info()
{
    return make_mbedtls_import_info();
}

const char* gxos_ca_store_status_name(GxosCaStoreStatus status)
{
    switch (status) {
    case GxosCaStoreStatus::Missing: return "Missing";
    case GxosCaStoreStatus::Loaded: return "Loaded";
    case GxosCaStoreStatus::TooLarge: return "TooLarge";
    case GxosCaStoreStatus::ReadError: return "ReadError";
    case GxosCaStoreStatus::ParseUnsupported: return "ParseUnsupported";
    case GxosCaStoreStatus::Invalid: return "Invalid";
    default: return "Unknown";
    }
}

const char* gxos_ca_parse_status_name(GxosCaParseStatus status)
{
    switch (status) {
    case GxosCaParseStatus::NotApplicable: return "NotApplicable";
    case GxosCaParseStatus::NotAttempted: return "NotAttempted";
    case GxosCaParseStatus::SourceMissing: return "SourceMissing";
    case GxosCaParseStatus::ConfigMissing: return "ConfigMissing";
    case GxosCaParseStatus::Parsed: return "Parsed";
    case GxosCaParseStatus::ParseError: return "ParseError";
    default: return "Unknown";
    }
}

bool gxos_ca_store_load_once()
{
#if defined(GXOS_BARE_METAL)
    BareMetalCaStoreState& state = ca_store_state();
    if (state.attempted) {
        return state.info.status == GxosCaStoreStatus::Loaded;
    }
    state.attempted = true;

    kernel::vfs::FileInfo info{};
    const kernel::vfs::Status statStatus = kernel::vfs::stat(kBareMetalCaBundlePath, &info);
    if (statStatus == kernel::vfs::VFS_ERR_NOT_FOUND ||
        statStatus == kernel::vfs::VFS_ERR_NOT_MOUNT) {
        state.info = {
            GxosCaStoreStatus::Missing,
            GxosCaParseStatus::NotAttempted,
            0,
            0,
            0,
            kBareMetalCaBundlePath,
            "Root CA bundle not found at /certs/ca-bundle.pem."
        };
        return false;
    }
    if (statStatus != kernel::vfs::VFS_OK) {
        state.info = {
            GxosCaStoreStatus::ReadError,
            GxosCaParseStatus::NotAttempted,
            0,
            0,
            0,
            kBareMetalCaBundlePath,
            "Could not stat the root CA bundle through the VFS."
        };
        return false;
    }
    if (info.type != kernel::vfs::FILE_TYPE_REGULAR) {
        state.info = {
            GxosCaStoreStatus::Invalid,
            GxosCaParseStatus::NotAttempted,
            0,
            0,
            0,
            kBareMetalCaBundlePath,
            "Root CA bundle path does not point to a regular file."
        };
        return false;
    }
    if (info.size == 0) {
        state.info = {
            GxosCaStoreStatus::Invalid,
            GxosCaParseStatus::NotAttempted,
            0,
            0,
            0,
            kBareMetalCaBundlePath,
            "Root CA bundle is empty."
        };
        return false;
    }
    if (info.size > static_cast<uint64_t>(kGxosMaxCaStoreBytes)) {
        state.info = {
            GxosCaStoreStatus::TooLarge,
            GxosCaParseStatus::NotAttempted,
            0,
            0,
            0,
            kBareMetalCaBundlePath,
            "Root CA bundle exceeds the 512 KiB safety cap."
        };
        return false;
    }

    const int32_t bytesRead = kernel::vfs::read_file(
        kBareMetalCaBundlePath,
        state.bytes,
        static_cast<uint32_t>(kGxosMaxCaStoreBytes));
    if (bytesRead < 0 || static_cast<uint64_t>(bytesRead) != info.size) {
        state.info = {
            GxosCaStoreStatus::ReadError,
            GxosCaParseStatus::NotAttempted,
            0,
            0,
            0,
            kBareMetalCaBundlePath,
            "Root CA bundle read did not complete successfully."
        };
        return false;
    }

    const size_t loaded = static_cast<size_t>(bytesRead);
    const size_t pemBegins = count_token_occurrences(state.bytes, loaded, "-----BEGIN CERTIFICATE-----");
    const size_t pemEnds = count_token_occurrences(state.bytes, loaded, "-----END CERTIFICATE-----");
    if (pemBegins == 0 || pemBegins != pemEnds ||
        !buffer_contains_token(state.bytes, loaded, "-----BEGIN CERTIFICATE-----") ||
        !buffer_contains_token(state.bytes, loaded, "-----END CERTIFICATE-----")) {
        state.info = {
            GxosCaStoreStatus::Invalid,
            GxosCaParseStatus::NotAttempted,
            loaded,
            pemBegins,
            0,
            kBareMetalCaBundlePath,
            "Root CA bundle does not look like a PEM certificate bundle."
        };
        return false;
    }

    const GxosTlsMbedTlsImportInfo importInfo = make_mbedtls_import_info();
    if (!importInfo.sourcePresent) {
        state.info = {
            GxosCaStoreStatus::Loaded,
            GxosCaParseStatus::SourceMissing,
            loaded,
            pemBegins,
            0,
            kBareMetalCaBundlePath,
            "Root CA bundle is loaded, but the official Mbed TLS source tree is missing so X.509 parsing cannot begin."
        };
        return true;
    }
    if (!importInfo.configPresent) {
        state.info = {
            GxosCaStoreStatus::Loaded,
            GxosCaParseStatus::ConfigMissing,
            loaded,
            pemBegins,
            0,
            kBareMetalCaBundlePath,
            "Root CA bundle is loaded, but the guideXOS Mbed TLS config is missing so X.509 parsing cannot begin."
        };
        return true;
    }

    state.info = {
        GxosCaStoreStatus::Loaded,
        GxosCaParseStatus::ParseError,
        loaded,
        pemBegins,
        0,
        kBareMetalCaBundlePath,
        "Root CA bundle is loaded, but Mbed TLS X.509 parser wiring is not linked into this build yet."
    };
    return true;
#else
    return true;
#endif
}

GxosCaStoreInfo gxos_ca_store_info()
{
#if defined(GXOS_BARE_METAL)
    gxos_ca_store_load_once();
    return ca_store_state().info;
#else
    static const GxosCaStoreInfo info = {
        GxosCaStoreStatus::Loaded,
        GxosCaParseStatus::NotApplicable,
        0,
        0,
        0,
        kHostedCaBundlePath,
        nullptr
    };
    return info;
#endif
}

const char* gxos_tls_arena_status_name(GxosTlsArenaStatus status)
{
    switch (status) {
    case GxosTlsArenaStatus::NotApplicable: return "NotApplicable";
    case GxosTlsArenaStatus::Ready: return "Ready";
    case GxosTlsArenaStatus::Unavailable: return "Unavailable";
    case GxosTlsArenaStatus::Exhausted: return "Exhausted";
    default: return "Unknown";
    }
}

GxosTlsArenaInfo gxos_tls_arena_info()
{
    return make_tls_arena_info();
}

GxosTlsHostnameValidationInfo gxos_tls_hostname_validation_info()
{
    return make_hostname_validation_info();
}

bool gxos_tls_certificate_validation_policy_enabled()
{
#if defined(GXOS_BARE_METAL)
    const GxosCaStoreInfo caInfo = gxos_ca_store_info();
    const GxosTlsHostnameValidationInfo hostnameInfo = gxos_tls_hostname_validation_info();
    return caInfo.status == GxosCaStoreStatus::Loaded &&
        caInfo.parseStatus == GxosCaParseStatus::Parsed &&
        hostnameInfo.available &&
        is_clock_ready(gxos_wall_clock_status()) &&
        gxos_random_quality() == GxosRandomQuality::Secure &&
        gxos_tls_backend_info().status == GxosTlsBackendStatus::ReadyForValidatedNavigation;
#else
    return true;
#endif
}

const char* gxos_tls_certificate_validation_policy()
{
#if defined(GXOS_BARE_METAL)
    const GxosTlsMbedTlsImportInfo importInfo = gxos_tls_mbedtls_import_info();
    if (!importInfo.sourcePresent) {
        return "disabled; original URL host is retained for future SNI and hostname checks, but Mbed TLS source import is missing";
    }
    if (!importInfo.configPresent) {
        return "disabled; original URL host retention is scaffolded, but the guideXOS Mbed TLS config is missing";
    }
    return "disabled; original URL host retention and bounded TLS scaffolding are present, but X.509 parsing and handshake wiring are not active";
#else
    return "enabled via Schannel, Windows trust, and hostname validation";
#endif
}

bool gxos_tls_prerequisites_ready()
{
    return compute_tls_prerequisites_blocker() == nullptr;
}

const char* gxos_tls_prerequisites_blocker_reason()
{
    const char* blocker = compute_tls_prerequisites_blocker();
    return blocker ? blocker : "none";
}

} // namespace gxos
