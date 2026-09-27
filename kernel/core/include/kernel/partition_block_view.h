#ifndef KERNEL_PARTITION_BLOCK_VIEW_H
#define KERNEL_PARTITION_BLOCK_VIEW_H

#include "kernel/block_device.h"
#include "kernel/partition_table.h"

namespace kernel {
namespace block {

static const uint8_t MAX_PARTITION_BLOCK_VIEWS = 16;

struct PartitionViewHandle {
    uint8_t slot;
    uint64_t generation;
};

enum EndpointKind : uint8_t {
    ENDPOINT_INVALID = 0,
    ENDPOINT_DEVICE,
    ENDPOINT_PARTITION_VIEW,
};

// A pointer-free, non-owning block endpoint. A partition endpoint is usable
// only while its generation-checked registry handle remains mounted.
struct BlockEndpoint {
    EndpointKind kind;
    uint8_t deviceIndex;
    uint64_t registrationId;
    uint64_t totalSectors;
    uint32_t sectorSize;
    uint8_t viewSlot;
    uint64_t viewGeneration;
    bool readOnly;
};

struct PartitionIdentity {
    bool valid;
    storage::PartitionScheme scheme;
    uint8_t parentDeviceIndex;
    uint64_t parentRegistrationId;
    uint64_t parentRegistryGeneration;
    uint16_t partitionNumber;
    uint8_t mbrType;
    uint8_t uniqueGuid[16];
    uint64_t startLba;
    uint64_t endLba;
    uint64_t sectorCount;
};

struct PartitionViewInfo {
    PartitionViewHandle handle;
    PartitionIdentity identity;
    uint64_t parentSectorCount;
    uint32_t sectorSize;
    uint64_t sectorCount;
    bool readOnly;
};

bool make_device_endpoint(uint8_t deviceIndex, BlockEndpoint& out);
bool create_partition_view(uint8_t deviceIndex,
                           storage::PartitionScheme scheme,
                           const storage::PartitionEntry& partition,
                           PartitionViewHandle& outHandle,
                           BlockEndpoint& outEndpoint,
                           PartitionIdentity* outIdentity = nullptr);
bool retain_partition_view(PartitionViewHandle handle);
void release_partition_view(PartitionViewHandle handle);
bool get_partition_view_info(PartitionViewHandle handle,
                             PartitionViewInfo& out);
bool partition_view_parent_valid(PartitionViewHandle handle);
bool same_partition_identity(const PartitionIdentity& left,
                             const PartitionIdentity& right);

Status read_endpoint(const BlockEndpoint& endpoint, uint64_t lba,
                     uint32_t count, void* buffer);
Status write_endpoint(const BlockEndpoint& endpoint, uint64_t lba,
                      uint32_t count, const void* buffer);
Status flush_endpoint(const BlockEndpoint& endpoint);

bool endpoint_supports_durable_writes(const BlockEndpoint& endpoint);

} // namespace block
} // namespace kernel

#endif
