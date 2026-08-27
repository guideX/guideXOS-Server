#pragma once

#if defined(GXOS_BARE_METAL)
#include <kernel/types.h>
#else
#include <cstddef>
#include <cstdint>
#endif

namespace gxos {

enum class GxosRandomQuality {
    Unavailable,
    TestOnly,
    Secure
};

enum class GxosClockStatus {
    Unavailable,
    Plausible,
    Verified
};

struct GxosSecureRandomDiagnostics {
    bool initialized;
    bool rdseedSupported;
    bool rdrandSupported;
    bool virtioDetected;
    bool virtioReady;
    const char* source;
    const char* status;
    uint32_t fillRequests;
    uint32_t fillFailures;
    uint32_t rdseedRetryFailures;
    uint32_t rdrandRetryFailures;
    uint32_t providerFallbacks;
};

bool gxos_random_bytes(void* buffer, size_t len);
GxosRandomQuality gxos_random_quality();
const char* gxos_random_quality_name(GxosRandomQuality quality);
const char* gxos_random_backend();
const char* gxos_secure_random_source();
const char* gxos_secure_random_status();
bool gxos_rdseed_supported();
bool gxos_rdrand_supported();
bool gxos_virtio_rng_detected();
const char* gxos_virtio_rng_status();
GxosSecureRandomDiagnostics gxos_secure_random_diagnostics();

bool gxos_wall_clock_unix_seconds(int64_t* out);
GxosClockStatus gxos_wall_clock_status();
const char* gxos_wall_clock_status_name(GxosClockStatus status);
const char* gxos_wall_clock_backend();
bool gxos_wall_clock_utc_text(char* out, size_t out_size);

} // namespace gxos
