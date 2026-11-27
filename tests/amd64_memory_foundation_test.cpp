#include <cstdio>
#include <cstdint>

#include "../kernel/core/include/kernel/boot_page_table_validation.h"
#include "../kernel/core/include/kernel/physical_frame_owner.h"
#include "../kernel/core/include/kernel/task_address_space.h"

using namespace kernel::memory;

namespace {

static const uint64_t kArenaBase = 0x20000000ull;
static const uint32_t kArenaPages = 64u;
static const uint64_t kKernelBase = 0x10000000ull;
static const uint64_t kMapBufferPhysical = 0x30000000ull;
static const uint64_t kPresent = 1ull;
static const uint64_t kWritable = 2ull;
static const uint64_t kUser = 4ull;
static const uint64_t kPageSize = 1ull << 7;
static const uint64_t kNoExecute = 1ull << 63;
static const uint64_t kSyntheticPageTableBase = 0x40000000ull;
static const uint32_t kSyntheticPageTableCount = 525u;

alignas(4096) static uint8_t s_arena[kArenaPages * kOwnedFrameBytes];
alignas(4096) static uint8_t s_kernelPages[8 * kOwnedFrameBytes];
alignas(4096) static uint8_t s_syntheticPageTables[
    kSyntheticPageTableCount * kOwnedFrameBytes];
alignas(8) static FirmwareMemoryDescriptor s_memoryMap[4];
static uint64_t s_currentCr3 = kKernelBase;
static uint32_t s_invalidations = 0u;
static bool s_nxSupported = true;
static bool s_tlbSafe = true;
static bool s_invalidationFails = false;
static uint64_t s_unmappablePhysical = UINT64_MAX;

static void expect(bool condition, const char* label, uint32_t* failures)
{
    if (!condition) {
        std::printf("FAIL: %s\n", label);
        ++*failures;
    }
}

static void* map_physical(void*, uint64_t physicalAddress)
{
    if (physicalAddress == s_unmappablePhysical) return nullptr;
    if (physicalAddress >= kArenaBase &&
        physicalAddress < kArenaBase + sizeof(s_arena)) {
        return &s_arena[physicalAddress - kArenaBase];
    }
    if (physicalAddress >= kKernelBase &&
        physicalAddress < kKernelBase + sizeof(s_kernelPages)) {
        return &s_kernelPages[physicalAddress - kKernelBase];
    }
    return nullptr;
}

static void* map_synthetic_page_table(void*, uint64_t physicalAddress)
{
    if (physicalAddress < kSyntheticPageTableBase ||
        physicalAddress >= kSyntheticPageTableBase + sizeof(s_syntheticPageTables)) {
        return nullptr;
    }
    return &s_syntheticPageTables[physicalAddress - kSyntheticPageTableBase];
}

static bool test_boot_page_table_validation(uint32_t* failures)
{
    for (uint32_t i = 0; i < sizeof(s_syntheticPageTables); ++i) {
        s_syntheticPageTables[i] = 0u;
    }

    uint64_t* root = static_cast<uint64_t*>(
        map_synthetic_page_table(nullptr, kSyntheticPageTableBase));
    for (uint32_t i = 0; i < 512u; ++i) {
        const uint64_t pdptPhysical =
            kSyntheticPageTableBase + (1u + i) * kOwnedFrameBytes;
        root[i] = pdptPhysical | kPresent | kWritable;
        if (i < 12u) {
            uint64_t* pdpt = static_cast<uint64_t*>(
                map_synthetic_page_table(nullptr, pdptPhysical));
            const uint64_t pdPhysical =
                kSyntheticPageTableBase + (513u + i) * kOwnedFrameBytes;
            pdpt[0] = pdPhysical | kPresent | kWritable;
        }
    }

    uint32_t visitedPages = 0u;
    BootPageTableValidationFailure failure = BootPageTableValidationFailure::None;
    const bool acceptedAtConfiguredLimit = boot_page_table_tree_avoids_range(
               kSyntheticPageTableBase, 0u, 0x1000000000ull, 0x1000001000ull,
               &map_synthetic_page_table, nullptr, &visitedPages,
               kMaximumBootPageTablePages, &failure);
    expect(acceptedAtConfiguredLimit && visitedPages == kSyntheticPageTableCount &&
           failure == BootPageTableValidationFailure::None &&
           kMaximumBootPageTablePages >= kSyntheticPageTableCount,
           "production boot audit accepts the loader's 525-page tree", failures);

    visitedPages = 0u;
    failure = BootPageTableValidationFailure::None;
    expect(!boot_page_table_tree_avoids_range(
        kSyntheticPageTableBase, 0u, 0x1000000000ull, 0x1000001000ull,
        &map_synthetic_page_table, nullptr, &visitedPages, 512u, &failure) &&
        visitedPages == 513u && failure == BootPageTableValidationFailure::PageCountLimit,
        "the former 512-page bound rejects the boot tree at page 513", failures);

    visitedPages = 0u;
    failure = BootPageTableValidationFailure::None;
    expect(!boot_page_table_tree_avoids_range(
               kSyntheticPageTableBase, 0u,
               kSyntheticPageTableBase + kOwnedFrameBytes,
               kSyntheticPageTableBase + 2u * kOwnedFrameBytes,
               &map_synthetic_page_table, nullptr, &visitedPages,
               kMaximumBootPageTablePages, &failure) &&
           failure == BootPageTableValidationFailure::ProtectedRangeOverlap,
           "boot page-table audit distinguishes protected-range overlap", failures);
    return *failures == 0u;
}

static uint64_t current_cr3(void*) { return s_currentCr3; }
static bool ensure_nx(void*) { return s_nxSupported; }
static bool supports_safe_tlb(void*) { return s_tlbSafe; }
static uint64_t enter_critical(void*) { return 0u; }
static void leave_critical(void*, uint64_t) {}
static bool invalidate_page(void*, uint64_t)
{
    ++s_invalidations;
    return !s_invalidationFails;
}

static AddressSpacePlatform make_platform()
{
    AddressSpacePlatform platform{};
    platform.mapPhysicalAddress = &map_physical;
    platform.readCurrentCr3 = &current_cr3;
    platform.ensureNxEnabled = &ensure_nx;
    platform.supportsSafeTlbInvalidation = &supports_safe_tlb;
    platform.invalidatePage = &invalidate_page;
    platform.enterCriticalSection = &enter_critical;
    platform.leaveCriticalSection = &leave_critical;
    return platform;
}

static void set_descriptor(FirmwareMemoryDescriptor* descriptor, uint32_t type,
                           uint64_t base, uint64_t pages)
{
    descriptor->type = type;
    descriptor->alignmentPadding = 0u;
    descriptor->physicalStart = base;
    descriptor->virtualStart = 0u;
    descriptor->numberOfPages = pages;
    descriptor->attributes = 0u;
}

static FrameOwnerConfig make_config(const FirmwareMemoryDescriptor* map,
                                   uint64_t entryCount = 4u,
                                   uint64_t descriptorSize = sizeof(FirmwareMemoryDescriptor))
{
    FrameOwnerConfig config{};
    config.memoryMapBytes = map;
    config.memoryMapPhysicalAddress = kMapBufferPhysical;
    config.memoryMapEntryCount = entryCount;
    config.memoryMapDescriptorSize = descriptorSize;
    config.taskArenaPhysicalBase = kArenaBase;
    config.taskArenaBytes = sizeof(s_arena);
    config.mapPhysicalAddress = &map_physical;
    config.enterCriticalSection = &enter_critical;
    config.leaveCriticalSection = &leave_critical;
    return config;
}

static bool accounting_equal(const FrameAccounting& a, const FrameAccounting& b)
{
    return a.capacityFrames == b.capacityFrames && a.freeFrames == b.freeFrames &&
           a.reservedFrames == b.reservedFrames && a.kernelOwnedFrames == b.kernelOwnedFrames &&
           a.sharedKernelFrames == b.sharedKernelFrames && a.taskDataFrames == b.taskDataFrames &&
           a.taskCodeFrames == b.taskCodeFrames && a.taskStackFrames == b.taskStackFrames &&
           a.pageTableFrames == b.pageTableFrames && a.mappedTaskFrames == b.mappedTaskFrames;
}

static uint64_t* table_at(uint64_t physicalAddress)
{
    return static_cast<uint64_t*>(map_physical(nullptr, physicalAddress));
}

static void initialize_kernel_page_tables()
{
    for (uint32_t i = 0; i < sizeof(s_kernelPages); ++i) s_kernelPages[i] = 0;
    uint64_t* root = table_at(kKernelBase);
    uint64_t* pdpt = table_at(kKernelBase + 0x1000u);
    uint64_t* pd = table_at(kKernelBase + 0x2000u);
    uint64_t* pt = table_at(kKernelBase + 0x3000u);
    uint64_t* largePdpt = table_at(kKernelBase + 0x5000u);
    uint64_t* largePd = table_at(kKernelBase + 0x6000u);
    root[0] = (kKernelBase + 0x1000u) | kPresent | kWritable; // U/S clear
    pdpt[0] = (kKernelBase + 0x2000u) | kPresent | kWritable;
    pd[0] = (kKernelBase + 0x3000u) | kPresent | kWritable;
    pt[0] = (kKernelBase + 0x4000u) | kPresent | kWritable;
    root[2] = (kKernelBase + 0x5000u) | kPresent | kWritable;
    largePdpt[0] = (kKernelBase + 0x6000u) | kPresent | kWritable;
    largePd[0] = 0x40000000ull | kPresent | kWritable | kPageSize; // 2 MiB leaf.
}

static bool test_invalid_boot_maps(uint32_t* failures)
{
    uint64_t beforeInitialize = 0xDEADBEEFull;
    expect(allocate_task_frame(FrameKind::TaskPrivateData, 1u, &beforeInitialize) ==
           FrameStatus::NotInitialized && beforeInitialize == 0xDEADBEEFull,
           "allocation is unavailable before initialization", failures);

    FrameOwnerConfig config = make_config(s_memoryMap);
    config.memoryMapEntryCount = 0u;
    expect(initialize_frame_owner(config) == FrameStatus::InvalidMemoryMap,
           "missing firmware memory ranges rejected", failures);

    config = make_config(s_memoryMap);
    FirmwareMemoryDescriptor overlap[4] = {};
    for (uint32_t i = 0; i < 4u; ++i) overlap[i] = s_memoryMap[i];
    overlap[0].numberOfPages = (kKernelBase / kOwnedFrameBytes) + 1u;
    config = make_config(overlap);
    expect(initialize_frame_owner(config) == FrameStatus::OverlappingMemoryMap,
           "overlapping firmware descriptors rejected", failures);

    FirmwareMemoryDescriptor malformed[4] = {};
    for (uint32_t i = 0; i < 4u; ++i) malformed[i] = s_memoryMap[i];
    malformed[1].physicalStart += 1u;
    config = make_config(malformed);
    expect(initialize_frame_owner(config) == FrameStatus::InvalidMemoryMap,
           "unaligned firmware range rejected", failures);

    config = make_config(s_memoryMap, 4u, sizeof(FirmwareMemoryDescriptor) - 8u);
    expect(initialize_frame_owner(config) == FrameStatus::InvalidMemoryMap,
           "short descriptor stride rejected", failures);

    FirmwareMemoryDescriptor noArena[4] = {};
    for (uint32_t i = 0; i < 4u; ++i) noArena[i] = s_memoryMap[i];
    noArena[2].type = 7u; // Conventional memory is not this boot-owned arena.
    config = make_config(noArena);
    expect(initialize_frame_owner(config) == FrameStatus::ArenaNotReservedByFirmware,
           "arena must be explicitly EFI loader data", failures);

    config = make_config(s_memoryMap);
    config.taskArenaPhysicalBase = UINT64_MAX - 0xFFFu;
    config.taskArenaBytes = 0x2000u;
    expect(initialize_frame_owner(config) == FrameStatus::InvalidRange,
           "overflowing arena rejected", failures);
    return *failures == 0u;
}

static bool test_frames(uint32_t* failures)
{
    FrameOwnerConfig config = make_config(s_memoryMap);
    expect(initialize_frame_owner(config) == FrameStatus::Ok,
           "validated firmware map initializes frame owner", failures);
    expect(frame_owner_initialized(), "frame owner becomes authoritative", failures);

    FrameAccounting initial{};
    get_frame_accounting(&initial);
    expect(initial.capacityFrames == kArenaPages && initial.freeFrames == kArenaPages,
           "bounded arena capacity accounting", failures);

    expect(reserve_frame_range(kArenaBase, kOwnedFrameBytes, FrameKind::KernelOwned) == FrameStatus::Ok,
           "reserve one arena frame for the kernel", failures);
    FrameInfo info{};
    expect(query_frame(kArenaBase, &info) == FrameStatus::Ok &&
           info.kind == FrameKind::KernelOwned,
           "reserved kernel frame reports its owner", failures);
    expect(reserve_frame_range(kArenaBase, kOwnedFrameBytes, FrameKind::Reserved) ==
           FrameStatus::OwnershipMismatch,
           "overlapping reservation rejected", failures);
    expect(reserve_frame_range(kArenaBase + 2u * kOwnedFrameBytes, kOwnedFrameBytes,
                               FrameKind::Reserved) == FrameStatus::Ok &&
           reserve_frame_range(kArenaBase + 3u * kOwnedFrameBytes, kOwnedFrameBytes,
                               FrameKind::SharedKernelMapping) == FrameStatus::Ok,
           "reserved and shared-kernel owner categories are supported", failures);
    FrameInfo reservedInfo{};
    FrameInfo sharedInfo{};
    expect(query_frame(kArenaBase + 2u * kOwnedFrameBytes, &reservedInfo) == FrameStatus::Ok &&
           reservedInfo.kind == FrameKind::Reserved &&
           query_frame(kArenaBase + 3u * kOwnedFrameBytes, &sharedInfo) == FrameStatus::Ok &&
           sharedInfo.kind == FrameKind::SharedKernelMapping,
           "reserved owner categories are queryable", failures);
    expect(reserve_frame_range(kArenaBase + 1u, kOwnedFrameBytes, FrameKind::Reserved) ==
           FrameStatus::InvalidRange,
           "unaligned reservation rejected", failures);
    expect(reserve_frame_range(kArenaBase + sizeof(s_arena), kOwnedFrameBytes,
                               FrameKind::Reserved) == FrameStatus::InvalidRange,
           "out-of-arena reservation rejected", failures);

    uint64_t unavailableOut = 0xDEADBEEFull;
    s_unmappablePhysical = kArenaBase + kOwnedFrameBytes;
    expect(allocate_task_frame(FrameKind::TaskPrivateData, 10u, &unavailableOut) ==
           FrameStatus::PhysicalMappingUnavailable && unavailableOut == 0u,
           "unavailable backing fails without ownership transfer", failures);
    s_unmappablePhysical = UINT64_MAX;
    expect(query_frame(kArenaBase + kOwnedFrameBytes, &info) == FrameStatus::Ok &&
           info.kind == FrameKind::Free,
           "failed backing leaves the frame free", failures);

    uint64_t frame = 0u;
    expect(allocate_task_frame(FrameKind::TaskPrivateData, 10u, &frame) == FrameStatus::Ok,
           "allocate an owned data frame", failures);
    expect(frame != kArenaBase, "allocator skips kernel-owned frame", failures);
    expect(query_frame(frame, &info) == FrameStatus::Ok &&
           info.kind == FrameKind::TaskPrivateData && info.taskId == 10u && !info.mapped,
           "allocation has one explicit task owner", failures);
    uint8_t* page = static_cast<uint8_t*>(map_physical(nullptr, frame));
    for (uint32_t i = 0; i < kOwnedFrameBytes; ++i) page[i] = 0xA5u;
    expect(release_task_frame(frame, FrameKind::TaskPrivateData, 11u) ==
           FrameStatus::OwnershipMismatch,
           "foreign task cannot release a frame", failures);
    expect(release_task_frame(frame, FrameKind::TaskPrivateData, 10u) == FrameStatus::Ok,
           "owner can release its frame", failures);
    expect(release_task_frame(frame, FrameKind::TaskPrivateData, 10u) == FrameStatus::DoubleRelease,
           "double release is detected", failures);
    uint64_t reused = 0u;
    expect(allocate_task_frame(FrameKind::TaskPrivateData, 12u, &reused) == FrameStatus::Ok,
           "reallocate cleared frame", failures);
    expect(reused == frame, "released frame is reusable", failures);
    page = static_cast<uint8_t*>(map_physical(nullptr, reused));
    bool allZero = true;
    for (uint32_t i = 0; i < kOwnedFrameBytes; ++i) allZero = allZero && page[i] == 0u;
    expect(allZero, "reused frame contents are cleared", failures);
    expect(transfer_frame_ownership(reused, FrameKind::TaskPrivateData,
                                   FrameKind::TaskPrivateCode, 99u) ==
           FrameStatus::OwnershipMismatch,
           "foreign task cannot transfer ownership", failures);
    expect(transfer_frame_ownership(reused, FrameKind::TaskPrivateData,
                                   FrameKind::TaskPrivateCode, 12u) == FrameStatus::Ok,
           "explicit private-owner transfer succeeds", failures);
    expect(release_task_frame(reused, FrameKind::TaskPrivateData, 12u) ==
           FrameStatus::OwnershipMismatch,
           "old owner category cannot release after transfer", failures);
    expect(release_task_frame(reused, FrameKind::TaskPrivateCode, 12u) == FrameStatus::Ok,
           "new owner category can release transferred frame", failures);

    uint64_t allocations[kArenaPages] = {};
    uint32_t allocated = 0u;
    while (allocated < kArenaPages &&
           allocate_task_frame(FrameKind::TaskPrivateData, 77u,
                               &allocations[allocated]) == FrameStatus::Ok) {
        ++allocated;
    }
    uint64_t exhaustedOut = 0xDEADBEEFull;
    expect(allocated == kArenaPages - 3u,
           "all and only free arena frames allocate", failures);
    expect(allocate_task_frame(FrameKind::TaskPrivateData, 77u, &exhaustedOut) ==
           FrameStatus::OutOfFrames && exhaustedOut == 0u,
           "exhaustion is explicit and clears output", failures);
    for (uint32_t i = 0; i < allocated; ++i) {
        expect(release_task_frame(allocations[i], FrameKind::TaskPrivateData, 77u) ==
               FrameStatus::Ok, "exhaustion cleanup releases exact owner", failures);
    }
    FrameAccounting after{};
    get_frame_accounting(&after);
    expect(after.freeFrames == kArenaPages - 3u && after.kernelOwnedFrames == 1u &&
           after.reservedFrames == 1u && after.sharedKernelFrames == 1u,
           "frame accounting returns after exhaustion", failures);
    return *failures == 0u;
}

static bool test_address_spaces(uint32_t* failures)
{
    const AddressSpacePlatform platform = make_platform();
    FrameAccounting baseline{};
    FrameAccounting after{};
    get_frame_accounting(&baseline);

    for (uint32_t repetition = 0; repetition < 24u; ++repetition) {
        DiagnosticFixtureResult result{};
        const uint32_t invalidationsBefore = s_invalidations;
        const AddressSpaceStatus status = run_task_address_space_fixture(
            platform, 100u + repetition, &result);
        expect(status == AddressSpaceStatus::Ok && result.mappingsCreated == 3u,
               "real address-space fixture builds code/data/stack mappings", failures);
        expect(result.kernelMappingStayedSupervisor && result.guardPageStayedUnmapped,
               "fixture preserves supervisor mapping and unmapped guard", failures);
        expect(result.ownershipReturnedToBaseline,
               "fixture teardown restores frame accounting", failures);
        expect(result.invalidationsIssued == 3u,
               "teardown invalidates every user translation", failures);
        expect(s_invalidations - invalidationsBefore == 3u,
               "real invalidation callback ran for each mapped address", failures);
    }

    TaskAddressSpace space{};
    expect(create_task_address_space(&space, 300u, kKernelBase, platform) ==
           AddressSpaceStatus::Ok, "create private page-table root", failures);
    uint64_t* taskRoot = table_at(space.rootPhysicalAddress);
    uint64_t* kernelRoot = table_at(kKernelBase);
    expect(taskRoot && taskRoot[0] == kernelRoot[0],
           "kernel page-table branch is shared without transfer", failures);
    MappingPermissions largeKernelMapping{};
    const uint64_t largeKernelAddress = (2ull << 39) + 0x1234u;
    expect(query_task_mapping(&space, largeKernelAddress, &largeKernelMapping, platform) ==
           AddressSpaceStatus::Ok && largeKernelMapping.present &&
           !largeKernelMapping.userAccessible &&
           largeKernelMapping.physicalAddress == 0x40001234ull,
           "shared supervisor 2 MiB mappings retain effective permissions", failures);

    uint64_t code = 0u;
    uint64_t data = 0u;
    expect(allocate_task_frame(FrameKind::TaskPrivateCode, 300u, &code) == FrameStatus::Ok &&
           allocate_task_frame(FrameKind::TaskPrivateData, 300u, &data) == FrameStatus::Ok,
           "allocate task code and data", failures);
    expect(map_task_frame(&space, kTaskUserCodeAddress, code, platform) == AddressSpaceStatus::Ok,
           "map non-writable executable user code", failures);
    expect(map_task_frame(&space, kTaskUserCodeAddress, data, platform) ==
           AddressSpaceStatus::MappingCollision,
           "duplicate virtual mapping is rejected", failures);
    expect(map_task_frame(&space, kTaskUserDataAddress, data, platform) ==
           AddressSpaceStatus::Ok,
           "map writable non-executable user data", failures);
    expect(release_task_frame(data, FrameKind::TaskPrivateData, 300u) ==
           FrameStatus::FrameMapped,
           "mapped task frame cannot be released early", failures);
    expect(map_task_frame(&space, kTaskUserDataAddress, code, platform) ==
           AddressSpaceStatus::OwnershipMismatch,
           "a task frame cannot be aliased at another address", failures);
    expect(map_task_frame(&space, kTaskUserStackAddress, data, platform) ==
           AddressSpaceStatus::OwnershipMismatch,
           "a mapped frame cannot be aliased at a second user address", failures);
    expect(map_task_frame(&space, kTaskUserDataAddress, 0x10004000u, platform) ==
           AddressSpaceStatus::OwnershipMismatch,
           "kernel-owned frame cannot receive a user alias", failures);
    expect(map_task_frame(&space, 0x0000800000000000ull, data, platform) ==
           AddressSpaceStatus::InvalidAddress,
           "noncanonical user address is rejected", failures);

    MappingPermissions codePermissions{};
    expect(query_task_mapping(&space, kTaskUserCodeAddress, &codePermissions, platform) ==
           AddressSpaceStatus::Ok && codePermissions.userAccessible &&
           !codePermissions.writable && codePermissions.executable,
           "effective code permissions are user read/execute", failures);
    expect(verify_task_access(&space, kTaskUserCodeAddress, UserAccess::Write, platform) ==
           AddressSpaceStatus::AccessDenied,
           "user code write is denied", failures);

    const uint64_t rootEntry = taskRoot[kTaskUserPml4Index];
    uint64_t* pdpt = table_at(rootEntry & 0x000FFFFFFFFFF000ull);
    uint64_t* pd = table_at(pdpt[0] & 0x000FFFFFFFFFF000ull);
    uint64_t* pt = table_at(pd[0] & 0x000FFFFFFFFFF000ull);
    const uint64_t rootSaved = taskRoot[kTaskUserPml4Index];
    taskRoot[kTaskUserPml4Index] &= ~kUser;
    expect(verify_task_access(&space, kTaskUserCodeAddress, UserAccess::Read, platform) ==
           AddressSpaceStatus::AccessDenied,
           "effective U/S permission includes PML4", failures);
    taskRoot[kTaskUserPml4Index] = rootSaved;
    const uint64_t pdptSaved = pdpt[0];
    pdpt[0] &= ~kWritable;
    expect(verify_task_access(&space, kTaskUserDataAddress, UserAccess::Write, platform) ==
           AddressSpaceStatus::AccessDenied,
           "effective writable permission includes PDPT", failures);
    pdpt[0] = pdptSaved;
    const uint64_t pdSaved = pd[0];
    pd[0] |= kNoExecute;
    expect(verify_task_access(&space, kTaskUserCodeAddress, UserAccess::Execute, platform) ==
           AddressSpaceStatus::AccessDenied,
           "effective execute permission includes PDE NX", failures);
    pd[0] = pdSaved;
    pd[0] |= kPageSize;
    MappingPermissions hugePage{};
    expect(query_task_mapping(&space, kTaskUserCodeAddress, &hugePage, platform) ==
           AddressSpaceStatus::UnsupportedConfiguration,
           "unsupported large-page path is rejected", failures);
    pd[0] = pdSaved;
    (void)pt;

    const uint64_t sharedKernelEntry = taskRoot[0];
    s_currentCr3 = space.rootPhysicalAddress;
    expect(destroy_task_address_space(&space, platform) == AddressSpaceStatus::AddressSpaceActive,
           "active address space cannot be torn down", failures);
    s_currentCr3 = kKernelBase;
    expect(destroy_task_address_space(&space, platform) == AddressSpaceStatus::Ok,
           "inactive address space tears down safely", failures);
    expect(kernelRoot[0] == sharedKernelEntry,
           "teardown preserves shared kernel page tables", failures);

    kernelRoot[5] = kKernelBase | kPresent | kUser;
    TaskAddressSpace exposedKernel{};
    expect(create_task_address_space(&exposedKernel, 302u, kKernelBase, platform) ==
           AddressSpaceStatus::KernelMappingNotSupervisor,
           "task root rejects a user-enabled kernel PML4 entry", failures);
    kernelRoot[5] = 0u;
    kernelRoot[kTaskUserPml4Index] = kPresent | kWritable;
    TaskAddressSpace occupiedWindow{};
    expect(create_task_address_space(&occupiedWindow, 303u, kKernelBase, platform) ==
           AddressSpaceStatus::UserWindowAlreadyMapped,
           "task root rejects a preoccupied user window", failures);
    kernelRoot[kTaskUserPml4Index] = 0u;

    TaskAddressSpace invalidationSpace{};
    expect(create_task_address_space(&invalidationSpace, 304u, kKernelBase, platform) ==
           AddressSpaceStatus::Ok,
           "create invalidation rollback fixture", failures);
    uint64_t invalidationCode = 0u;
    expect(allocate_task_frame(FrameKind::TaskPrivateCode, 304u, &invalidationCode) ==
           FrameStatus::Ok &&
           map_task_frame(&invalidationSpace, kTaskUserCodeAddress,
                          invalidationCode, platform) == AddressSpaceStatus::Ok,
           "map invalidation rollback code", failures);
    FrameAccounting beforeInvalidationFailure{};
    get_frame_accounting(&beforeInvalidationFailure);
    s_invalidationFails = true;
    expect(destroy_task_address_space(&invalidationSpace, platform) ==
           AddressSpaceStatus::InvalidationFailure,
           "failed TLB invalidation prevents frame reuse", failures);
    s_invalidationFails = false;
    get_frame_accounting(&after);
    expect(accounting_equal(beforeInvalidationFailure, after),
           "invalidation failure leaves ownership intact", failures);
    expect(destroy_task_address_space(&invalidationSpace, platform) ==
           AddressSpaceStatus::Ok,
           "address space can be reclaimed after invalidation recovery", failures);
    get_frame_accounting(&after);
    expect(accounting_equal(baseline, after),
           "mapping collision and teardown leak no frames", failures);

    s_nxSupported = false;
    TaskAddressSpace unsupported{};
    expect(create_task_address_space(&unsupported, 301u, kKernelBase, platform) ==
           AddressSpaceStatus::UnsupportedConfiguration,
           "unsupported NX configuration fails closed", failures);
    s_nxSupported = true;
    s_tlbSafe = false;
    TaskAddressSpace pcidUnsupported{};
    expect(create_task_address_space(&pcidUnsupported, 305u, kKernelBase, platform) ==
           AddressSpaceStatus::UnsupportedConfiguration,
           "PCID without safe invalidation support fails closed", failures);
    s_tlbSafe = true;

    uint64_t held[kArenaPages] = {};
    uint32_t heldCount = 0u;
    FrameAccounting beforePartial{};
    get_frame_accounting(&beforePartial);
    while (beforePartial.freeFrames - heldCount > 6u) {
        if (allocate_task_frame(FrameKind::TaskPrivateData, 900u,
                                &held[heldCount]) != FrameStatus::Ok) break;
        ++heldCount;
    }
    DiagnosticFixtureResult partial{};
    expect(run_task_address_space_fixture(platform, 901u, &partial) ==
           AddressSpaceStatus::AllocationFailure,
           "intermediate table exhaustion fails fixture", failures);
    expect(partial.ownershipReturnedToBaseline,
           "partial root and page-table construction rolls back", failures);
    for (uint32_t i = 0; i < heldCount; ++i) {
        expect(release_task_frame(held[i], FrameKind::TaskPrivateData, 900u) == FrameStatus::Ok,
               "release allocation pressure frames", failures);
    }
    get_frame_accounting(&after);
    expect(accounting_equal(baseline, after),
           "partial construction leaves no owner metadata behind", failures);
    return *failures == 0u;
}

} // namespace

int main()
{
    uint32_t failures = 0u;
    set_descriptor(&s_memoryMap[0], 0u, 0u, 256u); // Reserved low memory.
    set_descriptor(&s_memoryMap[1], 1u, kKernelBase, 8u); // Kernel and page tables.
    set_descriptor(&s_memoryMap[2], kEfiLoaderDataMemoryType, kArenaBase, kArenaPages);
    set_descriptor(&s_memoryMap[3], kEfiLoaderDataMemoryType, kMapBufferPhysical, 1u);
    initialize_kernel_page_tables();
    test_boot_page_table_validation(&failures);

    test_invalid_boot_maps(&failures);
    if (failures == 0u) test_frames(&failures);
    if (failures == 0u) test_address_spaces(&failures);
    if (failures == 0u) {
        std::printf("PASS: bounded frame ownership, rollback, effective AMD64 permissions, shared kernel mappings, and repeated teardown\n");
        std::printf("LIMIT: hosted execution of the production memory code; no hardware CPL3 transition or guest page-fault claim\n");
        return 0;
    }
    std::printf("FAILURES=%u\n", failures);
    return 1;
}
