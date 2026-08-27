// guideXOS secure random provider implementation

#include "include/kernel/secure_random.h"
#include "include/kernel/secure_random_retry.h"

#include "include/kernel/arch.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/virtio_rng.h"

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace kernel {
namespace secure_random {

namespace {

static const uint32_t kHardwareRetryLimit = 8;
static const size_t kVirtioChunkBytes = 256;

struct ProviderState {
    bool initAttempted;
    CpuRngFeatures cpu;
    Source selectedSource;
    Status lastStatus;
};

static ProviderState s_state = {
    false,
    { false, false },
    SOURCE_NONE,
    STATUS_NOT_INITIALIZED
};

static bool s_cpuFailureMarkerEmitted = false;
static bool s_noSourceFailureMarkerEmitted = false;
static bool s_sourceSuccessMarkerEmitted[4] = { false, false, false, false };
static uint32_t s_fillRequests = 0;
static uint32_t s_fillFailures = 0;
static uint32_t s_rdseedRetryFailures = 0;
static uint32_t s_rdrandRetryFailures = 0;
static uint32_t s_providerFallbacks = 0;

static void increment_bounded(uint32_t* value)
{
    if (value && *value != 0xFFFFFFFFu) ++*value;
}

#if defined(ARCH_X86) || defined(ARCH_AMD64)
static bool cpuid_available()
{
#if defined(ARCH_AMD64)
    return true;
#elif defined(_MSC_VER)
    __try {
        int regs[4];
        __cpuid(regs, 0);
        return true;
    } __except (1) {
        return false;
    }
#elif defined(__GNUC__) || defined(__clang__)
    uint32_t before = 0;
    uint32_t after = 0;
    __asm__ volatile(
        "pushfl\n\t"
        "pushfl\n\t"
        "popl %0\n\t"
        "movl %0, %1\n\t"
        "xorl $0x200000, %1\n\t"
        "pushl %1\n\t"
        "popfl\n\t"
        "pushfl\n\t"
        "popl %1\n\t"
        "popfl"
        : "=&r"(before), "=&r"(after)
        :
        : "cc");
    return ((before ^ after) & 0x200000U) != 0;
#else
    return false;
#endif
}

static void read_cpuid(uint32_t leaf,
                       uint32_t subleaf,
                       uint32_t* eax,
                       uint32_t* ebx,
                       uint32_t* ecx,
                       uint32_t* edx)
{
#if defined(_MSC_VER)
    int regs[4];
    __cpuidex(regs, static_cast<int>(leaf), static_cast<int>(subleaf));
    *eax = static_cast<uint32_t>(regs[0]);
    *ebx = static_cast<uint32_t>(regs[1]);
    *ecx = static_cast<uint32_t>(regs[2]);
    *edx = static_cast<uint32_t>(regs[3]);
#elif defined(__GNUC__) || defined(__clang__)
    __asm__ volatile(
        "cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(subleaf));
#else
    (void)leaf;
    (void)subleaf;
    *eax = 0;
    *ebx = 0;
    *ecx = 0;
    *edx = 0;
#endif
}

static CpuRngFeatures detect_cpu_features()
{
    CpuRngFeatures features = { false, false };
    if (!cpuid_available()) return features;

    uint32_t maxBasicLeaf = 0;
    uint32_t ignored = 0;
    uint32_t ignored2 = 0;
    uint32_t ignored3 = 0;
    read_cpuid(0, 0, &maxBasicLeaf, &ignored, &ignored2, &ignored3);
    if (maxBasicLeaf < 1) return features;

    uint32_t leaf1Ecx = 0;
    read_cpuid(1, 0, &ignored, &ignored2, &leaf1Ecx, &ignored3);

    uint32_t leaf7Ebx = 0;
    if (maxBasicLeaf >= 7) {
        read_cpuid(7, 0, &ignored, &leaf7Ebx, &ignored2, &ignored3);
    }
    return cpu_rng_features_from_cpuid(maxBasicLeaf, leaf1Ecx, leaf7Ebx);
}

static bool read_rdseed32(uint32_t* word)
{
    if (!word) return false;
#if defined(_MSC_VER)
    return _rdseed32_step(word) != 0;
#elif defined(__GNUC__) || defined(__clang__)
    unsigned char success = 0;
    __asm__ volatile("rdseed %0; setc %1"
                     : "=r"(*word), "=qm"(success)
                     :
                     : "cc");
    return success != 0;
#else
    return false;
#endif
}

static bool read_rdrand32(uint32_t* word)
{
    if (!word) return false;
#if defined(_MSC_VER)
    return _rdrand32_step(word) != 0;
#elif defined(__GNUC__) || defined(__clang__)
    unsigned char success = 0;
    __asm__ volatile("rdrand %0; setc %1"
                     : "=r"(*word), "=qm"(success)
                     :
                     : "cc");
    return success != 0;
#else
    return false;
#endif
}

#else

static CpuRngFeatures detect_cpu_features()
{
    return { false, false };
}

static bool read_rdseed32(uint32_t*)
{
    return false;
}

static bool read_rdrand32(uint32_t*)
{
    return false;
}

#endif

static void set_status(Status status)
{
    s_state.lastStatus = status;
}

static const char* source_name_for(Source source)
{
    switch (source) {
    case SOURCE_RDSEED: return "CPU RDSEED";
    case SOURCE_RDRAND: return "CPU RDRAND";
    case SOURCE_VIRTIO_RNG: return "virtio-rng legacy PCI transitional";
    default: return "none (secure entropy unavailable)";
    }
}

static void announce_successful_source(Source source)
{
    const uint8_t index = static_cast<uint8_t>(source);
    if (index >= 4 || s_sourceSuccessMarkerEmitted[index]) return;
    s_sourceSuccessMarkerEmitted[index] = true;
    serial::puts("[SECURE-RNG] provider=");
    serial::puts(source_name_for(source));
    serial::puts("; request succeeded\n");
}

static bool fill_from_virtio(void* buffer, size_t len)
{
    uint8_t* const start = static_cast<uint8_t*>(buffer);
    uint8_t* out = start;
    size_t remaining = len;
    while (remaining != 0) {
        const size_t request = remaining > kVirtioChunkBytes ? kVirtioChunkBytes : remaining;
        if (!virtio::rng::fill(out, request)) {
            zero_random_bytes(start, len);
            return false;
        }
        out += request;
        remaining -= request;
    }
    return true;
}

static void announce_initial_state()
{
    serial::puts("[SECURE-RNG] provider initialized; rdseed=");
    serial::puts(s_state.cpu.rdseed ? "supported" : "unsupported");
    serial::puts("; rdrand=");
    serial::puts(s_state.cpu.rdrand ? "supported" : "unsupported");
    serial::puts("; virtio-rng=");
    serial::puts(virtio::rng::ready() ? "ready" : "unavailable");
    serial::puts("; selected=");
    serial::puts(source_name());
    serial::puts("\n");
}

} // namespace

void init()
{
    if (s_state.initAttempted) return;

    s_state.initAttempted = true;
    s_state.cpu = detect_cpu_features();
    const ProviderPriority priority = provider_priority(s_state.cpu, virtio::rng::ready());
    if (priority.count != 0) {
        s_state.selectedSource = priority.sources[0];
        set_status(STATUS_SOURCE_AVAILABLE);
    } else {
        s_state.selectedSource = SOURCE_NONE;
        set_status(STATUS_CPU_RNG_UNSUPPORTED);
    }

    announce_initial_state();
}

bool fill(void* buffer, size_t len)
{
    if (len == 0) return true;
    increment_bounded(&s_fillRequests);
    if (!buffer) {
        set_status(STATUS_INVALID_ARGUMENT);
        increment_bounded(&s_fillFailures);
        return false;
    }

    init();

    const ProviderPriority priority = provider_priority(s_state.cpu, virtio::rng::ready());
    bool cpuProviderFailed = false;
    for (uint8_t index = 0; index < priority.count; ++index) {
        const Source candidate = priority.sources[index];
        bool success = false;
        switch (candidate) {
        case SOURCE_RDSEED:
            success = fill_from_word_source(buffer, len, read_rdseed32,
                kHardwareRetryLimit, &s_rdseedRetryFailures);
            break;
        case SOURCE_RDRAND:
            success = fill_from_word_source(buffer, len, read_rdrand32,
                kHardwareRetryLimit, &s_rdrandRetryFailures);
            break;
        case SOURCE_VIRTIO_RNG:
            success = fill_from_virtio(buffer, len);
            break;
        default:
            break;
        }

        if (success) {
            s_state.selectedSource = candidate;
            set_status(STATUS_SUCCESS);
            announce_successful_source(candidate);
            return true;
        }

        if (candidate == SOURCE_RDSEED || candidate == SOURCE_RDRAND) {
            cpuProviderFailed = true;
        }
        if (index + 1 < priority.count) increment_bounded(&s_providerFallbacks);
    }

    zero_random_bytes(static_cast<uint8_t*>(buffer), len);
    increment_bounded(&s_fillFailures);
    s_state.selectedSource = SOURCE_NONE;
    if (cpuProviderFailed) {
        set_status(STATUS_CPU_RNG_EXHAUSTED);
        if (!s_cpuFailureMarkerEmitted) {
            s_cpuFailureMarkerEmitted = true;
            serial::puts("[SECURE-RNG] CPU hardware RNG exhausted after bounded retries; request failed closed\n");
        }
    } else {
        set_status(STATUS_VIRTIO_UNAVAILABLE);
        if (!s_noSourceFailureMarkerEmitted) {
            s_noSourceFailureMarkerEmitted = true;
            serial::puts("[SECURE-RNG] no supported secure entropy source; request failed closed\n");
        }
    }
    return false;
}

bool ready()
{
    init();
    return s_state.selectedSource != SOURCE_NONE;
}

Source source()
{
    init();
    return s_state.selectedSource;
}

Status last_status()
{
    init();
    return s_state.lastStatus;
}

const char* source_name()
{
    return source_name_for(source());
}

const char* status_name()
{
    switch (last_status()) {
    case STATUS_SOURCE_AVAILABLE: return "source-available";
    case STATUS_CPU_RNG_UNSUPPORTED: return "cpu-rng-unsupported";
    case STATUS_CPU_RNG_EXHAUSTED: return "cpu-rng-exhausted";
    case STATUS_VIRTIO_UNAVAILABLE: return "virtio-rng-unavailable";
    case STATUS_INVALID_ARGUMENT: return "invalid-argument";
    case STATUS_SUCCESS: return "success";
    default: return "not-initialized";
    }
}

bool rdseed_supported()
{
    init();
    return s_state.cpu.rdseed;
}

bool rdrand_supported()
{
    init();
    return s_state.cpu.rdrand;
}

SecureRandomDiagnostics diagnostics()
{
    init();
    return {
        s_state.initAttempted,
        s_state.cpu,
        s_state.selectedSource,
        s_state.lastStatus,
        virtio::rng::detected(),
        virtio::rng::ready(),
        s_fillRequests,
        s_fillFailures,
        s_rdseedRetryFailures,
        s_rdrandRetryFailures,
        s_providerFallbacks
    };
}

} // namespace secure_random
} // namespace kernel
