#include <stdint.h>
#include <stdio.h>

#include <string>
#include <vector>

#include "../aarch64/phase2/phase2_platform.h"
#include "../aarch64/rpi4/rpi4_p1_validation.h"

static void put_be32(std::vector<uint8_t>& bytes, uint32_t value)
{
    bytes.push_back(static_cast<uint8_t>(value >> 24));
    bytes.push_back(static_cast<uint8_t>(value >> 16));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
    bytes.push_back(static_cast<uint8_t>(value));
}

static void write_be32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
{
    bytes[offset + 0] = static_cast<uint8_t>(value >> 24);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 16);
    bytes[offset + 2] = static_cast<uint8_t>(value >> 8);
    bytes[offset + 3] = static_cast<uint8_t>(value);
}

static void align4(std::vector<uint8_t>& bytes)
{
    while ((bytes.size() & 3u) != 0) bytes.push_back(0);
}

struct FdtBuilder {
    std::vector<uint8_t> structure;
    std::vector<uint8_t> strings;

    uint32_t string_offset(const char* name)
    {
        const size_t length = std::string(name).size() + 1;
        for (size_t offset = 0; offset + length <= strings.size(); ++offset) {
            if (std::string(reinterpret_cast<const char*>(&strings[offset])) == name) {
                return static_cast<uint32_t>(offset);
            }
        }
        const uint32_t result = static_cast<uint32_t>(strings.size());
        while (*name != 0) strings.push_back(static_cast<uint8_t>(*name++));
        strings.push_back(0);
        return result;
    }

    void begin(const char* name)
    {
        put_be32(structure, 1);
        while (*name != 0) structure.push_back(static_cast<uint8_t>(*name++));
        structure.push_back(0);
        align4(structure);
    }

    void end()
    {
        put_be32(structure, 2);
    }

    void prop(const char* name, const std::vector<uint8_t>& value)
    {
        put_be32(structure, 3);
        put_be32(structure, static_cast<uint32_t>(value.size()));
        put_be32(structure, string_offset(name));
        structure.insert(structure.end(), value.begin(), value.end());
        align4(structure);
    }

    void cells(const char* name, std::initializer_list<uint32_t> values)
    {
        std::vector<uint8_t> value;
        for (uint32_t cell : values) put_be32(value, cell);
        prop(name, value);
    }

    void string_prop(const char* name, const char* value)
    {
        std::vector<uint8_t> bytes;
        while (*value != 0) bytes.push_back(static_cast<uint8_t>(*value++));
        bytes.push_back(0);
        prop(name, bytes);
    }

    void compatible(std::initializer_list<const char*> values)
    {
        std::vector<uint8_t> bytes;
        for (const char* value : values) {
            while (*value != 0) bytes.push_back(static_cast<uint8_t>(*value++));
            bytes.push_back(0);
        }
        prop("compatible", bytes);
    }

    std::vector<uint8_t> finish()
    {
        put_be32(structure, 9);
        const uint32_t reserve_offset = 0x80;
        const uint32_t structure_offset = 0x100;
        const uint32_t strings_offset = (structure_offset + static_cast<uint32_t>(structure.size()) + 0xffu) & ~0xffu;
        const uint32_t total = strings_offset + static_cast<uint32_t>(strings.size());
        std::vector<uint8_t> result(total, 0);
        write_be32(result, 0, 0xd00dfeed);
        write_be32(result, 4, total);
        write_be32(result, 8, structure_offset);
        write_be32(result, 12, strings_offset);
        write_be32(result, 16, reserve_offset);
        write_be32(result, 20, 17);
        write_be32(result, 24, 16);
        write_be32(result, 28, 0);
        write_be32(result, 32, static_cast<uint32_t>(strings.size()));
        write_be32(result, 36, static_cast<uint32_t>(structure.size()));
        for (size_t i = 0; i < structure.size(); ++i) result[structure_offset + i] = structure[i];
        for (size_t i = 0; i < strings.size(); ++i) result[strings_offset + i] = strings[i];
        return result;
    }
};

static std::vector<uint8_t> make_dtb(bool raspberry_pi, bool malformed_ranges = false)
{
    FdtBuilder fdt;
    fdt.begin("");
    if (raspberry_pi) {
        fdt.compatible({"raspberrypi,4-model-b", "brcm,bcm2711"});
        fdt.cells("#address-cells", {2});
        fdt.cells("#size-cells", {2});
    } else {
        fdt.compatible({"linux,dummy-virt"});
        fdt.cells("#address-cells", {2});
        fdt.cells("#size-cells", {1});
    }

    fdt.begin("memory@0");
    fdt.string_prop("device_type", "memory");
    if (raspberry_pi) fdt.cells("reg", {0, 0, 0, 0x40000000});
    else fdt.cells("reg", {0, 0x40000000, 0x20000000});
    fdt.end();

    if (raspberry_pi) {
        fdt.begin("soc");
        fdt.cells("#address-cells", {1});
        fdt.cells("#size-cells", {1});
        fdt.cells("ranges", {0x7e000000, 0, 0xfe000000,
                             static_cast<uint32_t>(malformed_ranges ? 0x1000u : 0x02000000u)});
        fdt.begin("serial@7e201000");
        fdt.compatible({"brcm,bcm2835-pl011", "arm,pl011"});
        fdt.cells("reg", {0x7e201000, 0x200});
        fdt.end();
        fdt.end();
    } else {
        fdt.begin("pl011@9000000");
        fdt.compatible({"arm,pl011"});
        fdt.cells("reg", {0, 0x09000000, 0x1000});
        fdt.end();
    }

    fdt.begin("interrupt-controller@0");
    fdt.compatible({"arm,gic-400"});
    if (raspberry_pi) {
        fdt.cells("reg", {0, 0xff841000, 0, 0x1000,
                          0, 0xff842000, 0, 0x2000});
    } else {
        fdt.cells("reg", {0, 0x08000000, 0x10000,
                          0, 0x08010000, 0x10000});
    }
    fdt.end();

    fdt.begin("timer");
    fdt.compatible({raspberry_pi ? "arm,armv7-timer" : "arm,armv8-timer"});
    fdt.cells("interrupts", {1, 13, 0, 1, 14, 0, 1, 11, 0, 1, 10, 0});
    fdt.end();
    fdt.end();
    return fdt.finish();
}

static bool expect(bool condition, const char* name)
{
    if (!condition) fprintf(stderr, "FAIL: %s\n", name);
    return condition;
}

int main()
{
    bool ok = true;

    std::vector<uint8_t> pi_blob = make_dtb(true);
    gxos_aarch64_phase2_platform pi = {};
    const bool pi_parsed = gxos_aarch64_phase2_parse_dtb(pi_blob.data(), pi_blob.size(), &pi);
    ok &= expect(pi_parsed,
                 "BCM2711 DTB accepted");
    ok &= expect(pi.platform_kind == GXOS_AARCH64_PLATFORM_RASPBERRY_PI4,
                 "Raspberry Pi 4 platform identity detected");
    ok &= expect(pi.ram_count == 1 && pi.ram[0].base == 0 &&
                 pi.ram[0].size == UINT64_C(0x40000000),
                 "Pi RAM discovered from DTB");
    ok &= expect(pi.uart_kind == GXOS_AARCH64_UART_PL011 &&
                 pi.uart_base == UINT64_C(0xfe201000) && pi.uart_size == 0x200 &&
                 pi.ranges_translated != 0,
                 "Pi PL011 bus range translated to physical address");
    ok &= expect(pi.gic_version == 2 && pi.gicd_base == UINT64_C(0xff841000) &&
                 pi.gicc_base == UINT64_C(0xff842000),
                 "Pi GIC-400 distributor and CPU interface discovered");
    ok &= expect(pi.timer_source == 2 && pi.timer_irq == 30,
                 "Pi architectural timer PPI discovered");

    std::vector<uint8_t> qemu_blob = make_dtb(false);
    gxos_aarch64_phase2_platform qemu = {};
    ok &= expect(gxos_aarch64_phase2_parse_dtb(qemu_blob.data(), qemu_blob.size(), &qemu),
                 "QEMU virt DTB accepted");
    ok &= expect(qemu.platform_kind == GXOS_AARCH64_PLATFORM_QEMU_VIRT &&
                 qemu.uart_base == UINT64_C(0x09000000),
                 "QEMU virt platform identity and UART preserved");

    FdtBuilder unknown_builder;
    unknown_builder.begin("");
    unknown_builder.compatible({"vendor,unknown-board"});
    unknown_builder.cells("#address-cells", {2});
    unknown_builder.cells("#size-cells", {1});
    unknown_builder.end();
    std::vector<uint8_t> unknown_blob = unknown_builder.finish();
    gxos_aarch64_phase2_platform unknown = {};
    ok &= expect(!gxos_aarch64_phase2_parse_dtb(unknown_blob.data(), unknown_blob.size(), &unknown),
                 "unknown platform rejected");

    std::vector<uint8_t> bad_header = qemu_blob;
    bad_header[0] = 0;
    gxos_aarch64_phase2_platform bad_header_platform = {};
    ok &= expect(!gxos_aarch64_phase2_parse_dtb(bad_header.data(), bad_header.size(),
                                                 &bad_header_platform),
                 "malformed FDT header rejected");

    std::vector<uint8_t> truncated = qemu_blob;
    truncated.resize(truncated.size() - 1);
    gxos_aarch64_phase2_platform truncated_platform = {};
    ok &= expect(!gxos_aarch64_phase2_parse_dtb(truncated.data(), truncated.size(),
                                                 &truncated_platform),
                 "truncated FDT rejected");

    std::vector<uint8_t> malformed_blob = make_dtb(true, true);
    gxos_aarch64_phase2_platform malformed = {};
    ok &= expect(!gxos_aarch64_phase2_parse_dtb(malformed_blob.data(), malformed_blob.size(), &malformed),
                 "malformed Pi ranges node rejected");

    ok &= expect(gxos_aarch64_rpi4_p1_range_contains(0x40000000, 0x100000,
                                                      0x40001000, 0x2000),
                 "available fixed kernel range accepted");
    ok &= expect(!gxos_aarch64_rpi4_p1_range_contains(0x40000000, 0x1000,
                                                       0x40000000, 0x2000),
                 "unavailable fixed kernel range rejected");
    ok &= expect(!gxos_aarch64_rpi4_p1_range_contains(UINT64_MAX - 0xfff, 0x2000,
                                                       UINT64_MAX - 0xfff, 0x1000),
                 "fixed kernel range overflow rejected");

    ok &= expect(gxos_aarch64_rpi4_p1_framebuffer_geometry_valid(
                     0x10000000, 0x400000, 800, 600, 3200, 32, 2),
                 "valid framebuffer geometry accepted");
    ok &= expect(!gxos_aarch64_rpi4_p1_framebuffer_geometry_valid(
                     UINT64_MAX - 0xfff, 0x2000, 800, 600, 3200, 32, 2),
                 "malformed framebuffer address range rejected");
    ok &= expect(!gxos_aarch64_rpi4_p1_framebuffer_geometry_valid(
                     0x10000000, 0x1000, 800, 600, 3200, 32, 2),
                 "undersized framebuffer rejected");

    if (!ok) return 1;
    puts("AARCH64 Raspberry Pi 4 P1 host controls: PASS");
    return 0;
}
