// Generic single-address-space frame ownership and AMD64 4 KiB page mapping.
//
// This is deliberately independent of .NET, NativeAOT, and the managed heap.
// It consumes the UEFI usable-memory ranges handed off by the bootloader and
// edits the page tables already installed by the bootloader. The current
// implementation is single CPU; every mapping change invalidates the local
// translation immediately.

#include "include/kernel/address_space.h"
#include "include/kernel/arch.h"
#include "../../guideXOSBootLoader/guidexOSBootInfo.h"

#if defined(ARCH_AMD64)
#include "arch/amd64.h"
#endif

namespace kernel {
namespace memory {
namespace address_space {

namespace {

constexpr uint64_t kPageSize = 4096;
constexpr uint64_t kPageMask = 0x000FFFFFFFFFF000ULL;
// This bounds descriptor fragmentation, not physical capacity.  QEMU/UEFI
// maps are far below this limit; refusing a more fragmented map is safer than
// exposing untracked RAM.  Per-frame storage remains dynamically sized.
constexpr uint64_t kMaxUsableRanges = 256;
constexpr uint64_t kPtePresent = 1ULL << 0;
constexpr uint64_t kPteWritable = 1ULL << 1;
constexpr uint64_t kPtePageSize = 1ULL << 7;
constexpr uint8_t kFrameStateMask = 0x03;
constexpr uint8_t kFrameMappedBit = 0x04;
constexpr uint64_t kInvalidFrameIndex = static_cast<uint64_t>(-1);

enum class FrameState : uint8_t {
    Free = 0,
    VmRegion = 1,
    PageTable = 2
};

AddressSpace g_current = {0, false};
struct UsableRange {
    uint64_t physicalStart;
    uint64_t pageCount;
    uint64_t firstFrameIndex;
};

UsableRange g_usableRanges[kMaxUsableRanges] = {};
uint64_t g_usableRangeCount = 0;
uint64_t g_frameCount = 0;
uint64_t g_discoveredUsableFrames = 0;
uint64_t g_metadataFrames = 0;
uint64_t g_highestPhysicalAddress = 0;
uint8_t* g_frameMetadata = nullptr;
uint64_t g_allocatedFrames = 0;
uint64_t g_regionOwnedFrames = 0;
uint64_t g_pageTableFrames = 0;
uint64_t g_mappingCount = 0;
uint64_t g_releasedByDecommit = 0;
uint64_t g_releasedByRelease = 0;
uint64_t g_tlbInvalidations = 0;
uint64_t g_nextIdentity = 1;
uint64_t g_vmRegionFrameLimit = static_cast<uint64_t>(-1);
uint64_t g_vmRegionFramesAllocatedSinceLimit = 0;

bool aligned(uint64_t value) { return (value & (kPageSize - 1)) == 0; }

bool addNoOverflow(uint64_t left, uint64_t right, uint64_t* result) {
    if (result == nullptr || left > static_cast<uint64_t>(-1) - right) return false;
    *result = left + right;
    return true;
}

bool descriptorRange(const uint8_t* raw, uint64_t* physicalStart,
                    uint64_t* pageCount) {
    if (raw == nullptr || physicalStart == nullptr || pageCount == nullptr) return false;
    // EFI_MEMORY_DESCRIPTOR has Type at offset 0, a 32-bit reserved field,
    // then PhysicalStart and NumberOfPages at offsets 8 and 24.
    const uint64_t start = *reinterpret_cast<const uint64_t*>(raw + 8);
    const uint64_t pages = *reinterpret_cast<const uint64_t*>(raw + 24);
    uint64_t bytes = 0;
    if (pages == 0 || pages > static_cast<uint64_t>(-1) / kPageSize ||
        !addNoOverflow(start, pages * kPageSize, &bytes)) return false;
    (void)bytes;
    *physicalStart = start;
    *pageCount = pages;
    return true;
}

uint32_t descriptorType(const uint8_t* raw) {
    return raw == nullptr ? 0u : *reinterpret_cast<const uint32_t*>(raw);
}

bool appendUsableRange(uint64_t physicalStart, uint64_t pageCount) {
    if (pageCount == 0 || !aligned(physicalStart)) return true;

    // Physical zero is the public failure/sentinel value and can never be an
    // allocator result, even if firmware labels the first page conventional.
    if (physicalStart == 0) {
        if (pageCount == 1) return true;
        physicalStart = kPageSize;
        --pageCount;
    }

    uint64_t rangeBytes = 0;
    uint64_t rangeEnd = 0;
    if (pageCount > static_cast<uint64_t>(-1) / kPageSize ||
        !addNoOverflow(physicalStart, pageCount * kPageSize, &rangeEnd)) return false;
    rangeBytes = pageCount * kPageSize;
    (void)rangeBytes;

    if (g_usableRangeCount != 0) {
        UsableRange& previous = g_usableRanges[g_usableRangeCount - 1];
        uint64_t previousEnd = 0;
        if (!addNoOverflow(previous.physicalStart,
                           previous.pageCount * kPageSize, &previousEnd)) return false;
        if (previousEnd == physicalStart) {
            if (!addNoOverflow(previous.pageCount, pageCount, &previous.pageCount) ||
                !addNoOverflow(g_frameCount, pageCount, &g_frameCount)) return false;
            if (rangeEnd > g_highestPhysicalAddress) g_highestPhysicalAddress = rangeEnd;
            return true;
        }
    }

    if (g_usableRangeCount >= kMaxUsableRanges ||
        !addNoOverflow(g_frameCount, pageCount, &g_frameCount)) return false;
    g_usableRanges[g_usableRangeCount++] =
        UsableRange{physicalStart, pageCount, g_frameCount - pageCount};
    if (rangeEnd > g_highestPhysicalAddress) g_highestPhysicalAddress = rangeEnd;
    return true;
}

bool parseUsableMemoryMap(const guideXOS::BootInfo* bootInfo) {
    if (bootInfo == nullptr || bootInfo->MemoryMap == 0 ||
        bootInfo->MemoryMapEntryCount == 0 ||
        bootInfo->MemoryMapDescriptorSize < 40 ||
        bootInfo->MemoryMapEntryCount > static_cast<uint64_t>(-1) /
            bootInfo->MemoryMapDescriptorSize) return false;

    g_usableRangeCount = 0;
    g_frameCount = 0;
    g_discoveredUsableFrames = 0;
    g_highestPhysicalAddress = 0;
    const uint8_t* map = reinterpret_cast<const uint8_t*>(
        static_cast<uintptr_t>(bootInfo->MemoryMap));
    for (uint64_t entry = 0; entry < bootInfo->MemoryMapEntryCount; ++entry) {
        const uint8_t* raw = map + entry * bootInfo->MemoryMapDescriptorSize;
        if (!guideXOS::guidexos_memory_type_is_usable(descriptorType(raw))) continue;
        uint64_t physicalStart = 0;
        uint64_t pageCount = 0;
        if (!descriptorRange(raw, &physicalStart, &pageCount) ||
            !appendUsableRange(physicalStart, pageCount)) return false;
    }
    g_discoveredUsableFrames = g_frameCount;
    return g_frameCount != 0;
}

uint8_t frameMetadata(uint64_t index) {
    return g_frameMetadata == nullptr || index >= g_frameCount
        ? 0 : g_frameMetadata[index];
}

void setFrameMetadata(uint64_t index, uint8_t value) {
    if (g_frameMetadata != nullptr && index < g_frameCount) {
        g_frameMetadata[index] = value;
    }
}

FrameState frameState(uint64_t index) {
    return static_cast<FrameState>(frameMetadata(index) & kFrameStateMask);
}

bool frameMapped(uint64_t index) {
    return (frameMetadata(index) & kFrameMappedBit) != 0;
}

uint64_t physicalForFrame(uint64_t frameIndex) {
    if (frameIndex >= g_frameCount) return 0;
    for (uint64_t rangeIndex = 0; rangeIndex < g_usableRangeCount; ++rangeIndex) {
        const UsableRange& range = g_usableRanges[rangeIndex];
        if (frameIndex < range.firstFrameIndex ||
            frameIndex - range.firstFrameIndex >= range.pageCount) continue;
        const uint64_t offset = frameIndex - range.firstFrameIndex;
        if (offset > static_cast<uint64_t>(-1) / kPageSize) return 0;
        uint64_t result = 0;
        if (!addNoOverflow(range.physicalStart, offset * kPageSize, &result)) return 0;
        return result;
    }
    return 0;
}

uint64_t* physicalPointer(uint64_t physicalAddress) {
    return reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(physicalAddress));
}

bool poolIndex(uint64_t physicalAddress, uint64_t* index) {
    if (index == nullptr || !aligned(physicalAddress) || g_frameCount == 0) return false;
    for (uint64_t rangeIndex = 0; rangeIndex < g_usableRangeCount; ++rangeIndex) {
        const UsableRange& range = g_usableRanges[rangeIndex];
        if (physicalAddress < range.physicalStart) continue;
        const uint64_t offset = physicalAddress - range.physicalStart;
        if (offset / kPageSize >= range.pageCount) continue;
        *index = range.firstFrameIndex + offset / kPageSize;
        return *index < g_frameCount;
    }
    return false;
}

bool frameOwnerMatches(FrameState state, FrameOwner owner) {
    return (owner == FrameOwner::VmRegion && state == FrameState::VmRegion) ||
           (owner == FrameOwner::PageTable && state == FrameState::PageTable);
}

bool validOwner(FrameOwner owner) {
    return owner == FrameOwner::VmRegion || owner == FrameOwner::PageTable;
}

uint64_t allocateFrameInternal(FrameOwner owner) {
#if defined(ARCH_AMD64)
    if (!g_current.alive || !validOwner(owner)) return 0;
    if (owner == FrameOwner::VmRegion &&
        g_vmRegionFramesAllocatedSinceLimit >= g_vmRegionFrameLimit) return 0;
    for (uint64_t index = 0; index < g_frameCount; ++index) {
        if (frameState(index) != FrameState::Free) continue;
        setFrameMetadata(index, static_cast<uint8_t>(owner == FrameOwner::VmRegion
            ? FrameState::VmRegion : FrameState::PageTable));
        ++g_allocatedFrames;
        if (owner == FrameOwner::VmRegion) {
            ++g_regionOwnedFrames;
            ++g_vmRegionFramesAllocatedSinceLimit;
        } else {
            ++g_pageTableFrames;
        }
        const uint64_t physical = physicalForFrame(index);
        if (physical == 0) {
            setFrameMetadata(index, 0);
            --g_allocatedFrames;
            if (owner == FrameOwner::VmRegion) {
                --g_regionOwnedFrames;
                --g_vmRegionFramesAllocatedSinceLimit;
            } else {
                --g_pageTableFrames;
            }
            return 0;
        }
        return physical;
    }
#else
    (void)owner;
#endif
    return 0;
}

uint64_t allocatePageTableFrame() {
    const uint64_t physical = allocateFrameInternal(FrameOwner::PageTable);
    if (physical == 0 || !kernel::memory::address_space::zeroFrame(physical)) {
        if (physical != 0) {
            (void)kernel::memory::address_space::releaseFrame(
                physical, FrameOwner::PageTable, FrameReleaseReason::Other);
        }
        return 0;
    }
    return physical;
}

bool pageTableEntry(uintptr_t virtualAddress, uint64_t** entryOut,
                    bool createTables) {
#if defined(ARCH_AMD64)
    if (entryOut == nullptr || !g_current.alive || !aligned(virtualAddress)) return false;
    uint64_t rootPhysical = arch::amd64::read_cr3() & kPageMask;
    uint64_t* pml4 = physicalPointer(rootPhysical);
    const uint64_t pml4Index = (virtualAddress >> 39) & 0x1FFULL;
    const uint64_t pdptIndex = (virtualAddress >> 30) & 0x1FFULL;
    const uint64_t pdIndex = (virtualAddress >> 21) & 0x1FFULL;
    const uint64_t ptIndex = (virtualAddress >> 12) & 0x1FFULL;

    uint64_t entry = pml4[pml4Index];
    if ((entry & kPtePresent) == 0) {
        if (!createTables) return false;
        const uint64_t table = allocatePageTableFrame();
        if (table == 0) return false;
        pml4[pml4Index] = table | kPtePresent | kPteWritable;
        entry = pml4[pml4Index];
    }
    uint64_t* pdpt = physicalPointer(entry & kPageMask);

    entry = pdpt[pdptIndex];
    if ((entry & kPtePresent) == 0) {
        if (!createTables) return false;
        const uint64_t table = allocatePageTableFrame();
        if (table == 0) return false;
        pdpt[pdptIndex] = table | kPtePresent | kPteWritable;
        entry = pdpt[pdptIndex];
    } else if ((entry & kPtePageSize) != 0) {
        return false;
    }
    uint64_t* pd = physicalPointer(entry & kPageMask);

    entry = pd[pdIndex];
    if ((entry & kPtePresent) == 0) {
        if (!createTables) return false;
        const uint64_t table = allocatePageTableFrame();
        if (table == 0) return false;
        pd[pdIndex] = table | kPtePresent | kPteWritable;
        entry = pd[pdIndex];
    } else if ((entry & kPtePageSize) != 0) {
        return false;
    }
    uint64_t* pt = physicalPointer(entry & kPageMask);
    *entryOut = &pt[ptIndex];
    return true;
#else
    (void)virtualAddress;
    (void)entryOut;
    (void)createTables;
    return false;
#endif
}

} // namespace

bool initialize(const guideXOS::BootInfo* bootInfo) {
#if defined(ARCH_AMD64)
    if (bootInfo == nullptr || bootInfo->PhysicalFrameMetadataBase == 0 ||
        bootInfo->PhysicalFrameMetadataPages == 0 ||
        !aligned(bootInfo->PhysicalFrameMetadataBase)) {
        return false;
    }
    if (g_current.alive) return true;
    if (!parseUsableMemoryMap(bootInfo)) return false;
    g_metadataFrames = bootInfo->PhysicalFrameMetadataPages;
    if (g_metadataFrames > static_cast<uint64_t>(-1) / kPageSize ||
        g_metadataFrames * kPageSize < g_frameCount) return false;
    g_frameMetadata = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(
        bootInfo->PhysicalFrameMetadataBase));
    const uint64_t metadataBytes = g_metadataFrames * kPageSize;
    for (uint64_t index = 0; index < metadataBytes; ++index) {
        g_frameMetadata[index] = 0;
    }
    g_allocatedFrames = 0;
    g_regionOwnedFrames = 0;
    g_pageTableFrames = 0;
    g_mappingCount = 0;
    g_releasedByDecommit = 0;
    g_releasedByRelease = 0;
    g_tlbInvalidations = 0;
    g_vmRegionFrameLimit = static_cast<uint64_t>(-1);
    g_vmRegionFramesAllocatedSinceLimit = 0;
    g_current.identity = g_nextIdentity++;
    if (g_current.identity == 0) g_current.identity = g_nextIdentity++;
    g_current.alive = true;

    // Make supervisor writes honor read-only PTEs. The default bootloader
    // tables remain writable; only future transition tests depend on this.
    uint64_t cr0 = arch::amd64::read_cr0();
    arch::amd64::write_cr0(cr0 | (1ULL << 16));
    return true;
#else
    (void)bootInfo;
    return false;
#endif
}

bool isInitialized() { return g_current.alive; }

AddressSpace* current() { return g_current.alive ? &g_current : nullptr; }

uint64_t allocateFrame(FrameOwner owner) { return allocateFrameInternal(owner); }

bool releaseFrame(uint64_t physicalAddress, FrameOwner owner,
                  FrameReleaseReason reason) {
    uint64_t index = 0;
    if (!poolIndex(physicalAddress, &index) ||
        !validOwner(owner) || !frameOwnerMatches(frameState(index), owner) ||
        frameMapped(index) || g_allocatedFrames == 0) return false;
    setFrameMetadata(index, 0);
    --g_allocatedFrames;
    if (owner == FrameOwner::VmRegion) {
        --g_regionOwnedFrames;
    } else {
        --g_pageTableFrames;
    }
    if (reason == FrameReleaseReason::Decommit) ++g_releasedByDecommit;
    if (reason == FrameReleaseReason::Release) ++g_releasedByRelease;
    return true;
}

bool zeroFrame(uint64_t physicalAddress) {
    uint64_t index = 0;
    if (!poolIndex(physicalAddress, &index)) return false;
    volatile uint8_t* bytes = reinterpret_cast<volatile uint8_t*>(
        static_cast<uintptr_t>(physicalAddress));
    for (uint64_t i = 0; i < kPageSize; ++i) bytes[i] = 0;
    return true;
}

uint64_t frameIndexForPhysical(uint64_t physicalAddress) {
    uint64_t index = kInvalidFrameIndex;
    return poolIndex(physicalAddress, &index) ? index : kInvalidFrameIndex;
}

bool mapPage(AddressSpace* owner, uintptr_t virtualAddress,
             uint64_t physicalAddress, uint64_t flags) {
#if defined(ARCH_AMD64)
    uint64_t frameIndex = 0;
    if (owner == nullptr || owner != &g_current || !owner->alive ||
        !aligned(virtualAddress) || !aligned(physicalAddress) ||
        (flags & kPtePageSize) != 0 ||
        !poolIndex(physicalAddress, &frameIndex) ||
        frameState(frameIndex) != FrameState::VmRegion ||
        frameMapped(frameIndex)) return false;
    uint64_t* entry = nullptr;
    if (!pageTableEntry(virtualAddress, &entry, true) || *entry != 0) return false;
    *entry = (physicalAddress & kPageMask) | (flags & ~kPageMask);
    setFrameMetadata(frameIndex, frameMetadata(frameIndex) | kFrameMappedBit);
    ++g_mappingCount;
    invalidateTlb(virtualAddress);
    return true;
#else
    (void)owner; (void)virtualAddress; (void)physicalAddress; (void)flags;
    return false;
#endif
}

bool unmapPage(AddressSpace* owner, uintptr_t virtualAddress,
               MappingInfo* removed) {
#if defined(ARCH_AMD64)
    if (owner == nullptr || owner != &g_current || !owner->alive ||
        !aligned(virtualAddress)) return false;
    uint64_t* entry = nullptr;
    if (!pageTableEntry(virtualAddress, &entry, false) || *entry == 0) {
        if (removed != nullptr) *removed = MappingInfo{false, false, 0, 0};
        return false;
    }
    const uint64_t value = *entry;
    if (removed != nullptr) {
        removed->hasEntry = true;
        removed->present = (value & kPtePresent) != 0;
        removed->physicalAddress = value & kPageMask;
        removed->flags = value & ~kPageMask;
    }
    *entry = 0;
    uint64_t frameIndex = 0;
    if (poolIndex(value & kPageMask, &frameIndex) && frameMapped(frameIndex)) {
        setFrameMetadata(frameIndex, frameMetadata(frameIndex) & ~kFrameMappedBit);
        if (g_mappingCount != 0) --g_mappingCount;
    }
    invalidateTlb(virtualAddress);
    return true;
#else
    (void)owner; (void)virtualAddress; (void)removed;
    return false;
#endif
}

bool queryPage(AddressSpace* owner, uintptr_t virtualAddress,
               MappingInfo* mapping) {
#if defined(ARCH_AMD64)
    if (mapping == nullptr) return false;
    *mapping = MappingInfo{false, false, 0, 0};
    if (owner == nullptr || owner != &g_current || !owner->alive ||
        !aligned(virtualAddress)) return false;
    uint64_t* entry = nullptr;
    if (!pageTableEntry(virtualAddress, &entry, false) || *entry == 0) return false;
    mapping->hasEntry = true;
    mapping->present = (*entry & kPtePresent) != 0;
    mapping->physicalAddress = *entry & kPageMask;
    mapping->flags = *entry & ~kPageMask;
    return true;
#else
    (void)owner; (void)virtualAddress; (void)mapping;
    return false;
#endif
}

bool updatePageFlags(AddressSpace* owner, uintptr_t virtualAddress,
                     uint64_t flags) {
#if defined(ARCH_AMD64)
    if (owner == nullptr || owner != &g_current || !owner->alive ||
        !aligned(virtualAddress) || (flags & kPtePageSize) != 0) return false;
    uint64_t* entry = nullptr;
    if (!pageTableEntry(virtualAddress, &entry, false) || *entry == 0) return false;
    *entry = (*entry & kPageMask) | (flags & ~kPageMask);
    invalidateTlb(virtualAddress);
    return true;
#else
    (void)owner; (void)virtualAddress; (void)flags;
    return false;
#endif
}

bool rangeHasPresentMapping(AddressSpace* owner, uintptr_t base,
                            uint64_t pageCount) {
    if (owner == nullptr || owner != &g_current || !owner->alive ||
        !aligned(base)) return true;
    for (uint64_t index = 0; index < pageCount; ++index) {
        MappingInfo mapping{};
        if (queryPage(owner, base + index * kPageSize, &mapping) && mapping.present) {
            return true;
        }
    }
    return false;
}

void invalidateTlb(uintptr_t virtualAddress) {
#if defined(ARCH_AMD64)
    asm volatile("invlpg (%0)" : : "r"(virtualAddress) : "memory");
#else
    (void)virtualAddress;
#endif
    ++g_tlbInvalidations;
}

FrameAccounting accounting() {
    FrameAccounting result{};
    result.totalKnownFrames = g_frameCount;
    result.discoveredUsableFrames = g_discoveredUsableFrames;
    result.metadataFrames = g_metadataFrames;
    result.usableRangeCount = g_usableRangeCount;
    result.highestPhysicalAddress = g_highestPhysicalAddress;
    result.allocatedFrames = g_allocatedFrames;
    result.freeFrames = g_frameCount >= g_allocatedFrames
        ? g_frameCount - g_allocatedFrames : 0;
    result.regionOwnedFrames = g_regionOwnedFrames;
    result.pageTableFrames = g_pageTableFrames;
    result.mappingCount = g_mappingCount;
    result.framesReleasedByDecommit = g_releasedByDecommit;
    result.framesReleasedByRelease = g_releasedByRelease;
    result.tlbInvalidations = g_tlbInvalidations;
    return result;
}

void setVmRegionFrameLimitForTests(uint64_t additionalFrames) {
    g_vmRegionFrameLimit = g_vmRegionFramesAllocatedSinceLimit + additionalFrames;
}

void clearVmRegionFrameLimitForTests() {
    g_vmRegionFrameLimit = static_cast<uint64_t>(-1);
}

void destroyCurrent() {
    g_current.alive = false;
    g_current.identity = 0;
}

} // namespace address_space
} // namespace memory
} // namespace kernel
