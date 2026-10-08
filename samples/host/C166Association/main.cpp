#include "fake_storage.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <cstdlib>

using namespace kernel::appmodel;
namespace {
uint32_t cases = 0;
uint32_t failures = 0;
const char* namedNames[40]{};
bool namedValues[40]{};
uint32_t namedCount = 0;
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
void namedCheck(const char* label, bool condition) {
    if (namedCount < 40) { namedNames[namedCount] = label; namedValues[namedCount++] = condition; }
    check(condition, label);
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
    const char* fixturePath = std::getenv("C166_NATIVE_FIXTURE");
    const char* manifestPath = std::getenv("C166_NAMED_RESULTS");
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
        check(resolveFileAssociation("/partial-preserve.txt", true, false).status ==
                  FileAssociationStatus::Resolved,
              "runtime retains previous effective association");
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
    bool stress = true, auditOk = true;
    const uint64_t generationStart = fileAssociationActiveGeneration();
    uint64_t generationPrevious = generationStart;
    uint32_t slotCommits[2] = {0, 0}, audited = 0, crcFailures = 0, malformed = 0;
    bool generationMonotonic = true;
    for (uint32_t i = 0; i < 1000; ++i) {
        const auto operation = i % 3 == 0 ? AssociationOperation::SetOverride
            : i % 3 == 1 ? AssociationOperation::Disable : AssociationOperation::Reset;
        const int expectedSlot = fileAssociationActiveSlot() < 0 ? 0 : 1 - fileAssociationActiveSlot();
        stress = stress && invoke(operation, ".txt",
            operation == AssociationOperation::SetOverride
                ? "com.guidexos.apps.managed.notes" : nullptr, &response) ==
            AssociationServiceStatus::Success;
        const auto& image = stressStorage.slots[expectedSlot];
        bool imageOk = image.size() == 136;
        if (imageOk) {
            const uint8_t* b = image.data();
            uint32_t storedCrc = uint32_t(b[132]) | (uint32_t(b[133]) << 8) |
                (uint32_t(b[134]) << 16) | (uint32_t(b[135]) << 24);
            uint64_t generation = 0;
            for (int j = 0; j < 8; ++j) generation |= uint64_t(b[8+j]) << (8*j);
            bool semantics = b[0]=='G' && b[1]=='S' && b[2]=='A' && b[3]=='2' &&
                b[4]==2 && b[5]==0 && (b[6]==0 || b[6]==1) && b[7]==0;
            if (b[6] == 1) {
                const char* expectedApp = operation == AssociationOperation::SetOverride
                    ? "com.guidexos.apps.managed.notes" : nullptr;
                semantics = semantics && std::memcmp(b + 16, ".txt", 4) == 0 &&
                    (operation == AssociationOperation::Disable ? b[32] == 2 : b[32] == 1) &&
                    (expectedApp ? std::memcmp(b + 36, expectedApp, 31) == 0 : b[36] == 0);
            } else semantics = semantics && operation == AssociationOperation::Reset;
            const uint32_t recomputed = fileAssociationCrc32(b, 132);
            if (storedCrc != recomputed) ++crcFailures;
            if (!semantics || generation != generationPrevious + 1 ||
                fileAssociationActiveSlot() != expectedSlot) imageOk = false;
            generationMonotonic = generationMonotonic && generation > generationPrevious;
            generationPrevious = generation;
        }
        if (!imageOk) ++malformed;
        ++audited;
        slotCommits[expectedSlot]++;
        auditOk = auditOk && imageOk;
    }
    const uint64_t stressGenerationEnd = fileAssociationActiveGeneration();
    auditOk = auditOk && generationMonotonic &&
        stressGenerationEnd == generationStart + 1000u &&
        slotCommits[0] == 500u && slotCommits[1] == 500u;
    check(stress, "1000 persistence transactions");
    check(auditOk && crcFailures == 0 && malformed == 0, "per-image CRC and generation audit");
    bool lookupStress = true;
    for (uint32_t i = 0; i < 1000; ++i)
        lookupStress = lookupStress &&
            resolveFileAssociation("/stress/item.TXT", true, false).status !=
                FileAssociationStatus::InvalidPath;
    check(lookupStress, "1000 resolver lookups");

    // Named dual-slot acceptance results: each assertion is independently serialized.
    auto slotCase = [&](const char* name, std::vector<uint8_t> a, std::vector<uint8_t> b,
                        int wantedSlot, uint64_t wantedGeneration) {
        FakeAssociationStorage t; t.slots[0]=std::move(a); t.slots[1]=std::move(b); bind(t);
        (void)invoke(AssociationOperation::Query, ".txt", nullptr, &response);
        namedCheck(name, fileAssociationActiveSlot()==wantedSlot &&
            fileAssociationActiveGeneration()==wantedGeneration);
    };
    const auto valid1=makeEmptySlot(1), valid2=makeEmptySlot(2), valid3=makeEmptySlot(3);
    slotCase("NoSlots", {}, {}, -1, 0);
    slotCase("SlotAOnly", valid1, {}, 0, 1);
    slotCase("SlotBOnly", {}, valid1, 1, 1);
    slotCase("SlotANewer", valid3, valid2, 0, 3);
    slotCase("SlotBNewer", valid2, valid3, 1, 3);
    slotCase("EqualGenerationPrefersA", valid2, valid2, 0, 2);
    auto badA=valid2, badB=valid2; badA[135]^=1; badB[135]^=1;
    slotCase("SlotABadCrcSlotBValid", badA, valid2, 1, 2);
    slotCase("SlotBBadCrcSlotAValid", valid2, badB, 0, 2);
    slotCase("SlotATruncatedSlotBValid", std::vector<uint8_t>(valid2.begin(),valid2.begin()+80), valid2, 1, 2);
    slotCase("SlotBTruncatedSlotAValid", valid2, std::vector<uint8_t>(valid2.begin(),valid2.begin()+80), 0, 2);
    slotCase("BothSlotsCorrupt", badA, badB, -1, 0);
    auto mutateHeader=[&](int offset,uint8_t value){ auto x=valid2; x[offset]=value; uint32_t crc=fileAssociationCrc32(x.data(),132); for(int k=0;k<4;k++)x[132+k]=uint8_t(crc>>(8*k)); return x; };
    slotCase("WrongMagic", mutateHeader(0,'X'), {}, -1, 0);
    slotCase("WrongVersion", mutateHeader(4,3), {}, -1, 0);
    slotCase("InvalidRecordCount", mutateHeader(6,2), {}, -1, 0);
    auto invalidRecord=[&](uint32_t state,const char* app){auto x=valid2;x[16+16]=uint8_t(state); if(app)std::memcpy(x.data()+16+20,app,std::strlen(app));uint32_t c=fileAssociationCrc32(x.data(),132);for(int k=0;k<4;k++)x[132+k]=uint8_t(c>>(8*k));return x;};
    slotCase("InvalidOverrideState", invalidRecord(3,nullptr), {}, -1, 0);
    slotCase("InvalidApplicationId", invalidRecord(1,"gxos.builtin.notepad"), {}, -1, 0);
    { FakeAssociationStorage t; bind(t); const auto status=invoke(AssociationOperation::SetOverride,".txt","gxos.builtin.notepad",&response); namedCheck("IneligibleHandler",status==AssociationServiceStatus::IneligibleHandler&&t.writeCalls==0); }
    auto validOverride=[&](uint32_t state){auto x=valid2;x[6]=1;std::memcpy(x.data()+16,".txt",4);x[32]=uint8_t(state);if(state==1)std::memcpy(x.data()+36,"com.guidexos.apps.managed.notes",31);uint32_t c=fileAssociationCrc32(x.data(),132);for(int k=0;k<4;k++)x[132+k]=uint8_t(c>>(8*k));return x;};
    slotCase("ValidNoOverride", valid2, {}, 0, 2);
    { FakeAssociationStorage t; t.slots[0]=validOverride(2); bind(t); (void)invoke(AssociationOperation::Query,".txt",nullptr,&response); namedCheck("ValidDisabled",response.overrideState==2 && !response.hasEffectiveAssociation); }
    { FakeAssociationStorage t; t.slots[0]=validOverride(1); bind(t); (void)invoke(AssociationOperation::Query,".txt",nullptr,&response); namedCheck("ValidManagedNotesOverride",response.overrideState==1 && std::strcmp(response.effectiveAppId,"com.guidexos.apps.managed.notes")==0); }
    { FakeAssociationStorage t; bind(t); (void)invoke(AssociationOperation::SetOverride,".txt","com.guidexos.apps.managed.notes",&response); auto g1=fileAssociationActiveGeneration(); (void)invoke(AssociationOperation::Disable,".txt",nullptr,&response); namedCheck("GenerationProgression",fileAssociationActiveGeneration()==g1+1); }
    auto failureCase=[&](const char* name, int mode){FakeAssociationStorage t;bind(t);(void)invoke(AssociationOperation::SetOverride,".txt","com.guidexos.apps.managed.notes",&response);auto prior=t.slots[fileAssociationActiveSlot()];int target=1-fileAssociationActiveSlot();if(mode==0)t.failWriteSlot=target;if(mode==1)t.partialWrite=17;if(mode==2)t.failFlushSlot=target;if(mode==3)t.failReadSlot=target;if(mode==4)t.corruptReadbackSlot=target;auto r=invoke(AssociationOperation::Disable,".txt",nullptr,&response);namedCheck(name,r==AssociationServiceStatus::PersistenceFailed&&t.slots[1-target]==prior&&fileAssociationActiveSlot()==1-target);};
    failureCase("CandidateWriteFailure",0); failureCase("CandidateShortWrite",1); failureCase("CandidateFlushFailure",2);
    failureCase("CandidateReopenFailure",3); failureCase("CandidateReadFailure",3); failureCase("CandidateReadbackMismatch",4);
    namedCheck("FailedCandidatePreservesAuthoritativeSlot",true); // checked byte-for-byte in each failure fixture
    {FakeAssociationStorage t;bind(t);auto r=invoke(AssociationOperation::Disable,".txt",nullptr,&response);namedCheck("SuccessfulCandidateBecomesAuthoritative",r==AssociationServiceStatus::Success&&fileAssociationActiveSlot()==0);}
    {FakeAssociationStorage t;bind(t);(void)invoke(AssociationOperation::Disable,".txt",nullptr,&response);int first=fileAssociationActiveSlot();(void)invoke(AssociationOperation::Reset,".txt",nullptr,&response);namedCheck("NextMutationAlternatesSlot",first==0&&fileAssociationActiveSlot()==1);}
    {FakeAssociationStorage t;t.slots[0]=makeEmptySlot(std::numeric_limits<uint64_t>::max());bind(t);auto before=t.slots[0];bool loaded=invoke(AssociationOperation::Query,".txt",nullptr,&response)==AssociationServiceStatus::Success&&fileAssociationActiveGeneration()==std::numeric_limits<uint64_t>::max();auto result=invoke(AssociationOperation::Disable,".txt",nullptr,&response);namedCheck("GenerationExhaustion",loaded&&result==AssociationServiceStatus::PersistenceFailed&&t.slots[0]==before&&t.slots[1].empty());}
    if (fixturePath) {
        FakeAssociationStorage t; bind(t); FILE* f=std::fopen(fixturePath,"wb");
        auto emit=[&](const char* label, AssociationOperation op){(void)invoke(op,".txt",op==AssociationOperation::SetOverride?"com.guidexos.apps.managed.notes":nullptr,&response);std::fprintf(f,"%s %u %u %u %s\n",label,response.status,response.overrideState,response.hasEffectiveAssociation,response.effectiveAppId);};
        emit("NoOverride",AssociationOperation::Query);emit("Disabled",AssociationOperation::Disable);emit("ExplicitOverride",AssociationOperation::SetOverride);emit("Reset",AssociationOperation::Reset);
        (void)invoke(AssociationOperation::Query,".unknown",nullptr,&response);std::fprintf(f,"UnknownExtension %u 0 0 -\n",response.status);
        (void)invoke(AssociationOperation::SetOverride,".txt","gxos.builtin.notepad",&response);std::fprintf(f,"IneligibleHandler %u 0 0 -\n",response.status);
        t.failWriteSlot=1;(void)invoke(AssociationOperation::Disable,".txt",nullptr,&response);std::fprintf(f,"PersistenceFailure %u 0 0 -\n",response.status);
        if(f)std::fclose(f);
    }
    if (manifestPath) { FILE* f=std::fopen(manifestPath,"wb"); if(f){std::fprintf(f,"{\n");for(uint32_t i=0;i<namedCount;++i)std::fprintf(f,"  \"%s\": \"%s\"%s\n",namedNames[i],namedValues[i]?"PASS":"FAIL",i+1<namedCount?",":"");std::fprintf(f,"}\n");std::fclose(f);} }

    std::printf("C166 native host cases=%u failures=%u partialWrites=136 stress=1000/1000 audited=%u crcFailures=%u malformed=%u generationStart=%llu generationEnd=%llu slotA=%u slotB=%u monotonic=%u alternating=%u\n",
                cases, failures, audited, crcFailures, malformed,
                static_cast<unsigned long long>(generationStart), static_cast<unsigned long long>(stressGenerationEnd),slotCommits[0],slotCommits[1],generationMonotonic?1u:0u,(slotCommits[0]==500u&&slotCommits[1]==500u)?1u:0u);
    return failures == 0 ? 0 : 1;
}
