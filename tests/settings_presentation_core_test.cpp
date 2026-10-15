#include "settings_presentation_adapter.h"

#include <iostream>
#include <string>

using namespace gxos::apps::settings;

namespace {

int checks = 0;
int failures = 0;

void check(bool passed, const char* description)
{
    ++checks;
    if (!passed) {
        ++failures;
        std::cerr << "FAIL: " << description << '\n';
    }
}

bool getWindowConstraints(void*, SettingsWindowConstraints*) { return true; }
bool pollInput(void*, SettingsInputEvent*) { return false; }
bool readProviderSnapshot(void*, CapabilityId, void*, size_t, size_t*, uint64_t*) { return false; }
bool readCapabilitySnapshot(void*, CapabilityId, CapabilitySnapshot*) { return false; }
bool submitOperation(void*, const OperationRequest*) { return false; }
bool pollOperationResult(void*, OperationResult*) { return false; }
bool present(void*, const SettingsPresentationCore*) { return true; }

} // namespace

int main()
{
    SettingsPresentationCore core;
    check(core.selectedCategory() == CategoryId::System, "initial category is System");
    check(core.route() == SettingsRoute{ CategoryId::System, TargetId::Page }, "initial route is the System page");
    check(core.focusedControl() == FocusControl::None, "initial focus is empty");
    check(!core.hasSearchQuery() && core.searchQueryLength() == 0, "initial search is empty");
    check(kCategories.size() == 12 && CategoryId::Count == static_cast<CategoryId>(12),
          "the stable category table contains all twelve identifiers");

    for (size_t i = 0; i < kCategoryCount; ++i) {
        const CategoryId id = static_cast<CategoryId>(i);
        const CategoryInfo* info = categoryInfo(id);
        check(validCategory(id) && info != nullptr, "each category identifier resolves");
        check(static_cast<size_t>(kCategories[i].id) == i && kCategories[i].slug[0] != '\0' &&
              kCategories[i].label[0] != '\0' && kCategories[i].description[0] != '\0',
              "category identifiers and declarative labels remain ordered and complete");
        check(core.selectCategory(id) && core.route() == SettingsRoute{ id, TargetId::Page },
              "each category can be selected as a top-level page");
    }
    check(!validCategory(CategoryId::Count) && categoryInfo(CategoryId::Count) == nullptr,
          "the category sentinel is rejected as an input category");
    const SettingsRoute beforeInvalidCategory = core.route();
    check(!core.selectCategory(CategoryId::Count) && core.route() == beforeInvalidCategory,
          "invalid category selection leaves navigation unchanged");
    check(core.nextCategory() && core.selectedCategory() == CategoryId::System,
          "forward navigation wraps from About to System");
    check(core.previousCategory() && core.selectedCategory() == CategoryId::About,
          "backward navigation wraps from System to About");
    check(core.nextCategory() && core.selectedCategory() == CategoryId::System,
          "forward navigation leaves a stable wrapped route");

    check(core.navigate(SettingsRoute{ CategoryId::Display, TargetId::Resolution }),
          "valid deep-link route is accepted");
    check(core.route() == SettingsRoute{ CategoryId::Display, TargetId::Resolution },
          "deep-link selection updates the route");
    check(core.canFocus(FocusControl::DisplayResolution) &&
          core.setFocusedControl(FocusControl::DisplayResolution) &&
          core.focusedControl() == FocusControl::DisplayResolution,
          "focus selection accepts a control on the active page");
    check(!core.setFocusedControl(FocusControl::StorageDiskManager) &&
          core.focusedControl() == FocusControl::DisplayResolution,
          "focus selection rejects a control on a hidden page");
    check(core.selectCategory(CategoryId::Display) &&
          core.route() == SettingsRoute{ CategoryId::Display, TargetId::Page } &&
          core.focusedControl() == FocusControl::None,
          "reselecting a category clears its nested target and selected control");
    check(!core.navigate(SettingsRoute{ CategoryId::Count, TargetId::Page }),
          "navigation rejects an invalid category");
    check(!core.navigate(SettingsRoute{ CategoryId::Network, TargetId::Resolution }),
          "navigation rejects a target belonging to another category");
    check(!core.navigate(SettingsRoute{ CategoryId::Storage, static_cast<TargetId>(0xFFu) }),
          "navigation rejects malformed target identifiers");

    SettingsRoute parsed{ CategoryId::About, TargetId::AboutVersion };
    check(parseSettingsRoute(" settings://NETWORK-INTERNET/ipv4 ", parsed) &&
          parsed == SettingsRoute{ CategoryId::Network, TargetId::IPv4 },
          "route parsing remains bounded, trimmed, and case-insensitive");
    check(parseSettingsRoute("settings://dateandtime/timezone", parsed) &&
          parsed == SettingsRoute{ CategoryId::DateTime, TargetId::DateTimeTimeZone },
          "route parser retains the Date and Time alias");
    check(parseSettingsRoute("settings://", parsed) &&
          parsed == SettingsRoute{ CategoryId::System, TargetId::Page },
          "empty route path retains the System default");
    check(!parseSettingsRoute(nullptr, parsed), "null route input is rejected");
    check(!parseSettingsRoute("settings://unknown", parsed), "unknown route category is rejected");
    check(!parseSettingsRoute("settings://network/resolution", parsed), "category-mismatched route is rejected");
    check(!parseSettingsRoute("settings://network/ipv4/extra", parsed), "malformed nested route is rejected");
    const std::string oversizedRoute(kSettingsRouteMaxLength + 1, 'x');
    check(!parseSettingsRoute(oversizedRoute.c_str(), parsed), "oversized route input is rejected within a fixed bound");

    check(searchSettings("").accepted && searchSettings("").count == 0,
          "empty search has no results");
    const SearchResultSet matching = searchSettings("ENHANCED FOCUS");
    check(matching.accepted && matching.count > 0 &&
          matching.values[0].route == SettingsRoute{ CategoryId::Accessibility, TargetId::AccessibilityFocus },
          "case-insensitive matching finds the existing Accessibility control");
    const SearchResultSet nonmatching = searchSettings("no-such-setting");
    check(nonmatching.accepted && nonmatching.count == 0,
          "nonmatching search does not fabricate results");
    check(!searchSettings(nullptr).accepted, "null search input is rejected");
    const SearchResultSet networkResults = searchSettings("network");
    bool foundNetworkPage = false;
    bool foundDevices = false;
    for (size_t i = 0; i < networkResults.count; ++i) {
        foundNetworkPage = foundNetworkPage || networkResults.values[i].route.category == CategoryId::Network;
        foundDevices = foundDevices || networkResults.values[i].route.category == CategoryId::Devices;
        check(validRoute(networkResults.values[i].route), "every search result points to a valid page element");
    }
    check(networkResults.count <= SearchResultSet::kCapacity && foundNetworkPage && foundDevices,
          "global search preserves bounded results across category changes");
    for (size_t i = 0; i < kPageElementCount; ++i) {
        check(kPageElements[i].label[0] != '\0' && kPageElements[i].keywords[0] != '\0' &&
              validRoute(kPageElements[i].route),
              "declarative page elements have a label, keywords, and a valid route");
    }

    check(core.setSearchQuery("network") && core.searchResults().count > 0,
          "presentation state stores a matching search query");
    check(core.selectCategory(CategoryId::Display) && !core.hasSearchQuery() &&
          core.searchResults().count == 0,
          "category navigation clears the active global search like the hosted UI");
    std::string maximumQuery(kSearchQueryMaxLength, 'x');
    check(core.setSearchQuery(maximumQuery.c_str()) &&
          core.searchQueryLength() == kSearchQueryMaxLength,
          "maximum supported query length is accepted");
    const std::string oversizedQuery(kSearchQueryMaxLength + 1, 'x');
    check(!core.setSearchQuery(oversizedQuery.c_str()) &&
          core.searchQueryLength() == kSearchQueryMaxLength &&
          std::string(core.searchQuery()) == maximumQuery,
          "oversized query is rejected without truncating current state");
    check(!searchSettings(oversizedQuery.c_str()).accepted,
          "search matcher rejects a query beyond its supported bound");
    check(core.backspaceSearch() && core.searchQueryLength() == kSearchQueryMaxLength - 1,
          "backspace removes one bounded query character");
    check(core.setSearchQuery("network") && core.searchResults().count > 0,
          "search can be replaced after a category change");
    check(!core.setSearchQuery("bad\tquery") && std::string(core.searchQuery()) == "network",
          "malformed control characters are rejected without changing the query");
    check(core.setSearchQuery("") && !core.hasSearchQuery() && !core.backspaceSearch(),
          "empty query reset and empty backspace are stable");
    for (size_t i = 0; i < kSearchQueryMaxLength; ++i)
        check(core.appendSearchCharacter('a'), "query append succeeds within fixed capacity");
    check(!core.appendSearchCharacter('a') && core.searchQueryLength() == kSearchQueryMaxLength,
          "query append rejects a character beyond fixed capacity");

    core.reset();
    size_t expectedCategory = 0;
    for (size_t transition = 0; transition < 1000; ++transition) {
        if ((transition % 3) == 0) {
            expectedCategory = expectedCategory == 0 ? kCategoryCount - 1 : expectedCategory - 1;
            check(core.previousCategory(), "deterministic backward transition succeeds");
        } else {
            expectedCategory = (expectedCategory + 1) % kCategoryCount;
            check(core.nextCategory(), "deterministic forward transition succeeds");
        }
        check(static_cast<size_t>(core.selectedCategory()) == expectedCategory &&
              validRoute(core.route()), "1,000 transitions retain bounded, deterministic navigation state");
    }
    core.reset();
    check(core.selectedCategory() == CategoryId::System && core.route().target == TargetId::Page,
          "reset returns navigation to its initialized state");

    for (size_t i = 0; i < 100; ++i) {
        core.selectCategory(static_cast<CategoryId>(i % kCategoryCount));
        core.setSearchQuery("about");
        core.reset();
        check(core.selectedCategory() == CategoryId::System && !core.hasSearchQuery() &&
              core.focusedControl() == FocusControl::None,
              "repeated initialization and shutdown-style reset clears bounded state");
    }
    size_t lifecycleCycles = 0;
    for (size_t i = 0; i < 100; ++i) {
        {
            SettingsPresentationCore lifecycleCore;
            lifecycleCore.selectCategory(static_cast<CategoryId>(i % kCategoryCount));
            lifecycleCore.setSearchQuery("network");
            check(validCategory(lifecycleCore.selectedCategory()) && lifecycleCore.hasSearchQuery(),
                  "fresh presentation core initializes bounded state");
        }
        ++lifecycleCycles;
    }
    check(lifecycleCycles == 100,
          "presentation core completes 100 scoped initialization and shutdown lifetimes");
    check(sizeof(SettingsPresentationCore) <= 1024 && sizeof(SearchResultSet) <= 256,
          "presentation state and result storage remain fixed and bounded");

    check(core.capability(CapabilityId::Users)->availability == CapabilityAvailability::Unknown,
          "capability state begins unknown until an adapter supplies a snapshot");
    check(core.applyCapabilitySnapshot({ CapabilityId::Network, CapabilityAvailability::ReadOnly, 10 }),
          "read-only capability snapshot is accepted");
    check(core.applyCapabilitySnapshot({ CapabilityId::Users, CapabilityAvailability::Unsupported, 1 }),
          "unsupported capability snapshot is accepted");
    check(core.applyCapabilitySnapshot({ CapabilityId::Devices, CapabilityAvailability::Unavailable, 2 }),
          "temporarily unavailable capability snapshot is accepted");
    check(core.applyCapabilitySnapshot({ CapabilityId::Display, CapabilityAvailability::Writable, 3 }),
          "writable capability snapshot is accepted");
    check(!core.applyCapabilitySnapshot({ CapabilityId::Count, CapabilityAvailability::Writable, 1 }) &&
          !core.applyCapabilitySnapshot({ CapabilityId::Apps, CapabilityAvailability::Unknown, 1 }),
          "invalid capability identifiers and unknown provider snapshots are rejected");
    check(core.capability(CapabilityId::Network)->availability == CapabilityAvailability::ReadOnly &&
          core.capability(CapabilityId::Users)->availability == CapabilityAvailability::Unsupported &&
          core.capability(CapabilityId::Devices)->availability == CapabilityAvailability::Unavailable,
          "read-only, unsupported, and unavailable states remain distinct");
    check(!core.capability(CapabilityId::Count), "invalid capability lookup is bounded");

    OperationRequest request{};
    check(!core.beginOperation(CapabilityId::Network, 1, request) &&
          !core.beginOperation(CapabilityId::Users, 1, request) &&
          !core.beginOperation(CapabilityId::Devices, 1, request),
          "read-only, unsupported, and unavailable capabilities cannot start writes");
    const uint8_t payload[] = { 0x41, 0x00, 0x7F };
    check(core.beginOperation(CapabilityId::Display,
          static_cast<uint16_t>(SettingsOperationCode::ApplyDisplayConfiguration),
          request, payload, sizeof(payload)),
          "writable capability creates a bounded pending request");
    const uint32_t firstRequestId = request.requestId;
    check(firstRequestId != 0 && request.baseRevision == 3 && request.payloadLength == sizeof(payload) &&
          request.payload[0] == payload[0] && request.payload[1] == payload[1] &&
          request.payload[2] == payload[2],
          "operation request preserves its revision and bounded payload");
    check(core.capability(CapabilityId::Display)->status == PresentationStatus::Pending,
          "request state is pending until provider completion");
    check(!core.beginOperation(CapabilityId::Display, 1, request),
          "duplicate write is rejected while the capability is pending");
    check(core.applyCapabilitySnapshot({ CapabilityId::Display, CapabilityAvailability::Writable, 4 }) &&
          core.capability(CapabilityId::Display)->status == PresentationStatus::Pending,
          "provider refresh does not infer operation success");
    check(!core.applyCapabilitySnapshot({ CapabilityId::Display, CapabilityAvailability::ReadOnly, 2 }) &&
          core.capability(CapabilityId::Display)->revision == 4,
          "stale provider refresh is rejected");
    SettingsPresentationCore displayWrapCore;
    check(displayWrapCore.applyCapabilitySnapshot({ CapabilityId::Display,
              CapabilityAvailability::ReadOnly, 0x100000000ull }) &&
          displayWrapCore.applyCapabilitySnapshot({ CapabilityId::Display,
              CapabilityAvailability::ReadOnly, 2u }) &&
          displayWrapCore.capability(CapabilityId::Display)->revision == 2u,
          "Display source generation wrap advances with bounded serial ordering");
    check(!displayWrapCore.applyCapabilitySnapshot({ CapabilityId::Display,
              CapabilityAvailability::ReadOnly, 0x100000000ull }) &&
          displayWrapCore.capability(CapabilityId::Display)->revision == 2u,
          "a delayed pre-wrap Display revision is rejected after wrap");
    check(!core.completeOperation({ firstRequestId + 1, OperationCompletion::Success }),
          "stale operation completion is ignored");
    check(!core.completeOperation({ firstRequestId, static_cast<OperationCompletion>(9) }),
          "malformed operation completion is rejected");
    check(core.completeOperation({ firstRequestId, OperationCompletion::Success }) &&
          core.capability(CapabilityId::Display)->status == PresentationStatus::Success,
          "matching provider completion marks an operation successful");
    check(!core.completeOperation({ firstRequestId, OperationCompletion::Failure }),
          "repeated completion cannot overwrite a finished operation");

    OperationRequest failedRequest{};
    check(core.beginOperation(CapabilityId::Display, 2, failedRequest) &&
          failedRequest.requestId != firstRequestId,
          "a later operation receives a distinct request identifier");
    check(!core.completeOperation({ firstRequestId, OperationCompletion::Success }) &&
          core.capability(CapabilityId::Display)->status == PresentationStatus::Pending,
          "late completion from an earlier operation cannot finish the current request");
    check(core.completeOperation({ failedRequest.requestId, OperationCompletion::Failure }) &&
          core.capability(CapabilityId::Display)->status == PresentationStatus::Failure,
          "provider failure is represented separately from success");
    check(!core.beginOperation(CapabilityId::Display, 0, request),
          "zero operation code is rejected");
    const uint8_t oversizedPayload[kOperationPayloadCapacity + 1]{};
    check(!core.beginOperation(CapabilityId::Display, 1, request, oversizedPayload, sizeof(oversizedPayload)) &&
          !core.beginOperation(CapabilityId::Display, 1, request, nullptr, 1),
          "oversized or malformed operation payloads are rejected");

    for (uint64_t generation = 11; generation < 261; ++generation) {
        check(core.applyCapabilitySnapshot({ CapabilityId::Network,
                  CapabilityAvailability::ReadOnly, generation }),
              "repeated provider refresh cycle is accepted");
    }
    check(core.capability(CapabilityId::Network)->revision == 260 &&
          !core.applyCapabilitySnapshot({ CapabilityId::Network, CapabilityAvailability::Unavailable, 259 }),
          "250 refresh cycles retain the newest provider generation");

    SettingsPresentationAdapterV1 adapter{};
    adapter.structureSize = sizeof(adapter);
    adapter.version = kSettingsPresentationAdapterVersion;
    adapter.getWindowConstraints = getWindowConstraints;
    adapter.pollInput = pollInput;
    adapter.readProviderSnapshot = readProviderSnapshot;
    adapter.readCapabilitySnapshot = readCapabilitySnapshot;
    adapter.submitOperation = submitOperation;
    adapter.pollOperationResult = pollOperationResult;
    adapter.present = present;
    check(validSettingsPresentationAdapter(&adapter), "complete versioned adapter contract is accepted");
    check(!validSettingsPresentationAdapter(nullptr), "null adapter contract is rejected");
    adapter.version++;
    check(!validSettingsPresentationAdapter(&adapter), "unsupported adapter contract version is rejected");
    adapter.version = kSettingsPresentationAdapterVersion;
    adapter.submitOperation = nullptr;
    check(!validSettingsPresentationAdapter(&adapter), "missing required adapter operation callback is rejected");

    std::cout << "Settings presentation core tests: " << (checks - failures) << "/" << checks << " passed\n";
    return failures == 0 ? 0 : 1;
}
