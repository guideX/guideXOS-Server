#pragma once

#include <stdint.h>

namespace kernel {
namespace memory {

static const uint64_t kOwnedFrameBytes = 4096u;
static const uint32_t kMaxTaskArenaFrames = 4096u; // 16 MiB, fixed metadata bound
static const uint32_t kMaxFirmwareMemoryDescriptors = 512u;
static const uint32_t kEfiLoaderDataMemoryType = 2u;

enum class FrameKind : uint8_t {
    Unavailable = 0,
    Reserved,
    KernelOwned,
    SharedKernelMapping,
    Free,
    TaskPrivateData,
    TaskPrivateCode,
    TaskPrivateStack,
    PageTableStorage
};

enum class FrameStatus : uint8_t {
    Ok = 0,
    NotInitialized,
    AlreadyInitialized,
    InvalidArgument,
    InvalidMemoryMap,
    OverlappingMemoryMap,
    ArenaNotReservedByFirmware,
    ArenaTooLarge,
    OutOfFrames,
    InvalidRange,
    OwnershipMismatch,
    DoubleRelease,
    FrameMapped,
    InvalidOwner,
    PhysicalMappingUnavailable
};

struct FirmwareMemoryDescriptor {
    uint32_t type;
    uint32_t alignmentPadding;
    uint64_t physicalStart;
    uint64_t virtualStart;
    uint64_t numberOfPages;
    uint64_t attributes;
};

typedef void* (*PhysicalAddressMapper)(void* context, uint64_t physicalAddress);
typedef uint64_t (*EnterMemoryCriticalSection)(void* context);
typedef void (*LeaveMemoryCriticalSection)(void* context, uint64_t savedState);

struct FrameOwnerConfig {
    const void* memoryMapBytes;
    uint64_t memoryMapPhysicalAddress;
    uint64_t memoryMapEntryCount;
    uint64_t memoryMapDescriptorSize;
    uint64_t taskArenaPhysicalBase;
    uint64_t taskArenaBytes;
    PhysicalAddressMapper mapPhysicalAddress;
    void* mapperContext;
    EnterMemoryCriticalSection enterCriticalSection;
    LeaveMemoryCriticalSection leaveCriticalSection;
};

struct FrameInfo {
    uint64_t physicalAddress;
    uint32_t taskId;
    FrameKind kind;
    bool mapped;
};

struct FrameAccounting {
    uint32_t capacityFrames;
    uint32_t freeFrames;
    uint32_t reservedFrames;
    uint32_t kernelOwnedFrames;
    uint32_t sharedKernelFrames;
    uint32_t taskDataFrames;
    uint32_t taskCodeFrames;
    uint32_t taskStackFrames;
    uint32_t pageTableFrames;
    uint32_t mappedTaskFrames;
};

FrameStatus initialize_frame_owner(const FrameOwnerConfig& config);
bool frame_owner_initialized();
// The owner is a single-core kernel facility. Calls disable/restore local IRQs
// around metadata changes; this contract does not claim SMP synchronization.
FrameStatus allocate_task_frame(FrameKind kind, uint32_t taskId, uint64_t* outPhysicalAddress);
FrameStatus reserve_frame_range(uint64_t physicalBase, uint64_t byteCount, FrameKind kind);
FrameStatus query_frame(uint64_t physicalAddress, FrameInfo* outInfo);
FrameStatus transfer_frame_ownership(uint64_t physicalAddress, FrameKind expectedKind,
                                     FrameKind newKind, uint32_t taskId);
FrameStatus release_task_frame(uint64_t physicalAddress, FrameKind expectedKind, uint32_t taskId);
FrameStatus set_task_frame_mapped(uint64_t physicalAddress, FrameKind expectedKind,
                                  uint32_t taskId, bool mapped);
void* task_frame_pointer(uint64_t physicalAddress, FrameKind expectedKind, uint32_t taskId);
void get_frame_accounting(FrameAccounting* outAccounting);
const char* frame_status_name(FrameStatus status);

} // namespace memory
} // namespace kernel
