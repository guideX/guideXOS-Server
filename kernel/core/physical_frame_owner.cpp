#include "include/kernel/physical_frame_owner.h"

namespace kernel {
namespace memory {
namespace {

static const uint64_t kPhysicalAddressLimit = 1ull << 52;
static const uint64_t kPageMask = kOwnedFrameBytes - 1u;

struct FrameRecord {
    uint32_t taskId;
    FrameKind kind;
    bool mapped;
    uint8_t padding[2];
};

struct OwnerState {
    uint64_t arenaBase;
    uint32_t frameCount;
    bool initialized;
    PhysicalAddressMapper mapPhysicalAddress;
    void* mapperContext;
    EnterMemoryCriticalSection enterCriticalSection;
    LeaveMemoryCriticalSection leaveCriticalSection;
    FrameRecord frames[kMaxTaskArenaFrames];
};

static_assert(sizeof(FirmwareMemoryDescriptor) == 40u,
              "EFI memory descriptor prefix must match the UEFI handoff layout");

// This fixed metadata table is part of kernel BSS and is never returned to the
// frame allocator. The allocator only manages the separately reserved arena.
static OwnerState s_owner;

struct CriticalGuard {
    uint64_t savedState;
    bool held;
    CriticalGuard() : savedState(0u), held(false)
    {
        if (s_owner.initialized && s_owner.enterCriticalSection &&
            s_owner.leaveCriticalSection) {
            savedState = s_owner.enterCriticalSection(s_owner.mapperContext);
            held = true;
        }
    }
    ~CriticalGuard()
    {
        if (held) s_owner.leaveCriticalSection(s_owner.mapperContext, savedState);
    }
};

static bool add_overflows(uint64_t left, uint64_t right)
{
    return left > UINT64_MAX - right;
}

static bool multiply_overflows(uint64_t left, uint64_t right)
{
    return right != 0u && left > UINT64_MAX / right;
}

static bool task_kind(FrameKind kind)
{
    return kind == FrameKind::TaskPrivateData ||
           kind == FrameKind::TaskPrivateCode ||
           kind == FrameKind::TaskPrivateStack ||
           kind == FrameKind::PageTableStorage;
}

static bool transferable_task_kind(FrameKind kind)
{
    return kind == FrameKind::TaskPrivateData ||
           kind == FrameKind::TaskPrivateCode ||
           kind == FrameKind::TaskPrivateStack;
}

static bool reservable_kind(FrameKind kind)
{
    return kind == FrameKind::Reserved ||
           kind == FrameKind::KernelOwned ||
           kind == FrameKind::SharedKernelMapping;
}

static bool read_descriptor(const FrameOwnerConfig& config, uint64_t index,
                            FirmwareMemoryDescriptor* outDescriptor)
{
    if (!outDescriptor || index >= config.memoryMapEntryCount ||
        multiply_overflows(index, config.memoryMapDescriptorSize)) {
        return false;
    }
    const uint64_t offset = index * config.memoryMapDescriptorSize;
    const uint8_t* bytes = static_cast<const uint8_t*>(config.memoryMapBytes);
    const FirmwareMemoryDescriptor* descriptor =
        reinterpret_cast<const FirmwareMemoryDescriptor*>(bytes + offset);
    outDescriptor->type = descriptor->type;
    outDescriptor->alignmentPadding = descriptor->alignmentPadding;
    outDescriptor->physicalStart = descriptor->physicalStart;
    outDescriptor->virtualStart = descriptor->virtualStart;
    outDescriptor->numberOfPages = descriptor->numberOfPages;
    outDescriptor->attributes = descriptor->attributes;
    return true;
}

static bool descriptor_range(const FirmwareMemoryDescriptor& descriptor,
                             uint64_t* outStart, uint64_t* outEnd)
{
    if (!outStart || !outEnd || descriptor.numberOfPages == 0u ||
        (descriptor.physicalStart & kPageMask) != 0u ||
        multiply_overflows(descriptor.numberOfPages, kOwnedFrameBytes)) {
        return false;
    }
    const uint64_t bytes = descriptor.numberOfPages * kOwnedFrameBytes;
    if (descriptor.physicalStart >= kPhysicalAddressLimit ||
        bytes > kPhysicalAddressLimit - descriptor.physicalStart) {
        return false;
    }
    *outStart = descriptor.physicalStart;
    *outEnd = descriptor.physicalStart + bytes;
    return true;
}

static bool arena_is_loader_data(const FrameOwnerConfig& config, uint64_t arenaEnd)
{
    bool contained = false;
    for (uint64_t i = 0; i < config.memoryMapEntryCount; ++i) {
        FirmwareMemoryDescriptor descriptor{};
        uint64_t start = 0;
        uint64_t end = 0;
        if (!read_descriptor(config, i, &descriptor) ||
            !descriptor_range(descriptor, &start, &end)) {
            return false;
        }
        if (descriptor.type == kEfiLoaderDataMemoryType &&
            config.taskArenaPhysicalBase >= start && arenaEnd <= end) {
            if (contained) return false;
            contained = true;
        }
    }
    return contained;
}

static bool frame_index(uint64_t physicalAddress, uint32_t* outIndex)
{
    if (!s_owner.initialized || !outIndex ||
        (physicalAddress & kPageMask) != 0u ||
        physicalAddress < s_owner.arenaBase) {
        return false;
    }
    const uint64_t offset = physicalAddress - s_owner.arenaBase;
    if ((offset & kPageMask) != 0u ||
        offset / kOwnedFrameBytes >= s_owner.frameCount) {
        return false;
    }
    *outIndex = static_cast<uint32_t>(offset / kOwnedFrameBytes);
    return true;
}

static void clear_bytes(void* pointer, uint64_t byteCount)
{
    uint8_t* bytes = static_cast<uint8_t*>(pointer);
    for (uint64_t i = 0; i < byteCount; ++i) bytes[i] = 0;
}

static void clear_record(FrameRecord& record)
{
    record.taskId = 0;
    record.kind = FrameKind::Free;
    record.mapped = false;
    record.padding[0] = 0;
    record.padding[1] = 0;
}

} // namespace

FrameStatus initialize_frame_owner(const FrameOwnerConfig& config)
{
    if (s_owner.initialized) return FrameStatus::AlreadyInitialized;
    if (!config.memoryMapBytes || !config.mapPhysicalAddress ||
        !config.enterCriticalSection || !config.leaveCriticalSection ||
        config.memoryMapPhysicalAddress == 0u ||
        (config.memoryMapPhysicalAddress & 7u) != 0u ||
        config.memoryMapEntryCount == 0u ||
        config.memoryMapEntryCount > kMaxFirmwareMemoryDescriptors ||
        config.memoryMapDescriptorSize < sizeof(FirmwareMemoryDescriptor) ||
        config.memoryMapDescriptorSize > 256u ||
        (config.memoryMapDescriptorSize & 7u) != 0u ||
        multiply_overflows(config.memoryMapEntryCount, config.memoryMapDescriptorSize) ||
        add_overflows(config.memoryMapPhysicalAddress,
                      config.memoryMapEntryCount * config.memoryMapDescriptorSize)) {
        return FrameStatus::InvalidMemoryMap;
    }

    const uint64_t mapBytes = config.memoryMapEntryCount * config.memoryMapDescriptorSize;
    const uintptr_t mapPointer = reinterpret_cast<uintptr_t>(config.memoryMapBytes);
    if ((mapPointer & 7u) != 0u || mapBytes == 0u ||
        mapBytes > UINTPTR_MAX - mapPointer) {
        return FrameStatus::InvalidMemoryMap;
    }

    // Validate every firmware range, then reject overlaps before trusting any
    // page as allocatable. Both loops have a fixed descriptor-count ceiling.
    for (uint64_t i = 0; i < config.memoryMapEntryCount; ++i) {
        FirmwareMemoryDescriptor left{};
        uint64_t leftStart = 0;
        uint64_t leftEnd = 0;
        if (!read_descriptor(config, i, &left) ||
            !descriptor_range(left, &leftStart, &leftEnd)) {
            return FrameStatus::InvalidMemoryMap;
        }
        for (uint64_t j = i + 1u; j < config.memoryMapEntryCount; ++j) {
            FirmwareMemoryDescriptor right{};
            uint64_t rightStart = 0;
            uint64_t rightEnd = 0;
            if (!read_descriptor(config, j, &right) ||
                !descriptor_range(right, &rightStart, &rightEnd)) {
                return FrameStatus::InvalidMemoryMap;
            }
            if (leftStart < rightEnd && rightStart < leftEnd) {
                return FrameStatus::OverlappingMemoryMap;
            }
        }
    }

    if (config.taskArenaBytes == 0u ||
        (config.taskArenaPhysicalBase & kPageMask) != 0u ||
        (config.taskArenaBytes & kPageMask) != 0u ||
        config.taskArenaPhysicalBase >= kPhysicalAddressLimit ||
        config.taskArenaBytes > kPhysicalAddressLimit - config.taskArenaPhysicalBase) {
        return FrameStatus::InvalidRange;
    }
    const uint64_t frameCount64 = config.taskArenaBytes / kOwnedFrameBytes;
    if (frameCount64 == 0u || frameCount64 > kMaxTaskArenaFrames) {
        return FrameStatus::ArenaTooLarge;
    }
    const uint64_t arenaEnd = config.taskArenaPhysicalBase + config.taskArenaBytes;
    if (!arena_is_loader_data(config, arenaEnd)) {
        return FrameStatus::ArenaNotReservedByFirmware;
    }

    void* first = config.mapPhysicalAddress(config.mapperContext,
                                             config.taskArenaPhysicalBase);
    void* last = config.mapPhysicalAddress(config.mapperContext,
                                            arenaEnd - kOwnedFrameBytes);
    if (!first || !last ||
        (reinterpret_cast<uintptr_t>(first) & kPageMask) != 0u ||
        (reinterpret_cast<uintptr_t>(last) & kPageMask) != 0u) {
        return FrameStatus::PhysicalMappingUnavailable;
    }

    s_owner.arenaBase = config.taskArenaPhysicalBase;
    s_owner.frameCount = static_cast<uint32_t>(frameCount64);
    s_owner.mapPhysicalAddress = config.mapPhysicalAddress;
    s_owner.mapperContext = config.mapperContext;
    s_owner.enterCriticalSection = config.enterCriticalSection;
    s_owner.leaveCriticalSection = config.leaveCriticalSection;
    for (uint32_t i = 0; i < s_owner.frameCount; ++i) clear_record(s_owner.frames[i]);
    s_owner.initialized = true;
    return FrameStatus::Ok;
}

bool frame_owner_initialized()
{
    return s_owner.initialized;
}

FrameStatus allocate_task_frame(FrameKind kind, uint32_t taskId,
                                uint64_t* outPhysicalAddress)
{
    if (!s_owner.initialized) return FrameStatus::NotInitialized;
    if (!outPhysicalAddress || taskId == 0u || !task_kind(kind)) {
        return FrameStatus::InvalidOwner;
    }
    CriticalGuard guard;
    *outPhysicalAddress = 0u;
    for (uint32_t i = 0; i < s_owner.frameCount; ++i) {
        FrameRecord& record = s_owner.frames[i];
        if (record.kind != FrameKind::Free) continue;
        const uint64_t physical = s_owner.arenaBase +
                                  static_cast<uint64_t>(i) * kOwnedFrameBytes;
        void* pointer = s_owner.mapPhysicalAddress(s_owner.mapperContext, physical);
        if (!pointer || (reinterpret_cast<uintptr_t>(pointer) & kPageMask) != 0u) {
            return FrameStatus::PhysicalMappingUnavailable;
        }
        clear_bytes(pointer, kOwnedFrameBytes);
        record.kind = kind;
        record.taskId = taskId;
        record.mapped = false;
        *outPhysicalAddress = physical;
        return FrameStatus::Ok;
    }
    return FrameStatus::OutOfFrames;
}

FrameStatus reserve_frame_range(uint64_t physicalBase, uint64_t byteCount, FrameKind kind)
{
    if (!s_owner.initialized) return FrameStatus::NotInitialized;
    CriticalGuard guard;
    if (!reservable_kind(kind) || byteCount == 0u ||
        (physicalBase & kPageMask) != 0u || (byteCount & kPageMask) != 0u ||
        add_overflows(physicalBase, byteCount)) {
        return FrameStatus::InvalidRange;
    }
    uint32_t firstIndex = 0;
    uint32_t endIndex = 0;
    if (!frame_index(physicalBase, &firstIndex) ||
        !frame_index(physicalBase + byteCount - kOwnedFrameBytes, &endIndex) ||
        endIndex < firstIndex) {
        return FrameStatus::InvalidRange;
    }
    for (uint32_t i = firstIndex; i <= endIndex; ++i) {
        if (s_owner.frames[i].kind != FrameKind::Free) {
            return FrameStatus::OwnershipMismatch;
        }
    }
    for (uint32_t i = firstIndex; i <= endIndex; ++i) {
        s_owner.frames[i].kind = kind;
        s_owner.frames[i].taskId = 0u;
        s_owner.frames[i].mapped = false;
    }
    return FrameStatus::Ok;
}

FrameStatus query_frame(uint64_t physicalAddress, FrameInfo* outInfo)
{
    if (!s_owner.initialized) return FrameStatus::NotInitialized;
    if (!outInfo || (physicalAddress & kPageMask) != 0u) return FrameStatus::InvalidRange;
    CriticalGuard guard;
    outInfo->physicalAddress = physicalAddress;
    outInfo->taskId = 0u;
    outInfo->kind = FrameKind::Unavailable;
    outInfo->mapped = false;
    uint32_t index = 0;
    if (!frame_index(physicalAddress, &index)) return FrameStatus::Ok;
    outInfo->taskId = s_owner.frames[index].taskId;
    outInfo->kind = s_owner.frames[index].kind;
    outInfo->mapped = s_owner.frames[index].mapped;
    return FrameStatus::Ok;
}

FrameStatus transfer_frame_ownership(uint64_t physicalAddress, FrameKind expectedKind,
                                     FrameKind newKind, uint32_t taskId)
{
    if (!s_owner.initialized) return FrameStatus::NotInitialized;
    if (taskId == 0u || !transferable_task_kind(expectedKind) ||
        !transferable_task_kind(newKind)) {
        return FrameStatus::InvalidOwner;
    }
    CriticalGuard guard;
    uint32_t index = 0;
    if (!frame_index(physicalAddress, &index)) return FrameStatus::InvalidRange;
    FrameRecord& record = s_owner.frames[index];
    if (record.kind != expectedKind || record.taskId != taskId) {
        return FrameStatus::OwnershipMismatch;
    }
    if (record.mapped) return FrameStatus::FrameMapped;
    record.kind = newKind;
    return FrameStatus::Ok;
}

FrameStatus release_task_frame(uint64_t physicalAddress, FrameKind expectedKind,
                               uint32_t taskId)
{
    if (!s_owner.initialized) return FrameStatus::NotInitialized;
    if (taskId == 0u || !task_kind(expectedKind)) return FrameStatus::InvalidOwner;
    CriticalGuard guard;
    uint32_t index = 0;
    if (!frame_index(physicalAddress, &index)) return FrameStatus::InvalidRange;
    FrameRecord& record = s_owner.frames[index];
    if (record.kind == FrameKind::Free) return FrameStatus::DoubleRelease;
    if (record.kind != expectedKind || record.taskId != taskId) {
        return FrameStatus::OwnershipMismatch;
    }
    if (record.mapped) return FrameStatus::FrameMapped;
    void* pointer = s_owner.mapPhysicalAddress(s_owner.mapperContext, physicalAddress);
    if (!pointer || (reinterpret_cast<uintptr_t>(pointer) & kPageMask) != 0u) {
        return FrameStatus::PhysicalMappingUnavailable;
    }
    clear_bytes(pointer, kOwnedFrameBytes);
    clear_record(record);
    return FrameStatus::Ok;
}

FrameStatus set_task_frame_mapped(uint64_t physicalAddress, FrameKind expectedKind,
                                  uint32_t taskId, bool mapped)
{
    if (!s_owner.initialized) return FrameStatus::NotInitialized;
    if (taskId == 0u || !task_kind(expectedKind)) return FrameStatus::InvalidOwner;
    CriticalGuard guard;
    uint32_t index = 0;
    if (!frame_index(physicalAddress, &index)) return FrameStatus::InvalidRange;
    FrameRecord& record = s_owner.frames[index];
    if (record.kind != expectedKind || record.taskId != taskId) {
        return FrameStatus::OwnershipMismatch;
    }
    if (record.mapped == mapped) {
        return mapped ? FrameStatus::FrameMapped : FrameStatus::OwnershipMismatch;
    }
    record.mapped = mapped;
    return FrameStatus::Ok;
}

void* task_frame_pointer(uint64_t physicalAddress, FrameKind expectedKind, uint32_t taskId)
{
    if (!s_owner.initialized || taskId == 0u || !task_kind(expectedKind)) return nullptr;
    CriticalGuard guard;
    uint32_t index = 0;
    if (!frame_index(physicalAddress, &index)) return nullptr;
    const FrameRecord& record = s_owner.frames[index];
    if (record.kind != expectedKind || record.taskId != taskId) return nullptr;
    void* pointer = s_owner.mapPhysicalAddress(s_owner.mapperContext, physicalAddress);
    if (!pointer || (reinterpret_cast<uintptr_t>(pointer) & kPageMask) != 0u) return nullptr;
    return pointer;
}

void get_frame_accounting(FrameAccounting* outAccounting)
{
    if (!outAccounting) return;
    *outAccounting = FrameAccounting{};
    if (!s_owner.initialized) return;
    CriticalGuard guard;
    outAccounting->capacityFrames = s_owner.frameCount;
    for (uint32_t i = 0; i < s_owner.frameCount; ++i) {
        const FrameRecord& record = s_owner.frames[i];
        switch (record.kind) {
        case FrameKind::Free: ++outAccounting->freeFrames; break;
        case FrameKind::Reserved: ++outAccounting->reservedFrames; break;
        case FrameKind::KernelOwned: ++outAccounting->kernelOwnedFrames; break;
        case FrameKind::SharedKernelMapping: ++outAccounting->sharedKernelFrames; break;
        case FrameKind::TaskPrivateData: ++outAccounting->taskDataFrames; break;
        case FrameKind::TaskPrivateCode: ++outAccounting->taskCodeFrames; break;
        case FrameKind::TaskPrivateStack: ++outAccounting->taskStackFrames; break;
        case FrameKind::PageTableStorage: ++outAccounting->pageTableFrames; break;
        default: break;
        }
        if (record.mapped) ++outAccounting->mappedTaskFrames;
    }
}

const char* frame_status_name(FrameStatus status)
{
    switch (status) {
    case FrameStatus::Ok: return "ok";
    case FrameStatus::NotInitialized: return "not-initialized";
    case FrameStatus::AlreadyInitialized: return "already-initialized";
    case FrameStatus::InvalidArgument: return "invalid-argument";
    case FrameStatus::InvalidMemoryMap: return "invalid-memory-map";
    case FrameStatus::OverlappingMemoryMap: return "overlapping-memory-map";
    case FrameStatus::ArenaNotReservedByFirmware: return "arena-not-firmware-reserved";
    case FrameStatus::ArenaTooLarge: return "arena-too-large";
    case FrameStatus::OutOfFrames: return "out-of-frames";
    case FrameStatus::InvalidRange: return "invalid-range";
    case FrameStatus::OwnershipMismatch: return "ownership-mismatch";
    case FrameStatus::DoubleRelease: return "double-release";
    case FrameStatus::FrameMapped: return "frame-mapped";
    case FrameStatus::InvalidOwner: return "invalid-owner";
    case FrameStatus::PhysicalMappingUnavailable: return "physical-mapping-unavailable";
    default: return "unknown";
    }
}

} // namespace memory
} // namespace kernel
