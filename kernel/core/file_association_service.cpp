#include "include/kernel/file_association_service.h"
#include <stddef.h>
#include <stdint.h>

namespace kernel { namespace appmodel {
namespace {
struct FileAssociationRecord {
    const char* extension;
    const char* applicationId;
};

constexpr const char* kManagedNotesApplicationId =
    "com.guidexos.apps.managed.notes";
constexpr FileAssociationRecord kFileAssociations[kFileAssociationCapacity] = {
    { ".txt", kManagedNotesApplicationId },
};
constexpr uint32_t kAssociationMagic = 0x32415347u; // GSA2
constexpr uint16_t kAssociationVersion = 2u;
constexpr uint32_t kAssociationOverrideCapacity = 1u;
enum class PersistedOverrideState : uint32_t { Application = 1u, Disabled = 2u };
struct AssociationOverrideRecord {
    char extension[16];
    uint32_t state;
    char applicationId[96];
};
struct AssociationPersistenceImage {
    uint32_t magic;
    uint16_t version;
    uint16_t recordCount;
    uint64_t generation;
    AssociationOverrideRecord records[kAssociationOverrideCapacity];
    uint32_t integrity;
};
static_assert(sizeof(AssociationOverrideRecord) == 116u, "C166 record layout drift");
static_assert(offsetof(AssociationPersistenceImage, records) == 16u, "C166 v2 header layout drift");
static_assert(offsetof(AssociationPersistenceImage, integrity) == 132u, "C166 v2 integrity offset drift");
static_assert(sizeof(AssociationPersistenceImage) == 136u, "C166 v2 image layout drift");
AssociationOverrideRecord s_override{};
AssociationStorage s_storage{};
bool s_hasOverride = false;
bool s_associationsInitialized = false;
bool s_invalidPersistedState = false;
uint64_t s_activeGeneration = 0u;
int32_t s_activeSlot = -1;
static_assert(sizeof(kFileAssociations) ==
    kFileAssociationCapacity * sizeof(FileAssociationRecord),
    "file association table must retain its fixed capacity");

bool asciiEqualsIgnoreCase(const char* left, const char* right) {
    if (!left || !right) return false;
    while (*left && *right) {
        char a = *left++;
        char b = *right++;
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return *left == '\0' && *right == '\0';
}

bool validCanonicalAssociationPath(const char* path, uint32_t* outLength,
                                   const char** outLeaf) {
    if (!path || !outLength || !outLeaf) return false;
    uint32_t length = 0u;
    while (length <= kFileAssociationPathCapacity && path[length] != '\0')
        ++length;
    if (length == 0u || length > kFileAssociationPathCapacity ||
        path[0] != '/' || path[length - 1u] == '/') return false;

    uint32_t componentStart = 1u;
    uint32_t leafStart = 1u;
    for (uint32_t index = 1u; index <= length; ++index) {
        if (index < length && path[index] == '\\') return false;
        if (index != length && path[index] != '/') continue;
        const uint32_t componentLength = index - componentStart;
        if (componentLength == 0u ||
            (componentLength == 1u && path[componentStart] == '.') ||
            (componentLength == 2u && path[componentStart] == '.' &&
                path[componentStart + 1u] == '.')) return false;
        leafStart = componentStart;
        componentStart = index + 1u;
    }
    *outLength = length;
    *outLeaf = path + leafStart;
    return true;
}

} // namespace

bool normalizedKnownExtension(const char* extension, char output[16]);
const char* compiledAssociation(const char* extension);
bool copyBounded(char* destination, uint32_t capacity, const char* source);
bool persistCandidate(AssociationPersistenceImage* image);
void loadAssociationOverrides();

FileAssociationResolution resolveFileAssociation(
    const char* path, bool isRegularFile, bool isDirectory) {
    uint32_t pathLength = 0u;
    const char* leaf = nullptr;
    if (!path) return { FileAssociationStatus::InvalidPath, nullptr };
    while (pathLength <= kFileAssociationPathCapacity && path[pathLength])
        ++pathLength;
    if (pathLength > kFileAssociationPathCapacity)
        return { FileAssociationStatus::PathTooLong, nullptr };
    if (!validCanonicalAssociationPath(path, &pathLength, &leaf))
        return { FileAssociationStatus::InvalidPath, nullptr };
    if (isDirectory) return { FileAssociationStatus::Directory, nullptr };
    if (!isRegularFile)
        return { FileAssociationStatus::NotRegularFile, nullptr };

    const char* lastDot = nullptr;
    for (const char* cursor = leaf; *cursor; ++cursor) {
        if (*cursor == '.') lastDot = cursor;
    }
    if (!lastDot || lastDot == leaf)
        return { FileAssociationStatus::Unsupported, nullptr };

    loadAssociationOverrides();
    for (uint32_t index = 0u; index < kFileAssociationCapacity; ++index) {
        const FileAssociationRecord& record = kFileAssociations[index];
        if (record.extension && record.applicationId &&
            asciiEqualsIgnoreCase(lastDot, record.extension)) {
            if (s_hasOverride && asciiEqualsIgnoreCase(s_override.extension,
                    record.extension)) {
                if (s_override.state == static_cast<uint32_t>(PersistedOverrideState::Disabled))
                    return { FileAssociationStatus::Unsupported, nullptr };
                return { FileAssociationStatus::Resolved, s_override.applicationId };
            }
            return { FileAssociationStatus::Resolved, record.applicationId };
        }
    }
    return { FileAssociationStatus::Unsupported, nullptr };
}
uint32_t fileAssociationUsedCount() {
    uint32_t count = 0u;
    for (uint32_t index = 0u; index < kFileAssociationCapacity; ++index) {
        if (kFileAssociations[index].extension &&
            kFileAssociations[index].applicationId) ++count;
    }
    return count;
}

uint32_t fileAssociationCapacity() {
    return kFileAssociationCapacity;
}

uint32_t fileAssociationTableBytes() {
    return static_cast<uint32_t>(sizeof(kFileAssociations));
}

uint64_t fileAssociationActiveGeneration() {
    loadAssociationOverrides();
    return s_activeGeneration;
}

int32_t fileAssociationActiveSlot() {
    loadAssociationOverrides();
    return s_activeSlot;
}

bool boundedEqualsIgnoreCase(const char* left, const char* right, uint32_t capacity) {
    if (!left || !right) return false;
    for (uint32_t i = 0u; i < capacity; ++i) {
        char a = left[i], b = right[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
        if (a == '\0') return true;
    }
    return false;
}

bool fixedEqualsCanonical(const char* field, uint32_t capacity,
                          const char* canonical) {
    uint32_t i = 0u;
    for (; i < capacity && canonical[i]; ++i)
        if (field[i] != canonical[i]) return false;
    if (i == capacity) return false;
    for (; i < capacity; ++i) if (field[i] != '\0') return false;
    return true;
}

void setFileAssociationStorage(const AssociationStorage* storage) {
    if (storage) s_storage = *storage;
    else s_storage = {};
    s_associationsInitialized = false;
    s_invalidPersistedState = false;
    s_activeGeneration = 0u;
    s_activeSlot = -1;
    s_hasOverride = false;
    s_override = {};
}

void initializeFileAssociations() {
    loadAssociationOverrides();
}

uint32_t fileAssociationCrc32(const uint8_t* bytes, uint32_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0u; i < size; ++i) {
        crc ^= bytes[i];
        for (uint32_t bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    return crc ^ 0xFFFFFFFFu;
}
bool fileAssociationPersistenceRejected() { loadAssociationOverrides(); return s_invalidPersistedState; }

AssociationServiceStatus fileAssociationService(
    const AssociationServiceRequest* request,
    AssociationServiceResponse* response) {
    if (!request || !response) return AssociationServiceStatus::InvalidArgument;
    loadAssociationOverrides();
    *response = {};
#if defined(GUIDEXOS_PROOF_CONTROL)
    constexpr uint32_t maximumOperation = static_cast<uint32_t>(AssociationOperation::Diagnostics);
#else
    constexpr uint32_t maximumOperation = static_cast<uint32_t>(AssociationOperation::Reset);
#endif
    if (request->operation > maximumOperation) {
        response->status = static_cast<uint32_t>(AssociationServiceStatus::InvalidArgument);
        return AssociationServiceStatus::InvalidArgument;
    }
    char extension[16]{};
    if (!normalizedKnownExtension(request->extension, extension)) {
        response->status = static_cast<uint32_t>(AssociationServiceStatus::UnknownExtension);
        return AssociationServiceStatus::UnknownExtension;
    }
    copyBounded(response->normalizedExtension,
        sizeof(response->normalizedExtension), extension);
    const AssociationOperation operation = static_cast<AssociationOperation>(request->operation);
    if (operation == AssociationOperation::Diagnostics) {
        response->overrideState = s_hasOverride
            ? (s_override.state == static_cast<uint32_t>(PersistedOverrideState::Disabled)
                ? static_cast<uint32_t>(AssociationOverrideState::Disabled)
                : static_cast<uint32_t>(AssociationOverrideState::ApplicationOverride))
            : static_cast<uint32_t>(AssociationOverrideState::NoOverride);
        response->diagnosticSlot = s_activeSlot < 0 ? 0xFFFFFFFFu
            : static_cast<uint32_t>(s_activeSlot);
        response->diagnosticGeneration = s_activeGeneration;
        response->status = static_cast<uint32_t>(AssociationServiceStatus::Success);
        return AssociationServiceStatus::Success;
    }
    if (operation == AssociationOperation::SetOverride &&
        !boundedEqualsIgnoreCase(request->applicationId, kManagedNotesApplicationId,
            sizeof(request->applicationId))) {
        response->status = static_cast<uint32_t>(AssociationServiceStatus::IneligibleHandler);
        return AssociationServiceStatus::IneligibleHandler;
    }
    const char* compiled = compiledAssociation(extension);
    if (compiled && !copyBounded(response->compiledDefaultAppId,
            sizeof(response->compiledDefaultAppId), compiled))
        return AssociationServiceStatus::InvalidPersistedState;
    if (operation != AssociationOperation::Query) {
        AssociationPersistenceImage candidate{};
        candidate.magic = kAssociationMagic;
        candidate.version = kAssociationVersion;
        if (operation == AssociationOperation::SetOverride || operation == AssociationOperation::Disable) {
            candidate.recordCount = 1u;
            copyBounded(candidate.records[0].extension,
                sizeof(candidate.records[0].extension), extension);
            if (operation == AssociationOperation::SetOverride) {
                candidate.records[0].state = static_cast<uint32_t>(PersistedOverrideState::Application);
                copyBounded(candidate.records[0].applicationId,
                    sizeof(candidate.records[0].applicationId), kManagedNotesApplicationId);
            } else {
                candidate.records[0].state = static_cast<uint32_t>(PersistedOverrideState::Disabled);
            }
        }
        if (!persistCandidate(&candidate)) {
            response->status = static_cast<uint32_t>(AssociationServiceStatus::PersistenceFailed);
            return AssociationServiceStatus::PersistenceFailed;
        }
        s_hasOverride = candidate.recordCount != 0u;
        s_override = s_hasOverride ? candidate.records[0] : AssociationOverrideRecord{};
    }
    if (s_hasOverride) {
        response->overrideState = s_override.state == static_cast<uint32_t>(PersistedOverrideState::Disabled)
            ? static_cast<uint32_t>(AssociationOverrideState::Disabled)
            : static_cast<uint32_t>(AssociationOverrideState::ApplicationOverride);
        if (response->overrideState == static_cast<uint32_t>(AssociationOverrideState::ApplicationOverride))
            copyBounded(response->overrideAppId, sizeof(response->overrideAppId), s_override.applicationId);
        if (response->overrideState != static_cast<uint32_t>(AssociationOverrideState::Disabled))
            copyBounded(response->effectiveAppId, sizeof(response->effectiveAppId), s_override.applicationId);
    } else if (compiled) {
        response->overrideState = static_cast<uint32_t>(AssociationOverrideState::NoOverride);
        copyBounded(response->effectiveAppId, sizeof(response->effectiveAppId), compiled);
    }
    response->hasEffectiveAssociation = response->effectiveAppId[0] ? 1u : 0u;
    response->status = static_cast<uint32_t>(AssociationServiceStatus::Success);
    return AssociationServiceStatus::Success;
}

bool runC166AssociationServiceTests(uint32_t* outCases,
                                    uint32_t* outFailureMask) {
    uint32_t cases = 0u;
    uint32_t failures = 0u;
    bool passed = true;
    auto check = [&passed, &cases, &failures](bool condition) {
        if (!condition && cases < 32u) failures |= 1u << cases;
        passed = passed && condition;
        ++cases;
    };
    AssociationServiceRequest request{};
    AssociationServiceResponse response{};
    request.operation = static_cast<uint32_t>(AssociationOperation::Query);
    copyBounded(request.extension, sizeof(request.extension), ".TXT");
    AssociationServiceStatus status = fileAssociationService(&request, &response);
    check(status == AssociationServiceStatus::Success &&
        response.overrideState == static_cast<uint32_t>(AssociationOverrideState::NoOverride) &&
        response.hasEffectiveAssociation == 1u &&
        asciiEqualsIgnoreCase(response.effectiveAppId, kManagedNotesApplicationId) &&
        asciiEqualsIgnoreCase(response.normalizedExtension, ".txt"));
    check(asciiEqualsIgnoreCase(response.compiledDefaultAppId,
        kManagedNotesApplicationId) && response.overrideAppId[0] == '\0');
#if defined(GUIDEXOS_PROOF_CONTROL)
    const int32_t diagnosticsSlot = fileAssociationActiveSlot();
    const uint64_t diagnosticsGeneration = fileAssociationActiveGeneration();
    request.operation = static_cast<uint32_t>(AssociationOperation::Diagnostics);
    status = fileAssociationService(&request, &response);
    check(status == AssociationServiceStatus::Success &&
        response.diagnosticSlot == (diagnosticsSlot < 0 ? 0xFFFFFFFFu :
            static_cast<uint32_t>(diagnosticsSlot)) &&
        response.diagnosticGeneration == diagnosticsGeneration &&
        fileAssociationActiveSlot() == diagnosticsSlot &&
        fileAssociationActiveGeneration() == diagnosticsGeneration);
    request.operation = static_cast<uint32_t>(AssociationOperation::Query);
#else
    request.operation = static_cast<uint32_t>(AssociationOperation::Diagnostics);
    check(fileAssociationService(&request, &response) ==
        AssociationServiceStatus::InvalidArgument);
    request.operation = static_cast<uint32_t>(AssociationOperation::Query);
#endif
    check(resolveFileAssociation("/proof/clean.txt", true, false).status ==
        FileAssociationStatus::Resolved);
    check(fileAssociationCapacity() == 16u && fileAssociationUsedCount() == 1u);

    request = {};
    request.operation = static_cast<uint32_t>(AssociationOperation::SetOverride);
    copyBounded(request.extension, sizeof(request.extension), ".txt");
    copyBounded(request.applicationId, sizeof(request.applicationId), kManagedNotesApplicationId);
    status = fileAssociationService(&request, &response);
    check(status == AssociationServiceStatus::Success && response.overrideState ==
        static_cast<uint32_t>(AssociationOverrideState::ApplicationOverride) &&
        asciiEqualsIgnoreCase(response.effectiveAppId, kManagedNotesApplicationId));
    check(response.hasEffectiveAssociation == 1u &&
        asciiEqualsIgnoreCase(response.overrideAppId, kManagedNotesApplicationId));
    check(resolveFileAssociation("/proof/override.txt", true, false).status ==
        FileAssociationStatus::Resolved);
    status = fileAssociationService(&request, &response);
    check(status == AssociationServiceStatus::Success && response.overrideState ==
        static_cast<uint32_t>(AssociationOverrideState::ApplicationOverride));

    request.operation = static_cast<uint32_t>(AssociationOperation::Disable);
    status = fileAssociationService(&request, &response);
    check(status == AssociationServiceStatus::Success && response.overrideState ==
        static_cast<uint32_t>(AssociationOverrideState::Disabled) &&
        response.hasEffectiveAssociation == 0u && response.effectiveAppId[0] == '\0');
    check(resolveFileAssociation("/proof/disabled.txt", true, false).status ==
        FileAssociationStatus::Unsupported);
    status = fileAssociationService(&request, &response);
    check(status == AssociationServiceStatus::Success && response.overrideState ==
        static_cast<uint32_t>(AssociationOverrideState::Disabled));

    request.operation = static_cast<uint32_t>(AssociationOperation::Reset);
    status = fileAssociationService(&request, &response);
    check(status == AssociationServiceStatus::Success && response.overrideState ==
        static_cast<uint32_t>(AssociationOverrideState::NoOverride) &&
        asciiEqualsIgnoreCase(response.effectiveAppId, kManagedNotesApplicationId));
    check(resolveFileAssociation("/proof/reset.txt", true, false).status ==
        FileAssociationStatus::Resolved);
    status = fileAssociationService(&request, &response);
    check(status == AssociationServiceStatus::Success && response.overrideState ==
        static_cast<uint32_t>(AssociationOverrideState::NoOverride));

    request.operation = static_cast<uint32_t>(AssociationOperation::Query);
    copyBounded(request.extension, sizeof(request.extension), ".bin");
    check(fileAssociationService(&request, &response) == AssociationServiceStatus::UnknownExtension);
    copyBounded(request.extension, sizeof(request.extension), "txt");
    check(fileAssociationService(&request, &response) == AssociationServiceStatus::UnknownExtension);
    copyBounded(request.extension, sizeof(request.extension), ".TXT");
    check(fileAssociationService(&request, &response) == AssociationServiceStatus::Success &&
        response.overrideState == static_cast<uint32_t>(AssociationOverrideState::NoOverride));
    copyBounded(request.extension, sizeof(request.extension), ".txt");
    request.operation = 99u;
    check(fileAssociationService(&request, &response) == AssociationServiceStatus::InvalidArgument);
    request.operation = static_cast<uint32_t>(AssociationOperation::SetOverride);
    copyBounded(request.applicationId, sizeof(request.applicationId), "gxos.builtin.notepad");
    check(fileAssociationService(&request, &response) == AssociationServiceStatus::IneligibleHandler);
    request.applicationId[0] = '\0';
    check(fileAssociationService(&request, &response) == AssociationServiceStatus::IneligibleHandler);
    copyBounded(request.applicationId, sizeof(request.applicationId),
        "com.guidexos.apps.managed.fileexplorer");
    check(fileAssociationService(&request, &response) == AssociationServiceStatus::IneligibleHandler);
    check(fileAssociationService(nullptr, &response) == AssociationServiceStatus::InvalidArgument);
    check(fileAssociationService(&request, nullptr) == AssociationServiceStatus::InvalidArgument);

    request = {};
    request.operation = static_cast<uint32_t>(AssociationOperation::SetOverride);
    copyBounded(request.extension, sizeof(request.extension), ".txt");
    copyBounded(request.applicationId, sizeof(request.applicationId), kManagedNotesApplicationId);
    uint64_t generation = s_activeGeneration;
    bool generationMonotonic = true;
    for (uint32_t cycle = 0u; cycle < 5u; ++cycle) {
        status = fileAssociationService(&request, &response);
        generationMonotonic = generationMonotonic && status == AssociationServiceStatus::Success &&
            s_activeGeneration == generation + 1u;
        generation = s_activeGeneration;
        request.operation = static_cast<uint32_t>(AssociationOperation::Disable);
    }
    request.operation = static_cast<uint32_t>(AssociationOperation::Reset);
    status = fileAssociationService(&request, &response);
    generationMonotonic = generationMonotonic && status == AssociationServiceStatus::Success &&
        s_activeGeneration == generation + 1u && response.overrideState ==
        static_cast<uint32_t>(AssociationOverrideState::NoOverride);
    check(generationMonotonic);

    bool stress = true;
    for (uint32_t cycle = 0u; cycle < 100u; ++cycle) {
        request.operation = static_cast<uint32_t>(AssociationOperation::Disable);
        status = fileAssociationService(&request, &response);
        stress = stress && status == AssociationServiceStatus::Success &&
            response.overrideState == static_cast<uint32_t>(AssociationOverrideState::Disabled);
        request.operation = static_cast<uint32_t>(AssociationOperation::Reset);
        status = fileAssociationService(&request, &response);
        stress = stress && status == AssociationServiceStatus::Success &&
            response.overrideState == static_cast<uint32_t>(AssociationOverrideState::NoOverride) &&
            asciiEqualsIgnoreCase(response.effectiveAppId, kManagedNotesApplicationId);
    }
    check(stress);
    if (outCases) *outCases = cases;
    if (outFailureMask) *outFailureMask = failures;
    return passed && cases >= 25u;
}

bool copyBounded(char* destination, uint32_t capacity, const char* source) {
    if (!destination || capacity == 0u || !source) return false;
    uint32_t i = 0u;
    while (source[i] && i + 1u < capacity) { destination[i] = source[i]; ++i; }
    if (source[i]) return false;
    destination[i] = '\0';
    while (++i < capacity) destination[i] = '\0';
    return true;
}

const char* compiledAssociation(const char* extension) {
    for (uint32_t i = 0u; i < kFileAssociationCapacity; ++i)
        if (kFileAssociations[i].extension &&
            asciiEqualsIgnoreCase(extension, kFileAssociations[i].extension))
            return kFileAssociations[i].applicationId;
    return nullptr;
}

bool normalizedKnownExtension(const char* extension, char output[16]) {
    if (!extension) return false;
    uint32_t length = 0u;
    while (length < 16u && extension[length]) ++length;
    if (length < 2u || length >= 16u || extension[0] != '.') return false;
    for (uint32_t i = 1u; i < length; ++i) {
        char c = extension[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9'))) return false;
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        output[i] = c;
    }
    output[0] = '.'; output[length] = '\0';
    return compiledAssociation(output) != nullptr;
}

bool validateImage(const AssociationPersistenceImage& image) {
    if (image.magic != kAssociationMagic || image.version != kAssociationVersion ||
        image.recordCount > kAssociationOverrideCapacity || image.generation == 0u)
        return false;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&image);
    if (fileAssociationCrc32(bytes,
            offsetof(AssociationPersistenceImage, integrity)) != image.integrity) return false;
    if (image.recordCount == 0u) {
        const uint8_t* record = reinterpret_cast<const uint8_t*>(&image.records[0]);
        for (uint32_t i = 0u; i < sizeof(image.records[0]); ++i)
            if (record[i] != 0u) return false;
        return true;
    }
    char normalized[16]{};
    if (!normalizedKnownExtension(image.records[0].extension, normalized) ||
        !fixedEqualsCanonical(image.records[0].extension,
            sizeof(image.records[0].extension), normalized)) return false;
    if (image.records[0].state == static_cast<uint32_t>(PersistedOverrideState::Disabled)) {
        for (uint32_t i = 0u; i < sizeof(image.records[0].applicationId); ++i)
            if (image.records[0].applicationId[i] != '\0') return false;
        return true;
    }
    if (image.records[0].state != static_cast<uint32_t>(PersistedOverrideState::Application))
        return false;
    return fixedEqualsCanonical(image.records[0].applicationId,
        sizeof(image.records[0].applicationId), kManagedNotesApplicationId);
}

bool readAssociationSlot(uint32_t slot, AssociationPersistenceImage* image,
                         bool* present) {
    if (!image || !present || slot > 1u || !s_storage.readSlot) return false;
    bool exists = false;
    const bool read = s_storage.readSlot(s_storage.context, slot, image,
        sizeof(*image), &exists);
    *present = exists;
    return exists && read && validateImage(*image);
}

bool persistCandidate(AssociationPersistenceImage* image) {
    if (!image || s_activeGeneration == UINT64_MAX) return false;
    image->generation = s_activeGeneration + 1u;
    image->integrity = 0u;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(image);
    image->integrity = fileAssociationCrc32(bytes,
        offsetof(AssociationPersistenceImage, integrity));
    const uint32_t target = s_activeSlot < 0 ? 0u :
        static_cast<uint32_t>(1 - s_activeSlot);
    if (!s_storage.writeSlot || !s_storage.readSlot || !s_storage.flushSlot ||
        !s_storage.writeSlot(s_storage.context, target, image, sizeof(*image)) ||
        !s_storage.flushSlot(s_storage.context, target)) return false;
    AssociationPersistenceImage readback{};
    bool exists = false;
    if (!s_storage.readSlot(s_storage.context, target, &readback,
            sizeof(readback), &exists) || !exists || !validateImage(readback) ||
        __builtin_memcmp(image, &readback, sizeof(*image)) != 0) return false;
    s_activeGeneration = image->generation;
    s_activeSlot = static_cast<int32_t>(target);
    return true;
}

void loadAssociationOverrides() {
    if (s_associationsInitialized) return;
    s_associationsInitialized = true;
    s_hasOverride = false;
    s_override = {};
    AssociationPersistenceImage images[2]{};
    bool present[2] = { false, false };
    const bool valid[2] = {
        readAssociationSlot(0u, &images[0], &present[0]),
        readAssociationSlot(1u, &images[1], &present[1])
    };
    if (present[0] && !valid[0]) s_invalidPersistedState = true;
    if (present[1] && !valid[1]) s_invalidPersistedState = true;
    if (!valid[0] && !valid[1]) return;
    uint32_t selected = valid[0] ? 0u : 1u;
    if (valid[0] && valid[1])
        selected = images[1].generation > images[0].generation ? 1u : 0u;
    s_activeSlot = static_cast<int32_t>(selected);
    s_activeGeneration = images[selected].generation;
    if (images[selected].recordCount != 0u) {
        s_override = images[selected].records[0];
        s_hasOverride = true;
    }
}

} } // namespace kernel::appmodel
