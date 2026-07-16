#include "guidexos_virtual_memory_region.h"

#if defined(GXOS_BARE_METAL)
namespace gxos_vm_compat {
using size_t = ::size_t;
using uint8_t = ::uint8_t;
using uint64_t = ::uint64_t;
using uintptr_t = ::uintptr_t;
}
#define std gxos_vm_compat
#endif

#if defined(GXOS_BARE_METAL)

namespace gxos {
namespace runtime {
namespace virtual_memory {

namespace detail {
struct RegionAccess {
    static void*& state(VirtualMemoryRegion& region) { return region.opaqueState; }
    static std::uint64_t& generation(VirtualMemoryRegion& region) { return region.generation; }
};
} // namespace detail

namespace {

constexpr std::size_t kPageSize = 4096;
constexpr std::size_t kArenaSize = 256 * 1024;
constexpr std::size_t kMaxPages = kArenaSize / kPageSize;
constexpr std::size_t kMaxRegions = 8;

alignas(kPageSize) std::uint8_t g_arena[kArenaSize] = {};

struct BareRegionState {
    bool active;
    std::size_t offset;
    std::size_t size;
    std::size_t committedPages;
    std::uint64_t generation;
    std::uint8_t committed[kMaxPages];
};

BareRegionState g_regions[kMaxRegions] = {};
std::uint64_t g_nextGeneration = 1;

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

void zeroMemory(std::uint8_t* address, std::size_t size) {
    for (std::size_t index = 0; index < size; ++index) address[index] = 0;
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
    BareRegionState* state = static_cast<BareRegionState*>(detail::RegionAccess::state(region));
    for (std::size_t index = 0; index < kMaxRegions; ++index) {
        if (&g_regions[index] == state && state->active &&
            state->generation == detail::RegionAccess::generation(region)) {
            return state;
        }
    }
    return nullptr;
}

const BareRegionState* stateForAddress(const void* address, std::size_t* pageIndex) {
    const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(address);
    const std::uintptr_t arenaBase = reinterpret_cast<std::uintptr_t>(g_arena);
    const std::uintptr_t arenaEnd = arenaBase + kArenaSize;
    if (value < arenaBase || value >= arenaEnd) return nullptr;

    for (std::size_t index = 0; index < kMaxRegions; ++index) {
        const BareRegionState& state = g_regions[index];
        if (!state.active) continue;
        const std::uintptr_t base = arenaBase + state.offset;
        if (value >= base && value < base + state.size) {
            if (pageIndex != nullptr) *pageIndex = (value - base) / kPageSize;
            return &state;
        }
    }
    return nullptr;
}

bool overlaps(const BareRegionState& left, std::size_t offset, std::size_t size) {
    if (!left.active) return false;
    return offset < left.offset + left.size && left.offset < offset + size;
}

} // namespace

VirtualMemoryRegion::VirtualMemoryRegion()
    : base(nullptr), reservedSize(0), committedSize(0),
      opaqueState(nullptr), generation(0) {
}

VirtualMemoryRegion::~VirtualMemoryRegion() {
    if (detail::RegionAccess::state(*this) != nullptr) (void)release(*this);
}

VmResult reserve(std::size_t size, std::size_t alignment, void* preferredBase,
                 VirtualMemoryRegion* region) {
    if (region == nullptr) return VmResult::InvalidArgument;
    if (region->opaqueState != nullptr) return VmResult::AlreadyReserved;
    if (size == 0) return VmResult::InvalidArgument;
    if (alignment == 0) alignment = kPageSize;
    if (!isPowerOfTwo(alignment) || alignment < kPageSize) return VmResult::AlignmentError;
    if (size > kArenaSize) return VmResult::OutOfMemory;

    std::size_t roundedSize = 0;
    if (!roundUp(size, kPageSize, &roundedSize) || roundedSize == 0 ||
        roundedSize > kArenaSize) return VmResult::RangeOverflow;

    std::size_t requestedOffset = 0;
    if (preferredBase != nullptr) {
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(g_arena);
        const std::uintptr_t preferred = reinterpret_cast<std::uintptr_t>(preferredBase);
        if (preferred < base || preferred >= base + kArenaSize ||
            ((preferred - base) % kPageSize) != 0 || (preferred % alignment) != 0) {
            return VmResult::AlignmentError;
        }
        requestedOffset = static_cast<std::size_t>(preferred - base);
        if (requestedOffset > kArenaSize - roundedSize) return VmResult::AddressUnavailable;
    }

    std::size_t selectedOffset = kArenaSize;
    for (std::size_t candidate = requestedOffset;
         candidate <= kArenaSize - roundedSize;
         candidate += kPageSize) {
        const std::uintptr_t candidateAddress =
            reinterpret_cast<std::uintptr_t>(g_arena) + candidate;
        if ((candidateAddress % alignment) != 0) continue;
        bool free = true;
        for (std::size_t index = 0; index < kMaxRegions; ++index) {
            if (overlaps(g_regions[index], candidate, roundedSize)) {
                free = false;
                break;
            }
        }
        if (free) {
            selectedOffset = candidate;
            break;
        }
        if (preferredBase != nullptr) break;
    }
    if (selectedOffset == kArenaSize) return VmResult::AddressUnavailable;

    BareRegionState* state = nullptr;
    for (std::size_t index = 0; index < kMaxRegions; ++index) {
        if (!g_regions[index].active) {
            state = &g_regions[index];
            break;
        }
    }
    if (state == nullptr) return VmResult::OutOfMemory;

    *state = BareRegionState{};
    state->active = true;
    state->offset = selectedOffset;
    state->size = roundedSize;
    state->generation = g_nextGeneration++;
    if (state->generation == 0) state->generation = g_nextGeneration++;

    region->base = g_arena + selectedOffset;
    region->reservedSize = roundedSize;
    region->committedSize = 0;
    detail::RegionAccess::state(*region) = state;
    detail::RegionAccess::generation(*region) = state->generation;
    return VmResult::Ok;
}

VmResult commit(VirtualMemoryRegion& region, std::size_t offset, std::size_t size,
                MemoryProtection protection) {
    BareRegionState* state = stateFor(region);
    if (state == nullptr) return VmResult::AlreadyReleased;
    if (size == 0 || (offset % kPageSize) != 0 || (size % kPageSize) != 0) {
        return VmResult::AlignmentError;
    }
    if (offset > state->size || size > state->size - offset) return VmResult::OutOfRange;
    // The current kernel has no page-table protection primitive.  Keep the
    // compatibility backend honest: only ordinary read/write is accepted.
    if (protection != MemoryProtection::ReadWrite) return VmResult::ProtectionUnsupported;

    const std::size_t firstPage = offset / kPageSize;
    const std::size_t pageCount = size / kPageSize;
    for (std::size_t page = 0; page < pageCount; ++page) {
        const std::size_t index = firstPage + page;
        if (state->committed[index] != 0) continue;
        zeroMemory(g_arena + state->offset + index * kPageSize, kPageSize);
        state->committed[index] = 1;
        ++state->committedPages;
    }
    region.committedSize = state->committedPages * kPageSize;
    return VmResult::Ok;
}

VmResult decommit(VirtualMemoryRegion& region, std::size_t offset, std::size_t size) {
    BareRegionState* state = stateFor(region);
    if (state == nullptr) return VmResult::AlreadyReleased;
    if (size == 0 || (offset % kPageSize) != 0 || (size % kPageSize) != 0) {
        return VmResult::AlignmentError;
    }
    if (offset > state->size || size > state->size - offset) return VmResult::OutOfRange;
    const std::size_t firstPage = offset / kPageSize;
    const std::size_t pageCount = size / kPageSize;
    for (std::size_t page = 0; page < pageCount; ++page) {
        const std::size_t index = firstPage + page;
        if (state->committed[index] == 0) continue;
        zeroMemory(g_arena + state->offset + index * kPageSize, kPageSize);
        state->committed[index] = 0;
        --state->committedPages;
    }
    region.committedSize = state->committedPages * kPageSize;
    return VmResult::Ok;
}

VmResult protect(VirtualMemoryRegion& region, std::size_t offset, std::size_t size,
                 MemoryProtection protection) {
    BareRegionState* state = stateFor(region);
    if (state == nullptr) return VmResult::AlreadyReleased;
    if (size == 0 || (offset % kPageSize) != 0 || (size % kPageSize) != 0) {
        return VmResult::AlignmentError;
    }
    if (offset > state->size || size > state->size - offset) return VmResult::OutOfRange;
    (void)protection;
    return VmResult::Unsupported;
}

VmResult release(VirtualMemoryRegion& region) {
    BareRegionState* state = stateFor(region);
    if (state == nullptr) {
        clearRegion(region);
        return VmResult::AlreadyReleased;
    }
    state->active = false;
    *state = BareRegionState{};
    clearRegion(region);
    return VmResult::Ok;
}

VmResult query(const void* address, VirtualMemoryInfo* information) {
    if (address == nullptr || information == nullptr) return VmResult::InvalidArgument;
    *information = VirtualMemoryInfo{};
    std::size_t pageIndex = 0;
    const BareRegionState* state = stateForAddress(address, &pageIndex);
    if (state == nullptr) return VmResult::NotFound;
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(g_arena) + state->offset;
    information->regionBase = reinterpret_cast<void*>(base);
    information->pageBase = reinterpret_cast<void*>(base + pageIndex * kPageSize);
    information->pageSize = kPageSize;
    information->regionSize = kPageSize;
    information->reservedSize = state->size;
    information->committedSize = state->committedPages * kPageSize;
    information->generation = state->generation;
    information->reserved = true;
    information->committed = state->committed[pageIndex] != 0;
    information->protection = information->committed
        ? MemoryProtection::ReadWrite : MemoryProtection::NoAccess;
    return VmResult::Ok;
}

std::size_t pageSize() { return kPageSize; }
std::size_t allocationGranularity() { return kPageSize; }
std::size_t maximumRegionSize() { return kArenaSize; }

VirtualMemoryStats stats() {
    VirtualMemoryStats result{};
    for (std::size_t index = 0; index < kMaxRegions; ++index) {
        if (!g_regions[index].active) continue;
        ++result.activeRegions;
        result.committedPages += g_regions[index].committedPages;
    }
    result.physicalBackingAccounting = false;
    result.protectionEnforced = false;
    return result;
}

bool protectionIsEnforced() { return false; }

const char* lastDiagnostic() {
    return "bare-metal compatibility backend: reservation is eagerly backed; page-table protection is unavailable";
}

} // namespace virtual_memory
} // namespace runtime
} // namespace gxos

#else

#define WIN32_LEAN_AND_MEAN
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <cerrno>
#endif

#include <cstdio>
#include <cstring>
#include <new>
#include <mutex>

namespace gxos {
namespace runtime {
namespace virtual_memory {

namespace detail {
struct RegionAccess {
    static void*& state(VirtualMemoryRegion& region) { return region.opaqueState; }
    static std::uint64_t& generation(VirtualMemoryRegion& region) { return region.generation; }
};
} // namespace detail

namespace {

struct HostRegionState {
    void* base;
    std::size_t size;
    std::size_t pageCount;
    std::size_t committedPages;
    std::uint8_t* committed;
    std::uint8_t* protections;
    std::uint64_t generation;
    std::uint64_t processOwner;
    HostRegionState* next;
};

std::mutex g_lock;
HostRegionState* g_regions = nullptr;
std::uint64_t g_nextGeneration = 1;
thread_local char g_diagnostic[192] = "";

void setDiagnostic(const char* text) {
    if (text == nullptr) text = "";
    std::strncpy(g_diagnostic, text, sizeof(g_diagnostic) - 1);
    g_diagnostic[sizeof(g_diagnostic) - 1] = '\0';
}

void setDiagnosticWithCode(const char* operation, long code) {
    std::snprintf(g_diagnostic, sizeof(g_diagnostic), "%s failed (host code %ld)", operation, code);
}

bool isPowerOfTwo(std::size_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

bool checkedAdd(std::uintptr_t left, std::size_t right, std::uintptr_t* result) {
    if (result == nullptr || right > static_cast<std::uintptr_t>(-1) - left) return false;
    *result = left + static_cast<std::uintptr_t>(right);
    return true;
}

bool checkedAddSize(std::size_t left, std::size_t right, std::size_t* result) {
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
    return checkedAddSize(value, alignment - remainder, result);
}

std::size_t hostPageSize() {
#if defined(_WIN32)
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    return info.dwPageSize == 0 ? 4096 : static_cast<std::size_t>(info.dwPageSize);
#else
    const long value = sysconf(_SC_PAGESIZE);
    return value > 0 ? static_cast<std::size_t>(value) : 4096;
#endif
}

std::size_t hostAllocationGranularity() {
#if defined(_WIN32)
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    return info.dwAllocationGranularity == 0
        ? hostPageSize() : static_cast<std::size_t>(info.dwAllocationGranularity);
#else
    return hostPageSize();
#endif
}

std::uint64_t processOwner() {
#if defined(_WIN32)
    return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint64_t>(getpid());
#endif
}

void clearRegion(VirtualMemoryRegion& region) {
    region.base = nullptr;
    region.reservedSize = 0;
    region.committedSize = 0;
    detail::RegionAccess::state(region) = nullptr;
    detail::RegionAccess::generation(region) = 0;
}

HostRegionState* stateForLocked(VirtualMemoryRegion& region) {
    if (detail::RegionAccess::state(region) == nullptr ||
        detail::RegionAccess::generation(region) == 0) return nullptr;
    for (HostRegionState* state = g_regions; state != nullptr; state = state->next) {
        if (state == detail::RegionAccess::state(region) &&
            state->generation == detail::RegionAccess::generation(region) &&
            state->processOwner == processOwner()) return state;
    }
    return nullptr;
}

HostRegionState* stateForAddressLocked(const void* address, std::size_t* pageIndex) {
    const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(address);
    for (HostRegionState* state = g_regions; state != nullptr; state = state->next) {
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(state->base);
        std::uintptr_t end = 0;
        if (!checkedAdd(base, state->size, &end)) continue;
        if (value >= base && value < end) {
            if (pageIndex != nullptr) {
                *pageIndex = static_cast<std::size_t>(value - base) / hostPageSize();
            }
            return state;
        }
    }
    return nullptr;
}

bool validPageRange(const HostRegionState& state, std::size_t offset,
                    std::size_t size, std::size_t* firstPage, std::size_t* pageCount) {
    const std::size_t page = hostPageSize();
    if (size == 0 || offset % page != 0 || size % page != 0) return false;
    if (offset > state.size || size > state.size - offset) return false;
    if (firstPage != nullptr) *firstPage = offset / page;
    if (pageCount != nullptr) *pageCount = size / page;
    return true;
}

void releaseMetadata(HostRegionState* state) {
    delete[] state->committed;
    delete[] state->protections;
    delete state;
}

#if defined(_WIN32)
DWORD nativeProtection(MemoryProtection protection) {
    switch (protection) {
    case MemoryProtection::NoAccess: return PAGE_NOACCESS;
    case MemoryProtection::ReadOnly: return PAGE_READONLY;
    case MemoryProtection::ReadWrite: return PAGE_READWRITE;
    case MemoryProtection::ReadExecute: return PAGE_EXECUTE_READ;
    case MemoryProtection::ReadWriteExecute: return PAGE_EXECUTE_READWRITE;
    }
    return PAGE_NOACCESS;
}
#else
int nativeProtection(MemoryProtection protection) {
    switch (protection) {
    case MemoryProtection::NoAccess: return PROT_NONE;
    case MemoryProtection::ReadOnly: return PROT_READ;
    case MemoryProtection::ReadWrite: return PROT_READ | PROT_WRITE;
    case MemoryProtection::ReadExecute: return PROT_READ | PROT_EXEC;
    case MemoryProtection::ReadWriteExecute: return PROT_READ | PROT_WRITE | PROT_EXEC;
    }
    return PROT_NONE;
}
#endif

bool setProtection(void* address, std::size_t size, MemoryProtection protection) {
#if defined(_WIN32)
    DWORD oldProtection = 0;
    if (!VirtualProtect(address, size, nativeProtection(protection), &oldProtection)) {
        setDiagnosticWithCode("VirtualProtect", static_cast<long>(GetLastError()));
        return false;
    }
#else
    if (mprotect(address, size, nativeProtection(protection)) != 0) {
        setDiagnosticWithCode("mprotect", errno);
        return false;
    }
#endif
    return true;
}

bool commitPage(HostRegionState& state, std::size_t pageIndex,
                MemoryProtection protection) {
    const std::size_t page = hostPageSize();
    std::uint8_t* address = static_cast<std::uint8_t*>(state.base) + pageIndex * page;
#if defined(_WIN32)
    if (VirtualAlloc(address, page, MEM_COMMIT, PAGE_READWRITE) == nullptr) {
        setDiagnosticWithCode("VirtualAlloc(MEM_COMMIT)", static_cast<long>(GetLastError()));
        return false;
    }
#else
    if (mprotect(address, page, PROT_READ | PROT_WRITE) != 0) {
        setDiagnosticWithCode("mprotect(commit)", errno);
        return false;
    }
#endif
    // The host promises zero-filled newly committed pages, and the explicit
    // clear also guarantees zeroing after a reset/discard implementation.
    std::memset(address, 0, page);
    if (!setProtection(address, page, protection)) {
#if defined(_WIN32)
        (void)VirtualFree(address, page, MEM_DECOMMIT);
#else
        (void)mprotect(address, page, PROT_NONE);
#endif
        return false;
    }
    return true;
}

bool decommitPage(HostRegionState& state, std::size_t pageIndex) {
    const std::size_t page = hostPageSize();
    std::uint8_t* address = static_cast<std::uint8_t*>(state.base) + pageIndex * page;
#if defined(_WIN32)
    if (!VirtualFree(address, page, MEM_DECOMMIT)) {
        setDiagnosticWithCode("VirtualFree(MEM_DECOMMIT)", static_cast<long>(GetLastError()));
        return false;
    }
#else
    if (mprotect(address, page, PROT_NONE) != 0) {
        setDiagnosticWithCode("mprotect(decommit)", errno);
        return false;
    }
#if defined(MADV_DONTNEED)
    if (madvise(address, page, MADV_DONTNEED) != 0) {
        setDiagnosticWithCode("madvise(MADV_DONTNEED)", errno);
        return false;
    }
#endif
#endif
    return true;
}

bool restoreProtection(HostRegionState& state, std::size_t firstPage,
                       std::size_t count, const std::uint8_t* oldProtections) {
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t pageIndex = firstPage + index;
        if (!setProtection(static_cast<std::uint8_t*>(state.base) + pageIndex * hostPageSize(),
                           hostPageSize(), static_cast<MemoryProtection>(oldProtections[index]))) {
            return false;
        }
        state.protections[pageIndex] = oldProtections[index];
    }
    return true;
}

} // namespace

VirtualMemoryRegion::VirtualMemoryRegion()
    : base(nullptr), reservedSize(0), committedSize(0),
      opaqueState(nullptr), generation(0) {
}

VirtualMemoryRegion::~VirtualMemoryRegion() {
    if (detail::RegionAccess::state(*this) != nullptr) (void)release(*this);
}

VmResult reserve(std::size_t size, std::size_t alignment, void* preferredBase,
                 VirtualMemoryRegion* region) {
    if (region == nullptr) return VmResult::InvalidArgument;
    if (region->opaqueState != nullptr) return VmResult::AlreadyReserved;
    if (size == 0) return VmResult::InvalidArgument;
    const std::size_t page = hostPageSize();
    if (alignment == 0) alignment = page;
    if (!isPowerOfTwo(alignment) || alignment < page) return VmResult::AlignmentError;
    if (size > maximumRegionSize()) {
        setDiagnostic("reserve size exceeded maximum");
        return VmResult::RangeOverflow;
    }

    std::size_t roundedSize = 0;
    if (!roundUp(size, page, &roundedSize) || roundedSize == 0 ||
        roundedSize > maximumRegionSize()) return VmResult::RangeOverflow;
    if (preferredBase != nullptr) {
        const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(preferredBase);
        if ((address % page) != 0 || (address % alignment) != 0) {
            setDiagnostic("preferred base alignment rejected");
            return VmResult::AlignmentError;
        }
        std::uintptr_t end = 0;
        if (!checkedAdd(address, roundedSize, &end)) return VmResult::RangeOverflow;
        (void)end;
    }

    HostRegionState* state = new (std::nothrow) HostRegionState{};
    if (state == nullptr) return VmResult::OutOfMemory;
    state->pageCount = roundedSize / page;
    state->committed = new (std::nothrow) std::uint8_t[state->pageCount]{};
    state->protections = new (std::nothrow) std::uint8_t[state->pageCount];
    if (state->committed == nullptr || state->protections == nullptr) {
        releaseMetadata(state);
        return VmResult::OutOfMemory;
    }
    for (std::size_t index = 0; index < state->pageCount; ++index) {
        state->protections[index] = static_cast<std::uint8_t>(MemoryProtection::NoAccess);
    }

#if defined(_WIN32)
    state->base = VirtualAlloc(preferredBase, roundedSize, MEM_RESERVE, PAGE_NOACCESS);
    if (state->base == nullptr) {
        setDiagnosticWithCode("VirtualAlloc(MEM_RESERVE)", static_cast<long>(GetLastError()));
        releaseMetadata(state);
        return preferredBase == nullptr ? VmResult::HostFailure : VmResult::AddressUnavailable;
    }
#else
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#if defined(MAP_FIXED_NOREPLACE)
    if (preferredBase != nullptr) flags |= MAP_FIXED_NOREPLACE;
#endif
    state->base = mmap(preferredBase, roundedSize, PROT_NONE, flags, -1, 0);
    if (state->base == MAP_FAILED) {
        state->base = nullptr;
        setDiagnosticWithCode("mmap(MEM_RESERVE)", errno);
        releaseMetadata(state);
        return preferredBase == nullptr ? VmResult::HostFailure : VmResult::AddressUnavailable;
    }
#endif
    if (preferredBase != nullptr && state->base != preferredBase) {
#if defined(_WIN32)
        (void)VirtualFree(state->base, 0, MEM_RELEASE);
#else
        (void)munmap(state->base, roundedSize);
#endif
        setDiagnostic("host did not honor the exact preferred base");
        releaseMetadata(state);
        return VmResult::AddressUnavailable;
    }
    if ((reinterpret_cast<std::uintptr_t>(state->base) % alignment) != 0) {
#if defined(_WIN32)
        (void)VirtualFree(state->base, 0, MEM_RELEASE);
#else
        (void)munmap(state->base, roundedSize);
#endif
        setDiagnostic("host returned an address with insufficient alignment");
        releaseMetadata(state);
        return VmResult::AddressUnavailable;
    }

    state->size = roundedSize;
    state->committedPages = 0;
    state->generation = g_nextGeneration++;
    if (state->generation == 0) state->generation = g_nextGeneration++;
    state->processOwner = processOwner();

    {
        std::lock_guard<std::mutex> guard(g_lock);
        state->next = g_regions;
        g_regions = state;
    }
    region->base = state->base;
    region->reservedSize = state->size;
    region->committedSize = 0;
    detail::RegionAccess::state(*region) = state;
    detail::RegionAccess::generation(*region) = state->generation;
    setDiagnostic("ok");
    return VmResult::Ok;
}

VmResult commit(VirtualMemoryRegion& region, std::size_t offset, std::size_t size,
                MemoryProtection protection) {
    std::lock_guard<std::mutex> guard(g_lock);
    HostRegionState* state = stateForLocked(region);
    if (state == nullptr) return VmResult::AlreadyReleased;
    std::size_t firstPage = 0;
    std::size_t pageCount = 0;
    if (!validPageRange(*state, offset, size, &firstPage, &pageCount)) {
        if (size == 0 || offset % hostPageSize() != 0 || size % hostPageSize() != 0) {
            return VmResult::AlignmentError;
        }
        return VmResult::OutOfRange;
    }

    std::uint8_t* newlyCommittedFlags = new (std::nothrow) std::uint8_t[pageCount]{};
    if (newlyCommittedFlags == nullptr) return VmResult::OutOfMemory;
    std::size_t newlyCommitted = 0;
    for (std::size_t index = 0; index < pageCount; ++index) {
        if (state->committed[firstPage + index] == 0) {
            if (!commitPage(*state, firstPage + index, protection)) {
                for (std::size_t rollback = 0; rollback < pageCount; ++rollback) {
                    const std::size_t pageIndex = firstPage + rollback;
                    if (newlyCommittedFlags[rollback] == 0) continue;
                    (void)decommitPage(*state, pageIndex);
                    state->committed[pageIndex] = 0;
                    state->protections[pageIndex] = static_cast<std::uint8_t>(MemoryProtection::NoAccess);
                }
                delete[] newlyCommittedFlags;
                return VmResult::HostFailure;
            }
            state->committed[firstPage + index] = 1;
            state->protections[firstPage + index] = static_cast<std::uint8_t>(protection);
            newlyCommittedFlags[index] = 1;
            ++newlyCommitted;
        }
    }
    state->committedPages += newlyCommitted;

    // Repeated commit is idempotent for the same protection.  A different
    // requested protection is an explicit protection transition.
    bool needsProtection = false;
    for (std::size_t index = 0; index < pageCount; ++index) {
        if (static_cast<MemoryProtection>(state->protections[firstPage + index]) != protection) {
            needsProtection = true;
            break;
        }
    }
    if (needsProtection) {
        std::uint8_t* oldProtections = new (std::nothrow) std::uint8_t[pageCount];
        if (oldProtections == nullptr) {
            for (std::size_t index = 0; index < pageCount; ++index) {
                if (newlyCommittedFlags[index] == 0) continue;
                const std::size_t pageIndex = firstPage + index;
                (void)decommitPage(*state, pageIndex);
                state->committed[pageIndex] = 0;
                state->protections[pageIndex] = static_cast<std::uint8_t>(MemoryProtection::NoAccess);
            }
            delete[] newlyCommittedFlags;
            return VmResult::OutOfMemory;
        }
        for (std::size_t index = 0; index < pageCount; ++index) {
            oldProtections[index] = state->protections[firstPage + index];
        }
        std::size_t changed = 0;
        bool success = true;
        for (; changed < pageCount; ++changed) {
            if (!setProtection(static_cast<std::uint8_t*>(state->base) +
                               (firstPage + changed) * hostPageSize(), hostPageSize(), protection)) {
                success = false;
                break;
            }
            state->protections[firstPage + changed] = static_cast<std::uint8_t>(protection);
        }
        if (!success) {
            (void)restoreProtection(*state, firstPage, changed, oldProtections);
            for (std::size_t index = 0; index < pageCount; ++index) {
                if (newlyCommittedFlags[index] == 0) continue;
                const std::size_t pageIndex = firstPage + index;
                (void)decommitPage(*state, pageIndex);
                state->committed[pageIndex] = 0;
                state->protections[pageIndex] = static_cast<std::uint8_t>(MemoryProtection::NoAccess);
            }
            delete[] newlyCommittedFlags;
            delete[] oldProtections;
            return VmResult::HostFailure;
        }
        delete[] oldProtections;
    }
    delete[] newlyCommittedFlags;
    region.committedSize = state->committedPages * hostPageSize();
    setDiagnostic("ok");
    return VmResult::Ok;
}

VmResult decommit(VirtualMemoryRegion& region, std::size_t offset, std::size_t size) {
    std::lock_guard<std::mutex> guard(g_lock);
    HostRegionState* state = stateForLocked(region);
    if (state == nullptr) return VmResult::AlreadyReleased;
    std::size_t firstPage = 0;
    std::size_t pageCount = 0;
    if (!validPageRange(*state, offset, size, &firstPage, &pageCount)) {
        if (size == 0 || offset % hostPageSize() != 0 || size % hostPageSize() != 0) {
            return VmResult::AlignmentError;
        }
        return VmResult::OutOfRange;
    }
    for (std::size_t index = 0; index < pageCount; ++index) {
        const std::size_t pageIndex = firstPage + index;
        if (state->committed[pageIndex] == 0) continue;
        if (!decommitPage(*state, pageIndex)) return VmResult::HostFailure;
        state->committed[pageIndex] = 0;
        state->protections[pageIndex] = static_cast<std::uint8_t>(MemoryProtection::NoAccess);
        --state->committedPages;
    }
    region.committedSize = state->committedPages * hostPageSize();
    setDiagnostic("ok");
    return VmResult::Ok;
}

VmResult protect(VirtualMemoryRegion& region, std::size_t offset, std::size_t size,
                 MemoryProtection protection) {
    std::lock_guard<std::mutex> guard(g_lock);
    HostRegionState* state = stateForLocked(region);
    if (state == nullptr) return VmResult::AlreadyReleased;
    std::size_t firstPage = 0;
    std::size_t pageCount = 0;
    if (!validPageRange(*state, offset, size, &firstPage, &pageCount)) {
        if (size == 0 || offset % hostPageSize() != 0 || size % hostPageSize() != 0) {
            return VmResult::AlignmentError;
        }
        return VmResult::OutOfRange;
    }
    for (std::size_t index = 0; index < pageCount; ++index) {
        if (state->committed[firstPage + index] == 0) return VmResult::NotCommitted;
    }
    std::uint8_t* oldProtections = new (std::nothrow) std::uint8_t[pageCount];
    if (oldProtections == nullptr) return VmResult::OutOfMemory;
    for (std::size_t index = 0; index < pageCount; ++index) {
        oldProtections[index] = state->protections[firstPage + index];
    }
    std::size_t changed = 0;
    for (; changed < pageCount; ++changed) {
        const std::size_t pageIndex = firstPage + changed;
        if (static_cast<MemoryProtection>(oldProtections[changed]) == protection) continue;
        if (!setProtection(static_cast<std::uint8_t*>(state->base) + pageIndex * hostPageSize(),
                           hostPageSize(), protection)) {
            (void)restoreProtection(*state, firstPage, changed, oldProtections);
            delete[] oldProtections;
            return VmResult::HostFailure;
        }
        state->protections[pageIndex] = static_cast<std::uint8_t>(protection);
    }
    delete[] oldProtections;
    setDiagnostic("ok");
    return VmResult::Ok;
}

VmResult release(VirtualMemoryRegion& region) {
    std::lock_guard<std::mutex> guard(g_lock);
    HostRegionState* state = stateForLocked(region);
    if (state == nullptr) {
        clearRegion(region);
        return VmResult::AlreadyReleased;
    }
#if defined(_WIN32)
    if (!VirtualFree(state->base, 0, MEM_RELEASE)) {
        setDiagnosticWithCode("VirtualFree(MEM_RELEASE)", static_cast<long>(GetLastError()));
        return VmResult::HostFailure;
    }
#else
    if (munmap(state->base, state->size) != 0) {
        setDiagnosticWithCode("munmap", errno);
        return VmResult::HostFailure;
    }
#endif
    HostRegionState** link = &g_regions;
    while (*link != nullptr && *link != state) link = &(*link)->next;
    if (*link == state) *link = state->next;
    releaseMetadata(state);
    clearRegion(region);
    setDiagnostic("ok");
    return VmResult::Ok;
}

VmResult query(const void* address, VirtualMemoryInfo* information) {
    if (address == nullptr || information == nullptr) return VmResult::InvalidArgument;
    std::lock_guard<std::mutex> guard(g_lock);
    *information = VirtualMemoryInfo{};
    std::size_t pageIndex = 0;
    HostRegionState* state = stateForAddressLocked(address, &pageIndex);
    if (state == nullptr || state->processOwner != processOwner()) return VmResult::NotFound;
    const std::size_t page = hostPageSize();
    information->regionBase = state->base;
    information->pageBase = static_cast<std::uint8_t*>(state->base) + pageIndex * page;
    information->pageSize = page;
    information->regionSize = page;
    information->reservedSize = state->size;
    information->committedSize = state->committedPages * page;
    information->generation = state->generation;
    information->reserved = true;
    information->committed = state->committed[pageIndex] != 0;
    information->protection = static_cast<MemoryProtection>(state->protections[pageIndex]);
    return VmResult::Ok;
}

std::size_t pageSize() {
    return hostPageSize();
}

std::size_t allocationGranularity() {
    return hostAllocationGranularity();
}

std::size_t maximumRegionSize() {
#if UINTPTR_MAX > 0xFFFFFFFFu
    return static_cast<std::size_t>(1ULL << 40);
#else
    return static_cast<std::size_t>(1ULL << 30);
#endif
}

VirtualMemoryStats stats() {
    std::lock_guard<std::mutex> guard(g_lock);
    VirtualMemoryStats result{};
    for (HostRegionState* state = g_regions; state != nullptr; state = state->next) {
        ++result.activeRegions;
        result.committedPages += state->committedPages;
    }
    result.physicalBackingAccounting = true;
    result.protectionEnforced = true;
    return result;
}

bool protectionIsEnforced() { return true; }

const char* lastDiagnostic() { return g_diagnostic; }

} // namespace virtual_memory
} // namespace runtime
} // namespace gxos

#endif

namespace gxos {
namespace runtime {
namespace virtual_memory {

const char* vmResultName(VmResult result) {
    switch (result) {
    case VmResult::Ok: return "Ok";
    case VmResult::InvalidArgument: return "InvalidArgument";
    case VmResult::AlreadyReserved: return "AlreadyReserved";
    case VmResult::AlreadyReleased: return "AlreadyReleased";
    case VmResult::RangeOverflow: return "RangeOverflow";
    case VmResult::AlignmentError: return "AlignmentError";
    case VmResult::AddressUnavailable: return "AddressUnavailable";
    case VmResult::OutOfMemory: return "OutOfMemory";
    case VmResult::OutOfRange: return "OutOfRange";
    case VmResult::NotOwned: return "NotOwned";
    case VmResult::NotFound: return "NotFound";
    case VmResult::NotCommitted: return "NotCommitted";
    case VmResult::ProtectionUnsupported: return "ProtectionUnsupported";
    case VmResult::Unsupported: return "Unsupported";
    case VmResult::HostFailure: return "HostFailure";
    }
    return "Unknown";
}

const char* memoryProtectionName(MemoryProtection protection) {
    switch (protection) {
    case MemoryProtection::NoAccess: return "NoAccess";
    case MemoryProtection::ReadOnly: return "ReadOnly";
    case MemoryProtection::ReadWrite: return "ReadWrite";
    case MemoryProtection::ReadExecute: return "ReadExecute";
    case MemoryProtection::ReadWriteExecute: return "ReadWriteExecute";
    }
    return "Unknown";
}

} // namespace virtual_memory
} // namespace runtime
} // namespace gxos
