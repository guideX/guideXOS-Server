#include "guidexos_virtual_memory_region.h"

#if defined(GXOS_BARE_METAL) && defined(GXOS_TRUE_VIRTUAL_MEMORY)

#include "kernel/address_space.h"

namespace gxos_vm_true_compat {
using size_t = ::size_t;
using uint8_t = ::uint8_t;
using uint64_t = ::uint64_t;
using uintptr_t = ::uintptr_t;
}
#define std gxos_vm_true_compat

namespace gxos {
namespace runtime {
namespace virtual_memory {

namespace detail {
struct RegionAccess {
    static void*& state(VirtualMemoryRegion& region) { return region.opaqueState; }
    static std::uint64_t& generation(VirtualMemoryRegion& region) {
        return region.generation;
    }
};
} // namespace detail

namespace {

using kernel::memory::address_space::AddressSpace;
using kernel::memory::address_space::FrameOwner;
using kernel::memory::address_space::FrameReleaseReason;
using kernel::memory::address_space::FrameAccounting;
using kernel::memory::address_space::MappingInfo;

constexpr std::size_t kPageSize = 4096;
constexpr std::uintptr_t kRuntimeRangeBase = 0x100000000ULL;
constexpr std::size_t kRuntimeRangeSize = 64 * 1024 * 1024;
constexpr std::size_t kMaxRegions = 32;
constexpr std::size_t kMaxRegionPages = 1024;
constexpr std::size_t kMaxRegionSize = kMaxRegionPages * kPageSize;

constexpr std::uint64_t kPtePresent = 1ULL << 0;
constexpr std::uint64_t kPteWritable = 1ULL << 1;
constexpr std::uint64_t kPteNoExecute = 1ULL << 63;

struct PageRecord {
    bool committed;
    std::uint64_t physicalAddress;
    MemoryProtection protection;
};

struct BareRegionState {
    bool active;
    AddressSpace* owner;
    std::uintptr_t base;
    std::size_t size;
    std::size_t pageCount;
    std::uint64_t generation;
    std::size_t committedPages;
    PageRecord pages[kMaxRegionPages];
};

BareRegionState g_regions[kMaxRegions] = {};
std::uint64_t g_nextGeneration = 1;
const char* g_diagnostic = "bare-metal true reservation backend: not initialized";

void diagnostic(const char* message) {
    g_diagnostic = message == nullptr ? "bare-metal VM diagnostic unavailable" : message;
}

bool isPowerOfTwo(std::size_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

bool checkedAdd(std::size_t left, std::size_t right, std::size_t* result) {
    if (result == nullptr || right > static_cast<std::size_t>(-1) - left) return false;
    *result = left + right;
    return true;
}

bool roundUp(std::size_t value, std::size_t alignment, std::size_t* result) {
    if (alignment == 0) return false;
    const std::size_t remainder = value % alignment;
    if (remainder == 0) {
        if (result != nullptr) *result = value;
        return result != nullptr;
    }
    return checkedAdd(value, alignment - remainder, result);
}

bool validPageRange(const BareRegionState& state, std::size_t offset,
                    std::size_t size, std::size_t* firstPage,
                    std::size_t* pageCount) {
    if (size == 0 || (offset % kPageSize) != 0 || (size % kPageSize) != 0 ||
        offset > state.size || size > state.size - offset) return false;
    if (firstPage != nullptr) *firstPage = offset / kPageSize;
    if (pageCount != nullptr) *pageCount = size / kPageSize;
    return true;
}

void clearRegion(VirtualMemoryRegion& region) {
    region.base = nullptr;
    region.reservedSize = 0;
    region.committedSize = 0;
    detail::RegionAccess::state(region) = nullptr;
    detail::RegionAccess::generation(region) = 0;
}

BareRegionState* stateFor(VirtualMemoryRegion& region) {
    if (detail::RegionAccess::state(region) == nullptr ||
        detail::RegionAccess::generation(region) == 0) return nullptr;
    BareRegionState* state = static_cast<BareRegionState*>(
        detail::RegionAccess::state(region));
    AddressSpace* owner = kernel::memory::address_space::current();
    for (std::size_t index = 0; index < kMaxRegions; ++index) {
        if (&g_regions[index] == state && state->active &&
            state->owner == owner && state->generation ==
                detail::RegionAccess::generation(region)) {
            return state;
        }
    }
    return nullptr;
}

BareRegionState* stateForAddress(const void* address, std::size_t* pageIndex) {
    const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(address);
    AddressSpace* owner = kernel::memory::address_space::current();
    if (owner == nullptr || value < kRuntimeRangeBase ||
        value >= kRuntimeRangeBase + kRuntimeRangeSize) return nullptr;
    for (std::size_t index = 0; index < kMaxRegions; ++index) {
        BareRegionState& state = g_regions[index];
        if (!state.active || state.owner != owner) continue;
        if (value >= state.base && value < state.base + state.size) {
            if (pageIndex != nullptr) *pageIndex = (value - state.base) / kPageSize;
            return &state;
        }
    }
    return nullptr;
}

bool overlaps(const BareRegionState& state, std::uintptr_t base,
              std::size_t size) {
    return state.active && base < state.base + state.size &&
        state.base < base + size;
}

bool supportedProtection(MemoryProtection protection) {
    return protection == MemoryProtection::NoAccess ||
        protection == MemoryProtection::ReadOnly ||
        protection == MemoryProtection::ReadWrite;
}

std::uint64_t pageFlags(MemoryProtection protection) {
    switch (protection) {
    case MemoryProtection::NoAccess:
        // Retain the physical identity in the PTE but clear Present. This
        // keeps ownership intact while making the virtual page fault.
        return kPteNoExecute;
    case MemoryProtection::ReadOnly:
        return kPtePresent | kPteNoExecute;
    case MemoryProtection::ReadWrite:
        return kPtePresent | kPteWritable | kPteNoExecute;
    default:
        return 0;
    }
}

bool mappingMatches(const BareRegionState& state, std::size_t pageIndex,
                    MappingInfo* mapping) {
    const bool hasMapping = kernel::memory::address_space::queryPage(
        state.owner, state.base + pageIndex * kPageSize, mapping);
    const PageRecord& page = state.pages[pageIndex];
    if (!page.committed) return !hasMapping || !mapping->present;
    return hasMapping && mapping->hasEntry &&
        mapping->physicalAddress == page.physicalAddress &&
        mapping->present == (page.protection != MemoryProtection::NoAccess);
}

bool mapCommittedPage(BareRegionState& state, std::size_t pageIndex,
                      std::uint64_t physicalAddress,
                      MemoryProtection protection) {
    const std::uintptr_t address = state.base + pageIndex * kPageSize;
    return kernel::memory::address_space::mapPage(
        state.owner, address, physicalAddress, pageFlags(protection));
}

void rollbackNewPages(BareRegionState& state, const std::uint8_t* newly,
                      std::size_t firstPage, std::size_t pageCount) {
    for (std::size_t index = 0; index < pageCount; ++index) {
        if (newly[index] == 0) continue;
        const std::size_t pageIndex = firstPage + index;
        const std::uintptr_t address = state.base + pageIndex * kPageSize;
        (void)kernel::memory::address_space::unmapPage(state.owner, address, nullptr);
        const std::uint64_t physical = state.pages[pageIndex].physicalAddress;
        if (physical != 0) {
            (void)kernel::memory::address_space::zeroFrame(physical);
            (void)kernel::memory::address_space::releaseFrame(
                physical, FrameOwner::VmRegion, FrameReleaseReason::Other);
        }
        state.pages[pageIndex] = PageRecord{};
    }
}

bool releaseState(BareRegionState& state, FrameReleaseReason reason) {
    // Remove all virtual mappings first. A committed NoAccess page still has
    // a non-present physical PTE and is cleared by unmapPage as well.
    for (std::size_t index = 0; index < state.pageCount; ++index) {
        if (!state.pages[index].committed) continue;
        MappingInfo removed{};
        if (!kernel::memory::address_space::unmapPage(
                state.owner, state.base + index * kPageSize, &removed)) {
            diagnostic("page-table unmap failed during region release");
            return false;
        }
        if (removed.hasEntry && removed.physicalAddress !=
                state.pages[index].physicalAddress) {
            diagnostic("page-table frame disagrees with region metadata");
            return false;
        }
    }
    for (std::size_t index = 0; index < state.pageCount; ++index) {
        if (!state.pages[index].committed) continue;
        const std::uint64_t physical = state.pages[index].physicalAddress;
        (void)kernel::memory::address_space::zeroFrame(physical);
        if (!kernel::memory::address_space::releaseFrame(
                physical, FrameOwner::VmRegion, reason)) {
            diagnostic("physical frame release failed during region release");
            return false;
        }
        state.pages[index] = PageRecord{};
    }
    state.committedPages = 0;
    state.active = false;
    state.owner = nullptr;
    state.generation = 0;
    return true;
}

} // namespace

VirtualMemoryRegion::VirtualMemoryRegion()
    : base(nullptr), reservedSize(0), committedSize(0),
      opaqueState(nullptr), generation(0) {}

VirtualMemoryRegion::~VirtualMemoryRegion() {
    if (detail::RegionAccess::state(*this) != nullptr) (void)release(*this);
}

VmResult reserve(std::size_t size, std::size_t alignment, void* preferredBase,
                 VirtualMemoryRegion* region) {
    if (region == nullptr) return VmResult::InvalidArgument;
    if (detail::RegionAccess::state(*region) != nullptr) return VmResult::AlreadyReserved;
    AddressSpace* owner = kernel::memory::address_space::current();
    if (owner == nullptr) {
        diagnostic("true reservation backend has no initialized address space");
        return VmResult::Unsupported;
    }
    if (size == 0) return VmResult::InvalidArgument;
    if (alignment == 0) alignment = kPageSize;
    if (!isPowerOfTwo(alignment) || alignment < kPageSize) return VmResult::AlignmentError;
    std::size_t roundedSize = 0;
    if (!roundUp(size, kPageSize, &roundedSize) || roundedSize == 0 ||
        roundedSize > kMaxRegionSize) return VmResult::RangeOverflow;

    std::uintptr_t requested = 0;
    if (preferredBase != nullptr) {
        requested = reinterpret_cast<std::uintptr_t>(preferredBase);
        if ((requested % kPageSize) != 0 || (requested % alignment) != 0) {
            diagnostic("preferred base alignment rejected");
            return VmResult::AlignmentError;
        }
        if (requested < kRuntimeRangeBase ||
            requested > kRuntimeRangeBase + kRuntimeRangeSize - roundedSize) {
            diagnostic("preferred base is outside the runtime reservation range");
            return VmResult::AddressUnavailable;
        }
    }

    std::uintptr_t selected = 0;
    const std::uintptr_t end = kRuntimeRangeBase + kRuntimeRangeSize - roundedSize;
    const std::uintptr_t first = preferredBase == nullptr ? kRuntimeRangeBase : requested;
    for (std::uintptr_t candidate = first; candidate <= end; candidate += kPageSize) {
        if ((candidate % alignment) != 0) {
            if (preferredBase != nullptr) break;
            continue;
        }
        bool free = true;
        for (std::size_t index = 0; index < kMaxRegions; ++index) {
            if (overlaps(g_regions[index], candidate, roundedSize)) {
                free = false;
                break;
            }
        }
        if (free && !kernel::memory::address_space::rangeHasPresentMapping(
                owner, candidate, roundedSize / kPageSize)) {
            selected = candidate;
            break;
        }
        if (preferredBase != nullptr) break;
    }
    if (selected == 0) {
        diagnostic("requested virtual interval is unavailable");
        return VmResult::AddressUnavailable;
    }

    BareRegionState* state = nullptr;
    for (std::size_t index = 0; index < kMaxRegions; ++index) {
        if (!g_regions[index].active) {
            state = &g_regions[index];
            break;
        }
    }
    if (state == nullptr) {
        diagnostic("reservation metadata pool exhausted");
        return VmResult::OutOfMemory;
    }

    *state = BareRegionState{};
    state->active = true;
    state->owner = owner;
    state->base = selected;
    state->size = roundedSize;
    state->pageCount = roundedSize / kPageSize;
    state->generation = g_nextGeneration++;
    if (state->generation == 0) state->generation = g_nextGeneration++;

    region->base = reinterpret_cast<void*>(selected);
    region->reservedSize = roundedSize;
    region->committedSize = 0;
    detail::RegionAccess::state(*region) = state;
    detail::RegionAccess::generation(*region) = state->generation;
    diagnostic("true unbacked reservation created");
    return VmResult::Ok;
}

VmResult commit(VirtualMemoryRegion& region, std::size_t offset,
                std::size_t size, MemoryProtection protection) {
    BareRegionState* state = stateFor(region);
    if (state == nullptr) return VmResult::AlreadyReleased;
    if (!supportedProtection(protection)) return VmResult::ProtectionUnsupported;
    std::size_t firstPage = 0;
    std::size_t pageCount = 0;
    if (!validPageRange(*state, offset, size, &firstPage, &pageCount)) {
        return (size == 0 || (offset % kPageSize) != 0 ||
                (size % kPageSize) != 0) ? VmResult::AlignmentError
                                          : VmResult::OutOfRange;
    }

    std::uint8_t newly[kMaxRegionPages] = {};
    std::size_t newCount = 0;
    for (std::size_t index = 0; index < pageCount; ++index) {
        const std::size_t pageIndex = firstPage + index;
        if (state->pages[pageIndex].committed) continue;
        const std::uint64_t physical = kernel::memory::address_space::allocateFrame(
            FrameOwner::VmRegion);
        if (physical == 0 || !kernel::memory::address_space::zeroFrame(physical)) {
            rollbackNewPages(*state, newly, firstPage, pageCount);
            diagnostic("physical frame allocation exhausted during commit");
            return VmResult::OutOfMemory;
        }
        state->pages[pageIndex].physicalAddress = physical;
        if (!mapCommittedPage(*state, pageIndex, physical, protection)) {
            (void)kernel::memory::address_space::releaseFrame(
                physical, FrameOwner::VmRegion, FrameReleaseReason::Other);
            state->pages[pageIndex] = PageRecord{};
            rollbackNewPages(*state, newly, firstPage, pageCount);
            diagnostic("page-table mapping failed during commit");
            return VmResult::HostFailure;
        }
        newly[index] = 1;
        ++newCount;
    }

    std::uint8_t changed[kMaxRegionPages] = {};
    std::size_t changedCount = 0;
    for (std::size_t index = 0; index < pageCount; ++index) {
        const std::size_t pageIndex = firstPage + index;
        if (!state->pages[pageIndex].committed ||
            state->pages[pageIndex].protection == protection) continue;
        if (!kernel::memory::address_space::updatePageFlags(
                state->owner, state->base + pageIndex * kPageSize,
                pageFlags(protection))) {
            for (std::size_t rollback = 0; rollback < pageCount; ++rollback) {
                const std::size_t rollbackPage = firstPage + rollback;
                if (changed[rollback] != 0) {
                    (void)kernel::memory::address_space::updatePageFlags(
                        state->owner, state->base + rollbackPage * kPageSize,
                        pageFlags(state->pages[rollbackPage].protection));
                }
            }
            rollbackNewPages(*state, newly, firstPage, pageCount);
            diagnostic("protection transition failed during commit");
            return VmResult::HostFailure;
        }
        changed[index] = 1;
        ++changedCount;
    }

    (void)newCount;
    (void)changedCount;
    for (std::size_t index = 0; index < pageCount; ++index) {
        const std::size_t pageIndex = firstPage + index;
        if (newly[index] != 0) {
            state->pages[pageIndex].committed = true;
            state->pages[pageIndex].protection = protection;
            ++state->committedPages;
        } else if (changed[index] != 0) {
            state->pages[pageIndex].protection = protection;
        }
    }
    region.committedSize = state->committedPages * kPageSize;
    diagnostic("commit mapped owned zeroed frames");
    return VmResult::Ok;
}

VmResult decommit(VirtualMemoryRegion& region, std::size_t offset,
                  std::size_t size) {
    BareRegionState* state = stateFor(region);
    if (state == nullptr) return VmResult::AlreadyReleased;
    std::size_t firstPage = 0;
    std::size_t pageCount = 0;
    if (!validPageRange(*state, offset, size, &firstPage, &pageCount)) {
        return (size == 0 || (offset % kPageSize) != 0 ||
                (size % kPageSize) != 0) ? VmResult::AlignmentError
                                          : VmResult::OutOfRange;
    }
    for (std::size_t index = 0; index < pageCount; ++index) {
        const std::size_t pageIndex = firstPage + index;
        PageRecord& page = state->pages[pageIndex];
        if (!page.committed) continue;
        MappingInfo removed{};
        if (!kernel::memory::address_space::unmapPage(
                state->owner, state->base + pageIndex * kPageSize, &removed) ||
            !removed.hasEntry || removed.physicalAddress != page.physicalAddress) {
            diagnostic("page-table unmap disagrees during decommit");
            return VmResult::HostFailure;
        }
        const std::uint64_t physical = page.physicalAddress;
        (void)kernel::memory::address_space::zeroFrame(physical);
        if (!kernel::memory::address_space::releaseFrame(
                physical, FrameOwner::VmRegion, FrameReleaseReason::Decommit)) {
            diagnostic("physical frame release failed during decommit");
            return VmResult::HostFailure;
        }
        page = PageRecord{};
        --state->committedPages;
    }
    region.committedSize = state->committedPages * kPageSize;
    diagnostic("decommit removed mappings and released frames");
    return VmResult::Ok;
}

VmResult protect(VirtualMemoryRegion& region, std::size_t offset,
                 std::size_t size, MemoryProtection protection) {
    BareRegionState* state = stateFor(region);
    if (state == nullptr) return VmResult::AlreadyReleased;
    if (!supportedProtection(protection)) return VmResult::ProtectionUnsupported;
    std::size_t firstPage = 0;
    std::size_t pageCount = 0;
    if (!validPageRange(*state, offset, size, &firstPage, &pageCount)) {
        return (size == 0 || (offset % kPageSize) != 0 ||
                (size % kPageSize) != 0) ? VmResult::AlignmentError
                                          : VmResult::OutOfRange;
    }
    for (std::size_t index = 0; index < pageCount; ++index) {
        if (!state->pages[firstPage + index].committed) return VmResult::NotCommitted;
    }
    std::uint8_t changed[kMaxRegionPages] = {};
    for (std::size_t index = 0; index < pageCount; ++index) {
        const std::size_t pageIndex = firstPage + index;
        if (state->pages[pageIndex].protection == protection) continue;
        if (!kernel::memory::address_space::updatePageFlags(
                state->owner, state->base + pageIndex * kPageSize,
                pageFlags(protection))) {
            for (std::size_t rollback = 0; rollback < pageCount; ++rollback) {
                const std::size_t rollbackPage = firstPage + rollback;
                if (changed[rollback] != 0) {
                    (void)kernel::memory::address_space::updatePageFlags(
                        state->owner, state->base + rollbackPage * kPageSize,
                        pageFlags(state->pages[rollbackPage].protection));
                }
            }
            diagnostic("page-table protection transition failed");
            return VmResult::HostFailure;
        }
        changed[index] = 1;
    }
    for (std::size_t index = 0; index < pageCount; ++index) {
        if (changed[index] != 0) state->pages[firstPage + index].protection = protection;
    }
    diagnostic("page-table protection updated");
    return VmResult::Ok;
}

VmResult release(VirtualMemoryRegion& region) {
    BareRegionState* state = stateFor(region);
    if (state == nullptr) {
        clearRegion(region);
        return VmResult::AlreadyReleased;
    }
    const bool released = releaseState(*state, FrameReleaseReason::Release);
    if (!released) return VmResult::HostFailure;
    clearRegion(region);
    diagnostic("region released; virtual range and frames returned");
    return VmResult::Ok;
}

VmResult query(const void* address, VirtualMemoryInfo* information) {
    if (address == nullptr || information == nullptr) return VmResult::InvalidArgument;
    *information = VirtualMemoryInfo{};
    std::size_t pageIndex = 0;
    BareRegionState* state = stateForAddress(address, &pageIndex);
    if (state == nullptr) return VmResult::NotFound;

    MappingInfo mapping{};
    if (!mappingMatches(*state, pageIndex, &mapping)) {
        diagnostic("query detected metadata/page-table disagreement");
        return VmResult::HostFailure;
    }
    const std::uintptr_t pageBase = state->base + pageIndex * kPageSize;
    information->regionBase = reinterpret_cast<void*>(state->base);
    information->pageBase = reinterpret_cast<void*>(pageBase);
    information->pageSize = kPageSize;
    information->regionSize = kPageSize;
    information->reservedSize = state->size;
    information->committedSize = state->committedPages * kPageSize;
    information->generation = state->generation;
    information->reserved = true;
    information->committed = state->pages[pageIndex].committed;
    information->mappingPresent = mapping.present;
    information->physicalFrame = state->pages[pageIndex].committed
        ? state->pages[pageIndex].physicalAddress : 0;
    information->protection = information->committed
        ? state->pages[pageIndex].protection : MemoryProtection::NoAccess;
    return VmResult::Ok;
}

VmResult teardownAddressSpace() {
    AddressSpace* owner = kernel::memory::address_space::current();
    if (owner == nullptr) return VmResult::AlreadyReleased;
    for (std::size_t index = 0; index < kMaxRegions; ++index) {
        BareRegionState& state = g_regions[index];
        if (!state.active || state.owner != owner) continue;
        if (!releaseState(state, FrameReleaseReason::Release)) {
            return VmResult::HostFailure;
        }
    }
    kernel::memory::address_space::destroyCurrent();
    diagnostic("address-space teardown released all reservations and frames");
    return VmResult::Ok;
}

std::size_t pageSize() { return kPageSize; }
std::size_t allocationGranularity() { return kPageSize; }
std::size_t maximumRegionSize() { return kMaxRegionSize; }

VirtualMemoryStats stats() {
    VirtualMemoryStats result{};
    for (std::size_t index = 0; index < kMaxRegions; ++index) {
        if (!g_regions[index].active) continue;
        ++result.activeRegions;
        result.committedPages += g_regions[index].committedPages;
        for (std::size_t page = 0; page < g_regions[index].pageCount; ++page) {
            if (g_regions[index].pages[page].committed) ++result.mappingCount;
        }
    }
    const FrameAccounting frames = kernel::memory::address_space::accounting();
    result.totalKnownFrames = frames.totalKnownFrames;
    result.freeFrames = frames.freeFrames;
    result.allocatedFrames = frames.allocatedFrames;
    result.regionOwnedFrames = frames.regionOwnedFrames;
    result.pageTableFrames = frames.pageTableFrames;
    result.framesReleasedByDecommit = frames.framesReleasedByDecommit;
    result.framesReleasedByRelease = frames.framesReleasedByRelease;
    result.tlbInvalidations = frames.tlbInvalidations;
    result.activeMetadataEntries = result.activeRegions;
    result.metadataCapacity = kMaxRegions;
    result.trueReservation = true;
    result.physicalBackingAccounting = true;
    result.protectionEnforced = true;
    return result;
}

bool protectionIsEnforced() { return true; }

const char* lastDiagnostic() { return g_diagnostic; }

} // namespace virtual_memory
} // namespace runtime
} // namespace gxos

#undef std

#endif // GXOS_BARE_METAL && GXOS_TRUE_VIRTUAL_MEMORY
