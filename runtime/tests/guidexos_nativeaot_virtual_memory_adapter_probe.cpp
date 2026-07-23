#include "../../tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_virtual_memory_adapter.h"

#include <cstddef>
#include <cstdint>
#include <iostream>

namespace {

using namespace guidexos::nativeaot::virtual_memory;
using gxos::runtime::virtual_memory::MemoryProtection;
using gxos::runtime::virtual_memory::VmResult;
using gxos::runtime::virtual_memory::VirtualMemoryInfo;
using gxos::runtime::virtual_memory::VirtualMemoryStats;

bool g_passed = true;

void status(const char* label, bool passed) {
    std::cout << label << ": " << (passed ? "PASS" : "FAIL") << "\n";
    g_passed = g_passed && passed;
}

void metric(const char* label, std::int64_t value) {
    std::cout << label << ": " << value << "\n";
}

bool zero(const std::uint8_t* bytes, std::size_t size) {
    for (std::size_t index = 0; index < size; ++index) {
        if (bytes[index] != 0) return false;
    }
    return true;
}

} // namespace

int main() {
    const std::size_t page = getPageSize();
    const std::size_t granularity = getAllocationGranularity();
    status("Page size", page != 0);
    status("Allocation granularity", granularity >= page);
    status("True reservation semantics", trueReservationSemantics());
    std::cout << "Adapter backend mode: " << backendModeName() << "\n";
    status("Large pages disabled", !supportsLargePages() && getLargePageSize() == 0);
    status("NUMA disabled", !supportsNumaPlacement());
    status("Write-watch disabled", !supportsWriteWatch());

    status("Shutdown before initialization",
           shutdownVirtualMemoryAdapter() == VmResult::AlreadyReleased);
    status("Adapter initialization",
           initializeVirtualMemoryAdapter() == VmResult::Ok);
    status("Double initialization",
           initializeVirtualMemoryAdapter() == VmResult::AlreadyReserved);

    MemoryStatus memoryStatus{};
    const bool memoryStatusPass = getMemoryStatus(0, &memoryStatus);
    std::cout << "Memory status: "
              << (memoryStatusPass ? "PASS" : "NOT REQUIRED") << "\n";
    bool restricted = false;
    const nativeaot_vm_uint64 physicalLimit = gcGetPhysicalMemoryLimit(&restricted);
    nativeaot_vm_uint32 memoryLoad = 0;
    nativeaot_vm_uint64 availablePhysical = 0;
    nativeaot_vm_uint64 availablePageFile = 0;
    gcGetMemoryStatus(0, &memoryLoad, &availablePhysical, &availablePageFile);
    status("GC memory-status wrappers",
           gcGetVirtualMemoryLimit() != 0 && gcGetPageSize() == page &&
           (!memoryStatusPass || (physicalLimit != 0 &&
                                  availablePhysical <= physicalLimit &&
                                  availablePageFile <= physicalLimit &&
                                  memoryLoad <= 100 && !restricted)));
    if (memoryStatusPass) {
        std::cout << "Memory status physical limit: " << memoryStatus.physicalLimit << "\n";
    }

    const std::size_t reservationSize = page * 3;
    void* base = nullptr;
    const VirtualMemoryStats beforeReserve =
        gxos::runtime::virtual_memory::stats();
    const VmResult reserved = reserveVirtualMemoryRaw(
        reservationSize, page, 0, kNumaNodeUndefined, &base);
    const VirtualMemoryStats afterReserve =
        gxos::runtime::virtual_memory::stats();
    const bool reservationPass = reserved == VmResult::Ok && base != nullptr &&
        afterReserve.regionOwnedFrames == beforeReserve.regionOwnedFrames;
    status("PAL reserve", reservationPass);
    status("Adapter reserve", reservationPass);
    status("Reservation frame delta", afterReserve.regionOwnedFrames ==
        beforeReserve.regionOwnedFrames);
    metric("Reservation data-frame delta",
           static_cast<std::int64_t>(afterReserve.regionOwnedFrames) -
           static_cast<std::int64_t>(beforeReserve.regionOwnedFrames));

    ReservationDiagnostics oldDiagnostics{};
    const bool diagnosticsPass = queryReservationDiagnostics(base, &oldDiagnostics) &&
        oldDiagnostics.base == base && oldDiagnostics.reservedSize == reservationSize &&
        oldDiagnostics.active && oldDiagnostics.regionGeneration != 0 &&
        oldDiagnostics.reservationGeneration != 0;
    status("Collector region registry", diagnosticsPass);
    status("Reservation query", queryVirtualMemoryRaw(base, nullptr) ==
        VmResult::InvalidArgument);
    VirtualMemoryInfo reservationInfo{};
    const bool reservedQuery = queryVirtualMemoryRaw(base, &reservationInfo) == VmResult::Ok &&
        reservationInfo.reserved && !reservationInfo.committed &&
        !reservationInfo.mappingPresent && reservationInfo.regionBase == base &&
        reservationInfo.reservedSize == reservationSize;
    status("Reserved pages inaccessible before commit", reservedQuery);

    const bool outsideCommit = !commitVirtualMemoryRaw(
        reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(base) - page), page);
    status("Commit outside reservation", outsideCommit);

    void* secondBase = nullptr;
    const bool secondReserved = reserveVirtualMemoryRaw(
        page * 2, page, 0, kNumaNodeUndefined, &secondBase) == VmResult::Ok;
    const bool crossRange = secondReserved &&
        !commitVirtualMemoryRaw(base, reservationSize + page);
    status("Cross-reservation range rejection", crossRange);

    const VirtualMemoryStats beforeCommit =
        gxos::runtime::virtual_memory::stats();
    const bool firstCommit = commitVirtualMemoryRaw(base, page);
    const bool thirdCommit = commitVirtualMemoryRaw(
        reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(base) + page * 2), page);
    const VirtualMemoryStats afterCommit =
        gxos::runtime::virtual_memory::stats();
    const bool commitPass = firstCommit && thirdCommit;
    status("PAL commit", commitPass);
    status("Partial commit", commitPass);
    metric("Commit frame delta",
           static_cast<std::int64_t>(afterCommit.regionOwnedFrames) -
           static_cast<std::int64_t>(beforeCommit.regionOwnedFrames));

    std::uint8_t* bytes = static_cast<std::uint8_t*>(base);
    const bool initialZero = commitPass && zero(bytes, page) &&
        zero(bytes + page * 2, page);
    status("Zero initialization", initialZero);
    if (commitPass) {
        bytes[0] = 0x31;
        bytes[page * 2] = 0xA7;
    }
    const bool independentPatterns = commitPass && bytes[0] == 0x31 &&
        bytes[page * 2] == 0xA7;
    status("Independent committed-page patterns", independentPatterns);

    const VirtualMemoryStats beforeDecommit =
        gxos::runtime::virtual_memory::stats();
    const bool decommitted = decommitVirtualMemoryRaw(base, page);
    VirtualMemoryInfo decommittedInfo{};
    const bool decommittedQuery = decommitted &&
        queryVirtualMemoryRaw(base, &decommittedInfo) == VmResult::Ok &&
        decommittedInfo.reserved && !decommittedInfo.committed &&
        !decommittedInfo.mappingPresent;
    const VirtualMemoryStats afterDecommit =
        gxos::runtime::virtual_memory::stats();
    const bool thirdIntact = decommitted && bytes[page * 2] == 0xA7;
    status("PAL decommit", decommitted);
    status("Decommit preserves reservation", decommittedQuery);
    status("Decommit releases physical frame",
           !gxos::runtime::virtual_memory::stats().physicalBackingAccounting ||
           gxos::runtime::virtual_memory::stats().totalKnownFrames == 0 ||
           afterDecommit.regionOwnedFrames + 1 == beforeDecommit.regionOwnedFrames);
    status("Third page survives interior decommit", thirdIntact);
    metric("Decommit frame delta",
           static_cast<std::int64_t>(afterDecommit.regionOwnedFrames) -
           static_cast<std::int64_t>(beforeDecommit.regionOwnedFrames));

    const bool recommitted = commitVirtualMemoryRaw(base, page);
    const bool recommitZero = recommitted && zero(bytes, page);
    status("Recommit zeroing", recommitZero);

    const bool protectedReadOnly = protectVirtualMemoryRaw(
        reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(base) + page * 2),
        page, MemoryProtection::ReadOnly);
    VirtualMemoryInfo protectionInfo{};
    const bool protectionQuery = protectedReadOnly &&
        queryVirtualMemoryRaw(reinterpret_cast<void*>(
            reinterpret_cast<std::uintptr_t>(base) + page * 2), &protectionInfo) ==
        VmResult::Ok && protectionInfo.committed && protectionInfo.mappingPresent &&
        protectionInfo.protection == MemoryProtection::ReadOnly;
    status("Protection mapping", protectionQuery);
    status("Protection preserves commitment", protectionQuery);
    const bool protectionRestore = protectVirtualMemoryRaw(
        reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(base) + page * 2),
        page, MemoryProtection::ReadWrite);
    status("Protection restore", protectionRestore);

    const bool resetUnsupported = resetVirtualMemoryRaw(base, page, false) ==
        VmResult::Unsupported;
    status("Reset/discard classification", resetUnsupported);
    void* unsupportedBase = nullptr;
    const bool unsupportedWriteWatch =
        reserveVirtualMemoryRaw(page, page, kVirtualReserveWriteWatch,
                                kNumaNodeUndefined, &unsupportedBase) == VmResult::Unsupported;
    status("Unsupported write-watch fails honestly", unsupportedWriteWatch);

    const bool wrongReleaseSize = !releaseVirtualMemoryRaw(base, page);
    status("Release-size mismatch rejection", wrongReleaseSize);
    const bool releasedSecond = secondReserved && releaseVirtualMemoryRaw(secondBase, 0);
    status("PAL release", releasedSecond);
    const bool released = releaseVirtualMemoryRaw(base, reservationSize);
    status("Release", released);
    status("Double release rejection", !releaseVirtualMemoryRaw(base, reservationSize));
    status("Stale record rejection", !reservationIdentityIsActive(
        base, oldDiagnostics.reservationGeneration));
    const RegistryStats afterReleaseRegistry = registryStats();
    status("Registry cleanup", afterReleaseRegistry.liveReservations == 0);

    void* reusedBase = nullptr;
    const VmResult reused = reserveVirtualMemoryAt(
        reservationSize, page, base, 0, kNumaNodeUndefined, &reusedBase);
    ReservationDiagnostics newDiagnostics{};
    const bool rangeReuse = reused == VmResult::Ok && reusedBase == base &&
        queryReservationDiagnostics(reusedBase, &newDiagnostics) &&
        newDiagnostics.reservationGeneration != oldDiagnostics.reservationGeneration &&
        releaseVirtualMemoryRaw(reusedBase, reservationSize);
    status("Preferred-address reserve", reused == VmResult::Ok && reusedBase == base);
    status("Range reuse", rangeReuse);
    status("Registry reuse changes generation", rangeReuse);

    void* exhaustion[32] = {};
    bool exhaustionPass = true;
    for (std::size_t index = 0; index < 32; ++index) {
        exhaustionPass = exhaustionPass &&
            reserveVirtualMemoryRaw(page, page, 0, kNumaNodeUndefined,
                                     &exhaustion[index]) == VmResult::Ok;
    }
    void* overflow = nullptr;
    const bool registryExhausted = exhaustionPass &&
        reserveVirtualMemoryRaw(page, page, 0, kNumaNodeUndefined, &overflow) ==
        VmResult::OutOfMemory;
    status("Registry exhaustion", registryExhausted);
    for (void* entry : exhaustion) {
        if (entry != nullptr) (void)releaseVirtualMemoryRaw(entry, page);
    }
    status("Failure rollback", registryStats().liveReservations == 0);

    void* lifecycleBase = nullptr;
    const bool liveReservation = reserveVirtualMemoryRaw(
        page, page, 0, kNumaNodeUndefined, &lifecycleBase) == VmResult::Ok;
    const bool shutdownRejectsLive = liveReservation &&
        shutdownVirtualMemoryAdapter() == VmResult::NotOwned;
    status("Shutdown rejects live reservations", shutdownRejectsLive);
    const bool lifecycleReleased = !liveReservation ||
        releaseVirtualMemoryRaw(lifecycleBase, page);
    const bool shutdownClean = lifecycleReleased &&
        shutdownVirtualMemoryAdapter() == VmResult::Ok;
    status("Adapter shutdown", shutdownClean);
    status("Shutdown registry leak check", registryStats().liveReservations == 0);
    const bool reinitialized = initializeVirtualMemoryAdapter() == VmResult::Ok;
    status("Adapter reinitialization", reinitialized);
    if (reinitialized) {
        void* finalBase = nullptr;
        const bool finalCycle = reserveVirtualMemoryRaw(
            page, page, 0, kNumaNodeUndefined, &finalBase) == VmResult::Ok &&
            releaseVirtualMemoryRaw(finalBase, page) &&
            shutdownVirtualMemoryAdapter() == VmResult::Ok;
        status("Fresh second startup cycle", finalCycle);
    }

    std::cout << "Trace entries: " << traceEventCount() << "\n";
    std::cout << "NativeAOT adapter probe: " << (g_passed ? "PASS" : "FAIL") << "\n";
    return g_passed ? 0 : 1;
}
