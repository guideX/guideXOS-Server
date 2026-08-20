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
static constexpr size_t kGxosMaxCaManifestBytes = 16u * 1024u;
static constexpr size_t kGxosTlsArenaCapacityBytes = 1024u * 1024u;

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

enum class GxosCaManifestStatus {
    NotApplicable,
    Missing,
    Loaded,
    TooLarge,
    ReadError,
    Invalid
};

struct GxosCaManifestInfo {
    GxosCaManifestStatus status;
    const char* path;
    size_t bytesLoaded;
    bool present;
    const char* schemaVersion;
    const char* bundleType;
    const char* rotationId;
    const char* manifestSha256;
    const char* computedSha256;
    size_t rootCount;
    size_t pemBytes;
    bool productionReady;
    bool testOnly;
    bool hashMatch;
    const char* error;
};

struct GxosCaStoreParseInfo {
    size_t inputCertificateCount;
    size_t parsedCertificateCount;
    size_t skippedCertificateCount;
    size_t parseErrorCount;
    int firstParseErrorCode;
    size_t firstParseErrorIndex;
    bool filteredParse;
    const char* parseMode;
    const char* error;
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
    GxosCaManifestInfo manifest;
};

const char* gxos_ca_store_status_name(GxosCaStoreStatus status);
const char* gxos_ca_parse_status_name(GxosCaParseStatus status);
const char* gxos_ca_manifest_status_name(GxosCaManifestStatus status);
bool gxos_ca_store_load_once();
GxosCaStoreInfo gxos_ca_store_info();
GxosCaStoreParseInfo gxos_ca_store_parse_info();

enum class GxosTrustStoreSource {
    None,
    SmokeFixtureTrust,
    UserProvidedTrustStore,
    ProductionPublicProbeTrust,
    ShippedRootCandidate,
    ProductionRootStore,
    WindowsSystemTrustStore
};

enum class GxosTrustStorePolicyState {
    NoTrustStore,
    ProductionTrustStoreUnavailable,
    TrustStoreMalformed,
    TrustStoreParsed
};

struct GxosTrustStorePolicyInfo {
    GxosTrustStorePolicyState state;
    GxosTrustStoreSource source;
    const char* path;
    const char* sourceDetail;
    size_t sizeBytes;
    size_t parsedCertificateCount;
    bool smokeTestOnly;
    bool productionReady;
    bool publicInternetReady;
    const char* error;
};

const char* gxos_trust_store_source_name(GxosTrustStoreSource source);
const char* gxos_trust_store_policy_state_name(GxosTrustStorePolicyState state);
GxosTrustStorePolicyInfo gxos_tls_trust_store_policy_info();

enum class GxosValidatedHttpsPolicyState {
    Disabled,
    LocalSmokeOnly,
    UserTrustStoreDevMode,
    ProductionValidated
};

struct GxosValidatedHttpsPolicyInfo {
    GxosValidatedHttpsPolicyState state;
    GxosValidatedHttpsPolicyState selectedState;
    bool localAllowlistEnabled;
    bool localSmokeReady;
    bool validatedNavigationEnabled;
    bool broadPublicHttpsEnabled;
    bool publicHttpsPilotRequested;
    bool productionReady;
    const char* configPath;
    const char* configSource;
    const char* localAllowReason;
    const char* detail;
    const char* publicHttpsPilotReason;
    const char* blocker;
    const char* error;
};

const char* gxos_validated_https_policy_state_name(GxosValidatedHttpsPolicyState state);
GxosValidatedHttpsPolicyInfo gxos_validated_https_policy_info();
bool gxos_tls_local_smoke_https_ready();
const char* gxos_tls_local_smoke_https_blocker_reason();

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
    size_t tlsBioSendCalls;
    size_t tlsBioRecvCalls;
    size_t tlsBioBytesSent;
    size_t tlsBioBytesReceived;
    int tlsBioLastSendResult;
    int tlsBioLastRecvResult;
    uint32_t tlsHandshakeElapsedMs;
    uint32_t verifyFlags;
    int transportError;
    int mbedtlsError;
    int mbedtlsState;
    gxos::web::HttpByteStreamTlsStatus transportStatus;
    GxosTlsHookStatus allocatorStatus;
    GxosTlsHookStatus rngCallbackStatus;
    GxosTlsHookStatus timeCallbackStatus;
    GxosTlsHookStatus psaInitStatus;
    bool caChainReady;
    size_t caChainCertCount;
    int sslConfigDefaultsStatus;
    int sslSetupStatus;
    int sslHostnameStatus;
    int sslBioStatus;
    int sslAuthmode;
    int sslEndpointMode;
    int sslTransportMode;
    char tlsSetupStep[32];
    int tlsSetupErrorCode;
    char tlsSetupErrorName[48];
    char tlsHandshakeErrorName[64];
    size_t tlsSuiteContractCount;
    size_t tlsSuiteContractRealCount;
    bool tlsSuiteContractInstalled;
    bool tlsClientHelloSent;
    size_t tlsClientHelloRealSuiteCount;
    bool tlsClientHelloScsvOnly;
    bool tlsClientHelloCanonicalSuiteOffered;
    char tlsSuiteContractNames[160];
    char tlsContractFailureClass[48];
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

/* Direct PSA ECDSA probes used by the Phase 8J bare-metal crypto rail. */
bool gxos_tls_run_phase8j_raw_ecdsa_diagnostics();

#if defined(GXOS_NAVIGATOR_TLS_CAPABILITY_CONTRACT_NEGATIVE_TEST_ACTIVE)
bool gxos_tls_capability_contract_negative_test(GxosTlsLocalHandshakeResult* result);
#endif

} // namespace gxos
