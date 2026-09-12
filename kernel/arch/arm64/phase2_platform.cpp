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
    uint8_t is_timer;
    uint8_t is_virtio_mmio;
    uint8_t is_raspberry_pi;
    uint8_t is_qemu_virt;
    uint8_t gic_version;
    uint32_t interrupt_cells;
    uint8_t address_cells;
    uint8_t size_cells;
    uint8_t parent_address_cells;
    uint8_t parent_size_cells;
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
        const uint32_t tupleCells = childCells + parentCells + sizeCells;
        if (childCells == 0 || childCells > 2 || parentCells == 0 || parentCells > 2 ||
            sizeCells == 0 || sizeCells > 2 || tupleCells == 0) return false;
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
    uint32_t cells = addressCells + sizeCells;
    bool qemuVirtioRootCellFallback = false;
    if (addressCells == 0 || addressCells > 2 || sizeCells == 0 || sizeCells > 2 ||
        cells > 4 || length == 0 || length % (cells * 4) != 0) {
        // QEMU 11 emits platform-bus virtio-mmio children with root-shaped
        // reg tuples even though platform-bus advertises a different child
        // cell shape.  Keep this exception bounded to those optional QEMU
        // devices; Pi memory/UART/GIC nodes remain strict and translated.
        const uint32_t rootAddressCells = platform->address_cells;
        const uint32_t rootSizeCells = platform->size_cells;
        const uint32_t rootCells = rootAddressCells + rootSizeCells;
        if (!node.is_virtio_mmio || rootAddressCells == 0 || rootAddressCells > 2 ||
            rootSizeCells == 0 || rootSizeCells > 2 || rootCells > 4 ||
            length == 0 || length % (rootCells * 4) != 0) return false;
        addressCells = rootAddressCells;
        sizeCells = rootSizeCells;
        cells = rootCells;
        qemuVirtioRootCellFallback = true;
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
            } else if (entry == 1 && platform->gicc_base == 0) {
                platform->gicc_base = base;
                platform->gicc_size = size;
            }
        }
        if (node.is_virtio_mmio && entry == 0 && !add_virtio_mmio(platform, base, size)) return false;
        ++entry;
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
        node->gic_version = 2;
    }
    if (has_compatible(value, length, "arm,gic-v3") ||
        has_compatible(value, length, "arm,gic-v3.1")) {
        node->is_gic = 1;
        node->gic_version = 3;
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

} // namespace

extern "C" uint8_t gxos_aarch64_phase2_parse_dtb(const void* blob, uint64_t blob_size,
                                                  gxos_aarch64_phase2_platform* platform)
{
    if (!platform) return 0;
    zero_bytes(platform, sizeof(*platform));

    const uint8_t* bytes = (const uint8_t*)blob;
    uint32_t total = 0;
    uint32_t structOffset = 0;
    uint32_t structSize = 0;
    uint32_t stringsOffset = 0;
    uint32_t stringsSize = 0;
    if (!gxos_aarch64_fdt_header_bounds(bytes, blob_size, &total, &structOffset, &structSize,
                                        &stringsOffset, &stringsSize)) return 0;
    const uint8_t* structBase = bytes + structOffset;
    const uint8_t* structEnd = structBase + structSize;
    const uint8_t* strings = bytes + stringsOffset;

    platform->address_cells = 2;
    platform->size_cells = 1;
    FdtNode stack[32];
    zero_bytes(stack, sizeof(stack));
    uint32_t depth = 0;
    uint32_t offset = 0;
    while (offset < structSize) {
        if (structSize - offset < 4) return 0;
        const uint8_t* tokenBytes = structBase + offset;
        const uint32_t token = load_be32(tokenBytes);
        offset += 4;
        if (token == kFdtBeginNode) {
            if (depth >= 32) return 0;
            const uint32_t nameStart = offset;
            while (offset < structSize && structBase[offset] != 0) ++offset;
            if (offset >= structSize) return 0;
            FdtNode node;
            zero_bytes(&node, sizeof(node));
            node.is_root = (depth == 0);
            if (node.is_root) {
                node.address_cells = 2;
                node.size_cells = 1;
            } else {
                const FdtNode& parent = stack[depth - 1];
                node.parent_address_cells = parent.address_cells;
                node.parent_size_cells = parent.size_cells;
                node.address_cells = 2;
                node.size_cells = 1;
            }
            const uint32_t nameLength = offset - nameStart;
            if (node_name_is(structBase + nameStart, nameLength, "memory")) node.is_memory = 1;
            ++offset;
            offset = (offset + 3) & ~3u;
            if (offset > structSize) return 0;
            stack[depth++] = node;
        } else if (token == kFdtEndNode) {
            if (depth == 0) return 0;
            FdtNode& node = stack[depth - 1];
            if (node.reg_value && (node.is_memory || node.is_uart || node.is_gic ||
                                   node.is_virtio_mmio) &&
                !parse_reg(node.reg_value, node.reg_length, node, stack, depth, platform)) return 0;
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
            if (depth == 0 || structSize - offset < 8) return 0;
            const uint32_t length = load_be32(structBase + offset);
            const uint32_t nameOffset = load_be32(structBase + offset + 4);
            offset += 8;
            if (length > structSize - offset) return 0;
            if (!property_name(strings, stringsSize, nameOffset, "compatible") &&
                !property_name(strings, stringsSize, nameOffset, "reg") &&
                !property_name(strings, stringsSize, nameOffset, "interrupts") &&
                !property_name(strings, stringsSize, nameOffset, "#address-cells") &&
                !property_name(strings, stringsSize, nameOffset, "#size-cells") &&
                !property_name(strings, stringsSize, nameOffset, "#interrupt-cells") &&
                !property_name(strings, stringsSize, nameOffset, "device_type") &&
                !property_name(strings, stringsSize, nameOffset, "ranges")) {
                offset = (offset + length + 3) & ~3u;
                if (offset > structSize) return 0;
                continue;
            }
            FdtNode& node = stack[depth - 1];
            const uint8_t* value = structBase + offset;
            if (property_name(strings, stringsSize, nameOffset, "compatible")) {
                classify_compatible(value, length, &node);
            } else if (property_name(strings, stringsSize, nameOffset, "reg")) {
                node.reg_value = value;
                node.reg_length = length;
            } else if (property_name(strings, stringsSize, nameOffset, "interrupts")) {
                node.interrupts_value = value;
                node.interrupts_length = length;
            } else if (property_name(strings, stringsSize, nameOffset, "#address-cells") && length == 4) {
                const uint32_t cells = load_be32(value);
                if (cells == 0 || (node.is_root && cells > 2)) return 0;
                node.address_cells = cells <= 2 ? (uint8_t)cells : 0;
                if (node.is_root) platform->address_cells = (uint8_t)cells;
            } else if (property_name(strings, stringsSize, nameOffset, "#size-cells") && length == 4) {
                const uint32_t cells = load_be32(value);
                if (node.is_root && (cells == 0 || cells > 2)) return 0;
                node.size_cells = cells <= 2 ? (uint8_t)cells : 0;
                if (node.is_root) platform->size_cells = (uint8_t)cells;
            } else if (property_name(strings, stringsSize, nameOffset, "device_type") &&
                       property_string(value, length, "memory")) {
                node.is_memory = 1;
            } else if (property_name(strings, stringsSize, nameOffset, "#interrupt-cells") && length == 4) {
                const uint32_t cells = load_be32(value);
                if (cells == 0 || cells > 4) return 0;
                node.interrupt_cells = cells;
            } else if (property_name(strings, stringsSize, nameOffset, "ranges")) {
                node.ranges_value = value;
                node.ranges_length = length;
                node.has_ranges = 1;
            }
            offset = (offset + length + 3) & ~3u;
            if (offset > structSize) return 0;
        } else if (token == kFdtNop) {
            continue;
        } else if (token == kFdtEnd) {
            if (depth != 0) return 0;
            platform->valid = (platform->platform_kind != GXOS_AARCH64_PLATFORM_UNKNOWN &&
                               platform->ram_count != 0 && platform->uart_base != 0 &&
                               platform->uart_kind == GXOS_AARCH64_UART_PL011 &&
                               platform->gic_version == 2 && platform->gicd_base != 0 &&
                               platform->gicc_base != 0 && platform->timer_irq != 0);
            (void)total;
            (void)structEnd;
            return platform->valid;
        } else {
            return 0;
        }
    }
    return 0;
}
