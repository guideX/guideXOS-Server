#include "include/kernel/task_address_space.h"
#include "include/kernel/physical_frame_owner.h"

namespace kernel {
namespace memory {
namespace {

static const uint64_t kPageMask = 0xFFFull;
static const uint64_t kPhysicalAddressMask = 0x000FFFFFFFFFF000ull;
static const uint64_t kPresent = 1ull << 0;
static const uint64_t kWritable = 1ull << 1;
static const uint64_t kUser = 1ull << 2;
static const uint64_t kPageSize = 1ull << 7;
static const uint64_t kNoExecute = 1ull << 63;
static const uint32_t kEntriesPerTable = 512u;

struct CreatedTable {
    uint64_t* parentEntry;
    uint64_t physicalAddress;
};

struct WalkCounts {
    uint32_t mappings;
    uint32_t tables;
};

struct PlatformGuard {
    const AddressSpacePlatform& platform;
    uint64_t savedState;
    PlatformGuard(const AddressSpacePlatform& p)
        : platform(p), savedState(p.enterCriticalSection(p.context)) {}
    ~PlatformGuard() { platform.leaveCriticalSection(platform.context, savedState); }
};

enum class WalkMode : uint8_t { Validate = 0, Invalidate, Release };

static bool canonical_address(uint64_t address)
{
    const uint64_t upper = address >> 48;
    const bool sign = ((address >> 47) & 1u) != 0u;
    return sign ? upper == 0xFFFFu : upper == 0u;
}

static bool valid_platform(const AddressSpacePlatform& platform)
{
    return platform.mapPhysicalAddress && platform.readCurrentCr3 &&
           platform.ensureNxEnabled && platform.supportsSafeTlbInvalidation &&
           platform.invalidatePage &&
           platform.enterCriticalSection && platform.leaveCriticalSection;
}

static uint64_t pml4_index(uint64_t address) { return (address >> 39) & 0x1FFu; }
static uint64_t pdpt_index(uint64_t address) { return (address >> 30) & 0x1FFu; }
static uint64_t pd_index(uint64_t address) { return (address >> 21) & 0x1FFu; }
static uint64_t pt_index(uint64_t address) { return (address >> 12) & 0x1FFu; }

static uint64_t* owned_table_pointer(uint64_t physicalAddress, uint32_t taskId)
{
    return static_cast<uint64_t*>(task_frame_pointer(
        physicalAddress & kPhysicalAddressMask, FrameKind::PageTableStorage, taskId));
}

static bool valid_owned_frame(uint64_t physicalAddress, uint32_t taskId,
                              FrameInfo* outInfo)
{
    if (!outInfo || (physicalAddress & kPageMask) != 0u ||
        query_frame(physicalAddress, outInfo) != FrameStatus::Ok) {
        return false;
    }
    return outInfo->taskId == taskId &&
           (outInfo->kind == FrameKind::TaskPrivateCode ||
            outInfo->kind == FrameKind::TaskPrivateData ||
            outInfo->kind == FrameKind::TaskPrivateStack);
}

static AddressSpaceStatus allocate_table(uint32_t taskId, uint64_t* outPhysical,
                                         uint64_t** outPointer)
{
    if (!outPhysical || !outPointer) return AddressSpaceStatus::InvalidArgument;
    *outPhysical = 0u;
    *outPointer = nullptr;
    const FrameStatus status = allocate_task_frame(FrameKind::PageTableStorage,
                                                    taskId, outPhysical);
    if (status != FrameStatus::Ok) return AddressSpaceStatus::AllocationFailure;
    *outPointer = owned_table_pointer(*outPhysical, taskId);
    if (!*outPointer) {
        (void)release_task_frame(*outPhysical, FrameKind::PageTableStorage, taskId);
        *outPhysical = 0u;
        return AddressSpaceStatus::FrameOwnerUnavailable;
    }
    return AddressSpaceStatus::Ok;
}

static void rollback_created_tables(CreatedTable* created, uint32_t createdCount,
                                    uint32_t taskId)
{
    while (createdCount > 0u) {
        CreatedTable& table = created[--createdCount];
        *table.parentEntry = 0u;
        (void)release_task_frame(table.physicalAddress,
                                 FrameKind::PageTableStorage, taskId);
    }
}

static AddressSpaceStatus ensure_private_child(uint64_t* parent,
                                               uint64_t index,
                                               uint32_t taskId,
                                               CreatedTable* created,
                                               uint32_t* createdCount,
                                               uint64_t** outChild)
{
    if (!parent || index >= kEntriesPerTable || !created || !createdCount || !outChild) {
        return AddressSpaceStatus::InvalidArgument;
    }
    uint64_t& entry = parent[index];
    if ((entry & kPresent) != 0u) {
        if ((entry & kPageSize) != 0u || (entry & (kUser | kWritable)) != (kUser | kWritable) ||
            (entry & kNoExecute) != 0u) {
            return AddressSpaceStatus::CorruptPageTables;
        }
        const uint64_t childPhysical = entry & kPhysicalAddressMask;
        *outChild = owned_table_pointer(childPhysical, taskId);
        return *outChild ? AddressSpaceStatus::Ok : AddressSpaceStatus::OwnershipMismatch;
    }

    uint64_t childPhysical = 0;
    uint64_t* child = nullptr;
    AddressSpaceStatus status = allocate_table(taskId, &childPhysical, &child);
    if (status != AddressSpaceStatus::Ok) return status;
    if (*createdCount >= 3u) {
        (void)release_task_frame(childPhysical, FrameKind::PageTableStorage, taskId);
        return AddressSpaceStatus::CorruptPageTables;
    }
    entry = childPhysical | kPresent | kWritable | kUser;
    created[*createdCount].parentEntry = &entry;
    created[*createdCount].physicalAddress = childPhysical;
    ++*createdCount;
    *outChild = child;
    return AddressSpaceStatus::Ok;
}

static bool permissions_match_owner(const FrameInfo& info, uint64_t entry)
{
    const bool writable = (entry & kWritable) != 0u;
    const bool executable = (entry & kNoExecute) == 0u;
    if (!info.mapped || !((entry & kUser) != 0u)) return false;
    if (info.kind == FrameKind::TaskPrivateCode) return !writable && executable;
    if (info.kind == FrameKind::TaskPrivateData || info.kind == FrameKind::TaskPrivateStack) {
        return writable && !executable;
    }
    return false;
}

static AddressSpaceStatus walk_private_tree(TaskAddressSpace* space,
                                             uint64_t tablePhysical,
                                             uint32_t level,
                                             uint64_t virtualPrefix,
                                             bool effectiveWritable,
                                             bool effectiveExecutable,
                                             WalkMode mode,
                                             const AddressSpacePlatform& platform,
                                             WalkCounts* counts)
{
    if (!space || !counts || level < 1u || level > 3u) {
        return AddressSpaceStatus::InvalidArgument;
    }
    uint64_t* table = owned_table_pointer(tablePhysical, space->taskId);
    if (!table) return AddressSpaceStatus::OwnershipMismatch;
    ++counts->tables;

    for (uint32_t i = 0; i < kEntriesPerTable; ++i) {
        uint64_t& entry = table[i];
        if ((entry & kPresent) == 0u) continue;
        if (level < 3u) {
            if ((entry & kPageSize) != 0u ||
                (entry & (kUser | kWritable)) != (kUser | kWritable) ||
                (entry & kNoExecute) != 0u) {
                return AddressSpaceStatus::CorruptPageTables;
            }
            const uint64_t childPhysical = entry & kPhysicalAddressMask;
            FrameInfo childInfo{};
            if (query_frame(childPhysical, &childInfo) != FrameStatus::Ok ||
                childInfo.kind != FrameKind::PageTableStorage ||
                childInfo.taskId != space->taskId || childInfo.mapped) {
                return AddressSpaceStatus::OwnershipMismatch;
            }
            uint64_t childPrefix = virtualPrefix;
            if (level == 1u) childPrefix += static_cast<uint64_t>(i) << 30;
            else childPrefix += static_cast<uint64_t>(i) << 21;
            AddressSpaceStatus status = walk_private_tree(
                space, childPhysical, level + 1u, childPrefix,
                effectiveWritable && ((entry & kWritable) != 0u),
                effectiveExecutable && ((entry & kNoExecute) == 0u),
                mode, platform, counts);
            if (status != AddressSpaceStatus::Ok) return status;
            continue;
        }

        const uint64_t virtualAddress = virtualPrefix + (static_cast<uint64_t>(i) << 12);
        const uint64_t physicalAddress = entry & kPhysicalAddressMask;
        FrameInfo leafInfo{};
        if (!valid_owned_frame(physicalAddress, space->taskId, &leafInfo) ||
            !permissions_match_owner(leafInfo, entry) ||
            !effectiveWritable || !effectiveExecutable) {
            return AddressSpaceStatus::CorruptPageTables;
        }
        if (counts->mappings >= kMaxTaskArenaFrames) {
            return AddressSpaceStatus::CorruptPageTables;
        }
        ++counts->mappings;

        if (mode == WalkMode::Invalidate) {
            if (!platform.invalidatePage(platform.context, virtualAddress)) {
                return AddressSpaceStatus::InvalidationFailure;
            }
        } else if (mode == WalkMode::Release) {
            entry = 0u;
            if (set_task_frame_mapped(physicalAddress, leafInfo.kind,
                                      space->taskId, false) != FrameStatus::Ok ||
                release_task_frame(physicalAddress, leafInfo.kind,
                                   space->taskId) != FrameStatus::Ok) {
                return AddressSpaceStatus::FrameOwnerUnavailable;
            }
        }
    }
    return AddressSpaceStatus::Ok;
}

static AddressSpaceStatus release_private_tables(TaskAddressSpace* space,
                                                  uint64_t tablePhysical,
                                                  uint32_t level)
{
    uint64_t* table = owned_table_pointer(tablePhysical, space->taskId);
    if (!table) return AddressSpaceStatus::OwnershipMismatch;
    for (uint32_t i = 0; i < kEntriesPerTable; ++i) {
        uint64_t& entry = table[i];
        if ((entry & kPresent) == 0u) continue;
        if (level == 3u) continue; // Leaf frames were cleared in the prior pass.
        const uint64_t childPhysical = entry & kPhysicalAddressMask;
        AddressSpaceStatus status = release_private_tables(space, childPhysical, level + 1u);
        if (status != AddressSpaceStatus::Ok) return status;
        entry = 0u;
        if (release_task_frame(childPhysical, FrameKind::PageTableStorage,
                               space->taskId) != FrameStatus::Ok) {
            return AddressSpaceStatus::FrameOwnerUnavailable;
        }
    }
    return AddressSpaceStatus::Ok;
}

static bool same_accounting(const FrameAccounting& a, const FrameAccounting& b)
{
    return a.capacityFrames == b.capacityFrames && a.freeFrames == b.freeFrames &&
           a.reservedFrames == b.reservedFrames && a.kernelOwnedFrames == b.kernelOwnedFrames &&
           a.sharedKernelFrames == b.sharedKernelFrames && a.taskDataFrames == b.taskDataFrames &&
           a.taskCodeFrames == b.taskCodeFrames && a.taskStackFrames == b.taskStackFrames &&
           a.pageTableFrames == b.pageTableFrames && a.mappedTaskFrames == b.mappedTaskFrames;
}

} // namespace

AddressSpaceStatus create_task_address_space(TaskAddressSpace* outSpace,
                                              uint32_t taskId,
                                              uint64_t kernelRootPhysicalAddress,
                                              const AddressSpacePlatform& platform)
{
    if (!outSpace || taskId == 0u || !valid_platform(platform) ||
        (kernelRootPhysicalAddress & kPageMask) != 0u ||
        (kernelRootPhysicalAddress & ~kPhysicalAddressMask) != 0u) {
        return AddressSpaceStatus::InvalidArgument;
    }
    if (outSpace->initialized) return AddressSpaceStatus::InvalidArgument;
    if (!frame_owner_initialized()) return AddressSpaceStatus::FrameOwnerUnavailable;
    PlatformGuard guard(platform);
    if ((platform.readCurrentCr3(platform.context) & kPhysicalAddressMask) !=
        kernelRootPhysicalAddress) {
        return AddressSpaceStatus::InvalidArgument;
    }
    if (!platform.ensureNxEnabled(platform.context) ||
        !platform.supportsSafeTlbInvalidation(platform.context)) {
        return AddressSpaceStatus::UnsupportedConfiguration;
    }

    uint64_t* kernelRoot = static_cast<uint64_t*>(
        platform.mapPhysicalAddress(platform.context, kernelRootPhysicalAddress));
    if (!kernelRoot || (reinterpret_cast<uintptr_t>(kernelRoot) & kPageMask) != 0u) {
        return AddressSpaceStatus::FrameOwnerUnavailable;
    }
    if (kernelRoot[kTaskUserPml4Index] != 0u) {
        return AddressSpaceStatus::UserWindowAlreadyMapped;
    }
    for (uint32_t i = 0; i < kEntriesPerTable; ++i) {
        if ((kernelRoot[i] & kPresent) != 0u && (kernelRoot[i] & kUser) != 0u) {
            return AddressSpaceStatus::KernelMappingNotSupervisor;
        }
    }

    uint64_t rootPhysical = 0u;
    uint64_t* taskRoot = nullptr;
    AddressSpaceStatus status = allocate_table(taskId, &rootPhysical, &taskRoot);
    if (status != AddressSpaceStatus::Ok) return status;
    for (uint32_t i = 0; i < kEntriesPerTable; ++i) taskRoot[i] = kernelRoot[i];
    if (taskRoot[kTaskUserPml4Index] != 0u) {
        (void)release_task_frame(rootPhysical, FrameKind::PageTableStorage, taskId);
        return AddressSpaceStatus::UserWindowAlreadyMapped;
    }

    outSpace->rootPhysicalAddress = rootPhysical;
    outSpace->taskId = taskId;
    outSpace->mappingCount = 0u;
    outSpace->initialized = true;
    return AddressSpaceStatus::Ok;
}

AddressSpaceStatus map_task_frame(TaskAddressSpace* space,
                                   uint64_t virtualAddress,
                                   uint64_t physicalAddress,
                                   const AddressSpacePlatform& platform)
{
    if (!space || !space->initialized || !valid_platform(platform)) {
        return AddressSpaceStatus::InvalidArgument;
    }
    PlatformGuard guard(platform);
    if ((virtualAddress & kPageMask) != 0u ||
        (physicalAddress & kPageMask) != 0u || !canonical_address(virtualAddress) ||
        pml4_index(virtualAddress) != kTaskUserPml4Index ||
        virtualAddress < kTaskUserWindowBase) {
        return AddressSpaceStatus::InvalidAddress;
    }
    if (!platform.ensureNxEnabled(platform.context) ||
        !platform.supportsSafeTlbInvalidation(platform.context)) {
        return AddressSpaceStatus::UnsupportedConfiguration;
    }
    if (space->mappingCount >= kMaxTaskArenaFrames) return AddressSpaceStatus::AllocationFailure;

    FrameInfo leafInfo{};
    if (!valid_owned_frame(physicalAddress, space->taskId, &leafInfo) || leafInfo.mapped) {
        return AddressSpaceStatus::OwnershipMismatch;
    }
    bool writable = false;
    bool executable = false;
    if (leafInfo.kind == FrameKind::TaskPrivateCode) {
        executable = true;
    } else if (leafInfo.kind == FrameKind::TaskPrivateData ||
               leafInfo.kind == FrameKind::TaskPrivateStack) {
        writable = true;
    } else {
        return AddressSpaceStatus::InvalidPermissions;
    }

    uint64_t* root = owned_table_pointer(space->rootPhysicalAddress, space->taskId);
    if (!root) return AddressSpaceStatus::OwnershipMismatch;
    if (root[kTaskUserPml4Index] == 0u) {
        // The root is copied from the kernel, whose selected user window must
        // be empty. Create the first private level transactionally.
    } else if ((root[kTaskUserPml4Index] & (kPresent | kUser | kWritable)) !=
               (kPresent | kUser | kWritable)) {
        return AddressSpaceStatus::CorruptPageTables;
    }

    CreatedTable created[3] = {};
    uint32_t createdCount = 0u;
    uint64_t* pdpt = nullptr;
    AddressSpaceStatus status = ensure_private_child(
        root, pml4_index(virtualAddress), space->taskId,
        created, &createdCount, &pdpt);
    if (status != AddressSpaceStatus::Ok) {
        rollback_created_tables(created, createdCount, space->taskId);
        return status;
    }
    uint64_t* pd = nullptr;
    status = ensure_private_child(pdpt, pdpt_index(virtualAddress), space->taskId,
                                  created, &createdCount, &pd);
    if (status != AddressSpaceStatus::Ok) {
        rollback_created_tables(created, createdCount, space->taskId);
        return status;
    }
    uint64_t* pt = nullptr;
    status = ensure_private_child(pd, pd_index(virtualAddress), space->taskId,
                                  created, &createdCount, &pt);
    if (status != AddressSpaceStatus::Ok) {
        rollback_created_tables(created, createdCount, space->taskId);
        return status;
    }

    uint64_t& leaf = pt[pt_index(virtualAddress)];
    if ((leaf & kPresent) != 0u) {
        rollback_created_tables(created, createdCount, space->taskId);
        return AddressSpaceStatus::MappingCollision;
    }
    const FrameStatus mappedStatus = set_task_frame_mapped(
        physicalAddress, leafInfo.kind, space->taskId, true);
    if (mappedStatus != FrameStatus::Ok) {
        rollback_created_tables(created, createdCount, space->taskId);
        return AddressSpaceStatus::OwnershipMismatch;
    }

    leaf = physicalAddress | kPresent | kUser |
           (writable ? kWritable : 0u) |
           (executable ? 0u : kNoExecute);
    ++space->mappingCount;
    return AddressSpaceStatus::Ok;
}

AddressSpaceStatus query_task_mapping(const TaskAddressSpace* space,
                                       uint64_t virtualAddress,
                                       MappingPermissions* outPermissions,
                                       const AddressSpacePlatform& platform)
{
    if (!space || !space->initialized || !outPermissions || !valid_platform(platform) ||
        !canonical_address(virtualAddress)) {
        return AddressSpaceStatus::InvalidArgument;
    }
    PlatformGuard guard(platform);
    *outPermissions = MappingPermissions{};
    const uint64_t rootPhysical = space->rootPhysicalAddress & kPhysicalAddressMask;
    uint64_t* root = owned_table_pointer(rootPhysical, space->taskId);
    if (!root) return AddressSpaceStatus::OwnershipMismatch;

    const uint64_t indices[4] = {pml4_index(virtualAddress), pdpt_index(virtualAddress),
                                 pd_index(virtualAddress), pt_index(virtualAddress)};
    uint64_t* table = root;
    bool effectiveUser = true;
    bool effectiveWritable = true;
    bool effectiveExecutable = true;
    uint64_t entry = 0u;
    for (uint32_t level = 0; level < 4u; ++level) {
        entry = table[indices[level]];
        if ((entry & kPresent) == 0u) return AddressSpaceStatus::MappingNotFound;
        effectiveUser = effectiveUser && ((entry & kUser) != 0u);
        effectiveWritable = effectiveWritable && ((entry & kWritable) != 0u);
        effectiveExecutable = effectiveExecutable && ((entry & kNoExecute) == 0u);
        if (level == 3u) break;
        if ((entry & kPageSize) != 0u) {
            // Preserve and inspect existing 1 GiB and 2 MiB supervisor leaves.
            // The private user window never accepts a large-page leaf.
            if (level == 0u || indices[0] == kTaskUserPml4Index) {
                return AddressSpaceStatus::UnsupportedConfiguration;
            }
            const uint32_t pageShift = level == 1u ? 30u : 21u;
            const uint64_t pageMask = (1ull << pageShift) - 1u;
            const uint64_t physicalBase =
                (entry & kPhysicalAddressMask) & ~pageMask;
            const uint64_t physical = physicalBase | (virtualAddress & pageMask);
            outPermissions->physicalAddress = physical;
            outPermissions->pagePhysicalAddress = physical & ~kPageMask;
            outPermissions->present = true;
            outPermissions->userAccessible = effectiveUser;
            outPermissions->writable = effectiveWritable;
            outPermissions->executable = effectiveExecutable;
            return AddressSpaceStatus::Ok;
        }
        const uint64_t childPhysical = entry & kPhysicalAddressMask;
        if (indices[0] == kTaskUserPml4Index) {
            table = owned_table_pointer(childPhysical, space->taskId);
        } else {
            table = static_cast<uint64_t*>(
                platform.mapPhysicalAddress(platform.context, childPhysical));
        }
        if (!table || (reinterpret_cast<uintptr_t>(table) & kPageMask) != 0u) {
            return AddressSpaceStatus::CorruptPageTables;
        }
    }

    const uint64_t pagePhysical = entry & kPhysicalAddressMask;
    if (indices[0] == kTaskUserPml4Index) {
        FrameInfo leafInfo{};
        if (!valid_owned_frame(pagePhysical, space->taskId, &leafInfo) ||
            !permissions_match_owner(leafInfo, entry)) {
            return AddressSpaceStatus::OwnershipMismatch;
        }
    }
    outPermissions->physicalAddress = pagePhysical + (virtualAddress & kPageMask);
    outPermissions->pagePhysicalAddress = pagePhysical;
    outPermissions->present = true;
    outPermissions->userAccessible = effectiveUser;
    outPermissions->writable = effectiveWritable;
    outPermissions->executable = effectiveExecutable;
    return AddressSpaceStatus::Ok;
}

AddressSpaceStatus verify_task_access(const TaskAddressSpace* space,
                                      uint64_t virtualAddress,
                                      UserAccess access,
                                      const AddressSpacePlatform& platform)
{
    MappingPermissions permissions{};
    AddressSpaceStatus status = query_task_mapping(space, virtualAddress,
                                                    &permissions, platform);
    if (status != AddressSpaceStatus::Ok) return status;
    if (!permissions.userAccessible) return AddressSpaceStatus::AccessDenied;
    if (access == UserAccess::Write && !permissions.writable) {
        return AddressSpaceStatus::AccessDenied;
    }
    if (access == UserAccess::Execute && !permissions.executable) {
        return AddressSpaceStatus::AccessDenied;
    }
    return AddressSpaceStatus::Ok;
}

AddressSpaceStatus destroy_task_address_space(TaskAddressSpace* space,
                                                const AddressSpacePlatform& platform)
{
    if (!space || !space->initialized || !valid_platform(platform)) {
        return AddressSpaceStatus::InvalidArgument;
    }
    PlatformGuard guard(platform);
    if ((platform.readCurrentCr3(platform.context) & kPhysicalAddressMask) ==
        (space->rootPhysicalAddress & kPhysicalAddressMask)) {
        return AddressSpaceStatus::AddressSpaceActive;
    }
    if (!platform.supportsSafeTlbInvalidation(platform.context)) {
        return AddressSpaceStatus::UnsupportedConfiguration;
    }
    uint64_t* root = owned_table_pointer(space->rootPhysicalAddress, space->taskId);
    if (!root) return AddressSpaceStatus::OwnershipMismatch;
    const uint64_t rootEntry = root[kTaskUserPml4Index];
    if (rootEntry != 0u && (rootEntry & (kPresent | kUser | kWritable)) !=
                           (kPresent | kUser | kWritable)) {
        return AddressSpaceStatus::CorruptPageTables;
    }

    WalkCounts validation{};
    if (rootEntry != 0u) {
        const uint64_t pdptPhysical = rootEntry & kPhysicalAddressMask;
        FrameInfo pdptInfo{};
        if (query_frame(pdptPhysical, &pdptInfo) != FrameStatus::Ok ||
            pdptInfo.kind != FrameKind::PageTableStorage ||
            pdptInfo.taskId != space->taskId || pdptInfo.mapped) {
            return AddressSpaceStatus::OwnershipMismatch;
        }
        validation.tables = 1u;
        AddressSpaceStatus status = walk_private_tree(
            space, pdptPhysical, 1u, kTaskUserWindowBase, true, true,
            WalkMode::Validate, platform, &validation);
        if (status != AddressSpaceStatus::Ok || validation.mappings != space->mappingCount) {
            return status == AddressSpaceStatus::Ok ?
                AddressSpaceStatus::CorruptPageTables : status;
        }
    } else if (space->mappingCount != 0u) {
        return AddressSpaceStatus::CorruptPageTables;
    }

    if (rootEntry != 0u) {
        WalkCounts invalidationCounts{};
        AddressSpaceStatus status = walk_private_tree(
            space, rootEntry & kPhysicalAddressMask, 1u, kTaskUserWindowBase,
            true, true, WalkMode::Invalidate, platform, &invalidationCounts);
        if (status != AddressSpaceStatus::Ok ||
            invalidationCounts.mappings != validation.mappings) {
            return status == AddressSpaceStatus::Ok ?
                AddressSpaceStatus::CorruptPageTables : status;
        }

        WalkCounts releaseCounts{};
        status = walk_private_tree(space, rootEntry & kPhysicalAddressMask, 1u,
                                   kTaskUserWindowBase, true, true,
                                   WalkMode::Release, platform, &releaseCounts);
        if (status != AddressSpaceStatus::Ok ||
            releaseCounts.mappings != validation.mappings) {
            return status == AddressSpaceStatus::Ok ?
                AddressSpaceStatus::CorruptPageTables : status;
        }
        status = release_private_tables(space, rootEntry & kPhysicalAddressMask, 1u);
        if (status != AddressSpaceStatus::Ok) return status;
        root[kTaskUserPml4Index] = 0u;
        if (release_task_frame(rootEntry & kPhysicalAddressMask,
                               FrameKind::PageTableStorage, space->taskId) != FrameStatus::Ok) {
            return AddressSpaceStatus::FrameOwnerUnavailable;
        }
    }

    if (release_task_frame(space->rootPhysicalAddress, FrameKind::PageTableStorage,
                           space->taskId) != FrameStatus::Ok) {
        return AddressSpaceStatus::FrameOwnerUnavailable;
    }
    *space = TaskAddressSpace{};
    return AddressSpaceStatus::Ok;
}

AddressSpaceStatus run_task_address_space_fixture(const AddressSpacePlatform& platform,
                                                   uint32_t taskId,
                                                   DiagnosticFixtureResult* outResult)
{
    if (!outResult || taskId == 0u || !valid_platform(platform)) {
        return AddressSpaceStatus::InvalidArgument;
    }
    *outResult = DiagnosticFixtureResult{};
    outResult->status = AddressSpaceStatus::InvalidArgument;
    FrameAccounting baseline{};
    get_frame_accounting(&baseline);
    if (baseline.capacityFrames == 0u) {
        outResult->status = AddressSpaceStatus::FrameOwnerUnavailable;
        return outResult->status;
    }

    const uint64_t kernelRoot = platform.readCurrentCr3(platform.context) &
                                kPhysicalAddressMask;
    TaskAddressSpace space{};
    AddressSpaceStatus status = create_task_address_space(&space, taskId,
                                                           kernelRoot, platform);
    uint64_t codePhysical = 0u;
    uint64_t dataPhysical = 0u;
    uint64_t stackPhysical = 0u;
    if (status == AddressSpaceStatus::Ok) {
        if (allocate_task_frame(FrameKind::TaskPrivateCode, taskId, &codePhysical) != FrameStatus::Ok ||
            allocate_task_frame(FrameKind::TaskPrivateData, taskId, &dataPhysical) != FrameStatus::Ok ||
            allocate_task_frame(FrameKind::TaskPrivateStack, taskId, &stackPhysical) != FrameStatus::Ok) {
            status = AddressSpaceStatus::AllocationFailure;
        }
    }
    if (status == AddressSpaceStatus::Ok) {
        status = map_task_frame(&space, kTaskUserCodeAddress, codePhysical, platform);
        if (status == AddressSpaceStatus::Ok) {
            status = map_task_frame(&space, kTaskUserDataAddress, dataPhysical, platform);
        }
        if (status == AddressSpaceStatus::Ok) {
            status = map_task_frame(&space, kTaskUserStackAddress, stackPhysical, platform);
        }
    }
    if (status == AddressSpaceStatus::Ok) {
        outResult->mappingsCreated = space.mappingCount;
        const bool codeOkay =
            verify_task_access(&space, kTaskUserCodeAddress, UserAccess::Read, platform) == AddressSpaceStatus::Ok &&
            verify_task_access(&space, kTaskUserCodeAddress, UserAccess::Execute, platform) == AddressSpaceStatus::Ok &&
            verify_task_access(&space, kTaskUserCodeAddress, UserAccess::Write, platform) == AddressSpaceStatus::AccessDenied;
        const bool dataOkay =
            verify_task_access(&space, kTaskUserDataAddress, UserAccess::Read, platform) == AddressSpaceStatus::Ok &&
            verify_task_access(&space, kTaskUserDataAddress, UserAccess::Write, platform) == AddressSpaceStatus::Ok &&
            verify_task_access(&space, kTaskUserDataAddress, UserAccess::Execute, platform) == AddressSpaceStatus::AccessDenied;
        const bool stackOkay =
            verify_task_access(&space, kTaskUserStackAddress, UserAccess::Write, platform) == AddressSpaceStatus::Ok &&
            verify_task_access(&space, kTaskUserStackAddress, UserAccess::Execute, platform) == AddressSpaceStatus::AccessDenied;
        MappingPermissions guard{};
        outResult->guardPageStayedUnmapped =
            query_task_mapping(&space, kTaskUserStackGuardAddress, &guard, platform) ==
            AddressSpaceStatus::MappingNotFound;

        MappingPermissions kernelMapping{};
        const AddressSpaceStatus kernelStatus = query_task_mapping(&space, 0u,
                                                                    &kernelMapping, platform);
        outResult->kernelMappingStayedSupervisor =
            kernelStatus == AddressSpaceStatus::Ok && kernelMapping.present &&
            !kernelMapping.userAccessible &&
            verify_task_access(&space, 0u, UserAccess::Read, platform) ==
                AddressSpaceStatus::AccessDenied;
        if (!codeOkay || !dataOkay || !stackOkay ||
            !outResult->guardPageStayedUnmapped ||
            !outResult->kernelMappingStayedSupervisor) {
            status = AddressSpaceStatus::CorruptPageTables;
        }
    }

    if (space.initialized) {
        const uint32_t expectedInvalidations = space.mappingCount;
        AddressSpaceStatus teardown = destroy_task_address_space(&space, platform);
        if (teardown != AddressSpaceStatus::Ok && status == AddressSpaceStatus::Ok) status = teardown;
        if (teardown == AddressSpaceStatus::Ok) outResult->invalidationsIssued = expectedInvalidations;
    }
    const uint64_t extraFrames[] = {codePhysical, dataPhysical, stackPhysical};
    const FrameKind extraKinds[] = {FrameKind::TaskPrivateCode,
                                    FrameKind::TaskPrivateData,
                                    FrameKind::TaskPrivateStack};
    for (uint32_t i = 0; i < 3u; ++i) {
        if (extraFrames[i] == 0u) continue;
        FrameInfo info{};
        if (query_frame(extraFrames[i], &info) == FrameStatus::Ok &&
            info.taskId == taskId && info.kind == extraKinds[i] && !info.mapped) {
            if (release_task_frame(extraFrames[i], extraKinds[i], taskId) != FrameStatus::Ok &&
                status == AddressSpaceStatus::Ok) {
                status = AddressSpaceStatus::FrameOwnerUnavailable;
            }
        }
    }

    FrameAccounting after{};
    get_frame_accounting(&after);
    outResult->ownershipReturnedToBaseline = same_accounting(baseline, after);
    if (!outResult->ownershipReturnedToBaseline && status == AddressSpaceStatus::Ok) {
        status = AddressSpaceStatus::CorruptPageTables;
    }
    outResult->status = status;
    return status;
}

const char* address_space_status_name(AddressSpaceStatus status)
{
    switch (status) {
    case AddressSpaceStatus::Ok: return "ok";
    case AddressSpaceStatus::InvalidArgument: return "invalid-argument";
    case AddressSpaceStatus::FrameOwnerUnavailable: return "frame-owner-unavailable";
    case AddressSpaceStatus::AllocationFailure: return "allocation-failure";
    case AddressSpaceStatus::UnsupportedConfiguration: return "unsupported-configuration";
    case AddressSpaceStatus::KernelMappingNotSupervisor: return "kernel-mapping-not-supervisor";
    case AddressSpaceStatus::UserWindowAlreadyMapped: return "user-window-already-mapped";
    case AddressSpaceStatus::InvalidAddress: return "invalid-address";
    case AddressSpaceStatus::InvalidPermissions: return "invalid-permissions";
    case AddressSpaceStatus::OwnershipMismatch: return "ownership-mismatch";
    case AddressSpaceStatus::MappingCollision: return "mapping-collision";
    case AddressSpaceStatus::MappingNotFound: return "mapping-not-found";
    case AddressSpaceStatus::AccessDenied: return "access-denied";
    case AddressSpaceStatus::AddressSpaceActive: return "address-space-active";
    case AddressSpaceStatus::CorruptPageTables: return "corrupt-page-tables";
    case AddressSpaceStatus::InvalidationFailure: return "invalidation-failure";
    default: return "unknown";
    }
}

} // namespace memory
} // namespace kernel
