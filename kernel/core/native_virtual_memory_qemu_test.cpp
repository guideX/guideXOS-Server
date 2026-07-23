#include "include/kernel/native_virtual_memory_qemu_test.h"

#if defined(GXOS_NATIVE_VIRTUAL_MEMORY_QEMU_TEST)

#include "include/kernel/address_space.h"
#include "include/kernel/interrupts.h"
#include "include/kernel/serial_debug.h"
#include "runtime/memory/guidexos_virtual_memory_region.h"
#include "../../tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_virtual_memory_adapter.h"

namespace kernel {
namespace native_virtual_memory_qemu_test {
namespace {

using gxos::runtime::virtual_memory::MemoryProtection;
using gxos::runtime::virtual_memory::VmResult;
using gxos::runtime::virtual_memory::VirtualMemoryInfo;
using gxos::runtime::virtual_memory::VirtualMemoryRegion;
using gxos::runtime::virtual_memory::VirtualMemoryStats;
using vm_size = gxos_vm_size;

bool g_hasFailure = false;

void status(const char* name, bool passed) {
    kernel::serial::puts("[native-virtual-memory-test] ");
    kernel::serial::puts(name);
    kernel::serial::puts(passed ? ": PASS\n" : ": FAIL\n");
    if (!passed) g_hasFailure = true;
}

void metric(const char* name, vm_size value) {
    kernel::serial::puts("[native-virtual-memory-test] ");
    kernel::serial::puts(name);
    kernel::serial::puts(": ");
    kernel::serial::put_hex64(static_cast<uint64_t>(value));
    kernel::serial::puts("\n");
}

bool zero(const volatile uint8_t* address, vm_size size) {
    for (vm_size index = 0; index < size; ++index) {
        if (address[index] != 0) return false;
    }
    return true;
}

// These helpers contain one deliberately faulting instruction. The expected
// page-fault hook advances over that instruction and returns to the helper's
// caller, so an incorrect fault still follows the normal fatal path.
extern "C" void gxos_expected_read(volatile uint8_t* address);
extern "C" void gxos_expected_write(volatile uint8_t* address);

#if defined(__MINGW32__) || defined(__MINGW64__) || defined(_WIN64)
asm(
    ".global gxos_expected_read\n"
    "gxos_expected_read:\n"
    "    movb (%rcx), %al\n"
    "    ret\n"
    ".global gxos_expected_write\n"
    "gxos_expected_write:\n"
    "    movb $0xA5, (%rcx)\n"
    "    ret\n"
);
constexpr uint64_t kReadInstructionLength = 2;
constexpr uint64_t kWriteInstructionLength = 3;
#else
asm(
    ".global gxos_expected_read\n"
    "gxos_expected_read:\n"
    "    movb (%rdi), %al\n"
    "    ret\n"
    ".global gxos_expected_write\n"
    "gxos_expected_write:\n"
    "    movb $0xA5, (%rdi)\n"
    "    ret\n"
);
constexpr uint64_t kReadInstructionLength = 2;
constexpr uint64_t kWriteInstructionLength = 3;
#endif

struct ExpectedFault {
    uintptr_t address;
    bool write;
    bool protection;
    uint64_t instructionLength;
    bool active;
    bool passed;
};

ExpectedFault g_expectedFault{};

bool expectedFaultHandler(uint64_t faultAddress, uint64_t errorCode,
                          uint64_t faultRip, uint64_t* resumeRip) {
    const bool isWrite = (errorCode & 0x2) != 0;
    const bool isProtection = (errorCode & 0x1) != 0;
    if (!g_expectedFault.active ||
        faultAddress != g_expectedFault.address ||
        isWrite != g_expectedFault.write ||
        isProtection != g_expectedFault.protection ||
        resumeRip == nullptr) {
        return false;
    }
    g_expectedFault.passed = true;
    *resumeRip = faultRip + g_expectedFault.instructionLength;
    return true;
}

bool expectedFault(const char* name, volatile uint8_t* address, bool write,
                   bool protection) {
    g_expectedFault = ExpectedFault{
        reinterpret_cast<uintptr_t>(address), write, protection,
        write ? kWriteInstructionLength : kReadInstructionLength, true, false};
    kernel::interrupts::set_expected_page_fault_handler(expectedFaultHandler);
    if (write) gxos_expected_write(address);
    else gxos_expected_read(address);
    kernel::interrupts::clear_expected_page_fault_handler();
    g_expectedFault.active = false;
    status(name, g_expectedFault.passed);
    return g_expectedFault.passed;
}

bool reserveAt(VirtualMemoryRegion* region, vm_size size, void* base,
               VmResult expected) {
    return gxos::runtime::virtual_memory::reserve(size, gxos::runtime::virtual_memory::pageSize(),
                                                   base, region) == expected;
}

void runNativeAotAdapterProbe() {
    using namespace guidexos::nativeaot::virtual_memory;

    const vm_size page = gxos::runtime::virtual_memory::pageSize();
    status("Adapter initialization",
           initializeVirtualMemoryAdapter() == VmResult::Ok);
    const VirtualMemoryStats beforeReserve = gxos::runtime::virtual_memory::stats();
    void* base = nullptr;
    const VmResult reserved = reserveVirtualMemoryRaw(
        page * 4, page, 0, kNumaNodeUndefined, &base);
    const VirtualMemoryStats afterReserve = gxos::runtime::virtual_memory::stats();
    status("PAL reserve", reserved == VmResult::Ok && base != nullptr);
    metric("Adapter reserve data-frame delta",
           afterReserve.regionOwnedFrames - beforeReserve.regionOwnedFrames);
    status("Reserve data-frame delta",
           reserved == VmResult::Ok &&
           afterReserve.regionOwnedFrames == beforeReserve.regionOwnedFrames);

    ReservationDiagnostics oldDiagnostics{};
    const bool diagnosed = queryReservationDiagnostics(base, &oldDiagnostics);
    status("Registry ownership record", diagnosed && oldDiagnostics.active);
    status("Query/page size", getPageSize() == page && getAllocationGranularity() >= page);

    const VirtualMemoryStats beforeCommit = gxos::runtime::virtual_memory::stats();
    const bool committed = reserved == VmResult::Ok &&
        commitVirtualMemoryRaw(base, page) &&
        commitVirtualMemoryRaw(static_cast<uint8_t*>(base) + page * 2, page);
    const VirtualMemoryStats afterCommit = gxos::runtime::virtual_memory::stats();
    status("PAL commit", committed);
    status("Partial commit", committed);
    metric("Adapter commit frame delta",
           afterCommit.regionOwnedFrames - beforeCommit.regionOwnedFrames);
    status("Commit frame delta", committed &&
           afterCommit.regionOwnedFrames >= beforeCommit.regionOwnedFrames);
    bool zeroInitialized = true;
    if (committed) {
        uint8_t* bytes = static_cast<uint8_t*>(base);
        for (vm_size index = 0; index < page; ++index) {
            zeroInitialized = zeroInitialized && bytes[index] == 0 &&
                bytes[index + page * 2] == 0;
        }
        bytes[0] = 0x2D;
        bytes[page * 2] = 0x6E;
    }
    status("Zero initialization", zeroInitialized);

    const VirtualMemoryStats beforeDecommit = gxos::runtime::virtual_memory::stats();
    const bool decommitted = reserved == VmResult::Ok &&
        decommitVirtualMemoryRaw(base, page);
    VirtualMemoryInfo decommittedInfo{};
    const bool decommitQuery = decommitted &&
        queryVirtualMemoryRaw(base, &decommittedInfo) == VmResult::Ok &&
        decommittedInfo.reserved && !decommittedInfo.committed &&
        !decommittedInfo.mappingPresent;
    const VirtualMemoryStats afterDecommit = gxos::runtime::virtual_memory::stats();
    status("PAL decommit", decommitted);
    status("Decommit query", decommitQuery);
    status("Decommit frame recovery", decommitted &&
           afterDecommit.regionOwnedFrames < beforeDecommit.regionOwnedFrames);
    const bool recommitted = decommitted && commitVirtualMemoryRaw(base, page);
    bool recommitZero = recommitted;
    if (recommitted) {
        const uint8_t* bytes = static_cast<const uint8_t*>(base);
        for (vm_size index = 0; index < page; ++index) recommitZero = recommitZero && bytes[index] == 0;
    }
    status("Recommit zeroing", recommitZero);

    const bool protectedPage = protectVirtualMemoryRaw(
        static_cast<uint8_t*>(base) + page * 2, page,
        MemoryProtection::ReadOnly);
    VirtualMemoryInfo protectedInfo{};
    const bool protection = protectedPage && queryVirtualMemoryRaw(
        static_cast<uint8_t*>(base) + page * 2, &protectedInfo) == VmResult::Ok &&
        protectedInfo.committed && protectedInfo.mappingPresent &&
        protectedInfo.protection == MemoryProtection::ReadOnly;
    status("Protection mapping", protection);
    status("Protection mapping result", protectedPage);
    status("Protection restore", protectVirtualMemoryRaw(
        static_cast<uint8_t*>(base) + page * 2, page,
        MemoryProtection::ReadWrite));
    status("Reset/discard status", resetVirtualMemoryRaw(base, page, false) ==
           VmResult::Unsupported);
    MemoryStatus memoryStatus{};
    const bool memoryAvailable = getMemoryStatus(0, &memoryStatus);
    status("Memory status", memoryAvailable);
    bool restricted = false;
    nativeaot_vm_uint32 memoryLoad = 0;
    nativeaot_vm_uint64 availablePhysical = 0;
    nativeaot_vm_uint64 availablePageFile = 0;
    gcGetMemoryStatus(0, &memoryLoad, &availablePhysical, &availablePageFile);
    status("GC memory-status wrappers",
           memoryAvailable && gcGetVirtualMemoryLimit() != 0 &&
           gcGetPageSize() == page &&
           gcGetPhysicalMemoryLimit(&restricted) == memoryStatus.physicalLimit &&
           !restricted && memoryLoad <= 100 &&
           availablePhysical <= memoryStatus.physicalLimit &&
           availablePageFile <= memoryStatus.physicalLimit);

    const bool crossRange = !commitVirtualMemoryRaw(base, page * 5);
    status("Failure rollback", crossRange);
    const void* oldBase = base;
    const bool released = releaseVirtualMemoryRaw(base, page * 4);
    status("PAL release", released);
    status("Release", released);
    status("Mapping leak check", released &&
           gxos::runtime::virtual_memory::stats().mappingCount ==
               beforeReserve.mappingCount);
    status("Frame leak check", released &&
           gxos::runtime::virtual_memory::stats().regionOwnedFrames ==
               beforeReserve.regionOwnedFrames);

    void* reused = nullptr;
    const bool preferredReuse = reserveVirtualMemoryAt(
        page * 4, page, const_cast<void*>(oldBase), 0,
        kNumaNodeUndefined, &reused) == VmResult::Ok && reused == oldBase;
    ReservationDiagnostics newDiagnostics{};
    const bool registryReuse = preferredReuse &&
        queryReservationDiagnostics(reused, &newDiagnostics) &&
        newDiagnostics.reservationGeneration != oldDiagnostics.reservationGeneration;
    status("Preferred-address behavior", preferredReuse);
    status("Range reuse", preferredReuse && releaseVirtualMemoryRaw(reused, page * 4));
    status("Registry reuse", registryReuse);
    status("Stale record rejection", !reservationIdentityIsActive(
        const_cast<void*>(oldBase), oldDiagnostics.reservationGeneration));

    status("Adapter shutdown", shutdownVirtualMemoryAdapter() == VmResult::Ok);
    status("Registry leak check", registryStats().liveReservations == 0);
}

} // namespace

void run() {
    kernel::serial::puts("[native-virtual-memory-test] BEGIN\n");
    runNativeAotAdapterProbe();
    const vm_size page = gxos::runtime::virtual_memory::pageSize();
    const VirtualMemoryStats initial = gxos::runtime::virtual_memory::stats();
    status("Initial metadata", kernel::memory::address_space::isInitialized() &&
        initial.activeRegions == 0 && initial.committedPages == 0 &&
        initial.regionOwnedFrames == 0);

    VirtualMemoryRegion region;
    const VirtualMemoryStats beforeReservation = gxos::runtime::virtual_memory::stats();
    const VmResult reserved = gxos::runtime::virtual_memory::reserve(
        page * 4, page, nullptr, &region);
    const VirtualMemoryStats afterReservation = gxos::runtime::virtual_memory::stats();
    VirtualMemoryInfo info{};
    const bool reservationQuery = reserved == VmResult::Ok &&
        gxos::runtime::virtual_memory::query(region.base, &info) == VmResult::Ok &&
        info.reserved && !info.committed && !info.mappingPresent &&
        info.physicalFrame == 0;
    const bool trueUnbacked = reserved == VmResult::Ok && reservationQuery &&
        afterReservation.regionOwnedFrames == beforeReservation.regionOwnedFrames &&
        afterReservation.freeFrames == beforeReservation.freeFrames &&
        afterReservation.pageTableFrames == beforeReservation.pageTableFrames &&
        afterReservation.mappingCount == beforeReservation.mappingCount;
    status("True unbacked reservation", trueUnbacked);
    metric("Reservation data-frame delta",
           afterReservation.regionOwnedFrames - beforeReservation.regionOwnedFrames);
    status("Query distinguishes reserved and committed", reservationQuery);

    const void* base = region.base;
    VirtualMemoryRegion exactOverlap;
    VirtualMemoryRegion beginningOverlap;
    VirtualMemoryRegion endingOverlap;
    VirtualMemoryRegion containingOverlap;
    const bool exact = reserved == VmResult::Ok && reserveAt(
        &exactOverlap, page * 4, const_cast<void*>(base), VmResult::AddressUnavailable);
    const bool beginning = reserved == VmResult::Ok && reserveAt(
        &beginningOverlap, page * 2,
        static_cast<uint8_t*>(const_cast<void*>(base)) + page,
        VmResult::AddressUnavailable);
    const bool ending = reserved == VmResult::Ok && reserveAt(
        &endingOverlap, page * 2,
        static_cast<uint8_t*>(const_cast<void*>(base)) + page * 2,
        VmResult::AddressUnavailable);
    const bool containing = reserved == VmResult::Ok && reserveAt(
        &containingOverlap, page * 6, const_cast<void*>(base),
        VmResult::AddressUnavailable);
    status("Preferred-base reservation", reserved == VmResult::Ok && base != nullptr);
    status("Overlap rejection", exact && beginning && ending && containing);

    VirtualMemoryRegion adjacent;
    const bool adjacentResult = reserved == VmResult::Ok &&
        reserveAt(&adjacent, page, static_cast<uint8_t*>(const_cast<void*>(base)) + page * 4,
                  VmResult::Ok);
    status("Adjacent non-overlapping reservation", adjacentResult);
    if (adjacentResult) (void)gxos::runtime::virtual_memory::release(adjacent);

    bool committed = false;
    bool zeroInitialized = false;
    bool directReadWrite = false;
    bool mappingInvariant = false;
    if (reserved == VmResult::Ok) {
        const VirtualMemoryStats beforeCommit = gxos::runtime::virtual_memory::stats();
        uint8_t* bytes = static_cast<uint8_t*>(region.base);
        committed = gxos::runtime::virtual_memory::commit(
            region, 0, page, MemoryProtection::ReadWrite) == VmResult::Ok;
        const VirtualMemoryStats afterCommit = gxos::runtime::virtual_memory::stats();
        VirtualMemoryInfo committedInfo{};
        zeroInitialized = committed && zero(bytes, page) &&
            gxos::runtime::virtual_memory::query(bytes, &committedInfo) == VmResult::Ok &&
            committedInfo.committed && committedInfo.mappingPresent &&
            committedInfo.physicalFrame != 0 &&
            afterCommit.regionOwnedFrames == beforeCommit.regionOwnedFrames + 1 &&
            afterCommit.mappingCount == beforeCommit.mappingCount + 1;
        if (committed) {
            bytes[0] = 0x5A;
            directReadWrite = bytes[0] == 0x5A;
        }
        mappingInvariant = committed && committedInfo.mappingPresent;
        metric("Committed-frame delta", afterCommit.regionOwnedFrames - beforeCommit.regionOwnedFrames);
    }
    status("Partial commit", committed);
    status("Zero initialization", zeroInitialized);
    status("Direct read/write behavior", directReadWrite);
    status("Page-table mapping invariant", mappingInvariant);

    bool partial = false;
    bool decommitReleased = false;
    bool recommitZero = false;
    if (reserved == VmResult::Ok) {
        uint8_t* bytes = static_cast<uint8_t*>(region.base);
        const bool selected =
            gxos::runtime::virtual_memory::commit(region, page, page,
                                                   MemoryProtection::ReadWrite) == VmResult::Ok &&
            gxos::runtime::virtual_memory::commit(region, page * 3, page,
                                                   MemoryProtection::ReadWrite) == VmResult::Ok;
        if (selected) {
            bytes[page] = 0x31;
            bytes[page * 3] = 0x3C;
        }
        VirtualMemoryInfo third{};
        const bool queryThird = selected &&
            gxos::runtime::virtual_memory::query(bytes + page * 3, &third) == VmResult::Ok &&
            third.committed && third.mappingPresent;
        const VirtualMemoryStats beforeDecommit = gxos::runtime::virtual_memory::stats();
        const bool removed = selected &&
            gxos::runtime::virtual_memory::decommit(region, page, page) == VmResult::Ok;
        VirtualMemoryInfo removedInfo{};
        const bool retained = removed &&
            gxos::runtime::virtual_memory::query(bytes + page, &removedInfo) == VmResult::Ok &&
            removedInfo.reserved && !removedInfo.committed &&
            !removedInfo.mappingPresent && bytes[page * 3] == 0x3C;
        const VirtualMemoryStats afterDecommit = gxos::runtime::virtual_memory::stats();
        decommitReleased = removed &&
            afterDecommit.regionOwnedFrames + 1 == beforeDecommit.regionOwnedFrames &&
            afterDecommit.mappingCount + 1 == beforeDecommit.mappingCount;
        partial = selected && queryThird && retained;
        status("Partial decommit", partial);
        status("Reserved range retained after decommit", retained);
        status("Decommit releases physical frame", decommitReleased);

        const bool recommitted = gxos::runtime::virtual_memory::commit(
            region, page, page, MemoryProtection::ReadWrite) == VmResult::Ok;
        recommitZero = recommitted && zero(bytes + page, page);
        status("Recommit zeroing", recommitZero);
    } else {
        status("Partial decommit", false);
        status("Reserved range retained after decommit", false);
        status("Decommit releases physical frame", false);
        status("Recommit zeroing", false);
    }

    bool protection = false;
    if (reserved == VmResult::Ok) {
        uint8_t* bytes = static_cast<uint8_t*>(region.base);
        VirtualMemoryInfo beforeProtection{};
        const bool havePhysical = gxos::runtime::virtual_memory::query(
            bytes, &beforeProtection) == VmResult::Ok && beforeProtection.committed;
        const bool readOnly = gxos::runtime::virtual_memory::protect(
            region, 0, page, MemoryProtection::ReadOnly) == VmResult::Ok &&
            gxos::runtime::virtual_memory::query(bytes, &info) == VmResult::Ok &&
            info.protection == MemoryProtection::ReadOnly && info.mappingPresent &&
            info.physicalFrame == beforeProtection.physicalFrame;
        const bool readOnlyFault = expectedFault(
            "Read-only enforcement", bytes, true, true);
        const bool restored = gxos::runtime::virtual_memory::protect(
            region, 0, page, MemoryProtection::ReadWrite) == VmResult::Ok;
        const bool noAccess = gxos::runtime::virtual_memory::protect(
            region, 0, page, MemoryProtection::NoAccess) == VmResult::Ok &&
            gxos::runtime::virtual_memory::query(bytes, &info) == VmResult::Ok &&
            info.committed && !info.mappingPresent &&
            info.physicalFrame == beforeProtection.physicalFrame;
        const bool noAccessFault = expectedFault(
            "No-access enforcement", bytes, false, false);
        const bool restoredAgain = gxos::runtime::virtual_memory::protect(
            region, 0, page, MemoryProtection::ReadWrite) == VmResult::Ok;
        const bool reservedFault = expectedFault(
            "Reserved-uncommitted fault", bytes + page * 2, false, false);

        const bool decommitForFault = gxos::runtime::virtual_memory::decommit(
            region, page, page) == VmResult::Ok;
        const bool decommittedFault = decommitForFault && expectedFault(
            "Decommitted-page fault", bytes + page, false, false);
        const bool restoreAfterFault = gxos::runtime::virtual_memory::commit(
            region, page, page, MemoryProtection::ReadWrite) == VmResult::Ok &&
            zero(bytes + page, page);
        protection = havePhysical && readOnly && readOnlyFault && restored &&
            noAccess && noAccessFault && restoredAgain && reservedFault &&
            decommittedFault && restoreAfterFault;
    }
    status("Protection transitions", protection);

    // Deterministic physical-frame rollback: only two VM frames are allowed
    // for a four-page commit; all newly mapped pages must be rolled back.
    VirtualMemoryRegion exhaustion;
    const bool exhaustionReserved = gxos::runtime::virtual_memory::reserve(
        page * 4, page, nullptr, &exhaustion) == VmResult::Ok;
    const VirtualMemoryStats beforeExhaustion = gxos::runtime::virtual_memory::stats();
    kernel::memory::address_space::setVmRegionFrameLimitForTests(2);
    const VmResult exhaustionResult = exhaustionReserved
        ? gxos::runtime::virtual_memory::commit(
            exhaustion, 0, page * 4, MemoryProtection::ReadWrite)
        : VmResult::HostFailure;
    kernel::memory::address_space::clearVmRegionFrameLimitForTests();
    VirtualMemoryInfo exhaustionInfo{};
    const VirtualMemoryStats afterExhaustion = gxos::runtime::virtual_memory::stats();
    const bool rollback = exhaustionReserved && exhaustionResult == VmResult::OutOfMemory &&
        exhaustion.committedSize == 0 && afterExhaustion.regionOwnedFrames ==
            beforeExhaustion.regionOwnedFrames &&
        gxos::runtime::virtual_memory::query(exhaustion.base, &exhaustionInfo) == VmResult::Ok &&
        !exhaustionInfo.committed && !exhaustionInfo.mappingPresent;
    status("Physical-frame exhaustion rollback", rollback);
    if (exhaustionReserved) (void)gxos::runtime::virtual_memory::release(exhaustion);

    const void* oldBase = region.base;
    const VirtualMemoryStats beforeRelease = gxos::runtime::virtual_memory::stats();
    const VmResult released = gxos::runtime::virtual_memory::release(region);
    const VirtualMemoryStats afterRelease = gxos::runtime::virtual_memory::stats();
    const bool releaseOk = released == VmResult::Ok && region.base == nullptr &&
        afterRelease.activeRegions == 0 && afterRelease.mappingCount == 0 &&
        afterRelease.regionOwnedFrames == 0 &&
        afterRelease.freeFrames + afterRelease.pageTableFrames ==
            afterRelease.totalKnownFrames;
    status("Release", releaseOk);
    metric("Released-frame delta",
           beforeRelease.regionOwnedFrames - afterRelease.regionOwnedFrames);
    const bool releaseFault = releaseOk && expectedFault(
        "Released-page fault", static_cast<volatile uint8_t*>(const_cast<void*>(oldBase)),
        false, false);
    (void)releaseFault;

    VirtualMemoryRegion reused;
    const bool rangeReuse = releaseOk && gxos::runtime::virtual_memory::reserve(
        page, page, const_cast<void*>(oldBase), &reused) == VmResult::Ok &&
        reused.base == oldBase &&
        gxos::runtime::virtual_memory::release(reused) == VmResult::Ok;
    status("Range reuse", rangeReuse);

    // Force metadata-pool exhaustion without consuming backing frames.
    VirtualMemoryRegion metadataRegions[32];
    bool metadataReserved = true;
    for (vm_size index = 0; index < 32; ++index) {
        metadataReserved = metadataReserved &&
            gxos::runtime::virtual_memory::reserve(page, page, nullptr,
                                                   &metadataRegions[index]) == VmResult::Ok;
    }
    VirtualMemoryRegion metadataOverflow;
    const bool metadataExhausted = metadataReserved &&
        gxos::runtime::virtual_memory::reserve(page, page, nullptr,
                                               &metadataOverflow) == VmResult::OutOfMemory;
    status("Metadata exhaustion", metadataExhausted);
    for (vm_size index = 0; index < 32; ++index) {
        (void)gxos::runtime::virtual_memory::release(metadataRegions[index]);
    }
    VirtualMemoryRegion metadataReuseRegion;
    const bool metadataReuse =
        gxos::runtime::virtual_memory::reserve(page, page, nullptr,
                                               &metadataReuseRegion) == VmResult::Ok &&
        gxos::runtime::virtual_memory::release(metadataReuseRegion) == VmResult::Ok;
    status("Metadata reuse", metadataReuse);

    // Fill the bounded runtime virtual range with max-sized reservations.
    VirtualMemoryRegion rangeRegions[16];
    bool rangeReserved = true;
    for (vm_size index = 0; index < 16; ++index) {
        rangeReserved = rangeReserved &&
            gxos::runtime::virtual_memory::reserve(
                gxos::runtime::virtual_memory::maximumRegionSize(), page, nullptr,
                &rangeRegions[index]) == VmResult::Ok;
    }
    VirtualMemoryRegion rangeOverflow;
    const bool rangeExhausted = rangeReserved &&
        gxos::runtime::virtual_memory::reserve(page, page, nullptr,
                                               &rangeOverflow) == VmResult::AddressUnavailable;
    status("Virtual-range exhaustion", rangeExhausted);
    for (vm_size index = 0; index < 16; ++index) {
        (void)gxos::runtime::virtual_memory::release(rangeRegions[index]);
    }

    // Process/address-space teardown: full, partial, and uncommitted regions
    // all belong to the one explicit current owner in this pass.
    VirtualMemoryRegion ownerFull;
    VirtualMemoryRegion ownerPartial;
    VirtualMemoryRegion ownerUncommitted;
    const bool ownersReserved =
        gxos::runtime::virtual_memory::reserve(page * 2, page, nullptr, &ownerFull) == VmResult::Ok &&
        gxos::runtime::virtual_memory::reserve(page * 2, page, nullptr, &ownerPartial) == VmResult::Ok &&
        gxos::runtime::virtual_memory::reserve(page, page, nullptr, &ownerUncommitted) == VmResult::Ok;
    const bool ownersCommitted = ownersReserved &&
        gxos::runtime::virtual_memory::commit(ownerFull, 0, page,
                                              MemoryProtection::ReadWrite) == VmResult::Ok &&
        gxos::runtime::virtual_memory::commit(ownerPartial, 0, page,
                                              MemoryProtection::ReadWrite) == VmResult::Ok;
    const void* staleOwnerBase = ownerFull.base;
    const VmResult teardown = gxos::runtime::virtual_memory::teardownAddressSpace();
    const VirtualMemoryStats afterTeardown = gxos::runtime::virtual_memory::stats();
    const bool staleHandle = ownersCommitted &&
        gxos::runtime::virtual_memory::commit(ownerFull, 0, page,
            MemoryProtection::ReadWrite) == VmResult::AlreadyReleased;
    const bool teardownOk = ownersCommitted && teardown == VmResult::Ok &&
        afterTeardown.activeRegions == 0 && afterTeardown.mappingCount == 0 &&
        afterTeardown.regionOwnedFrames == 0 &&
        gxos::runtime::virtual_memory::query(staleOwnerBase, &info) == VmResult::NotFound;
    status("Stale-handle rejection", staleHandle);
    status("Process/address-space teardown", teardownOk);

    const bool physicalLeakCheck = afterTeardown.regionOwnedFrames == 0 &&
        afterTeardown.freeFrames + afterTeardown.pageTableFrames ==
            afterTeardown.totalKnownFrames;
    status("Physical-frame leak check", physicalLeakCheck);
    status("Mapping leak check", afterTeardown.mappingCount == 0);
    status("TLB invalidation", afterTeardown.tlbInvalidations != 0);

    if (g_hasFailure) {
        kernel::serial::puts("[native-virtual-memory-test] ALL_FAIL\n");
    } else {
        kernel::serial::puts("[native-virtual-memory-test] ALL_PASS\n");
    }
}

} // namespace native_virtual_memory_qemu_test
} // namespace kernel

#endif
