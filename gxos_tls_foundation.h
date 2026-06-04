#pragma once

#if defined(GXOS_BARE_METAL)
#include <kernel/types.h>
#else
#include <cstddef>
#include <cstdint>
#endif

#include "guide_web_http_shared.h"

namespace gxos {

static constexpr size_t kGxosMaxCaStoreBytes = 512u * 1024u;
static constexpr size_t kGxosTlsArenaCapacityBytes = 256u * 1024u;

enum class GxosTlsBackendStatus {
    Unavailable,
    SourceMissing,
    CompileProbeReady,
    AllocatorUnavailable,
    AllocatorReady,
    RngCallbackUnavailable,
    RngCallbackReady,
    ClockCallbackUnavailable,
    ClockCallbackReady,
    CaMissing,
    CaParseFailed,
    CaParsed,
    HostnameValidationReady,
    ReadyForLocalHandshake,
    ReadyForValidatedNavigation,
    Error
};

struct GxosTlsBackendInfo {
    GxosTlsBackendStatus status;
    const char* backendName;
    const char* backendVersion;
    const char* error;
};

const char* gxos_tls_backend_status_name(GxosTlsBackendStatus status);
bool gxos_tls_backend_available();
GxosTlsBackendInfo gxos_tls_backend_info();

struct GxosTlsMbedTlsImportInfo {
    bool sourcePresent;
    bool sourceReadyForCompile;
    bool configPresent;
    bool cryptoConfigPresent;
    bool tfPsaDependencyPresent;
    const char* importPath;
    const char* configPath;
    const char* cryptoConfigPath;
    const char* tfPsaPath;
    const char* buildPlanPath;
    const char* expectedVersion;
    const char* detectedVersion;
    const char* tfPsaDetectedVersion;
    size_t plannedSourceCount;
    const char* plannedSubset;
    const char* detail;
};

GxosTlsMbedTlsImportInfo gxos_tls_mbedtls_import_info();

enum class GxosCaStoreStatus {
    Missing,
    Loaded,
    TooLarge,
    ReadError,
    ParseUnsupported,
    Invalid
};

enum class GxosCaParseStatus {
    NotApplicable,
    NotAttempted,
    SourceMissing,
    ConfigMissing,
    Parsed,
    ParseError
};

struct GxosCaStoreInfo {
    GxosCaStoreStatus status;
    GxosCaParseStatus parseStatus;
    size_t bytesLoaded;
    size_t pemBlocksDetected;
    size_t parsedCertificateCount;
    bool testOnlyFixture;
    const char* path;
    const char* error;
};

const char* gxos_ca_store_status_name(GxosCaStoreStatus status);
const char* gxos_ca_parse_status_name(GxosCaParseStatus status);
bool gxos_ca_store_load_once();
GxosCaStoreInfo gxos_ca_store_info();

enum class GxosTlsArenaStatus {
    NotApplicable,
    Pending,
    Ready,
    Unavailable,
    Exhausted
};

struct GxosTlsArenaInfo {
    GxosTlsArenaStatus status;
    size_t capacityBytes;
    size_t bytesInUse;
    size_t highWaterBytes;
    const char* error;
};

const char* gxos_tls_arena_status_name(GxosTlsArenaStatus status);
GxosTlsArenaInfo gxos_tls_arena_info();

enum class GxosTlsHookStatus {
    NotApplicable,
    Pending,
    Ready,
    Unavailable,
    Error
};

struct GxosTlsRuntimeHookInfo {
    GxosTlsHookStatus allocatorStatus;
    GxosTlsHookStatus rngCallbackStatus;
    GxosTlsHookStatus timeCallbackStatus;
    GxosTlsHookStatus psaInitStatus;
    const char* allocatorDetail;
    const char* rngDetail;
    const char* timeDetail;
    const char* psaDetail;
};

const char* gxos_tls_hook_status_name(GxosTlsHookStatus status);
GxosTlsRuntimeHookInfo gxos_tls_runtime_hook_info();

struct GxosTlsHostnameValidationInfo {
    bool available;
    bool sniSupported;
    bool originalHostnameRetained;
    bool numericIpSupported;
    const char* policy;
};

GxosTlsHostnameValidationInfo gxos_tls_hostname_validation_info();

struct GxosTlsByteStream {
    void* context;
    int (*read)(void* context, uint8_t* buffer, int length);
    int (*write)(void* context, const uint8_t* buffer, int length);
    void (*close)(void* context);
    void (*poll)(void* context);
};

struct GxosTlsLocalHandshakeResult {
    bool attempted;
    bool tcpConnected;
    bool handshakeSuccess;
    bool certificateValidationSuccess;
    bool hostnameValidationSuccess;
    bool requestWriteSuccess;
    bool responseReadSuccess;
    bool parserAcceptedResponse;
    bool usedSniHostname;
    size_t requestBytesWritten;
    size_t responseBytesRead;
    uint32_t verifyFlags;
    int transportError;
    int mbedtlsError;
    int mbedtlsState;
    gxos::web::HttpByteStreamTlsStatus transportStatus;
    char sniHost[64];
    char stage[48];
    char protocol[32];
    char cipherSuite[64];
    char error[160];
};

bool gxos_tls_open_http_byte_stream(const char* sniHostname,
                                    GxosTlsByteStream tcpStream,
                                    gxos::web::HttpByteStream* outStream,
                                    GxosTlsLocalHandshakeResult* result);

bool gxos_tls_smoke_https_request(const char* sniHostname,
                                  const char* requestBytes,
                                  size_t requestLength,
                                  GxosTlsByteStream stream,
                                  char* responseBuffer,
                                  size_t responseBufferSize,
                                  size_t* responseBytesOut,
                                  GxosTlsLocalHandshakeResult* result);

bool gxos_tls_certificate_validation_policy_enabled();
const char* gxos_tls_certificate_validation_policy();

bool gxos_tls_prerequisites_ready();
const char* gxos_tls_prerequisites_blocker_reason();

} // namespace gxos
