#include "fake_storage.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>

using namespace kernel::appmodel;
namespace {
uint32_t cases = 0;
uint32_t failures = 0;
void check(bool condition, const char* label) {
    ++cases;
    if (!condition) { ++failures; std::printf("FAIL %s\n", label); }
}
AssociationServiceStatus invoke(AssociationOperation operation,
                                const char* extension,
                                const char* app,
                                AssociationServiceResponse* response) {
    AssociationServiceRequest request{};
    request.operation = static_cast<uint32_t>(operation);
    std::strncpy(request.extension, extension, sizeof(request.extension) - 1);
    if (app) std::strncpy(request.applicationId, app,
                          sizeof(request.applicationId) - 1);
    return fileAssociationService(&request, response);
}
void bind(FakeAssociationStorage& storage) {
    AssociationStorage interface = storage.Interface();
    setFileAssociationStorage(&interface);
}
#pragma pack(push, 1)
struct SlotImageForFixture {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    uint64_t generation;
    uint8_t record[116];
    uint32_t crc;
};
#pragma pack(pop)
static_assert(sizeof(SlotImageForFixture) == 136, "C166 fixture format size");
std::vector<uint8_t> makeEmptySlot(uint64_t generation) {
    SlotImageForFixture image{};
    image.magic = 0x32415347u;
    image.version = 2;
    image.generation = generation;
    image.crc = fileAssociationCrc32(
        reinterpret_cast<const uint8_t*>(&image), 132);
    const auto* begin = reinterpret_cast<const uint8_t*>(&image);
    return { begin, begin + sizeof(image) };
}
}

int main() {
    const uint8_t crcInput[] = { '1','2','3','4','5','6','7','8','9' };
    check(fileAssociationCrc32(crcInput, sizeof(crcInput)) == 0xCBF43926u,
          "CRC-32/ISO-HDLC known vector");
    check(fileAssociationCrc32(nullptr, 0u) == 0u, "CRC empty vector");
    FakeAssociationStorage storage;
    bind(storage);
    AssociationServiceResponse response{};
    check(invoke(AssociationOperation::Query, ".txt", nullptr, &response) ==
        AssociationServiceStatus::Success, "clean query");
    check(std::strcmp(response.effectiveAppId,
        "com.guidexos.apps.managed.notes") == 0, "compiled default");
    check(fileAssociationCapacity() == 16 && fileAssociationUsedCount() == 1,
        "fixed table capacity");
    check(fileAssociationTableBytes() == 16u * 2u * sizeof(void*), "compiled table bytes");
    check(invoke(AssociationOperation::SetOverride, ".TXT",
        "com.guidexos.apps.managed.notes", &response) == AssociationServiceStatus::Success,
        "explicit override");
    check(response.overrideState == static_cast<uint32_t>(AssociationOverrideState::ApplicationOverride),
        "override state");
    check(invoke(AssociationOperation::Disable, ".txt", nullptr, &response) ==
        AssociationServiceStatus::Success && response.hasEffectiveAssociation == 0,
        "disabled state");
    check(invoke(AssociationOperation::Reset, ".txt", nullptr, &response) ==
        AssociationServiceStatus::Success && response.overrideState == 0,
        "reset state");
    check(invoke(AssociationOperation::Query, ".unknown", nullptr, &response) ==
        AssociationServiceStatus::UnknownExtension, "unknown extension");
    check(invoke(AssociationOperation::SetOverride, ".txt", "gxos.builtin.notepad",
        &response) == AssociationServiceStatus::IneligibleHandler, "ineligible handler");
    check(invoke(AssociationOperation::Query, ".txt", nullptr, &response) ==
        AssociationServiceStatus::Success && response.overrideState == 0,
        "runtime remains default after validation errors");

    uint32_t existing = 0, failed = 0;
    check(runC166AssociationServiceTests(&existing, &failed), "production service suite");
    check(existing >= 25 && failed == 0, "production suite count and mask");

    // Each test starts from a committed authoritative generation 2 image.
    for (int partial = 0; partial < 136; ++partial) {
        FakeAssociationStorage trial;
        bind(trial);
        invoke(AssociationOperation::SetOverride, ".txt",
            "com.guidexos.apps.managed.notes", &response);
        invoke(AssociationOperation::Reset, ".txt", nullptr, &response);
        const int activeSlot = fileAssociationActiveSlot();
        const auto authoritative = trial.slots[activeSlot];
        trial.partialWrite = partial;
        const auto result = invoke(AssociationOperation::Disable, ".txt", nullptr,
                                   &response);
        check(result == AssociationServiceStatus::PersistenceFailed,
              "partial write rejected");
        check(trial.slots[activeSlot] == authoritative,
              "authoritative bytes preserved");
        bind(trial);
        check(invoke(AssociationOperation::Query, ".txt", nullptr, &response) ==
                  AssociationServiceStatus::Success && response.overrideState == 0,
              "prior generation reloads");
    }

    FakeAssociationStorage full;
    bind(full);
    invoke(AssociationOperation::SetOverride, ".txt",
        "com.guidexos.apps.managed.notes", &response);
    invoke(AssociationOperation::Reset, ".txt", nullptr, &response);
    full.partialWrite = -1;
    check(invoke(AssociationOperation::Disable, ".txt", nullptr, &response) ==
        AssociationServiceStatus::Success && fileAssociationActiveGeneration() == 3,
        "full write control");

    FakeAssociationStorage alternating;
    bind(alternating);
    invoke(AssociationOperation::SetOverride, ".txt",
        "com.guidexos.apps.managed.notes", &response);
    const auto slotA = alternating.slots[0];
    invoke(AssociationOperation::Reset, ".txt", nullptr, &response);
    check(fileAssociationActiveSlot() == 1 && fileAssociationActiveGeneration() == 2,
        "alternating targets and generations");
    check(alternating.slots[0] == slotA && alternating.slots[1].size() == 136,
        "alternating slot contents");
    alternating.slots[0] = alternating.slots[1];
    bind(alternating);
    check(fileAssociationActiveSlot() == 0 && fileAssociationActiveGeneration() == 2,
        "equal generation selects A");
    invoke(AssociationOperation::Disable, ".txt", nullptr, &response);
    check(fileAssociationActiveSlot() == 1 && fileAssociationActiveGeneration() == 3,
        "equal generation mutation targets B");

    FakeAssociationStorage corrupt;
    corrupt.slots[0] = { 1, 2, 3 };
    corrupt.slots[1] = { 4, 5, 6 };
    bind(corrupt);
    check(invoke(AssociationOperation::Query, ".txt", nullptr, &response) ==
        AssociationServiceStatus::Success && response.overrideState == 0 &&
        response.hasEffectiveAssociation == 1, "both corrupt slots use default");

    FakeAssociationStorage exhaustion;
    exhaustion.slots[0] = makeEmptySlot(std::numeric_limits<uint64_t>::max());
    bind(exhaustion);
    const auto maxSlot = exhaustion.slots[0];
    check(invoke(AssociationOperation::Query, ".txt", nullptr, &response) ==
        AssociationServiceStatus::Success &&
        fileAssociationActiveGeneration() == std::numeric_limits<uint64_t>::max(),
        "generation exhaustion loaded");
    check(invoke(AssociationOperation::Disable, ".txt", nullptr, &response) ==
        AssociationServiceStatus::PersistenceFailed && exhaustion.slots[0] == maxSlot &&
        exhaustion.slots[1].empty(), "generation exhaustion does not write");

    FakeAssociationStorage flushFailure;
    bind(flushFailure);
    invoke(AssociationOperation::SetOverride, ".txt",
        "com.guidexos.apps.managed.notes", &response);
    flushFailure.failFlushSlot = 1;
    check(invoke(AssociationOperation::Reset, ".txt", nullptr, &response) ==
        AssociationServiceStatus::PersistenceFailed && fileAssociationActiveSlot() == 0,
        "flush failure leaves runtime authoritative state");

    FakeAssociationStorage readFailure;
    readFailure.slots[0] = makeEmptySlot(1);
    readFailure.failReadSlot = 0;
    bind(readFailure);
    check(invoke(AssociationOperation::Query, ".txt", nullptr, &response) ==
        AssociationServiceStatus::Success && response.hasEffectiveAssociation == 1 &&
        fileAssociationPersistenceRejected(), "slot read failure falls back safely");

    FakeAssociationStorage readbackFailure;
    bind(readbackFailure);
    invoke(AssociationOperation::SetOverride, ".txt",
        "com.guidexos.apps.managed.notes", &response);
    const auto priorAuthoritative = readbackFailure.slots[0];
    readbackFailure.corruptReadbackSlot = 1;
    check(invoke(AssociationOperation::Disable, ".txt", nullptr, &response) ==
        AssociationServiceStatus::PersistenceFailed &&
        readbackFailure.slots[0] == priorAuthoritative && fileAssociationActiveSlot() == 0,
        "readback corruption preserves prior authority");

    FakeAssociationStorage stressStorage;
    bind(stressStorage);
    bool stress = true;
    for (uint32_t i = 0; i < 1000; ++i) {
        const auto operation = i % 3 == 0 ? AssociationOperation::SetOverride
            : i % 3 == 1 ? AssociationOperation::Disable : AssociationOperation::Reset;
        stress = stress && invoke(operation, ".txt",
            operation == AssociationOperation::SetOverride
                ? "com.guidexos.apps.managed.notes" : nullptr, &response) ==
            AssociationServiceStatus::Success;
    }
    check(stress, "1000 persistence transactions");
    bool lookupStress = true;
    for (uint32_t i = 0; i < 1000; ++i)
        lookupStress = lookupStress &&
            resolveFileAssociation("/stress/item.TXT", true, false).status !=
                FileAssociationStatus::InvalidPath;
    check(lookupStress, "1000 resolver lookups");

    std::printf("C166 native host cases=%u failures=%u partialWrites=136 stress=1000/1000\n",
                cases, failures);
    return failures == 0 ? 0 : 1;
}
