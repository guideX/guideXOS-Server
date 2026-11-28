#include <guidexos/app.h>
#include <guidexos/sql.h>
#include <stddef.h>
#include "edge32_oracle.h"
#include "edge64_oracle.h"

static_assert(sizeof(gx_app_context) == 24, "AMD64 app context layout changed");
static_assert(offsetof(gx_host_calls, get_document_activation_path) == 248,
              "existing host-call ordering changed");
static_assert(offsetof(gx_host_calls, sql_open_read_only_database) == 256,
              "SQL open slot moved");
static_assert(offsetof(gx_host_calls, sql_close_database) == 264,
              "SQL close slot moved");
static_assert(offsetof(gx_host_calls, sql_lookup_index) == 272,
              "SQL point slot moved");
static_assert(offsetof(gx_host_calls, sql_scan_index_range) == 280,
              "SQL range slot moved");
static_assert(offsetof(gx_host_calls, sql_catalog_query) == 288,
              "SQL catalog extension slot moved");
static_assert(sizeof(gx_host_calls) == 296, "AMD64 host-call table size changed");
static_assert(sizeof(gx_sql_key) == 16, "gx_sql_key ABI size changed");
static_assert(sizeof(((gx_sql_key*)0)->value.text_value) == 8,
              "inline Text key must fit the existing union slot");
static_assert(offsetof(gx_sql_key, value) == 8,
              "gx_sql_key value offset changed");

namespace {

static void log(gx_app_context* context, const char* message) {
    if (context && context->host && context->host->log) {
        (void)context->host->log(context, message);
    }
}

static bool check(gx_app_context* context, bool passed, const char* name) {
    log(context, passed ? name : "SQLReadDemo: check failed");
    return passed;
}

static bool textEquals(const char* left, const char* right) {
    if (!left || !right) return false;
    size_t i = 0;
    while (i < GX_SQL_CATALOG_NAME_BYTES && left[i] && right[i] &&
           left[i] == right[i]) ++i;
    return i < GX_SQL_CATALOG_NAME_BYTES && left[i] == right[i];
}

static void setCatalogRequest(gx_sql_catalog_request* request,
                              uint32_t query,
                              gx_sql_database_handle database,
                              uint32_t startIndex,
                              uint32_t maxEntries) {
    request->size = sizeof(*request);
    request->query = query;
    request->databaseHandle = database;
    request->startIndex = startIndex;
    request->maxEntries = maxEntries;
}

static void prepareCatalogResult(gx_sql_catalog_result* result) {
    result->size = sizeof(*result);
}

static bool setTextKey(gx_sql_key* key, uint32_t keyType, bool isNull,
                       const uint8_t* bytes, uint8_t length) {
    if (!key || length > GX_SQL_TEXT_INLINE_BYTES || (length && !bytes) ||
        (isNull && length != 0)) return false;
    key->type = keyType;
    key->is_null = isNull ? 1u : 0u;
    key->value.text_value.length = length;
    for (uint8_t i = 0; i < GX_SQL_TEXT_INLINE_BYTES; ++i) {
        key->value.text_value.bytes[i] = i < length ? bytes[i] : 0;
    }
    return true;
}

static bool rowsEqual(const gx_sql_locator* rows, uint32_t count,
                      uint64_t firstPage, uint32_t firstSlot,
                      uint64_t lastPage, uint32_t lastSlot) {
    return rows && count != 0 && rows[0].page_id == firstPage &&
        rows[0].slot == firstSlot && rows[count - 1].page_id == lastPage &&
        rows[count - 1].slot == lastSlot;
}

static bool rowsEqualExpected(const gx_sql_locator* rows, uint32_t count,
                              const gx_sql_locator* expected,
                              uint32_t expectedCount) {
    if (!rows || !expected || count != expectedCount) return false;
    for (uint32_t i = 0; i < count; ++i) {
        if (rows[i].page_id != expected[i].page_id ||
            rows[i].slot != expected[i].slot || rows[i].reserved != 0) {
            return false;
        }
    }
    return true;
}

// Independently decoded from the persisted KBool leaf pages by
// tests/verify_r3ah_persisted_index.py. The service supplies the actual rows.
static const gx_sql_locator kSqlDemoBooleanTrueExpected[34] = {
    {3,2,0},{3,4,0},{3,6,0},{10,0,0},{10,2,0},{10,4,0},
    {22,0,0},{22,2,0},{22,4,0},{22,6,0},{71,0,0},{71,2,0},
    {71,6,0},{130,0,0},{130,2,0},{130,4,0},{130,6,0},{190,0,0},
    {190,4,0},{190,6,0},{250,0,0},{250,2,0},{250,4,0},{250,6,0},
    {308,2,0},{308,4,0},{308,6,0},{360,1,0},{360,3,0},{360,5,0},
    {422,1,0},{422,3,0},{422,5,0},{422,7,0}
};
static const gx_sql_locator kSqlDemoBooleanFalseExpected[34] = {
    {3,1,0},{3,3,0},{3,5,0},{10,1,0},{10,3,0},{10,5,0},{10,7,0},
    {22,1,0},{22,3,0},{22,7,0},{71,1,0},{71,3,0},{71,5,0},{71,7,0},
    {130,1,0},{130,5,0},{130,7,0},{190,1,0},{190,3,0},{190,5,0},
    {190,7,0},{250,3,0},{250,5,0},{250,7,0},{308,1,0},{308,3,0},
    {308,5,0},{360,2,0},{360,4,0},{360,6,0},{422,0,0},{422,2,0},
    {422,4,0},{479,0,0}
};
static const gx_sql_locator kSqlDemoBooleanNullExpected[12] = {
    {3,0,0},{3,7,0},{10,6,0},{22,5,0},{71,4,0},{130,3,0},
    {190,2,0},{250,1,0},{308,0,0},{360,0,0},{360,7,0},{422,6,0}
};
// Independently decoded from KText leaf pages by the Python raw-page verifier.
static const gx_sql_locator kSqlDemoTextNullExpected[8] = {
    {3,0,0},{10,3,0},{22,6,0},{130,1,0},
    {190,4,0},{250,7,0},{360,3,0},{422,6,0}
};
static const gx_sql_locator kSqlDemoTextEmbeddedNulExpected[24] = {
    {3,3,0},{3,6,0},{10,1,0},{10,4,0},
    {10,7,0},{22,2,0},{22,5,0},{71,0,0},
    {71,3,0},{71,6,0},{130,4,0},{130,7,0},
    {190,2,0},{190,5,0},{250,0,0},{250,3,0},
    {250,6,0},{308,1,0},{308,4,0},{360,0,0},
    {360,6,0},{422,1,0},{422,4,0},{422,7,0}
};
static const gx_sql_locator kSqlDemoTextOtherExpected[48] = {
    {3,1,0},{3,2,0},{3,4,0},{3,5,0},
    {3,7,0},{10,0,0},{10,2,0},{10,5,0},
    {10,6,0},{22,0,0},{22,1,0},{22,3,0},
    {22,4,0},{22,7,0},{71,1,0},{71,2,0},
    {71,4,0},{71,5,0},{71,7,0},{130,0,0},
    {130,2,0},{130,3,0},{130,5,0},{130,6,0},
    {190,0,0},{190,1,0},{190,3,0},{190,6,0},
    {190,7,0},{250,1,0},{250,2,0},{250,4,0},
    {250,5,0},{308,0,0},{308,2,0},{308,3,0},
    {308,5,0},{308,6,0},{360,1,0},{360,2,0},
    {360,4,0},{360,5,0},{360,7,0},{422,0,0},
    {422,2,0},{422,3,0},{422,5,0},{479,0,0}
};

static uint64_t locatorSequenceDigest(const gx_sql_locator* rows, uint32_t count) {
    // FNV-1a over each manifest locator in index order, using canonical
    // page_id LE64, slot LE32, and reserved LE16 bytes. Only this scalar
    // fingerprint is embedded; the SDK app never synthesizes locator rows.
    uint64_t digest = 14695981039346656037ull;
    for (uint32_t i = 0; rows && i < count; ++i) {
        uint64_t page = rows[i].page_id;
        uint32_t slot = rows[i].slot;
        uint16_t reserved = rows[i].reserved;
        for (uint32_t b = 0; b < 8; ++b) {
            digest = (digest ^ static_cast<uint8_t>(page & 0xFFu)) * 1099511628211ull;
            page >>= 8;
        }
        for (uint32_t b = 0; b < 4; ++b) {
            digest = (digest ^ static_cast<uint8_t>(slot & 0xFFu)) * 1099511628211ull;
            slot >>= 8;
        }
        for (uint32_t b = 0; b < 2; ++b) {
            digest = (digest ^ static_cast<uint8_t>(reserved & 0xFFu)) * 1099511628211ull;
            reserved >>= 8;
        }
    }
    return digest;
}

static void fillSentinel(gx_sql_locator* rows, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        rows[i].page_id = 0xA5000000ull + i;
        rows[i].slot = 0xB000u + i;
        rows[i].reserved = 0xC000u + i;
    }
}

static bool sentinelsIntact(const gx_sql_locator* rows, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        if (rows[i].page_id != 0xA5000000ull + i ||
            rows[i].slot != 0xB000u + i || rows[i].reserved != 0xC000u + i) {
            return false;
        }
    }
    return true;
}

static bool runEdge32Demonstration(gx_app_context* context,
                                   gx_sql_database_handle database,
                                   uint32_t indexIdentity,
                                   gx_sql_locator* rows) {
    bool inventoryMatches = kSqlReadEdge32OracleCount == 80u;
    bool sawNegative = false, sawZero = false, sawPositive = false;
    uint32_t matchedSeparators = 0;
    for (uint32_t i = 0; i < kSqlReadEdge32OracleCount; ++i) {
        const SqlReadEdge32OracleEntry& expected = kSqlReadEdge32Oracle[i];
        if (i != 0 && kSqlReadEdge32Oracle[i - 1].key >= expected.key) {
            inventoryMatches = false;
        }
        sawNegative = sawNegative || expected.key < 0;
        sawZero = sawZero || expected.key == 0;
        sawPositive = sawPositive || expected.key > 0;
        for (uint32_t boundary = 0;
             boundary < kSqlReadEdge32InternalSeparatorCount; ++boundary) {
            if (expected.key == kSqlReadEdge32InternalSeparators[boundary]) {
                ++matchedSeparators;
                break;
            }
        }
        gx_sql_key key;
        key.type = GX_SQL_KEY_INT32;
        key.is_null = 0;
        key.value.int64_value = 0;
        key.value.int32_value = expected.key;
        uint32_t count = 0;
        const gx_sql_status status = context->host->sql_lookup_index(context,
            database, indexIdentity, &key, GX_SQL_QUERY_ORDERED_LOCATORS, rows,
            GX_SQL_MAX_RESULTS, &count);
        if (status != GX_SQL_STATUS_OK || count != 1 ||
            rows[0].page_id != expected.pageId ||
            rows[0].slot != expected.slot || rows[0].reserved != 0) {
            inventoryMatches = false;
        }
    }
    const bool boundariesCovered = matchedSeparators ==
        kSqlReadEdge32InternalSeparatorCount;
    if (!check(context, inventoryMatches && sawNegative && sawZero &&
            sawPositive && boundariesCovered &&
            kSqlReadEdge32Oracle[0].key == (-2147483647 - 1) &&
            kSqlReadEdge32Oracle[kSqlReadEdge32OracleCount - 1].key == 2147483647,
            "SQLReadDemo: Edge32 signed lookups match all 80 independent locators, both extrema, zero, and internal separators")) {
        return false;
    }

    gx_sql_key key;
    key.type = GX_SQL_KEY_INT32;
    key.is_null = 0;
    key.value.int64_value = 0;
    key.value.int32_value = kSqlReadEdge32FirstAbsentKey;
    fillSentinel(rows, GX_SQL_MAX_RESULTS);
    uint32_t count = 0xFFFFFFFFu;
    const gx_sql_status noMatch = context->host->sql_lookup_index(context,
        database, indexIdentity, &key, GX_SQL_QUERY_ORDERED_LOCATORS, rows,
        GX_SQL_MAX_RESULTS, &count);
    if (!check(context, noMatch == GX_SQL_STATUS_NO_MATCH && count == 0 &&
            sentinelsIntact(rows, GX_SQL_MAX_RESULTS),
            "SQLReadDemo: signed negative Edge32 no-match leaves rows unchanged")) {
        return false;
    }

    key.value.int32_value = 40;
    fillSentinel(rows, GX_SQL_MAX_RESULTS);
    count = 0xFFFFFFFFu;
    const gx_sql_status positiveNoMatch = context->host->sql_lookup_index(
        context, database, indexIdentity, &key, GX_SQL_QUERY_ORDERED_LOCATORS,
        rows, GX_SQL_MAX_RESULTS, &count);
    if (!check(context, positiveNoMatch == GX_SQL_STATUS_NO_MATCH && count == 0 &&
            sentinelsIntact(rows, GX_SQL_MAX_RESULTS),
            "SQLReadDemo: Edge32 positive key 40 remains absent")) {
        return false;
    }

    key.type = GX_SQL_KEY_INT64;
    key.value.int64_value = -38;
    fillSentinel(rows, GX_SQL_MAX_RESULTS);
    count = 0xFFFFFFFFu;
    const gx_sql_status wrongType = context->host->sql_lookup_index(context,
        database, indexIdentity, &key, GX_SQL_QUERY_ORDERED_LOCATORS, rows,
        GX_SQL_MAX_RESULTS, &count);
    if (!check(context, wrongType == GX_SQL_STATUS_INVALID_ARGUMENT &&
            count == 0 && sentinelsIntact(rows, GX_SQL_MAX_RESULTS),
            "SQLReadDemo: Edge32 rejects a wrong key type without changing rows")) {
        return false;
    }

    key.type = GX_SQL_KEY_INT32;
    key.is_null = 2;
    key.value.int64_value = 0;
    fillSentinel(rows, GX_SQL_MAX_RESULTS);
    count = 0xFFFFFFFFu;
    const gx_sql_status malformedKey = context->host->sql_lookup_index(context,
        database, indexIdentity, &key, GX_SQL_QUERY_ORDERED_LOCATORS, rows,
        GX_SQL_MAX_RESULTS, &count);
    if (!check(context, malformedKey == GX_SQL_STATUS_INVALID_ARGUMENT &&
            count == 0 && sentinelsIntact(rows, GX_SQL_MAX_RESULTS),
            "SQLReadDemo: malformed Edge32 null flag is rejected without changing rows")) {
        return false;
    }

    gx_sql_range range;
    range.has_lower = 1;
    range.lower_inclusive = 1;
    range.has_upper = 0;
    range.upper_inclusive = 0;
    range.lower.type = GX_SQL_KEY_INT32;
    range.lower.is_null = 0;
    range.lower.value.int64_value = 0;
    range.lower.value.int32_value = -38;
    range.upper.type = 0;
    range.upper.is_null = 0;
    range.upper.value.int64_value = 0;
    fillSentinel(rows, GX_SQL_MAX_RESULTS);
    count = 0xFFFFFFFFu;
    const gx_sql_status unsupportedRange = context->host->sql_scan_index_range(
        context, database, indexIdentity, &range, GX_SQL_QUERY_ORDERED_LOCATORS,
        rows, GX_SQL_MAX_RESULTS, &count);
    if (!check(context, unsupportedRange == GX_SQL_STATUS_UNSUPPORTED &&
            count == 0 && sentinelsIntact(rows, GX_SQL_MAX_RESULTS),
            "SQLReadDemo: Edge32 range scan remains explicitly unsupported")) {
        return false;
    }

    key.type = GX_SQL_KEY_INT32;
    key.is_null = 0;
    key.value.int64_value = 0;
    key.value.int32_value = kSqlReadEdge32Oracle[0].key;
    count = 0;
    const gx_sql_status recovery = context->host->sql_lookup_index(context,
        database, indexIdentity, &key, GX_SQL_QUERY_ORDERED_LOCATORS, rows,
        GX_SQL_MAX_RESULTS, &count);
    return check(context, recovery == GX_SQL_STATUS_OK && count == 1 &&
            rows[0].page_id == kSqlReadEdge32Oracle[0].pageId &&
            rows[0].slot == kSqlReadEdge32Oracle[0].slot,
            "SQLReadDemo: Edge32 minimum lookup recovers after rejected requests");
}

static bool runEdge64Demonstration(gx_app_context* context,
                                   gx_sql_database_handle database,
                                   uint32_t indexIdentity,
                                   gx_sql_locator* rows) {
    bool inventoryMatches = kSqlReadEdge64OracleCount == 80u;
    bool sawMinimum = false, sawMaximum = false;
    bool sawNegative = false, sawZero = false, sawPositive = false;
    for (uint32_t i = 0; i < kSqlReadEdge64OracleCount; ++i) {
        const SqlReadEdge64OracleEntry& expected = kSqlReadEdge64Oracle[i];
        if (i != 0 && kSqlReadEdge64Oracle[i - 1].key >= expected.key) {
            inventoryMatches = false;
        }
        sawMinimum = sawMinimum || expected.key == INT64_MIN;
        sawMaximum = sawMaximum || expected.key == INT64_MAX;
        sawNegative = sawNegative || expected.key < 0;
        sawZero = sawZero || expected.key == 0;
        sawPositive = sawPositive || expected.key > 0;

        gx_sql_key key;
        key.type = GX_SQL_KEY_INT64;
        key.is_null = 0;
        key.value.int64_value = expected.key;
        uint32_t count = 0;
        const gx_sql_status status = context->host->sql_lookup_index(context,
            database, indexIdentity, &key, GX_SQL_QUERY_ORDERED_LOCATORS,
            rows, GX_SQL_MAX_RESULTS, &count);
        if (status != GX_SQL_STATUS_OK || count != 1 ||
            rows[0].page_id != expected.page_id ||
            rows[0].slot != expected.slot || rows[0].reserved != 0) {
            inventoryMatches = false;
        }
    }
    if (!check(context, inventoryMatches && sawMinimum && sawMaximum &&
            sawNegative && sawZero && sawPositive,
            "SQLReadDemo: Edge64 matches all 80 independent signed key and locator pairs")) {
        return false;
    }

    const int64_t absentKeys[] = {
        (-9223372036854775807ll), 39ll, 9223372036854775806ll
    };
    for (uint32_t i = 0; i < sizeof(absentKeys) / sizeof(absentKeys[0]); ++i) {
        gx_sql_key key;
        key.type = GX_SQL_KEY_INT64;
        key.is_null = 0;
        key.value.int64_value = absentKeys[i];
        fillSentinel(rows, GX_SQL_MAX_RESULTS);
        uint32_t count = 0xFFFFFFFFu;
        const gx_sql_status status = context->host->sql_lookup_index(context,
            database, indexIdentity, &key, GX_SQL_QUERY_ORDERED_LOCATORS,
            rows, GX_SQL_MAX_RESULTS, &count);
        if (status != GX_SQL_STATUS_NO_MATCH || count != 0 ||
            !sentinelsIntact(rows, GX_SQL_MAX_RESULTS)) {
            return check(context, false,
                "SQLReadDemo: Edge64 absent boundary probes leave rows unchanged");
        }
    }
    if (!check(context, true,
            "SQLReadDemo: Edge64 absent minimum, interior, and maximum keys return no match")) {
        return false;
    }

    gx_sql_key wrongType;
    wrongType.type = GX_SQL_KEY_INT32;
    wrongType.is_null = 0;
    wrongType.value.int32_value = 1;
    fillSentinel(rows, GX_SQL_MAX_RESULTS);
    uint32_t count = 0xFFFFFFFFu;
    const gx_sql_status wrongTypeStatus = context->host->sql_lookup_index(
        context, database, indexIdentity, &wrongType,
        GX_SQL_QUERY_ORDERED_LOCATORS, rows, GX_SQL_MAX_RESULTS, &count);
    if (!check(context, wrongTypeStatus == GX_SQL_STATUS_INVALID_ARGUMENT &&
            count == 0 && sentinelsIntact(rows, GX_SQL_MAX_RESULTS),
            "SQLReadDemo: Edge64 rejects Int32 keys without changing rows")) {
        return false;
    }

    gx_sql_key capacityKey;
    capacityKey.type = GX_SQL_KEY_INT64;
    capacityKey.is_null = 0;
    capacityKey.value.int64_value = kSqlReadEdge64Oracle[0].key;
    fillSentinel(rows, GX_SQL_MAX_RESULTS);
    count = 0xFFFFFFFFu;
    const gx_sql_status capacityStatus = context->host->sql_lookup_index(
        context, database, indexIdentity, &capacityKey,
        GX_SQL_QUERY_ORDERED_LOCATORS, rows, 0, &count);
    if (!check(context, capacityStatus == GX_SQL_STATUS_CAPACITY_EXCEEDED &&
            count == 0 && sentinelsIntact(rows, GX_SQL_MAX_RESULTS),
            "SQLReadDemo: Edge64 insufficient capacity publishes no locator")) {
        return false;
    }

    gx_sql_range range;
    range.has_lower = 1;
    range.lower_inclusive = 1;
    range.lower.type = GX_SQL_KEY_INT64;
    range.lower.is_null = 0;
    range.lower.value.int64_value = -1;
    range.has_upper = 1;
    range.upper_inclusive = 1;
    range.upper.type = GX_SQL_KEY_INT64;
    range.upper.is_null = 0;
    range.upper.value.int64_value = 1;
    fillSentinel(rows, GX_SQL_MAX_RESULTS);
    count = 0xFFFFFFFFu;
    const gx_sql_status rangeStatus = context->host->sql_scan_index_range(
        context, database, indexIdentity, &range,
        GX_SQL_QUERY_ORDERED_LOCATORS, rows, GX_SQL_MAX_RESULTS, &count);
    if (!check(context, rangeStatus == GX_SQL_STATUS_UNSUPPORTED &&
            count == 0 && sentinelsIntact(rows, GX_SQL_MAX_RESULTS),
            "SQLReadDemo: Edge64 range scanning remains unsupported")) {
        return false;
    }

    gx_sql_key recoveryKey;
    recoveryKey.type = GX_SQL_KEY_INT64;
    recoveryKey.is_null = 0;
    recoveryKey.value.int64_value = kSqlReadEdge64Oracle[40].key;
    count = 0;
    const gx_sql_status recoveryStatus = context->host->sql_lookup_index(
        context, database, indexIdentity, &recoveryKey,
        GX_SQL_QUERY_ORDERED_LOCATORS, rows, GX_SQL_MAX_RESULTS, &count);
    return check(context, recoveryStatus == GX_SQL_STATUS_OK && count == 1 &&
            rows[0].page_id == kSqlReadEdge64Oracle[40].page_id &&
            rows[0].slot == kSqlReadEdge64Oracle[40].slot &&
            rows[0].reserved == 0,
            "SQLReadDemo: Edge64 lookup recovers after rejected requests");
}

} // namespace

extern "C" gx_result GX_CALL gx_main(gx_app_context* context) {
    const size_t sqlTableBytes = offsetof(gx_host_calls, sql_scan_index_range) +
        sizeof(context->host->sql_scan_index_range);
    if (!context || context->size < sizeof(gx_app_context) ||
        context->apiVersion != GX_API_VERSION || !context->host ||
        context->host->version != GX_API_VERSION ||
        context->host->size < sqlTableBytes || !context->host->log ||
        !context->host->get_api_version ||
        context->host->get_api_version(context) != GX_API_VERSION ||
        !context->host->sql_open_read_only_database ||
        !context->host->sql_close_database || !context->host->sql_lookup_index ||
        !context->host->sql_scan_index_range) {
        log(context, "SQLReadDemo: public host ABI negotiation failed");
        return GX_ERROR_UNSUPPORTED;
    }
    log(context, "SQLReadDemo: public host ABI version and size accepted");

    gx_sql_catalog_request catalogRequest;
    setCatalogRequest(&catalogRequest, GX_SQL_CATALOG_QUERY_DATABASES, 0, 0, 1);
    gx_sql_catalog_entry databaseInfo;
    gx_sql_catalog_result catalogResult;
    prepareCatalogResult(&catalogResult);
    const gx_sql_status databaseCatalogStatus = gx_sql_query_catalog(context,
        &catalogRequest, &databaseInfo, 1, &catalogResult);
    if (databaseCatalogStatus == GX_SQL_STATUS_UNSUPPORTED) {
        log(context, "SQLReadDemo: catalog API is explicitly unsupported by this host");
        return GX_ERROR_UNSUPPORTED;
    }
    if (!check(context, databaseCatalogStatus == GX_SQL_STATUS_OK &&
            catalogResult.entryCount == 1 && catalogResult.totalEntries == 1 &&
            catalogResult.nextIndex == 1 &&
            databaseInfo.size == sizeof(databaseInfo) &&
            databaseInfo.kind == GX_SQL_CATALOG_KIND_DATABASE &&
            textEquals(databaseInfo.name, "NDB1R4P11R3AH_MULTILEVEL") &&
            (databaseInfo.capabilities & (GX_SQL_CATALOG_CAP_READ_ONLY |
                GX_SQL_CATALOG_CAP_IMMUTABLE_PACKAGE)) ==
                (GX_SQL_CATALOG_CAP_READ_ONLY |
                 GX_SQL_CATALOG_CAP_IMMUTABLE_PACKAGE),
            "SQLReadDemo: discovered approved database from service catalog")) {
        return GX_ERROR_FAILED;
    }

    const uint32_t unknownDatabase = UINT32_MAX;
    gx_sql_database_handle unknownHandle = 0;
    if (!check(context,
            context->host->sql_open_read_only_database(context, unknownDatabase,
                GX_SQL_OPEN_READ_ONLY, &unknownHandle) ==
                GX_SQL_STATUS_INVALID_DATABASE && unknownHandle == 0,
            "SQLReadDemo: unknown database identity is rejected")) {
        return GX_ERROR_FAILED;
    }

    const uint32_t maxDiscoveredIndexes = 6;
    gx_sql_catalog_entry indexes[6];
    uint32_t discoveredIndexCount = 0;
    uint32_t nextIndex = 0;
    uint32_t totalIndexes = 0;
    setCatalogRequest(&catalogRequest, GX_SQL_CATALOG_QUERY_INDEXES, 0, 0,
                      GX_SQL_CATALOG_MAX_ENTRIES);
    gx_sql_catalog_entry prematureIndex;
    prepareCatalogResult(&catalogResult);
    const gx_sql_status unopenedCatalogStatus = gx_sql_query_catalog(context,
        &catalogRequest, &prematureIndex, 1, &catalogResult);
    if (!check(context, unopenedCatalogStatus == GX_SQL_STATUS_INVALID_HANDLE &&
            catalogResult.entryCount == 0,
            "SQLReadDemo: index enumeration requires an opened database")) {
        return GX_ERROR_FAILED;
    }

    gx_sql_database_handle database = 0;
    if (!check(context,
            context->host->sql_open_read_only_database(context,
                databaseInfo.identity, GX_SQL_OPEN_READ_ONLY,
                &database) == GX_SQL_STATUS_OK && database != 0,
            "SQLReadDemo: opened approved read-only database")) {
        return GX_ERROR_FAILED;
    }

    nextIndex = 0;
    do {
        uint32_t chunkCapacity = maxDiscoveredIndexes - discoveredIndexCount;
        if (chunkCapacity > GX_SQL_CATALOG_MAX_ENTRIES) {
            chunkCapacity = GX_SQL_CATALOG_MAX_ENTRIES;
        }
        setCatalogRequest(&catalogRequest, GX_SQL_CATALOG_QUERY_INDEXES,
                          database, nextIndex, chunkCapacity);
        if (discoveredIndexCount >= maxDiscoveredIndexes) {
            (void)context->host->sql_close_database(context, database);
            return GX_ERROR_FAILED;
        }
        prepareCatalogResult(&catalogResult);
        const gx_sql_status status = gx_sql_query_catalog(context,
            &catalogRequest, indexes + discoveredIndexCount, chunkCapacity,
            &catalogResult);
        if (status != GX_SQL_STATUS_OK || catalogResult.entryCount == 0 ||
            catalogResult.entryCount > chunkCapacity ||
            catalogResult.size != sizeof(catalogResult) ||
            catalogResult.totalEntries > maxDiscoveredIndexes ||
            catalogResult.nextIndex != nextIndex + catalogResult.entryCount ||
            discoveredIndexCount + catalogResult.entryCount >
                maxDiscoveredIndexes) {
            (void)context->host->sql_close_database(context, database);
            (void)check(context, false,
                "SQLReadDemo: bounded index catalog enumeration failed");
            return GX_ERROR_FAILED;
        }
        if (nextIndex == 0) totalIndexes = catalogResult.totalEntries;
        if (catalogResult.totalEntries != totalIndexes) {
            (void)context->host->sql_close_database(context, database);
            return GX_ERROR_FAILED;
        }
        discoveredIndexCount += catalogResult.entryCount;
        nextIndex = catalogResult.nextIndex;
    } while (nextIndex < totalIndexes);

    uint32_t k64IndexIdentity = 0;
    uint32_t k32IndexIdentity = 0;
    uint32_t kBoolIndexIdentity = 0;
    uint32_t kBoolKeyType = 0;
    uint32_t kTextIndexIdentity = 0;
    uint32_t kTextKeyType = 0;
    uint32_t edge64IndexIdentity = 0;
    uint32_t edge32IndexIdentity = 0;
    for (uint32_t i = 0; i < discoveredIndexCount; ++i) {
        const gx_sql_catalog_entry& index = indexes[i];
        const bool knownName = textEquals(index.name, "IX_Rows_K64") ||
            textEquals(index.name, "IX_Rows_K32") ||
            textEquals(index.name, "IX_Rows_KBool") ||
            textEquals(index.name, "IX_Rows_KText") ||
            textEquals(index.name, "IX_Rows_Edge64") ||
            textEquals(index.name, "IX_Rows_Edge32");
        const bool edge64NoNulls =
            index.identity == GX_SQL_INDEX_ROWS_EDGE64 &&
            index.keyType == GX_SQL_KEY_INT64 &&
            index.comparisonSemantics ==
                GX_SQL_COMPARISON_SIGNED_ASCENDING_NO_NULLS;
        const bool validComparison =
            (index.keyType == GX_SQL_KEY_BOOLEAN &&
             index.comparisonSemantics ==
                GX_SQL_COMPARISON_BOOLEAN_ASCENDING_NULLS_FIRST) ||
            (index.keyType == GX_SQL_KEY_TEXT &&
             index.comparisonSemantics ==
                GX_SQL_COMPARISON_TEXT_BYTES_ASCENDING_NULLS_FIRST) ||
            ((index.keyType == GX_SQL_KEY_INT32 ||
              index.keyType == GX_SQL_KEY_INT64) &&
             index.comparisonSemantics ==
                GX_SQL_COMPARISON_SIGNED_ASCENDING_NULLS_FIRST) ||
            edge64NoNulls;
        if (index.size != sizeof(index) || !knownName ||
            index.kind != GX_SQL_CATALOG_KIND_INDEX ||
            !validComparison ||
            index.maximumResults != GX_SQL_MAX_RESULTS ||
            !(index.capabilities & GX_SQL_CATALOG_CAP_ORDERED_LOCATORS)) {
            (void)context->host->sql_close_database(context, database);
            return GX_ERROR_FAILED;
        }
        if (textEquals(index.name, "IX_Rows_K64") &&
            index.keyType == GX_SQL_KEY_INT64 &&
            (index.capabilities & GX_SQL_CATALOG_CAP_POINT_LOOKUP) &&
            !(index.capabilities & GX_SQL_CATALOG_CAP_RANGE_SCAN)) {
            k64IndexIdentity = index.identity;
        }
        if (textEquals(index.name, "IX_Rows_K32") &&
            index.keyType == GX_SQL_KEY_INT32 &&
            (index.capabilities & GX_SQL_CATALOG_CAP_RANGE_SCAN) &&
            !(index.capabilities & GX_SQL_CATALOG_CAP_POINT_LOOKUP)) {
            k32IndexIdentity = index.identity;
        }
        if (textEquals(index.name, "IX_Rows_KBool") &&
            index.keyType == GX_SQL_KEY_BOOLEAN &&
            index.comparisonSemantics ==
                GX_SQL_COMPARISON_BOOLEAN_ASCENDING_NULLS_FIRST &&
            (index.capabilities & GX_SQL_CATALOG_CAP_POINT_LOOKUP) &&
            !(index.capabilities & GX_SQL_CATALOG_CAP_RANGE_SCAN)) {
            kBoolIndexIdentity = index.identity;
            kBoolKeyType = index.keyType;
        }
        if (textEquals(index.name, "IX_Rows_KText") &&
            index.keyType == GX_SQL_KEY_TEXT &&
            index.comparisonSemantics ==
                GX_SQL_COMPARISON_TEXT_BYTES_ASCENDING_NULLS_FIRST &&
            (index.capabilities & GX_SQL_CATALOG_CAP_POINT_LOOKUP) &&
            !(index.capabilities & GX_SQL_CATALOG_CAP_RANGE_SCAN)) {
            kTextIndexIdentity = index.identity;
            kTextKeyType = index.keyType;
        }
        if (textEquals(index.name, "IX_Rows_Edge32") &&
            index.identity == GX_SQL_INDEX_ROWS_EDGE32 &&
            index.keyType == GX_SQL_KEY_INT32 &&
            index.comparisonSemantics ==
                GX_SQL_COMPARISON_SIGNED_ASCENDING_NULLS_FIRST &&
            (index.capabilities & GX_SQL_CATALOG_CAP_POINT_LOOKUP) &&
            !(index.capabilities & GX_SQL_CATALOG_CAP_RANGE_SCAN)) {
            edge32IndexIdentity = index.identity;
        }
        if (textEquals(index.name, "IX_Rows_Edge64") &&
            index.identity == GX_SQL_INDEX_ROWS_EDGE64 &&
            index.keyType == GX_SQL_KEY_INT64 && edge64NoNulls &&
            (index.capabilities & GX_SQL_CATALOG_CAP_POINT_LOOKUP) &&
            !(index.capabilities & GX_SQL_CATALOG_CAP_RANGE_SCAN)) {
            edge64IndexIdentity = index.identity;
        }
    }
    const bool catalogIdentityOrder = discoveredIndexCount == 6 &&
        indexes[0].identity == GX_SQL_INDEX_ROWS_K64 &&
        indexes[1].identity == GX_SQL_INDEX_ROWS_K32 &&
        indexes[2].identity == GX_SQL_INDEX_ROWS_KBOOL &&
        indexes[3].identity == GX_SQL_INDEX_ROWS_KTEXT &&
        indexes[4].identity == GX_SQL_INDEX_ROWS_EDGE64 &&
        indexes[5].identity == GX_SQL_INDEX_ROWS_EDGE32;
    if (!check(context, discoveredIndexCount == totalIndexes && totalIndexes == 6 &&
            catalogIdentityOrder &&
            k64IndexIdentity != 0 && k32IndexIdentity != 0 &&
            kBoolIndexIdentity != 0 && kTextIndexIdentity != 0 &&
            edge64IndexIdentity != 0 && edge32IndexIdentity != 0,
            "SQLReadDemo: paginated catalog discovered all six indexes in identity order")) {
        (void)context->host->sql_close_database(context, database);
        return GX_ERROR_FAILED;
    }

    gx_sql_locator rows[GX_SQL_MAX_RESULTS];
    uint32_t count = 0;
    gx_sql_key point;
    point.type = GX_SQL_KEY_INT64;
    point.is_null = 0;
    point.value.int64_value = 7;
    gx_sql_range range;
    range.has_lower = 1;
    range.lower_inclusive = 1;
    range.lower.type = GX_SQL_KEY_INT32;
    range.lower.is_null = 0;
    range.lower.value.int32_value = 1;
    range.has_upper = 1;
    range.upper_inclusive = 1;
    range.upper.type = GX_SQL_KEY_INT32;
    range.upper.is_null = 0;
    range.upper.value.int32_value = 2;

    fillSentinel(rows, GX_SQL_MAX_RESULTS);
    count = 0xFFFFFFFFu;
    const gx_sql_status unsupportedOperationStatus =
        context->host->sql_scan_index_range(context, database,
            k64IndexIdentity, &range, GX_SQL_QUERY_ORDERED_LOCATORS,
            rows, GX_SQL_MAX_RESULTS, &count);
    if (!check(context,
            unsupportedOperationStatus == GX_SQL_STATUS_UNSUPPORTED &&
            count == 0 && sentinelsIntact(rows, GX_SQL_MAX_RESULTS),
            "SQLReadDemo: unsupported range operation leaves results unchanged")) {
        (void)context->host->sql_close_database(context, database);
        return GX_ERROR_FAILED;
    }

    fillSentinel(rows, GX_SQL_MAX_RESULTS);
    count = 0xFFFFFFFFu;
    const gx_sql_status unknownIndexStatus = context->host->sql_lookup_index(
        context, database, UINT32_MAX, &point, GX_SQL_QUERY_ORDERED_LOCATORS,
        rows, GX_SQL_MAX_RESULTS, &count);
    if (!check(context, unknownIndexStatus == GX_SQL_STATUS_INVALID_INDEX &&
            count == 0 && sentinelsIntact(rows, GX_SQL_MAX_RESULTS),
            "SQLReadDemo: unknown index identity is rejected")) {
        (void)context->host->sql_close_database(context, database);
        return GX_ERROR_FAILED;
    }

    fillSentinel(rows, GX_SQL_MAX_RESULTS);
    count = 0xFFFFFFFFu;
    const gx_sql_status capacityStatus = context->host->sql_lookup_index(context,
        database, k64IndexIdentity, &point, GX_SQL_QUERY_ORDERED_LOCATORS,
        rows, GX_SQL_MAX_RESULTS - 1u, &count);
    if (!check(context, capacityStatus == GX_SQL_STATUS_CAPACITY_EXCEEDED &&
            count == 0 && sentinelsIntact(rows, GX_SQL_MAX_RESULTS),
            "SQLReadDemo: insufficient capacity publishes no point locators")) {
        (void)context->host->sql_close_database(context, database);
        return GX_ERROR_FAILED;
    }
    count = 0;
    const gx_sql_status pointStatus = context->host->sql_lookup_index(context,
        database, k64IndexIdentity, &point, GX_SQL_QUERY_ORDERED_LOCATORS,
        rows, GX_SQL_MAX_RESULTS, &count);
    if (pointStatus != GX_SQL_STATUS_OK) log(context, "SQLReadDemo: point lookup returned non-OK status");
    else if (count != 73) log(context, "SQLReadDemo: point lookup count mismatch");
    else if (!rowsEqual(rows, count, 3, 1, 479, 0)) log(context, "SQLReadDemo: point locator boundary mismatch");
    else if (locatorSequenceDigest(rows, count) != 0x61532064D1753604ull) log(context, "SQLReadDemo: point locator sequence mismatch");
    if (!check(context, pointStatus == GX_SQL_STATUS_OK && count == 73 &&
            rowsEqual(rows, count, 3, 1, 479, 0) &&
            locatorSequenceDigest(rows, count) == 0x61532064D1753604ull,
            "SQLReadDemo: point lookup matches all 73 ordered manifest locators")) {
        (void)context->host->sql_close_database(context, database);
        return GX_ERROR_FAILED;
    }

    const gx_sql_status rangeStatus = context->host->sql_scan_index_range(context,
        database, k32IndexIdentity, &range, GX_SQL_QUERY_ORDERED_LOCATORS,
        rows, GX_SQL_MAX_RESULTS, &count);
    if (rangeStatus != GX_SQL_STATUS_OK) log(context, "SQLReadDemo: range scan returned non-OK status");
    else if (count != 32) log(context, "SQLReadDemo: range scan count mismatch");
    else if (!rowsEqual(rows, count, 3, 1, 422, 7)) log(context, "SQLReadDemo: range locator boundary mismatch");
    if (!check(context, rangeStatus == GX_SQL_STATUS_OK && count == 32 &&
            rowsEqual(rows, count, 3, 1, 422, 7) &&
            locatorSequenceDigest(rows, count) == 0xD4841B43ACFFF3A6ull,
            "SQLReadDemo: range scan matches all 32 ordered manifest locators")) {
        (void)context->host->sql_close_database(context, database);
        return GX_ERROR_FAILED;
    }

    gx_sql_key booleanKey = {};
    booleanKey.type = kBoolKeyType;
    booleanKey.is_null = 0;
    booleanKey.value.boolean_value = 1;
    fillSentinel(rows, GX_SQL_MAX_RESULTS);
    count = 0xFFFFFFFFu;
    const gx_sql_status booleanCapacityStatus = context->host->sql_lookup_index(
        context, database, kBoolIndexIdentity, &booleanKey,
        GX_SQL_QUERY_ORDERED_LOCATORS, rows, 33, &count);
    if (!check(context,
            booleanCapacityStatus == GX_SQL_STATUS_CAPACITY_EXCEEDED &&
            count == 0 && sentinelsIntact(rows, GX_SQL_MAX_RESULTS),
            "SQLReadDemo: Boolean point capacity failure publishes no locators")) {
        (void)context->host->sql_close_database(context, database);
        return GX_ERROR_FAILED;
    }
    fillSentinel(rows, GX_SQL_MAX_RESULTS);
    count = 0;
    const gx_sql_status booleanStatus = context->host->sql_lookup_index(context,
        database, kBoolIndexIdentity, &booleanKey, GX_SQL_QUERY_ORDERED_LOCATORS,
        rows, GX_SQL_MAX_RESULTS, &count);
    if (!check(context, booleanStatus == GX_SQL_STATUS_OK && count == 34 &&
            rowsEqualExpected(rows, count, kSqlDemoBooleanTrueExpected, 34),
            "SQLReadDemo: Boolean point lookup matches all 34 independently decoded locators")) {
        (void)context->host->sql_close_database(context, database);
        return GX_ERROR_FAILED;
    }

    gx_sql_key booleanNullKey = {};
    booleanNullKey.type = kBoolKeyType;
    booleanNullKey.is_null = 1;
    fillSentinel(rows, GX_SQL_MAX_RESULTS);
    count = 0;
    const gx_sql_status booleanNullStatus = context->host->sql_lookup_index(
        context, database, kBoolIndexIdentity, &booleanNullKey,
        GX_SQL_QUERY_ORDERED_LOCATORS, rows, GX_SQL_MAX_RESULTS, &count);
    if (!check(context, booleanNullStatus == GX_SQL_STATUS_OK && count == 12 &&
            rowsEqualExpected(rows, count, kSqlDemoBooleanNullExpected, 12),
            "SQLReadDemo: Boolean NULL lookup matches all 12 NULL-first locators")) {
        (void)context->host->sql_close_database(context, database);
        return GX_ERROR_FAILED;
    }

    gx_sql_key booleanFalseKey = {};
    booleanFalseKey.type = kBoolKeyType;
    booleanFalseKey.value.boolean_value = 0;
    fillSentinel(rows, GX_SQL_MAX_RESULTS);
    count = 0;
    const gx_sql_status booleanFalseStatus = context->host->sql_lookup_index(
        context, database, kBoolIndexIdentity, &booleanFalseKey,
        GX_SQL_QUERY_ORDERED_LOCATORS, rows, GX_SQL_MAX_RESULTS, &count);
    if (!check(context, booleanFalseStatus == GX_SQL_STATUS_OK && count == 34 &&
            rowsEqualExpected(rows, count, kSqlDemoBooleanFalseExpected, 34),
            "SQLReadDemo: Boolean false lookup matches all 34 ordered locators")) {
        (void)context->host->sql_close_database(context, database);
        return GX_ERROR_FAILED;
    }

    gx_sql_key wrongBooleanType = {};
    wrongBooleanType.type = GX_SQL_KEY_INT32;
    wrongBooleanType.value.int32_value = 1;
    fillSentinel(rows, GX_SQL_MAX_RESULTS);
    count = 0xFFFFFFFFu;
    const gx_sql_status wrongBooleanTypeStatus = context->host->sql_lookup_index(
        context, database, kBoolIndexIdentity, &wrongBooleanType,
        GX_SQL_QUERY_ORDERED_LOCATORS, rows, GX_SQL_MAX_RESULTS, &count);
    if (!check(context,
            wrongBooleanTypeStatus == GX_SQL_STATUS_INVALID_ARGUMENT &&
            count == 0 && sentinelsIntact(rows, GX_SQL_MAX_RESULTS),
            "SQLReadDemo: wrong Boolean key type is rejected without result mutation")) {
        (void)context->host->sql_close_database(context, database);
        return GX_ERROR_FAILED;
    }

    gx_sql_key malformedBoolean = booleanKey;
    malformedBoolean.value.boolean_value = 2;
    fillSentinel(rows, GX_SQL_MAX_RESULTS);
    count = 0xFFFFFFFFu;
    const gx_sql_status malformedBooleanStatus = context->host->sql_lookup_index(
        context, database, kBoolIndexIdentity, &malformedBoolean,
        GX_SQL_QUERY_ORDERED_LOCATORS, rows, GX_SQL_MAX_RESULTS, &count);
    if (!check(context,
            malformedBooleanStatus == GX_SQL_STATUS_INVALID_ARGUMENT &&
            count == 0 && sentinelsIntact(rows, GX_SQL_MAX_RESULTS),
            "SQLReadDemo: malformed Boolean value is rejected without result mutation")) {
        (void)context->host->sql_close_database(context, database);
        return GX_ERROR_FAILED;
    }

    fillSentinel(rows, GX_SQL_MAX_RESULTS);
    count = 0xFFFFFFFFu;
    const gx_sql_status unsupportedBooleanRangeStatus =
        context->host->sql_scan_index_range(context, database, kBoolIndexIdentity,
            &range, GX_SQL_QUERY_ORDERED_LOCATORS, rows, GX_SQL_MAX_RESULTS,
            &count);
    if (!check(context,
            unsupportedBooleanRangeStatus == GX_SQL_STATUS_UNSUPPORTED &&
            count == 0 && sentinelsIntact(rows, GX_SQL_MAX_RESULTS),
            "SQLReadDemo: Boolean range operation remains unsupported")) {
        (void)context->host->sql_close_database(context, database);
        return GX_ERROR_FAILED;
    }

    count = 0;
    const gx_sql_status booleanRecoveryStatus = context->host->sql_lookup_index(
        context, database, kBoolIndexIdentity, &booleanKey,
        GX_SQL_QUERY_ORDERED_LOCATORS, rows, GX_SQL_MAX_RESULTS, &count);
    if (!check(context, booleanRecoveryStatus == GX_SQL_STATUS_OK && count == 34 &&
            rowsEqualExpected(rows, count, kSqlDemoBooleanTrueExpected, 34),
            "SQLReadDemo: Boolean lookup succeeds after rejected requests")) {
        (void)context->host->sql_close_database(context, database);
        return GX_ERROR_FAILED;
    }

    const uint8_t embeddedNulTextBytes[] = {0x61u, 0x00u, 0x62u};
    const uint8_t otherTextBytes[] = {0x6Fu, 0x74u, 0x68u, 0x65u, 0x72u};
    const uint8_t missingTextBytes[] = {0x61u, 0x62u, 0x73u, 0x65u, 0x6Eu, 0x74u};
    gx_sql_key textNullKey;
    gx_sql_key embeddedNulTextKey;
    gx_sql_key otherTextKey;
    gx_sql_key missingTextKey;
    if (!check(context,
            setTextKey(&textNullKey, kTextKeyType, true, nullptr, 0) &&
            setTextKey(&embeddedNulTextKey, kTextKeyType, false,
                embeddedNulTextBytes, sizeof(embeddedNulTextBytes)) &&
            setTextKey(&otherTextKey, kTextKeyType, false,
                otherTextBytes, sizeof(otherTextBytes)) &&
            setTextKey(&missingTextKey, kTextKeyType, false,
                missingTextBytes, sizeof(missingTextBytes)),
            "SQLReadDemo: prepared bounded binary Text keys with explicit lengths")) {
        (void)context->host->sql_close_database(context, database);
        return GX_ERROR_FAILED;
    }

    fillSentinel(rows, GX_SQL_MAX_RESULTS);
    count = 0;
    const gx_sql_status textNullStatus = context->host->sql_lookup_index(context,
        database, kTextIndexIdentity, &textNullKey, GX_SQL_QUERY_ORDERED_LOCATORS,
        rows, GX_SQL_MAX_RESULTS, &count);
    if (!check(context, textNullStatus == GX_SQL_STATUS_OK && count == 8 &&
            rowsEqualExpected(rows, count, kSqlDemoTextNullExpected, 8),
            "SQLReadDemo: Text NULL lookup matches all 8 independent locators")) {
        (void)context->host->sql_close_database(context, database);
        return GX_ERROR_FAILED;
    }

    fillSentinel(rows, GX_SQL_MAX_RESULTS);
    count = 0;
    const gx_sql_status embeddedNulTextStatus = context->host->sql_lookup_index(
        context, database, kTextIndexIdentity, &embeddedNulTextKey,
        GX_SQL_QUERY_ORDERED_LOCATORS, rows, GX_SQL_MAX_RESULTS, &count);
    if (!check(context, embeddedNulTextStatus == GX_SQL_STATUS_OK && count == 24 &&
            rowsEqualExpected(rows, count, kSqlDemoTextEmbeddedNulExpected, 24),
            "SQLReadDemo: embedded-NUL Text lookup matches all 24 independent locators")) {
        (void)context->host->sql_close_database(context, database);
        return GX_ERROR_FAILED;
    }

    fillSentinel(rows, GX_SQL_MAX_RESULTS);
    count = 0;
    const gx_sql_status otherTextCapacityStatus =
        context->host->sql_lookup_index(context, database, kTextIndexIdentity,
            &otherTextKey, GX_SQL_QUERY_ORDERED_LOCATORS, rows, 47, &count);
    if (!check(context,
            otherTextCapacityStatus == GX_SQL_STATUS_CAPACITY_EXCEEDED &&
            count == 0 && sentinelsIntact(rows, GX_SQL_MAX_RESULTS),
            "SQLReadDemo: Text capacity below 48 publishes no locators")) {
        (void)context->host->sql_close_database(context, database);
        return GX_ERROR_FAILED;
    }

    fillSentinel(rows, GX_SQL_MAX_RESULTS);
    count = 0;
    const gx_sql_status otherTextStatus = context->host->sql_lookup_index(context,
        database, kTextIndexIdentity, &otherTextKey, GX_SQL_QUERY_ORDERED_LOCATORS,
        rows, GX_SQL_MAX_RESULTS, &count);
    if (!check(context, otherTextStatus == GX_SQL_STATUS_OK && count == 48 &&
            rowsEqualExpected(rows, count, kSqlDemoTextOtherExpected, 48),
            "SQLReadDemo: other Text lookup matches all 48 independent locators")) {
        (void)context->host->sql_close_database(context, database);
        return GX_ERROR_FAILED;
    }

    fillSentinel(rows, GX_SQL_MAX_RESULTS);
    count = 0xFFFFFFFFu;
    const gx_sql_status missingTextStatus = context->host->sql_lookup_index(
        context, database, kTextIndexIdentity, &missingTextKey,
        GX_SQL_QUERY_ORDERED_LOCATORS, rows, GX_SQL_MAX_RESULTS, &count);
    if (!check(context, missingTextStatus == GX_SQL_STATUS_NO_MATCH && count == 0 &&
            sentinelsIntact(rows, GX_SQL_MAX_RESULTS),
            "SQLReadDemo: Text no-match leaves locator storage unchanged")) {
        (void)context->host->sql_close_database(context, database);
        return GX_ERROR_FAILED;
    }

    gx_sql_key wrongTextType;
    wrongTextType.type = GX_SQL_KEY_INT32;
    wrongTextType.is_null = 0;
    wrongTextType.value.int32_value = 1;
    fillSentinel(rows, GX_SQL_MAX_RESULTS);
    count = 0xFFFFFFFFu;
    const gx_sql_status wrongTextTypeStatus = context->host->sql_lookup_index(
        context, database, kTextIndexIdentity, &wrongTextType,
        GX_SQL_QUERY_ORDERED_LOCATORS, rows, GX_SQL_MAX_RESULTS, &count);
    if (!check(context,
            wrongTextTypeStatus == GX_SQL_STATUS_INVALID_ARGUMENT && count == 0 &&
            sentinelsIntact(rows, GX_SQL_MAX_RESULTS),
            "SQLReadDemo: wrong Text key type is rejected without result mutation")) {
        (void)context->host->sql_close_database(context, database);
        return GX_ERROR_FAILED;
    }

    gx_sql_key invalidTextLength;
    invalidTextLength.type = kTextKeyType;
    invalidTextLength.is_null = 0;
    invalidTextLength.value.text_value.length = GX_SQL_TEXT_INLINE_BYTES + 1u;
    for (uint8_t i = 0; i < GX_SQL_TEXT_INLINE_BYTES; ++i) {
        invalidTextLength.value.text_value.bytes[i] = embeddedNulTextBytes[i % 3u];
    }
    fillSentinel(rows, GX_SQL_MAX_RESULTS);
    count = 0xFFFFFFFFu;
    const gx_sql_status invalidTextLengthStatus = context->host->sql_lookup_index(
        context, database, kTextIndexIdentity, &invalidTextLength,
        GX_SQL_QUERY_ORDERED_LOCATORS, rows, GX_SQL_MAX_RESULTS, &count);
    if (!check(context,
            invalidTextLengthStatus == GX_SQL_STATUS_INVALID_ARGUMENT && count == 0 &&
            sentinelsIntact(rows, GX_SQL_MAX_RESULTS),
            "SQLReadDemo: Text length beyond inline capacity is rejected safely")) {
        (void)context->host->sql_close_database(context, database);
        return GX_ERROR_FAILED;
    }

    fillSentinel(rows, GX_SQL_MAX_RESULTS);
    count = 0xFFFFFFFFu;
    const gx_sql_status unsupportedTextRangeStatus =
        context->host->sql_scan_index_range(context, database, kTextIndexIdentity,
            &range, GX_SQL_QUERY_ORDERED_LOCATORS, rows, GX_SQL_MAX_RESULTS,
            &count);
    if (!check(context,
            unsupportedTextRangeStatus == GX_SQL_STATUS_UNSUPPORTED && count == 0 &&
            sentinelsIntact(rows, GX_SQL_MAX_RESULTS),
            "SQLReadDemo: Text range scan remains unsupported")) {
        (void)context->host->sql_close_database(context, database);
        return GX_ERROR_FAILED;
    }

    fillSentinel(rows, GX_SQL_MAX_RESULTS);
    count = 0;
    const gx_sql_status textRecoveryStatus = context->host->sql_lookup_index(
        context, database, kTextIndexIdentity, &otherTextKey,
        GX_SQL_QUERY_ORDERED_LOCATORS, rows, GX_SQL_MAX_RESULTS, &count);
    if (!check(context, textRecoveryStatus == GX_SQL_STATUS_OK && count == 48 &&
            rowsEqualExpected(rows, count, kSqlDemoTextOtherExpected, 48),
            "SQLReadDemo: Text lookup succeeds after rejected requests")) {
        (void)context->host->sql_close_database(context, database);
        return GX_ERROR_FAILED;
    }

    if (!runEdge32Demonstration(context, database, edge32IndexIdentity, rows)) {
        (void)context->host->sql_close_database(context, database);
        return GX_ERROR_FAILED;
    }
    if (!runEdge64Demonstration(context, database, edge64IndexIdentity, rows)) {
        (void)context->host->sql_close_database(context, database);
        return GX_ERROR_FAILED;
    }

    range.lower_inclusive = 0;
    range.upper_inclusive = 0;
    fillSentinel(rows, GX_SQL_MAX_RESULTS);
    count = 0xFFFFFFFFu;
    const gx_sql_status exclusiveStatus = context->host->sql_scan_index_range(context,
        database, k32IndexIdentity, &range, GX_SQL_QUERY_ORDERED_LOCATORS,
        rows, GX_SQL_MAX_RESULTS, &count);
    if (!check(context, exclusiveStatus == GX_SQL_STATUS_NO_MATCH && count == 0 &&
            sentinelsIntact(rows, GX_SQL_MAX_RESULTS),
            "SQLReadDemo: exclusive bounds exclude both range endpoints")) {
        (void)context->host->sql_close_database(context, database);
        return GX_ERROR_FAILED;
    }

    fillSentinel(rows, GX_SQL_MAX_RESULTS);
    gx_sql_key missing;
    missing.type = GX_SQL_KEY_INT64;
    missing.is_null = 0;
    missing.value.int64_value = 999;
    count = 0xFFFFFFFFu;
    const gx_sql_status missingStatus = context->host->sql_lookup_index(context,
        database, k64IndexIdentity, &missing, GX_SQL_QUERY_ORDERED_LOCATORS,
        rows, GX_SQL_MAX_RESULTS, &count);
    if (!check(context, missingStatus == GX_SQL_STATUS_NO_MATCH && count == 0 &&
            sentinelsIntact(rows, GX_SQL_MAX_RESULTS),
            "SQLReadDemo: no-match leaves locator storage unchanged")) {
        (void)context->host->sql_close_database(context, database);
        return GX_ERROR_FAILED;
    }

    gx_sql_key invalid;
    invalid.type = 99;
    invalid.is_null = 0;
    invalid.value.int64_value = 0;
    fillSentinel(rows, GX_SQL_MAX_RESULTS);
    count = 0xFFFFFFFFu;
    const gx_sql_status invalidStatus = context->host->sql_lookup_index(context,
        database, k64IndexIdentity, &invalid, GX_SQL_QUERY_ORDERED_LOCATORS,
        rows, GX_SQL_MAX_RESULTS, &count);
    if (!check(context, invalidStatus == GX_SQL_STATUS_INVALID_ARGUMENT &&
            count == 0 && sentinelsIntact(rows, GX_SQL_MAX_RESULTS),
            "SQLReadDemo: invalid key rejected without result mutation")) {
        (void)context->host->sql_close_database(context, database);
        return GX_ERROR_FAILED;
    }

    count = 0;
    const gx_sql_status recoveryStatus = context->host->sql_lookup_index(context,
        database, k64IndexIdentity, &point, GX_SQL_QUERY_ORDERED_LOCATORS,
        rows, GX_SQL_MAX_RESULTS, &count);
    if (!check(context, recoveryStatus == GX_SQL_STATUS_OK && count == 73 &&
            rowsEqual(rows, count, 3, 1, 479, 0),
            "SQLReadDemo: valid lookup succeeds after rejected request")) {
        (void)context->host->sql_close_database(context, database);
        return GX_ERROR_FAILED;
    }

    if (!check(context,
            context->host->sql_close_database(context, database) == GX_SQL_STATUS_OK,
            "SQLReadDemo: closed database handle")) {
        return GX_ERROR_FAILED;
    }
    count = 0;
    const gx_sql_status closedStatus = context->host->sql_lookup_index(context,
        database, k64IndexIdentity, &point, GX_SQL_QUERY_ORDERED_LOCATORS,
        rows, GX_SQL_MAX_RESULTS, &count);
    if (!check(context, closedStatus == GX_SQL_STATUS_INVALID_HANDLE && count == 0,
            "SQLReadDemo: closed database handle rejected")) {
        return GX_ERROR_FAILED;
    }

#if defined(GX_SQLREAD_LIFECYCLE_QUALIFICATION) && GX_SQLREAD_LIFECYCLE_QUALIFICATION
    gx_sql_database_handle exitDatabase = 0;
    if (!check(context,
            context->host->sql_open_read_only_database(context,
                GX_SQL_DATABASE_R3AH_MULTILEVEL, GX_SQL_OPEN_READ_ONLY,
                &exitDatabase) == GX_SQL_STATUS_OK && exitDatabase != 0,
            "SQLReadDemo: opened second session for normal application return")) {
        return GX_ERROR_FAILED;
    }
    log(context, "SQLReadDemo: returning normally with one session outstanding");
#else
    log(context, "SQLReadDemo: completed all application-facing checks");
#endif
    return GX_OK;
}
