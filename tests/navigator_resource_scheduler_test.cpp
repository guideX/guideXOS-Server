#include "../navigator_resource_scheduler.h"

#include <array>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

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

struct ViewportFixtureResource {
    std::string url;
    uint64_t decodedBytes = 0;
    int32_t top = 0;
    int32_t bottom = 0;
    uint8_t formatHint = 1;
    bool supported = true;
    bool corrupt = false;
    bool encodedTooLarge = false;
};

struct ViewportFixtureResult {
    NavigatorResourceSchedulerStats stats{};
    NavigatorResourceMemoryAccounting memory{};
    std::vector<uint32_t> selectionOrder;
    std::array<uint8_t, 64> canonicalViewportClass{};
    uint32_t networkFetches = 0;
    uint32_t decodes = 0;
};

static void fixtureNoteViewportReferenceClass(NavigatorResourceSchedulerStats& stats,
    NavigatorResourceViewportClass viewportClass)
{
    switch (viewportClass) {
    case NavigatorResourceViewportClass::Visible: ++stats.visibleReferences; break;
    case NavigatorResourceViewportClass::Near: ++stats.nearReferences; break;
    case NavigatorResourceViewportClass::Far: ++stats.farReferences; break;
    case NavigatorResourceViewportClass::Unknown: ++stats.unknownViewportReferences; break;
    default: break;
    }
}

static void fixtureNoteViewportLoad(NavigatorResourceSchedulerStats& stats,
    NavigatorResourceViewportClass viewportClass, uint64_t decodedBytes)
{
    switch (viewportClass) {
    case NavigatorResourceViewportClass::Visible:
        ++stats.visibleLoaded;
        stats.decodedBytesVisible += decodedBytes;
        break;
    case NavigatorResourceViewportClass::Near:
        ++stats.nearLoaded;
        stats.decodedBytesNear += decodedBytes;
        break;
    case NavigatorResourceViewportClass::Far:
        ++stats.farLoaded;
        stats.decodedBytesFar += decodedBytes;
        break;
    default: break;
    }
}

static void fixtureNoteViewportBudgetDenial(NavigatorResourceSchedulerStats& stats,
    NavigatorResourceViewportClass viewportClass)
{
    switch (viewportClass) {
    case NavigatorResourceViewportClass::Visible: ++stats.visibleBudgetDenied; break;
    case NavigatorResourceViewportClass::Near: ++stats.nearBudgetDenied; break;
    case NavigatorResourceViewportClass::Far:
        ++stats.farBudgetDenied;
        ++stats.offscreenBudgetDenied;
        break;
    default: break;
    }
}

static ViewportFixtureResult runViewportFixture(
    const std::array<ViewportFixtureResource, 64>& resources, size_t count,
    uint64_t budget, bool viewportAware)
{
    ViewportFixtureResult result;
    result.canonicalViewportClass.fill(static_cast<uint8_t>(NavigatorResourceViewportClass::Unknown));
    std::array<int, 64> canonical{};
    std::array<bool, 64> pending{};
    canonical.fill(-1);
    NavigatorResourceViewportGeometry viewport;
    viewport.viewportTop = 82;
    viewport.viewportBottom = 610;
    viewport.viewportWidth = 872;
    viewport.viewportHeight = 528;
    viewport.preloadMargin = 528;
    result.stats.viewportTop = viewport.viewportTop;
    result.stats.viewportBottom = viewport.viewportBottom;
    result.stats.viewportWidth = viewport.viewportWidth;
    result.stats.viewportHeight = viewport.viewportHeight;
    result.stats.preloadMargin = viewport.preloadMargin;

    std::array<NavigatorResourceViewportClass, 64> classes{};
    std::array<uint8_t, 64> priorities{};
    std::array<uint8_t, 64> oldPriorities{};
    for (size_t i = 0; i < count; ++i) {
        ++result.stats.referencesDiscovered;
        int32_t distance = -1;
        NavigatorResourceViewportClass viewportClass = navigatorClassifyViewportRect(
            resources[i].top, resources[i].bottom, viewport, &distance);
        if (!resources[i].supported) viewportClass = NavigatorResourceViewportClass::Unsupported;
        classes[i] = viewportClass;
        fixtureNoteViewportReferenceClass(result.stats, viewportClass);
        oldPriorities[i] = navigatorResourcePriorityBeforeViewport(resources[i].supported ? resources[i].formatHint : 3u);
        priorities[i] = navigatorResourcePriorityWithViewport(viewportClass,
            resources[i].supported ? resources[i].formatHint : 3u);
        int duplicateOf = -1;
        for (size_t prior = 0; prior < i; ++prior) {
            if (resources[prior].url == resources[i].url) {
                duplicateOf = canonical[prior];
                break;
            }
        }
        if (duplicateOf >= 0) {
            ++result.stats.duplicateReferences;
            if (static_cast<uint8_t>(classes[i]) < result.canonicalViewportClass[duplicateOf]) {
                result.canonicalViewportClass[duplicateOf] = static_cast<uint8_t>(classes[i]);
                classes[duplicateOf] = classes[i];
                priorities[duplicateOf] = priorities[i];
            }
            continue;
        }
        canonical[i] = static_cast<int>(i);
        result.canonicalViewportClass[i] = static_cast<uint8_t>(viewportClass);
        ++result.stats.uniqueReferences;
        pending[i] = true;
        if (resources[i].supported) ++result.stats.schedulerCandidates;
    }

    for (;;) {
        int selected = -1;
        for (size_t i = 0; i < count; ++i) {
            if (!pending[i]) continue;
            const uint8_t leftPriority = viewportAware ? priorities[i] : oldPriorities[i];
            const uint8_t rightPriority = selected < 0 ? 0 :
                (viewportAware ? priorities[static_cast<size_t>(selected)] : oldPriorities[static_cast<size_t>(selected)]);
            if (selected < 0 || leftPriority < rightPriority ||
                (leftPriority == rightPriority && i < static_cast<size_t>(selected))) {
                selected = static_cast<int>(i);
            }
        }
        if (selected < 0) break;
        pending[static_cast<size_t>(selected)] = false;
        const size_t index = static_cast<size_t>(selected);
        result.selectionOrder.push_back(static_cast<uint32_t>(index));
        const NavigatorResourceViewportClass viewportClass = classes[index];
        if (viewportAware) {
            for (size_t other = 0; other < count; ++other) {
                if (pending[other] && other < index &&
                    priorities[index] < priorities[other] &&
                    oldPriorities[index] >= oldPriorities[other]) {
                    ++result.stats.visiblePriorityAdmissions;
                    break;
                }
            }
        }
        if (viewportClass == NavigatorResourceViewportClass::Unsupported) {
            ++result.stats.unsupportedSkipped;
            continue;
        }
        if (resources[index].encodedTooLarge) continue;
        ++result.networkFetches;
        ++result.stats.fetchStarted;
        ++result.stats.fetchCompleted;
        if (!result.memory.reserveDecoded(resources[index].decodedBytes, budget)) {
            ++result.stats.budgetDenied;
            result.stats.noteDeniedDecoded(resources[index].decodedBytes);
            fixtureNoteViewportBudgetDenial(result.stats, viewportClass);
            continue;
        }
        ++result.decodes;
        ++result.stats.decodeStarted;
        if (resources[index].corrupt) {
            result.memory.releaseDecoded(resources[index].decodedBytes);
            continue;
        }
        ++result.stats.decoded;
        ++result.stats.attached;
        ++result.stats.activeCount;
        result.stats.activeBytes = result.memory.activeDecodedBytes;
        result.stats.peakActiveBytes = std::max(result.stats.peakActiveBytes, result.stats.activeBytes);
        result.stats.noteLoadedDecoded(resources[index].decodedBytes);
        fixtureNoteViewportLoad(result.stats, viewportClass, resources[index].decodedBytes);
    }
    return result;
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

    // 11. Deterministic competing-resource fixture: four far 16 MiB images
    // precede four visible and four near 4 MiB images.  The old policy fills
    // the 64 MiB budget with far images; viewport admission spends it on the
    // visible/near set first without changing any cap.
    std::array<ViewportFixtureResource, 64> competing{};
    for (size_t i = 0; i < 4; ++i) {
        competing[i] = {"https://fixture.test/far-" + std::to_string(i) + ".png",
            16u * 1024u * 1024u, 2400 + static_cast<int32_t>(i * 120),
            2400 + static_cast<int32_t>(i * 120) + 128};
    }
    for (size_t i = 0; i < 4; ++i) {
        competing[4 + i] = {"https://fixture.test/visible-" + std::to_string(i) + ".png",
            4u * 1024u * 1024u, 120 + static_cast<int32_t>(i * 120),
            120 + static_cast<int32_t>(i * 120) + 96};
        competing[8 + i] = {"https://fixture.test/near-" + std::to_string(i) + ".png",
            4u * 1024u * 1024u, 700 + static_cast<int32_t>(i * 120),
            700 + static_cast<int32_t>(i * 120) + 96};
    }
    const ViewportFixtureResult baselineCompeting = runViewportFixture(
        competing, 12, kNavigatorDecodedImageBudgetBytes, false);
    const ViewportFixtureResult viewportCompeting = runViewportFixture(
        competing, 12, kNavigatorDecodedImageBudgetBytes, true);
    ok &= expect(baselineCompeting.stats.visibleLoaded == 0 &&
        baselineCompeting.stats.visibleBudgetDenied == 4,
        "baseline competing fixture denies visible resources");
    ok &= expect(baselineCompeting.stats.farLoaded == 4 &&
        baselineCompeting.memory.activeDecodedBytes == kNavigatorDecodedImageBudgetBytes,
        "baseline competing fixture spends full budget far below fold");
    ok &= expect(viewportCompeting.stats.visibleLoaded == 4 &&
        viewportCompeting.stats.visibleBudgetDenied == 0 &&
        viewportCompeting.stats.nearLoaded == 4 &&
        viewportCompeting.stats.farLoaded == 2,
        "viewport competing fixture loads visible and near resources first");
    ok &= expect(viewportCompeting.stats.decodedBytesVisible == 16u * 1024u * 1024u &&
        baselineCompeting.stats.decodedBytesVisible == 0,
        "viewport fixture improves visible decoded bytes under the same budget");
    ok &= expect(viewportCompeting.memory.activeDecodedBytes == baselineCompeting.memory.activeDecodedBytes &&
        viewportCompeting.memory.activeDecodedBytes == kNavigatorDecodedImageBudgetBytes,
        "viewport fixture preserves exact aggregate image budget");
    ok &= expect(viewportCompeting.stats.visiblePriorityAdmissions > 0,
        "visible admissions record displacement of earlier far resources");

    // 12. A visible resource wins an exact one-image budget even when the far
    // resource is earlier in source order.
    std::array<ViewportFixtureResource, 64> visibleVsFar{};
    visibleVsFar[0] = {"https://fixture.test/far-first.png", 16u * 1024u * 1024u, 2200, 2300};
    visibleVsFar[1] = {"https://fixture.test/visible-second.png", 16u * 1024u * 1024u, 100, 200};
    const ViewportFixtureResult visibleVsFarResult = runViewportFixture(
        visibleVsFar, 2, 16u * 1024u * 1024u, true);
    ok &= expect(visibleVsFarResult.stats.visibleLoaded == 1 &&
        visibleVsFarResult.stats.farLoaded == 0 &&
        visibleVsFarResult.stats.farBudgetDenied == 1,
        "visible resource wins exact budget over earlier far resource");

    // 13. Visible, near, far form a strict priority ladder.
    std::array<ViewportFixtureResource, 64> ladder{};
    ladder[0] = {"https://fixture.test/far.png", 8u * 1024u * 1024u, 2200, 2300};
    ladder[1] = {"https://fixture.test/near.png", 8u * 1024u * 1024u, 650, 750};
    ladder[2] = {"https://fixture.test/visible.png", 8u * 1024u * 1024u, 120, 220};
    const ViewportFixtureResult ladderResult = runViewportFixture(
        ladder, 3, 16u * 1024u * 1024u, true);
    ok &= expect(ladderResult.stats.visibleLoaded == 1 && ladderResult.stats.nearLoaded == 1 &&
        ladderResult.stats.farLoaded == 0 && ladderResult.stats.farBudgetDenied == 1,
        "near resource outranks far resource after visible resource");

    // 14. Equal-class resources remain source-ordinal stable across runs.
    std::array<ViewportFixtureResource, 64> equalClass{};
    equalClass[0] = {"https://fixture.test/v0.png", 1u * 1024u * 1024u, 100, 160};
    equalClass[1] = {"https://fixture.test/v1.png", 1u * 1024u * 1024u, 200, 260};
    equalClass[2] = {"https://fixture.test/v2.png", 1u * 1024u * 1024u, 300, 360};
    const ViewportFixtureResult equalClassA = runViewportFixture(equalClass, 3, 3u * 1024u * 1024u, true);
    const ViewportFixtureResult equalClassB = runViewportFixture(equalClass, 3, 3u * 1024u * 1024u, true);
    ok &= expect(equalClassA.selectionOrder == equalClassB.selectionOrder &&
        equalClassA.selectionOrder == std::vector<uint32_t>({0, 1, 2}),
        "equal visible class ordering is deterministic by source ordinal");

    // 15. Unknown geometry is a deterministic fallback and cannot outrank
    // clearly visible supported content.
    std::array<ViewportFixtureResource, 64> unknownGeometry{};
    unknownGeometry[0] = {"https://fixture.test/unknown.png", 4u * 1024u * 1024u, -1, -1};
    unknownGeometry[1] = {"https://fixture.test/known-visible.png", 4u * 1024u * 1024u, 120, 220};
    const ViewportFixtureResult unknownResult = runViewportFixture(
        unknownGeometry, 2, 4u * 1024u * 1024u, true);
    ok &= expect(unknownResult.stats.unknownViewportReferences == 1 &&
        unknownResult.selectionOrder.size() == 2 &&
        unknownResult.stats.visibleLoaded == 1 && unknownResult.stats.visibleBudgetDenied == 0,
        "unknown geometry falls back below visible content");

    // 16. Duplicate canonical ownership promotes the far-first URL to
    // Visible while retaining one fetch and one decode.
    std::array<ViewportFixtureResource, 64> duplicateViewport{};
    duplicateViewport[0] = {"https://fixture.test/shared.png", 8u * 1024u * 1024u, 2200, 2300};
    duplicateViewport[1] = {"https://fixture.test/shared.png", 8u * 1024u * 1024u, 100, 200};
    const ViewportFixtureResult duplicateViewportResult = runViewportFixture(
        duplicateViewport, 2, 8u * 1024u * 1024u, true);
    ok &= expect(duplicateViewportResult.stats.duplicateReferences == 1 &&
        duplicateViewportResult.networkFetches == 1 && duplicateViewportResult.decodes == 1 &&
        duplicateViewportResult.stats.visibleLoaded == 1 &&
        duplicateViewportResult.canonicalViewportClass[0] ==
            static_cast<uint8_t>(NavigatorResourceViewportClass::Visible),
        "duplicate canonical resource inherits best visible relevance once");

    // 17. Hard safety and continuation rules remain stronger than priority.
    std::array<ViewportFixtureResource, 64> oversizedVisible{};
    oversizedVisible[0] = {"https://fixture.test/oversized-visible.png",
        kNavigatorMaxDecodedImageBytes + 1u, 100, 200};
    oversizedVisible[1] = {"https://fixture.test/small-visible.png",
        1u * 1024u * 1024u, 220, 320};
    const ViewportFixtureResult oversizedResult = runViewportFixture(
        oversizedVisible, 2, kNavigatorDecodedImageBudgetBytes, true);
    ok &= expect(oversizedResult.stats.visibleLoaded == 1 &&
        oversizedResult.stats.visibleBudgetDenied == 1 &&
        oversizedResult.memory.activeDecodedBytes == 1u * 1024u * 1024u,
        "oversized visible image cannot bypass per-image safety cap");

    std::array<ViewportFixtureResource, 64> corruptVisible{};
    corruptVisible[0] = {"https://fixture.test/corrupt-visible.jpg", 1u * 1024u * 1024u, 100, 200, 2u, true, true};
    corruptVisible[1] = {"https://fixture.test/valid-visible.png", 1u * 1024u * 1024u, 220, 320};
    const ViewportFixtureResult corruptResult = runViewportFixture(corruptVisible, 2, kNavigatorDecodedImageBudgetBytes, true);
    ok &= expect(corruptResult.networkFetches == 2 && corruptResult.stats.visibleLoaded == 1,
        "corrupt visible resource does not starve later valid visible image");

    std::array<ViewportFixtureResource, 64> unsupportedVisible{};
    unsupportedVisible[0] = {"https://fixture.test/visible.svg", 1u * 1024u * 1024u, 100, 200, 3u, false};
    unsupportedVisible[1] = {"https://fixture.test/near.jpg", 1u * 1024u * 1024u, 650, 750, 2u};
    const ViewportFixtureResult unsupportedVisibleResult = runViewportFixture(unsupportedVisible, 2, kNavigatorDecodedImageBudgetBytes, true);
    ok &= expect(unsupportedVisibleResult.networkFetches == 1 && unsupportedVisibleResult.stats.unsupportedSkipped == 1 &&
        unsupportedVisibleResult.stats.nearLoaded == 1,
        "unsupported visible resource is skipped without blocking near JPEG");

    std::array<ViewportFixtureResource, 64> encodedOverlimit{};
    encodedOverlimit[0] = {"https://fixture.test/encoded-overlimit.png", 1u * 1024u * 1024u, 100, 200, 1u, true, false, true};
    encodedOverlimit[1] = {"https://fixture.test/encoded-valid.png", 1u * 1024u * 1024u, 220, 320};
    const ViewportFixtureResult encodedOverlimitResult = runViewportFixture(encodedOverlimit, 2, kNavigatorDecodedImageBudgetBytes, true);
    ok &= expect(encodedOverlimitResult.networkFetches == 1 && encodedOverlimitResult.stats.visibleLoaded == 1,
        "encoded-overlimit visible resource does not starve later valid image");

    // 18. Fresh fixture state is identical after a simulated navigation reset.
    const ViewportFixtureResult competingAgain = runViewportFixture(
        competing, 12, kNavigatorDecodedImageBudgetBytes, true);
    ok &= expect(competingAgain.selectionOrder == viewportCompeting.selectionOrder &&
        competingAgain.stats.visibleLoaded == viewportCompeting.stats.visibleLoaded &&
        competingAgain.memory.activeDecodedBytes == viewportCompeting.memory.activeDecodedBytes,
        "repeated viewport-heavy navigation resets priority state deterministically");

    std::cout << (ok ? "Navigator resource scheduler tests PASS\n"
                     : "Navigator resource scheduler tests FAIL\n");
    return ok ? 0 : 1;
}
