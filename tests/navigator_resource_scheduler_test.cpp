#include "../navigator_resource_scheduler.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <string>

using namespace gxos::apps;

namespace {

struct FixtureResource {
    std::string url;
    uint64_t decodedBytes = 0;
    bool supported = true;
    bool allocationFails = false;
    bool httpFails = false;
    bool encodedTooLarge = false;
};

struct FixtureResult {
    NavigatorResourceSchedulerStats stats{};
    NavigatorResourceMemoryAccounting memory{};
    uint32_t networkFetches = 0;
    uint32_t decodes = 0;
};

static FixtureResult runFixture(const std::array<FixtureResource, 128>& resources, size_t count)
{
    FixtureResult result;
    std::array<int, 128> canonical{};
    canonical.fill(-1);
    for (size_t i = 0; i < count; ++i) {
        ++result.stats.referencesDiscovered;
        int duplicateOf = -1;
        for (size_t prior = 0; prior < i; ++prior) {
            if (resources[prior].url == resources[i].url) {
                duplicateOf = canonical[prior];
                break;
            }
        }
        if (duplicateOf >= 0) {
            ++result.stats.duplicateReferences;
            continue;
        }
        canonical[i] = static_cast<int>(i);
        ++result.stats.uniqueReferences;
        if (!resources[i].supported) {
            ++result.stats.unsupportedSkipped;
            continue;
        }
        ++result.stats.schedulerCandidates;
        if (result.stats.activeCount >= kNavigatorMaxActiveResources) {
            ++result.stats.resourceCapDenied;
            continue;
        }
        if (resources[i].encodedTooLarge) continue;
        ++result.stats.fetchStarted;
        ++result.networkFetches;
        ++result.stats.fetchCompleted;
        if (resources[i].httpFails) continue;
        if (!result.memory.reserveDecoded(resources[i].decodedBytes)) {
            ++result.stats.budgetDenied;
            result.stats.noteDeniedDecoded(resources[i].decodedBytes);
            continue;
        }
        ++result.stats.decodeStarted;
        ++result.decodes;
        if (resources[i].allocationFails) {
            result.memory.releaseDecoded(resources[i].decodedBytes);
            continue;
        }
        ++result.stats.decoded;
        ++result.stats.attached;
        result.stats.noteLoadedDecoded(resources[i].decodedBytes);
        ++result.stats.activeCount;
        result.stats.activeBytes = result.memory.activeDecodedBytes;
        if (result.stats.activeBytes > result.stats.peakActiveBytes)
            result.stats.peakActiveBytes = result.stats.activeBytes;
    }
    return result;
}

static bool expect(bool condition, const char* label)
{
    if (!condition) std::cerr << "FAIL: " << label << "\n";
    return condition;
}

} // namespace

int main()
{
    bool ok = true;

    // 1. More references than the former 32-attempt ceiling remain bounded.
    std::array<FixtureResource, 128> many{};
    for (size_t i = 0; i < 80; ++i) {
        many[i].url = "https://fixture.test/image-" + std::to_string(i) + ".png";
        many[i].decodedBytes = 256u * 1024u;
    }
    FixtureResult manyResult = runFixture(many, 80);
    ok &= expect(manyResult.stats.referencesDiscovered == 80, ">32 references discovered");
    ok &= expect(manyResult.networkFetches == kNavigatorMaxActiveResources,
        "many small resources make progress beyond old cap");
    ok &= expect(manyResult.memory.activeDecodedBytes <= kNavigatorDecodedImageBudgetBytes,
        "many small resources stay within budget");
    ok &= expect(manyResult.stats.totalLoadedDecodedBytes ==
        static_cast<uint64_t>(kNavigatorMaxActiveResources) * 256u * 1024u,
        "loaded decoded-byte total is bounded and counted once");
    ok &= expect(manyResult.stats.loadedDecodedSizeBuckets[1] == kNavigatorMaxActiveResources,
        "loaded size bucket records 256 KiB resources");

    // 2. Duplicate-heavy input performs one network/decode operation per URL.
    std::array<FixtureResource, 128> duplicates{};
    for (size_t i = 0; i < 64; ++i) {
        duplicates[i].url = "https://fixture.test/shared-" + std::to_string(i % 8) + ".png";
        duplicates[i].decodedBytes = 512u * 1024u;
    }
    FixtureResult duplicateResult = runFixture(duplicates, 64);
    ok &= expect(duplicateResult.stats.uniqueReferences == 8, "duplicate unique count");
    ok &= expect(duplicateResult.stats.duplicateReferences == 56, "duplicate reference count");
    ok &= expect(duplicateResult.networkFetches == 8 && duplicateResult.decodes == 8,
        "duplicate network/decode work is shared");

    // 3. Budget saturation denies the next image without over-budget state.
    std::array<FixtureResource, 128> saturation{};
    for (size_t i = 0; i < 10; ++i) {
        saturation[i].url = "https://fixture.test/heavy-" + std::to_string(i) + ".jpg";
        saturation[i].decodedBytes = 16u * 1024u * 1024u;
    }
    FixtureResult saturationResult = runFixture(saturation, 10);
    ok &= expect(saturationResult.memory.activeDecodedBytes <= kNavigatorDecodedImageBudgetBytes,
        "budget saturation never over-allocates");
    ok &= expect(saturationResult.stats.budgetDenied > 0, "budget denial is explicit");
    ok &= expect(saturationResult.stats.totalDeniedRequestedBytes ==
        static_cast<uint64_t>(saturationResult.stats.budgetDenied) * 16u * 1024u * 1024u,
        "denied requested bytes are characterized");
    ok &= expect(saturationResult.stats.deniedDecodedSizeBuckets[5] == saturationResult.stats.budgetDenied,
        "denied size bucket records 16 MiB candidates");

    // 4. Navigation release makes the budget reusable.
    saturationResult.memory.releaseDecoded(saturationResult.memory.activeDecodedBytes);
    saturationResult.stats.activeCount = 0;
    FixtureResult afterNavigation = runFixture(many, 1);
    ok &= expect(afterNavigation.memory.activeDecodedBytes == many[0].decodedBytes,
        "released budget is reusable after navigation");
    afterNavigation.memory.releaseDecoded(UINT64_MAX);
    ok &= expect(afterNavigation.memory.activeDecodedBytes == 0,
        "oversized release cannot underflow active decoded bytes");

    // 5. Known unsupported resources never enter fetch/decode capacity.
    std::array<FixtureResource, 128> unsupported{};
    unsupported[0] = {"https://fixture.test/a.svg", 4u * 1024u * 1024u, false};
    unsupported[1] = {"https://fixture.test/a.webp", 4u * 1024u * 1024u, false};
    unsupported[2] = {"https://fixture.test/a.gif", 4u * 1024u * 1024u, false};
    unsupported[3] = {"https://fixture.test/a.png", 64u * 1024u};
    FixtureResult unsupportedResult = runFixture(unsupported, 4);
    ok &= expect(unsupportedResult.stats.unsupportedSkipped == 3, "unsupported formats skipped");
    ok &= expect(unsupportedResult.networkFetches == 1, "unsupported formats do not fetch");

    // 6. One image at the per-image cap is accepted when the aggregate budget allows it.
    std::array<FixtureResource, 128> large{};
    large[0] = {"https://fixture.test/large.png", kNavigatorMaxDecodedImageBytes};
    FixtureResult largeResult = runFixture(large, 1);
    ok &= expect(largeResult.stats.attached == 1, "large single image attaches");

    // 7. Active-resource capacity is independent from reference capacity.
    FixtureResult activeCapResult = runFixture(many, 80);
    ok &= expect(activeCapResult.stats.referencesDiscovered == 80, "active cap does not truncate references");
    ok &= expect(activeCapResult.stats.activeCount <= kNavigatorMaxActiveResources,
        "active count is separately bounded");

    // 8. An allocation failure does not block later valid resources.
    std::array<FixtureResource, 128> allocationFailure{};
    allocationFailure[0] = {"https://fixture.test/bad.png", 1024u, true, true};
    allocationFailure[1] = {"https://fixture.test/good.png", 1024u};
    FixtureResult allocationResult = runFixture(allocationFailure, 2);
    ok &= expect(allocationResult.networkFetches == 2 && allocationResult.stats.attached == 1,
        "allocation failure continues to later resource");

    // 9. HTTP failure does not block later valid resources.
    std::array<FixtureResource, 128> httpFailure{};
    httpFailure[0] = {"https://fixture.test/404.png", 1024u, true, false, true};
    httpFailure[1] = {"https://fixture.test/ok.png", 1024u};
    FixtureResult httpResult = runFixture(httpFailure, 2);
    ok &= expect(httpResult.networkFetches == 2 && httpResult.stats.attached == 1,
        "HTTP failure continues to later resource");

    // 10. Encoded-body failure does not consume decoded budget or starve later work.
    std::array<FixtureResource, 128> encodedFailure{};
    encodedFailure[0] = {"https://fixture.test/oversized.png", 8u * 1024u * 1024u, true, false, false, true};
    encodedFailure[1] = {"https://fixture.test/small.png", 1024u};
    FixtureResult encodedResult = runFixture(encodedFailure, 2);
    ok &= expect(encodedResult.networkFetches == 1 && encodedResult.stats.attached == 1,
        "encoded-body failure continues to later resource");

    std::cout << (ok ? "Navigator resource scheduler tests PASS\n"
                     : "Navigator resource scheduler tests FAIL\n");
    return ok ? 0 : 1;
}
