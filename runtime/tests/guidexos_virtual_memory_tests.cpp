#include "runtime/memory/guidexos_virtual_memory_region.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

namespace {

using namespace gxos::runtime::virtual_memory;

bool g_allPassed = true;

void status(const char* name, bool passed) {
    std::cout << name << ": " << (passed ? "PASS" : "FAIL") << "\n";
    if (!passed) g_allPassed = false;
}

bool isZero(const std::uint8_t* bytes, std::size_t size) {
    for (std::size_t index = 0; index < size; ++index) {
        if (bytes[index] != 0) return false;
    }
    return true;
}

bool isPowerOfTwo(std::size_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

} // namespace

int main() {
    const std::size_t page = pageSize();
    bool pageRules = page != 0 && isPowerOfTwo(page) &&
        allocationGranularity() >= page && isPowerOfTwo(allocationGranularity()) &&
        maximumRegionSize() >= page;
    status("Page and granularity", pageRules);

    VirtualMemoryRegion region;
    const VmResult reserved = reserve(page * 4 + 1, page, nullptr, &region);
    const bool basicReserved = reserved == VmResult::Ok && region.base != nullptr &&
        region.reservedSize == page * 5 && region.committedSize == 0;
    status("Reserve", basicReserved);

    VirtualMemoryInfo info{};
    const bool reservationQuery = query(region.base, &info) == VmResult::Ok &&
        info.reserved && !info.committed && info.reservedSize == region.reservedSize &&
        info.pageSize == page;
    status("Query reservation", reservationQuery);

    bool basicLifecycle = false;
    bool zeroAfterRecommit = false;
    if (basicReserved) {
        std::uint8_t* bytes = static_cast<std::uint8_t*>(region.base);
        const VmResult firstCommit = commit(region, 0, page, MemoryProtection::ReadWrite);
        const bool firstZero = firstCommit == VmResult::Ok && isZero(bytes, page);
        for (std::size_t index = 0; index < page; ++index) bytes[index] = 0x5A;
        const bool readWrite = bytes[0] == 0x5A && bytes[page - 1] == 0x5A;
        const VmResult firstDecommit = decommit(region, 0, page);
        const bool decommitted = firstDecommit == VmResult::Ok &&
            query(bytes, &info) == VmResult::Ok && !info.committed;
        const VmResult recommit = commit(region, 0, page, MemoryProtection::ReadWrite);
        zeroAfterRecommit = recommit == VmResult::Ok && isZero(bytes, page);
        basicLifecycle = firstZero && readWrite && decommitted && zeroAfterRecommit;
    }
    status("Commit", basicReserved && basicLifecycle);
    status("Zero initialization", basicReserved &&
        query(region.base, &info) == VmResult::Ok && info.committed);
    status("Read/write access", basicReserved && basicLifecycle);
    status("Decommit", basicReserved && zeroAfterRecommit);
    status("Recommit zeroing", basicReserved && zeroAfterRecommit);

    bool partial = false;
    if (basicReserved) {
        std::uint8_t* bytes = static_cast<std::uint8_t*>(region.base);
        const bool committed =
            commit(region, page, page, MemoryProtection::ReadWrite) == VmResult::Ok &&
            commit(region, page * 3, page, MemoryProtection::ReadWrite) == VmResult::Ok;
        if (committed) {
            bytes[page] = 0x11;
            bytes[page * 3] = 0x33;
        }
        VirtualMemoryInfo first{};
        VirtualMemoryInfo second{};
        const bool queried = query(bytes + page, &first) == VmResult::Ok &&
            query(bytes + page * 3, &second) == VmResult::Ok &&
            first.committed && second.committed;
        const bool removed = decommit(region, page, page) == VmResult::Ok &&
            query(bytes + page, &first) == VmResult::Ok && !first.committed &&
            bytes[page * 3] == 0x33;
        const bool recommitted = commit(region, page, page, MemoryProtection::ReadWrite) == VmResult::Ok &&
            isZero(bytes + page, page);
        partial = committed && queried && removed && recommitted;
    }
    status("Partial commit", partial);
    status("Partial decommit", partial);

    bool protection = false;
    if (basicReserved) {
        const std::size_t offset = page * 3;
        const bool readOnly = protect(region, offset, page, MemoryProtection::ReadOnly) == VmResult::Ok &&
            query(static_cast<std::uint8_t*>(region.base) + offset, &info) == VmResult::Ok &&
            info.protection == MemoryProtection::ReadOnly;
        const bool restored = protect(region, offset, page, MemoryProtection::ReadWrite) == VmResult::Ok;
        const bool noAccess = protect(region, offset, page, MemoryProtection::NoAccess) == VmResult::Ok &&
            query(static_cast<std::uint8_t*>(region.base) + offset, &info) == VmResult::Ok &&
            info.protection == MemoryProtection::NoAccess;
        const bool restoredAgain = protect(region, offset, page, MemoryProtection::ReadWrite) == VmResult::Ok;
        protection = readOnly && restored && noAccess && restoredAgain;
    }
    status("Protection transitions", protection);
    status("Protection enforcement", protectionIsEnforced());

    bool validation = true;
    VirtualMemoryRegion invalidRegion;
    validation = validation && reserve(0, page, nullptr, &invalidRegion) == VmResult::InvalidArgument;
    validation = validation && reserve(page, page / 2, nullptr, &invalidRegion) == VmResult::AlignmentError;
    validation = validation && reserve(std::numeric_limits<std::size_t>::max(), page,
                                       nullptr, &invalidRegion) == VmResult::RangeOverflow;
    if (basicReserved) {
        const VmResult outsideCommit = commit(region, region.reservedSize, page,
                                               MemoryProtection::ReadWrite);
        const VmResult outsideDecommit = decommit(region, region.reservedSize, page);
        const VmResult outsideProtect = protect(region, region.reservedSize, page,
                                                MemoryProtection::ReadWrite);
        const VmResult uncommittedProtect = protect(region, page * 2, page,
                                                    MemoryProtection::ReadWrite);
        const VmResult unalignedCommit = commit(region, 1, page, MemoryProtection::ReadWrite);
        const VmResult duplicateReserve = reserve(page, page, nullptr, &region);
        const VmResult unalignedPreferred = reserve(page, page,
                                                    static_cast<std::uint8_t*>(region.base) + 1,
                                                    &invalidRegion);
        validation = validation && outsideCommit == VmResult::OutOfRange;
        validation = validation && outsideDecommit == VmResult::OutOfRange;
        validation = validation && outsideProtect == VmResult::OutOfRange;
        validation = validation && uncommittedProtect == VmResult::NotCommitted;
        validation = validation && unalignedCommit == VmResult::AlignmentError;
        validation = validation && duplicateReserve == VmResult::AlreadyReserved;
        validation = validation && unalignedPreferred == VmResult::AlignmentError;
    }
    status("Alignment and validation", validation);

    bool overlap = false;
    if (basicReserved) {
        VirtualMemoryRegion overlapping;
        const VmResult result = reserve(page, page, region.base, &overlapping);
        overlap = result == VmResult::AddressUnavailable;
        if (overlapping.base != nullptr) (void)release(overlapping);
    }
    status("Overlap rejection", overlap);

    const void* oldBase = region.base;
    const VmResult released = release(region);
    const bool releaseOk = released == VmResult::Ok && region.base == nullptr &&
        region.reservedSize == 0 && region.committedSize == 0;
    status("Release", releaseOk);
    status("Double release", release(region) == VmResult::AlreadyReleased);
    status("Stale region usage", commit(region, 0, page, MemoryProtection::ReadWrite) == VmResult::AlreadyReleased);
    status("Query after release", query(oldBase, &info) == VmResult::NotFound);

    VirtualMemoryRegion reused;
    const bool rangeReuse = releaseOk &&
        reserve(page, page, const_cast<void*>(oldBase), &reused) == VmResult::Ok &&
        reused.base == oldBase && release(reused) == VmResult::Ok;
    status("Range reuse", rangeReuse);

    const VirtualMemoryStats beforeCycles = stats();
    bool cycles = true;
    for (std::uint32_t iteration = 0; iteration < 32; ++iteration) {
        VirtualMemoryRegion cycle;
        cycles = cycles && reserve(page * 2, page, nullptr, &cycle) == VmResult::Ok;
        cycles = cycles && commit(cycle, 0, page, MemoryProtection::ReadWrite) == VmResult::Ok;
        if (cycles) static_cast<std::uint8_t*>(cycle.base)[0] = static_cast<std::uint8_t>(iteration);
        cycles = cycles && decommit(cycle, 0, page) == VmResult::Ok;
        cycles = cycles && release(cycle) == VmResult::Ok;
    }
    const VirtualMemoryStats afterCycles = stats();
    cycles = cycles && afterCycles.activeRegions == beforeCycles.activeRegions &&
        afterCycles.committedPages == beforeCycles.committedPages;
    status("Repeated cleanup cycles", cycles);
    status("No leaked reservation or committed metadata", afterCycles.activeRegions == 0 &&
        afterCycles.committedPages == 0);

    if (!g_allPassed) {
        std::cerr << "VM diagnostic: " << lastDiagnostic() << "\n";
    }
    return g_allPassed ? 0 : 1;
}
