#pragma once

#include <stdint.h>

namespace kernel {
namespace memory {

enum class AddressSpaceStatus : uint8_t {
    Ok = 0,
    InvalidArgument,
    FrameOwnerUnavailable,
    AllocationFailure,
    UnsupportedConfiguration,
    KernelMappingNotSupervisor,
    UserWindowAlreadyMapped,
    InvalidAddress,
    InvalidPermissions,
    OwnershipMismatch,
    MappingCollision,
    MappingNotFound,
    AccessDenied,
    AddressSpaceActive,
    CorruptPageTables,
    InvalidationFailure
};

enum class UserAccess : uint8_t {
    Read = 0,
    Write,
    Execute
};

struct AddressSpacePlatform {
    void* context;
    void* (*mapPhysicalAddress)(void* context, uint64_t physicalAddress);
    uint64_t (*readCurrentCr3)(void* context);
    bool (*ensureNxEnabled)(void* context);
    bool (*supportsSafeTlbInvalidation)(void* context);
    bool (*invalidatePage)(void* context, uint64_t virtualAddress);
    uint64_t (*enterCriticalSection)(void* context);
    void (*leaveCriticalSection)(void* context, uint64_t savedState);
};

struct TaskAddressSpace {
    uint64_t rootPhysicalAddress;
    uint32_t taskId;
    uint32_t mappingCount;
    bool initialized;
};

// Zero-initialize this descriptor before create(). Keep it kernel-owned and
// alive until destroy(); page tables and frames have independent arena owners.

struct MappingPermissions {
    uint64_t physicalAddress;
    uint64_t pagePhysicalAddress;
    bool present;
    bool userAccessible;
    bool writable;
    bool executable;
};

struct DiagnosticFixtureResult {
    AddressSpaceStatus status;
    uint32_t mappingsCreated;
    uint32_t invalidationsIssued;
    bool kernelMappingStayedSupervisor;
    bool guardPageStayedUnmapped;
    bool ownershipReturnedToBaseline;
};

AddressSpacePlatform amd64_address_space_platform();

static const uint64_t kTaskUserWindowBase = 0x00007F8000000000ull;
static const uint64_t kTaskUserCodeAddress = kTaskUserWindowBase;
static const uint64_t kTaskUserDataAddress = kTaskUserWindowBase + 0x1000u;
static const uint64_t kTaskUserStackGuardAddress = kTaskUserWindowBase + 0x2000u;
static const uint64_t kTaskUserStackAddress = kTaskUserWindowBase + 0x3000u;
static const uint32_t kTaskUserPml4Index = 255u;

AddressSpaceStatus create_task_address_space(TaskAddressSpace* outSpace,
                                             uint32_t taskId,
                                             uint64_t kernelRootPhysicalAddress,
                                             const AddressSpacePlatform& platform);
AddressSpaceStatus map_task_frame(TaskAddressSpace* space,
                                  uint64_t virtualAddress,
                                  uint64_t physicalAddress,
                                  const AddressSpacePlatform& platform);
AddressSpaceStatus query_task_mapping(const TaskAddressSpace* space,
                                      uint64_t virtualAddress,
                                      MappingPermissions* outPermissions,
                                      const AddressSpacePlatform& platform);
AddressSpaceStatus verify_task_access(const TaskAddressSpace* space,
                                      uint64_t virtualAddress,
                                      UserAccess access,
                                      const AddressSpacePlatform& platform);
AddressSpaceStatus destroy_task_address_space(TaskAddressSpace* space,
                                               const AddressSpacePlatform& platform);
AddressSpaceStatus run_task_address_space_fixture(const AddressSpacePlatform& platform,
                                                  uint32_t taskId,
                                                  DiagnosticFixtureResult* outResult);
const char* address_space_status_name(AddressSpaceStatus status);

} // namespace memory
} // namespace kernel
