#include "ephemeris_core.h"

#include <float.h>
#include <stddef.h>

namespace stellar_native {
namespace {

static const uint32_t kSupportedVersion = 1u;
static const uint32_t kMaxProvenanceBytes = 16384u;
static const uint32_t kMaxDegree = 50u;
static const double kRecordDomainSlack = 1.0e-9;
static const uint64_t kProgressIntervalBytes = 4u * 1024u * 1024u;

static const uint8_t kExpectedSha256[32] = {
    0xd5, 0x2b, 0x91, 0x57, 0x3c, 0x29, 0xe6, 0xa4,
    0xe5, 0xb6, 0xdc, 0x19, 0xc4, 0xaf, 0x4d, 0x97,
    0x47, 0xf6, 0xda, 0x21, 0x22, 0xf5, 0xb3, 0xaf,
    0xf4, 0x50, 0x36, 0x84, 0xa9, 0x6d, 0x2c, 0x0a
};

struct Sha256 {
    uint32_t state[8];
    uint64_t bitCount;
    uint8_t block[64];
    uint32_t blockBytes;
};

static const uint32_t kShaRound[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static uint32_t rotate_right(uint32_t value, uint32_t amount) {
    return (value >> amount) | (value << (32u - amount));
}

static uint32_t read_be_u32(const uint8_t* bytes) {
    return (static_cast<uint32_t>(bytes[0]) << 24) |
           (static_cast<uint32_t>(bytes[1]) << 16) |
           (static_cast<uint32_t>(bytes[2]) << 8) |
           static_cast<uint32_t>(bytes[3]);
}

static void write_be_u32(uint8_t* bytes, uint32_t value) {
    bytes[0] = static_cast<uint8_t>(value >> 24);
    bytes[1] = static_cast<uint8_t>(value >> 16);
    bytes[2] = static_cast<uint8_t>(value >> 8);
    bytes[3] = static_cast<uint8_t>(value);
}

static void sha_transform(Sha256* sha, const uint8_t* block) {
    uint32_t words[64];
    for (uint32_t i = 0; i < 16u; ++i) words[i] = read_be_u32(block + i * 4u);
    for (uint32_t i = 16u; i < 64u; ++i) {
        const uint32_t s0 = rotate_right(words[i - 15u], 7u) ^
                            rotate_right(words[i - 15u], 18u) ^ (words[i - 15u] >> 3u);
        const uint32_t s1 = rotate_right(words[i - 2u], 17u) ^
                            rotate_right(words[i - 2u], 19u) ^ (words[i - 2u] >> 10u);
        words[i] = words[i - 16u] + s0 + words[i - 7u] + s1;
    }

    uint32_t a = sha->state[0];
    uint32_t b = sha->state[1];
    uint32_t c = sha->state[2];
    uint32_t d = sha->state[3];
    uint32_t e = sha->state[4];
    uint32_t f = sha->state[5];
    uint32_t g = sha->state[6];
    uint32_t h = sha->state[7];

    for (uint32_t i = 0; i < 64u; ++i) {
        const uint32_t sum1 = rotate_right(e, 6u) ^ rotate_right(e, 11u) ^ rotate_right(e, 25u);
        const uint32_t choose = (e & f) ^ ((~e) & g);
        const uint32_t temp1 = h + sum1 + choose + kShaRound[i] + words[i];
        const uint32_t sum0 = rotate_right(a, 2u) ^ rotate_right(a, 13u) ^ rotate_right(a, 22u);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = sum0 + majority;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    sha->state[0] += a; sha->state[1] += b; sha->state[2] += c; sha->state[3] += d;
    sha->state[4] += e; sha->state[5] += f; sha->state[6] += g; sha->state[7] += h;
}

static void sha_init(Sha256* sha) {
    static const uint32_t initial[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };
    for (uint32_t i = 0; i < 8u; ++i) sha->state[i] = initial[i];
    sha->bitCount = 0;
    sha->blockBytes = 0;
}

static void sha_update(Sha256* sha, const uint8_t* bytes, uint32_t count) {
    sha->bitCount += static_cast<uint64_t>(count) * 8u;
    uint32_t offset = 0;
    while (offset < count) {
        uint32_t available = 64u - sha->blockBytes;
        uint32_t amount = count - offset;
        if (amount > available) amount = available;
        for (uint32_t i = 0; i < amount; ++i) sha->block[sha->blockBytes + i] = bytes[offset + i];
        sha->blockBytes += amount;
        offset += amount;
        if (sha->blockBytes == 64u) {
            sha_transform(sha, sha->block);
            sha->blockBytes = 0;
        }
    }
}

static void sha_finish(Sha256* sha, uint8_t digest[32]) {
    const uint64_t messageBits = sha->bitCount;
    sha->block[sha->blockBytes++] = 0x80u;
    if (sha->blockBytes > 56u) {
        while (sha->blockBytes < 64u) sha->block[sha->blockBytes++] = 0;
        sha_transform(sha, sha->block);
        sha->blockBytes = 0;
    }
    while (sha->blockBytes < 56u) sha->block[sha->blockBytes++] = 0;
    for (uint32_t i = 0; i < 8u; ++i)
        sha->block[56u + i] = static_cast<uint8_t>(messageBits >> (56u - i * 8u));
    sha_transform(sha, sha->block);
    for (uint32_t i = 0; i < 8u; ++i) write_be_u32(digest + i * 4u, sha->state[i]);
}

static bool finite(double value) {
    return value == value && value <= DBL_MAX && value >= -DBL_MAX;
}

static void zero_bytes(void* memory, size_t count) {
    uint8_t* bytes = static_cast<uint8_t*>(memory);
    for (size_t i = 0; i < count; ++i) bytes[i] = 0;
}

static bool add_u64(uint64_t a, uint64_t b, uint64_t* result) {
    if (!result || b > UINT64_MAX - a) return false;
    *result = a + b;
    return true;
}

static bool read_exact(const Reader& reader, uint64_t offset, void* destination, uint32_t count) {
    if (!reader.readAt || (!destination && count != 0u)) return false;
    uint8_t* output = static_cast<uint8_t*>(destination);
    uint32_t complete = 0;
    while (complete < count) {
        uint32_t amount = 0;
        const uint32_t requested = count - complete > kHashChunkBytes
            ? kHashChunkBytes : count - complete;
        uint64_t at = 0;
        if (!add_u64(offset, complete, &at) || !reader.readAt(reader.user, at,
                output + complete, requested, &amount) || amount > requested || amount == 0u) {
            return false;
        }
        complete += amount;
    }
    return true;
}

static void report_progress(const Context* context, ProgressStage stage, uint64_t value) {
    if (context && context->reader.progress)
        context->reader.progress(context->reader.progressUser, stage, value);
}

static uint32_t u32_le(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

static uint64_t u64_le(const uint8_t* bytes) {
    return static_cast<uint64_t>(u32_le(bytes)) |
           (static_cast<uint64_t>(u32_le(bytes + 4)) << 32);
}

static int32_t i32_le(const uint8_t* bytes) {
    return static_cast<int32_t>(u32_le(bytes));
}

static double f64_le(const uint8_t* bytes) {
    union Bits { uint64_t bits; double value; } bits;
    bits.bits = u64_le(bytes);
    return bits.value;
}

static bool read_u32(const Reader& reader, uint64_t* cursor, uint32_t* result) {
    uint8_t bytes[4];
    if (!cursor || !result || !read_exact(reader, *cursor, bytes, sizeof(bytes))) return false;
    *result = u32_le(bytes);
    return add_u64(*cursor, sizeof(bytes), cursor);
}

static bool read_fields(const Reader& reader, uint64_t* cursor, uint8_t* bytes, uint32_t count) {
    if (!cursor || !read_exact(reader, *cursor, bytes, count)) return false;
    return add_u64(*cursor, count, cursor);
}

static bool read_id(const Reader& reader, uint64_t* cursor, char* destination, uint32_t capacity) {
    uint32_t length = 0;
    if (!destination || capacity < 2u || !read_u32(reader, cursor, &length) ||
        length == 0u || length >= capacity) return false;
    uint8_t bytes[kMaxBodyIdBytes];
    if (!read_fields(reader, cursor, bytes, length)) return false;
    for (uint32_t i = 0; i < length; ++i) {
        if (bytes[i] == 0u || bytes[i] < 0x20u || bytes[i] > 0x7eu) return false;
        destination[i] = static_cast<char>(bytes[i]);
    }
    destination[length] = '\0';
    return true;
}

static int find_segment(const Context* context, int32_t targetId) {
    for (uint32_t i = 0; i < context->segmentCount; ++i)
        if (context->segments[i].targetId == targetId) return static_cast<int>(i);
    return -1;
}

static Error parse_index(Context* context) {
    uint8_t header[20];
    if (!read_exact(context->reader, 8u, header, sizeof(header))) return Error::ReadFailure;
    const uint32_t version = u32_le(header);
    const uint32_t segmentCount = u32_le(header + 4u);
    const uint64_t totalRecords = u64_le(header + 8u);
    const uint32_t provenanceLength = u32_le(header + 16u);
    if (version != kSupportedVersion) return Error::UnsupportedFormat;
    if (segmentCount == 0u || segmentCount > kMaxSegments || totalRecords == 0u ||
        provenanceLength < 2u || provenanceLength > kMaxProvenanceBytes) return Error::InvalidHeader;

    uint64_t cursor = 28u;
    if (!add_u64(cursor, provenanceLength, &cursor) || cursor > context->reader.fileSize)
        return Error::InvalidHeader;

    uint64_t recordSum = 0;
    context->segmentCount = segmentCount;
    for (uint32_t i = 0; i < segmentCount; ++i) {
        Segment& segment = context->segments[i];
        if (!read_id(context->reader, &cursor, segment.bodyId, sizeof(segment.bodyId)) ||
            !read_id(context->reader, &cursor, segment.centerBodyId, sizeof(segment.centerBodyId)))
            return Error::InvalidIndex;
        uint8_t words[76];
        if (!read_fields(context->reader, &cursor, words, 76u)) return Error::ReadFailure;
        segment.targetId = i32_le(words);
        segment.centerId = i32_le(words + 4u);
        segment.frameId = i32_le(words + 8u);
        segment.segmentType = i32_le(words + 12u);
        segment.degree = i32_le(words + 16u);
        segment.recordIntervalSeconds = f64_le(words + 20u);
        segment.initEt = f64_le(words + 28u);
        segment.recordCount = u64_le(words + 36u);
        segment.coverageStartJd = f64_le(words + 44u);
        segment.coverageEndJd = f64_le(words + 52u);
        segment.dataOffset = u64_le(words + 60u);
        segment.recordWords = u64_le(words + 68u);

        if (segment.targetId <= 0 || segment.centerId < 0 || segment.frameId != 1 ||
            segment.segmentType != 2 || segment.degree < 0 ||
            static_cast<uint32_t>(segment.degree) > kMaxDegree || segment.recordCount == 0u ||
            !finite(segment.recordIntervalSeconds) || segment.recordIntervalSeconds <= 0.0 ||
            !finite(segment.initEt) || !finite(segment.coverageStartJd) ||
            !finite(segment.coverageEndJd) || segment.coverageEndJd < segment.coverageStartJd)
            return Error::InvalidIndex;

        const uint64_t expectedWords = 3u * (static_cast<uint64_t>(segment.degree) + 1u) + 2u;
        if (segment.recordWords != expectedWords || segment.recordWords > kMaxRecordWords)
            return Error::InvalidIndex;
        uint64_t recordBytes = segment.recordWords * 8u;
        if (segment.dataOffset < cursor ||
            segment.recordCount > (UINT64_MAX - segment.dataOffset) / recordBytes)
            return Error::InvalidIndex;
        const uint64_t endOffset = segment.dataOffset + segment.recordCount * recordBytes;
        if (endOffset > context->reader.fileSize) return Error::InvalidIndex;

        const double endEt = segment.initEt +
            static_cast<double>(segment.recordCount) * segment.recordIntervalSeconds;
        if (!finite(endEt) || endEt <= segment.initEt) return Error::InvalidIndex;
        if (segment.recordCount > UINT64_MAX - recordSum) return Error::InvalidIndex;
        recordSum += segment.recordCount;

        for (uint32_t prior = 0; prior < i; ++prior) {
            const Segment& previous = context->segments[prior];
            if (previous.targetId == segment.targetId) return Error::InvalidIndex;
            const uint64_t previousEnd = previous.dataOffset +
                previous.recordCount * previous.recordWords * 8u;
            if (segment.dataOffset < previousEnd && previous.dataOffset < endOffset)
                return Error::InvalidIndex;
        }
    }
    if (recordSum != totalRecords) return Error::InvalidIndex;
    for (uint32_t i = 0; i < context->segmentCount; ++i)
        if (context->segments[i].dataOffset < cursor) return Error::InvalidIndex;

    for (uint32_t i = 0; i < context->segmentCount; ++i) {
        const Segment& segment = context->segments[i];
        int32_t center = segment.centerId;
        uint32_t depth = 0;
        while (center != 0) {
            if (++depth > context->segmentCount) return Error::InvalidIndex;
            const int index = find_segment(context, center);
            if (index < 0) return Error::InvalidIndex;
            center = context->segments[index].centerId;
        }
    }
    return Error::Ok;
}

static Error authenticate_dataset(Context* context) {
    if (context->reader.fileSize != kDatasetBytes) return Error::DatasetSizeMismatch;
    report_progress(context, ProgressStage::AuthenticationStarted, 0u);
    uint8_t extra = 0;
    uint32_t extraBytes = 0;
    if (!context->reader.readAt(context->reader.user, kDatasetBytes, &extra, 1u, &extraBytes) || extraBytes > 1u)
        return Error::ReadFailure;
    if (extraBytes != 0u) return Error::DatasetSizeMismatch;

    Sha256 sha;
    sha_init(&sha);
    uint64_t offset = 0;
    uint64_t lastProgress = 0;
    while (offset < kDatasetBytes) {
        const uint64_t remaining = kDatasetBytes - offset;
        const uint32_t requested = remaining > kHashChunkBytes
            ? kHashChunkBytes : static_cast<uint32_t>(remaining);
        uint32_t bytesRead = 0;
        if (!context->reader.readAt(context->reader.user, offset, context->scratch,
                requested, &bytesRead) || bytesRead != requested) return Error::ReadFailure;
        sha_update(&sha, context->scratch, bytesRead);
        offset += bytesRead;
        if (offset - lastProgress >= kProgressIntervalBytes || offset == kDatasetBytes) {
            report_progress(context, ProgressStage::AuthenticationProgress, offset);
            lastProgress = offset;
        }
    }
    uint8_t digest[32];
    sha_finish(&sha, digest);
    for (uint32_t i = 0; i < sizeof(digest); ++i)
        if (digest[i] != kExpectedSha256[i]) return Error::DatasetHashMismatch;
    report_progress(context, ProgressStage::AuthenticationComplete, offset);
    return Error::Ok;
}

static Error evaluate_segment(const Context* context, const Segment& segment,
                              double et, Vector3* positionKm) {
    const double endEt = segment.initEt +
        static_cast<double>(segment.recordCount) * segment.recordIntervalSeconds;
    if (et < segment.initEt || et > endEt) return Error::EpochOutOfCoverage;

    double recordIndexValue = (et - segment.initEt) / segment.recordIntervalSeconds;
    if (!finite(recordIndexValue) || recordIndexValue < 0.0) return Error::InvalidEpoch;
    uint64_t recordIndex = recordIndexValue >= static_cast<double>(segment.recordCount)
        ? segment.recordCount - 1u : static_cast<uint64_t>(recordIndexValue);
    const uint64_t recordBytes = segment.recordWords * 8u;
    if (recordIndex > (UINT64_MAX - segment.dataOffset) / recordBytes) return Error::InvalidIndex;
    const uint64_t recordOffset = segment.dataOffset + recordIndex * recordBytes;
    if (recordOffset > context->reader.fileSize || recordBytes > context->reader.fileSize - recordOffset)
        return Error::InvalidIndex;
    report_progress(context, ProgressStage::TargetRecordSelected, recordOffset);

    uint8_t record[kMaxRecordWords * 8u];
    if (!read_exact(context->reader, recordOffset, record, static_cast<uint32_t>(recordBytes)))
        return Error::ReadFailure;
    const double midpoint = f64_le(record);
    const double radius = f64_le(record + 8u);
    if (!finite(midpoint) || !finite(radius) || radius <= 0.0) return Error::InvalidRecord;
    for (uint64_t word = 2u; word < segment.recordWords; ++word)
        if (!finite(f64_le(record + word * 8u))) return Error::InvalidRecord;

    const double normalized = (et - midpoint) / radius;
    if (!finite(normalized) || normalized < -1.0 - kRecordDomainSlack ||
        normalized > 1.0 + kRecordDomainSlack) return Error::InvalidRecord;
    const double u = normalized < -1.0 ? -1.0 : (normalized > 1.0 ? 1.0 : normalized);
    report_progress(context, ProgressStage::CoefficientEvaluationStarted, recordOffset);
    const uint32_t coefficientCount = static_cast<uint32_t>(segment.degree) + 1u;
    double values[3];
    for (uint32_t axis = 0; axis < 3u; ++axis) {
        const uint32_t first = 2u + axis * coefficientCount;
        double b1 = 0.0;
        double b2 = 0.0;
        for (int32_t coefficient = segment.degree; coefficient >= 1; --coefficient) {
            const double value = f64_le(record + (first + static_cast<uint32_t>(coefficient)) * 8u);
            const double b0 = value + 2.0 * u * b1 - b2;
            b2 = b1;
            b1 = b0;
        }
        values[axis] = f64_le(record + first * 8u) + u * b1 - b2;
        if (!finite(values[axis])) return Error::InvalidRecord;
    }
    positionKm->x = values[0];
    positionKm->y = values[1];
    positionKm->z = values[2];
    report_progress(context, ProgressStage::CoefficientEvaluationComplete, recordOffset);
    return Error::Ok;
}

} // namespace

Error initialize(Context* context, const Reader* reader) {
    if (!context) return Error::InvalidArgument;
    zero_bytes(context, sizeof(*context));
    if (!reader || !reader->readAt) return Error::InvalidArgument;
    context->reader = *reader;
    Error result = authenticate_dataset(context);
    if (result == Error::Ok) {
        uint8_t magic[8];
        if (!read_exact(context->reader, 0u, magic, sizeof(magic))) result = Error::ReadFailure;
        else if (magic[0] != 'S' || magic[1] != 'N' || magic[2] != 'E' || magic[3] != 'P' ||
                 magic[4] != 'H' || magic[5] != '0' || magic[6] != '4' || magic[7] != 'B')
            result = Error::InvalidHeader;
    }
    if (result == Error::Ok) {
        report_progress(context, ProgressStage::IndexValidationStarted, 0u);
        result = parse_index(context);
        if (result == Error::Ok)
            report_progress(context, ProgressStage::IndexValidationComplete, context->segmentCount);
    }
    if (result != Error::Ok) {
        shutdown(context);
        return result;
    }
    context->initialized = true;
    return Error::Ok;
}

void shutdown(Context* context) {
    if (context) zero_bytes(context, sizeof(*context));
}

Error evaluatePosition(const Context* context, int32_t targetId,
                       double etTdbSeconds, Vector3* positionKm) {
    if (!context || !positionKm) return Error::InvalidArgument;
    if (!context->initialized) return Error::NotInitialized;
    if (targetId <= 0) return Error::BodyUnavailable;
    if (!finite(etTdbSeconds)) return Error::InvalidEpoch;
    const int first = find_segment(context, targetId);
    if (first < 0) return Error::BodyUnavailable;

    Vector3 sum = { 0.0, 0.0, 0.0 };
    int32_t current = targetId;
    int32_t visited[kMaxSegments];
    uint32_t visitedCount = 0;
    while (current != 0) {
        if (visitedCount >= context->segmentCount) return Error::InvalidIndex;
        for (uint32_t i = 0; i < visitedCount; ++i)
            if (visited[i] == current) return Error::InvalidIndex;
        visited[visitedCount++] = current;
        const int index = find_segment(context, current);
        if (index < 0) return Error::InvalidIndex;
        Vector3 step = { 0.0, 0.0, 0.0 };
        const Error result = evaluate_segment(context, context->segments[index], etTdbSeconds, &step);
        if (result != Error::Ok) return result;
        sum.x += step.x; sum.y += step.y; sum.z += step.z;
        if (!finite(sum.x) || !finite(sum.y) || !finite(sum.z)) return Error::InvalidRecord;
        current = context->segments[index].centerId;
    }
    *positionKm = sum;
    return Error::Ok;
}

const char* errorText(Error error) {
    switch (error) {
    case Error::Ok: return "available";
    case Error::InvalidArgument: return "invalid application or reader argument";
    case Error::NotInitialized: return "ephemeris data is not initialized";
    case Error::ReadFailure: return "ephemeris file read failed or ended early";
    case Error::DatasetSizeMismatch: return "ephemeris data size does not match the trusted DE440s asset";
    case Error::DatasetHashMismatch: return "ephemeris SHA-256 does not match the trusted DE440s manifest";
    case Error::InvalidHeader: return "ephemeris header is invalid";
    case Error::UnsupportedFormat: return "ephemeris binary version is unsupported";
    case Error::InvalidIndex: return "ephemeris segment index or center chain is invalid";
    case Error::BodyUnavailable: return "requested body is unavailable in the dataset";
    case Error::EpochOutOfCoverage: return "ET is outside the selected body's data coverage; no extrapolation was done";
    case Error::InvalidEpoch: return "ET must be a finite TDB value";
    case Error::InvalidRecord: return "selected ephemeris record is invalid or non-finite";
    }
    return "unknown ephemeris error";
}

const char* datasetSha256() {
    return "d52b91573c29e6a4e5b6dc19c4af4d9747f6da2122f5b3aff4503684a96d2c0a";
}

} // namespace stellar_native
