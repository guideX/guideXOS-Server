#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <map>
#include <string>
#include <vector>

#include "../aarch64/phase2/phase2_platform.h"
#include "../kernel/arch/arm64/gicv3_controller.h"
#include "../kernel/core/include/kernel/arch_interface.h"
#include "../kernel/core/include/kernel/common_scheduler.h"
#include "../kernel/core/include/kernel/irq_registry.h"

namespace kernel {
namespace arch {
namespace {
static uint64_t gHostedContexts[16];
static uint32_t gHostedContextCount = 0;
}

interrupt_state_t irq_save() { return 0; }
void irq_restore(interrupt_state_t) {}
void irq_enable() {}
void irq_disable() {}
void idle() {}
void memory_barrier() {}
const char* interface_name() { return "hosted GICv3 integration model"; }
void* context_init(uint64_t, uint64_t, thread_entry_t, void*)
{
    if (gHostedContextCount >= 16) return nullptr;
    return &gHostedContexts[gHostedContextCount++];
}
void context_switch(void**, void*) {}
bool interrupt_frame_save(void* frame, void* destination)
{
    return frame != nullptr && destination != nullptr;
}
bool context_to_interrupt(void* context, void* destination)
{
    if (!context || !destination) return false;
    uint64_t* words = static_cast<uint64_t*>(destination);
    for (uint32_t i = 0; i < kInterruptContextWords; ++i) words[i] = 0;
    words[0] = reinterpret_cast<uint64_t>(context);
    return true;
}
} // namespace arch
} // namespace kernel

namespace {

static const uint64_t kGicd = UINT64_C(0x08000000);
static const uint64_t kGicr = UINT64_C(0x080a0000);
static const uint64_t kUart = UINT64_C(0x09000000);
static const uint64_t kRam = UINT64_C(0x40000000);
static const uint32_t kSpurious = 1023;

static uint32_t gAssertions = 0;
static uint32_t gFailures = 0;

static bool expect(bool condition, const char* name)
{
    ++gAssertions;
    if (!condition) {
        ++gFailures;
        fprintf(stderr, "FAIL: %s\n", name);
        return false;
    }
    return true;
}

static void append_be32(std::vector<uint8_t>& bytes, uint32_t value)
{
    bytes.push_back(static_cast<uint8_t>(value >> 24));
    bytes.push_back(static_cast<uint8_t>(value >> 16));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
    bytes.push_back(static_cast<uint8_t>(value));
}

static void set_be32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
{
    bytes[offset] = static_cast<uint8_t>(value >> 24);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 16);
    bytes[offset + 2] = static_cast<uint8_t>(value >> 8);
    bytes[offset + 3] = static_cast<uint8_t>(value);
}

static void append_u64_cells(std::vector<uint8_t>& bytes, uint64_t value)
{
    append_be32(bytes, static_cast<uint32_t>(value >> 32));
    append_be32(bytes, static_cast<uint32_t>(value));
}

class FdtBuilder {
public:
    FdtBuilder() : structure_(), strings_(), name_offsets_() {}

    void begin(const char* name)
    {
        append_be32(structure_, 1);
        for (const char* p = name; *p; ++p) structure_.push_back(static_cast<uint8_t>(*p));
        structure_.push_back(0);
        align_structure();
    }

    void end()
    {
        append_be32(structure_, 2);
    }

    void property(const char* name, const std::vector<uint8_t>& value)
    {
        append_be32(structure_, 3);
        append_be32(structure_, static_cast<uint32_t>(value.size()));
        append_be32(structure_, name_offset(name));
        structure_.insert(structure_.end(), value.begin(), value.end());
        align_structure();
    }

    void property_cells(const char* name, const std::vector<uint32_t>& cells)
    {
        std::vector<uint8_t> value;
        for (uint32_t cell : cells) append_be32(value, cell);
        property(name, value);
    }

    void property_text(const char* name, const char* text)
    {
        std::vector<uint8_t> value;
        for (const char* p = text; *p; ++p) value.push_back(static_cast<uint8_t>(*p));
        value.push_back(0);
        property(name, value);
    }

    void property_stringlist(const char* name, const char* first, const char* second)
    {
        std::vector<uint8_t> value;
        for (const char* p = first; *p; ++p) value.push_back(static_cast<uint8_t>(*p));
        value.push_back(0);
        for (const char* p = second; *p; ++p) value.push_back(static_cast<uint8_t>(*p));
        value.push_back(0);
        property(name, value);
    }

    std::vector<uint8_t> finish()
    {
        append_be32(structure_, 9);
        std::vector<uint8_t> result(56, 0);
        result.insert(result.end(), structure_.begin(), structure_.end());
        while ((result.size() & 3u) != 0) result.push_back(0);
        const uint32_t strings_offset = static_cast<uint32_t>(result.size());
        result.insert(result.end(), strings_.begin(), strings_.end());
        set_be32(result, 0, UINT32_C(0xd00dfeed));
        set_be32(result, 4, static_cast<uint32_t>(result.size()));
        set_be32(result, 8, 56);
        set_be32(result, 12, strings_offset);
        set_be32(result, 16, 40);
        set_be32(result, 20, 17);
        set_be32(result, 24, 16);
        set_be32(result, 28, 0);
        set_be32(result, 32, static_cast<uint32_t>(strings_.size()));
        set_be32(result, 36, static_cast<uint32_t>(structure_.size()));
        return result;
    }

private:
    void align_structure()
    {
        while ((structure_.size() & 3u) != 0) structure_.push_back(0);
    }

    uint32_t name_offset(const char* name)
    {
        const std::string key(name);
        std::map<std::string, uint32_t>::const_iterator found = name_offsets_.find(key);
        if (found != name_offsets_.end()) return found->second;
        const uint32_t offset = static_cast<uint32_t>(strings_.size());
        for (const char* p = name; *p; ++p) strings_.push_back(static_cast<uint8_t>(*p));
        strings_.push_back(0);
        name_offsets_[key] = offset;
        return offset;
    }

    std::vector<uint8_t> structure_;
    std::vector<uint8_t> strings_;
    std::map<std::string, uint32_t> name_offsets_;
};

static std::vector<uint8_t> reg_tuple(uint64_t base, uint64_t size)
{
    std::vector<uint8_t> result;
    append_u64_cells(result, base);
    append_u64_cells(result, size);
    return result;
}

static void append_bytes(std::vector<uint8_t>& target, const std::vector<uint8_t>& source)
{
    target.insert(target.end(), source.begin(), source.end());
}

static std::vector<uint8_t> build_dtb(uint32_t version, uint64_t gicd_size = 0x10000,
                                      uint64_t gicr_size = 0x40000,
                                      uint32_t stride = 0,
                                      bool gic_available = true,
                                      bool translated_uart = false,
                                      uint64_t gicr_base = kGicr,
                                      uint64_t second_gicr_base = 0)
{
    FdtBuilder fdt;
    fdt.begin("");
    fdt.property_stringlist("compatible", "linux,dummy-virt", "qemu,virt");
    fdt.property_cells("#address-cells", {2});
    fdt.property_cells("#size-cells", {2});

    fdt.begin("memory@40000000");
    fdt.property_text("device_type", "memory");
    fdt.property("reg", reg_tuple(kRam, UINT64_C(0x10000000)));
    fdt.end();

    fdt.begin("cpus");
    fdt.property_cells("#address-cells", {2});
    fdt.property_cells("#size-cells", {0});
    fdt.begin("cpu@0");
    fdt.property_text("device_type", "cpu");
    fdt.property("reg", std::vector<uint8_t>{0, 0, 0, 0, 0, 0, 0, 0});
    fdt.end();
    fdt.end();

    if (translated_uart) {
        fdt.begin("bus@0");
        fdt.property_cells("#address-cells", {1});
        fdt.property_cells("#size-cells", {1});
        fdt.property_cells("ranges", {0, 0, 0x09000000, 0x10000});
        fdt.begin("serial@1000");
        fdt.property_text("compatible", "arm,pl011");
        fdt.property_cells("reg", {0x1000, 0x1000});
        fdt.end();
        fdt.end();
    } else {
        fdt.begin("serial@9000000");
        fdt.property_text("compatible", "arm,pl011");
        fdt.property("reg", reg_tuple(kUart, 0x1000));
        fdt.end();
    }

    fdt.begin(version == GXOS_AARCH64_GIC_V2 ? "interrupt-controller@8000000" : "interrupt-controller@8000000");
    fdt.property_text("compatible", version == GXOS_AARCH64_GIC_V2 ? "arm,gic-v2" : "arm,gic-v3");
    fdt.property("interrupt-controller", {});
    fdt.property_cells("#interrupt-cells", {3});
    std::vector<uint8_t> gic_regs;
    append_bytes(gic_regs, reg_tuple(kGicd, version == GXOS_AARCH64_GIC_V2 ? 0x1000 : gicd_size));
    if (version == GXOS_AARCH64_GIC_V2) {
        append_bytes(gic_regs, reg_tuple(UINT64_C(0x08010000), 0x2000));
    } else {
        fdt.property_cells("#redistributor-regions", {second_gicr_base ? 2u : 1u});
        if (stride != 0) fdt.property_cells("redistributor-stride", {stride});
        append_bytes(gic_regs, reg_tuple(gicr_base, gicr_size));
        if (second_gicr_base != 0) append_bytes(gic_regs, reg_tuple(second_gicr_base, gicr_size));
    }
    fdt.property("reg", gic_regs);
    if (!gic_available) fdt.property_text("status", "disabled");
    fdt.end();

    if (version == GXOS_AARCH64_GIC_V2) {
        // Add a GICv2 redistributor-shaped region is intentionally omitted.
    }
    fdt.begin("timer");
    fdt.property_text("compatible", "arm,armv8-timer");
    fdt.property_cells("interrupts", {1, 13, 0x104, 1, 14, 0x104,
                                       1, 11, 0x104, 1, 10, 0x104});
    fdt.end();
    fdt.end();
    return fdt.finish();
}

static std::vector<uint8_t> build_unknown_gic_dtb()
{
    std::vector<uint8_t> dtb = build_dtb(GXOS_AARCH64_GIC_V2);
    // The compatibility string list is tested separately by constructing a
    // minimal unknown interrupt-controller fixture below.
    FdtBuilder fdt;
    fdt.begin("");
    fdt.property_text("compatible", "qemu,virt");
    fdt.property_cells("#address-cells", {2});
    fdt.property_cells("#size-cells", {2});
    fdt.begin("intc@8000000");
    fdt.property_text("compatible", "vendor,unknown-gic");
    fdt.property("interrupt-controller", {});
    fdt.property_cells("#interrupt-cells", {3});
    fdt.property("reg", reg_tuple(kGicd, 0x10000));
    fdt.end();
    fdt.end();
    (void)dtb;
    return fdt.finish();
}

struct GicModel {
    std::map<uint64_t, uint64_t> registers;
    uint64_t system[10];
    uint32_t next_ack;
    uint32_t eoir_count;
    uint32_t dir_count;
    uint32_t eoir_value;
    uint32_t dir_value;
    uint32_t barriers;
    bool force_distributor_rwp;
    bool force_redistributor_rwp;
    bool force_wake_timeout;
    bool ignore_sre_write;
    bool force_split_eoi;

    GicModel()
        : registers(), system(), next_ack(kSpurious), eoir_count(0), dir_count(0),
          eoir_value(0), dir_value(0), barriers(0), force_distributor_rwp(false),
          force_redistributor_rwp(false), force_wake_timeout(false), ignore_sre_write(false),
          force_split_eoi(false)
    {
        system[static_cast<uint32_t>(arm64::gicv3::SystemRegister::IdAa64Pfr0El1)] = UINT64_C(1) << 24;
        system[static_cast<uint32_t>(arm64::gicv3::SystemRegister::MpidrEl1)] = UINT64_C(1) << 32;
        system[static_cast<uint32_t>(arm64::gicv3::SystemRegister::SreEl1)] = 0;
        system[static_cast<uint32_t>(arm64::gicv3::SystemRegister::CtlrEl1)] = 0;
        registers[kGicd + 0x0004] = 7; // 256 INTIDs.
        registers[kGicd + 0xffe8] = UINT32_C(3) << 4;
        registers[kGicr + 0x0008] = (UINT64_C(1) << 56) | (UINT64_C(1) << 4);
        registers[kGicr + 0xffe8] = UINT32_C(3) << 4;
        registers[kGicr + 0x0000] = 0;
        registers[kGicr + 0x0014] = 6;
    }
};

static uint64_t address(uint64_t base, uint32_t offset)
{
    return base + offset;
}

static uint32_t model_read32(void* context, uint64_t base, uint32_t offset)
{
    GicModel* model = static_cast<GicModel*>(context);
    const uint64_t key = address(base, offset);
    if (base == kGicd && offset == 0 && model->force_distributor_rwp) {
        return static_cast<uint32_t>(model->registers[key]) | (UINT32_C(1) << 31);
    }
    if (base >= kGicr && base < kGicr + UINT64_C(0x40000) &&
        offset == 0 && model->force_redistributor_rwp) return UINT32_C(1) << 3;
    std::map<uint64_t, uint64_t>::const_iterator found = model->registers.find(key);
    return found == model->registers.end() ? 0 : static_cast<uint32_t>(found->second);
}

static uint64_t model_read64(void* context, uint64_t base, uint32_t offset)
{
    GicModel* model = static_cast<GicModel*>(context);
    const uint64_t key = address(base, offset);
    std::map<uint64_t, uint64_t>::const_iterator found = model->registers.find(key);
    return found == model->registers.end() ? 0 : found->second;
}

static uint8_t model_read8(void* context, uint64_t base, uint32_t offset)
{
    GicModel* model = static_cast<GicModel*>(context);
    const uint64_t key = address(base, offset);
    std::map<uint64_t, uint64_t>::const_iterator found = model->registers.find(key);
    return found == model->registers.end() ? 0 : static_cast<uint8_t>(found->second);
}

static void model_write32(void* context, uint64_t base, uint32_t offset, uint32_t value)
{
    GicModel* model = static_cast<GicModel*>(context);
    const uint64_t key = address(base, offset);
    if (base >= kGicr && base < kGicr + UINT64_C(0x40000) &&
        offset == 0x14 && !model->force_wake_timeout) {
        value &= ~(UINT32_C(1) << 2);
    }
    model->registers[key] = value;
}

static void model_write64(void* context, uint64_t base, uint32_t offset, uint64_t value)
{
    GicModel* model = static_cast<GicModel*>(context);
    model->registers[address(base, offset)] = value;
}

static void model_write8(void* context, uint64_t base, uint32_t offset, uint8_t value)
{
    GicModel* model = static_cast<GicModel*>(context);
    model->registers[address(base, offset)] = value;
}

static uint64_t model_sys_read(void* context, arm64::gicv3::SystemRegister reg)
{
    GicModel* model = static_cast<GicModel*>(context);
    if (reg == arm64::gicv3::SystemRegister::Iar1El1) return model->next_ack;
    return model->system[static_cast<uint32_t>(reg)];
}

static void model_sys_write(void* context, arm64::gicv3::SystemRegister reg, uint64_t value)
{
    GicModel* model = static_cast<GicModel*>(context);
    if (reg == arm64::gicv3::SystemRegister::Eoir1El1) {
        ++model->eoir_count;
        model->eoir_value = static_cast<uint32_t>(value);
        return;
    }
    if (reg == arm64::gicv3::SystemRegister::DirEl1) {
        ++model->dir_count;
        model->dir_value = static_cast<uint32_t>(value);
        return;
    }
    if (reg == arm64::gicv3::SystemRegister::SreEl1 && model->ignore_sre_write) return;
    if (reg == arm64::gicv3::SystemRegister::CtlrEl1 && model->force_split_eoi) {
        model->system[static_cast<uint32_t>(reg)] = value | (UINT64_C(1) << 1);
        return;
    }
    model->system[static_cast<uint32_t>(reg)] = value;
}

static void model_barrier(void* context, arm64::gicv3::Barrier)
{
    ++static_cast<GicModel*>(context)->barriers;
}

static arm64::gicv3::RegisterIo make_io(GicModel* model)
{
    arm64::gicv3::RegisterIo io = {};
    io.context = model;
    io.read32 = model_read32;
    io.read64 = model_read64;
    io.read8 = model_read8;
    io.write32 = model_write32;
    io.write64 = model_write64;
    io.write8 = model_write8;
    io.sys_read = model_sys_read;
    io.sys_write = model_sys_write;
    io.barrier = model_barrier;
    return io;
}

static gxos_aarch64_phase2_platform make_platform(uint32_t version = GXOS_AARCH64_GIC_V3)
{
    gxos_aarch64_phase2_platform platform = {};
    platform.valid = 1;
    platform.gic_version = static_cast<uint8_t>(version);
    platform.gicd_base = kGicd;
    platform.gicd_size = 0x10000;
    platform.timer_irq = 30;
    platform.gic_redistributor_region_count = 1;
    platform.gic_redistributor_regions[0].base = kGicr;
    platform.gic_redistributor_regions[0].size = 0x40000;
    platform.gic_cpu_affinity_count = 1;
    platform.gic_cpu_affinity[0] = UINT64_C(1) << 32;
    return platform;
}

static bool parse_fixture(const std::vector<uint8_t>& dtb,
                          gxos_aarch64_phase2_platform* platform,
                          gxos_aarch64_dtb_status expected,
                          const char* label)
{
    const gxos_aarch64_dtb_status status = gxos_aarch64_phase2_parse_dtb_ex(
        dtb.data(), dtb.size(), platform);
    return expect(status == expected, label);
}

static uint64_t gTimerAccounting = 0;
static void* timer_irq_handler(uint32_t, void* frame, void*)
{
    ++gTimerAccounting;
    return kernel::scheduler::timer_interrupt(frame);
}

alignas(4096) static uint8_t gSchedulerStackPool[UINT32_C(0x10000)];
static uint64_t gSchedulerStackOffset = 0;
static void scheduler_allocate_pages(uint64_t pages, uint64_t* base)
{
    const uint64_t bytes = pages * UINT64_C(0x1000);
    if (!base || bytes == 0 || bytes > sizeof(gSchedulerStackPool) - gSchedulerStackOffset) {
        if (base) *base = 0;
        return;
    }
    *base = reinterpret_cast<uint64_t>(gSchedulerStackPool + gSchedulerStackOffset);
    gSchedulerStackOffset += bytes;
}

static void scheduler_task(void*) {}

static bool test_dtb_parser()
{
    bool ok = true;
    gxos_aarch64_phase2_platform platform = {};
    const std::vector<uint8_t> v2 = build_dtb(GXOS_AARCH64_GIC_V2);
    ok &= parse_fixture(v2, &platform, GXOS_AARCH64_DTB_OK, "GICv2 DTB detection remains valid");
    ok &= expect(platform.gic_version == GXOS_AARCH64_GIC_V2 && platform.gicc_base != 0,
                 "GICv2 discovery preserves distributor and CPU interface");

    const std::vector<uint8_t> v3 = build_dtb(GXOS_AARCH64_GIC_V3, 0x10000, 0x40000,
                                               0x20000, true, true);
    platform = {};
    ok &= parse_fixture(v3, &platform, GXOS_AARCH64_DTB_OK, "GICv3 DTB detection succeeds");
    ok &= expect(platform.gic_version == GXOS_AARCH64_GIC_V3 &&
                 platform.gic_redistributor_region_count == 1 &&
                 platform.gic_redistributor_stride == 0x20000 &&
                 platform.gic_interrupt_cells == 3,
                 "GICv3 distributor, redistributor, stride, and interrupt cells are retained");
    ok &= expect(platform.timer_irq == 30 && platform.gic_cpu_affinity_count == 1,
                 "timer PPI and boot CPU affinity are discovered from the DTB");
    ok &= expect(platform.uart_base == UINT64_C(0x09001000) && platform.ranges_translated,
                 "translated MMIO address is resolved through parent ranges");
    platform = {};
    const std::vector<uint8_t> multi_region = build_dtb(
        GXOS_AARCH64_GIC_V3, 0x10000, 0x40000, 0, true, false, kGicr, UINT64_C(0x080e0000));
    ok &= parse_fixture(multi_region, &platform, GXOS_AARCH64_DTB_OK,
                        "multiple GICv3 Redistributor regions are parsed");
    ok &= expect(platform.gic_redistributor_region_count == 2 &&
                 platform.gic_redistributor_regions[1].base == UINT64_C(0x080e0000),
                 "second Redistributor region retains its translated base and length");

    platform = {};
    const std::vector<uint8_t> unknown = build_unknown_gic_dtb();
    ok &= parse_fixture(unknown, &platform, GXOS_AARCH64_DTB_UNSUPPORTED_CONTROLLER,
                        "unknown interrupt controller fails safely");

    std::vector<uint8_t> malformed = v3;
    malformed[0] = 0;
    platform = {};
    ok &= parse_fixture(malformed, &platform, GXOS_AARCH64_DTB_MALFORMED,
                        "malformed DTB header is diagnosed");
    platform = {};
    ok &= parse_fixture(std::vector<uint8_t>(v3.begin(), v3.begin() + 32), &platform,
                        GXOS_AARCH64_DTB_MALFORMED, "truncated DTB header is rejected");

    platform = {};
    const std::vector<uint8_t> short_dist = build_dtb(GXOS_AARCH64_GIC_V3, 0x8000);
    ok &= parse_fixture(short_dist, &platform, GXOS_AARCH64_DTB_INVALID_CONTROLLER_LAYOUT,
                        "truncated GIC distributor mapping is rejected");
    platform = {};
    const std::vector<uint8_t> short_rd = build_dtb(GXOS_AARCH64_GIC_V3, 0x10000, 0x10000);
    ok &= parse_fixture(short_rd, &platform, GXOS_AARCH64_DTB_INVALID_CONTROLLER_LAYOUT,
                        "truncated GIC Redistributor region is rejected");
    platform = {};
    const std::vector<uint8_t> bad_stride = build_dtb(GXOS_AARCH64_GIC_V3, 0x10000, 0x40000,
                                                       0x10000);
    ok &= parse_fixture(bad_stride, &platform, GXOS_AARCH64_DTB_INVALID_CONTROLLER_LAYOUT,
                        "invalid Redistributor stride is rejected");
    platform = {};
    const std::vector<uint8_t> unavailable = build_dtb(GXOS_AARCH64_GIC_V3, 0x10000, 0x40000,
                                                        0, false);
    ok &= parse_fixture(unavailable, &platform, GXOS_AARCH64_DTB_DEVICE_UNAVAILABLE,
                        "disabled GIC mapping is rejected");
    platform = {};
    const std::vector<uint8_t> overlap = build_dtb(GXOS_AARCH64_GIC_V3, 0x10000, 0x40000,
                                                    0, true, false, kUart);
    ok &= parse_fixture(overlap, &platform, GXOS_AARCH64_DTB_INVALID_CONTROLLER_LAYOUT,
                        "overlapping GIC and UART MMIO mappings are rejected");
    platform = {};
    const std::vector<uint8_t> overflow = build_dtb(GXOS_AARCH64_GIC_V3, 0x10000, 0x40000,
                                                     0, true, false,
                                                     UINT64_C(0xffffffffffff0000));
    ok &= expect(gxos_aarch64_phase2_parse_dtb_ex(overflow.data(), overflow.size(), &platform) !=
                     GXOS_AARCH64_DTB_OK,
                 "overflowing translated MMIO region is rejected");
    return ok;
}

static bool initialize_model(arm64::gicv3::State* state, GicModel* model,
                             gxos_aarch64_phase2_platform* platform,
                             arm64::gicv3::Error expected,
                             const char* label)
{
    const arm64::gicv3::RegisterIo io = make_io(model);
    const arm64::gicv3::Error actual = arm64::gicv3::initialize(state, platform, &io, 4);
    return expect(actual == expected, label);
}

static bool test_register_driver_and_irq_flow()
{
    bool ok = true;
    GicModel model;
    gxos_aarch64_phase2_platform platform = make_platform();
    // The first frame belongs to another affinity. The boot CPU is in frame 1.
    const uint64_t second = kGicr + UINT64_C(0x20000);
    model.registers[kGicr + 0x0008] = (UINT64_C(1) << 32);
    model.registers[second + 0x0008] = (UINT64_C(1) << 56) | (UINT64_C(1) << 4);
    model.registers[second + 0xffe8] = UINT32_C(3) << 4;
    model.registers[second + 0x0014] = 6;
    arm64::gicv3::State state = {};
    ok &= initialize_model(&state, &model, &platform, arm64::gicv3::Error::Ok,
                           "driver matches boot CPU affinity across Redistributor frames");
    ok &= expect(state.redistributor_base == second && state.interrupt_count == 256,
                 "matched Redistributor and distributor capabilities are recorded");
    ok &= expect(model.system[static_cast<uint32_t>(arm64::gicv3::SystemRegister::SreEl1)] & 1,
                 "ICC_SRE_EL1 is enabled with read-back confirmation");
    ok &= expect(model.system[static_cast<uint32_t>(arm64::gicv3::SystemRegister::IgrpEn1El1)] == 1,
                 "non-secure Group 1 CPU interface is enabled");
    ok &= expect((model.registers[second + 0x10000 + 0x100 + 30 / 32 * 4] &
                  (UINT64_C(1) << 30)) != 0,
                 "architectural timer PPI is enabled in the local Redistributor");
    ok &= expect(model.registers[kGicd + 0x80] == 0,
                 "initialization leaves unrelated distributor interrupt groups untouched");
    ok &= expect(initialize_model(&state, &model, &platform,
                                  arm64::gicv3::Error::AlreadyInitialized,
                                  "duplicate initialization is rejected"),
                 "duplicate initialization result is explicit");

    ok &= expect(arm64::gicv3::enable(&state, 35) == arm64::gicv3::Error::Ok,
                 "SPI enable succeeds");
    ok &= expect(model.registers[kGicd + 0x100 + 4] == (UINT64_C(1) << 3),
                 "SPI enable writes the correct distributor bit");
    ok &= expect(model.registers[kGicd + 0x80 + 4] == (UINT64_C(1) << 3),
                 "SPI is routed to non-secure Group 1");
    ok &= expect(model.registers[kGicd + 0x6000 + (35 - 32) * 8] == (UINT64_C(1) << 32),
                 "SPI IROUTER encodes the boot CPU Aff3 in its architectural bit field");
    ok &= expect(arm64::gicv3::set_trigger(&state, 35, arm64::gicv3::Trigger::Edge) ==
                     arm64::gicv3::Error::Ok,
                 "SPI trigger configuration succeeds");
    ok &= expect((model.registers[kGicd + 0xc00 + (35 / 16) * 4] &
                  (UINT64_C(2) << ((35 % 16) * 2))) != 0,
                 "SPI trigger configuration selects edge mode");
    ok &= expect(arm64::gicv3::disable(&state, 35) == arm64::gicv3::Error::Ok,
                 "SPI disable succeeds");
    ok &= expect(model.registers[kGicd + 0x180 + 4] == (UINT64_C(1) << 3),
                 "SPI disable writes the correct distributor bit");
    ok &= expect(arm64::gicv3::enable(&state, 30) == arm64::gicv3::Error::Ok,
                 "PPI enable succeeds through the Redistributor");
    ok &= expect(model.registers[second + 0x10000 + 0x100] == (UINT64_C(1) << 30),
                 "PPI routes to the boot CPU local enable register");
    ok &= expect(arm64::gicv3::enable(&state, 3) == arm64::gicv3::Error::InvalidInterrupt,
                 "SGI IDs are rejected from the enable API");
    ok &= expect(arm64::gicv3::enable(&state, 300) == arm64::gicv3::Error::InvalidInterrupt,
                 "unsupported INTID is rejected");
    ok &= expect(arm64::gicv3::set_priority(&state, 30, 0x40) == arm64::gicv3::Error::Ok &&
                 model.registers[second + 0x10000 + 0x400 + 30] == 0x40,
                 "PPI priority is configured in the Redistributor");
    ok &= expect(arm64::gicv3::set_trigger(&state, 30, arm64::gicv3::Trigger::Edge) ==
                     arm64::gicv3::Error::Ok,
                 "PPI trigger configuration succeeds");
    ok &= expect(arm64::gicv3::is_pending(&state, 30, nullptr) ==
                     arm64::gicv3::Error::InvalidArgument,
                 "null pending output is rejected");
    model.registers[second + 0x10000 + 0x200] = UINT64_C(1) << 30;
    bool pending = false;
    ok &= expect(arm64::gicv3::is_pending(&state, 30, &pending) == arm64::gicv3::Error::Ok && pending,
                 "PPI pending state is read from the Redistributor");
    ok &= expect(arm64::gicv3::clear_pending(&state, 30) == arm64::gicv3::Error::Ok &&
                 model.registers[second + 0x10000 + 0x280] == (UINT64_C(1) << 30),
                 "PPI pending state is cleared with the write-one register");

    model.next_ack = 30;
    const uint32_t timer_ack = arm64::gicv3::acknowledge(&state);
    ok &= expect(arm64::gicv3::valid_acknowledgement(&state, timer_ack),
                 "ordinary PPI acknowledgement is valid");
    gSchedulerStackOffset = 0;
    kernel::arch::gHostedContextCount = 0;
    const kernel::scheduler::Config schedulerConfig = {
        scheduler_allocate_pages, 2, 1, 1
    };
    bool scheduler_ready = kernel::scheduler::initialize(schedulerConfig);
    kernel::scheduler::Task* worker = scheduler_ready
        ? kernel::scheduler::create_task(1, "timer-worker", scheduler_task, nullptr, true) : nullptr;
    kernel::scheduler::Task* completion = worker
        ? kernel::scheduler::create_task(2, "completion", scheduler_task, nullptr, true) : nullptr;
    if (completion) kernel::scheduler::set_completion_task(completion);
    kernel::scheduler::set_phase(kernel::scheduler::PHASE_PREEMPTIVE);
    scheduler_ready = scheduler_ready && worker && completion &&
                      kernel::scheduler::prepare_interrupt_contexts();
    if (scheduler_ready) kernel::scheduler::start();
    ok &= expect(scheduler_ready, "common scheduler accepts hosted interrupt contexts");
    ok &= expect(kernel::irq::initialize() &&
                 kernel::irq::register_handler(30, timer_irq_handler, nullptr),
                 "common IRQ registry accepts the timer handler");
    gTimerAccounting = 0;
    void* notification = kernel::irq::dispatch(30, reinterpret_cast<void*>(0x1234));
    ok &= expect(notification == completion->interrupt_context && gTimerAccounting == 1 &&
                 kernel::scheduler::preemptions() == 1 &&
                 kernel::scheduler::preemptive_complete() &&
                 kernel::irq::dispatch_count(30) == 1,
                 "modeled GICv3 timer reaches the common registry, timer accounting, and scheduler preemption");
    ok &= expect(arm64::gicv3::complete(&state, timer_ack) == arm64::gicv3::Error::Ok &&
                 model.eoir_count == 1 && model.eoir_value == timer_ack && model.dir_count == 0,
                 "combined EOIR1 deactivates a valid interrupt without unnecessary DIR");

    model.next_ack = kSpurious;
    const uint32_t spurious = arm64::gicv3::acknowledge(&state);
    const uint32_t eoir_before_spurious = model.eoir_count;
    ok &= expect(!arm64::gicv3::valid_acknowledgement(&state, spurious) &&
                 arm64::gicv3::complete(&state, spurious) == arm64::gicv3::Error::InvalidAcknowledgement &&
                 model.eoir_count == eoir_before_spurious,
                 "spurious interrupt is ignored without EOIR or DIR");
    model.next_ack = 35;
    const uint32_t unknown_ack = arm64::gicv3::acknowledge(&state);
    ok &= expect(arm64::gicv3::valid_acknowledgement(&state, unknown_ack) &&
                 !kernel::irq::has_handler(35), "unknown but valid SPI is distinguishable from spurious");
    const uint32_t eoir_before_unknown = model.eoir_count;
    ok &= expect(arm64::gicv3::complete(&state, unknown_ack) == arm64::gicv3::Error::Ok &&
                 model.eoir_count == eoir_before_unknown + 1,
                 "unknown but valid SPI is completed to prevent an interrupt storm");
    const uint32_t eoir_before_bad = model.eoir_count;
    ok &= expect(arm64::gicv3::complete(&state, 32) == arm64::gicv3::Error::InvalidAcknowledgement &&
                 model.eoir_count == eoir_before_bad,
                 "unacknowledged INTID is never sent to EOIR");
    ok &= expect(model.barriers > 0, "MMIO and CPU interface operations issue synchronization barriers");
    return ok;
}

static bool test_negative_register_controls()
{
    bool ok = true;
    gxos_aarch64_phase2_platform platform = make_platform();
    {
        GicModel model;
        model.force_wake_timeout = true;
        arm64::gicv3::State state = {};
        ok &= initialize_model(&state, &model, &platform,
                               arm64::gicv3::Error::RedistributorWakeTimeout,
                               "Redistributor wake polling is bounded");
    }
    {
        GicModel model;
        model.force_distributor_rwp = true;
        arm64::gicv3::State state = {};
        ok &= initialize_model(&state, &model, &platform,
                               arm64::gicv3::Error::DistributorWriteTimeout,
                               "Distributor write-pending polling is bounded");
    }
    {
        GicModel model;
        model.force_redistributor_rwp = true;
        arm64::gicv3::State state = {};
        ok &= initialize_model(&state, &model, &platform,
                               arm64::gicv3::Error::RedistributorWriteTimeout,
                               "Redistributor write-pending polling is bounded");
    }
    {
        GicModel model;
        model.registers[kGicr + 0x0008] = (UINT64_C(1) << 32) | (UINT64_C(1) << 4);
        arm64::gicv3::State state = {};
        ok &= initialize_model(&state, &model, &platform,
                               arm64::gicv3::Error::RedistributorNotFound,
                               "unmatched CPU affinity fails safely");
    }
    {
        GicModel model;
        model.system[static_cast<uint32_t>(arm64::gicv3::SystemRegister::IdAa64Pfr0El1)] = 0;
        arm64::gicv3::State state = {};
        ok &= initialize_model(&state, &model, &platform,
                               arm64::gicv3::Error::UnsupportedCapability,
                               "missing GIC system-register capability is rejected");
    }
    {
        GicModel model;
        model.ignore_sre_write = true;
        arm64::gicv3::State state = {};
        ok &= initialize_model(&state, &model, &platform,
                               arm64::gicv3::Error::UnsupportedCapability,
                               "read-only or blocked ICC_SRE_EL1 is rejected");
    }
    {
        GicModel model;
        model.force_split_eoi = true;
        arm64::gicv3::State state = {};
        ok &= initialize_model(&state, &model, &platform,
                               arm64::gicv3::Error::UnsupportedCapability,
                               "unsupported split EOI mode is rejected at initialization");
    }
    {
        GicModel model;
        model.registers[kGicd + 0xffe8] = 2u << 4;
        arm64::gicv3::State state = {};
        ok &= initialize_model(&state, &model, &platform,
                               arm64::gicv3::Error::UnsupportedCapability,
                               "non-GICv3 distributor revision is rejected");
    }
    {
        GicModel model;
        arm64::gicv3::State state = {};
        platform.gic_version = GXOS_AARCH64_GIC_V2;
        ok &= initialize_model(&state, &model, &platform,
                               arm64::gicv3::Error::InvalidLayout,
                               "GICv2 cannot be passed to the GICv3 driver");
        platform.gic_version = GXOS_AARCH64_GIC_V3;
    }
    {
        GicModel model;
        arm64::gicv3::State state = {};
        platform.gic_cpu_affinity[0] = UINT64_C(1) << 8;
        ok &= initialize_model(&state, &model, &platform,
                               arm64::gicv3::Error::RedistributorNotFound,
                               "DTB CPU map must contain the boot CPU affinity");
        platform.gic_cpu_affinity[0] = 0;
    }
    return ok;
}

} // namespace

int main()
{
    bool ok = true;
    ok &= test_dtb_parser();
    ok &= test_register_driver_and_irq_flow();
    ok &= test_negative_register_controls();
    if (!ok || gFailures != 0) {
        fprintf(stderr, "AARCH64 GICv3 hosted tests: FAIL (%u/%u assertions failed)\n",
                gFailures, gAssertions);
        return 1;
    }
    printf("AARCH64 GICv3 hosted tests: PASS (%u assertions)\n", gAssertions);
    return 0;
}
