#include "include/kernel/partition_block_view.h"

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace kernel {
namespace block {

namespace {

struct ViewSlot {
    bool active;
    bool closing;
    uint64_t generation;
    uint16_t references;
    uint16_t inFlight;
    PartitionIdentity identity;
    uint64_t parentSectorCount;
    uint32_t sectorSize;
    bool readOnly;
};

static ViewSlot s_views[MAX_PARTITION_BLOCK_VIEWS];
static uint64_t s_nextGeneration = 0;
static volatile uint32_t s_viewLock = 0;

static void lock_views()
{
#if defined(_MSC_VER)
    while (_InterlockedCompareExchange(
        reinterpret_cast<volatile long*>(&s_viewLock), 1, 0) != 0) {}
#else
    while (__sync_lock_test_and_set(&s_viewLock, 1) != 0) {}
#endif
}

static void unlock_views()
{
#if defined(_MSC_VER)
    _InterlockedExchange(reinterpret_cast<volatile long*>(&s_viewLock), 0);
#else
    __sync_lock_release(&s_viewLock);
#endif
}

static void clear_bytes(void* value, size_t length)
{
    uint8_t* bytes = static_cast<uint8_t*>(value);
    for (size_t i = 0; i < length; ++i) bytes[i] = 0;
}

static bool valid_sector_size(uint32_t size)
{
    return size >= 512 && size <= 4096 && (size & (size - 1)) == 0;
}

static bool valid_handle_locked(PartitionViewHandle handle)
{
    return handle.slot < MAX_PARTITION_BLOCK_VIEWS &&
        s_views[handle.slot].active && !s_views[handle.slot].closing &&
        s_views[handle.slot].generation == handle.generation;
}

static void finish_close_locked(ViewSlot& view)
{
    if (!view.closing || view.references != 0 || view.inFlight != 0) return;
    const uint64_t generation = view.generation;
    clear_bytes(&view, sizeof(view));
    view.generation = generation;
}

static bool identity_equal(const PartitionIdentity& left,
                           const PartitionIdentity& right)
{
    if (!left.valid || !right.valid || left.scheme != right.scheme ||
        left.parentDeviceIndex != right.parentDeviceIndex ||
        left.parentRegistrationId != right.parentRegistrationId ||
        left.partitionNumber != right.partitionNumber ||
        left.startLba != right.startLba || left.endLba != right.endLba ||
        left.sectorCount != right.sectorCount) return false;
    if (left.scheme == storage::PARTITION_SCHEME_GPT) {
        for (size_t i = 0; i < sizeof(left.uniqueGuid); ++i)
            if (left.uniqueGuid[i] != right.uniqueGuid[i]) return false;
        return true;
    }
    return left.scheme == storage::PARTITION_SCHEME_MBR &&
        left.mbrType == right.mbrType;
}

struct ViewSnapshot {
    PartitionIdentity identity;
    uint64_t parentSectorCount;
    uint32_t sectorSize;
    bool readOnly;
};

static bool endpoint_matches_view(const BlockEndpoint& endpoint,
                                  const ViewSnapshot& view)
{
    return endpoint.kind == ENDPOINT_PARTITION_VIEW &&
        endpoint.deviceIndex == view.identity.parentDeviceIndex &&
        endpoint.registrationId == view.identity.parentRegistrationId &&
        endpoint.totalSectors == view.identity.sectorCount &&
        endpoint.sectorSize == view.sectorSize &&
        endpoint.readOnly == view.readOnly &&
        endpoint.viewSlot < MAX_PARTITION_BLOCK_VIEWS;
}

static bool begin_view_io(PartitionViewHandle handle, ViewSnapshot& out)
{
    if (handle.slot >= MAX_PARTITION_BLOCK_VIEWS) return false;
    lock_views();
    ViewSlot& view = s_views[handle.slot];
    if (!valid_handle_locked(handle) || view.inFlight == UINT16_MAX) {
        unlock_views();
        return false;
    }
    ++view.inFlight;
    out.identity = view.identity;
    out.parentSectorCount = view.parentSectorCount;
    out.sectorSize = view.sectorSize;
    out.readOnly = view.readOnly;
    unlock_views();
    return true;
}

static void end_view_io(PartitionViewHandle handle)
{
    if (handle.slot >= MAX_PARTITION_BLOCK_VIEWS) return;
    lock_views();
    ViewSlot& view = s_views[handle.slot];
    if (view.active && view.generation == handle.generation && view.inFlight)
        --view.inFlight;
    finish_close_locked(view);
    unlock_views();
}

static bool pin_parent(const PartitionIdentity& identity,
                       uint64_t expectedSectorCount, uint32_t expectedSectorSize,
                       BlockDevice& parent)
{
    if (!identity.valid || identity.parentRegistrationId == 0 ||
        !pin_device(identity.parentDeviceIndex, identity.parentRegistrationId))
        return false;
    if (!copy_device(identity.parentDeviceIndex, parent) ||
        parent.registrationId != identity.parentRegistrationId ||
        parent.totalSectors != expectedSectorCount ||
        parent.sectorSize != expectedSectorSize || !parent.readFn) {
        unpin_device(identity.parentDeviceIndex, identity.parentRegistrationId);
        return false;
    }
    return true;
}

static Status endpoint_io(const BlockEndpoint& endpoint, uint64_t lba,
                          uint32_t count, void* readBuffer,
                          const void* writeBuffer, bool write)
{
    if (count == 0 || (write ? writeBuffer == nullptr : readBuffer == nullptr))
        return BLOCK_ERR_INVALID;
    if (endpoint.kind == ENDPOINT_DEVICE) {
        if (endpoint.registrationId == 0 ||
            lba > endpoint.totalSectors ||
            static_cast<uint64_t>(count) > endpoint.totalSectors - lba ||
            (write && endpoint.readOnly)) return write && endpoint.readOnly
                ? BLOCK_ERR_UNSUPPORTED : BLOCK_ERR_INVALID;
        if (!pin_device(endpoint.deviceIndex, endpoint.registrationId))
            return BLOCK_ERR_NO_MEDIA;
        BlockDevice parent = {};
        Status status = BLOCK_ERR_NO_MEDIA;
        if (copy_device(endpoint.deviceIndex, parent) &&
            parent.registrationId == endpoint.registrationId &&
            parent.totalSectors == endpoint.totalSectors &&
            parent.sectorSize == endpoint.sectorSize) {
            status = write
                ? write_sectors(endpoint.deviceIndex, lba, count, writeBuffer)
                : read_sectors(endpoint.deviceIndex, lba, count, readBuffer);
        }
        unpin_device(endpoint.deviceIndex, endpoint.registrationId);
        return status;
    }
    if (endpoint.kind != ENDPOINT_PARTITION_VIEW ||
        endpoint.viewSlot >= MAX_PARTITION_BLOCK_VIEWS) return BLOCK_ERR_INVALID;

    const PartitionViewHandle handle = { endpoint.viewSlot,
                                         endpoint.viewGeneration };
    ViewSnapshot view = {};
    if (!begin_view_io(handle, view)) return BLOCK_ERR_NO_MEDIA;
    Status status = BLOCK_ERR_INVALID;
    const uint64_t start = view.identity.startLba;
    const uint64_t sectors = view.identity.sectorCount;
    const bool endpointValid = endpoint_matches_view(endpoint, view);
    const bool localRangeValid = lba <= sectors &&
        static_cast<uint64_t>(count) <= sectors - lba;
    const bool translatedLbaValid = localRangeValid &&
        start <= UINT64_MAX - lba;
    const uint64_t parentLba = translatedLbaValid ? start + lba : 0;
    const bool parentRangeValid = translatedLbaValid && parentLba <=
        view.parentSectorCount &&
        static_cast<uint64_t>(count) <= view.parentSectorCount - parentLba;
    if (!endpointValid) {
        status = BLOCK_ERR_INVALID;
    } else if (write && view.readOnly) {
        status = BLOCK_ERR_UNSUPPORTED;
    } else if (localRangeValid && translatedLbaValid && parentRangeValid) {
        BlockDevice parent = {};
        if (!pin_parent(view.identity, view.parentSectorCount,
                        view.sectorSize, parent)) {
            status = BLOCK_ERR_NO_MEDIA;
        } else {
            status = write
                ? write_sectors(view.identity.parentDeviceIndex, parentLba,
                                count, writeBuffer)
                : read_sectors(view.identity.parentDeviceIndex, parentLba,
                               count, readBuffer);
            unpin_device(view.identity.parentDeviceIndex,
                         view.identity.parentRegistrationId);
        }
    }
    end_view_io(handle);
    return status;
}

} // namespace

bool same_partition_identity(const PartitionIdentity& left,
                             const PartitionIdentity& right)
{
    return identity_equal(left, right);
}

bool make_device_endpoint(uint8_t deviceIndex, BlockEndpoint& out)
{
    BlockDevice device = {};
    if (!copy_device(deviceIndex, device) || !device.readFn ||
        !valid_sector_size(device.sectorSize) || device.totalSectors == 0)
        return false;
    BlockEndpoint endpoint = {};
    endpoint.kind = ENDPOINT_DEVICE;
    endpoint.deviceIndex = deviceIndex;
    endpoint.registrationId = device.registrationId;
    endpoint.totalSectors = device.totalSectors;
    endpoint.sectorSize = device.sectorSize;
    endpoint.readOnly = !device.writeFn;
    out = endpoint;
    return true;
}

bool create_partition_view(uint8_t deviceIndex,
                           storage::PartitionScheme scheme,
                           const storage::PartitionEntry& partition,
                           PartitionViewHandle& outHandle,
                           BlockEndpoint& outEndpoint,
                           PartitionIdentity* outIdentity)
{
    BlockDevice parent = {};
    if (!copy_device(deviceIndex, parent) || !parent.readFn ||
        !valid_sector_size(parent.sectorSize) || parent.totalSectors == 0 ||
        partition.partitionNumber == 0 || partition.sectorCount == 0 ||
        partition.startLba > parent.totalSectors ||
        partition.sectorCount > parent.totalSectors - partition.startLba ||
        partition.startLba > UINT64_MAX - (partition.sectorCount - 1))
        return false;
    if (partition.endLba != partition.startLba + partition.sectorCount - 1)
        return false;
    if ((scheme != storage::PARTITION_SCHEME_GPT &&
         scheme != storage::PARTITION_SCHEME_MBR) ||
        partition.isGpt != (scheme == storage::PARTITION_SCHEME_GPT))
        return false;

    PartitionIdentity identity = {};
    identity.valid = true;
    identity.scheme = scheme;
    identity.parentDeviceIndex = deviceIndex;
    identity.parentRegistrationId = parent.registrationId;
    identity.parentRegistryGeneration = registry_generation();
    identity.partitionNumber = partition.partitionNumber;
    identity.mbrType = partition.mbrType;
    for (size_t i = 0; i < sizeof(identity.uniqueGuid); ++i)
        identity.uniqueGuid[i] = partition.uniqueGuid[i];
    identity.startLba = partition.startLba;
    identity.sectorCount = partition.sectorCount;
    identity.endLba = partition.startLba + partition.sectorCount - 1;

    lock_views();
    for (uint8_t i = 0; i < MAX_PARTITION_BLOCK_VIEWS; ++i) {
        ViewSlot& view = s_views[i];
        if (view.active && !view.closing && identity_equal(view.identity, identity)) {
            if (view.references == UINT16_MAX) {
                unlock_views();
                return false;
            }
            ++view.references;
            outHandle.slot = i;
            outHandle.generation = view.generation;
            outEndpoint.kind = ENDPOINT_PARTITION_VIEW;
            outEndpoint.deviceIndex = deviceIndex;
            outEndpoint.registrationId = parent.registrationId;
            outEndpoint.totalSectors = view.identity.sectorCount;
            outEndpoint.sectorSize = view.sectorSize;
            outEndpoint.viewSlot = i;
            outEndpoint.viewGeneration = view.generation;
            outEndpoint.readOnly = view.readOnly;
            if (outIdentity) *outIdentity = view.identity;
            unlock_views();
            return true;
        }
    }

    uint8_t freeSlot = 0xFF;
    for (uint8_t i = 0; i < MAX_PARTITION_BLOCK_VIEWS; ++i) {
        if (!s_views[i].active) { freeSlot = i; break; }
    }
    if (freeSlot == 0xFF || s_nextGeneration == UINT64_MAX) {
        unlock_views();
        return false;
    }
    ViewSlot& view = s_views[freeSlot];
    clear_bytes(&view, sizeof(view));
    view.active = true;
    view.generation = ++s_nextGeneration;
    view.references = 1;
    view.identity = identity;
    view.parentSectorCount = parent.totalSectors;
    view.sectorSize = parent.sectorSize;
    view.readOnly = !parent.writeFn;
    outHandle.slot = freeSlot;
    outHandle.generation = view.generation;
    outEndpoint.kind = ENDPOINT_PARTITION_VIEW;
    outEndpoint.deviceIndex = deviceIndex;
    outEndpoint.registrationId = parent.registrationId;
    outEndpoint.totalSectors = partition.sectorCount;
    outEndpoint.sectorSize = parent.sectorSize;
    outEndpoint.viewSlot = freeSlot;
    outEndpoint.viewGeneration = view.generation;
    outEndpoint.readOnly = view.readOnly;
    if (outIdentity) *outIdentity = identity;
    unlock_views();
    if (!partition_view_parent_valid(outHandle)) {
        release_partition_view(outHandle);
        return false;
    }
    return true;
}

bool retain_partition_view(PartitionViewHandle handle)
{
    if (handle.slot >= MAX_PARTITION_BLOCK_VIEWS) return false;
    lock_views();
    ViewSlot& view = s_views[handle.slot];
    const bool retained = valid_handle_locked(handle) &&
        view.references != UINT16_MAX;
    if (retained) ++view.references;
    unlock_views();
    return retained;
}

void release_partition_view(PartitionViewHandle handle)
{
    if (handle.slot >= MAX_PARTITION_BLOCK_VIEWS) return;
    lock_views();
    ViewSlot& view = s_views[handle.slot];
    if (view.active && !view.closing && view.generation == handle.generation &&
        view.references != 0) {
        --view.references;
        if (view.references == 0) view.closing = true;
        finish_close_locked(view);
    }
    unlock_views();
}

bool get_partition_view_info(PartitionViewHandle handle,
                             PartitionViewInfo& out)
{
    if (handle.slot >= MAX_PARTITION_BLOCK_VIEWS) return false;
    lock_views();
    const ViewSlot& view = s_views[handle.slot];
    if (!valid_handle_locked(handle)) {
        unlock_views();
        return false;
    }
    out.handle = handle;
    out.identity = view.identity;
    out.parentSectorCount = view.parentSectorCount;
    out.sectorSize = view.sectorSize;
    out.sectorCount = view.identity.sectorCount;
    out.readOnly = view.readOnly;
    unlock_views();
    return true;
}

bool partition_view_parent_valid(PartitionViewHandle handle)
{
    PartitionViewInfo info = {};
    if (!get_partition_view_info(handle, info)) return false;
    if (!pin_device(info.identity.parentDeviceIndex,
                    info.identity.parentRegistrationId)) return false;
    BlockDevice parent = {};
    const bool valid = copy_device(info.identity.parentDeviceIndex, parent) &&
        parent.registrationId == info.identity.parentRegistrationId &&
        parent.totalSectors == info.parentSectorCount &&
        parent.totalSectors >= info.identity.startLba &&
        info.identity.sectorCount <= parent.totalSectors - info.identity.startLba &&
        parent.sectorSize == info.sectorSize;
    unpin_device(info.identity.parentDeviceIndex,
                 info.identity.parentRegistrationId);
    return valid;
}

Status read_endpoint(const BlockEndpoint& endpoint, uint64_t lba,
                     uint32_t count, void* buffer)
{
    return endpoint_io(endpoint, lba, count, buffer, nullptr, false);
}

Status write_endpoint(const BlockEndpoint& endpoint, uint64_t lba,
                      uint32_t count, const void* buffer)
{
    return endpoint_io(endpoint, lba, count, nullptr, buffer, true);
}

Status flush_endpoint(const BlockEndpoint& endpoint)
{
    if (endpoint.kind != ENDPOINT_DEVICE &&
        endpoint.kind != ENDPOINT_PARTITION_VIEW) return BLOCK_ERR_INVALID;
    const PartitionViewHandle handle = { endpoint.viewSlot,
                                         endpoint.viewGeneration };
    ViewSnapshot view = {};
    const bool isView = endpoint.kind == ENDPOINT_PARTITION_VIEW;
    if (isView && !begin_view_io(handle, view)) return BLOCK_ERR_NO_MEDIA;
    if (isView && !endpoint_matches_view(endpoint, view)) {
        end_view_io(handle);
        return BLOCK_ERR_INVALID;
    }
    const uint8_t parentIndex = isView
        ? view.identity.parentDeviceIndex : endpoint.deviceIndex;
    const uint64_t parentId = isView
        ? view.identity.parentRegistrationId : endpoint.registrationId;
    if (!pin_device(parentIndex, parentId)) {
        if (isView) end_view_io(handle);
        return BLOCK_ERR_NO_MEDIA;
    }
    BlockDevice parent = {};
    Status status = BLOCK_ERR_NO_MEDIA;
    const bool sameParent = copy_device(parentIndex, parent) &&
        parent.registrationId == parentId &&
        parent.sectorSize == endpoint.sectorSize &&
        (isView ? parent.totalSectors == view.parentSectorCount
                : parent.totalSectors == endpoint.totalSectors);
    if (sameParent) status = flush(parentIndex);
    unpin_device(parentIndex, parentId);
    if (isView) end_view_io(handle);
    return status;
}

bool endpoint_supports_durable_writes(const BlockEndpoint& endpoint)
{
    if (endpoint.kind != ENDPOINT_DEVICE &&
        endpoint.kind != ENDPOINT_PARTITION_VIEW) return false;
    const bool isView = endpoint.kind == ENDPOINT_PARTITION_VIEW;
    const PartitionViewHandle handle = { endpoint.viewSlot,
                                         endpoint.viewGeneration };
    ViewSnapshot view = {};
    if (isView) {
        if (!begin_view_io(handle, view)) return false;
        if (view.readOnly || !endpoint_matches_view(endpoint, view)) {
            end_view_io(handle);
            return false;
        }
    }
    const uint8_t parentIndex = isView
        ? view.identity.parentDeviceIndex : endpoint.deviceIndex;
    const uint64_t parentId = isView
        ? view.identity.parentRegistrationId : endpoint.registrationId;
    if (endpoint.readOnly || !pin_device(parentIndex, parentId)) {
        if (isView) end_view_io(handle);
        return false;
    }
    BlockDevice parent = {};
    bool durable = false;
    if (copy_device(parentIndex, parent) &&
        parent.registrationId == parentId && parent.writeFn &&
        parent.sectorSize == endpoint.sectorSize &&
        (!isView || parent.totalSectors == view.parentSectorCount) &&
        parent.type != BDEV_RAMDISK) {
        durable = parent.flushFn
            ? (parent.flushSemanticsKnown && !parent.writeCompletionDurable)
            : (parent.writeCompletionDurable && !parent.flushSemanticsKnown);
    }
    unpin_device(parentIndex, parentId);
    if (isView) end_view_io(handle);
    return durable;
}

} // namespace block
} // namespace kernel
