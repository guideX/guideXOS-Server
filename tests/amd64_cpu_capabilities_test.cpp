#include "../kernel/arch/amd64/include/arch/cpu_capabilities.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace kernel::arch::amd64;

#define CHECK(expression) do { \
    if (!(expression)) { \
        std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expression); \
        std::exit(1); \
    } \
} while (false)

namespace {

struct Leaf {
    uint32_t id;
    CpuidRegisters regs;
    bool available = true;
};

struct Fixture {
    const Leaf* next;
    const Leaf* end;
};

bool read_fixture(void* context, uint32_t leaf, uint32_t subleaf, CpuidRegisters& regs)
{
    auto& fixture = *static_cast<Fixture*>(context);
    if (fixture.next == fixture.end || leaf != fixture.next->id || subleaf != 0) {
        std::fprintf(stderr, "Unexpected CPUID query leaf=%08x subleaf=%08x\n", leaf, subleaf);
        std::exit(1);
    }
    const Leaf& expected = *fixture.next++;
    regs = expected.regs;
    return expected.available;
}

template <size_t N>
CpuCapabilities decode(const Leaf (&leaves)[N])
{
    Fixture fixture{leaves, leaves + N};
    const CpuCapabilities result = decode_cpu_capabilities(read_fixture, &fixture);
    CHECK(fixture.next == fixture.end);
    return result;
}

constexpr uint32_t word(const char* chars)
{
    return uint32_t(chars[0]) | (uint32_t(chars[1]) << 8) |
           (uint32_t(chars[2]) << 16) | (uint32_t(chars[3]) << 24);
}

constexpr CpuidRegisters intel_root(uint32_t max_leaf)
{
    return {max_leaf, word("Genu"), word("ntel"), word("ineI")};
}

void test_unavailable_cpuid()
{
    const CpuCapabilities no_reader = decode_cpu_capabilities(nullptr, nullptr);
    CHECK(!no_reader.cpuid_available && no_reader.vendor[0] == '\0');
    const CpuCapabilities missing = decode({{0, intel_root(0x1F), false}});
    CHECK(!missing.cpuid_available && missing.max_basic_leaf == 0);
    CHECK(missing.features.apic == CpuidBit::Unreported);
    CHECK(missing.topology.source == CpuTopologySource::Unavailable);
}

void test_leaf_zero_and_invalid_extended_max()
{
    const CpuCapabilities result = decode({
        {0, intel_root(0)},
        {0x80000000u, intel_root(7)}, // An unsupported request may alias a basic leaf.
    });
    CHECK(result.cpuid_available && result.max_basic_leaf == 0 && result.max_extended_leaf == 0);
    CHECK(std::strcmp(result.vendor, "GenuineIntel") == 0);
    CHECK(result.brand[0] == '\0');
    CHECK(result.physical_address_bits == 0 && result.linear_address_bits == 0);
    CHECK(result.features.avx == CpuidBit::Unreported);
    CHECK(result.features.nx == CpuidBit::Unreported);

    const CpuCapabilities bad_max = decode({
        {0, intel_root(0x80000008u)},
        {0x80000000u, {0xC0000001u, 0, 0, 0}},
    });
    CHECK(bad_max.max_basic_leaf == 0 && bad_max.max_extended_leaf == 0);
}

void test_features_identity_and_preferred_topology()
{
    const CpuCapabilities result = decode({
        {0, intel_root(0x1F)},
        {0x80000000u, {0x80000008u, 0, 0, 0}},
        {1, {0, 0, (1u << 17) | (1u << 21) | (1u << 24) | (1u << 26) | (1u << 28),
                (1u << 4) | (1u << 5) | (1u << 9) | (1u << 26)}},
        {7, {0, (1u << 5) | (1u << 7) | (1u << 20), 0, 0}},
        {0x80000001u, {0, 0, 0, (1u << 20) | (1u << 26) | (1u << 27) | (1u << 29)}},
        {0x80000002u, {word("0123"), word("4567"), word("89AB"), word("CDEF")}},
        {0x80000003u, {word("GHIJ"), word("KLMN"), word("OPQR"), word("STUV")}},
        {0x80000004u, {word("WXYZ"), word("abcd"), word("efgh"), word("ijkl")}},
        {0x80000007u, {0, 0, 0, 1u << 8}},
        {0x80000008u, {0xDD003934u, 0, 0, 0}},
        {0x1F, {0xE1, 0xBEEF0002u, 0x0100, 0x10203}},
    });
    CHECK(result.cpuid_available && result.max_basic_leaf == 0x1F && result.max_extended_leaf == 0x80000008u);
    CHECK(std::strcmp(result.brand, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijkl") == 0);
    CHECK(result.physical_address_bits == 52 && result.linear_address_bits == 57);

    const CpuFeatures& f = result.features;
    CHECK(f.tsc == CpuidBit::Set && f.msr == CpuidBit::Set && f.apic == CpuidBit::Set);
    CHECK(f.sse2 == CpuidBit::Set && f.pcid == CpuidBit::Set && f.x2apic == CpuidBit::Set);
    CHECK(f.tsc_deadline == CpuidBit::Set && f.xsave == CpuidBit::Set && f.osxsave == CpuidBit::Clear);
    CHECK(f.avx == CpuidBit::Set && f.avx2 == CpuidBit::Set && f.hypervisor == CpuidBit::Clear);
    CHECK(f.smep == CpuidBit::Set && f.smap == CpuidBit::Set && f.nx == CpuidBit::Set);
    CHECK(f.page_1g == CpuidBit::Set && f.rdtscp == CpuidBit::Set && f.long_mode == CpuidBit::Set);
    CHECK(f.invariant_tsc == CpuidBit::Set);

    const CpuTopologyHint& topology = result.topology;
    CHECK(topology.source == CpuTopologySource::Leaf1F && topology.initial_apic_id == 0x10203u);
    CHECK(topology.level_type == 1 && topology.apic_id_shift == 1 && topology.logical_count_hint == 2);
}

void test_empty_1f_falls_back_to_0b()
{
    const CpuCapabilities result = decode({
        {0, intel_root(0x1F)},
        {0x80000000u, {0x80000000u, 0, 0, 0}},
        {1, {0, 0, (1u << 27) | (1u << 31), 0}},
        {7, {}},
        {0x1F, {0, 0, 0, 123}},
        {0x0B, {0, 1, 0x100, 0}},
    });
    CHECK(result.max_extended_leaf == 0x80000000u);
    CHECK(result.features.osxsave == CpuidBit::Set && result.features.avx == CpuidBit::Clear);
    CHECK(result.features.hypervisor == CpuidBit::Set && result.features.smap == CpuidBit::Clear);
    CHECK(result.topology.source == CpuTopologySource::Leaf0B && result.topology.initial_apic_id == 0);
    CHECK(result.topology.level_type == 1 && result.topology.apic_id_shift == 0);
    CHECK(result.topology.logical_count_hint == 1);
}

void test_unusable_structured_topology_falls_back_to_legacy()
{
    const CpuCapabilities result = decode({
        {0, {0x1F, word("Auth"), word("cAMD"), word("enti")}},
        {0x80000000u, {}},
        {1, {0, (0xABu << 24) | (16u << 16), 0, (1u << 9) | (1u << 28)}},
        {7, {}},
        {0x0B, {1, 2, 0, 1}},
    });
    CHECK(std::strcmp(result.vendor, "AuthenticAMD") == 0);
    CHECK(result.topology.source == CpuTopologySource::LegacyLeaf1 && result.topology.initial_apic_id == 0xAB);
    CHECK(result.topology.level_type == 0 && result.topology.apic_id_shift == 0);
    CHECK(result.topology.logical_count_hint == 16);
}

void test_legacy_field_masks()
{
    const CpuCapabilities no_htt = decode({
        {0, intel_root(1)}, {0x80000000u, {}, false},
        {1, {0, 0x01080000u, 0, 1u << 9}},
    });
    CHECK(no_htt.topology.source == CpuTopologySource::LegacyLeaf1 && no_htt.topology.initial_apic_id == 1);
    CHECK(no_htt.topology.logical_count_hint == 0);

    const CpuCapabilities no_apic = decode({
        {0, intel_root(1)}, {0x80000000u, {0x80000001u, 0, 0, 0}},
        {1, {0, 0x01080000u, 0, 1u << 28}}, {0x80000001u, {}},
    });
    CHECK(no_apic.features.apic == CpuidBit::Clear && no_apic.features.nx == CpuidBit::Clear);
    CHECK(no_apic.topology.source == CpuTopologySource::Unavailable);
}

void test_brand_requires_all_three_leaves()
{
    const Leaf prefix{0x80000002u, {word("part"), word("ial "), word("bran"), word("d   ")}};
    const CpuCapabilities below_brand_max = decode({
        {0, intel_root(0)}, {0x80000000u, {0x80000003u, 0, 0, 0}}, {0x80000001u, {}},
    });
    CHECK(below_brand_max.brand[0] == '\0');

    const CpuCapabilities failed_read = decode({
        {0, intel_root(0)}, {0x80000000u, {0x80000004u, 0, 0, 0}}, {0x80000001u, {}},
        prefix, {0x80000003u, {}, false},
    });
    const char empty[49]{};
    CHECK(std::memcmp(failed_read.brand, empty, sizeof(empty)) == 0);
}

void test_failed_feature_reads_and_independent_widths()
{
    const CpuCapabilities result = decode({
        {0, intel_root(7)}, {0x80000000u, {0x80000008u, 0, 0, 0}},
        {1, {0, 0x01080000u, ~0u, ~0u}, false}, {7, {0, ~0u, 0, 0}, false},
        {0x80000001u, {0, 0, 0, ~0u}, false}, {0x80000002u, {}, false},
        {0x80000007u, {}}, {0x80000008u, {48, 0, 0, 0}},
    });
    CHECK(result.features.apic == CpuidBit::Unreported && result.features.osxsave == CpuidBit::Unreported);
    CHECK(result.features.avx2 == CpuidBit::Unreported && result.features.nx == CpuidBit::Unreported);
    CHECK(result.features.invariant_tsc == CpuidBit::Clear);
    CHECK(result.topology.source == CpuTopologySource::Unavailable);
    CHECK(result.physical_address_bits == 48 && result.linear_address_bits == 0);
}

} // namespace

int main()
{
    test_unavailable_cpuid();
    test_leaf_zero_and_invalid_extended_max();
    test_features_identity_and_preferred_topology();
    test_empty_1f_falls_back_to_0b();
    test_unusable_structured_topology_falls_back_to_legacy();
    test_legacy_field_masks();
    test_brand_requires_all_three_leaves();
    test_failed_feature_reads_and_independent_widths();
    std::puts("PASS: 8 AMD64 CPUID fixture groups");

    const CpuCapabilities native = read_cpu_capabilities();
    CHECK(native.cpuid_available);
    CHECK(native.vendor[12] == '\0' && native.brand[48] == '\0');
    CHECK(cpu_supports_nx() == (native.features.nx == CpuidBit::Set));
    std::puts("PASS: hosted AMD64 native CPUID adapter and NX query");
}
