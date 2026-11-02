#include <stdint.h>

#include "../../../aarch64/phase2/phase2_platform.h"
#include "../../../aarch64/phase2/phase2_validation.h"

namespace {

static const uint32_t kFdtBeginNode = 1;
static const uint32_t kFdtEndNode = 2;
static const uint32_t kFdtProp = 3;
static const uint32_t kFdtNop = 4;
static const uint32_t kFdtEnd = 9;

static void zero_bytes(void* destination, uint64_t size)
{
    uint8_t* bytes = (uint8_t*)destination;
    for (uint64_t i = 0; i < size; ++i) bytes[i] = 0;
}

static bool bytes_equal(const uint8_t* left, const char* right, uint32_t length)
{
    for (uint32_t i = 0; i < length; ++i) if (left[i] != (uint8_t)right[i]) return false;
    return true;
}

struct FdtNode {
    uint8_t is_root;
    uint8_t is_memory;
    uint8_t is_uart;
    uint8_t is_gic;
    uint8_t is_cpu;
    uint8_t is_interrupt_controller;
    uint8_t available;
    uint8_t has_redistributor_stride;
    uint8_t has_redistributor_regions;
    uint8_t is_timer;
    uint8_t is_virtio_mmio;
    uint8_t is_raspberry_pi;
    uint8_t is_qemu_virt;
    uint8_t name_length;
    uint8_t name_truncated;
    char name[48];
    uint8_t gic_version;
    uint32_t interrupt_cells;
    uint32_t redistributor_stride;
    uint32_t redistributor_regions;
    uint32_t address_cells;
    uint32_t size_cells;
    uint32_t parent_address_cells;
    uint32_t parent_size_cells;
    const uint8_t* reg_value;
    uint32_t reg_length;
    const uint8_t* interrupts_value;
    uint32_t interrupts_length;
    const uint8_t* ranges_value;
    uint32_t ranges_length;
    uint8_t has_ranges;
};

static uint32_t load_be32(const uint8_t* p)
{
    return gxos_aarch64_be32(p);
}

static bool equal_string(const uint8_t* value, uint32_t length, const char* expected)
{
    uint32_t i = 0;
    for (; expected[i] != 0; ++i) {
        if (i >= length || value[i] != (uint8_t)expected[i]) return false;
    }
    return i < length && value[i] == 0;
}

static bool has_compatible(const uint8_t* value, uint32_t length, const char* expected)
{
    uint32_t offset = 0;
    while (offset < length) {
        uint32_t end = offset;
        while (end < length && value[end] != 0) ++end;
        if (end == length) return false;
        if (equal_string(value + offset, end - offset + 1, expected)) return true;
        offset = end + 1;
    }
    return false;
}

static bool string_list_valid(const uint8_t* value, uint32_t length)
{
    if (!value || length == 0 || value[length - 1] != 0) return false;
    uint32_t offset = 0;
    while (offset < length) {
        uint32_t end = offset;
        while (end < length && value[end] != 0) ++end;
        if (end == offset || end == length) return false;
        offset = end + 1;
    }
    return offset == length;
}

static bool node_name_is(const uint8_t* value, uint32_t length, const char* prefix)
{
    uint32_t prefixLength = 0;
    while (prefix[prefixLength] != 0) ++prefixLength;
    return length >= prefixLength &&
           (prefixLength == 0 || bytes_equal(value, prefix, prefixLength)) &&
           (length == prefixLength || value[prefixLength] == '@');
}

static bool read_cells(const uint8_t* bytes, uint32_t cells, uint64_t* value)
{
    if (!value || cells == 0 || cells > 2) return false;
    uint64_t result = 0;
    for (uint32_t i = 0; i < cells; ++i) {
        if (result > (UINT64_MAX >> 32)) return false;
        result = (result << 32) | load_be32(bytes + i * 4);
    }
    *value = result;
    return true;
}

static bool property_string(const uint8_t* value, uint32_t length, const char* expected)
{
    return length != 0 && value[length - 1] == 0 && equal_string(value, length, expected);
}

static bool add_ram(gxos_aarch64_phase2_platform* platform, uint64_t base, uint64_t size)
{
    if (size == 0) return true;
    if (platform->ram_count >= GXOS_AARCH64_PHASE2_MAX_RAM_RANGES) return false;
    uint64_t end = 0;
    if (!gxos_aarch64_add_u64(base, size, &end) || end <= base) return false;
    platform->ram[platform->ram_count].base = base;
    platform->ram[platform->ram_count].size = size;
    ++platform->ram_count;
    return true;
}

static bool add_virtio_mmio(gxos_aarch64_phase2_platform* platform,
                            uint64_t base, uint64_t size)
{
    if (size == 0) return true;
    if (platform->virtio_mmio_count >= GXOS_AARCH64_PHASE2_MAX_VIRTIO_MMIO) return false;
    uint64_t end = 0;
    if (!gxos_aarch64_add_u64(base, size, &end) || end <= base) return false;
    gxos_aarch64_phase2_virtio_mmio& device =
        platform->virtio_mmio[platform->virtio_mmio_count++];
    device.base = base;
    device.size = size;
    device.irq = 0;
    device.reserved = 0;
    return true;
}

static bool add_gicv3_region(gxos_aarch64_phase2_platform* platform,
                             uint64_t base, uint64_t size)
{
    if (platform->gic_redistributor_region_count >= GXOS_AARCH64_PHASE2_MAX_GICR_REGIONS) {
        return false;
    }
    gxos_aarch64_gicv3_region& region =
        platform->gic_redistributor_regions[platform->gic_redistributor_region_count++];
    region.base = base;
    region.size = size;
    return true;
}

static bool ranges_overlap_checked(uint64_t left_base, uint64_t left_size,
                                   uint64_t right_base, uint64_t right_size)
{
    uint64_t left_end = 0;
    uint64_t right_end = 0;
    if (left_size == 0 || right_size == 0 ||
        !gxos_aarch64_add_u64(left_base, left_size, &left_end) ||
        !gxos_aarch64_add_u64(right_base, right_size, &right_end)) return true;
    return gxos_aarch64_range_overlaps({left_base, left_end}, {right_base, right_end});
}

static bool translate_through_parent_ranges(const FdtNode* stack, uint32_t depth,
                                            uint64_t* base, uint64_t size,
                                            gxos_aarch64_phase2_platform* platform)
{
    if (!stack || depth < 2 || !base || size == 0) return false;
    uint64_t currentBase = *base;
    uint64_t currentEnd = 0;
    if (!gxos_aarch64_add_u64(currentBase, size, &currentEnd)) return false;

    // A node's reg is expressed in its parent bus address space.  Walk each
    // ancestor bus and apply its ranges property until the root physical
    // address space is reached.  Root-level devices are already physical.
    for (int32_t busIndex = (int32_t)depth - 2; busIndex > 0; --busIndex) {
        const FdtNode& bus = stack[busIndex];
        if (!bus.has_ranges) return false;
        const uint32_t childCells = bus.address_cells;
        const uint32_t parentCells = bus.parent_address_cells;
        const uint32_t sizeCells = bus.size_cells;
        if (childCells == 0 || childCells > 2 || parentCells == 0 || parentCells > 2 ||
            sizeCells == 0 || sizeCells > 2) return false;
        const uint32_t tupleCells = childCells + parentCells + sizeCells;
        // An explicitly empty ranges property means a 1:1 bus mapping.
        if (bus.ranges_length == 0) continue;
        if ((bus.ranges_length % (tupleCells * 4)) != 0) return false;

        bool matched = false;
        const uint32_t tupleCount = bus.ranges_length / (tupleCells * 4);
        for (uint32_t tuple = 0; tuple < tupleCount; ++tuple) {
            const uint8_t* entry = bus.ranges_value + tuple * tupleCells * 4;
            uint64_t childBase = 0;
            uint64_t parentBase = 0;
            uint64_t rangeSize = 0;
            if (!read_cells(entry, childCells, &childBase) ||
                !read_cells(entry + childCells * 4, parentCells, &parentBase) ||
                !read_cells(entry + (childCells + parentCells) * 4, sizeCells, &rangeSize) ||
                rangeSize == 0) return false;
            uint64_t childRangeEnd = 0;
            uint64_t rangeEnd = 0;
            if (!gxos_aarch64_add_u64(childBase, rangeSize, &childRangeEnd) ||
                !gxos_aarch64_add_u64(parentBase, rangeSize, &rangeEnd)) return false;
            if (currentBase >= childBase && currentEnd <= childRangeEnd) {
                const uint64_t offset = currentBase - childBase;
                uint64_t translated = 0;
                if (!gxos_aarch64_add_u64(parentBase, offset, &translated)) return false;
                if (!gxos_aarch64_add_u64(translated, size, &rangeEnd)) return false;
                currentBase = translated;
                currentEnd = rangeEnd;
                platform->ranges_translated = 1;
                matched = true;
                break;
            }
        }
        if (!matched) return false;
    }
    *base = currentBase;
    return currentEnd > currentBase;
}

static bool parse_reg(const uint8_t* value, uint32_t length, const FdtNode& node,
                      const FdtNode* stack, uint32_t depth,
                      gxos_aarch64_phase2_platform* platform)
{
    uint32_t addressCells = node.parent_address_cells;
    uint32_t sizeCells = node.parent_size_cells;
    uint32_t cells = 0;
    bool qemuVirtioRootCellFallback = false;
    if (addressCells == 0 || addressCells > 2 || sizeCells == 0 || sizeCells > 2 ||
        length == 0) {
        // QEMU 11 emits platform-bus virtio-mmio children with root-shaped
        // reg tuples even though platform-bus advertises a different child
        // cell shape.  Keep this exception bounded to those optional QEMU
        // devices; Pi memory/UART/GIC nodes remain strict and translated.
        const uint32_t rootAddressCells = platform->address_cells;
        const uint32_t rootSizeCells = platform->size_cells;
        if (!node.is_virtio_mmio || rootAddressCells == 0 || rootAddressCells > 2 ||
            rootSizeCells == 0 || rootSizeCells > 2) return false;
        addressCells = rootAddressCells;
        sizeCells = rootSizeCells;
        cells = rootAddressCells + rootSizeCells;
        if (length % (cells * 4) != 0) return false;
        qemuVirtioRootCellFallback = true;
    } else {
        cells = addressCells + sizeCells;
        if (cells > 4 || length % (cells * 4) != 0) return false;
    }

    uint32_t entry = 0;
    uint32_t entryCount = length / (cells * 4);
    while (entry < entryCount) {
        const uint8_t* p = value + entry * cells * 4;
        uint64_t base = 0;
        uint64_t size = 0;
        if (!read_cells(p, addressCells, &base) ||
            !read_cells(p + addressCells * 4, sizeCells, &size) || size == 0) return false;
        if (!qemuVirtioRootCellFallback &&
            !translate_through_parent_ranges(stack, depth, &base, size, platform)) return false;
        if (node.is_memory && !add_ram(platform, base, size)) return false;
        if (node.is_uart && platform->uart_base == 0) {
            platform->uart_base = base;
            platform->uart_size = size;
            platform->uart_kind = GXOS_AARCH64_UART_PL011;
        }
        if (node.is_gic) {
            platform->gic_version = node.gic_version;
            if (entry == 0 && platform->gicd_base == 0) {
                platform->gicd_base = base;
                platform->gicd_size = size;
            } else if (node.gic_version == GXOS_AARCH64_GIC_V2 &&
                       entry == 1 && platform->gicc_base == 0) {
                platform->gicc_base = base;
                platform->gicc_size = size;
            } else if (node.gic_version == GXOS_AARCH64_GIC_V3 && entry > 0 &&
                       !add_gicv3_region(platform, base, size)) {
                return false;
            }
        }
        if (node.is_virtio_mmio && entry == 0 && !add_virtio_mmio(platform, base, size)) return false;
        ++entry;
    }
    return true;
}

static bool parse_cpu_affinities(const uint8_t* value, uint32_t length,
                                 const FdtNode& node,
                                 gxos_aarch64_phase2_platform* platform)
{
    if (!node.is_cpu) return true;
    const uint32_t addressCells = node.parent_address_cells;
    const uint32_t sizeCells = node.parent_size_cells;
    if (addressCells == 0 || addressCells > 2 || sizeCells > 2 ||
        (addressCells + sizeCells) == 0 ||
        length % ((addressCells + sizeCells) * 4) != 0) return false;
    const uint32_t tupleBytes = (addressCells + sizeCells) * 4;
    for (uint32_t offset = 0; offset < length; offset += tupleBytes) {
        uint64_t affinity = 0;
        if (!read_cells(value + offset, addressCells, &affinity) ||
            (affinity & ~UINT64_C(0x000000ff00ffffff)) != 0 ||
            platform->gic_cpu_affinity_count >= GXOS_AARCH64_PHASE2_MAX_CPU_AFFINITIES) {
            return false;
        }
        platform->gic_cpu_affinity[platform->gic_cpu_affinity_count++] = affinity;
    }
    return true;
}

static void parse_virtio_interrupts(const FdtNode& node,
                                    gxos_aarch64_phase2_platform* platform)
{
    if (!node.is_virtio_mmio || platform->virtio_mmio_count == 0 ||
        !node.interrupts_value || node.interrupts_length < 12 ||
        (node.interrupts_length % 12) != 0) return;
    // On the QEMU virt machine the interrupt-parent is the GIC and the
    // binding is <type, number, flags>.  A type of zero denotes a SPI whose
    // global interrupt number is 32 + number.
    const uint32_t type = load_be32(node.interrupts_value);
    const uint32_t number = load_be32(node.interrupts_value + 4);
    if (type != 0 || number > 95) return;
    platform->virtio_mmio[platform->virtio_mmio_count - 1].irq = 32 + number;
}

static void parse_timer_interrupts(const uint8_t* value, uint32_t length,
                                   gxos_aarch64_phase2_platform* platform)
{
    // QEMU's arm,armv8-timer binding lists secure, physical, virtual, and
    // hypervisor PPIs in that order.  Use the non-secure physical timer PPI;
    // it is directly controlled at EL1 without relying on EL2 virtual-timer
    // routing state left by firmware.
    if (length < 3 * 4 * 3 || (length % (3 * 4)) != 0) return;
    const uint32_t tuple = 1;
    const uint8_t* p = value + tuple * 3 * 4;
    const uint32_t type = load_be32(p);
    const uint32_t number = load_be32(p + 4);
    if (type != 1 || number > 31) return;
    platform->timer_irq = 16 + number;
    platform->timer_source = 2;
}

static void classify_compatible(const uint8_t* value, uint32_t length, FdtNode* node)
{
    if (has_compatible(value, length, "arm,pl011") ||
        has_compatible(value, length, "brcm,bcm2835-pl011")) node->is_uart = 1;
    if (has_compatible(value, length, "arm,cortex-a15-gic") ||
        has_compatible(value, length, "arm,gic-400") ||
        has_compatible(value, length, "arm,gic-v2")) {
        node->is_gic = 1;
        node->gic_version = GXOS_AARCH64_GIC_V2;
    }
    if (has_compatible(value, length, "arm,gic-v3") ||
        has_compatible(value, length, "arm,gic-v3.1")) {
        node->is_gic = 1;
        node->gic_version = GXOS_AARCH64_GIC_V3;
    }
    if (has_compatible(value, length, "arm,armv8-timer") ||
        has_compatible(value, length, "arm,armv7-timer")) node->is_timer = 1;
    if (has_compatible(value, length, "virtio,mmio")) node->is_virtio_mmio = 1;
    if (has_compatible(value, length, "raspberrypi,4-model-b") ||
        has_compatible(value, length, "brcm,bcm2711")) node->is_raspberry_pi = 1;
    if (has_compatible(value, length, "linux,dummy-virt") ||
        has_compatible(value, length, "qemu,virt")) node->is_qemu_virt = 1;
}

static bool property_name(const uint8_t* strings, uint32_t stringsSize, uint32_t nameOffset,
                          const char* expected)
{
    if (nameOffset >= stringsSize) return false;
    const uint8_t* name = strings + nameOffset;
    uint32_t remaining = stringsSize - nameOffset;
    return equal_string(name, remaining, expected);
}

static bool property_name_is_terminated(const uint8_t* strings, uint32_t stringsSize,
                                        uint32_t nameOffset)
{
    if (!strings || nameOffset >= stringsSize) return false;
    for (uint32_t offset = nameOffset; offset < stringsSize; ++offset) {
        if (strings[offset] == 0) return offset != nameOffset;
    }
    return false;
}

static uint8_t parser_fail(gxos_aarch64_phase2_platform* platform,
                           gxos_aarch64_dtb_parse_error error)
{
    if (platform) platform->parser_error = (uint32_t)error;
    return 0;
}

struct PropertyDiagnosticContext {
    uint32_t token_offset;
    uint32_t name_field_offset;
    uint32_t name_offset;
    uint32_t value_offset;
    uint32_t value_length;
    const uint8_t* value;
};

static uint8_t parser_property_format_fail(
    gxos_aarch64_phase2_platform* platform,
    const FdtNode* stack, uint32_t depth,
    const uint8_t* strings, uint32_t strings_size,
    const PropertyDiagnosticContext& property,
    uint32_t predicate, uint32_t observed_cells, bool has_observed_cells)
{
    if (platform) {
        platform->parser_property_predicate = predicate;
        platform->parser_property_token_offset = property.token_offset;
        platform->parser_property_name_field_offset = property.name_field_offset;
        platform->parser_property_name_offset = property.name_offset;
        platform->parser_property_value_offset = property.value_offset;
        platform->parser_property_value_length = property.value_length;
        platform->parser_property_observed_cells = observed_cells;
        platform->parser_property_detail_valid = 1;
        platform->parser_property_has_observed_cells = has_observed_cells ? 1 : 0;
        if (depth != 0 && stack) {
            const FdtNode& node = stack[depth - 1];
            platform->parser_property_parent_address_cells = node.parent_address_cells;
            platform->parser_property_parent_size_cells = node.parent_size_cells;
            platform->parser_property_interrupt_cells = (uint8_t)node.interrupt_cells;
        }

        uint32_t path_length = 0;
        platform->parser_property_node_path[path_length++] = '/';
        for (uint32_t i = 0; stack && i < depth; ++i) {
            const FdtNode& node = stack[i];
            if (i == 0 && node.name_length == 0) continue;
            if (path_length > 1) {
                if (path_length + 1 >= GXOS_AARCH64_DTB_PROPERTY_DIAGNOSTIC_PATH_BYTES) {
                    platform->parser_property_path_truncated = 1;
                    break;
                }
                platform->parser_property_node_path[path_length++] = '/';
            }
            if (node.name_truncated) platform->parser_property_path_truncated = 1;
            for (uint32_t n = 0; n < node.name_length; ++n) {
                if (path_length + 1 >= GXOS_AARCH64_DTB_PROPERTY_DIAGNOSTIC_PATH_BYTES) {
                    platform->parser_property_path_truncated = 1;
                    break;
                }
                platform->parser_property_node_path[path_length++] = node.name[n];
            }
            if (platform->parser_property_path_truncated && path_length + 1 >=
                GXOS_AARCH64_DTB_PROPERTY_DIAGNOSTIC_PATH_BYTES) break;
        }
        platform->parser_property_node_path[path_length] = 0;

        if (strings && property.name_offset < strings_size) {
            uint32_t name_length = 0;
            while (name_length < strings_size - property.name_offset &&
                   strings[property.name_offset + name_length] != 0) ++name_length;
            if (name_length < strings_size - property.name_offset) {
                uint32_t copy_length = name_length;
                if (copy_length >= GXOS_AARCH64_DTB_PROPERTY_DIAGNOSTIC_NAME_BYTES) {
                    copy_length = GXOS_AARCH64_DTB_PROPERTY_DIAGNOSTIC_NAME_BYTES - 1;
                    platform->parser_property_name_truncated = 1;
                }
                for (uint32_t i = 0; i < copy_length; ++i) {
                    platform->parser_property_name[i] =
                        (char)strings[property.name_offset + i];
                }
                platform->parser_property_name[copy_length] = 0;
            }
        }

        uint32_t preview_length = property.value_length;
        if (preview_length > GXOS_AARCH64_DTB_PROPERTY_DIAGNOSTIC_PREVIEW_BYTES) {
            preview_length = GXOS_AARCH64_DTB_PROPERTY_DIAGNOSTIC_PREVIEW_BYTES;
        }
        if (property.value) {
            for (uint32_t i = 0; i < preview_length; ++i) {
                platform->parser_property_preview[i] = property.value[i];
            }
            platform->parser_property_preview_length = (uint8_t)preview_length;
        }
    }
    return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_PROPERTY_VALUE_FORMAT);
}

struct ParserTraceGuard {
    gxos_aarch64_phase2_platform* platform;
    bool complete;

    explicit ParserTraceGuard(gxos_aarch64_phase2_platform* value)
        : platform(value), complete(false) {}

    ~ParserTraceGuard()
    {
        if (platform && !complete && platform->parser_error == GXOS_AARCH64_DTB_PARSE_ERROR_NONE) {
            platform->parser_error = GXOS_AARCH64_DTB_PARSE_ERROR_STRUCTURE;
        }
    }
};

} // namespace

extern "C" uint8_t gxos_aarch64_phase2_parse_dtb(const void* blob, uint64_t blob_size,
                                                  gxos_aarch64_phase2_platform* platform)
{
    if (!platform) return 0;
    zero_bytes(platform, sizeof(*platform));
    platform->diagnostic = GXOS_AARCH64_DTB_MALFORMED;
    platform->parser_stage = GXOS_AARCH64_DTB_PARSE_STAGE_HEADER;

    const uint8_t* bytes = (const uint8_t*)blob;
    platform->header_status = gxos_aarch64_fdt_header_validate(bytes, blob_size,
                                                               &platform->header);
    if (platform->header_status != GXOS_AARCH64_FDT_HEADER_OK) return 0;
    const uint32_t total = platform->header.total_size;
    const uint32_t structOffset = platform->header.structure_offset;
    const uint32_t structSize = platform->header.structure_size;
    const uint32_t stringsOffset = platform->header.strings_offset;
    const uint32_t stringsSize = platform->header.strings_size;
    const uint8_t* structBase = bytes + structOffset;
    const uint8_t* structEnd = structBase + structSize;
    const uint8_t* strings = bytes + stringsOffset;

    ParserTraceGuard trace(platform);
    platform->parser_stage = GXOS_AARCH64_DTB_PARSE_STAGE_STRUCTURE;
    platform->address_cells = 2;
    platform->size_cells = 1;
    FdtNode stack[32];
    zero_bytes(stack, sizeof(stack));
    uint32_t depth = 0;
    uint32_t offset = 0;
    while (offset < structSize) {
        if (structSize - offset < 4) {
            return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_TRUNCATED_TOKEN);
        }
        const uint8_t* tokenBytes = structBase + offset;
        const uint32_t token = load_be32(tokenBytes);
        offset += 4;
        const uint32_t traceOffset = offset & UINT32_C(0x00ffffff);
        platform->parser_last_token = (uint8_t)token;
        platform->parser_offset = offset;
        platform->parser_offset_low[0] = (uint8_t)traceOffset;
        platform->parser_offset_low[1] = (uint8_t)(traceOffset >> 8);
        platform->parser_offset_low[2] = (uint8_t)(traceOffset >> 16);
        if (token == kFdtBeginNode) {
            if (depth >= 32) return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_NESTING);
            const uint32_t nameStart = offset;
            while (offset < structSize && structBase[offset] != 0) ++offset;
            if (offset >= structSize) return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_NODE_NAME);
            FdtNode& node = stack[depth];
            zero_bytes(&node, sizeof(node));
            node.is_root = (depth == 0);
            node.available = 1;
            if (node.is_root) {
                node.address_cells = 2;
                node.size_cells = 1;
                node.parent_address_cells = 2;
                node.parent_size_cells = 1;
            } else {
                const FdtNode& parent = stack[depth - 1];
                node.parent_address_cells = parent.address_cells;
                node.parent_size_cells = parent.size_cells;
                node.address_cells = 2;
                node.size_cells = 1;
            }
            const uint32_t nameLength = offset - nameStart;
            const uint32_t storedNameLength = nameLength < sizeof(node.name) - 1
                ? nameLength : (uint32_t)sizeof(node.name) - 1;
            for (uint32_t i = 0; i < storedNameLength; ++i) {
                node.name[i] = (char)structBase[nameStart + i];
            }
            node.name[storedNameLength] = 0;
            node.name_length = (uint8_t)storedNameLength;
            node.name_truncated = nameLength > storedNameLength ? 1 : 0;
            if (node_name_is(structBase + nameStart, nameLength, "memory")) node.is_memory = 1;
            if (node_name_is(structBase + nameStart, nameLength, "cpu")) node.is_cpu = 1;
            ++offset;
            offset = (offset + 3) & ~3u;
            if (offset > structSize) return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_NODE_NAME);
            ++depth;
        } else if (token == kFdtEndNode) {
            if (depth == 0) return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_END_NODE);
            FdtNode& node = stack[depth - 1];
            if (node.is_gic) {
                if (platform->gic_node_seen) {
                    platform->diagnostic = GXOS_AARCH64_DTB_UNSUPPORTED_CONTROLLER;
                    return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_CONTROLLER);
                }
                platform->gic_node_seen = 1;
                platform->gic_version = node.gic_version;
                platform->gic_node_available = node.available;
                platform->gic_interrupt_cells = node.interrupt_cells;
                platform->gic_has_redistributor_stride = node.has_redistributor_stride;
                platform->gic_has_redistributor_regions = node.has_redistributor_regions;
                if (node.has_redistributor_stride) platform->gic_redistributor_stride = node.redistributor_stride;
            } else if (node.is_interrupt_controller) {
                platform->unsupported_interrupt_controller_seen = 1;
            }
            if (node.available && node.reg_value &&
                (node.is_memory || node.is_uart || node.is_gic || node.is_virtio_mmio) &&
                !parse_reg(node.reg_value, node.reg_length, node, stack, depth, platform)) {
                return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_REG_RANGE);
            }
            if (node.is_gic && node.available && node.gic_version == GXOS_AARCH64_GIC_V3 &&
                node.has_redistributor_regions &&
                node.redistributor_regions != platform->gic_redistributor_region_count) {
                platform->diagnostic = GXOS_AARCH64_DTB_INVALID_CONTROLLER_LAYOUT;
                return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_CONTROLLER);
            }
            if (node.available && node.is_cpu && node.reg_value &&
                !parse_cpu_affinities(node.reg_value, node.reg_length, node, platform)) {
                return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_CPU_AFFINITY);
            }
            if (node.is_timer && node.interrupts_value) {
                parse_timer_interrupts(node.interrupts_value, node.interrupts_length, platform);
            }
            if (node.is_virtio_mmio) parse_virtio_interrupts(node, platform);
            if (node.is_root) {
                if (node.is_raspberry_pi) platform->platform_kind = GXOS_AARCH64_PLATFORM_RASPBERRY_PI4;
                else if (node.is_qemu_virt) platform->platform_kind = GXOS_AARCH64_PLATFORM_QEMU_VIRT;
            }
            --depth;
        } else if (token == kFdtProp) {
            if (depth == 0 || structSize - offset < 8) {
                return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_PROPERTY_HEADER);
            }
            const uint32_t tokenOffset = offset - 4;
            const uint32_t length = load_be32(structBase + offset);
            const uint32_t nameOffset = load_be32(structBase + offset + 4);
            const uint32_t valueOffset = offset + 8;
            if (length > structSize - valueOffset) {
                return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_PROPERTY_VALUE);
            }
            const uint32_t valueEnd = valueOffset + length;
            const uint32_t padding = (4 - (length & 3)) & 3;
            if (padding > structSize - valueEnd) {
                return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_PROPERTY_VALUE);
            }
            const uint32_t propertyNext = valueEnd + padding;
            if (!property_name_is_terminated(strings, stringsSize, nameOffset)) {
                return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_PROPERTY_NAME);
            }
            const PropertyDiagnosticContext property = {
                tokenOffset, offset + 4, nameOffset, valueOffset, length,
                structBase + valueOffset
            };
            offset = valueOffset;
            if (!property_name(strings, stringsSize, nameOffset, "compatible") &&
                !property_name(strings, stringsSize, nameOffset, "reg") &&
                !property_name(strings, stringsSize, nameOffset, "interrupts") &&
                !property_name(strings, stringsSize, nameOffset, "#address-cells") &&
                !property_name(strings, stringsSize, nameOffset, "#size-cells") &&
                !property_name(strings, stringsSize, nameOffset, "#interrupt-cells") &&
                !property_name(strings, stringsSize, nameOffset, "#redistributor-regions") &&
                !property_name(strings, stringsSize, nameOffset, "redistributor-stride") &&
                !property_name(strings, stringsSize, nameOffset, "device_type") &&
                !property_name(strings, stringsSize, nameOffset, "interrupt-controller") &&
                !property_name(strings, stringsSize, nameOffset, "status") &&
                !property_name(strings, stringsSize, nameOffset, "ranges")) {
                offset = propertyNext;
                continue;
            }
            FdtNode& node = stack[depth - 1];
            const uint8_t* value = structBase + offset;
            if (property_name(strings, stringsSize, nameOffset, "compatible")) {
                if (!string_list_valid(value, length)) {
                    return parser_property_format_fail(platform, stack, depth, strings,
                        stringsSize, property,
                        GXOS_AARCH64_DTB_PROPERTY_PREDICATE_COMPATIBLE_STRING_LIST,
                        0, false);
                }
                classify_compatible(value, length, &node);
            } else if (property_name(strings, stringsSize, nameOffset, "reg")) {
                node.reg_value = value;
                node.reg_length = length;
            } else if (property_name(strings, stringsSize, nameOffset, "interrupts")) {
                node.interrupts_value = value;
                node.interrupts_length = length;
            } else if (property_name(strings, stringsSize, nameOffset, "#address-cells")) {
                if (length != 4) return parser_property_format_fail(platform, stack, depth,
                    strings, stringsSize, property,
                    GXOS_AARCH64_DTB_PROPERTY_PREDICATE_ADDRESS_CELLS_LENGTH, 0, false);
                const uint32_t cells = load_be32(value);
                // #address-cells is a bus-defined child-reg width, not a
                // universal one-or-two-cell address limit. PCI buses use 3.
                // Keep the full count; resource decoding below remains
                // bounded to the address widths this platform parser supports.
                node.address_cells = cells;
                if (node.is_root) {
                    platform->address_cells = cells <= UINT8_MAX ? (uint8_t)cells : UINT8_MAX;
                }
            } else if (property_name(strings, stringsSize, nameOffset, "#size-cells")) {
                if (length != 4) return parser_property_format_fail(platform, stack, depth,
                    strings, stringsSize, property,
                    GXOS_AARCH64_DTB_PROPERTY_PREDICATE_SIZE_CELLS_LENGTH, 0, false);
                const uint32_t cells = load_be32(value);
                node.size_cells = cells;
                if (node.is_root) {
                    platform->size_cells = cells <= UINT8_MAX ? (uint8_t)cells : UINT8_MAX;
                }
            } else if (property_name(strings, stringsSize, nameOffset, "device_type") &&
                       property_string(value, length, "memory")) {
                node.is_memory = 1;
            } else if (property_name(strings, stringsSize, nameOffset, "#interrupt-cells")) {
                if (length != 4) return parser_property_format_fail(platform, stack, depth,
                    strings, stringsSize, property,
                    GXOS_AARCH64_DTB_PROPERTY_PREDICATE_INTERRUPT_CELLS_LENGTH, 0, false);
                const uint32_t cells = load_be32(value);
                if (cells == 0 || cells > 4) return parser_property_format_fail(platform,
                    stack, depth, strings, stringsSize, property,
                    GXOS_AARCH64_DTB_PROPERTY_PREDICATE_INTERRUPT_CELLS_RANGE,
                    cells, true);
                node.interrupt_cells = cells;
            } else if (property_name(strings, stringsSize, nameOffset, "#redistributor-regions")) {
                if (length != 4) return parser_property_format_fail(platform, stack, depth,
                    strings, stringsSize, property,
                    GXOS_AARCH64_DTB_PROPERTY_PREDICATE_REDISTRIBUTOR_REGIONS_LENGTH,
                    0, false);
                node.redistributor_regions = load_be32(value);
                node.has_redistributor_regions = 1;
            } else if (property_name(strings, stringsSize, nameOffset, "redistributor-stride")) {
                if (length != 4) return parser_property_format_fail(platform, stack, depth,
                    strings, stringsSize, property,
                    GXOS_AARCH64_DTB_PROPERTY_PREDICATE_REDISTRIBUTOR_STRIDE_LENGTH,
                    0, false);
                node.redistributor_stride = load_be32(value);
                node.has_redistributor_stride = 1;
            } else if (property_name(strings, stringsSize, nameOffset, "interrupt-controller") && length == 0) {
                node.is_interrupt_controller = 1;
            } else if (property_name(strings, stringsSize, nameOffset, "status")) {
                if (length == 0 || value[length - 1] != 0) {
                    return parser_property_format_fail(platform, stack, depth, strings,
                        stringsSize, property,
                        GXOS_AARCH64_DTB_PROPERTY_PREDICATE_STATUS_STRING, 0, false);
                }
                node.available = property_string(value, length, "okay") ||
                                 property_string(value, length, "ok");
            } else if (property_name(strings, stringsSize, nameOffset, "ranges")) {
                node.ranges_value = value;
                node.ranges_length = length;
                node.has_ranges = 1;
            }
            offset = propertyNext;
        } else if (token == kFdtNop) {
            continue;
        } else if (token == kFdtEnd) {
            platform->parser_stage = GXOS_AARCH64_DTB_PARSE_STAGE_PLATFORM;
            if (depth != 0) return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_STRUCTURE_END);
            if (!platform->gic_node_seen || platform->gic_version == GXOS_AARCH64_GIC_UNSUPPORTED) {
                platform->diagnostic = GXOS_AARCH64_DTB_UNSUPPORTED_CONTROLLER;
                return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_CONTROLLER);
            }
            if (!platform->gic_node_available) {
                platform->diagnostic = GXOS_AARCH64_DTB_DEVICE_UNAVAILABLE;
                return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_CONTROLLER);
            }
            if (platform->gicd_base == 0 ||
                platform->gicd_size < (platform->gic_version == GXOS_AARCH64_GIC_V3
                    ? GXOS_AARCH64_GICV3_MIN_DISTRIBUTOR_SIZE : UINT64_C(0x1000)) ||
                (platform->gicd_base & UINT64_C(0xfff)) != 0 ||
                (platform->gicd_size & UINT64_C(0xfff)) != 0) {
                platform->diagnostic = GXOS_AARCH64_DTB_INVALID_CONTROLLER_LAYOUT;
                return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_CONTROLLER);
            }
            if (platform->gic_version == GXOS_AARCH64_GIC_V2) {
                if (platform->gicc_base == 0 || platform->gicc_size < UINT64_C(0x1000) ||
                    (platform->gicc_base & UINT64_C(0xfff)) != 0 ||
                    (platform->gicc_size & UINT64_C(0xfff)) != 0) {
                    platform->diagnostic = GXOS_AARCH64_DTB_INVALID_CONTROLLER_LAYOUT;
                    return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_CONTROLLER);
                }
            } else if (platform->gic_version == GXOS_AARCH64_GIC_V3) {
                if (platform->gic_redistributor_region_count == 0 ||
                    platform->gic_redistributor_region_count > GXOS_AARCH64_PHASE2_MAX_GICR_REGIONS ||
                    platform->gic_interrupt_cells != 3 ||
                    (platform->gic_has_redistributor_regions &&
                     platform->gic_redistributor_region_count == 0) ||
                    (platform->gic_has_redistributor_stride &&
                     platform->gic_redistributor_stride == 0) ||
                    (platform->gic_redistributor_stride != 0 &&
                     (platform->gic_redistributor_stride < UINT32_C(0x20000) ||
                      (platform->gic_redistributor_stride & UINT32_C(0xffff)) != 0))) {
                    platform->diagnostic = GXOS_AARCH64_DTB_INVALID_CONTROLLER_LAYOUT;
                    return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_CONTROLLER);
                }
                uint64_t distributor_end = 0;
                if (!gxos_aarch64_add_u64(platform->gicd_base, platform->gicd_size,
                                          &distributor_end)) {
                    platform->diagnostic = GXOS_AARCH64_DTB_INVALID_CONTROLLER_LAYOUT;
                    return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_CONTROLLER);
                }
                const gxos_aarch64_validation_range distributor = {
                    platform->gicd_base, distributor_end};
                for (uint32_t i = 0; i < platform->ram_count; ++i) {
                    if (ranges_overlap_checked(platform->gicd_base, platform->gicd_size,
                                               platform->ram[i].base, platform->ram[i].size)) {
                        platform->diagnostic = GXOS_AARCH64_DTB_INVALID_CONTROLLER_LAYOUT;
                        return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_CONTROLLER);
                    }
                }
                if (ranges_overlap_checked(platform->gicd_base, platform->gicd_size,
                                           platform->uart_base, platform->uart_size)) {
                    platform->diagnostic = GXOS_AARCH64_DTB_INVALID_CONTROLLER_LAYOUT;
                    return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_CONTROLLER);
                }
                for (uint32_t i = 0; i < platform->virtio_mmio_count; ++i) {
                    if (ranges_overlap_checked(platform->gicd_base, platform->gicd_size,
                                               platform->virtio_mmio[i].base,
                                               platform->virtio_mmio[i].size)) {
                        platform->diagnostic = GXOS_AARCH64_DTB_INVALID_CONTROLLER_LAYOUT;
                        return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_CONTROLLER);
                    }
                }
                for (uint32_t i = 0; i < platform->gic_redistributor_region_count; ++i) {
                    const gxos_aarch64_gicv3_region& region =
                        platform->gic_redistributor_regions[i];
                    uint64_t region_end = 0;
                    if (region.base == 0 || region.size < GXOS_AARCH64_GICV3_MIN_REDISTRIBUTOR_SIZE ||
                        (region.base & UINT64_C(0xffff)) != 0 ||
                        (region.size & UINT64_C(0xffff)) != 0 ||
                        !gxos_aarch64_add_u64(region.base, region.size, &region_end)) {
                        platform->diagnostic = GXOS_AARCH64_DTB_INVALID_CONTROLLER_LAYOUT;
                        return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_CONTROLLER);
                    }
                    const gxos_aarch64_validation_range current = {region.base, region_end};
                    if (gxos_aarch64_range_overlaps(current, distributor)) {
                        platform->diagnostic = GXOS_AARCH64_DTB_INVALID_CONTROLLER_LAYOUT;
                        return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_CONTROLLER);
                    }
                    for (uint32_t ram = 0; ram < platform->ram_count; ++ram) {
                        if (ranges_overlap_checked(region.base, region.size,
                                                   platform->ram[ram].base,
                                                   platform->ram[ram].size)) {
                            platform->diagnostic = GXOS_AARCH64_DTB_INVALID_CONTROLLER_LAYOUT;
                            return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_CONTROLLER);
                        }
                    }
                    if (ranges_overlap_checked(region.base, region.size,
                                               platform->uart_base, platform->uart_size)) {
                        platform->diagnostic = GXOS_AARCH64_DTB_INVALID_CONTROLLER_LAYOUT;
                        return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_CONTROLLER);
                    }
                    for (uint32_t device = 0; device < platform->virtio_mmio_count; ++device) {
                        if (ranges_overlap_checked(region.base, region.size,
                                                   platform->virtio_mmio[device].base,
                                                   platform->virtio_mmio[device].size)) {
                            platform->diagnostic = GXOS_AARCH64_DTB_INVALID_CONTROLLER_LAYOUT;
                            return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_CONTROLLER);
                        }
                    }
                    for (uint32_t j = 0; j < i; ++j) {
                        const gxos_aarch64_gicv3_region& prior =
                            platform->gic_redistributor_regions[j];
                        uint64_t prior_end = 0;
                        if (!gxos_aarch64_add_u64(prior.base, prior.size, &prior_end) ||
                            gxos_aarch64_range_overlaps(current, {prior.base, prior_end})) {
                            platform->diagnostic = GXOS_AARCH64_DTB_INVALID_CONTROLLER_LAYOUT;
                            return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_CONTROLLER);
                        }
                    }
                }
            } else {
                platform->diagnostic = GXOS_AARCH64_DTB_UNSUPPORTED_CONTROLLER;
                return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_CONTROLLER);
            }
            if (platform->timer_irq == 0) {
                platform->diagnostic = GXOS_AARCH64_DTB_TIMER_UNAVAILABLE;
                return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_TIMER);
            }
            platform->valid = (platform->platform_kind != GXOS_AARCH64_PLATFORM_UNKNOWN &&
                               platform->ram_count != 0 && platform->uart_base != 0 &&
                               platform->uart_kind == GXOS_AARCH64_UART_PL011 &&
                               platform->gicd_base != 0);
            if (!platform->valid) {
                platform->diagnostic = GXOS_AARCH64_DTB_MALFORMED;
                return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_REQUIRED_PLATFORM);
            }
            platform->diagnostic = GXOS_AARCH64_DTB_OK;
            platform->parser_stage = GXOS_AARCH64_DTB_PARSE_STAGE_COMPLETE;
            trace.complete = true;
            (void)total;
            (void)structEnd;
            return 1;
        } else {
            return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_UNKNOWN_TOKEN);
        }
    }
    platform->parser_stage = GXOS_AARCH64_DTB_PARSE_STAGE_PLATFORM;
    return parser_fail(platform, GXOS_AARCH64_DTB_PARSE_ERROR_STRUCTURE_END);
}

extern "C" gxos_aarch64_dtb_status gxos_aarch64_phase2_parse_dtb_ex(
    const void* blob, uint64_t blob_size, gxos_aarch64_phase2_platform* platform)
{
    if (!platform) return GXOS_AARCH64_DTB_INVALID_ARGUMENT;
    if (gxos_aarch64_phase2_parse_dtb(blob, blob_size, platform)) {
        return GXOS_AARCH64_DTB_OK;
    }
    return static_cast<gxos_aarch64_dtb_status>(platform->diagnostic);
}
