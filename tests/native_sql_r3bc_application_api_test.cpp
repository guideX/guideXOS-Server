#define main native_sql_r3aq_original_test_main
#include "native_sql_r3aq_query_service_test.cpp"
#undef main

#include "kernel/native_sql_application_service.h"
#include "../sdk/samples/sqlread/main.cpp"

#include <cstdint>
#include <cstring>

namespace {

uintptr_t g_bufferBegin = 0;
uintptr_t g_bufferEnd = 0;
std::vector<std::string> g_appLog;

gx_result GX_CALL testAppLog(gx_app_context*, const char* message) {
    g_appLog.push_back(message ? message : "");
    std::cout << "APP: " << (message ? message : "") << "\n";
    return GX_OK;
}

uint32_t GX_CALL testGetApiVersion(gx_app_context*) {
    return GX_API_VERSION;
}

int testBufferValidator(gx_app_context*, const void* address, size_t length,
                        uint8_t) {
    if (!address || length == 0) return 0;
    const uintptr_t begin = reinterpret_cast<uintptr_t>(address);
    if (length > static_cast<size_t>(UINTPTR_MAX - begin)) return 0;
    const uintptr_t end = begin + length;
    return begin >= g_bufferBegin && end <= g_bufferEnd;
}

void initHostContext(gx_host_calls& host, gx_app_context& context) {
    std::memset(&host, 0, sizeof(host));
    host.size = sizeof(host);
    host.version = GX_API_VERSION;
    host.log = testAppLog;
    host.get_api_version = testGetApiVersion;
    kernel::native_sql_application::installHostCalls(&host);
    context.size = sizeof(context);
    context.apiVersion = GX_API_VERSION;
    context.host = &host;
    context.userData = nullptr;
}

uint8_t findBlockDeviceByName(const char* name) {
    if (!name) return 0xFF;
    for (uint8_t index = 0; index < kernel::block::MAX_BLOCK_DEVICES; ++index) {
        const kernel::block::BlockDevice* device = kernel::block::get_device(index);
        if (device && std::strcmp(device->name, name) == 0) return index;
    }
    return 0xFF;
}

bool logsContain(const char* expected) {
    for (const std::string& entry : g_appLog) {
        if (entry == expected) return true;
    }
    return false;
}

bool logsContainFragment(const char* fragment) {
    for (const std::string& entry : g_appLog) {
        if (entry.find(fragment) != std::string::npos) return true;
    }
    return false;
}

bool logsContainInOrder(const char* const* fragments, size_t count) {
    size_t next = 0;
    for (const std::string& entry : g_appLog) {
        if (entry.find(fragments[next]) != std::string::npos && ++next == count) {
            return true;
        }
    }
    return false;
}

bool allRowsStillSentinel(const gx_sql_locator* rows, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        if (rows[i].page_id != 0xEE000000ull + i ||
            rows[i].slot != 0xDD00u + i || rows[i].reserved != 0xCC00u + i) {
            return false;
        }
    }
    return true;
}

void fillApiSentinel(gx_sql_locator* rows, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        rows[i].page_id = 0xEE000000ull + i;
        rows[i].slot = 0xDD00u + i;
        rows[i].reserved = 0xCC00u + i;
    }
}

int runApplicationApiTests(const char* sourceFixturePath,
                           const std::vector<uint8_t>& candidateImage,
                           const std::vector<uint8_t>& missingImage,
                           uint8_t candidateDevice) {
    int32_t stackAnchor = 0;
    const uintptr_t anchor = reinterpret_cast<uintptr_t>(&stackAnchor);
    g_bufferBegin = anchor > 4u * 1024u * 1024u ? anchor - 4u * 1024u * 1024u : 0;
    g_bufferEnd = anchor < UINTPTR_MAX - 4u * 1024u * 1024u
        ? anchor + 4u * 1024u * 1024u : UINTPTR_MAX;

    gx_host_calls host = {};
    gx_app_context context = {};
    initHostContext(host, context);
    check(kernel::native_sql_application::attach(&context, 0x101u, 1u,
              testBufferValidator),
          "R3BC SDK App Model context attaches with database.read permission");
    gx_host_calls duplicateHost = {};
    gx_app_context duplicateContext = {};
    initHostContext(duplicateHost, duplicateContext);
    check(!kernel::native_sql_application::attach(&duplicateContext, 0x101u, 1u,
              testBufferValidator),
          "R3BC duplicate runtime owner identity cannot alias another context");

    g_appLog.clear();
    const gx_result appResult = gx_main(&context);
#if defined(GX_SQLREAD_LIFECYCLE_QUALIFICATION) && GX_SQLREAD_LIFECYCLE_QUALIFICATION
    check(appResult == GX_OK &&
          logsContain("SQLReadDemo: returning normally with one session outstanding") &&
          !logsContain("SQLReadDemo: completed all application-facing checks"),
          "R3BE SDK entry returns normally with exactly one deliberate session outstanding");
#else
    check(appResult == GX_OK &&
          logsContain("SQLReadDemo: completed all application-facing checks"),
          "R3BC SDK application entry invokes the public host API and checks live SQL results");
#endif
    check(logsContain("SQLReadDemo: discovered approved database from service catalog") &&
          logsContain("SQLReadDemo: paginated catalog discovered all six indexes in identity order") &&
          logsContain("SQLReadDemo: Edge32 signed lookups match all 80 independent locators, both extrema, zero, and internal separators") &&
          logsContain("SQLReadDemo: Edge32 positive key 40 remains absent") &&
          logsContain("SQLReadDemo: Edge64 matches all 80 independent signed key and locator pairs") &&
          logsContain("SQLReadDemo: Edge64 absent minimum, interior, and maximum keys return no match") &&
          logsContain("SQLReadDemo: Edge64 rejects Int32 keys without changing rows") &&
          logsContain("SQLReadDemo: Edge64 insufficient capacity publishes no locator") &&
          logsContain("SQLReadDemo: Edge64 range scanning remains unsupported") &&
          logsContain("SQLReadDemo: Edge64 lookup recovers after rejected requests") &&
          logsContain("SQLReadDemo: Boolean point lookup matches all 34 independently decoded locators") &&
          logsContain("SQLReadDemo: Boolean false lookup matches all 34 ordered locators") &&
          logsContain("SQLReadDemo: Boolean NULL lookup matches all 12 NULL-first locators") &&
          logsContain("SQLReadDemo: Boolean point capacity failure publishes no locators") &&
          logsContain("SQLReadDemo: wrong Boolean key type is rejected without result mutation") &&
          logsContain("SQLReadDemo: malformed Boolean value is rejected without result mutation") &&
          logsContain("SQLReadDemo: Boolean range operation remains unsupported") &&
          logsContain("SQLReadDemo: Boolean lookup succeeds after rejected requests") &&
          logsContain("SQLReadDemo: Text NULL lookup matches all 8 independent locators") &&
          logsContain("SQLReadDemo: embedded-NUL Text lookup matches all 24 independent locators") &&
          logsContain("SQLReadDemo: other Text lookup matches all 48 independent locators") &&
          logsContain("SQLReadDemo: Text no-match leaves locator storage unchanged") &&
          logsContain("SQLReadDemo: wrong Text key type is rejected without result mutation") &&
          logsContain("SQLReadDemo: Text length beyond inline capacity is rejected safely") &&
          logsContain("SQLReadDemo: Text capacity below 48 publishes no locators") &&
          logsContain("SQLReadDemo: Text range scan remains unsupported") &&
          logsContain("SQLReadDemo: Text lookup succeeds after rejected requests"),
          "SDK caller selects database and supported indexes from production catalog results");
    gx_sql_catalog_request databaseCatalogRequest = {};
    databaseCatalogRequest.size = sizeof(databaseCatalogRequest);
    databaseCatalogRequest.query = GX_SQL_CATALOG_QUERY_DATABASES;
    databaseCatalogRequest.maxEntries = 1;
    gx_sql_catalog_entry databaseCatalogEntry = {};
    gx_sql_catalog_result databaseCatalogResult = {};
    databaseCatalogResult.size = sizeof(databaseCatalogResult);
    check(host.sql_catalog_query(&context, &databaseCatalogRequest,
              &databaseCatalogEntry, 1, &databaseCatalogResult) ==
              GX_SQL_STATUS_OK && databaseCatalogResult.entryCount == 1 &&
          databaseCatalogResult.totalEntries == 1 &&
          databaseCatalogEntry.identity == GX_SQL_DATABASE_R3AH_MULTILEVEL &&
          std::memchr(databaseCatalogEntry.name, '\0',
                      sizeof(databaseCatalogEntry.name)) != nullptr &&
          std::strcmp(databaseCatalogEntry.name,
                      "NDB1R4P11R3AH_MULTILEVEL") == 0 &&
          (databaseCatalogEntry.capabilities & GX_SQL_CATALOG_CAP_READ_ONLY) &&
          (databaseCatalogEntry.capabilities &
              GX_SQL_CATALOG_CAP_IMMUTABLE_PACKAGE),
          "R3BI approved database identity and bounded NUL-terminated name are returned by the service");
    gx_host_calls oldCatalogHost = {};
    gx_app_context oldCatalogContext = {};
    oldCatalogHost.size = static_cast<uint32_t>(
        offsetof(gx_host_calls, sql_catalog_query));
    oldCatalogContext.size = sizeof(oldCatalogContext);
    oldCatalogContext.apiVersion = GX_API_VERSION;
    oldCatalogContext.host = &oldCatalogHost;
    gx_sql_catalog_request oldCatalogRequest = {};
    oldCatalogRequest.size = sizeof(oldCatalogRequest);
    oldCatalogRequest.query = GX_SQL_CATALOG_QUERY_DATABASES;
    oldCatalogRequest.maxEntries = 1;
    gx_sql_catalog_result oldCatalogResult = {};
    oldCatalogResult.size = sizeof(oldCatalogResult);
    check(gx_sql_query_catalog(&oldCatalogContext, &oldCatalogRequest, nullptr,
              0, &oldCatalogResult) == GX_SQL_STATUS_UNSUPPORTED,
          "SDK extension probe detects a 288-byte legacy host without reading past its prefix");
    check(allVfsHandlesClosed(),
          "SDK application queries close every production VFS provider handle");

#if defined(GX_SQLREAD_LIFECYCLE_QUALIFICATION) && GX_SQLREAD_LIFECYCLE_QUALIFICATION && \
    defined(GX_NATIVE_SQL_READ_QUALIFICATION) && GX_NATIVE_SQL_READ_QUALIFICATION && \
    defined(GX_NATIVE_SQL_READ_DIAGNOSTIC) && GX_NATIVE_SQL_READ_DIAGNOSTIC
    const char* callbackOrder[] = {
        "event=RETURN op=OpenReadOnlyDatabase status=OK count=0",
        "event=RETURN op=LookupIndex status=CAPACITY_EXCEEDED count=0",
        "event=SERVICE op=LookupIndex target=SQL_EXECUTE_READ key=INT64 index=ROWS_K64",
        "event=RETURN op=LookupIndex status=OK count=73",
        "event=SERVICE op=ScanIndexRange target=SQL_EXECUTE_READ range=INT32 index=ROWS_K32",
        "event=RETURN op=ScanIndexRange status=OK count=32",
        "event=RETURN op=ScanIndexRange status=NO_MATCH count=0",
        "event=RETURN op=LookupIndex status=NO_MATCH count=0",
        "event=RETURN op=LookupIndex status=INVALID_ARGUMENT count=0",
        "event=RETURN op=LookupIndex status=OK count=73",
        "event=RETURN op=CloseDatabase status=OK count=0",
        "event=RETURN op=LookupIndex status=INVALID_HANDLE count=0",
        "event=RETURN op=OpenReadOnlyDatabase status=OK count=0"
    };
    check(logsContainInOrder(callbackOrder,
              sizeof(callbackOrder) / sizeof(callbackOrder[0])),
          "R3BE kernel SQL callback trace preserves request order, real service results, and returns");
    check(logsContainFragment("SQL_APP_API seq=") &&
          logsContainFragment("runtime=0x101 event=SERVICE op=OpenReadOnlyDatabase target=SQL_VALIDATE_DATABASE") &&
          logsContainFragment("runtime=0x101 event=SERVICE op=LookupIndex target=SQL_EXECUTE_READ key=INT64 index=ROWS_K64") &&
          logsContainFragment("runtime=0x101 event=SERVICE op=ScanIndexRange target=SQL_EXECUTE_READ range=INT32 index=ROWS_K32"),
          "R3BE callback trace correlates the Native ELF runtime with production SQL service calls");

    gx_sql_locator lifecycleRows[GX_SQL_MAX_RESULTS] = {};
    gx_sql_key lifecycleKey = {};
    lifecycleKey.type = GX_SQL_KEY_INT64;
    lifecycleKey.value.int64_value = 7;
    uint32_t lifecycleCount = 0xFFFFFFFFu;
    // This hosted harness calls the production detach API after gx_main returns.
    // The bare-metal loader's return-to-detach caller is source-audited separately;
    // this test does not execute the desktop or Native ELF loader.
    const kernel::native_sql_application::DetachResult firstExit =
        kernel::native_sql_application::detach(&context);
    check(firstExit.principalFound && firstExit.success &&
          firstExit.sessionsBefore == 1 && firstExit.sessionsReleased == 1 &&
          firstExit.sessionsAfter == 0 && firstExit.lastReleasedHandle != 0 &&
          logsContainFragment("SQL_APP_LIFECYCLE") &&
          logsContainFragment("event=DETACH_BEGIN owner_sessions_before=1") &&
          logsContainFragment("event=DETACH_END cleanup=PRODUCTION sessions_released=1 owner_sessions_after=0 provider_refs_retained=0 status=PASS"),
          "R3BE hosted return followed by production detach reclaims the outstanding session");
    check(kernel::native_sql_application::attach(&context, 0x101u, 1u,
              testBufferValidator) &&
          host.sql_lookup_index(&context, firstExit.lastReleasedHandle,
              GX_SQL_INDEX_ROWS_K64, &lifecycleKey,
              GX_SQL_QUERY_ORDERED_LOCATORS, lifecycleRows,
              GX_SQL_MAX_RESULTS, &lifecycleCount) == GX_SQL_STATUS_INVALID_HANDLE &&
          lifecycleCount == 0,
          "R3BE detached handle generation remains invalid after owner reattachment");

    gx_host_calls repeatedHost = {};
    gx_app_context repeatedContext = {};
    initHostContext(repeatedHost, repeatedContext);
    for (uint32_t cycle = 0; cycle < 3; ++cycle) {
        const uint64_t ownerId = 0xC001u + cycle;
        check(kernel::native_sql_application::attach(&repeatedContext, ownerId, 1u,
                  testBufferValidator),
              "R3BE repeated launch receives a fresh runtime owner");
        g_appLog.clear();
        const gx_result repeatedResult = gx_main(&repeatedContext);
        const kernel::native_sql_application::DetachResult repeatedExit =
            kernel::native_sql_application::detach(&repeatedContext);
        check(repeatedResult == GX_OK && repeatedExit.principalFound &&
              repeatedExit.success && repeatedExit.sessionsBefore == 1 &&
              repeatedExit.sessionsReleased == 1 && repeatedExit.sessionsAfter == 0 &&
              logsContain("SQLReadDemo: returning normally with one session outstanding"),
              "R3BE repeated hosted returns followed by production detach reclaim the owned session");
        check(kernel::native_sql_application::attach(&repeatedContext,
                  ownerId + 0x100u, 1u, testBufferValidator) &&
              repeatedHost.sql_lookup_index(&repeatedContext,
                  repeatedExit.lastReleasedHandle, GX_SQL_INDEX_ROWS_K64,
                  &lifecycleKey, GX_SQL_QUERY_ORDERED_LOCATORS,
                  lifecycleRows, GX_SQL_MAX_RESULTS, &lifecycleCount) ==
                  GX_SQL_STATUS_INVALID_HANDLE && lifecycleCount == 0,
              "R3BE repeated teardown does not resurrect a released handle generation");
        (void)kernel::native_sql_application::detach(&repeatedContext);
    }

    gx_host_calls missingHost = {};
    gx_app_context missingContext = {};
    initHostContext(missingHost, missingContext);
    missingHost.sql_scan_index_range = nullptr;
    check(kernel::native_sql_application::attach(&missingContext, 0xD001u, 1u,
              testBufferValidator),
          "R3BE missing-callback negative case attaches a real service owner");
    g_appLog.clear();
    check(gx_main(&missingContext) == GX_ERROR_UNSUPPORTED &&
          logsContain("SQLReadDemo: public host ABI negotiation failed") &&
          !logsContain("SQLReadDemo: returning normally with one session outstanding"),
          "R3BE missing mandatory callback cannot produce an application PASS");
    const kernel::native_sql_application::DetachResult missingExit =
        kernel::native_sql_application::detach(&missingContext);
    check(missingExit.success && missingExit.sessionsBefore == 0 &&
          missingExit.sessionsReleased == 0 && missingExit.sessionsAfter == 0,
          "R3BE failed application entry detaches without inventing an SQL session");
#endif

    gx_sql_database_handle opened = 0xDEADBEEFull;
    check(host.sql_open_read_only_database(&context, 99u,
              GX_SQL_OPEN_READ_ONLY, &opened) == GX_SQL_STATUS_INVALID_DATABASE &&
          opened == 0,
          "R3BC invalid database identity is rejected before session allocation");
    check(host.sql_open_read_only_database(&context,
              GX_SQL_DATABASE_R3AH_MULTILEVEL, 0u, &opened) ==
              GX_SQL_STATUS_INVALID_ARGUMENT && opened == 0,
          "R3BC open requires the explicit read-only operation flag");

    gx_sql_database_handle handle = 0;
    gx_sql_database_handle secondHandle = 0;
    check(host.sql_open_read_only_database(&context,
              GX_SQL_DATABASE_R3AH_MULTILEVEL, GX_SQL_OPEN_READ_ONLY,
              &handle) == GX_SQL_STATUS_OK && handle != 0,
          "R3BC open validates the approved fixture through the production reader");
    check(host.sql_open_read_only_database(&context,
              GX_SQL_DATABASE_R3AH_MULTILEVEL, GX_SQL_OPEN_READ_ONLY,
              &secondHandle) == GX_SQL_STATUS_OK && secondHandle != handle,
          "R3BC multiple independent application database sessions are bounded and distinct");

    gx_sql_catalog_request catalogRequest = {};
    catalogRequest.size = sizeof(catalogRequest);
    catalogRequest.query = GX_SQL_CATALOG_QUERY_INDEXES;
    catalogRequest.databaseHandle = handle;
    catalogRequest.maxEntries = 2;
    gx_sql_catalog_entry catalogEntries[GX_SQL_CATALOG_MAX_ENTRIES];
    std::memset(catalogEntries, 0xA5, sizeof(catalogEntries));
    gx_sql_catalog_entry catalogEntriesBefore[GX_SQL_CATALOG_MAX_ENTRIES];
    std::memcpy(catalogEntriesBefore, catalogEntries, sizeof(catalogEntries));
    gx_sql_catalog_result catalogResult = {};
    catalogResult.size = sizeof(catalogResult);
    check(host.sql_catalog_query(&context, &catalogRequest, catalogEntries, 1,
              &catalogResult) == GX_SQL_STATUS_CAPACITY_EXCEEDED &&
          catalogResult.entryCount == 0 && catalogResult.totalEntries == 6 &&
          catalogResult.nextIndex == 0 && catalogResult.requiredCapacity == 2 &&
          std::memcmp(catalogEntries, catalogEntriesBefore,
                      sizeof(catalogEntries)) == 0,
          "R3BI insufficient catalog capacity reports the required chunk without partial entries");

    catalogRequest.startIndex = 0;
    catalogRequest.maxEntries = GX_SQL_CATALOG_MAX_ENTRIES;
    catalogResult = {};
    catalogResult.size = sizeof(catalogResult);
    check(host.sql_catalog_query(&context, &catalogRequest, catalogEntries,
              GX_SQL_CATALOG_MAX_ENTRIES, &catalogResult) == GX_SQL_STATUS_OK &&
          catalogResult.entryCount == 2 && catalogResult.totalEntries == 6 &&
          catalogResult.nextIndex == 2 &&
          catalogEntries[0].identity == GX_SQL_INDEX_ROWS_K64 &&
          catalogEntries[1].identity == GX_SQL_INDEX_ROWS_K32,
          "R3BK first catalog page returns two entries and advances by two");
    catalogRequest.startIndex = 2;
    catalogResult = {};
    catalogResult.size = sizeof(catalogResult);
    check(host.sql_catalog_query(&context, &catalogRequest, catalogEntries,
              GX_SQL_CATALOG_MAX_ENTRIES, &catalogResult) == GX_SQL_STATUS_OK &&
          catalogResult.entryCount == 2 && catalogResult.totalEntries == 6 &&
          catalogResult.nextIndex == 4 &&
          catalogEntries[0].identity == GX_SQL_INDEX_ROWS_KBOOL &&
          catalogEntries[1].identity == GX_SQL_INDEX_ROWS_KTEXT,
          "R3BL second catalog page returns KBool and binary Text");
    catalogRequest.startIndex = 4;
    catalogResult = {};
    catalogResult.size = sizeof(catalogResult);
    check(host.sql_catalog_query(&context, &catalogRequest, catalogEntries,
              GX_SQL_CATALOG_MAX_ENTRIES, &catalogResult) == GX_SQL_STATUS_OK &&
          catalogResult.entryCount == 2 && catalogResult.totalEntries == 6 &&
          catalogResult.nextIndex == 6 &&
          catalogEntries[0].identity == GX_SQL_INDEX_ROWS_EDGE64 &&
          catalogEntries[1].identity == GX_SQL_INDEX_ROWS_EDGE32,
          "R3BM third catalog page returns Edge64 and Edge32 exactly once");

    catalogRequest.startIndex = 0;
    catalogRequest.maxEntries = 1;
    catalogResult = {};
    catalogResult.size = sizeof(catalogResult);
    check(host.sql_catalog_query(&context, &catalogRequest, catalogEntries, 1,
              &catalogResult) == GX_SQL_STATUS_OK &&
          catalogResult.entryCount == 1 && catalogResult.totalEntries == 6 &&
          catalogResult.nextIndex == 1 &&
          catalogEntries[0].identity == GX_SQL_INDEX_ROWS_K64 &&
          std::memchr(catalogEntries[0].name, '\0',
                      sizeof(catalogEntries[0].name)) != nullptr &&
          std::strcmp(catalogEntries[0].name, "IX_Rows_K64") == 0 &&
          catalogEntries[0].keyType == GX_SQL_KEY_INT64 &&
          (catalogEntries[0].capabilities & GX_SQL_CATALOG_CAP_POINT_LOOKUP) &&
          !(catalogEntries[0].capabilities & GX_SQL_CATALOG_CAP_RANGE_SCAN) &&
          catalogEntries[0].comparisonSemantics ==
              GX_SQL_COMPARISON_SIGNED_ASCENDING_NULLS_FIRST &&
          catalogEntries[0].maximumResults == GX_SQL_MAX_RESULTS,
          "R3BI first chunk reports validated Int64 point-index metadata");
    const gx_sql_catalog_entry firstCatalogEntry = catalogEntries[0];

    catalogRequest.startIndex = 1;
    catalogResult = {};
    catalogResult.size = sizeof(catalogResult);
    check(host.sql_catalog_query(&context, &catalogRequest, catalogEntries, 1,
              &catalogResult) == GX_SQL_STATUS_OK &&
          catalogResult.entryCount == 1 && catalogResult.totalEntries == 6 &&
          catalogResult.nextIndex == 2 &&
          catalogEntries[0].identity == GX_SQL_INDEX_ROWS_K32 &&
          std::memchr(catalogEntries[0].name, '\0',
                      sizeof(catalogEntries[0].name)) != nullptr &&
          std::strcmp(catalogEntries[0].name, "IX_Rows_K32") == 0 &&
          catalogEntries[0].keyType == GX_SQL_KEY_INT32 &&
          (catalogEntries[0].capabilities & GX_SQL_CATALOG_CAP_RANGE_SCAN) &&
          !(catalogEntries[0].capabilities & GX_SQL_CATALOG_CAP_POINT_LOOKUP) &&
          catalogEntries[0].maximumResults == GX_SQL_MAX_RESULTS,
          "R3BI second stable chunk reports the Int32 range-index metadata");

    catalogRequest.startIndex = 2;
    catalogResult = {};
    catalogResult.size = sizeof(catalogResult);
    check(host.sql_catalog_query(&context, &catalogRequest, catalogEntries, 1,
              &catalogResult) == GX_SQL_STATUS_OK &&
          catalogResult.entryCount == 1 && catalogResult.totalEntries == 6 &&
          catalogResult.nextIndex == 3 &&
          catalogEntries[0].identity == GX_SQL_INDEX_ROWS_KBOOL &&
          std::memchr(catalogEntries[0].name, '\0',
                      sizeof(catalogEntries[0].name)) != nullptr &&
          std::strcmp(catalogEntries[0].name, "IX_Rows_KBool") == 0 &&
          catalogEntries[0].keyType == GX_SQL_KEY_BOOLEAN &&
          (catalogEntries[0].capabilities & GX_SQL_CATALOG_CAP_POINT_LOOKUP) &&
          !(catalogEntries[0].capabilities & GX_SQL_CATALOG_CAP_RANGE_SCAN) &&
          catalogEntries[0].comparisonSemantics ==
              GX_SQL_COMPARISON_BOOLEAN_ASCENDING_NULLS_FIRST &&
          catalogEntries[0].maximumResults == GX_SQL_MAX_RESULTS,
          "R3BJ third stable chunk reports Boolean point-index metadata");

    catalogRequest.startIndex = 3;
    catalogResult = {};
    catalogResult.size = sizeof(catalogResult);
    check(host.sql_catalog_query(&context, &catalogRequest, catalogEntries, 1,
              &catalogResult) == GX_SQL_STATUS_OK &&
          catalogResult.entryCount == 1 && catalogResult.totalEntries == 6 &&
          catalogResult.nextIndex == 4 &&
          catalogEntries[0].identity == GX_SQL_INDEX_ROWS_KTEXT &&
          std::memchr(catalogEntries[0].name, '\0',
                      sizeof(catalogEntries[0].name)) != nullptr &&
          std::strcmp(catalogEntries[0].name, "IX_Rows_KText") == 0 &&
          catalogEntries[0].keyType == GX_SQL_KEY_TEXT &&
          (catalogEntries[0].capabilities & GX_SQL_CATALOG_CAP_POINT_LOOKUP) &&
          !(catalogEntries[0].capabilities & GX_SQL_CATALOG_CAP_RANGE_SCAN) &&
          catalogEntries[0].comparisonSemantics ==
              GX_SQL_COMPARISON_TEXT_BYTES_ASCENDING_NULLS_FIRST &&
          catalogEntries[0].maximumResults == GX_SQL_MAX_RESULTS,
          "R3BK fourth stable chunk reports binary Text point-index metadata");

    catalogRequest.startIndex = 4;
    catalogResult = {};
    catalogResult.size = sizeof(catalogResult);
    check(host.sql_catalog_query(&context, &catalogRequest, catalogEntries, 1,
              &catalogResult) == GX_SQL_STATUS_OK &&
          catalogResult.entryCount == 1 && catalogResult.totalEntries == 6 &&
          catalogResult.nextIndex == 5 &&
          catalogEntries[0].identity == GX_SQL_INDEX_ROWS_EDGE64 &&
          std::memchr(catalogEntries[0].name, '\0',
                      sizeof(catalogEntries[0].name)) != nullptr &&
          std::strcmp(catalogEntries[0].name, "IX_Rows_Edge64") == 0 &&
          catalogEntries[0].keyType == GX_SQL_KEY_INT64 &&
          (catalogEntries[0].capabilities & GX_SQL_CATALOG_CAP_POINT_LOOKUP) &&
          !(catalogEntries[0].capabilities & GX_SQL_CATALOG_CAP_RANGE_SCAN) &&
          catalogEntries[0].comparisonSemantics ==
              GX_SQL_COMPARISON_SIGNED_ASCENDING_NO_NULLS &&
          catalogEntries[0].maximumResults == GX_SQL_MAX_RESULTS,
          "R3BM fifth catalog entry reports Edge64 point-index metadata");

    catalogRequest.startIndex = 5;
    catalogResult = {};
    catalogResult.size = sizeof(catalogResult);
    check(host.sql_catalog_query(&context, &catalogRequest, catalogEntries, 1,
              &catalogResult) == GX_SQL_STATUS_OK &&
          catalogResult.entryCount == 1 && catalogResult.totalEntries == 6 &&
          catalogResult.nextIndex == 6 &&
          catalogEntries[0].identity == GX_SQL_INDEX_ROWS_EDGE32 &&
          std::memchr(catalogEntries[0].name, '\0',
                      sizeof(catalogEntries[0].name)) != nullptr &&
          std::strcmp(catalogEntries[0].name, "IX_Rows_Edge32") == 0 &&
          catalogEntries[0].keyType == GX_SQL_KEY_INT32 &&
          (catalogEntries[0].capabilities & GX_SQL_CATALOG_CAP_POINT_LOOKUP) &&
          !(catalogEntries[0].capabilities & GX_SQL_CATALOG_CAP_RANGE_SCAN) &&
          catalogEntries[0].comparisonSemantics ==
              GX_SQL_COMPARISON_SIGNED_ASCENDING_NULLS_FIRST &&
          catalogEntries[0].maximumResults == GX_SQL_MAX_RESULTS,
          "R3BM sixth catalog entry reports Edge32 point-index metadata");

    catalogRequest.startIndex = 0;
    catalogResult = {};
    catalogResult.size = sizeof(catalogResult);
    check(host.sql_catalog_query(&context, &catalogRequest, catalogEntries, 1,
              &catalogResult) == GX_SQL_STATUS_OK &&
          catalogResult.entryCount == 1 && catalogResult.nextIndex == 1 &&
          catalogEntries[0].identity == firstCatalogEntry.identity &&
          std::strcmp(catalogEntries[0].name, firstCatalogEntry.name) == 0 &&
          catalogEntries[0].keyType == firstCatalogEntry.keyType &&
          catalogEntries[0].capabilities == firstCatalogEntry.capabilities,
          "R3BI repeated enumeration returns the same stable catalog entry");

    gx_sql_catalog_request malformedCatalogRequest = catalogRequest;
    malformedCatalogRequest.size -= 1;
    catalogResult = {};
    catalogResult.size = sizeof(catalogResult);
    check(host.sql_catalog_query(&context, &malformedCatalogRequest,
              catalogEntries, 1, &catalogResult) == GX_SQL_STATUS_INVALID_ARGUMENT,
          "R3BI catalog request structure size is validated");
    malformedCatalogRequest = catalogRequest;
    malformedCatalogRequest.query = 99;
    check(host.sql_catalog_query(&context, &malformedCatalogRequest,
              catalogEntries, 1, &catalogResult) == GX_SQL_STATUS_INVALID_ARGUMENT,
          "R3BI malformed catalog query identifiers are rejected");
    malformedCatalogRequest = catalogRequest;
    malformedCatalogRequest.startIndex = 7;
    check(host.sql_catalog_query(&context, &malformedCatalogRequest,
              catalogEntries, 1, &catalogResult) == GX_SQL_STATUS_INVALID_ARGUMENT,
          "R3BI catalog offsets past the stable entry count are rejected");
    gx_sql_catalog_result wrongSizeResult = {};
    wrongSizeResult.size = sizeof(wrongSizeResult) - 1;
    check(host.sql_catalog_query(&context, &catalogRequest, catalogEntries, 1,
              &wrongSizeResult) == GX_SQL_STATUS_INVALID_ARGUMENT,
          "R3BI catalog result structure size is validated");
    check(host.sql_catalog_query(&context, &catalogRequest, catalogEntries, 3,
              &catalogResult) == GX_SQL_STATUS_INVALID_ARGUMENT,
          "R3BI catalog entry capacity is bounded before multiplication");

    struct PartialRequestBuffers {
        gx_sql_catalog_result result;
        gx_sql_catalog_request request;
    } partialRequestBuffers = {};
    partialRequestBuffers.result.size = sizeof(partialRequestBuffers.result);
    partialRequestBuffers.request.size = sizeof(partialRequestBuffers.request);
    partialRequestBuffers.request.query = GX_SQL_CATALOG_QUERY_DATABASES;
    partialRequestBuffers.request.maxEntries = 1;
    const uintptr_t savedBufferBegin = g_bufferBegin;
    const uintptr_t savedBufferEnd = g_bufferEnd;
    g_bufferBegin = reinterpret_cast<uintptr_t>(&partialRequestBuffers.result);
    g_bufferEnd = reinterpret_cast<uintptr_t>(&partialRequestBuffers.request) +
        sizeof(uint32_t);
    const gx_sql_status shortRequestSpanStatus = host.sql_catalog_query(&context,
        &partialRequestBuffers.request, nullptr, 0,
        &partialRequestBuffers.result);
    g_bufferBegin = savedBufferBegin;
    g_bufferEnd = savedBufferEnd;
    check(shortRequestSpanStatus == GX_SQL_STATUS_INVALID_ARGUMENT,
          "R3BI request header is not enough when the full sized request span is unavailable");

    struct PartialResultBuffers {
        gx_sql_catalog_request request;
        gx_sql_catalog_result result;
    } partialResultBuffers = {};
    partialResultBuffers.request.size = sizeof(partialResultBuffers.request);
    partialResultBuffers.request.query = GX_SQL_CATALOG_QUERY_DATABASES;
    partialResultBuffers.request.maxEntries = 1;
    partialResultBuffers.result.size = sizeof(partialResultBuffers.result);
    g_bufferBegin = reinterpret_cast<uintptr_t>(&partialResultBuffers.request);
    g_bufferEnd = reinterpret_cast<uintptr_t>(&partialResultBuffers.result) +
        sizeof(uint32_t);
    const gx_sql_status shortResultSpanStatus = host.sql_catalog_query(&context,
        &partialResultBuffers.request, nullptr, 0,
        &partialResultBuffers.result);
    g_bufferBegin = savedBufferBegin;
    g_bufferEnd = savedBufferEnd;
    check(shortResultSpanStatus == GX_SQL_STATUS_INVALID_ARGUMENT,
          "R3BI result size header is not enough when the full output span is unavailable");

    gx_sql_catalog_entry* wrappingCatalogEntries =
        reinterpret_cast<gx_sql_catalog_entry*>(UINTPTR_MAX - 7u);
    check(host.sql_catalog_query(&context, &catalogRequest,
              wrappingCatalogEntries, 1, &catalogResult) ==
              GX_SQL_STATUS_INVALID_ARGUMENT,
          "R3BI checked pointer arithmetic rejects a wrapping catalog-entry span");

    gx_sql_locator rows[GX_SQL_MAX_RESULTS];
    uint32_t count = 0xFFFFFFFFu;
    gx_sql_key point = {};
    point.type = GX_SQL_KEY_INT64;
    point.value.int64_value = 7;
    const gx_sql_status fullPointStatus = host.sql_lookup_index(&context, handle,
        GX_SQL_INDEX_ROWS_K64, &point, GX_SQL_QUERY_ORDERED_LOCATORS, rows,
        GX_SQL_MAX_RESULTS, &count);
    check(fullPointStatus == GX_SQL_STATUS_OK && count == 73 &&
          rows[0].page_id == 3 && rows[0].slot == 1 &&
          rows[count - 1].page_id == 479 && rows[count - 1].slot == 0,
          "R3BC full-capacity public point lookup returns actual locators");
    fillApiSentinel(rows, GX_SQL_MAX_RESULTS);
    check(host.sql_lookup_index(&context, handle, GX_SQL_INDEX_ROWS_K64,
              &point, GX_SQL_QUERY_ORDERED_LOCATORS, rows, 1, &count) ==
              GX_SQL_STATUS_CAPACITY_EXCEEDED && count == 0 &&
          allRowsStillSentinel(rows, GX_SQL_MAX_RESULTS),
          "R3BC insufficient caller capacity reports exhaustion with no partial rows");

    gx_sql_key nullKey = {};
    nullKey.type = GX_SQL_KEY_INT64;
    nullKey.is_null = 1;
    check(host.sql_lookup_index(&context, handle, GX_SQL_INDEX_ROWS_K64,
              &nullKey, GX_SQL_QUERY_ORDERED_LOCATORS, rows,
              GX_SQL_MAX_RESULTS, &count) == GX_SQL_STATUS_OK && count == 7,
          "R3BC a different valid typed point key returns its actual matches");

    gx_sql_range singleton = {};
    singleton.has_lower = 1;
    singleton.lower_inclusive = 1;
    singleton.lower.type = GX_SQL_KEY_INT32;
    singleton.lower.value.int32_value = 1;
    singleton.has_upper = 1;
    singleton.upper_inclusive = 1;
    singleton.upper.type = GX_SQL_KEY_INT32;
    singleton.upper.value.int32_value = 1;
    check(host.sql_scan_index_range(&context, handle, GX_SQL_INDEX_ROWS_K32,
              &singleton, GX_SQL_QUERY_ORDERED_LOCATORS, rows,
              GX_SQL_MAX_RESULTS, &count) == GX_SQL_STATUS_OK && count == 16,
          "R3BC caller-selected singleton range bound returns actual ordered rows");

    gx_sql_range reversed = {};
    reversed.has_lower = 1;
    reversed.lower_inclusive = 1;
    reversed.lower.type = GX_SQL_KEY_INT32;
    reversed.lower.value.int32_value = 2;
    reversed.has_upper = 1;
    reversed.upper_inclusive = 1;
    reversed.upper.type = GX_SQL_KEY_INT32;
    reversed.upper.value.int32_value = 1;
    fillApiSentinel(rows, GX_SQL_MAX_RESULTS);
    check(host.sql_scan_index_range(&context, handle, GX_SQL_INDEX_ROWS_K32,
              &reversed, GX_SQL_QUERY_ORDERED_LOCATORS, rows,
              GX_SQL_MAX_RESULTS, &count) == GX_SQL_STATUS_INVALID_ARGUMENT &&
          count == 0 && allRowsStillSentinel(rows, GX_SQL_MAX_RESULTS),
          "R3BC reversed range bounds are rejected without output mutation");

    gx_sql_locator* badRows = reinterpret_cast<gx_sql_locator*>(UINTPTR_MAX - 7u);
    check(host.sql_lookup_index(&context, handle, GX_SQL_INDEX_ROWS_K64,
              &point, GX_SQL_QUERY_ORDERED_LOCATORS, badRows, 1, &count) ==
              GX_SQL_STATUS_INVALID_ARGUMENT && count == 0,
          "R3BC checked pointer arithmetic rejects a wrapping result span");

    gx_host_calls deniedHost = {};
    gx_app_context deniedContext = {};
    initHostContext(deniedHost, deniedContext);
    check(kernel::native_sql_application::attach(&deniedContext, 0x202u, 0u,
              testBufferValidator),
          "R3BC context without database.read attaches without fabricated identity");
    gx_sql_database_handle deniedHandle = 0xFFFFFFFFu;
    check(deniedHost.sql_open_read_only_database(&deniedContext,
              GX_SQL_DATABASE_R3AH_MULTILEVEL, GX_SQL_OPEN_READ_ONLY,
              &deniedHandle) == GX_SQL_STATUS_PERMISSION_DENIED && deniedHandle == 0,
          "R3BC manifest permission denial is enforced at database open");
    check(deniedHost.sql_lookup_index(&deniedContext, handle,
              GX_SQL_INDEX_ROWS_K64, &point, GX_SQL_QUERY_ORDERED_LOCATORS,
              rows, GX_SQL_MAX_RESULTS, &count) == GX_SQL_STATUS_PERMISSION_DENIED,
          "R3BC foreign owner cannot use another application's handle");
#if defined(GX_NATIVE_SQL_READ_QUALIFICATION) && GX_NATIVE_SQL_READ_QUALIFICATION && \
    defined(GX_NATIVE_SQL_READ_DIAGNOSTIC) && GX_NATIVE_SQL_READ_DIAGNOSTIC
    check(logsContainFragment("runtime=0x202 event=RETURN op=LookupIndex status=PERMISSION_DENIED count=0"),
          "R3BE invalid owner is visible in the production callback return trace");
#endif

    gx_host_calls otherHost = {};
    gx_app_context otherContext = {};
    initHostContext(otherHost, otherContext);
    check(kernel::native_sql_application::attach(&otherContext, 0x303u, 1u,
              testBufferValidator),
          "R3BC second authorized application context attaches");
    check(otherHost.sql_close_database(&otherContext, handle) ==
              GX_SQL_STATUS_PERMISSION_DENIED,
          "R3BC cross-owner close is rejected");
    gx_sql_catalog_request foreignCatalogRequest = {};
    foreignCatalogRequest.size = sizeof(foreignCatalogRequest);
    foreignCatalogRequest.query = GX_SQL_CATALOG_QUERY_INDEXES;
    foreignCatalogRequest.databaseHandle = handle;
    foreignCatalogRequest.maxEntries = 1;
    gx_sql_catalog_entry foreignCatalogEntry = {};
    gx_sql_catalog_result foreignCatalogResult = {};
    foreignCatalogResult.size = sizeof(foreignCatalogResult);
    check(otherHost.sql_catalog_query(&otherContext, &foreignCatalogRequest,
              &foreignCatalogEntry, 1, &foreignCatalogResult) ==
              GX_SQL_STATUS_PERMISSION_DENIED &&
          foreignCatalogResult.entryCount == 0,
          "R3BI catalog enumeration enforces database-session owner association");
#if defined(GX_NATIVE_SQL_READ_QUALIFICATION) && GX_NATIVE_SQL_READ_QUALIFICATION && \
    defined(GX_NATIVE_SQL_READ_DIAGNOSTIC) && GX_NATIVE_SQL_READ_DIAGNOSTIC
    check(logsContainFragment("runtime=0x303 event=RETURN op=CloseDatabase status=PERMISSION_DENIED count=0"),
          "R3BE cross-owner close failure is visible in the production callback trace");
#endif
    kernel::native_sql_application::detach(&otherContext);
    kernel::native_sql_application::detach(&deniedContext);

    gx_sql_catalog_request catalogFaultRequest = {};
    catalogFaultRequest.size = sizeof(catalogFaultRequest);
    catalogFaultRequest.query = GX_SQL_CATALOG_QUERY_DATABASES;
    catalogFaultRequest.maxEntries = 1;
    gx_sql_catalog_entry catalogFaultEntry;
    std::memset(&catalogFaultEntry, 0xA5, sizeof(catalogFaultEntry));
    gx_sql_catalog_entry catalogFaultBefore = catalogFaultEntry;
    gx_sql_catalog_result catalogFaultResult = {};
    catalogFaultResult.size = sizeof(catalogFaultResult);
    gx_db_file_bind_ops(&kFaultOps);
    failNextRead = true;
    const gx_sql_status catalogReadFailure = host.sql_catalog_query(&context,
        &catalogFaultRequest, &catalogFaultEntry, 1, &catalogFaultResult);
    gx_db_file_bind_ops(nullptr);
    check(catalogReadFailure == GX_SQL_STATUS_DATABASE_UNAVAILABLE &&
          catalogFaultResult.entryCount == 0 &&
          std::memcmp(&catalogFaultEntry, &catalogFaultBefore,
                      sizeof(catalogFaultEntry)) == 0 && allVfsHandlesClosed(),
          "R3BI failed production VFS reads report DATABASE_UNAVAILABLE without publishing catalog entries");

    gx_db_file_bind_ops(&kFaultOps);
    failNextClose = true;
    catalogFaultResult = {};
    catalogFaultResult.size = sizeof(catalogFaultResult);
    const gx_sql_status catalogCloseFailure = host.sql_catalog_query(&context,
        &catalogFaultRequest, &catalogFaultEntry, 1, &catalogFaultResult);
    gx_db_file_bind_ops(nullptr);
    check(catalogCloseFailure == GX_SQL_STATUS_DATABASE_UNAVAILABLE &&
          catalogFaultResult.entryCount == 0 &&
          std::memcmp(&catalogFaultEntry, &catalogFaultBefore,
                      sizeof(catalogFaultEntry)) == 0 && allVfsHandlesClosed(),
          "R3BI provider close failure is explicit and leaves catalog storage unchanged");

    gx_db_file_bind_ops(&kFaultOps);
    failNextRead = true;
    fillApiSentinel(rows, GX_SQL_MAX_RESULTS);
    const gx_sql_status readFailure = host.sql_lookup_index(&context, handle,
        GX_SQL_INDEX_ROWS_K64, &point, GX_SQL_QUERY_ORDERED_LOCATORS, rows,
        GX_SQL_MAX_RESULTS, &count);
    gx_db_file_bind_ops(nullptr);
    check(readFailure == GX_SQL_STATUS_STORAGE_FAILURE && count == 0 &&
          allRowsStillSentinel(rows, GX_SQL_MAX_RESULTS) && allVfsHandlesClosed(),
          "R3BC production VFS read failure propagates and publishes no rows");
    check(host.sql_lookup_index(&context, handle, GX_SQL_INDEX_ROWS_K64,
              &point, GX_SQL_QUERY_ORDERED_LOCATORS, rows,
              GX_SQL_MAX_RESULTS, &count) == GX_SQL_STATUS_OK && count == 73,
          "R3BC subsequent query succeeds after provider read failure");

    gx_db_file_bind_ops(&kFaultOps);
    failNextClose = true;
    gx_sql_database_handle failedOpen = 0xFFFFFFFFu;
    const gx_sql_status closeFailure = host.sql_open_read_only_database(&context,
        GX_SQL_DATABASE_R3AH_MULTILEVEL, GX_SQL_OPEN_READ_ONLY, &failedOpen);
    gx_db_file_bind_ops(nullptr);
    check(closeFailure == GX_SQL_STATUS_STORAGE_FAILURE && failedOpen == 0 &&
          allVfsHandlesClosed(),
          "R3BC open validation close failure leaves no app or VFS handle");

    gx_host_calls partialHost = {};
    gx_app_context partialContext = {};
    initHostContext(partialHost, partialContext);
    check(kernel::native_sql_application::attach(&partialContext, 0x505u, 1u,
              testBufferValidator),
          "R3BE partial-open cleanup case attaches a distinct runtime owner");
    gx_db_file_bind_ops(&kFaultOps);
    failNextClose = true;
    gx_sql_database_handle partialHandle = 0xFFFFFFFFu;
    const gx_sql_status partialOpenStatus = partialHost.sql_open_read_only_database(
        &partialContext, GX_SQL_DATABASE_R3AH_MULTILEVEL, GX_SQL_OPEN_READ_ONLY,
        &partialHandle);
    gx_db_file_bind_ops(nullptr);
    const kernel::native_sql_application::DetachResult partialCleanup =
        kernel::native_sql_application::detach(&partialContext);
    check(partialOpenStatus == GX_SQL_STATUS_STORAGE_FAILURE && partialHandle == 0 &&
          partialCleanup.success && partialCleanup.sessionsBefore == 0 &&
          partialCleanup.sessionsReleased == 0 && partialCleanup.sessionsAfter == 0 &&
          allVfsHandlesClosed(),
          "R3BE detach after partially failed open has no session or provider reference to reclaim");

    for (uint32_t i = 0; i < 20; ++i) {
        gx_sql_database_handle loopHandle = 0;
        const bool cycle = host.sql_open_read_only_database(&context,
            GX_SQL_DATABASE_R3AH_MULTILEVEL, GX_SQL_OPEN_READ_ONLY,
            &loopHandle) == GX_SQL_STATUS_OK && loopHandle != 0 &&
            host.sql_close_database(&context, loopHandle) == GX_SQL_STATUS_OK;
        if (!cycle) break;
        if (i == 19) check(true, "R3BC repeated open/close cycles release every bounded session");
    }

    check(host.sql_close_database(&context, handle) == GX_SQL_STATUS_OK,
          "R3BC explicit close consumes a database session");
    check(host.sql_close_database(&context, handle) == GX_SQL_STATUS_INVALID_HANDLE,
          "R3BC explicit close invalidates the old handle generation");
    catalogRequest.databaseHandle = handle;
    catalogResult = {};
    catalogResult.size = sizeof(catalogResult);
    check(host.sql_catalog_query(&context, &catalogRequest, catalogEntries, 1,
              &catalogResult) == GX_SQL_STATUS_INVALID_HANDLE &&
          catalogResult.entryCount == 0,
          "R3BI stale database handle cannot enumerate catalog entries");
    check(host.sql_close_database(&context, secondHandle) == GX_SQL_STATUS_OK,
          "R3BC second database session closes independently");

    gx_sql_database_handle bounded[16] = {};
    uint32_t openedCount = 0;
    for (; openedCount < 16; ++openedCount) {
        if (host.sql_open_read_only_database(&context,
                GX_SQL_DATABASE_R3AH_MULTILEVEL, GX_SQL_OPEN_READ_ONLY,
                &bounded[openedCount]) != GX_SQL_STATUS_OK) break;
    }
    gx_sql_database_handle overflowHandle = 123;
    const gx_sql_status overflowStatus = host.sql_open_read_only_database(&context,
        GX_SQL_DATABASE_R3AH_MULTILEVEL, GX_SQL_OPEN_READ_ONLY, &overflowHandle);
    check(openedCount == 16 && overflowStatus == GX_SQL_STATUS_RESOURCE_EXHAUSTED &&
          overflowHandle == 0,
          "R3BC application database handle count has a hard bound");
#if defined(GX_NATIVE_SQL_READ_QUALIFICATION) && GX_NATIVE_SQL_READ_QUALIFICATION && \
    defined(GX_NATIVE_SQL_READ_DIAGNOSTIC) && GX_NATIVE_SQL_READ_DIAGNOSTIC
    check(logsContainFragment("runtime=0x101 event=RETURN op=OpenReadOnlyDatabase status=RESOURCE_EXHAUSTED count=0"),
          "R3BE session-limit rejection is visible in the production callback trace");
#endif
    for (uint32_t i = 0; i < openedCount; ++i) {
        (void)host.sql_close_database(&context, bounded[i]);
    }

    gx_sql_database_handle exitHandle = 0;
    check(host.sql_open_read_only_database(&context,
              GX_SQL_DATABASE_R3AH_MULTILEVEL, GX_SQL_OPEN_READ_ONLY,
              &exitHandle) == GX_SQL_STATUS_OK,
          "R3BC app-exit cleanup case opens a live session");
    const kernel::native_sql_application::DetachResult manualCleanup =
        kernel::native_sql_application::detach(&context);
    check(manualCleanup.success && manualCleanup.sessionsBefore == 1 &&
          manualCleanup.sessionsReleased == 1 && manualCleanup.sessionsAfter == 0,
          "R3BC direct production detach cleanup releases the live session");
    check(kernel::native_sql_application::attach(&context, 0x404u, 1u,
              testBufferValidator),
          "R3BC app context can be reused after its previous lifetime ends");
    check(host.sql_close_database(&context, exitHandle) == GX_SQL_STATUS_INVALID_HANDLE,
          "R3BC app-exit cleanup invalidates every owned handle");
    check(kernel::native_sql_application::detach(&context).success,
          "R3BC reattached owner detaches cleanly after stale-handle validation");

#if defined(GX_SQLREAD_LIFECYCLE_QUALIFICATION) && GX_SQLREAD_LIFECYCLE_QUALIFICATION
    gx_app_context unregisteredContext = {};
    g_appLog.clear();
    const kernel::native_sql_application::DetachResult unexpectedCleanup =
        kernel::native_sql_application::detach(&unregisteredContext);
    check(!unexpectedCleanup.principalFound && !unexpectedCleanup.success &&
          unexpectedCleanup.sessionsReleased == 0 &&
          !logsContainFragment("SQL_APP_LIFECYCLE"),
          "R3BE unexpected detach failure cannot emit lifecycle PASS evidence");

    gx_host_calls failureHost = {};
    gx_app_context failureContext = {};
    initHostContext(failureHost, failureContext);
    check(kernel::native_sql_application::attach(&failureContext, 0xD002u, 1u,
              testBufferValidator),
          "R3BE callback-failure case attaches a production service owner");
    g_appLog.clear();
    gx_db_file_bind_ops(&kFaultOps);
    failNextRead = true;
    const gx_result failedAppResult = gx_main(&failureContext);
    gx_db_file_bind_ops(nullptr);
    const kernel::native_sql_application::DetachResult failedAppCleanup =
        kernel::native_sql_application::detach(&failureContext);
    check(failedAppResult != GX_OK &&
          logsContainFragment("event=RETURN op=CatalogQuery status=DATABASE_UNAVAILABLE") &&
          !logsContainFragment("event=RETURN op=OpenReadOnlyDatabase") &&
          !logsContain("SQLReadDemo: returning normally with one session outstanding") &&
          !logsContain("SQLReadDemo: completed all application-facing checks") &&
          failedAppCleanup.success && failedAppCleanup.sessionsBefore == 0 &&
          failedAppCleanup.sessionsAfter == 0 && allVfsHandlesClosed(),
          "R3BE mandatory SQL callback failure cannot be hidden by an application PASS");
#endif

    check(kernel::native_sql_application::attach(&context, 0x405u, 1u,
              testBufferValidator),
          "R3BI provider-failure catalog checks attach a fresh authorized owner");

    std::vector<uint8_t> sourceFixtureBytes;
    std::vector<uint8_t> corruptImage = candidateImage;
    const bool corruptPrepared = readHostFile(sourceFixturePath,
        sourceFixtureBytes) && corruptFixtureRoot(corruptImage, sourceFixtureBytes);
    uint8_t corruptDevice = 0xFF;
    bool corruptMounted = false;
    if (corruptPrepared && kernel::vfs::unmount("/system") == kernel::vfs::VFS_OK) {
        corruptDevice = attachReadonlyVolume(corruptImage, "r3bi-corrupt");
        corruptMounted = corruptDevice != 0xFF &&
            kernel::vfs::mount_type("/system", corruptDevice,
                kernel::vfs::FS_TYPE_FAT32) != 0xFF;
    }
    gx_sql_catalog_request providerRequest = {};
    providerRequest.size = sizeof(providerRequest);
    providerRequest.query = GX_SQL_CATALOG_QUERY_DATABASES;
    providerRequest.maxEntries = 1;
    gx_sql_catalog_entry providerEntry;
    std::memset(&providerEntry, 0xA5, sizeof(providerEntry));
    gx_sql_catalog_entry providerEntryBefore = providerEntry;
    gx_sql_catalog_result providerResult = {};
    providerResult.size = sizeof(providerResult);
    const gx_sql_status corruptCatalogStatus = corruptMounted
        ? host.sql_catalog_query(&context, &providerRequest, &providerEntry, 1,
                                 &providerResult)
        : GX_SQL_STATUS_STORAGE_FAILURE;
    check(corruptPrepared && corruptMounted &&
          corruptCatalogStatus == GX_SQL_STATUS_DATABASE_CORRUPT &&
          providerResult.entryCount == 0 &&
          std::memcmp(&providerEntry, &providerEntryBefore,
                      sizeof(providerEntry)) == 0 && allVfsHandlesClosed(),
          "R3BI corrupted supported index root is reported without partial catalog publication");
    if (corruptMounted) (void)kernel::vfs::unmount("/system");
    if (corruptDevice != 0xFF) kernel::ramdisk::destroy(corruptDevice);
    const bool restoredAfterCorrupt = candidateDevice != 0xFF &&
        kernel::vfs::mount_type("/system", candidateDevice,
            kernel::vfs::FS_TYPE_FAT32) != 0xFF;
    check(restoredAfterCorrupt,
          "R3BI restore the approved read-only package after corruption injection");

    std::vector<uint8_t> corruptCatalogImage = candidateImage;
    const bool corruptCatalogPrepared = restoredAfterCorrupt &&
        corruptFixtureCatalog(corruptCatalogImage, sourceFixtureBytes);
    uint8_t corruptCatalogDevice = 0xFF;
    bool corruptCatalogMounted = false;
    if (corruptCatalogPrepared && kernel::vfs::unmount("/system") ==
            kernel::vfs::VFS_OK) {
        corruptCatalogDevice = attachReadonlyVolume(corruptCatalogImage,
            "r3bi-corrupt-catalog");
        corruptCatalogMounted = corruptCatalogDevice != 0xFF &&
            kernel::vfs::mount_type("/system", corruptCatalogDevice,
                kernel::vfs::FS_TYPE_FAT32) != 0xFF;
    }
    providerEntry = {};
    std::memset(&providerEntry, 0xA5, sizeof(providerEntry));
    providerEntryBefore = providerEntry;
    providerResult = {};
    providerResult.size = sizeof(providerResult);
    const gx_sql_status corruptCatalogPageStatus = corruptCatalogMounted
        ? host.sql_catalog_query(&context, &providerRequest, &providerEntry, 1,
                                 &providerResult)
        : GX_SQL_STATUS_STORAGE_FAILURE;
    check(corruptCatalogPrepared && corruptCatalogMounted &&
          corruptCatalogPageStatus == GX_SQL_STATUS_DATABASE_CORRUPT &&
          providerResult.entryCount == 0 &&
          std::memcmp(&providerEntry, &providerEntryBefore,
                      sizeof(providerEntry)) == 0 && allVfsHandlesClosed(),
          "R3BI a corrupt persisted catalog root is rejected without partial database publication");
    if (corruptCatalogMounted) (void)kernel::vfs::unmount("/system");
    if (corruptCatalogDevice != 0xFF) kernel::ramdisk::destroy(corruptCatalogDevice);
    const bool restoredAfterCatalogCorrupt = candidateDevice != 0xFF &&
        kernel::vfs::mount_type("/system", candidateDevice,
            kernel::vfs::FS_TYPE_FAT32) != 0xFF;
    check(restoredAfterCatalogCorrupt,
          "R3BI restore the approved package after catalog-page corruption injection");

    uint8_t missingDevice = 0xFF;
    uint8_t missingBlockDevice = 0xFF;
    bool missingMounted = false;
    const bool missingUnmounted = kernel::vfs::unmount("/system") ==
        kernel::vfs::VFS_OK;
    if (restoredAfterCatalogCorrupt && missingUnmounted) {
        missingDevice = attachReadonlyVolume(missingImage, "r3bi-missing");
        missingBlockDevice = findBlockDeviceByName("r3bi-missing");
        if (missingDevice != 0xFF && missingBlockDevice != 0xFF) {
            missingMounted = kernel::vfs::mount_type("/system", missingBlockDevice,
                kernel::vfs::FS_TYPE_FAT32) != 0xFF;
        }
    }
    providerEntry = {};
    std::memset(&providerEntry, 0xA5, sizeof(providerEntry));
    providerResult = {};
    providerResult.size = sizeof(providerResult);
    const gx_sql_status missingCatalogStatus = missingMounted
        ? host.sql_catalog_query(&context, &providerRequest, &providerEntry, 1,
                                 &providerResult)
        : GX_SQL_STATUS_STORAGE_FAILURE;
    check(missingMounted && missingCatalogStatus == GX_SQL_STATUS_PACKAGE_INVALID &&
          providerResult.entryCount == 0 && allVfsHandlesClosed(),
          "R3BI missing approved package is distinguished from an unknown database identity");
    if (missingMounted) (void)kernel::vfs::unmount("/system");
    if (missingDevice != 0xFF) kernel::ramdisk::destroy(missingDevice);
    const bool restoredAfterMissing = candidateDevice != 0xFF &&
        kernel::vfs::mount_type("/system", candidateDevice,
            kernel::vfs::FS_TYPE_FAT32) != 0xFF;
    check(restoredAfterMissing,
          "R3BI restore the approved read-only package after missing-package validation");
    check(kernel::native_sql_application::detach(&context).success,
          "R3BI detach catalog-validation owner with no retained sessions");

    std::vector<uint8_t> sourceFixture;
    std::vector<uint8_t> finalFixture;
    check(readHostFile(sourceFixturePath, sourceFixture) &&
          readVfsFile("/system/Apps/R3AI/MULTI.GDB", finalFixture) &&
          finalFixture == sourceFixture,
          "R3BC SDK API preserved packaged fixture bytes after all calls");
    check(allVfsHandlesClosed(),
          "R3BC application API suite exits with no VFS handles owned by SQL");
    return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    std::cout.setf(std::ios::unitbuf);
    if (argc != 4) {
        std::cerr << "usage: native_sql_r3bc_application_api_test <candidate-ramdisk> "
                     "<missing-fixture-ramdisk> <source-fixture>\n";
        return 2;
    }
    // Run the API harness first so its backing image remains alive for the
    // complete app invocation. The retained R3AQ suite then initializes its
    // own independent filesystem state and keeps its historical fixtures.
    std::vector<uint8_t> apiCandidateImage;
    std::vector<uint8_t> apiMissingImage;
    check(readHostFile(argv[1], apiCandidateImage),
          "R3BC read candidate RAM disk for App Model invocation");
    check(readHostFile(argv[2], apiMissingImage),
          "R3BI read missing-fixture RAM disk for package-status qualification");
    if (failures) return 1;
    kernel::block::init();
    kernel::fs_fat::init();
    kernel::ramdisk::init();
    kernel::vfs::init();
    const uint8_t apiDevice = attachReadonlyVolume(apiCandidateImage, "r3bcvalid");
    check(apiDevice != 0xFF && kernel::vfs::mount_type("/system", apiDevice,
              kernel::vfs::FS_TYPE_FAT32) != 0xFF,
          "R3BC mount production read-only fixture for App Model caller");
    if (failures || runApplicationApiTests(argv[3], apiCandidateImage,
            apiMissingImage, apiDevice) != 0) return 1;
    (void)kernel::vfs::unmount("/system");
    kernel::ramdisk::destroy(apiDevice);
    kernel::block::init();
    failures = 0;
    checks = 0;
    return native_sql_r3aq_original_test_main(argc, argv);
}
