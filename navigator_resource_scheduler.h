#pragma once

// Shared, freestanding-friendly policy primitives for Navigator resource
// scheduling.  The scheduler itself remains platform-specific because hosted
// Navigator owns ImageInfo objects while bare metal owns decoder pixel
// buffers.  These constants and accounting rules are deliberately shared so
// the two paths cannot silently drift.

#include <stdint.h>

namespace gxos {
namespace apps {

// A document may retain more bounded reference metadata than the former
// shared HTTP fetch cap.  The record below is compact and stores no URL body.
static const uint32_t kNavigatorMaxResourceReferences = 96u;
// Sequential scheduling keeps one transaction/decoder active at a time, but
// the number of decoded resources retained by one document is still bounded.
static const uint32_t kNavigatorMaxActiveResources = 64u;
// NASA's Phase 8R page retained about 93 MiB of estimated RGBA pixels.  Keep a
// 64 MiB per-document hard ceiling so one page cannot consume essentially all
// of a small kernel/application heap while still allowing substantially more
// useful small resources than the old first-32 attempt gate.
static const uint64_t kNavigatorDecodedImageBudgetBytes = 64ull * 1024ull * 1024ull;
static const uint64_t kNavigatorMaxDecodedImageBytes = 2048ull * 2048ull * 4ull;

// Phase 8T keeps initial admission finite and deterministic.  The first
// viewport is supplied by the embedding Navigator because chrome consumes part
// of the framebuffer; callers must not silently substitute the full window.
enum class NavigatorResourceViewportClass : uint8_t {
    Visible = 0,
    Near = 1,
    Far = 2,
    Unknown = 3,
    Unsupported = 4,
};

struct NavigatorResourceViewportGeometry {
    int32_t viewportTop = 0;
    int32_t viewportBottom = -1;
    int32_t viewportWidth = 0;
    int32_t viewportHeight = 0;
    int32_t scrollOffset = 0;
    int32_t preloadMargin = 0;
};

inline const char* navigatorResourceViewportClassName(NavigatorResourceViewportClass value)
{
    switch (value) {
    case NavigatorResourceViewportClass::Visible: return "visible";
    case NavigatorResourceViewportClass::Near: return "near";
    case NavigatorResourceViewportClass::Far: return "far";
    case NavigatorResourceViewportClass::Unknown: return "unknown";
    case NavigatorResourceViewportClass::Unsupported: return "unsupported";
    default: return "unknown";
    }
}

inline NavigatorResourceViewportClass navigatorBestViewportClass(
    NavigatorResourceViewportClass left, NavigatorResourceViewportClass right)
{
    return static_cast<uint8_t>(left) <= static_cast<uint8_t>(right) ? left : right;
}

// Eviction is deliberately a relevance policy, not a time-based cache.  The
// lower the class rank, the more useful the resource is to the current
// viewport; eviction therefore walks this order in reverse.  A caller still
// supplies the stable source/hash tie-breakers so the policy is identical on
// hosted and bare-metal Navigator.
inline uint8_t navigatorResourceEvictionClassRank(NavigatorResourceViewportClass value)
{
    switch (value) {
    case NavigatorResourceViewportClass::Far: return 0u;
    case NavigatorResourceViewportClass::Unknown: return 1u;
    case NavigatorResourceViewportClass::Near: return 2u;
    case NavigatorResourceViewportClass::Visible: return 3u;
    default: return 4u;
    }
}

inline NavigatorResourceViewportClass navigatorClassifyViewportRect(
    int32_t blockTop, int32_t blockBottom,
    const NavigatorResourceViewportGeometry& viewport, int32_t* outDistance = nullptr)
{
    if (outDistance) *outDistance = -1;
    if (blockTop < 0 || blockBottom < blockTop || viewport.viewportBottom < viewport.viewportTop ||
        viewport.viewportWidth <= 0 || viewport.viewportHeight <= 0) {
        return NavigatorResourceViewportClass::Unknown;
    }
    const int64_t top = blockTop;
    const int64_t bottom = blockBottom;
    const int64_t viewportTop = viewport.viewportTop;
    const int64_t viewportBottom = viewport.viewportBottom;
    if (bottom >= viewportTop && top <= viewportBottom) {
        if (outDistance) *outDistance = 0;
        return NavigatorResourceViewportClass::Visible;
    }
    const int64_t distance = bottom < viewportTop ? viewportTop - bottom : top - viewportBottom;
    if (outDistance && distance <= 0x7fffffffll) *outDistance = static_cast<int32_t>(distance);
    const int64_t margin = viewport.preloadMargin > 0 ? viewport.preloadMargin : 0;
    return distance <= margin ? NavigatorResourceViewportClass::Near
        : NavigatorResourceViewportClass::Far;
}

// formatHint follows the existing compact scheduler encoding: PNG=1,
// JPEG=2, unknown existing image=0, known unsupported=3.
inline uint8_t navigatorResourceFormatRank(uint8_t formatHint)
{
    return (formatHint == 1u || formatHint == 2u) ? 0u : (formatHint == 3u ? 3u : 1u);
}

inline uint8_t navigatorResourcePriorityBeforeViewport(uint8_t formatHint)
{
    return formatHint == 3u ? 3u : navigatorResourceFormatRank(formatHint);
}

inline uint8_t navigatorResourcePriorityWithViewport(
    NavigatorResourceViewportClass viewportClass, uint8_t formatHint)
{
    if (viewportClass == NavigatorResourceViewportClass::Unsupported || formatHint == 3u)
        return 0xFFu;
    const uint8_t classRank = static_cast<uint8_t>(viewportClass);
    return static_cast<uint8_t>(classRank * 4u + navigatorResourceFormatRank(formatHint));
}

enum class NavigatorResourceSchedulerState : uint8_t {
    Empty = 0,
    Discovered,
    Deduplicated,
    Pending,
    Fetching,
    Fetched,
    DecodeStarted,
    Decoded,
    Attached,
    Failed,
    BudgetDenied,
    ResourceCapDenied,
    UnsupportedSkipped,
    Released,
    Evicted,
};

inline const char* navigatorResourceSchedulerStateName(NavigatorResourceSchedulerState state)
{
    switch (state) {
    case NavigatorResourceSchedulerState::Empty: return "empty";
    case NavigatorResourceSchedulerState::Discovered: return "discovered";
    case NavigatorResourceSchedulerState::Deduplicated: return "deduplicated";
    case NavigatorResourceSchedulerState::Pending: return "pending";
    case NavigatorResourceSchedulerState::Fetching: return "fetching";
    case NavigatorResourceSchedulerState::Fetched: return "fetched";
    case NavigatorResourceSchedulerState::DecodeStarted: return "decode_started";
    case NavigatorResourceSchedulerState::Decoded: return "decoded";
    case NavigatorResourceSchedulerState::Attached: return "attached";
    case NavigatorResourceSchedulerState::Failed: return "failed";
    case NavigatorResourceSchedulerState::BudgetDenied: return "budget_denied";
    case NavigatorResourceSchedulerState::ResourceCapDenied: return "resource_cap_denied";
    case NavigatorResourceSchedulerState::UnsupportedSkipped: return "unsupported_skipped";
    case NavigatorResourceSchedulerState::Released: return "released";
    case NavigatorResourceSchedulerState::Evicted: return "evicted";
    default: return "unknown";
    }
}

// This is the only metadata retained for an unscheduled reference.  The full
// URL remains owned by the bounded document/parser block and is addressed by
// blockIndex; this record does not duplicate it.
struct NavigatorResourceReferenceMetadata {
    uint32_t sourceOrdinal = 0;
    uint16_t blockIndex = 0;
    uint16_t duplicateOf = 0xFFFFu;
    uint32_t normalizedUrlHash = 0;
    uint32_t budgetRequestedBytes = 0;
    uint32_t budgetAcceptedBytes = 0;
    uint32_t activeBytesBefore = 0;
    uint32_t budgetHeadroomBefore = 0;
    int32_t blockTop = -1;
    int32_t blockBottom = -1;
    int32_t distanceFromViewport = -1;
    uint8_t priority = 0;
    uint8_t priorityBeforeViewport = 0;
    uint8_t formatHint = 0;
    uint8_t state = static_cast<uint8_t>(NavigatorResourceSchedulerState::Empty);
    uint8_t classification = 0;
    uint8_t viewportClass = static_cast<uint8_t>(NavigatorResourceViewportClass::Unknown);
    uint8_t admittedDueToViewportPriority = 0;
    uint8_t previousViewportClass = static_cast<uint8_t>(NavigatorResourceViewportClass::Unknown);
    uint8_t admissionReason = 0;
    uint8_t evictionReason = 0;
    uint8_t paintObserved = 0;
    uint16_t evictionCount = 0;
    uint16_t readmissionCount = 0;
};

static_assert(sizeof(NavigatorResourceReferenceMetadata) <= 64u,
    "Navigator resource metadata must remain compact");

inline uint32_t navigatorDecodedSizeBucket(uint64_t bytes);

struct NavigatorResourceSchedulerStats {
    uint32_t referencesDiscovered = 0;
    uint32_t uniqueReferences = 0;
    uint32_t duplicateReferences = 0;
    uint32_t schedulerCandidates = 0;
    uint32_t pending = 0;
    uint32_t fetchStarted = 0;
    uint32_t fetchCompleted = 0;
    uint32_t decodeStarted = 0;
    uint32_t decoded = 0;
    uint32_t attached = 0;
    uint32_t budgetDenied = 0;
    uint32_t resourceCapDenied = 0;
    uint32_t unsupportedSkipped = 0;
    uint32_t referencesCapacityDenied = 0;
    uint32_t released = 0;
    uint32_t activeCount = 0;
    uint64_t activeBytes = 0;
    uint64_t peakActiveBytes = 0;
    uint64_t currentEncodedResourceBytes = 0;
    uint64_t peakEncodedResourceBytes = 0;
    uint64_t peakTemporaryDecodeBytes = 0;
    uint64_t releasedDecodedBytes = 0;
    uint64_t deniedAllocationBytes = 0;
    uint64_t totalLoadedDecodedBytes = 0;
    uint64_t totalDeniedRequestedBytes = 0;
    int32_t viewportTop = 0;
    int32_t viewportBottom = -1;
    int32_t viewportWidth = 0;
    int32_t viewportHeight = 0;
    int32_t initialScrollOffset = 0;
    int32_t preloadMargin = 0;
    uint32_t visibleReferences = 0;
    uint32_t nearReferences = 0;
    uint32_t farReferences = 0;
    uint32_t unknownViewportReferences = 0;
    uint32_t visibleLoaded = 0;
    uint32_t visibleBudgetDenied = 0;
    uint32_t nearLoaded = 0;
    uint32_t nearBudgetDenied = 0;
    uint32_t farLoaded = 0;
    uint32_t farBudgetDenied = 0;
    uint32_t visiblePriorityAdmissions = 0;
    uint32_t offscreenBudgetDenied = 0;
    uint64_t decodedBytesVisible = 0;
    uint64_t decodedBytesNear = 0;
    uint64_t decodedBytesFar = 0;
    uint32_t viewportGeneration = 0;
    uint32_t viewportAdmissionPasses = 0;
    uint32_t scrollTriggeredAdmissions = 0;
    uint32_t resourcesReconsidered = 0;
    uint32_t evictions = 0;
    uint32_t reAdmissions = 0;
    uint32_t visibleAdmissionFailures = 0;
    uint32_t budgetDenialsAfterEvictionAttempts = 0;
    uint64_t evictedDecodedBytes = 0;
    int32_t currentScrollOffset = 0;
    uint32_t loadedDecodedSizeBuckets[6] = {};
    uint32_t deniedDecodedSizeBuckets[6] = {};

    void noteLoadedDecoded(uint64_t bytes)
    {
        if (totalLoadedDecodedBytes <= UINT64_MAX - bytes)
            totalLoadedDecodedBytes += bytes;
        const uint32_t bucket = navigatorDecodedSizeBucket(bytes);
        if (bucket < 6u && loadedDecodedSizeBuckets[bucket] < UINT32_MAX)
            ++loadedDecodedSizeBuckets[bucket];
    }

    void noteDeniedDecoded(uint64_t bytes)
    {
        if (totalDeniedRequestedBytes <= UINT64_MAX - bytes)
            totalDeniedRequestedBytes += bytes;
        const uint32_t bucket = navigatorDecodedSizeBucket(bytes);
        if (bucket < 6u && deniedDecodedSizeBuckets[bucket] < UINT32_MAX)
            ++deniedDecodedSizeBuckets[bucket];
    }
};

struct NavigatorResourceMemoryAccounting {
    uint64_t activeDecodedBytes = 0;
    uint64_t peakDecodedBytes = 0;
    uint64_t currentEncodedBytes = 0;
    uint64_t peakEncodedBytes = 0;
    uint64_t peakTemporaryDecodeBytes = 0;
    uint64_t releasedDecodedBytes = 0;
    uint64_t deniedAllocationBytes = 0;

    void reset()
    {
        activeDecodedBytes = 0;
        peakDecodedBytes = 0;
        currentEncodedBytes = 0;
        peakEncodedBytes = 0;
        peakTemporaryDecodeBytes = 0;
        releasedDecodedBytes = 0;
        deniedAllocationBytes = 0;
    }

    bool beginEncoded(uint64_t bytes)
    {
        if (bytes > UINT64_MAX - currentEncodedBytes) return false;
        currentEncodedBytes += bytes;
        if (currentEncodedBytes > peakEncodedBytes) peakEncodedBytes = currentEncodedBytes;
        return true;
    }

    void endEncoded(uint64_t bytes)
    {
        currentEncodedBytes = bytes > currentEncodedBytes ? 0 : currentEncodedBytes - bytes;
    }

    bool reserveDecoded(uint64_t bytes, uint64_t budget = kNavigatorDecodedImageBudgetBytes)
    {
        if (bytes > kNavigatorMaxDecodedImageBytes ||
            bytes > UINT64_MAX - activeDecodedBytes ||
            activeDecodedBytes + bytes > budget) {
            if (deniedAllocationBytes <= UINT64_MAX - bytes) deniedAllocationBytes += bytes;
            return false;
        }
        activeDecodedBytes += bytes;
        if (activeDecodedBytes > peakDecodedBytes) peakDecodedBytes = activeDecodedBytes;
        if (bytes > peakTemporaryDecodeBytes) peakTemporaryDecodeBytes = bytes;
        return true;
    }

    void releaseDecoded(uint64_t bytes)
    {
        const uint64_t released = bytes > activeDecodedBytes ? activeDecodedBytes : bytes;
        activeDecodedBytes -= released;
        if (releasedDecodedBytes <= UINT64_MAX - released) releasedDecodedBytes += released;
    }
};

inline bool navigatorCheckedRgbaBytes(uint32_t width, uint32_t height, uint64_t& bytes)
{
    const uint64_t pixels = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
    if (width == 0 || height == 0 || pixels > UINT64_MAX / 4ull) {
        bytes = 0;
        return false;
    }
    bytes = pixels * 4ull;
    return bytes <= kNavigatorMaxDecodedImageBytes;
}

// Buckets are deliberately shared by hosted and bare-metal telemetry so the
// offline NASA report compares like with like: <256 KiB, 256 KiB-1 MiB,
// 1-2 MiB, 2-4 MiB, 4-8 MiB, and 8-16 MiB inclusive at the policy cap.
inline uint32_t navigatorDecodedSizeBucket(uint64_t bytes)
{
    if (bytes < 256ull * 1024ull) return 0u;
    if (bytes < 1ull * 1024ull * 1024ull) return 1u;
    if (bytes < 2ull * 1024ull * 1024ull) return 2u;
    if (bytes < 4ull * 1024ull * 1024ull) return 3u;
    if (bytes < 8ull * 1024ull * 1024ull) return 4u;
    return 5u;
}

} // namespace apps
} // namespace gxos
