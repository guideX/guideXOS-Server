#include "include/kernel/native_physical_frame_scaling_qemu_test.h"

#if defined(GXOS_C011EC99_PHYSICAL_FRAME_SCALING_QEMU_TEST)

#include "include/kernel/address_space.h"
#include "include/kernel/serial_debug.h"
#include "../../guideXOSBootLoader/guidexOSBootInfo.h"

namespace kernel {
namespace native_physical_frame_scaling_qemu_test {
namespace {

using kernel::memory::address_space::AddressSpace;
using kernel::memory::address_space::FrameAccounting;
using kernel::memory::address_space::FrameOwner;
using kernel::memory::address_space::FrameReleaseReason;
using kernel::memory::address_space::MappingInfo;

constexpr uint64_t kPageSize = 0x1000;
constexpr uint64_t kOldFrameLimit = 0x1000;
// Stay outside the kernel's low virtual image, the loader's explicit 64 MiB
// allocator window, and the bootloader's low identity map.  The physical
// frame itself is still selected by the PMM; this is only a probe address.
constexpr uintptr_t kHighMappingVirtual = 0x50000000ULL;
constexpr uint64_t kInvalidFrameIndex = static_cast<uint64_t>(-1);

bool g_failed = false;

void status(const char* name, bool passed)
{
    serial::puts("[C99-PMM] ");
    serial::puts(name);
    serial::puts(passed ? ": PASS\n" : ": FAIL\n");
    if (!passed) g_failed = true;
}

void metric(const char* name, uint64_t value)
{
    serial::puts("[C99-PMM] ");
    serial::puts(name);
    serial::puts("=");
    serial::put_hex64(value);
    serial::puts("\n");
}

uint32_t mapType(const uint8_t* raw)
{
    return raw == nullptr ? 0u : *reinterpret_cast<const uint32_t*>(raw);
}

uint64_t mapPhysicalStart(const uint8_t* raw)
{
    return raw == nullptr ? 0 : *reinterpret_cast<const uint64_t*>(raw + 8);
}

uint64_t mapPages(const uint8_t* raw)
{
    return raw == nullptr ? 0 : *reinterpret_cast<const uint64_t*>(raw + 24);
}

void emitAccounting(const char* checkpoint, const FrameAccounting& frames)
{
    serial::puts("[C99-PMM] checkpoint=");
    serial::puts(checkpoint);
    serial::puts(" discoveredUsablePages=");
    serial::put_hex64(frames.discoveredUsableFrames);
    serial::puts(" trackedFrames=");
    serial::put_hex64(frames.totalKnownFrames);
    serial::puts(" metadataPages=");
    serial::put_hex64(frames.metadataFrames);
    serial::puts(" freeFrames=");
    serial::put_hex64(frames.freeFrames);
    serial::puts(" allocatedFrames=");
    serial::put_hex64(frames.allocatedFrames);
    serial::puts(" regionOwnedFrames=");
    serial::put_hex64(frames.regionOwnedFrames);
    serial::puts(" pageTableFrames=");
    serial::put_hex64(frames.pageTableFrames);
    serial::puts(" rangeCount=");
    serial::put_hex64(frames.usableRangeCount);
    serial::puts(" highestPhysical=");
    serial::put_hex64(frames.highestPhysicalAddress);
    serial::puts("\n");
}

bool accountingReconciles(const FrameAccounting& frames)
{
    return frames.freeFrames + frames.allocatedFrames == frames.totalKnownFrames &&
           frames.allocatedFrames == frames.regionOwnedFrames + frames.pageTableFrames &&
           frames.discoveredUsableFrames == frames.totalKnownFrames;
}

} // namespace

void run(const guideXOS::BootInfo* bootInfo)
{
    serial::puts("[C99-PMM] BEGIN\n");
    const FrameAccounting initial = kernel::memory::address_space::accounting();
    emitAccounting("initial", initial);

    status("allocator initialized", kernel::memory::address_space::isInitialized());
    status("capacity is map-derived", initial.totalKnownFrames > 0 &&
           initial.discoveredUsableFrames == initial.totalKnownFrames);
    status("tracked capacity exceeds old 4096-frame limit",
           initial.totalKnownFrames > kOldFrameLimit);
    status("metadata is smaller than a dense two-byte table",
           initial.metadataFrames * kPageSize >= initial.totalKnownFrames &&
           initial.metadataFrames * kPageSize < initial.totalKnownFrames * 2 + kPageSize);
    status("initial accounting reconciles", accountingReconciles(initial));

    bool usableDescriptorEnrolled = false;
    bool reservedDescriptorExcluded = false;
    if (bootInfo != nullptr && bootInfo->MemoryMap != 0 &&
        bootInfo->MemoryMapEntryCount != 0 && bootInfo->MemoryMapDescriptorSize >= 40) {
        const uint8_t* map = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(
            bootInfo->MemoryMap));
        for (uint64_t entry = 0; entry < bootInfo->MemoryMapEntryCount; ++entry) {
            const uint8_t* raw = map + entry * bootInfo->MemoryMapDescriptorSize;
            const uint64_t start = mapPhysicalStart(raw);
            const uint64_t pages = mapPages(raw);
            if (pages == 0) continue;
            if (guideXOS::guidexos_memory_type_is_usable(mapType(raw))) {
                const uint64_t probe = start == 0 && pages > 1 ? kPageSize : start;
                if (kernel::memory::address_space::frameIndexForPhysical(probe) !=
                    kInvalidFrameIndex) usableDescriptorEnrolled = true;
            } else if (mapType(raw) == 0u || mapType(raw) == 1u || mapType(raw) == 2u ||
                       mapType(raw) == 5u || mapType(raw) == 6u ||
                       mapType(raw) == 8u || mapType(raw) == 9u ||
                       mapType(raw) == 10u || mapType(raw) == 11u ||
                       mapType(raw) == 12u || mapType(raw) == 13u ||
                       mapType(raw) == 14u) {
                if (kernel::memory::address_space::frameIndexForPhysical(start) ==
                    kInvalidFrameIndex) reservedDescriptorExcluded = true;
            }
        }
    }
    status("usable descriptor enrolled", usableDescriptorEnrolled);
    status("reserved/MMIO descriptor excluded", reservedDescriptorExcluded);
    status("frame zero is sentinel", kernel::memory::address_space::frameIndexForPhysical(0) ==
           kInvalidFrameIndex);
    status("metadata pages excluded", bootInfo != nullptr &&
           kernel::memory::address_space::frameIndexForPhysical(
               bootInfo->PhysicalFrameMetadataBase) == kInvalidFrameIndex);

    // Hold the first old-limit frames, then allocate both owner classes above
    // index 0xFFF.  This is bounded and leaves the remainder of RAM untouched.
    static uint64_t lowFrames[kOldFrameLimit] = {};
    bool lowAllocations = true;
    for (uint64_t index = 0; index < kOldFrameLimit; ++index) {
        lowFrames[index] = kernel::memory::address_space::allocateFrame(FrameOwner::VmRegion);
        if (lowFrames[index] == 0 ||
            kernel::memory::address_space::frameIndexForPhysical(lowFrames[index]) != index) {
            lowAllocations = false;
            break;
        }
    }
    status("old boundary frames allocated", lowAllocations);

    const uint64_t highVm = kernel::memory::address_space::allocateFrame(FrameOwner::VmRegion);
    const uint64_t highVmIndex = kernel::memory::address_space::frameIndexForPhysical(highVm);
    const uint64_t highPageTable = kernel::memory::address_space::allocateFrame(FrameOwner::PageTable);
    const uint64_t highPageTableIndex =
        kernel::memory::address_space::frameIndexForPhysical(highPageTable);
    metric("firstAllocationBeyondOldBoundaryIndex", highVmIndex);
    metric("firstAllocationBeyondOldBoundaryPhysical", highVm);
    metric("pageTableBeyondOldBoundaryIndex", highPageTableIndex);
    status("first allocation beyond old boundary succeeds", highVm != 0 &&
           highVmIndex >= kOldFrameLimit);
    status("page-table owner above old boundary succeeds", highPageTable != 0 &&
           highPageTableIndex > kOldFrameLimit);
    status("physical address is page aligned", highVm != 0 && (highVm & (kPageSize - 1)) == 0);

    AddressSpace* owner = kernel::memory::address_space::current();
    MappingInfo beforeMapping{};
    const bool emptyMappingSlot = highVm != 0 && owner != nullptr &&
        !kernel::memory::address_space::queryPage(owner, kHighMappingVirtual,
                                                  &beforeMapping) &&
        !beforeMapping.hasEntry;
    status("high frame mapping slot is initially free", emptyMappingSlot);
    const bool zeroed = emptyMappingSlot &&
        kernel::memory::address_space::zeroFrame(highVm);
    const bool remapped = zeroed && kernel::memory::address_space::mapPage(
        owner, kHighMappingVirtual, highVm, 0x3);
    status("high frame page mapping installed", remapped);
    MappingInfo afterMapping{};
    const bool highMappingValid = remapped &&
        kernel::memory::address_space::queryPage(owner, kHighMappingVirtual,
                                                 &afterMapping) &&
        afterMapping.present && afterMapping.physicalAddress == highVm;
    status("high frame mapping query returned", highMappingValid);
    if (afterMapping.hasEntry) {
        metric("highFrameObservedPhysical", afterMapping.physicalAddress);
        metric("highFrameObservedFlags", afterMapping.flags);
    }
    const bool unmappedAgain = remapped &&
        kernel::memory::address_space::unmapPage(owner, kHighMappingVirtual, nullptr);
    status("high frame page-table mapping", emptyMappingSlot && remapped && highMappingValid &&
           unmappedAgain);

    const FrameAccounting duringBoundary = kernel::memory::address_space::accounting();
    emitAccounting("boundary-allocated", duringBoundary);
    status("old 0xFFF allocated state crossed", duringBoundary.allocatedFrames >= kOldFrameLimit + 2);

    // Exercise the existing bounded VM rollback gate with the new metadata.
    const uint64_t rollbackFreeBefore = duringBoundary.freeFrames;
    kernel::memory::address_space::setVmRegionFrameLimitForTests(2);
    const uint64_t rollbackFirst = kernel::memory::address_space::allocateFrame(FrameOwner::VmRegion);
    const uint64_t rollbackSecond = kernel::memory::address_space::allocateFrame(FrameOwner::VmRegion);
    const uint64_t rollbackThird = kernel::memory::address_space::allocateFrame(FrameOwner::VmRegion);
    const bool rollbackAllocations = rollbackFirst != 0 && rollbackSecond != 0 && rollbackThird == 0;
    if (rollbackFirst != 0) (void)kernel::memory::address_space::releaseFrame(
        rollbackFirst, FrameOwner::VmRegion, FrameReleaseReason::Other);
    if (rollbackSecond != 0) (void)kernel::memory::address_space::releaseFrame(
        rollbackSecond, FrameOwner::VmRegion, FrameReleaseReason::Other);
    kernel::memory::address_space::clearVmRegionFrameLimitForTests();
    const FrameAccounting afterRollback = kernel::memory::address_space::accounting();
    emitAccounting("rollback-restored", afterRollback);
    status("bounded rollback restores free frames", rollbackAllocations &&
           afterRollback.freeFrames == rollbackFreeBefore &&
           afterRollback.allocatedFrames == duringBoundary.allocatedFrames);

    const bool ownerMismatchRejected = highPageTable != 0 &&
        !kernel::memory::address_space::releaseFrame(
            highPageTable, FrameOwner::VmRegion, FrameReleaseReason::Other);
    const bool pageTableFreed = highPageTable != 0 &&
        kernel::memory::address_space::releaseFrame(
            highPageTable, FrameOwner::PageTable, FrameReleaseReason::Other);
    const bool highVmFreed = highVm != 0 &&
        kernel::memory::address_space::releaseFrame(
            highVm, FrameOwner::VmRegion, FrameReleaseReason::Other);
    status("owner mismatch rejected", ownerMismatchRejected);
    status("page-table frame freed", pageTableFreed);
    status("high VM frame freed", highVmFreed);
    status("double-free rejected", highVm != 0 &&
           !kernel::memory::address_space::releaseFrame(
               highVm, FrameOwner::VmRegion, FrameReleaseReason::Other));

    bool lowFreed = lowAllocations;
    for (uint64_t index = 0; index < kOldFrameLimit; ++index) {
        if (lowFrames[index] != 0) {
            lowFreed = kernel::memory::address_space::releaseFrame(
                lowFrames[index], FrameOwner::VmRegion, FrameReleaseReason::Other) && lowFreed;
        }
    }
    const FrameAccounting finalFrames = kernel::memory::address_space::accounting();
    emitAccounting("final", finalFrames);
    const uint64_t persistentPageTables = duringBoundary.pageTableFrames -
        (highPageTable != 0 ? 1 : 0);
    status("boundary allocation frees successfully", lowFreed && pageTableFreed && highVmFreed);
    status("counters restore exactly", finalFrames.totalKnownFrames == initial.totalKnownFrames &&
           finalFrames.freeFrames + persistentPageTables == initial.freeFrames &&
           finalFrames.allocatedFrames == initial.allocatedFrames + persistentPageTables &&
           finalFrames.regionOwnedFrames == initial.regionOwnedFrames &&
           finalFrames.pageTableFrames == initial.pageTableFrames + persistentPageTables &&
           finalFrames.mappingCount == initial.mappingCount);
    status("final accounting reconciles", accountingReconciles(finalFrames));
    status("out-of-range release rejected", !kernel::memory::address_space::releaseFrame(
        0, FrameOwner::VmRegion, FrameReleaseReason::Other));

    if (!g_failed) serial::puts("[C99-PMM] ALL_PASS\n");
    else serial::puts("[C99-PMM] ALL_FAIL\n");
}

} // namespace native_physical_frame_scaling_qemu_test
} // namespace kernel

#endif
