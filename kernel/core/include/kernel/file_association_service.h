#ifndef KERNEL_FILE_ASSOCIATION_SERVICE_H
#define KERNEL_FILE_ASSOCIATION_SERVICE_H

#include <stdint.h>

namespace kernel { namespace appmodel {

enum class FileAssociationStatus : uint32_t {
    Resolved = 0, Unsupported = 1, InvalidPath = 2, PathTooLong = 3,
    Directory = 4, NotRegularFile = 5, NotFound = 6, IoFailure = 7,
};
struct FileAssociationResolution {
    FileAssociationStatus status;
    const char* applicationId;
};

enum class AssociationServiceStatus : uint32_t {
    Success = 0, NotSupported = 1, InvalidArgument = 2,
    UnknownExtension = 3, IneligibleHandler = 4,
    CapacityExceeded = 5, PersistenceFailed = 6,
    InvalidPersistedState = 7,
};
enum class AssociationOverrideState : uint32_t {
    NoOverride = 0, ApplicationOverride = 1, Disabled = 2,
};
enum class AssociationOperation : uint32_t {
    Query = 0, SetOverride = 1, Disable = 2, Reset = 3, Diagnostics = 4,
};
struct AssociationServiceRequest {
    uint32_t operation;
    char extension[16];
    char applicationId[96];
};
struct AssociationServiceResponse {
    uint32_t status;
    uint32_t overrideState;
    char normalizedExtension[16];
    char compiledDefaultAppId[96];
    char overrideAppId[96];
    char effectiveAppId[96];
    uint32_t hasEffectiveAssociation;
    uint32_t diagnosticSlot;
    uint64_t diagnosticGeneration;
};

static constexpr uint32_t kFileAssociationCapacity = 16u;
static constexpr uint32_t kFileAssociationPathCapacity = 96u;
struct AssociationStorage {
    void* context;
    // Return true only for an exact-size read. exists is false only when absent.
    bool (*readSlot)(void*, uint32_t, void*, uint32_t, bool* exists);
    // Return true only when all requested bytes were written.
    bool (*writeSlot)(void*, uint32_t, const void*, uint32_t);
    bool (*flushSlot)(void*, uint32_t);
};

void setFileAssociationStorage(const AssociationStorage* storage);
uint32_t fileAssociationCrc32(const uint8_t* bytes, uint32_t size);
FileAssociationResolution resolveFileAssociation(
    const char* path, bool isRegularFile, bool isDirectory);
uint32_t fileAssociationUsedCount();
uint32_t fileAssociationCapacity();
uint32_t fileAssociationTableBytes();
uint64_t fileAssociationActiveGeneration();
int32_t fileAssociationActiveSlot();
bool runC166AssociationServiceTests(uint32_t* outCases, uint32_t* outFailureMask);
AssociationServiceStatus fileAssociationService(
    const AssociationServiceRequest*, AssociationServiceResponse*);
void initializeFileAssociations();
bool fileAssociationPersistenceRejected();

} }
#endif
