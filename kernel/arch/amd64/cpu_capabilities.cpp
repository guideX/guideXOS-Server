#include "include/arch/cpu_capabilities.h"

#if defined(_M_X64) && defined(_MSC_VER)
#include <intrin.h>
#endif

namespace kernel {
namespace arch {
namespace amd64 {
namespace {

constexpr uint32_t kExtendedRoot = 0x80000000u;
constexpr uint32_t kExtendedFeatures = 0x80000001u;
constexpr uint32_t kBrandStart = 0x80000002u;
constexpr uint32_t kBrandEnd = 0x80000004u;
constexpr uint32_t kExtendedPower = 0x80000007u;
constexpr uint32_t kAddressWidths = 0x80000008u;
constexpr uint32_t kAmdExtendedApicId = 0x8000001Eu;
constexpr uint32_t kAmdExtendedTopology = 0x80000026u;
constexpr unsigned kNxBit = 20;

bool native_cpuid(void*, uint32_t leaf, uint32_t subleaf, CpuidRegisters& regs)
{
#if defined(_M_X64) && defined(_MSC_VER)
    int values[4];
    __cpuidex(values, static_cast<int>(leaf), static_cast<int>(subleaf));
    regs = {static_cast<uint32_t>(values[0]), static_cast<uint32_t>(values[1]),
            static_cast<uint32_t>(values[2]), static_cast<uint32_t>(values[3])};
#elif defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    __asm__ volatile ("cpuid"
                      : "=a"(regs.eax), "=b"(regs.ebx), "=c"(regs.ecx), "=d"(regs.edx)
                      : "a"(leaf), "c"(subleaf));
#else
    (void)leaf;
    (void)subleaf;
    (void)regs;
    return false;
#endif
    return true;
}

CpuidBit bit(uint32_t value, unsigned index)
{
    return (value & (1u << index)) != 0 ? CpuidBit::Set : CpuidBit::Clear;
}

uint32_t max_extended_leaf(CpuidReader read, void* context)
{
    CpuidRegisters regs{};
    if (read(context, kExtendedRoot, 0, regs) &&
        regs.eax >= kExtendedRoot && regs.eax <= 0x8000FFFFu) {
        return regs.eax;
    }
    return 0;
}

void write_word(char* output, uint32_t word)
{
    for (unsigned i = 0; i < 4; ++i) {
        output[i] = static_cast<char>((word >> (i * 8)) & 0xFFu);
    }
}

void read_brand(CpuidReader read, void* context, char (&brand)[49])
{
    CpuidRegisters words[kBrandEnd - kBrandStart + 1]{};
    for (uint32_t i = 0; i <= kBrandEnd - kBrandStart; ++i) {
        if (!read(context, kBrandStart + i, 0, words[i])) return;
    }
    char* output = brand;
    for (const CpuidRegisters& regs : words) {
        write_word(output, regs.eax);
        write_word(output + 4, regs.ebx);
        write_word(output + 8, regs.ecx);
        write_word(output + 12, regs.edx);
        output += 16;
    }
}

bool read_topology_hint(CpuidReader read, void* context, uint32_t leaf,
                        CpuTopologySource source, CpuTopologyHint& hint)
{
    CpuidRegisters regs{};
    if (!read(context, leaf, 0, regs)) return false;

    const auto level_type = static_cast<uint8_t>((regs.ecx >> 8) & 0xFFu);
    const auto logical_count = static_cast<uint16_t>(regs.ebx & 0xFFFFu);
    if (level_type == 0 || logical_count == 0 || (regs.ecx & 0xFFu) != 0) return false;

    hint = {source, regs.edx, level_type, static_cast<uint8_t>(regs.eax & 0x1Fu), logical_count};
    return true;
}

bool vendor_is(const char* actual, const char* expected)
{
    for (unsigned i = 0; i < 12; ++i) {
        if (actual[i] != expected[i]) return false;
    }
    return true;
}

CpuTopologyHint topology_hint(CpuidReader read, void* context, const CpuCapabilities& cpu,
                              const CpuidRegisters& leaf1, bool amd_topology_extensions)
{
    CpuTopologyHint hint{};
    const bool amd = vendor_is(cpu.vendor, "AuthenticAMD");
    const bool amd_family = amd || vendor_is(cpu.vendor, "HygonGenuine");
    if (amd && cpu.max_extended_leaf >= kAmdExtendedTopology &&
        read_topology_hint(read, context, kAmdExtendedTopology, CpuTopologySource::AmdLeaf80000026, hint)) {
        return hint;
    }
    if (!amd_family && cpu.max_basic_leaf >= 0x1Fu &&
        read_topology_hint(read, context, 0x1Fu, CpuTopologySource::Leaf1F, hint)) {
        return hint;
    }
    if (cpu.max_basic_leaf >= 0x0Bu &&
        read_topology_hint(read, context, 0x0Bu, CpuTopologySource::Leaf0B, hint)) {
        return hint;
    }
    CpuidRegisters regs{};
    if (amd_family && amd_topology_extensions && cpu.max_extended_leaf >= kAmdExtendedApicId &&
        read(context, kAmdExtendedApicId, 0, regs)) {
        hint.source = CpuTopologySource::AmdLeaf8000001E;
        hint.initial_apic_id = regs.eax;
        return hint;
    }
    if (cpu.features.apic == CpuidBit::Set) {
        hint.source = CpuTopologySource::LegacyLeaf1;
        hint.initial_apic_id = leaf1.ebx >> 24;
        if (bit(leaf1.edx, 28) == CpuidBit::Set) {
            hint.logical_count_hint = static_cast<uint16_t>((leaf1.ebx >> 16) & 0xFFu);
        }
    }
    return hint;
}

} // namespace

CpuCapabilities decode_cpu_capabilities(CpuidReader read, void* context)
{
    CpuCapabilities result{};
    CpuidRegisters root{};
    if (!read || !read(context, 0, 0, root)) return result;

    result.cpuid_available = true;
    // Hypervisor and extended leaves have independent namespaces.
    if (root.eax < 0x40000000u) result.max_basic_leaf = root.eax;
    result.max_extended_leaf = max_extended_leaf(read, context);
    write_word(result.vendor, root.ebx);
    write_word(result.vendor + 4, root.edx);
    write_word(result.vendor + 8, root.ecx);

    CpuFeatures& features = result.features;
    CpuidRegisters leaf1{};
    if (result.max_basic_leaf >= 1 && read(context, 1, 0, leaf1)) {
        features.tsc = bit(leaf1.edx, 4);
        features.msr = bit(leaf1.edx, 5);
        features.apic = bit(leaf1.edx, 9);
        features.sse2 = bit(leaf1.edx, 26);
        features.pcid = bit(leaf1.ecx, 17);
        features.x2apic = bit(leaf1.ecx, 21);
        features.tsc_deadline = bit(leaf1.ecx, 24);
        features.xsave = bit(leaf1.ecx, 26);
        features.osxsave = bit(leaf1.ecx, 27);
        features.avx = bit(leaf1.ecx, 28);
        features.hypervisor = bit(leaf1.ecx, 31);
    }

    CpuidRegisters regs{};
    if (result.max_basic_leaf >= 7 && read(context, 7, 0, regs)) {
        features.avx2 = bit(regs.ebx, 5);
        features.smep = bit(regs.ebx, 7);
        features.smap = bit(regs.ebx, 20);
    }
    bool amd_topology_extensions = false;
    if (result.max_extended_leaf >= kExtendedFeatures && read(context, kExtendedFeatures, 0, regs)) {
        amd_topology_extensions = bit(regs.ecx, 22) == CpuidBit::Set;
        features.nx = bit(regs.edx, kNxBit);
        features.page_1g = bit(regs.edx, 26);
        features.rdtscp = bit(regs.edx, 27);
        features.long_mode = bit(regs.edx, 29);
    }
    if (result.max_extended_leaf >= kBrandEnd) read_brand(read, context, result.brand);
    if (result.max_extended_leaf >= kExtendedPower && read(context, kExtendedPower, 0, regs)) {
        features.invariant_tsc = bit(regs.edx, 8);
    }
    if (result.max_extended_leaf >= kAddressWidths && read(context, kAddressWidths, 0, regs)) {
        result.physical_address_bits = static_cast<uint8_t>(regs.eax & 0xFFu);
        result.linear_address_bits = static_cast<uint8_t>((regs.eax >> 8) & 0xFFu);
    }

    result.topology = topology_hint(read, context, result, leaf1, amd_topology_extensions);
    return result;
}

CpuCapabilities read_cpu_capabilities()
{
    return decode_cpu_capabilities(native_cpuid, nullptr);
}

bool cpu_supports_nx()
{
    CpuidRegisters regs{};
    return max_extended_leaf(native_cpuid, nullptr) >= kExtendedFeatures &&
           native_cpuid(nullptr, kExtendedFeatures, 0, regs) &&
           bit(regs.edx, kNxBit) == CpuidBit::Set;
}

} // namespace amd64
} // namespace arch
} // namespace kernel
