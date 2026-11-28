#include "include/kernel/native_sql_application_service.h"
#include "include/kernel/arch.h"

#include <stdint.h>

#if (defined(ARCH_AMD64) && defined(GX_NATIVE_SQL_READ_API) && GX_NATIVE_SQL_READ_API) || defined(GXOS_HOSTED_TEST)

#include "sdk/include/guidexos/app.h"
#include "gx_index_boolean_key.h"
#include "gx_index_integer_key.h"
#include "gx_index_text_key.h"
#include "gx_native_sql_read_service.h"

namespace kernel {
namespace native_sql_application {
namespace {

const uint32_t kMaxPrincipals = 8;
const uint32_t kMaxHandles = 16;
const uint32_t kHandleIndexBits = 4;
const uint64_t kMaxGeneration = UINT64_MAX >> kHandleIndexBits;

struct ApprovedDatabase {
    uint32_t identity;
    GxNativeSqlReadFixture fixture;
    const char* name;
};

const ApprovedDatabase kApprovedDatabases[] = {
    {GX_SQL_DATABASE_R3AH_MULTILEVEL,
     GX_NATIVE_SQL_FIXTURE_R3AH_MULTILEVEL,
     "NDB1R4P11R3AH_MULTILEVEL"}
};
const size_t kApprovedDatabaseCount =
    sizeof(kApprovedDatabases) / sizeof(kApprovedDatabases[0]);

static_assert(static_cast<uint32_t>(GX_NATIVE_SQL_KEY_INT32) == GX_SQL_KEY_INT32,
              "internal and public Int32 key tags differ");
static_assert(static_cast<uint32_t>(GX_NATIVE_SQL_KEY_INT64) == GX_SQL_KEY_INT64,
              "internal and public Int64 key tags differ");
static_assert(static_cast<uint32_t>(GX_NATIVE_SQL_KEY_BOOLEAN) == GX_SQL_KEY_BOOLEAN,
              "internal and public Boolean key tags differ");
static_assert(static_cast<uint32_t>(GX_NATIVE_SQL_KEY_TEXT) == GX_SQL_KEY_TEXT,
              "internal and public Text key tags differ");
static_assert(static_cast<uint32_t>(GX_NATIVE_SQL_INDEX_ROWS_EDGE64) ==
                  GX_SQL_INDEX_ROWS_EDGE64,
              "internal and public Edge64 index identities differ");

struct Principal {
    gx_app_context* context;
    uint64_t ownerId;
    uint8_t databaseReadGranted;
    BufferValidator validateBuffer;
};

struct DatabaseSession {
    uint64_t generation;
    uint64_t ownerId;
    uint32_t databaseIdentity;
    uint8_t active;
};

Principal g_principals[kMaxPrincipals] = {};
DatabaseSession g_sessions[kMaxHandles] = {};

const ApprovedDatabase* findApprovedDatabase(uint32_t identity) {
    for (size_t i = 0; i < kApprovedDatabaseCount; ++i) {
        if (kApprovedDatabases[i].identity == identity) {
            return &kApprovedDatabases[i];
        }
    }
    return nullptr;
}

uint32_t ownerSessionCount(uint64_t ownerId) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < kMaxHandles; ++i) {
        if (g_sessions[i].active && g_sessions[i].ownerId == ownerId) ++count;
    }
    return count;
}

#if defined(GX_NATIVE_SQL_READ_QUALIFICATION) && GX_NATIVE_SQL_READ_QUALIFICATION && \
    defined(GX_NATIVE_SQL_READ_DIAGNOSTIC) && GX_NATIVE_SQL_READ_DIAGNOSTIC
uint32_t g_dispatchSequence = 0;

struct DispatchTrace {
    gx_app_context* context;
    uint64_t ownerId;
    uint32_t sequence;
    const char* operation;
};

void appendText(char* output, size_t capacity, size_t* length,
                const char* text) {
    if (!output || !length || !text) return;
    while (*text && *length + 1u < capacity) output[(*length)++] = *text++;
    output[*length] = '\0';
}

void appendUnsigned(char* output, size_t capacity, size_t* length,
                    uint64_t value, uint8_t hexadecimal) {
    char digits[20];
    size_t count = 0;
    const uint32_t radix = hexadecimal ? 16u : 10u;
    do {
        const uint32_t digit = static_cast<uint32_t>(value % radix);
        digits[count++] = static_cast<char>(digit < 10u ? '0' + digit : 'A' + digit - 10u);
        value /= radix;
    } while (value && count < sizeof(digits));
    if (hexadecimal) appendText(output, capacity, length, "0x");
    while (count && *length + 1u < capacity) output[(*length)++] = digits[--count];
    output[*length] = '\0';
}

void emitTrace(gx_app_context* context, uint32_t sequence, uint64_t ownerId,
               const char* family, const char* event, const char* operation,
               const char* details) {
    if (!context || !context->host || !context->host->log) return;
    char line[192] = {};
    size_t length = 0;
    appendText(line, sizeof(line), &length, family);
    appendText(line, sizeof(line), &length, " seq=");
    appendUnsigned(line, sizeof(line), &length, sequence, 0);
    appendText(line, sizeof(line), &length, " runtime=");
    appendUnsigned(line, sizeof(line), &length, ownerId, 1);
    appendText(line, sizeof(line), &length, " event=");
    appendText(line, sizeof(line), &length, event);
    if (operation) {
        appendText(line, sizeof(line), &length, " op=");
        appendText(line, sizeof(line), &length, operation);
    }
    if (details) {
        appendText(line, sizeof(line), &length, " ");
        appendText(line, sizeof(line), &length, details);
    }
    (void)context->host->log(context, line);
}

DispatchTrace beginDispatch(Principal* principal, gx_app_context* context,
                            const char* operation, const char* request) {
    ++g_dispatchSequence;
    if (g_dispatchSequence == 0) ++g_dispatchSequence;
    DispatchTrace trace = {principal ? principal->context : nullptr,
        principal ? principal->ownerId : 0, g_dispatchSequence, operation};
    if (trace.context == context) {
        emitTrace(trace.context, trace.sequence, trace.ownerId, "SQL_APP_API",
            "ENTER", trace.operation, request);
    }
    return trace;
}

void dispatchService(const DispatchTrace& trace, const char* service) {
    if (!trace.context) return;
    char details[64] = {};
    size_t length = 0;
    appendText(details, sizeof(details), &length, "target=");
    appendText(details, sizeof(details), &length, service);
    emitTrace(trace.context, trace.sequence, trace.ownerId, "SQL_APP_API",
        "SERVICE", trace.operation, details);
}

const char* statusName(gx_sql_status status) {
    switch (status) {
        case GX_SQL_STATUS_OK: return "OK";
        case GX_SQL_STATUS_NO_MATCH: return "NO_MATCH";
        case GX_SQL_STATUS_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case GX_SQL_STATUS_INVALID_DATABASE: return "INVALID_DATABASE";
        case GX_SQL_STATUS_INVALID_INDEX: return "INVALID_INDEX";
        case GX_SQL_STATUS_INVALID_HANDLE: return "INVALID_HANDLE";
        case GX_SQL_STATUS_PERMISSION_DENIED: return "PERMISSION_DENIED";
        case GX_SQL_STATUS_CAPACITY_EXCEEDED: return "CAPACITY_EXCEEDED";
        case GX_SQL_STATUS_STORAGE_FAILURE: return "STORAGE_FAILURE";
        case GX_SQL_STATUS_CORRUPT_INDEX: return "CORRUPT_INDEX";
        case GX_SQL_STATUS_UNSUPPORTED: return "UNSUPPORTED";
        case GX_SQL_STATUS_RESOURCE_EXHAUSTED: return "RESOURCE_EXHAUSTED";
        case GX_SQL_STATUS_PACKAGE_INVALID: return "PACKAGE_INVALID";
        case GX_SQL_STATUS_DATABASE_UNAVAILABLE: return "DATABASE_UNAVAILABLE";
        case GX_SQL_STATUS_DATABASE_CORRUPT: return "DATABASE_CORRUPT";
        default: return "UNKNOWN";
    }
}

gx_sql_status finishDispatch(const DispatchTrace& trace, gx_sql_status status,
                             uint32_t resultCount) {
    if (trace.context) {
        char details[64] = {};
        size_t length = 0;
        appendText(details, sizeof(details), &length, "status=");
        appendText(details, sizeof(details), &length, statusName(status));
        appendText(details, sizeof(details), &length, " count=");
        appendUnsigned(details, sizeof(details), &length, resultCount, 0);
        emitTrace(trace.context, trace.sequence, trace.ownerId, "SQL_APP_API",
            "RETURN", trace.operation, details);
    }
    return status;
}

#else
struct DispatchTrace {};
DispatchTrace beginDispatch(Principal*, gx_app_context*, const char*, const char*) { return {}; }
void dispatchService(const DispatchTrace&, const char*) {}
gx_sql_status finishDispatch(const DispatchTrace&, gx_sql_status status, uint32_t) { return status; }
#endif

bool addressSpan(const void* address, size_t length,
                 uintptr_t* begin, uintptr_t* end) {
    if (!address || !begin || !end || length == 0) return false;
    const uintptr_t start = reinterpret_cast<uintptr_t>(address);
    if (length > static_cast<size_t>(UINTPTR_MAX - start)) return false;
    *begin = start;
    *end = start + length;
    return true;
}

bool spansOverlap(const void* a, size_t aLength,
                  const void* b, size_t bLength) {
    uintptr_t aBegin = 0, aEnd = 0, bBegin = 0, bEnd = 0;
    if (!addressSpan(a, aLength, &aBegin, &aEnd) ||
        !addressSpan(b, bLength, &bBegin, &bEnd)) return true;
    return aBegin < bEnd && bBegin < aEnd;
}

Principal* findPrincipal(gx_app_context* context) {
    if (!context) return nullptr;
    for (uint32_t i = 0; i < kMaxPrincipals; ++i) {
        if (g_principals[i].context == context) return &g_principals[i];
    }
    return nullptr;
}

bool bufferValid(Principal* principal, const void* address, size_t length,
                 uint8_t writable) {
    uintptr_t begin = 0, end = 0;
    return principal && principal->validateBuffer &&
        addressSpan(address, length, &begin, &end) &&
        principal->validateBuffer(principal->context, address, length, writable) != 0;
}

uint64_t makeHandle(uint32_t index, uint64_t generation) {
    return (generation << kHandleIndexBits) | index;
}

DatabaseSession* findSession(uint64_t handle, uint64_t ownerId,
                             gx_sql_status* failure) {
    const uint32_t index = static_cast<uint32_t>(handle & 0xFu);
    const uint64_t generation = handle >> kHandleIndexBits;
    if (handle == 0 || generation == 0 || index >= kMaxHandles ||
        !g_sessions[index].active ||
        g_sessions[index].generation != generation) {
        *failure = GX_SQL_STATUS_INVALID_HANDLE;
        return nullptr;
    }
    if (g_sessions[index].ownerId != ownerId) {
        *failure = GX_SQL_STATUS_PERMISSION_DENIED;
        return nullptr;
    }
    *failure = GX_SQL_STATUS_OK;
    return &g_sessions[index];
}

gx_sql_status mapReadStatus(GxNativeSqlReadStatus status) {
    switch (status) {
        case GX_NATIVE_SQL_READ_OK: return GX_SQL_STATUS_OK;
        case GX_NATIVE_SQL_READ_NO_MATCH: return GX_SQL_STATUS_NO_MATCH;
        case GX_NATIVE_SQL_READ_INVALID_REQUEST: return GX_SQL_STATUS_INVALID_ARGUMENT;
        case GX_NATIVE_SQL_READ_UNSUPPORTED_OPERATION: return GX_SQL_STATUS_UNSUPPORTED;
        case GX_NATIVE_SQL_READ_STORAGE_FAILURE: return GX_SQL_STATUS_STORAGE_FAILURE;
        case GX_NATIVE_SQL_READ_CORRUPT_INDEX: return GX_SQL_STATUS_CORRUPT_INDEX;
        case GX_NATIVE_SQL_READ_RESOURCE_EXHAUSTED: return GX_SQL_STATUS_RESOURCE_EXHAUSTED;
        default: return GX_SQL_STATUS_STORAGE_FAILURE;
    }
}

gx_sql_status mapCatalogValidation(GxNativeSqlReadStatus status,
    GxNativeSqlImageError imageError,
    GxNativeSqlImageError cleanupError) {
    if (status == GX_NATIVE_SQL_READ_OK &&
        cleanupError == GX_NATIVE_SQL_IMAGE_OK) return GX_SQL_STATUS_OK;
    if (cleanupError != GX_NATIVE_SQL_IMAGE_OK ||
        imageError == GX_NATIVE_SQL_IMAGE_READ_FAILURE ||
        imageError == GX_NATIVE_SQL_IMAGE_CLOSE_FAILURE ||
        imageError == GX_NATIVE_SQL_IMAGE_OFFSET_OVERFLOW ||
        imageError == GX_NATIVE_SQL_IMAGE_BAD_STATE) {
        return GX_SQL_STATUS_DATABASE_UNAVAILABLE;
    }
    if (imageError == GX_NATIVE_SQL_IMAGE_FILE_OPEN ||
        imageError == GX_NATIVE_SQL_IMAGE_UNSUPPORTED_MODE) {
        return GX_SQL_STATUS_PACKAGE_INVALID;
    }
    if (status == GX_NATIVE_SQL_READ_INVALID_REQUEST ||
        imageError == GX_NATIVE_SQL_IMAGE_INVALID_ARGUMENT) {
        return GX_SQL_STATUS_INVALID_ARGUMENT;
    }
    if (status == GX_NATIVE_SQL_READ_STORAGE_FAILURE &&
        imageError == GX_NATIVE_SQL_IMAGE_OK) {
        return GX_SQL_STATUS_DATABASE_UNAVAILABLE;
    }
    return GX_SQL_STATUS_DATABASE_CORRUPT;
}

bool copyCatalogName(char* destination, size_t capacity, const char* source) {
    if (!destination || capacity == 0 || !source) return false;
    size_t length = 0;
    while (source[length] && length < capacity) ++length;
    if (length == capacity) return false;
    for (size_t i = 0; i <= length; ++i) destination[i] = source[i];
    return true;
}

bool fillDatabaseCatalogEntry(const ApprovedDatabase& database,
                              gx_sql_catalog_entry* entry) {
    if (!entry) return false;
    *entry = {};
    entry->size = sizeof(*entry);
    entry->kind = GX_SQL_CATALOG_KIND_DATABASE;
    entry->identity = database.identity;
    entry->capabilities = GX_SQL_CATALOG_CAP_READ_ONLY |
        GX_SQL_CATALOG_CAP_IMMUTABLE_PACKAGE;
    return copyCatalogName(entry->name, sizeof(entry->name), database.name);
}

bool fillIndexCatalogEntry(const GxNativeSqlIndexCatalogEntry& metadata,
                           gx_sql_catalog_entry* entry) {
    const bool keyTypeSupported =
        metadata.keyType == GX_NATIVE_SQL_KEY_INT32 ||
        metadata.keyType == GX_NATIVE_SQL_KEY_INT64 ||
        metadata.keyType == GX_NATIVE_SQL_KEY_BOOLEAN ||
        metadata.keyType == GX_NATIVE_SQL_KEY_TEXT;
    const bool edge64NoNulls =
        metadata.identity == GX_SQL_INDEX_ROWS_EDGE64 &&
        metadata.keyType == GX_NATIVE_SQL_KEY_INT64 &&
        metadata.comparisonSemantics ==
            GX_NATIVE_SQL_COMPARE_SIGNED_ASCENDING_NO_NULLS;
    const uint32_t expectedComparison =
        metadata.keyType == GX_NATIVE_SQL_KEY_BOOLEAN
            ? GX_NATIVE_SQL_COMPARE_BOOLEAN_ASCENDING_NULLS_FIRST
            : metadata.keyType == GX_NATIVE_SQL_KEY_TEXT
                ? GX_NATIVE_SQL_COMPARE_TEXT_BYTES_ASCENDING_NULLS_FIRST
                : edge64NoNulls
                    ? GX_NATIVE_SQL_COMPARE_SIGNED_ASCENDING_NO_NULLS
                    : GX_NATIVE_SQL_COMPARE_SIGNED_ASCENDING_NULLS_FIRST;
    if (!entry || !metadata.identity || !metadata.name ||
        !keyTypeSupported ||
        (metadata.identity == GX_SQL_INDEX_ROWS_EDGE64 && !edge64NoNulls) ||
        metadata.maximumResults == 0 ||
        metadata.maximumResults > GX_SQL_MAX_RESULTS ||
        metadata.comparisonSemantics != expectedComparison) return false;
    *entry = {};
    entry->size = sizeof(*entry);
    entry->kind = GX_SQL_CATALOG_KIND_INDEX;
    entry->identity = metadata.identity;
    entry->keyType = metadata.keyType;
    entry->capabilities = GX_SQL_CATALOG_CAP_ORDERED_LOCATORS;
    if (metadata.capabilities & GX_NATIVE_SQL_INDEX_CAP_POINT) {
        entry->capabilities |= GX_SQL_CATALOG_CAP_POINT_LOOKUP;
    }
    if (metadata.capabilities & GX_NATIVE_SQL_INDEX_CAP_RANGE) {
        entry->capabilities |= GX_SQL_CATALOG_CAP_RANGE_SCAN;
    }
    if ((metadata.capabilities & ~(GX_NATIVE_SQL_INDEX_CAP_POINT |
                                   GX_NATIVE_SQL_INDEX_CAP_RANGE)) != 0 ||
        (metadata.capabilities & (GX_NATIVE_SQL_INDEX_CAP_POINT |
                                  GX_NATIVE_SQL_INDEX_CAP_RANGE)) == 0) {
        return false;
    }
    entry->comparisonSemantics = metadata.comparisonSemantics;
    entry->maximumResults = metadata.maximumResults;
    return copyCatalogName(entry->name, sizeof(entry->name), metadata.name);
}

bool validKey(const gx_sql_key& key, uint32_t requiredType) {
    if (key.type != requiredType || key.is_null > 1u) return false;
    if (requiredType == GX_SQL_KEY_BOOLEAN) {
        return key.is_null || key.value.boolean_value <= 1u;
    }
    if (requiredType == GX_SQL_KEY_TEXT) {
        return key.is_null
            ? key.value.text_value.length == 0
            : key.value.text_value.length <= GX_SQL_TEXT_INLINE_BYTES;
    }
    return true;
}

bool encodeKey(const gx_sql_key& key, uint32_t requiredType,
               uint8_t* bytes, size_t capacity, size_t* length) {
    if (!validKey(key, requiredType)) return false;
    if (requiredType == GX_SQL_KEY_INT64) {
        return gxEncodeIndexInt64Key(key.is_null != 0, key.value.int64_value,
            bytes, capacity, length) == GxStatus::Ok;
    }
    if (requiredType == GX_SQL_KEY_INT32) {
        return gxEncodeIndexInt32Key(key.is_null != 0, key.value.int32_value,
            bytes, capacity, length) == GxStatus::Ok;
    }
    if (requiredType == GX_SQL_KEY_BOOLEAN) {
        return gxEncodeIndexBooleanKey(key.is_null != 0,
            key.value.boolean_value != 0, bytes, capacity, length) == GxStatus::Ok;
    }
    if (requiredType == GX_SQL_KEY_TEXT) {
        if (key.is_null) {
            if (!bytes || capacity < 1 || !length) return false;
            bytes[0] = 0;
            *length = 1;
            return true;
        }
        return gxEncodeIndexTextKey(key.value.text_value.bytes,
            key.value.text_value.length, bytes, capacity, length) == GxStatus::Ok;
    }
    return false;
}

int compareKeys(const gx_sql_key& left, const gx_sql_key& right,
                uint32_t requiredType, bool* valid) {
    *valid = validKey(left, requiredType) && validKey(right, requiredType);
    if (!*valid) return 0;
    if (left.is_null != right.is_null) return left.is_null ? -1 : 1;
    if (left.is_null) return 0;
    if (requiredType == GX_SQL_KEY_INT64) {
        return left.value.int64_value < right.value.int64_value ? -1 :
            (left.value.int64_value > right.value.int64_value ? 1 : 0);
    }
    if (requiredType == GX_SQL_KEY_BOOLEAN) {
        return left.value.boolean_value < right.value.boolean_value ? -1 :
            (left.value.boolean_value > right.value.boolean_value ? 1 : 0);
    }
    if (requiredType == GX_SQL_KEY_TEXT) {
        const uint8_t leftLength = left.value.text_value.length;
        const uint8_t rightLength = right.value.text_value.length;
        const uint8_t sharedLength = leftLength < rightLength
            ? leftLength : rightLength;
        for (uint8_t i = 0; i < sharedLength; ++i) {
            const uint8_t leftByte = left.value.text_value.bytes[i];
            const uint8_t rightByte = right.value.text_value.bytes[i];
            if (leftByte != rightByte) return leftByte < rightByte ? -1 : 1;
        }
        return leftLength < rightLength ? -1 :
            (leftLength > rightLength ? 1 : 0);
    }
    return left.value.int32_value < right.value.int32_value ? -1 :
        (left.value.int32_value > right.value.int32_value ? 1 : 0);
}

gx_sql_status openReadOnlyDatabase(
    gx_app_context* context, uint32_t databaseIdentity, uint32_t flags,
    gx_sql_database_handle* outHandle, const DispatchTrace& trace) {
    Principal* principal = findPrincipal(context);
    if (!principal || !bufferValid(principal, outHandle, sizeof(*outHandle), 1)) {
        return GX_SQL_STATUS_INVALID_ARGUMENT;
    }
    *outHandle = 0;
    if (!principal->databaseReadGranted) return GX_SQL_STATUS_PERMISSION_DENIED;
    if (flags != GX_SQL_OPEN_READ_ONLY) return GX_SQL_STATUS_INVALID_ARGUMENT;
    const ApprovedDatabase* database = findApprovedDatabase(databaseIdentity);
    if (!database) return GX_SQL_STATUS_INVALID_DATABASE;

    GxNativeSqlImageError imageError = GX_NATIVE_SQL_IMAGE_OK;
    GxNativeSqlImageError cleanupError = GX_NATIVE_SQL_IMAGE_OK;
    dispatchService(trace, "SQL_VALIDATE_DATABASE");
    const GxNativeSqlReadStatus validation = gxNativeSqlValidateDatabase(
        database->fixture, &imageError, &cleanupError);
    if (validation != GX_NATIVE_SQL_READ_OK) return mapReadStatus(validation);
    if (cleanupError != GX_NATIVE_SQL_IMAGE_OK) return GX_SQL_STATUS_STORAGE_FAILURE;

    for (uint32_t i = 0; i < kMaxHandles; ++i) {
        DatabaseSession& session = g_sessions[i];
        if (session.active || session.generation == kMaxGeneration) continue;
        ++session.generation;
        session.ownerId = principal->ownerId;
        session.databaseIdentity = databaseIdentity;
        session.active = 1;
        *outHandle = makeHandle(i, session.generation);
        return GX_SQL_STATUS_OK;
    }
    return GX_SQL_STATUS_RESOURCE_EXHAUSTED;
}

gx_sql_status GX_CALL hostOpenReadOnlyDatabase(
    gx_app_context* context, uint32_t databaseIdentity, uint32_t flags,
    gx_sql_database_handle* outHandle) {
    Principal* principal = findPrincipal(context);
    const DispatchTrace trace = beginDispatch(principal, context, "OpenReadOnlyDatabase",
        "request=READ_ONLY_DATABASE");
    const gx_sql_status status = openReadOnlyDatabase(context, databaseIdentity,
        flags, outHandle, trace);
    return finishDispatch(trace, status, 0);
}

gx_sql_status closeDatabase(gx_app_context* context,
                            gx_sql_database_handle handle,
                            const DispatchTrace& trace) {
    Principal* principal = findPrincipal(context);
    if (!principal) return GX_SQL_STATUS_PERMISSION_DENIED;
    gx_sql_status failure = GX_SQL_STATUS_INVALID_HANDLE;
    DatabaseSession* session = findSession(handle, principal->ownerId, &failure);
    if (!session) return failure;
    dispatchService(trace, "OWNER_SESSION_RELEASE");
    session->active = 0;
    session->ownerId = 0;
    session->databaseIdentity = 0;
    return GX_SQL_STATUS_OK;
}

gx_sql_status GX_CALL hostCloseDatabase(gx_app_context* context,
                                         gx_sql_database_handle handle) {
    Principal* principal = findPrincipal(context);
    const DispatchTrace trace = beginDispatch(principal, context, "CloseDatabase",
        "request=DATABASE_HANDLE");
    const gx_sql_status status = closeDatabase(context, handle, trace);
    return finishDispatch(trace, status, 0);
}

gx_sql_status runQuery(const DispatchTrace& trace, Principal* principal,
                       uint64_t handle, uint32_t indexIdentity, uint32_t flags,
                       const void* request, size_t requestSize,
                       gx_sql_locator* results, uint32_t resultCapacity,
                       uint32_t* outCount, bool isRange,
                       uint32_t* traceResultCount) {
    if (traceResultCount) *traceResultCount = 0;
    if (!principal || !bufferValid(principal, outCount, sizeof(*outCount), 1)) {
        return GX_SQL_STATUS_INVALID_ARGUMENT;
    }
    *outCount = 0;
    if (!principal->databaseReadGranted) return GX_SQL_STATUS_PERMISSION_DENIED;
    if (flags != GX_SQL_QUERY_ORDERED_LOCATORS) return GX_SQL_STATUS_INVALID_ARGUMENT;
    if (!request || !bufferValid(principal, request, requestSize, 0)) {
        return GX_SQL_STATUS_INVALID_ARGUMENT;
    }
    if (resultCapacity == 0 || resultCapacity > GX_SQL_MAX_RESULTS || !results) {
        return GX_SQL_STATUS_CAPACITY_EXCEEDED;
    }
    const size_t resultsBytes = static_cast<size_t>(resultCapacity) * sizeof(*results);
    if (!bufferValid(principal, results, resultsBytes, 1) ||
        spansOverlap(results, resultsBytes, outCount, sizeof(*outCount)) ||
        spansOverlap(results, resultsBytes, request, requestSize) ||
        spansOverlap(outCount, sizeof(*outCount), request, requestSize)) {
        return GX_SQL_STATUS_INVALID_ARGUMENT;
    }
    gx_sql_status failure = GX_SQL_STATUS_INVALID_HANDLE;
    DatabaseSession* session = findSession(handle, principal->ownerId, &failure);
    if (!session) return failure;
    const ApprovedDatabase* database =
        findApprovedDatabase(session->databaseIdentity);
    if (!database) return GX_SQL_STATUS_INVALID_DATABASE;

    const GxNativeSqlIndexCatalogEntry* indexMetadata = nullptr;
    const size_t supportedIndexCount = gxNativeSqlGetSupportedIndexCount();
    for (size_t i = 0; i < supportedIndexCount; ++i) {
        const GxNativeSqlIndexCatalogEntry* candidate =
            gxNativeSqlGetSupportedIndexAt(i);
        if (candidate && candidate->identity == indexIdentity) {
            indexMetadata = candidate;
            break;
        }
    }
    if (!indexMetadata) return GX_SQL_STATUS_INVALID_INDEX;
    const uint32_t requestedCapability = isRange
        ? GX_NATIVE_SQL_INDEX_CAP_RANGE : GX_NATIVE_SQL_INDEX_CAP_POINT;
    if ((indexMetadata->capabilities & requestedCapability) == 0) {
        return GX_SQL_STATUS_UNSUPPORTED;
    }

    GxNativeSqlReadRequest sqlRequest = {};
    sqlRequest.fixture = database->fixture;
    sqlRequest.consistency = GX_NATIVE_SQL_CONSISTENCY_IMMUTABLE_IMAGE;
    sqlRequest.index = static_cast<GxNativeSqlReadIndex>(indexIdentity);
    if (!gxNativeSqlGetReadIndexIdentity(sqlRequest.index,
            &sqlRequest.catalogRootPageId, &sqlRequest.rootPageId,
            &sqlRequest.ownerIndexId)) {
        return GX_SQL_STATUS_INVALID_INDEX;
    }
    // The production reader consumes these synchronously below, so keep every
    // encoded span alive until gxNativeSqlExecuteRead returns.
    uint8_t encodedKey[9] = {};
    size_t encodedKeyLength = 0;
    uint8_t encodedLower[5] = {};
    uint8_t encodedUpper[5] = {};
    size_t encodedLowerLength = 0;
    size_t encodedUpperLength = 0;
    if (!isRange) {
        const gx_sql_key& key = *static_cast<const gx_sql_key*>(request);
        if (!encodeKey(key, indexMetadata->keyType, encodedKey, sizeof(encodedKey),
                       &encodedKeyLength)) {
            return GX_SQL_STATUS_INVALID_ARGUMENT;
        }
        sqlRequest.operation = GX_NATIVE_SQL_READ_POINT;
        sqlRequest.keyBytes = encodedKey;
        sqlRequest.keyLength = encodedKeyLength;
    } else {
        const gx_sql_range& range = *static_cast<const gx_sql_range*>(request);
        if (range.has_lower > 1u || range.has_upper > 1u ||
            range.lower_inclusive > 1u || range.upper_inclusive > 1u) {
            return GX_SQL_STATUS_INVALID_ARGUMENT;
        }
        if ((!range.has_lower && (range.lower_inclusive || range.lower.type ||
                                  range.lower.is_null)) ||
            (!range.has_upper && (range.upper_inclusive || range.upper.type ||
                                  range.upper.is_null))) {
            return GX_SQL_STATUS_INVALID_ARGUMENT;
        }
        if (range.has_lower && !validKey(range.lower, indexMetadata->keyType)) {
            return GX_SQL_STATUS_INVALID_ARGUMENT;
        }
        if (range.has_upper && !validKey(range.upper, indexMetadata->keyType)) {
            return GX_SQL_STATUS_INVALID_ARGUMENT;
        }
        if (range.has_lower && range.has_upper) {
            bool validBounds = false;
            if (compareKeys(range.lower, range.upper, indexMetadata->keyType,
                            &validBounds) > 0 || !validBounds) {
                return GX_SQL_STATUS_INVALID_ARGUMENT;
            }
        }
        if ((range.has_lower && !encodeKey(range.lower, indexMetadata->keyType,
                                            encodedLower, sizeof(encodedLower),
                                            &encodedLowerLength)) ||
            (range.has_upper && !encodeKey(range.upper, indexMetadata->keyType,
                                            encodedUpper, sizeof(encodedUpper),
                                            &encodedUpperLength))) {
            return GX_SQL_STATUS_INVALID_ARGUMENT;
        }
        sqlRequest.operation = GX_NATIVE_SQL_READ_RANGE;
        sqlRequest.hasLower = static_cast<uint8_t>(range.has_lower);
        sqlRequest.lowerKeyBytes = range.has_lower ? encodedLower : nullptr;
        sqlRequest.lowerKeyLength = encodedLowerLength;
        sqlRequest.lowerInclusive = static_cast<uint8_t>(range.lower_inclusive);
        sqlRequest.hasUpper = static_cast<uint8_t>(range.has_upper);
        sqlRequest.upperKeyBytes = range.has_upper ? encodedUpper : nullptr;
        sqlRequest.upperKeyLength = encodedUpperLength;
        sqlRequest.upperInclusive = static_cast<uint8_t>(range.upper_inclusive);
    }

    GxNativeSqlReadLocator staged[GX_NATIVE_SQL_READ_MAX_RESULTS] = {};
    GxNativeSqlReadResponse response = {};
    const char* queryTarget = isRange
        ? "SQL_EXECUTE_READ range=INT32 index=ROWS_K32"
        : indexMetadata->keyType == GX_NATIVE_SQL_KEY_BOOLEAN
            ? "SQL_EXECUTE_READ key=BOOLEAN index=ROWS_KBOOL"
            : indexMetadata->keyType == GX_NATIVE_SQL_KEY_TEXT
                ? "SQL_EXECUTE_READ key=TEXT index=ROWS_KTEXT"
                : indexMetadata->keyType == GX_NATIVE_SQL_KEY_INT32
                    ? "SQL_EXECUTE_READ key=INT32 index=ROWS_EDGE32"
                    : indexMetadata->identity == GX_SQL_INDEX_ROWS_EDGE64
                        ? "SQL_EXECUTE_READ key=INT64 index=ROWS_EDGE64"
                        : "SQL_EXECUTE_READ key=INT64 index=ROWS_K64";
    dispatchService(trace, queryTarget);
    const GxNativeSqlReadStatus status = gxNativeSqlExecuteRead(
        &sqlRequest, staged, GX_NATIVE_SQL_READ_MAX_RESULTS, &response);
    const gx_sql_status publicStatus = mapReadStatus(status);
    if (publicStatus != GX_SQL_STATUS_OK) return publicStatus;
    if (response.rowCount > resultCapacity || response.rowCount > GX_SQL_MAX_RESULTS) {
        return GX_SQL_STATUS_CAPACITY_EXCEEDED;
    }
    for (size_t i = 0; i < response.rowCount; ++i) {
        results[i].page_id = staged[i].pageId;
        results[i].slot = staged[i].slot;
        results[i].reserved = 0;
    }
    *outCount = static_cast<uint32_t>(response.rowCount);
    if (traceResultCount) *traceResultCount = static_cast<uint32_t>(response.rowCount);
    return GX_SQL_STATUS_OK;
}

gx_sql_status GX_CALL hostLookupIndex(
    gx_app_context* context, gx_sql_database_handle handle,
    uint32_t indexIdentity, const gx_sql_key* key, uint32_t flags,
    gx_sql_locator* results, uint32_t resultCapacity, uint32_t* outCount) {
    Principal* principal = findPrincipal(context);
    const DispatchTrace trace = beginDispatch(principal, context, "LookupIndex",
        "request=POINT");
    uint32_t resultCount = 0;
    const gx_sql_status status = runQuery(trace, principal, handle, indexIdentity,
        flags, key, sizeof(gx_sql_key), results, resultCapacity, outCount, false,
        &resultCount);
    return finishDispatch(trace, status, resultCount);
}

gx_sql_status GX_CALL hostScanIndexRange(
    gx_app_context* context, gx_sql_database_handle handle,
    uint32_t indexIdentity, const gx_sql_range* range, uint32_t flags,
    gx_sql_locator* results, uint32_t resultCapacity, uint32_t* outCount) {
    Principal* principal = findPrincipal(context);
    const DispatchTrace trace = beginDispatch(principal, context, "ScanIndexRange",
        "request=RANGE");
    uint32_t resultCount = 0;
    const gx_sql_status status = runQuery(trace, principal, handle, indexIdentity,
        flags, range, sizeof(gx_sql_range), results, resultCapacity, outCount,
        true, &resultCount);
    return finishDispatch(trace, status, resultCount);
}

gx_sql_status catalogQuery(Principal* principal,
    const gx_sql_catalog_request* request, gx_sql_catalog_entry* entries,
    uint32_t entryCapacity, gx_sql_catalog_result* outResult,
    uint32_t* traceResultCount) {
    if (traceResultCount) *traceResultCount = 0;
    if (!principal || !bufferValid(principal, request, sizeof(uint32_t), 0) ||
        !bufferValid(principal, outResult, sizeof(uint32_t), 1)) {
        return GX_SQL_STATUS_INVALID_ARGUMENT;
    }
    if (request->size != sizeof(*request) ||
        !bufferValid(principal, request, request->size, 0) ||
        outResult->size != sizeof(*outResult) ||
        !bufferValid(principal, outResult, outResult->size, 1)) {
        return GX_SQL_STATUS_INVALID_ARGUMENT;
    }
    if (entryCapacity > GX_SQL_CATALOG_MAX_ENTRIES) {
        return GX_SQL_STATUS_INVALID_ARGUMENT;
    }
    const size_t entriesBytes =
        static_cast<size_t>(entryCapacity) * sizeof(gx_sql_catalog_entry);
    if ((entriesBytes != 0 &&
         (!bufferValid(principal, entries, entriesBytes, 1) ||
          spansOverlap(entries, entriesBytes, request, sizeof(*request)) ||
          spansOverlap(entries, entriesBytes, outResult, sizeof(*outResult)))) ||
        spansOverlap(request, sizeof(*request), outResult, sizeof(*outResult))) {
        return GX_SQL_STATUS_INVALID_ARGUMENT;
    }
    gx_sql_catalog_result stagedResult = {};
    stagedResult.size = sizeof(stagedResult);
    *outResult = stagedResult;
    if (!principal->databaseReadGranted) return GX_SQL_STATUS_PERMISSION_DENIED;
    if (request->maxEntries == 0 ||
        request->maxEntries > GX_SQL_CATALOG_MAX_ENTRIES) {
        return GX_SQL_STATUS_INVALID_ARGUMENT;
    }

    gx_sql_catalog_entry stagedEntries[GX_SQL_CATALOG_MAX_ENTRIES] = {};
    const ApprovedDatabase* database = nullptr;
    uint32_t totalEntries = 0;

    if (request->query == GX_SQL_CATALOG_QUERY_DATABASES) {
        if (request->databaseHandle != 0) {
            *outResult = stagedResult;
            return GX_SQL_STATUS_INVALID_ARGUMENT;
        }
        totalEntries = static_cast<uint32_t>(kApprovedDatabaseCount);
        if (request->startIndex > totalEntries) {
            stagedResult.totalEntries = totalEntries;
            stagedResult.nextIndex = request->startIndex;
            *outResult = stagedResult;
            return GX_SQL_STATUS_INVALID_ARGUMENT;
        }
        if (kApprovedDatabaseCount != 1 ||
            !fillDatabaseCatalogEntry(kApprovedDatabases[0], &stagedEntries[0])) {
            *outResult = stagedResult;
            return GX_SQL_STATUS_DATABASE_CORRUPT;
        }
        database = &kApprovedDatabases[0];
    } else if (request->query == GX_SQL_CATALOG_QUERY_INDEXES) {
        gx_sql_status failure = GX_SQL_STATUS_INVALID_HANDLE;
        DatabaseSession* session = findSession(request->databaseHandle,
            principal->ownerId, &failure);
        if (!session) {
            *outResult = stagedResult;
            return failure;
        }
        database = findApprovedDatabase(session->databaseIdentity);
        if (!database) {
            *outResult = stagedResult;
            return GX_SQL_STATUS_INVALID_DATABASE;
        }
        const size_t supportedCount = gxNativeSqlGetSupportedIndexCount();
        totalEntries = static_cast<uint32_t>(supportedCount);
        uint32_t previousIdentity = 0;
        for (uint32_t i = 0; i < totalEntries; ++i) {
            const GxNativeSqlIndexCatalogEntry* metadata =
                gxNativeSqlGetSupportedIndexAt(i);
            if (!metadata || metadata->identity <= previousIdentity) {
                *outResult = stagedResult;
                return GX_SQL_STATUS_DATABASE_CORRUPT;
            }
            previousIdentity = metadata->identity;
        }
        if (request->startIndex > totalEntries) {
            stagedResult.totalEntries = totalEntries;
            stagedResult.nextIndex = request->startIndex;
            *outResult = stagedResult;
            return GX_SQL_STATUS_INVALID_ARGUMENT;
        }
    } else {
        *outResult = stagedResult;
        return GX_SQL_STATUS_INVALID_ARGUMENT;
    }

    const uint32_t remaining = totalEntries - request->startIndex;
    const uint32_t chunkCount = request->maxEntries < remaining
        ? request->maxEntries : remaining;
    stagedResult.totalEntries = totalEntries;
    stagedResult.nextIndex = request->startIndex;
    if (entryCapacity < chunkCount) {
        stagedResult.requiredCapacity = chunkCount;
        *outResult = stagedResult;
        return GX_SQL_STATUS_CAPACITY_EXCEEDED;
    }

    GxNativeSqlImageError imageError = GX_NATIVE_SQL_IMAGE_OK;
    GxNativeSqlImageError cleanupError = GX_NATIVE_SQL_IMAGE_OK;
    const GxNativeSqlReadStatus validation = gxNativeSqlValidateCatalog(
        database->fixture, &imageError, &cleanupError);
    const gx_sql_status validationStatus = mapCatalogValidation(
        validation, imageError, cleanupError);
    if (validationStatus != GX_SQL_STATUS_OK) {
        *outResult = stagedResult;
        return validationStatus;
    }

    for (uint32_t i = 0; i < chunkCount; ++i) {
        const uint32_t sourceIndex = request->startIndex + i;
        const GxNativeSqlIndexCatalogEntry* metadata =
            request->query == GX_SQL_CATALOG_QUERY_INDEXES
                ? gxNativeSqlGetSupportedIndexAt(sourceIndex) : nullptr;
        const bool filled = request->query == GX_SQL_CATALOG_QUERY_DATABASES
            ? (sourceIndex == 0 && fillDatabaseCatalogEntry(
                kApprovedDatabases[0], &stagedEntries[i]))
            : (metadata && fillIndexCatalogEntry(*metadata, &stagedEntries[i]));
        if (!filled) {
            *outResult = stagedResult;
            return GX_SQL_STATUS_DATABASE_CORRUPT;
        }
    }

    for (uint32_t i = 0; i < chunkCount; ++i) entries[i] = stagedEntries[i];
    stagedResult.entryCount = chunkCount;
    stagedResult.nextIndex = request->startIndex + chunkCount;
    if (traceResultCount) *traceResultCount = chunkCount;
    *outResult = stagedResult;
    return GX_SQL_STATUS_OK;
}

gx_sql_status GX_CALL hostCatalogQuery(
    gx_app_context* context, const gx_sql_catalog_request* request,
    gx_sql_catalog_entry* entries, uint32_t entryCapacity,
    gx_sql_catalog_result* outResult) {
    Principal* principal = findPrincipal(context);
    const DispatchTrace trace = beginDispatch(principal, context,
        "CatalogQuery", "request=BOUNDED_CATALOG");
    uint32_t resultCount = 0;
    const gx_sql_status status = catalogQuery(principal, request, entries,
        entryCapacity, outResult, &resultCount);
    return finishDispatch(trace, status, resultCount);
}

} // namespace

bool attach(gx_app_context* context, uint64_t ownerId,
            uint8_t databaseReadGranted, BufferValidator validator) {
    if (!context || ownerId == 0 || databaseReadGranted > 1u || !validator ||
        findPrincipal(context)) return false;
    for (uint32_t i = 0; i < kMaxPrincipals; ++i) {
        if (g_principals[i].context) {
            if (g_principals[i].ownerId == ownerId) return false;
            continue;
        }
        g_principals[i].context = context;
        g_principals[i].ownerId = ownerId;
        g_principals[i].databaseReadGranted = databaseReadGranted;
        g_principals[i].validateBuffer = validator;
        return true;
    }
    return false;
}

DetachResult detach(gx_app_context* context) {
    DetachResult result = {};
    Principal* principal = findPrincipal(context);
    if (!principal) return result;
#if defined(GX_NATIVE_SQL_READ_QUALIFICATION) && GX_NATIVE_SQL_READ_QUALIFICATION && \
    defined(GX_NATIVE_SQL_READ_DIAGNOSTIC) && GX_NATIVE_SQL_READ_DIAGNOSTIC
    ++g_dispatchSequence;
    if (g_dispatchSequence == 0) ++g_dispatchSequence;
    const uint32_t sequence = g_dispatchSequence;
    const uint64_t ownerId = principal->ownerId;
    const uint32_t ownerSessionsBefore = ownerSessionCount(ownerId);
    char beginDetails[72] = {};
    size_t beginLength = 0;
    appendText(beginDetails, sizeof(beginDetails), &beginLength,
        "owner_sessions_before=");
    appendUnsigned(beginDetails, sizeof(beginDetails), &beginLength,
        ownerSessionsBefore, 0);
    emitTrace(principal->context, sequence, ownerId,
        "SQL_APP_LIFECYCLE", "DETACH_BEGIN", nullptr, beginDetails);
#else
    const uint64_t ownerId = principal->ownerId;
#endif
    result.principalFound = 1;
    result.sessionsBefore = ownerSessionCount(ownerId);
    for (uint32_t i = 0; i < kMaxHandles; ++i) {
        DatabaseSession& session = g_sessions[i];
        if (session.active && session.ownerId == ownerId) {
            result.lastReleasedHandle = makeHandle(i, session.generation);
            session.active = 0;
            session.ownerId = 0;
            session.databaseIdentity = 0;
            ++result.sessionsReleased;
        }
    }
    result.sessionsAfter = ownerSessionCount(ownerId);
    result.success = result.sessionsBefore == result.sessionsReleased &&
        result.sessionsAfter == 0;
    principal->context = nullptr;
    principal->ownerId = 0;
    principal->databaseReadGranted = 0;
    principal->validateBuffer = nullptr;
#if defined(GX_NATIVE_SQL_READ_QUALIFICATION) && GX_NATIVE_SQL_READ_QUALIFICATION && \
    defined(GX_NATIVE_SQL_READ_DIAGNOSTIC) && GX_NATIVE_SQL_READ_DIAGNOSTIC
    char endDetails[128] = {};
    size_t endLength = 0;
    appendText(endDetails, sizeof(endDetails), &endLength, "cleanup=PRODUCTION");
    appendText(endDetails, sizeof(endDetails), &endLength, " sessions_released=");
    appendUnsigned(endDetails, sizeof(endDetails), &endLength,
        result.sessionsReleased, 0);
    appendText(endDetails, sizeof(endDetails), &endLength, " owner_sessions_after=");
    appendUnsigned(endDetails, sizeof(endDetails), &endLength,
        result.sessionsAfter, 0);
    appendText(endDetails, sizeof(endDetails), &endLength,
        " provider_refs_retained=0 status=");
    appendText(endDetails, sizeof(endDetails), &endLength,
        result.success ? "PASS" : "FAIL");
    emitTrace(context, sequence, ownerId, "SQL_APP_LIFECYCLE",
        "DETACH_END", nullptr, endDetails);
#endif
    return result;
}

void installHostCalls(gx_host_calls* host) {
    if (!host) return;
    host->sql_open_read_only_database = hostOpenReadOnlyDatabase;
    host->sql_close_database = hostCloseDatabase;
    host->sql_lookup_index = hostLookupIndex;
    host->sql_scan_index_range = hostScanIndexRange;
    host->sql_catalog_query = hostCatalogQuery;
}

} // namespace native_sql_application
} // namespace kernel

#else

namespace kernel {
namespace native_sql_application {
bool attach(gx_app_context*, uint64_t, uint8_t, BufferValidator) { return false; }
DetachResult detach(gx_app_context*) { return {}; }
void installHostCalls(gx_host_calls*) {}
} // namespace native_sql_application
} // namespace kernel

#endif
