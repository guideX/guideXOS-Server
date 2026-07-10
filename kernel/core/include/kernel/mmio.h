// Kernel MMIO Mapping Diagnostics
//
// Diagnostic-only helpers for reasoning about whether a physical MMIO range
// fits inside guideXOS's current safe direct-map assumptions.
//
// This header does not install page tables or touch device registers.  It is a
// conservative feasibility layer only.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace mmio {

// The current branch only treats low physical MMIO ranges as directly safe.
// This is a diagnostic ceiling, not proof of actual page-table coverage.
static const uint64_t SAFE_DIRECT_MAP_CEILING = 0x100000000ULL; // 4 GiB
static const uint64_t PAGE_SIZE_BYTES = 0x1000ULL;

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
    uint64_t safeDirectMapCeiling;
    uint32_t flags;
    bool pageAligned;
    bool withinSafeDirectMap;
    bool requiresPageRounding;
    bool requiresNewPageTableEntries;
    bool cacheAttributesRequested;
    bool cacheAttributesSupported;
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

inline MappingReport describeRange(uint64_t physicalBase, uint64_t length, uint32_t flags = MAP_FLAG_NONE)
{
    MappingReport report{};
    report.physicalBase = physicalBase;
    report.length = length;
    report.alignedBase = align_down(physicalBase);
    report.safeDirectMapCeiling = SAFE_DIRECT_MAP_CEILING;
    report.flags = flags;
    report.pageAligned = ((physicalBase & (PAGE_SIZE_BYTES - 1ULL)) == 0) &&
                         ((length & (PAGE_SIZE_BYTES - 1ULL)) == 0);
    report.requiresPageRounding = !report.pageAligned;
    report.cacheAttributesRequested =
        (flags & (MAP_FLAG_UNCACHED | MAP_FLAG_WRITE_THROUGH | MAP_FLAG_WRITE_COMBINING)) != 0u;
    report.cacheAttributesSupported = false; // TODO: PAT/MTRR plumbing

    const bool rangeOverflow = (length != 0) && (physicalBase > (~0ULL - length));
    if (!rangeOverflow && length != 0) {
        const uint64_t alignedEnd = align_up(physicalBase + length);
        report.alignedLength = alignedEnd - report.alignedBase;
        report.withinSafeDirectMap = (report.alignedBase <= SAFE_DIRECT_MAP_CEILING) &&
                                     (report.alignedLength <= SAFE_DIRECT_MAP_CEILING - report.alignedBase);
    } else {
        report.alignedLength = 0;
        report.withinSafeDirectMap = false;
    }
    report.requiresNewPageTableEntries = !report.withinSafeDirectMap;

    if (length == 0) {
        report.reason = "MMIO range length is zero";
        report.nextKernelFeature = "length validation";
    } else if (rangeOverflow) {
        report.reason = "MMIO range overflows address space";
        report.nextKernelFeature = "overflow-safe MMIO range validation";
    } else if (!report.withinSafeDirectMap) {
        report.reason = "outside current safe direct-map ceiling";
        report.nextKernelFeature = "runtime MMIO page-table mapping";
    } else if (report.requiresPageRounding) {
        report.reason = "range requires page rounding";
        report.nextKernelFeature = "page-rounded MMIO mapping";
    } else if (report.cacheAttributesRequested && !report.cacheAttributesSupported) {
        report.reason = "requested MMIO cache attributes are not supported yet";
        report.nextKernelFeature = "PAT/MTRR cache attribute plumbing";
    } else {
        report.reason = "range fits the current safe direct-map policy";
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

    // The current branch can only claim feasibility when the range is inside
    // the conservative ceiling and no unsupported cache attributes were asked
    // for.  Page rounding is still considered feasible.
    return report.withinSafeDirectMap && !report.cacheAttributesRequested;
}

inline bool mapForDevice(uint64_t physicalBase, uint64_t length,
                         uint64_t* mappedVirtualOut = nullptr,
                         MappingReport* reportOut = nullptr,
                         uint32_t flags = MAP_FLAG_NONE)
{
    const MappingReport report = describeRange(physicalBase, length, flags);
    if (reportOut != nullptr) {
        *reportOut = report;
    }

    if (mappedVirtualOut != nullptr) {
        *mappedVirtualOut = 0;
    }

    // TODO: install a guarded MMIO mapping with explicit cache attributes and
    // unmap support once runtime page-table updates are proven safe.
    return false;
}

inline bool unmap(uint64_t /*mappedVirtual*/, uint64_t /*length*/, const char** reasonOut = nullptr)
{
    if (reasonOut != nullptr) {
        *reasonOut = "MMIO unmap is not implemented yet";
    }
    return false;
}

} // namespace mmio
} // namespace kernel
