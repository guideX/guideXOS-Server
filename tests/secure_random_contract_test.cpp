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

    uint8_t output[7];
    for (size_t i = 0; i < sizeof(output); ++i) output[i] = 0xA5;
    ReaderState reader = { 0, 0, 0x11223344U };
    g_reader = &reader;
    require(kernel::secure_random::fill_from_word_source(
        output, sizeof(output), scripted_reader, 3));
    require(reader.attempts == 2);
    require(output[0] == 0x44 && output[1] == 0x33 && output[2] == 0x22 && output[3] == 0x11);
    require(output[4] == 0x44 && output[5] == 0x33 && output[6] == 0x22);

    for (size_t i = 0; i < sizeof(output); ++i) output[i] = 0xA5;
    reader = { 0, 2, 0xAABBCCDDU };
    require(kernel::secure_random::fill_from_word_source(
        output, sizeof(uint32_t), scripted_reader, 3));
    require(reader.attempts == 3);
    require(output[0] == 0xDD && output[1] == 0xCC && output[2] == 0xBB && output[3] == 0xAA);

    for (size_t i = 0; i < sizeof(output); ++i) output[i] = 0xA5;
    reader = { 0, 0, 0 };
    require(!kernel::secure_random::fill_from_word_source(
        output, sizeof(output), failing_reader, 4));
    require(reader.attempts == 4);
    require(all_bytes_equal(output, sizeof(output), 0));

    for (size_t i = 0; i < sizeof(output); ++i) output[i] = 0xA5;
    reader = { 0, 0, 0 };
    require(!kernel::secure_random::fill_from_word_source(
        output, sizeof(output), nullptr, 4));
    require(all_bytes_equal(output, sizeof(output), 0));

    require(kernel::secure_random::fill_from_word_source(nullptr, 0, nullptr, 0));
    require(!kernel::secure_random::fill_from_word_source(nullptr, 1, failing_reader, 4));
    require(kernel::secure_random::fill_from_word_source(output, 0, failing_reader, 0));
    return 0;
}
