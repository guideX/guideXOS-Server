#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <map>
#include <string>
#include <vector>

#include "../aarch64/phase2/phase2_platform.h"
#include "../aarch64/phase2/phase2_validation.h"

static uint32_t gAssertions = 0;

static void be32(uint8_t* p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static void le32(uint8_t* p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void le64(uint8_t* p, uint64_t value)
{
    le32(p, (uint32_t)value);
    le32(p + 4, (uint32_t)(value >> 32));
}

static bool expect(bool condition, const char* name)
{
    ++gAssertions;
    if (!condition) fprintf(stderr, "FAIL: %s\n", name);
    return condition;
}

class FdtBuilder {
public:
    void begin(const char* name)
    {
        append_be32(structure_, 1);
        for (const char* p = name; *p; ++p) structure_.push_back((uint8_t)*p);
        structure_.push_back(0);
        align_structure();
    }

    void end_node() { append_be32(structure_, 2); }

    void property(const char* name, const std::vector<uint8_t>& value)
    {
        append_be32(structure_, 3);
        append_be32(structure_, (uint32_t)value.size());
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
        for (const char* p = text; *p; ++p) value.push_back((uint8_t)*p);
        value.push_back(0);
        property(name, value);
    }

    void property_stringlist(const char* name, const char* first, const char* second)
    {
        std::vector<uint8_t> value;
        for (const char* p = first; *p; ++p) value.push_back((uint8_t)*p);
        value.push_back(0);
        for (const char* p = second; *p; ++p) value.push_back((uint8_t)*p);
        value.push_back(0);
        property(name, value);
    }

    std::vector<uint8_t> finish()
    {
        append_be32(structure_, 9);
        std::vector<uint8_t> result(56, 0); // 40-byte header and empty reservation terminator.
        result.insert(result.end(), structure_.begin(), structure_.end());
        while ((result.size() & 3u) != 0) result.push_back(0);
        const uint32_t strings_offset = (uint32_t)result.size();
        result.insert(result.end(), strings_.begin(), strings_.end());
        be32(result.data() + 0, UINT32_C(0xd00dfeed));
        be32(result.data() + 4, (uint32_t)result.size());
        be32(result.data() + 8, 56);
        be32(result.data() + 12, strings_offset);
        be32(result.data() + 16, 40);
        be32(result.data() + 20, 17);
        be32(result.data() + 24, 16);
        be32(result.data() + 28, 0);
        be32(result.data() + 32, (uint32_t)strings_.size());
        be32(result.data() + 36, (uint32_t)structure_.size());
        return result;
    }

private:
    static void append_be32(std::vector<uint8_t>& target, uint32_t value)
    {
        target.push_back((uint8_t)(value >> 24));
        target.push_back((uint8_t)(value >> 16));
        target.push_back((uint8_t)(value >> 8));
        target.push_back((uint8_t)value);
    }

    void align_structure()
    {
        while ((structure_.size() & 3u) != 0) structure_.push_back(0);
    }

    uint32_t name_offset(const char* name)
    {
        const std::string key(name);
        const std::map<std::string, uint32_t>::const_iterator found = name_offsets_.find(key);
        if (found != name_offsets_.end()) return found->second;
        const uint32_t offset = (uint32_t)strings_.size();
        for (const char* p = name; *p; ++p) strings_.push_back((uint8_t)*p);
        strings_.push_back(0);
        name_offsets_[key] = offset;
        return offset;
    }

    std::vector<uint8_t> structure_;
    std::vector<uint8_t> strings_;
    std::map<std::string, uint32_t> name_offsets_;
};

static void append_u64(std::vector<uint8_t>& target, uint64_t value)
{
    target.push_back((uint8_t)(value >> 56));
    target.push_back((uint8_t)(value >> 48));
    target.push_back((uint8_t)(value >> 40));
    target.push_back((uint8_t)(value >> 32));
    target.push_back((uint8_t)(value >> 24));
    target.push_back((uint8_t)(value >> 16));
    target.push_back((uint8_t)(value >> 8));
    target.push_back((uint8_t)value);
}

static std::vector<uint8_t> reg_tuple(uint64_t base, uint64_t size)
{
    std::vector<uint8_t> result;
    append_u64(result, base);
    append_u64(result, size);
    return result;
}

static void append_bytes(std::vector<uint8_t>& target, const std::vector<uint8_t>& source)
{
    target.insert(target.end(), source.begin(), source.end());
}

static std::vector<uint8_t> build_qemu_gicv2_dtb()
{
    FdtBuilder fdt;
    fdt.begin("");
    fdt.property_stringlist("compatible", "linux,dummy-virt", "qemu,virt");
    fdt.property_cells("#address-cells", {2});
    fdt.property_cells("#size-cells", {2});

    fdt.begin("memory@40000000");
    fdt.property_text("device_type", "memory");
    fdt.property("reg", reg_tuple(UINT64_C(0x40000000), UINT64_C(0x20000000)));
    fdt.end_node();

    fdt.begin("serial@9000000");
    fdt.property_text("compatible", "arm,pl011");
    fdt.property("reg", reg_tuple(UINT64_C(0x09000000), UINT64_C(0x1000)));
    fdt.end_node();

    fdt.begin("interrupt-controller@8000000");
    fdt.property_text("compatible", "arm,gic-v2");
    fdt.property("interrupt-controller", {});
    fdt.property_cells("#interrupt-cells", {3});
    std::vector<uint8_t> gic_regs = reg_tuple(UINT64_C(0x08000000), UINT64_C(0x1000));
    append_bytes(gic_regs, reg_tuple(UINT64_C(0x08010000), UINT64_C(0x2000)));
    fdt.property("reg", gic_regs);
    fdt.end_node();

    fdt.begin("timer");
    fdt.property_text("compatible", "arm,armv8-timer");
    fdt.property_cells("interrupts", {1, 13, 0x104, 1, 14, 0x104,
                                        1, 11, 0x104, 1, 10, 0x104});
    fdt.end_node();
    fdt.end_node();
    return fdt.finish();
}

static bool parse_header_failure(std::vector<uint8_t> blob,
                                 gxos_aarch64_fdt_header_status expected,
                                 const char* label)
{
    gxos_aarch64_phase2_platform platform = {};
    const gxos_aarch64_dtb_status parsed = gxos_aarch64_phase2_parse_dtb_ex(
        blob.data(), blob.size(), &platform);
    bool ok = true;
    ok &= expect(parsed == GXOS_AARCH64_DTB_MALFORMED, label);
    ok &= expect(platform.parser_stage == GXOS_AARCH64_DTB_PARSE_STAGE_HEADER,
                 "malformed header is rejected before structure parsing");
    ok &= expect(platform.header_status == expected,
                 "header failure stage matches the production validator");
    return ok;
}

static bool test_header_validation(const std::vector<uint8_t>& valid)
{
    bool ok = true;
    gxos_aarch64_fdt_header_info info = {};
    ok &= expect(gxos_aarch64_fdt_header_validate(valid.data(), valid.size(), &info) ==
                     GXOS_AARCH64_FDT_HEADER_OK,
                 "valid FDT header accepted");
    ok &= expect(info.magic == UINT32_C(0xd00dfeed) && info.total_size == valid.size() &&
                 info.version == 17 && info.reservation_offset == 40,
                 "validated header fields are retained for diagnostics");

    std::vector<uint8_t> malformed = valid;
    malformed[0] = 0;
    ok &= parse_header_failure(malformed, GXOS_AARCH64_FDT_HEADER_BAD_MAGIC,
                               "invalid FDT magic is rejected");

    malformed.assign(valid.begin(), valid.begin() + 32);
    ok &= parse_header_failure(malformed, GXOS_AARCH64_FDT_HEADER_TRUNCATED,
                               "truncated FDT header is rejected");

    malformed = valid;
    be32(malformed.data() + 4, (uint32_t)valid.size() + 1);
    ok &= parse_header_failure(malformed, GXOS_AARCH64_FDT_HEADER_BAD_TOTAL_SIZE,
                               "incorrect FDT totalsize is rejected");

    malformed = valid;
    be32(malformed.data() + 8, UINT32_C(0xfffffff0));
    be32(malformed.data() + 36, UINT32_C(0x80));
    ok &= parse_header_failure(malformed, GXOS_AARCH64_FDT_HEADER_BAD_BLOCK_BOUNDS,
                               "overflowing structure offset is rejected");

    malformed = valid;
    be32(malformed.data() + 8, (uint32_t)valid.size() - 4);
    be32(malformed.data() + 36, 16);
    ok &= parse_header_failure(malformed, GXOS_AARCH64_FDT_HEADER_BAD_BLOCK_BOUNDS,
                               "out-of-range structure block is rejected");

    malformed = valid;
    be32(malformed.data() + 12, (uint32_t)valid.size() - 4);
    be32(malformed.data() + 32, 16);
    ok &= parse_header_failure(malformed, GXOS_AARCH64_FDT_HEADER_BAD_BLOCK_BOUNDS,
                               "out-of-range strings block is rejected");

    malformed = valid;
    be32(malformed.data() + 12, 56);
    ok &= parse_header_failure(malformed, GXOS_AARCH64_FDT_HEADER_OVERLAPPING_BLOCKS,
                               "overlapping structure and strings ranges are rejected");

    malformed = valid;
    be32(malformed.data() + 20, 20);
    ok &= parse_header_failure(malformed, GXOS_AARCH64_FDT_HEADER_UNSUPPORTED_VERSION,
                               "unsupported FDT version is rejected");

    return ok;
}

static uint32_t find_property_length_offset(const std::vector<uint8_t>& blob,
                                            const char* property_name)
{
    if (blob.size() < 40 || !property_name) return UINT32_MAX;
    const uint32_t structure_offset = gxos_aarch64_be32(blob.data() + 8);
    const uint32_t structure_size = gxos_aarch64_be32(blob.data() + 36);
    const uint32_t strings_offset = gxos_aarch64_be32(blob.data() + 12);
    const uint32_t strings_size = gxos_aarch64_be32(blob.data() + 32);
    if ((uint64_t)structure_offset + structure_size > blob.size() ||
        (uint64_t)strings_offset + strings_size > blob.size()) return UINT32_MAX;

    uint32_t name_offset = UINT32_MAX;
    const uint32_t wanted_size = (uint32_t)strlen(property_name);
    for (uint32_t cursor = 0; cursor < strings_size;) {
        const uint32_t start = cursor;
        while (cursor < strings_size && blob[strings_offset + cursor] != 0) ++cursor;
        if (cursor == strings_size) return UINT32_MAX;
        if (cursor - start == wanted_size &&
            memcmp(blob.data() + strings_offset + start, property_name, wanted_size) == 0) {
            name_offset = start;
            break;
        }
        ++cursor;
    }
    if (name_offset == UINT32_MAX) return UINT32_MAX;

    uint32_t cursor = 0;
    while (structure_size - cursor >= 4) {
        const uint32_t token = gxos_aarch64_be32(blob.data() + structure_offset + cursor);
        cursor += 4;
        if (token == 1) {
            while (cursor < structure_size && blob[structure_offset + cursor] != 0) ++cursor;
            if (cursor == structure_size) return UINT32_MAX;
            cursor = (cursor + 4) & ~UINT32_C(3);
        } else if (token == 2 || token == 4) {
            continue;
        } else if (token == 3) {
            if (structure_size - cursor < 8) return UINT32_MAX;
            const uint32_t length = gxos_aarch64_be32(blob.data() + structure_offset + cursor);
            const uint32_t property_name_offset =
                gxos_aarch64_be32(blob.data() + structure_offset + cursor + 4);
            if (property_name_offset == name_offset) return cursor;
            cursor += 8;
            if (length > structure_size - cursor) return UINT32_MAX;
            cursor = (cursor + length + 3) & ~UINT32_C(3);
        } else {
            return UINT32_MAX;
        }
        if (cursor > structure_size) return UINT32_MAX;
    }
    return UINT32_MAX;
}

static bool test_structure_trace(const std::vector<uint8_t>& valid)
{
    std::vector<uint8_t> malformed = valid;
    be32(malformed.data() + 56, UINT32_C(0x00000007));
    gxos_aarch64_phase2_platform platform = {};
    gxos_aarch64_dtb_status status = gxos_aarch64_phase2_parse_dtb_ex(
        malformed.data(), malformed.size(), &platform);
    bool ok = true;
    ok &= expect(status == GXOS_AARCH64_DTB_MALFORMED,
                 "unknown structure token remains a malformed DTB");
    ok &= expect(platform.parser_stage == GXOS_AARCH64_DTB_PARSE_STAGE_STRUCTURE &&
                 platform.parser_error == GXOS_AARCH64_DTB_PARSE_ERROR_UNKNOWN_TOKEN &&
                 platform.parser_last_token == 7 && platform.parser_offset == 4,
                 "parser records the exact rejected structure token and offset");

    malformed = valid;
    const uint32_t strings_size = gxos_aarch64_be32(malformed.data() + 32);
    be32(malformed.data() + 56 + 16, strings_size + 1); // First property nameoff.
    status = gxos_aarch64_phase2_parse_dtb_ex(malformed.data(), malformed.size(), &platform);
    ok &= expect(status == GXOS_AARCH64_DTB_MALFORMED &&
                 platform.parser_stage == GXOS_AARCH64_DTB_PARSE_STAGE_STRUCTURE &&
                 platform.parser_error == GXOS_AARCH64_DTB_PARSE_ERROR_PROPERTY_NAME,
                 "unterminated out-of-range property name is rejected precisely");

    malformed = valid;
    const uint32_t property_length_offset = find_property_length_offset(malformed, "#address-cells");
    ok &= expect(property_length_offset != UINT32_MAX,
                 "synthetic fixture locates a property header through its FDT strings table");
    if (property_length_offset != UINT32_MAX) {
        const uint32_t structure_offset = gxos_aarch64_be32(malformed.data() + 8);
        be32(malformed.data() + structure_offset + property_length_offset, 3);
        status = gxos_aarch64_phase2_parse_dtb_ex(malformed.data(), malformed.size(), &platform);
        ok &= expect(status == GXOS_AARCH64_DTB_MALFORMED &&
                     platform.parser_stage == GXOS_AARCH64_DTB_PARSE_STAGE_STRUCTURE &&
                     platform.parser_error == GXOS_AARCH64_DTB_PARSE_ERROR_PROPERTY_VALUE_FORMAT &&
                     platform.parser_last_token == 3 &&
                     platform.parser_offset == property_length_offset,
                     "known synthetic property length failure reports its production parser stage and cursor");
    }
    return ok;
}

static bool test_handoff_range_validation(uint64_t dtb_size)
{
    bool ok = true;
    uint64_t end = 0;
    ok &= expect(gxos_aarch64_dtb_allocation_bounds(UINT64_C(0x70000000), dtb_size, &end) &&
                 end == UINT64_C(0x70001000),
                 "page-aligned DTB handoff range validates");
    ok &= expect(!gxos_aarch64_dtb_allocation_bounds(0, dtb_size, &end),
                 "null DTB handoff pointer is rejected");
    ok &= expect(!gxos_aarch64_dtb_allocation_bounds(UINT64_C(0x70000001), dtb_size, &end),
                 "unaligned DTB handoff pointer is rejected");
    ok &= expect(!gxos_aarch64_dtb_allocation_bounds(UINT64_C(0x70000000), 39, &end),
                 "truncated DTB handoff length is rejected");
    ok &= expect(!gxos_aarch64_dtb_allocation_bounds(UINT64_C(0x70000000),
                                                      UINT64_C(16) * 1024 * 1024 + 1, &end),
                 "oversized DTB handoff length is rejected");
    ok &= expect(!gxos_aarch64_dtb_allocation_bounds(UINT64_C(0xfffffffffffff000), 4097, &end),
                 "overflowing DTB allocation range is rejected");
    return ok;
}

static bool test_memory_map_coverage(uint64_t dtb_size)
{
    uint8_t descriptor[40] = {};
    le32(descriptor, 2); // UEFI EfiLoaderData.
    le64(descriptor + 8, UINT64_C(0x70000000));
    le64(descriptor + 24, 1);
    const uint64_t dtbPages = (dtb_size + UINT64_C(0xfff)) / UINT64_C(0x1000);
    bool ok = true;
    ok &= expect(gxos_aarch64_memory_map_range_covered(
                     descriptor, UINT64_C(0x71000000), sizeof(descriptor), sizeof(descriptor), 1,
                     UINT64_C(0x70000000), UINT64_C(0x1000), 2),
                 "complete LoaderData memory-map coverage is accepted");
    ok &= expect(!gxos_aarch64_memory_map_range_covered(
                     descriptor, UINT64_C(0x71000000), sizeof(descriptor), sizeof(descriptor), 1,
                     UINT64_C(0x70000000), UINT64_C(0x2000), 2),
                 "incomplete DTB allocation mapping is rejected");
    ok &= expect(!gxos_aarch64_memory_map_range_covered(
                     descriptor, UINT64_C(0x71000000), sizeof(descriptor), sizeof(descriptor), 1,
                     UINT64_C(0x70000000), UINT64_C(0x1000), 7),
                 "conventional memory does not masquerade as the LoaderData DTB allocation");
    ok &= expect(!gxos_aarch64_memory_map_range_covered(
                     nullptr, UINT64_C(0x71000000), sizeof(descriptor), sizeof(descriptor), 1,
                     UINT64_C(0x70000000), UINT64_C(0x1000), 2),
                 "null memory-map pointer is rejected before descriptor reads");
    ok &= expect(!gxos_aarch64_memory_map_range_covered(
                     descriptor, 0, sizeof(descriptor), sizeof(descriptor), 1,
                     UINT64_C(0x70000000), UINT64_C(0x1000), 2),
                 "zero memory-map address is rejected");
    ok &= expect(dtbPages == 1, "synthetic FDT occupies one allocated DTB page");
    return ok;
}

static bool test_loader_kernel_transfer(const std::vector<uint8_t>& source)
{
    std::vector<uint8_t> copy(source);
    uint8_t descriptor[40] = {};
    le32(descriptor, 2); // UEFI EfiLoaderData.
    le64(descriptor + 8, UINT64_C(0x72000000));
    le64(descriptor + 24, 1);
    uint64_t allocationEnd = 0;
    gxos_aarch64_fdt_header_info header = {};
    gxos_aarch64_phase2_platform platform = {};
    bool ok = true;
    ok &= expect(gxos_aarch64_fdt_header_validate(copy.data(), copy.size(), &header) ==
                     GXOS_AARCH64_FDT_HEADER_OK,
                 "loader validates the complete DTB header before copying");
    ok &= expect(gxos_aarch64_dtb_allocation_bounds(UINT64_C(0x72000000), copy.size(),
                                                     &allocationEnd),
                 "copied DTB physical range is bounded");
    ok &= expect(gxos_aarch64_memory_map_range_covered(
                     descriptor, UINT64_C(0x73000000), sizeof(descriptor), sizeof(descriptor), 1,
                     UINT64_C(0x72000000), allocationEnd - UINT64_C(0x72000000), 2),
                 "copied DTB remains in LoaderData after ExitBootServices");
    ok &= expect(gxos_aarch64_phase2_parse_dtb_ex(copy.data(), copy.size(), &platform) ==
                     GXOS_AARCH64_DTB_OK,
                 "kernel production parser accepts the copied DTB");
    ok &= expect(platform.platform_kind == GXOS_AARCH64_PLATFORM_QEMU_VIRT &&
                 platform.gic_version == GXOS_AARCH64_GIC_V2 &&
                 platform.gicc_base == UINT64_C(0x08010000) &&
                 platform.timer_irq == 30 && platform.timer_source == 2,
                 "transferred DTB discovers RAM, GICv2, and the physical timer");
    return ok;
}

int main(int argc, char** argv)
{
    bool ok = true;
    const std::vector<uint8_t> valid = build_qemu_gicv2_dtb();
    gxos_aarch64_phase2_platform platform = {};
    ok &= expect(gxos_aarch64_phase2_parse_dtb_ex(valid.data(), valid.size(), &platform) ==
                     GXOS_AARCH64_DTB_OK,
                 "real production parser accepts a complete QEMU virt GICv2 fixture");
    ok &= expect(platform.parser_stage == GXOS_AARCH64_DTB_PARSE_STAGE_COMPLETE,
                 "successful parser reports complete stage");
    ok &= test_header_validation(valid);
    ok &= test_structure_trace(valid);
    ok &= test_handoff_range_validation(valid.size());
    ok &= test_memory_map_coverage(valid.size());
    ok &= test_loader_kernel_transfer(valid);

    if (argc > 1) {
        FILE* file = fopen(argv[1], "rb");
        static uint8_t blob[16 * 1024 * 1024];
        const size_t size = file ? fread(blob, 1, sizeof(blob), file) : 0;
        if (file) fclose(file);
        gxos_aarch64_phase2_platform external = {};
        const bool parsed = file != nullptr && size != 0 &&
            gxos_aarch64_phase2_parse_dtb(blob, size, &external) != 0;
        printf("source DTB parser result=%u diagnostic=%u stage=%u token=%u offset=%u\n",
               parsed ? 1 : 0, external.diagnostic, external.parser_stage,
               external.parser_last_token, external.parser_offset);
        ok &= expect(parsed, "provided source-matched DTB parses");
    }

    if (!ok) {
        fprintf(stderr, "AARCH64 Phase 2 host validation tests: FAIL (%u assertions)\n", gAssertions);
        return 1;
    }
    printf("AARCH64 Phase 2 host validation tests: PASS (%u assertions)\n", gAssertions);
    return 0;
}
