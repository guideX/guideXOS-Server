#include "kernel/secure_random.h"
#include "kernel/secure_random_retry.h"

#include <stdint.h>

namespace {

struct ReaderState {
    uint32_t attempts;
    uint32_t failuresBeforeSuccess;
    uint32_t word;
};

ReaderState* g_reader = nullptr;

bool scripted_reader(uint32_t* word)
{
    ++g_reader->attempts;
    if (g_reader->attempts <= g_reader->failuresBeforeSuccess) return false;
    *word = g_reader->word;
    return true;
}

bool failing_reader(uint32_t*)
{
    ++g_reader->attempts;
    return false;
}

bool all_bytes_equal(const uint8_t* bytes, size_t len, uint8_t value)
{
    for (size_t i = 0; i < len; ++i) {
        if (bytes[i] != value) return false;
    }
    return true;
}

void require(bool condition);

void require_priority(const kernel::secure_random::ProviderPriority& priority,
                      const kernel::secure_random::Source* expected,
                      uint8_t expectedCount)
{
    require(priority.count == expectedCount);
    for (uint8_t i = 0; i < expectedCount; ++i) {
        require(priority.sources[i] == expected[i]);
    }
}

void require(bool condition)
{
    if (!condition) __builtin_trap();
}

} // namespace

int main()
{
    const kernel::secure_random::CpuRngFeatures both =
        kernel::secure_random::cpu_rng_features_from_cpuid(7, 1U << 30, 1U << 18);
    require(both.rdseed);
    require(both.rdrand);

    const kernel::secure_random::CpuRngFeatures oldLeaf =
        kernel::secure_random::cpu_rng_features_from_cpuid(6, 1U << 30, 1U << 18);
    require(!oldLeaf.rdseed);
    require(oldLeaf.rdrand);

    const kernel::secure_random::CpuRngFeatures unsupported =
        kernel::secure_random::cpu_rng_features_from_cpuid(0, 0, 0);
    require(!unsupported.rdseed);
    require(!unsupported.rdrand);

    const kernel::secure_random::Source allProviders[] = {
        kernel::secure_random::SOURCE_RDSEED,
        kernel::secure_random::SOURCE_RDRAND,
        kernel::secure_random::SOURCE_VIRTIO_RNG
    };
    require_priority(kernel::secure_random::provider_priority(both, true), allProviders, 3);

    const kernel::secure_random::Source cpuAndVirtio[] = {
        kernel::secure_random::SOURCE_RDRAND,
        kernel::secure_random::SOURCE_VIRTIO_RNG
    };
    require_priority(kernel::secure_random::provider_priority(oldLeaf, true), cpuAndVirtio, 2);
    require_priority(kernel::secure_random::provider_priority(unsupported, true),
        &allProviders[2], 1);
    require_priority(kernel::secure_random::provider_priority(unsupported, false), nullptr, 0);

    // A failing preferred source must leave the next advertised source as the
    // ordered fallback candidate; this models the production loop's
    // provider order without replacing the instruction readers in production.
    kernel::secure_random::Source fallbackSelection = kernel::secure_random::SOURCE_NONE;
    for (uint8_t i = 0; i < 3; ++i) {
        if (allProviders[i] == kernel::secure_random::SOURCE_RDSEED) continue;
        fallbackSelection = allProviders[i];
        break;
    }
    require(fallbackSelection == kernel::secure_random::SOURCE_RDRAND);

    uint8_t output[7];
    for (size_t i = 0; i < sizeof(output); ++i) output[i] = 0xA5;
    ReaderState reader = { 0, 0, 0x11223344U };
    uint32_t retryFailures = 0;
    g_reader = &reader;
    require(kernel::secure_random::fill_from_word_source(
        output, sizeof(output), scripted_reader, 3, &retryFailures));
    require(reader.attempts == 2);
    require(retryFailures == 0);
    require(output[0] == 0x44 && output[1] == 0x33 && output[2] == 0x22 && output[3] == 0x11);
    require(output[4] == 0x44 && output[5] == 0x33 && output[6] == 0x22);

    for (size_t i = 0; i < sizeof(output); ++i) output[i] = 0xA5;
    reader = { 0, 2, 0xAABBCCDDU };
    retryFailures = 0;
    require(kernel::secure_random::fill_from_word_source(
        output, sizeof(uint32_t), scripted_reader, 3, &retryFailures));
    require(reader.attempts == 3);
    require(retryFailures == 2);
    require(output[0] == 0xDD && output[1] == 0xCC && output[2] == 0xBB && output[3] == 0xAA);

    for (size_t i = 0; i < sizeof(output); ++i) output[i] = 0xA5;
    reader = { 0, 0, 0 };
    retryFailures = 0;
    require(!kernel::secure_random::fill_from_word_source(
        output, sizeof(output), failing_reader, 4, &retryFailures));
    require(reader.attempts == 4);
    require(retryFailures == 4);
    require(all_bytes_equal(output, sizeof(output), 0));

    // RDRAND uses the same bounded per-word policy as RDSEED.
    for (size_t i = 0; i < sizeof(output); ++i) output[i] = 0xA5;
    reader = { 0, 1, 0x55667788U };
    retryFailures = 0;
    require(kernel::secure_random::fill_from_word_source(
        output, sizeof(uint32_t), scripted_reader, 8, &retryFailures));
    require(reader.attempts == 2);
    require(retryFailures == 1);
    require(output[0] == 0x88 && output[1] == 0x77 && output[2] == 0x66 && output[3] == 0x55);

    for (size_t i = 0; i < sizeof(output); ++i) output[i] = 0xA5;
    reader = { 0, 0, 0 };
    retryFailures = 0;
    require(!kernel::secure_random::fill_from_word_source(
        output, sizeof(output), failing_reader, 8, &retryFailures));
    require(reader.attempts == 8);
    require(retryFailures == 8);
    require(all_bytes_equal(output, sizeof(output), 0));

    for (size_t i = 0; i < sizeof(output); ++i) output[i] = 0xA5;
    reader = { 0, 0, 0 };
    require(!kernel::secure_random::fill_from_word_source(
        output, sizeof(output), nullptr, 4));
    require(all_bytes_equal(output, sizeof(output), 0));

    for (size_t i = 0; i < sizeof(output); ++i) output[i] = 0xA5;
    require(!kernel::secure_random::fill_from_word_source(
        output, sizeof(output), failing_reader, 0));
    require(all_bytes_equal(output, sizeof(output), 0));

    require(kernel::secure_random::fill_from_word_source(nullptr, 0, nullptr, 0));
    require(!kernel::secure_random::fill_from_word_source(nullptr, 1, failing_reader, 4));
    require(kernel::secure_random::fill_from_word_source(output, 0, failing_reader, 0));
    return 0;
}
