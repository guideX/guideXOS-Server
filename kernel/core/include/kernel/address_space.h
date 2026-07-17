#pragma once

#include "kernel/types.h"

namespace guideXOS {
struct BootInfo;
}

namespace kernel {
namespace memory {
namespace address_space {

// Generic owner categories. VM-region pages and page-table pages share one
// allocator and are accounted separately; there is no second frame pool.
enum class FrameOwner : uint8_t {
    VmRegion = 1,
    PageTable = 2
};

enum class FrameReleaseReason : uint8_t {
    Decommit = 1,
    Release = 2,
    Other = 3
};

struct FrameAccounting {
    uint64_t totalKnownFrames;
    uint64_t freeFrames;
    uint64_t allocatedFrames;
    uint64_t regionOwnedFrames;
    uint64_t pageTableFrames;
    uint64_t framesReleasedByDecommit;
    uint64_t framesReleasedByRelease;
    uint64_t tlbInvalidations;
};

struct MappingInfo {
    bool hasEntry;
    bool present;
    uint64_t physicalAddress;
    uint64_t flags;
};

// The current kernel has one address space. Keeping the owner explicit in
// reservation metadata makes the ownership hierarchy extensible without
// pretending that process isolation already exists.
struct AddressSpace {
    uint64_t identity;
    bool alive;
};

bool initialize(const guideXOS::BootInfo* bootInfo);
bool isInitialized();
AddressSpace* current();

uint64_t allocateFrame(FrameOwner owner);
bool releaseFrame(uint64_t physicalAddress, FrameOwner owner,
                  FrameReleaseReason reason);
bool zeroFrame(uint64_t physicalAddress);

bool mapPage(AddressSpace* owner, uintptr_t virtualAddress,
             uint64_t physicalAddress, uint64_t flags);
bool unmapPage(AddressSpace* owner, uintptr_t virtualAddress,
               MappingInfo* removed);
bool queryPage(AddressSpace* owner, uintptr_t virtualAddress,
               MappingInfo* mapping);
bool updatePageFlags(AddressSpace* owner, uintptr_t virtualAddress,
                     uint64_t flags);
bool rangeHasPresentMapping(AddressSpace* owner, uintptr_t base,
                            uint64_t pageCount);

void invalidateTlb(uintptr_t virtualAddress);
FrameAccounting accounting();

// Test-only policy controls make bounded exhaustion deterministic without
// draining the whole machine's memory.
void setVmRegionFrameLimitForTests(uint64_t additionalFrames);
void clearVmRegionFrameLimitForTests();

// The narrow teardown primitive used by the VM smoke. The VM layer releases
// owned reservations before invalidating this address-space owner.
void destroyCurrent();

} // namespace address_space
} // namespace memory
} // namespace kernel
