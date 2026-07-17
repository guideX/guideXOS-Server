// Generic single-address-space frame ownership and AMD64 4 KiB page mapping.
//
// This is deliberately independent of .NET, NativeAOT, and the managed heap.
// It consumes the explicit boot-provided frame pool and edits the page tables
// already installed by the bootloader. The current implementation is single
// CPU; every mapping change invalidates the local translation immediately.

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
constexpr uint64_t kMaxFramePoolPages = 4096;
constexpr uint64_t kPtePresent = 1ULL << 0;
constexpr uint64_t kPteWritable = 1ULL << 1;
constexpr uint64_t kPtePageSize = 1ULL << 7;

enum class FrameState : uint8_t {
    Free = 0,
    VmRegion = 1,
    PageTable = 2
};

AddressSpace g_current = {0, false};
uint64_t g_poolBase = 0;
uint64_t g_poolPages = 0;
FrameState g_frameState[kMaxFramePoolPages] = {};
uint64_t g_allocatedFrames = 0;
uint64_t g_regionOwnedFrames = 0;
uint64_t g_pageTableFrames = 0;
uint64_t g_releasedByDecommit = 0;
uint64_t g_releasedByRelease = 0;
uint64_t g_tlbInvalidations = 0;
uint64_t g_nextIdentity = 1;
uint64_t g_vmRegionFrameLimit = static_cast<uint64_t>(-1);
uint64_t g_vmRegionFramesAllocatedSinceLimit = 0;

bool aligned(uint64_t value) { return (value & (kPageSize - 1)) == 0; }

uint64_t* physicalPointer(uint64_t physicalAddress) {
    return reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(physicalAddress));
}

bool poolIndex(uint64_t physicalAddress, uint64_t* index) {
    if (index == nullptr || !aligned(physicalAddress) ||
        physicalAddress < g_poolBase || g_poolPages == 0) return false;
    const uint64_t offset = physicalAddress - g_poolBase;
    if (offset / kPageSize >= g_poolPages) return false;
    *index = offset / kPageSize;
    return true;
}

bool frameOwnerMatches(FrameState state, FrameOwner owner) {
    return (owner == FrameOwner::VmRegion && state == FrameState::VmRegion) ||
           (owner == FrameOwner::PageTable && state == FrameState::PageTable);
}

uint64_t allocateFrameInternal(FrameOwner owner) {
#if defined(ARCH_AMD64)
    if (!g_current.alive) return 0;
    if (owner == FrameOwner::VmRegion &&
        g_vmRegionFramesAllocatedSinceLimit >= g_vmRegionFrameLimit) return 0;
    for (uint64_t index = 0; index < g_poolPages; ++index) {
        if (g_frameState[index] != FrameState::Free) continue;
        g_frameState[index] = owner == FrameOwner::VmRegion
            ? FrameState::VmRegion : FrameState::PageTable;
        ++g_allocatedFrames;
        if (owner == FrameOwner::VmRegion) {
            ++g_regionOwnedFrames;
            ++g_vmRegionFramesAllocatedSinceLimit;
        } else {
            ++g_pageTableFrames;
        }
        return g_poolBase + index * kPageSize;
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
    if (bootInfo == nullptr || bootInfo->RuntimeFramePoolBase == 0 ||
        bootInfo->RuntimeFramePoolPages == 0 ||
        bootInfo->RuntimeFramePoolPages > kMaxFramePoolPages ||
        !aligned(bootInfo->RuntimeFramePoolBase)) {
        return false;
    }
    if (g_current.alive) return true;
    g_poolBase = bootInfo->RuntimeFramePoolBase;
    g_poolPages = bootInfo->RuntimeFramePoolPages;
    for (uint64_t index = 0; index < kMaxFramePoolPages; ++index) {
        g_frameState[index] = FrameState::Free;
    }
    g_allocatedFrames = 0;
    g_regionOwnedFrames = 0;
    g_pageTableFrames = 0;
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
        !frameOwnerMatches(g_frameState[index], owner)) return false;
    g_frameState[index] = FrameState::Free;
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

bool mapPage(AddressSpace* owner, uintptr_t virtualAddress,
             uint64_t physicalAddress, uint64_t flags) {
#if defined(ARCH_AMD64)
    if (owner == nullptr || owner != &g_current || !owner->alive ||
        !aligned(virtualAddress) || !aligned(physicalAddress) ||
        (flags & kPtePageSize) != 0) return false;
    uint64_t* entry = nullptr;
    if (!pageTableEntry(virtualAddress, &entry, true) || *entry != 0) return false;
    *entry = (physicalAddress & kPageMask) | (flags & ~kPageMask);
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
    result.totalKnownFrames = g_poolPages;
    result.allocatedFrames = g_allocatedFrames;
    result.freeFrames = g_poolPages >= g_allocatedFrames
        ? g_poolPages - g_allocatedFrames : 0;
    result.regionOwnedFrames = g_regionOwnedFrames;
    result.pageTableFrames = g_pageTableFrames;
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
