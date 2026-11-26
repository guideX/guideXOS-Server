#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <thread>
#include "arch/bsp_apic_base_probe.h"
#include "arch/bsp_xapic_admission.h"

using namespace kernel::arch::amd64;
using namespace guideXOS::acpi;
using Status = ApicBaseProbeStatus;

// The hosted test calls the frame matcher, never this original kernel entry.
extern "C" void exception_stub_13() { std::abort(); }

struct GpFrame {
    uint64_t error, rip, cs, rflags, rsp, ss;
};
extern "C" uint64_t gxos_test_apic_base_fault_rip();
extern "C" int gxos_test_apic_base_gp_matches(const GpFrame*, uint64_t, uint32_t, uint32_t);
extern "C" int gxos_test_apic_base_probe_state_clean();
extern "C" int gxos_test_apic_base_probe_context_idle();
extern "C" void gxos_test_apic_base_probe_set_context(uint64_t, uint32_t);
extern "C" int gxos_test_apic_base_publishes_index(uint32_t);

namespace {

constexpr uint64_t kBase = 0xFEE00000;
constexpr uint64_t kSample = kBase | 0x900;
const PerCpuState* active_roster;
std::atomic<bool> entered{false};
std::atomic<bool> release_reader{false};

CpuCapabilities supported()
{
    CpuCapabilities cpu{};
    cpu.cpuid_available = true;
    cpu.features.long_mode = cpu.features.apic = cpu.features.msr = CpuidBit::Set;
    cpu.topology.source = CpuTopologySource::LegacyLeaf1;
    cpu.topology.initial_apic_id = 7;
    cpu.physical_address_bits = 36;
    return cpu;
}

PerCpuState online_roster(const CpuCapabilities& cpu)
{
    ProcessorInventory inventory{};
    inventory.status = InventoryStatus::Complete;
    inventory.stored_count = inventory.advertised_count = 1;
    inventory.records[0].kind = ProcessorKind::LocalApic;
    inventory.records[0].apic_id = 7;
    inventory.records[0].flags = 1;
    PerCpuState roster;
    assert(roster.initialize_bootstrap(cpu, &inventory));
    assert(roster.mark_online(0));
    return roster;
}

Status unexpected_read(uint64_t&)
{
    std::abort();
}

Status successful_read(uint64_t& value)
{
    value = kSample;
    return Status::Observed;
}

void test_sampling_and_admission()
{
    const auto roster = online_roster(supported());
    const auto sample = sample_bootstrap_apic_base(roster, successful_read);
    assert(sample.status == Status::Observed && *sample.observation() == kSample);

    InterruptTopology madt{};
    madt.local_apic_address = kBase;
    assert(evaluate_bsp_xapic_admission(roster, &madt, sample.observation(), nullptr) ==
           XapicAdmission::OwnershipUnverified);
    assert(evaluate_bsp_xapic_admission(roster, &madt, nullptr, nullptr) ==
           XapicAdmission::MissingApicBaseSample);
    const uint64_t invalid_base = 0x900;
    const uint64_t x2apic_without_cpuid_support = kSample | (1u << 10);
    const uint64_t no_bsp_bit = kSample & ~(1u << 8);
    const uint64_t disabled = kSample & ~(1u << 11);
    for (const uint64_t* value : {&x2apic_without_cpuid_support, &no_bsp_bit, &disabled}) {
        assert(evaluate_bsp_xapic_admission(roster, &madt, value, nullptr) == XapicAdmission::UnsupportedMode);
    }
    assert(evaluate_bsp_xapic_admission(roster, &madt, &invalid_base, nullptr) == XapicAdmission::InvalidAperture);
    madt.local_apic_address += 4096;
    assert(evaluate_bsp_xapic_admission(roster, &madt, sample.observation(), nullptr) == XapicAdmission::BaseMismatch);
    auto other_cpu = supported();
    other_cpu.topology.initial_apic_id = 8;
    const auto unmatched_roster = online_roster(other_cpu);
    assert(evaluate_bsp_xapic_admission(unmatched_roster, &madt, sample.observation(), nullptr) ==
           XapicAdmission::FirmwareInvalid);
}

void test_context_and_capabilities()
{
    PerCpuState roster;
    assert(sample_bootstrap_apic_base(roster, unexpected_read).status == Status::InvalidContext);
    assert(roster.initialize_bootstrap(supported(), nullptr));
    assert(sample_bootstrap_apic_base(roster, unexpected_read).status == Status::InvalidContext);
    assert(roster.mark_online(0));
    assert(sample_bootstrap_apic_base(roster, nullptr).status == Status::InvalidContext);
    for (auto feature : {&CpuFeatures::apic, &CpuFeatures::msr, &CpuFeatures::long_mode}) {
        for (auto bit : {CpuidBit::Clear, CpuidBit::Unreported}) {
            auto cpu = supported();
            cpu.features.*feature = bit;
            assert(sample_bootstrap_apic_base(online_roster(cpu), unexpected_read).status == Status::UnsupportedCpu);
        }
    }
    auto cpu = supported();
    cpu.cpuid_available = false;
    assert(sample_bootstrap_apic_base(online_roster(cpu), unexpected_read).status == Status::UnsupportedCpu);
}

void test_failures_reentry_and_cleanup()
{
    const auto roster = online_roster(supported());
    active_roster = &roster;
    const auto fault = sample_bootstrap_apic_base(roster, [](uint64_t& value) {
        assert(sample_bootstrap_apic_base(*active_roster, unexpected_read).status == Status::Busy);
        value = kSample; // Even a backend that supplied a value cannot publish a failed sample.
        return Status::GeneralProtection;
    });
    assert(fault.status == Status::GeneralProtection && !fault.observation());
    const auto bad_context = sample_bootstrap_apic_base(roster, [](uint64_t&) { return Status::InvalidContext; });
    assert(bad_context.status == Status::InvalidContext && !bad_context.observation());

    std::thread owner([&] {
        const auto sample = sample_bootstrap_apic_base(roster, [](uint64_t& value) {
            entered.store(true);
            while (!release_reader.load()) std::this_thread::yield();
            return successful_read(value);
        });
        assert(sample.status == Status::Observed);
    });
    while (!entered.load()) std::this_thread::yield();
    assert(sample_bootstrap_apic_base(roster, unexpected_read).status == Status::Busy);
    release_reader.store(true);
    owner.join();
    assert(sample_bootstrap_apic_base(roster, successful_read).status == Status::Observed);
}

void test_assembly_frame_matcher()
{
    const GpFrame expected{0, gxos_test_apic_base_fault_rip(), 8, 0x10002, 0x123408, 0x10};
    assert(gxos_test_apic_base_gp_matches(&expected, expected.rsp, 0x1B, 13));
    assert(gxos_test_apic_base_probe_state_clean());
    assert(!gxos_test_apic_base_gp_matches(&expected, 0, 0x1B, 13));
    assert(gxos_test_apic_base_probe_state_clean());
    assert(!gxos_test_apic_base_gp_matches(&expected, expected.rsp + 16, 0x1B, 13));
    assert(gxos_test_apic_base_probe_state_clean());
    assert(!gxos_test_apic_base_gp_matches(&expected, expected.rsp, 0x1A, 13));
    assert(gxos_test_apic_base_probe_state_clean());
    for (uint32_t vector = 0; vector < 256; ++vector) {
        if (vector != 13) assert(!gxos_test_apic_base_gp_matches(&expected, expected.rsp, 0x1B, vector));
    }

    for (auto field : {&GpFrame::error, &GpFrame::rip, &GpFrame::rsp, &GpFrame::ss}) {
        auto frame = expected;
        ++(frame.*field);
        assert(!gxos_test_apic_base_gp_matches(&frame, expected.rsp, 0x1B, 13));
    }
    auto frame = expected;
    frame.cs = 0x23;
    assert(!gxos_test_apic_base_gp_matches(&frame, expected.rsp, 0x1B, 13));
    frame.cs = 0x18; // CPL0 with a different selector still cannot claim the fixup.
    assert(!gxos_test_apic_base_gp_matches(&frame, expected.rsp, 0x1B, 13));
    for (int bit : {8, 9, 10, 14, 17}) {
        frame = expected;
        frame.rflags |= 1ull << bit;
        assert(!gxos_test_apic_base_gp_matches(&frame, expected.rsp, 0x1B, 13));
        assert(gxos_test_apic_base_probe_state_clean());
    }
}

void test_stale_probe_context_rejection()
{
    assert(gxos_test_apic_base_probe_context_idle());
    gxos_test_apic_base_probe_set_context(0x12345000, 0x1B);
    assert(!gxos_test_apic_base_probe_context_idle());
    gxos_test_apic_base_probe_set_context(0, 0x1B);
    assert(!gxos_test_apic_base_probe_context_idle());
    gxos_test_apic_base_probe_set_context(0, 0);
    assert(gxos_test_apic_base_probe_context_idle());
    assert(gxos_test_apic_base_probe_state_clean());
}

void test_qualification_indices()
{
    // CPU7B3: the qualification lane arms the same exact-site matcher with
    // non-0x1B indices. The matcher compares ECX with the armed index, so a
    // #GP for one index can never claim the fixup armed for another.
    // The hosted matcher arms 0x1B, so only ECX=0x1B may match.
    const GpFrame expected{0, gxos_test_apic_base_fault_rip(), 8, 0x10002, 0x123408, 0x10};
    for (uint32_t ecx : {0x1Bu, 0x0u, 0x800u, 0x801u, 0x8000001Bu, 0xFFFFFFFFu}) {
        assert(gxos_test_apic_base_gp_matches(&expected, expected.rsp, ecx, 13) == (ecx == 0x1Bu));
        assert(gxos_test_apic_base_probe_state_clean());
    }
    for (uint32_t armed : {0x800u, 0xFFFFFFFFu}) {
        gxos_test_apic_base_probe_set_context(0, armed);
        assert(!gxos_test_apic_base_probe_context_idle());
        gxos_test_apic_base_probe_set_context(0, 0);
        assert(gxos_test_apic_base_probe_context_idle());
    }

    // Only IA32_APIC_BASE may publish a returned value. Reserved and
    // x2APIC-range qualification indices stay fail-closed if RDMSR returns.
    assert(gxos_test_apic_base_publishes_index(0x1B));
    for (uint32_t index : {0x0u, 0x1Au, 0x1Cu, 0x800u, 0x802u, 0x8FFu, 0x8000001Bu, 0xFFFFFFFFu}) {
        assert(!gxos_test_apic_base_publishes_index(index));
    }
}

} // namespace

int main()
{
    test_sampling_and_admission();
    test_context_and_capabilities();
    test_failures_reentry_and_cleanup();
    test_assembly_frame_matcher();
    test_stale_probe_context_rejection();
    test_qualification_indices();
    std::puts("CPU7B2/CPU7B3 hosted: 6 groups passed (simulated read results, raw #GP frame matching, "
              "qualification index publication)");
}
