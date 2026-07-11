// Kernel MMIO Mapping Diagnostics and Runtime Front Door
//
// Conservative helpers for reasoning about whether a physical MMIO range can
// be mapped safely at runtime.
//
// The active x86_64 runtime path uses a reserved high virtual MMIO window in
// QEMU diagnostic builds only.  The helpers here centralize the safety checks,
// window geometry, and the precise blocker text used by the QEMU-only
// virtio-gpu probe.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace mmio {

static const uint64_t PAGE_SIZE_BYTES = 0x1000ULL;
static const uint64_t SAFE_DIRECT_MAP_CEILING = 0x100000000ULL; // Legacy diagnostic ceiling retained for compatibility.
// Reserved kernel MMIO window for the x86_64 QEMU probe build.
// Base: 0xFFFFC00000000000
// Size: 16 MiB
// Allocation model: bounded bump allocator with conservative conflict checks.
// Current limitation: mapped slots are retained for the duration of the probe
// and are not recycled yet.
static const uint64_t MMIO_WINDOW_BASE = 0xFFFFC00000000000ULL;
static const uint64_t MMIO_WINDOW_SIZE = 0x01000000ULL; // 16 MiB temporary MMIO window.
static const uint64_t MMIO_WINDOW_LIMIT = MMIO_WINDOW_BASE + MMIO_WINDOW_SIZE;
static const uint64_t MMIO_WINDOW_PAGE_COUNT = MMIO_WINDOW_SIZE / PAGE_SIZE_BYTES;

enum MapFlags : uint32_t {
    MAP_FLAG_NONE            = 0u,
    MAP_FLAG_NON_USER        = 1u << 0,
    MAP_FLAG_NO_EXEC         = 1u << 1,
    MAP_FLAG_UNCACHED        = 1u << 2,
    MAP_FLAG_WRITE_THROUGH   = 1u << 3,
    MAP_FLAG_WRITE_COMBINING = 1u << 4,
};

struct MappingReport {
    uint64_t physicalBase;
    uint64_t length;
    uint64_t alignedBase;
    uint64_t alignedLength;
    uint64_t mappedLength;
    uint64_t pageCount; // Number of 4K pages covered after alignment
    uint64_t safeDirectMapCeiling;
    uint64_t kernelVirtualBase;
    uint64_t mappedVirtual;
    uint64_t pageOffset;
    uint32_t flags;
    bool pageAligned;
    bool withinSafeDirectMap;
    bool requiresPageRounding;
    bool requiresNewPageTableEntries;
    bool cacheAttributesRequested;
    bool cacheAttributesSupported;
    bool success;
    const char* cacheMode;
    const char* reason;
    const char* nextKernelFeature;
};

inline uint64_t align_down(uint64_t value)
{
    return value & ~(PAGE_SIZE_BYTES - 1ULL);
}

inline uint64_t align_up(uint64_t value)
{
    return (value + PAGE_SIZE_BYTES - 1ULL) & ~(PAGE_SIZE_BYTES - 1ULL);
}

inline bool range_overflows(uint64_t physicalBase, uint64_t length)
{
    return (length != 0ULL) && (physicalBase > (~0ULL - length));
}

inline MappingReport describeRange(uint64_t physicalBase, uint64_t length, uint32_t flags = MAP_FLAG_NONE)
{
    MappingReport report{};
    report.physicalBase = physicalBase;
    report.length = length;
    report.alignedBase = align_down(physicalBase);
    report.alignedLength = 0;
    report.mappedLength = 0;
    report.pageCount = 0;
    report.safeDirectMapCeiling = MMIO_WINDOW_LIMIT;
    report.kernelVirtualBase = 0;
    report.mappedVirtual = 0;
    report.pageOffset = physicalBase - report.alignedBase;
    report.flags = flags;
    report.pageAligned = ((physicalBase & (PAGE_SIZE_BYTES - 1ULL)) == 0) &&
                         ((length & (PAGE_SIZE_BYTES - 1ULL)) == 0);
    report.requiresPageRounding = !report.pageAligned;
    report.cacheMode = "cache-mode-unset";
    report.cacheAttributesRequested =
        (flags & (MAP_FLAG_UNCACHED | MAP_FLAG_WRITE_THROUGH | MAP_FLAG_WRITE_COMBINING)) != 0u;
    report.cacheAttributesSupported =
#if defined(__x86_64__) || defined(_M_X64)
        (flags & MAP_FLAG_UNCACHED) != 0u &&
        (flags & (MAP_FLAG_WRITE_THROUGH | MAP_FLAG_WRITE_COMBINING)) == 0u;
#else
        false;
#endif
    report.success = false;

    const bool rangeOverflow = range_overflows(physicalBase, length);
    if (!rangeOverflow && length != 0) {
        const uint64_t alignedEnd = align_up(physicalBase + length);
        report.alignedLength = alignedEnd - report.alignedBase;
        report.mappedLength = report.alignedLength;
        report.pageCount = report.alignedLength / PAGE_SIZE_BYTES;
        report.withinSafeDirectMap = ((flags & (MAP_FLAG_NON_USER | MAP_FLAG_NO_EXEC)) ==
                                      (MAP_FLAG_NON_USER | MAP_FLAG_NO_EXEC)) &&
                                     report.cacheAttributesSupported;
    } else {
        report.withinSafeDirectMap = false;
    }
    report.requiresNewPageTableEntries = report.withinSafeDirectMap && report.pageCount != 0;
    if (report.cacheAttributesSupported) {
        report.cacheMode = "uc(pcd+pwt)";
    } else if ((flags & MAP_FLAG_UNCACHED) != 0u) {
        report.cacheMode = "uc-unsupported";
    } else if ((flags & MAP_FLAG_WRITE_THROUGH) != 0u) {
        report.cacheMode = "wt-requested";
    } else if ((flags & MAP_FLAG_WRITE_COMBINING) != 0u) {
        report.cacheMode = "wc-requested";
    }

    if (length == 0) {
        report.reason = "MMIO range length is zero";
        report.nextKernelFeature = "length validation";
    } else if (rangeOverflow) {
        report.reason = "MMIO range overflows address space";
        report.nextKernelFeature = "overflow-safe MMIO range validation";
    } else if ((flags & MAP_FLAG_NON_USER) == 0u || (flags & MAP_FLAG_NO_EXEC) == 0u) {
        report.reason = "MMIO mappings must be kernel-only, no-executable, and uncached";
        report.nextKernelFeature = "kernel-only UC MMIO page-table flags";
    } else if ((flags & MAP_FLAG_UNCACHED) == 0u) {
        report.reason = "MMIO mappings must request the UC cache mode";
        report.nextKernelFeature = "UC PCD/PWT MMIO page-table flags";
    } else if ((flags & (MAP_FLAG_WRITE_THROUGH | MAP_FLAG_WRITE_COMBINING)) != 0u) {
        report.reason = "requested MMIO cache mode is not supported";
        report.nextKernelFeature = "UC-only MMIO cache policy";
    } else if (!report.withinSafeDirectMap) {
        report.reason = "MMIO range is eligible but not yet window-mapped";
        report.nextKernelFeature = "runtime MMIO page-table mapping";
    } else if (report.requiresPageRounding) {
        report.reason = "range requires page rounding";
        report.nextKernelFeature = "page-rounded MMIO mapping";
    } else if (report.cacheAttributesRequested && !report.cacheAttributesSupported) {
        report.reason = "requested MMIO cache attributes are not supported yet";
        report.nextKernelFeature = "UC cache attribute plumbing";
    } else {
        report.reason = "MMIO range is eligible for the reserved window";
        report.nextKernelFeature = "runtime MMIO mapping helper";
    }

    return report;
}

inline bool canMap(uint64_t physicalBase, uint64_t length,
                   MappingReport* reportOut = nullptr, uint32_t flags = MAP_FLAG_NONE)
{
    const MappingReport report = describeRange(physicalBase, length, flags);
    if (reportOut != nullptr) {
        *reportOut = report;
    }

    // The current branch can only claim feasibility when the range is eligible
    // for the reserved QEMU-only MMIO window and the safe UC cache mode is
    // available.  This is a preflight check only; it does not prove that the
    // runtime window still has free slots.
    const bool hasRequiredSafetyFlags =
        (flags & (MAP_FLAG_NON_USER | MAP_FLAG_NO_EXEC)) == (MAP_FLAG_NON_USER | MAP_FLAG_NO_EXEC);
    return report.withinSafeDirectMap && hasRequiredSafetyFlags && report.cacheAttributesSupported;
}

void set_kernel_physical_base(uint64_t physicalBase);

bool mapForDevice(uint64_t physicalBase, uint64_t length,
                  uint64_t* mappedVirtualOut = nullptr,
                  MappingReport* reportOut = nullptr,
                  uint32_t flags = MAP_FLAG_NONE);

bool unmap(uint64_t mappedVirtual, uint64_t length, const char** reasonOut = nullptr);

} // namespace mmio
} // namespace kernel
