#pragma once

#include <stdint.h>

namespace stellar_native {

static const uint64_t kDatasetBytes = 32117373u;
static const uint32_t kHashChunkBytes = 65536u;
static const uint32_t kMaxSegments = 16u;
static const uint32_t kMaxBodyIdBytes = 32u;
static const uint32_t kMaxRecordWords = 155u;

enum class Error : uint32_t {
    Ok = 0,
    InvalidArgument,
    NotInitialized,
    ReadFailure,
    DatasetSizeMismatch,
    DatasetHashMismatch,
    InvalidHeader,
    UnsupportedFormat,
    InvalidIndex,
    BodyUnavailable,
    EpochOutOfCoverage,
    InvalidEpoch,
    InvalidRecord
};

typedef bool (*ReadAt)(void* user, uint64_t offset, void* buffer,
                       uint32_t capacity, uint32_t* bytesRead);

struct Reader {
    ReadAt readAt;
    void* user;
    uint64_t fileSize;
};

struct Segment {
    char bodyId[kMaxBodyIdBytes];
    char centerBodyId[kMaxBodyIdBytes];
    int32_t targetId;
    int32_t centerId;
    int32_t frameId;
    int32_t segmentType;
    int32_t degree;
    double recordIntervalSeconds;
    double initEt;
    uint64_t recordCount;
    double coverageStartJd;
    double coverageEndJd;
    uint64_t dataOffset;
    uint64_t recordWords;
};

struct Vector3 {
    double x;
    double y;
    double z;
};

// Caller-owned fixed storage: no allocation, STL, exceptions, or runtime
// dependency is required by the native application.
struct Context {
    Reader reader;
    Segment segments[kMaxSegments];
    uint8_t scratch[kHashChunkBytes];
    uint32_t segmentCount;
    bool initialized;
};

// Authenticates the complete bundled SNEPH04B DE440s file, then validates
// its bounded index and center chains. Failure leaves context uninitialized.
Error initialize(Context* context, const Reader* reader);
void shutdown(Context* context);

// Returns the barycentric J2000 geometric position in kilometres at ET/TDB
// seconds past J2000. No extrapolation is performed.
Error evaluatePosition(const Context* context, int32_t targetId,
                       double etTdbSeconds, Vector3* positionKm);

const char* errorText(Error error);
const char* datasetSha256();

} // namespace stellar_native
