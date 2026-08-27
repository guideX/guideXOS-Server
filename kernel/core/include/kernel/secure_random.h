// guideXOS secure random provider
//
// This is the single kernel contract for cryptographic random bytes.  It only
// reports success when a supported hardware-backed source supplied the entire
// request.  There is deliberately no deterministic or timing-based fallback.

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace secure_random {

enum Source : uint8_t {
    SOURCE_NONE = 0,
    SOURCE_RDSEED,
    SOURCE_RDRAND,
    SOURCE_VIRTIO_RNG,
};

enum Status : uint8_t {
    STATUS_NOT_INITIALIZED = 0,
    STATUS_SOURCE_AVAILABLE,
    STATUS_CPU_RNG_UNSUPPORTED,
    STATUS_CPU_RNG_EXHAUSTED,
    STATUS_VIRTIO_UNAVAILABLE,
    STATUS_INVALID_ARGUMENT,
    STATUS_SUCCESS,
};

struct CpuRngFeatures {
    bool rdseed;
    bool rdrand;
};

struct ProviderPriority {
    Source sources[3];
    uint8_t count;
};

// The order is part of the secure-random contract.  It describes candidates
// only; a provider is not considered selected for a request until it supplies
// the complete output successfully.
inline ProviderPriority provider_priority(CpuRngFeatures cpu, bool virtioReady)
{
    ProviderPriority priority = {{ SOURCE_NONE, SOURCE_NONE, SOURCE_NONE }, 0};
    if (cpu.rdseed) priority.sources[priority.count++] = SOURCE_RDSEED;
    if (cpu.rdrand) priority.sources[priority.count++] = SOURCE_RDRAND;
    if (virtioReady) priority.sources[priority.count++] = SOURCE_VIRTIO_RNG;
    return priority;
}

struct SecureRandomDiagnostics {
    bool initialized;
    CpuRngFeatures cpu;
    Source selectedSource;
    Status lastStatus;
    bool virtioDetected;
    bool virtioReady;
    uint32_t fillRequests;
    uint32_t fillFailures;
    uint32_t rdseedRetryFailures;
    uint32_t rdrandRetryFailures;
    uint32_t providerFallbacks;
};

// Pure feature-mask interpretation used by the runtime detector and host
// tests.  The caller supplies the already-validated CPUID leaf values.
inline CpuRngFeatures cpu_rng_features_from_cpuid(uint32_t maxBasicLeaf,
                                                  uint32_t leaf1Ecx,
                                                  uint32_t leaf7Ebx)
{
    CpuRngFeatures features = { false, false };
    if (maxBasicLeaf >= 1) {
        features.rdrand = (leaf1Ecx & (1U << 30)) != 0;
    }
    if (maxBasicLeaf >= 7) {
        features.rdseed = (leaf7Ebx & (1U << 18)) != 0;
    }
    return features;
}

void init();
bool fill(void* buffer, size_t len);
bool ready();
Source source();
Status last_status();
const char* source_name();
const char* status_name();
bool rdseed_supported();
bool rdrand_supported();
SecureRandomDiagnostics diagnostics();

} // namespace secure_random
} // namespace kernel
