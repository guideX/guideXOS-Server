#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t gx_sql_database_handle;

typedef enum gx_sql_database_identity {
    GX_SQL_DATABASE_R3AH_MULTILEVEL = 1
} gx_sql_database_identity;

typedef enum gx_sql_index_identity {
    GX_SQL_INDEX_ROWS_K64 = 1,
    GX_SQL_INDEX_ROWS_K32 = 2,
    GX_SQL_INDEX_ROWS_KBOOL = 3,
    GX_SQL_INDEX_ROWS_KTEXT = 4,
    GX_SQL_INDEX_ROWS_EDGE64 = 5,
    GX_SQL_INDEX_ROWS_EDGE32 = 6
} gx_sql_index_identity;

typedef enum gx_sql_status {
    GX_SQL_STATUS_OK = 0,
    GX_SQL_STATUS_NO_MATCH = 1,
    GX_SQL_STATUS_INVALID_ARGUMENT = 2,
    GX_SQL_STATUS_INVALID_DATABASE = 3,
    GX_SQL_STATUS_INVALID_INDEX = 4,
    GX_SQL_STATUS_INVALID_HANDLE = 5,
    GX_SQL_STATUS_PERMISSION_DENIED = 6,
    GX_SQL_STATUS_CAPACITY_EXCEEDED = 7,
    GX_SQL_STATUS_STORAGE_FAILURE = 8,
    GX_SQL_STATUS_CORRUPT_INDEX = 9,
    GX_SQL_STATUS_UNSUPPORTED = 10,
    GX_SQL_STATUS_RESOURCE_EXHAUSTED = 11,
    GX_SQL_STATUS_PACKAGE_INVALID = 12,
    GX_SQL_STATUS_DATABASE_UNAVAILABLE = 13,
    GX_SQL_STATUS_DATABASE_CORRUPT = 14
} gx_sql_status;

enum {
    GX_SQL_OPEN_READ_ONLY = 1u,
    GX_SQL_QUERY_ORDERED_LOCATORS = 1u,
    GX_SQL_MAX_RESULTS = 73u,
    GX_SQL_KEY_INT32 = 1u,
    GX_SQL_KEY_INT64 = 2u,
    GX_SQL_KEY_BOOLEAN = 3u,
    GX_SQL_KEY_TEXT = 4u,
    GX_SQL_TEXT_INLINE_BYTES = 7u
};

typedef struct gx_sql_key {
    uint32_t type;
    uint32_t is_null;
    union {
        int32_t int32_value;
        int64_t int64_value;
        // Boolean uses the existing 8-byte union slot. Only 0 and 1 are valid
        // for non-NULL keys; this addition does not change gx_sql_key layout.
        uint32_t boolean_value;
        // Text is a bounded inline byte span. The explicit length preserves
        // embedded zero bytes; maximum 7 payload bytes keeps the existing
        // 8-byte union slot and 16-byte gx_sql_key ABI unchanged.
        struct {
            uint8_t length;
            uint8_t bytes[GX_SQL_TEXT_INLINE_BYTES];
        } text_value;
    } value;
} gx_sql_key;

typedef struct gx_sql_range {
    uint32_t has_lower;
    uint32_t lower_inclusive;
    gx_sql_key lower;
    uint32_t has_upper;
    uint32_t upper_inclusive;
    gx_sql_key upper;
} gx_sql_range;

typedef struct gx_sql_locator {
    uint64_t page_id;
    uint32_t slot;
    uint32_t reserved;
} gx_sql_locator;

enum {
    GX_SQL_CATALOG_QUERY_DATABASES = 1u,
    GX_SQL_CATALOG_QUERY_INDEXES = 2u,
    GX_SQL_CATALOG_KIND_DATABASE = 1u,
    GX_SQL_CATALOG_KIND_INDEX = 2u,
    GX_SQL_CATALOG_CAP_READ_ONLY = 1u,
    GX_SQL_CATALOG_CAP_IMMUTABLE_PACKAGE = 2u,
    GX_SQL_CATALOG_CAP_POINT_LOOKUP = 4u,
    GX_SQL_CATALOG_CAP_RANGE_SCAN = 8u,
    GX_SQL_CATALOG_CAP_ORDERED_LOCATORS = 16u,
    GX_SQL_CATALOG_MAX_ENTRIES = 2u,
    GX_SQL_COMPARISON_SIGNED_ASCENDING_NULLS_FIRST = 1u,
    GX_SQL_COMPARISON_BOOLEAN_ASCENDING_NULLS_FIRST = 2u,
    GX_SQL_COMPARISON_TEXT_BYTES_ASCENDING_NULLS_FIRST = 3u,
    GX_SQL_COMPARISON_SIGNED_ASCENDING_NO_NULLS = 4u,
    GX_SQL_CATALOG_NAME_BYTES = 64u
};

// Requests and results are version-sized: callers set size to sizeof the
// structure they compiled. Names in returned entries are NUL-terminated UTF-8
// (currently ASCII). startIndex/maxEntries provide stable bounded enumeration.
typedef struct gx_sql_catalog_request {
    uint32_t size;
    uint32_t query;
    gx_sql_database_handle databaseHandle;
    uint32_t startIndex;
    uint32_t maxEntries;
} gx_sql_catalog_request;

typedef struct gx_sql_catalog_entry {
    uint32_t size;
    uint32_t kind;
    uint32_t identity;
    uint32_t keyType;
    uint32_t capabilities;
    uint32_t comparisonSemantics;
    uint32_t maximumResults;
    char name[GX_SQL_CATALOG_NAME_BYTES];
} gx_sql_catalog_entry;

typedef struct gx_sql_catalog_result {
    uint32_t size;
    uint32_t entryCount;
    uint32_t totalEntries;
    uint32_t nextIndex;
    uint32_t requiredCapacity;
} gx_sql_catalog_result;

#ifdef __cplusplus
}
#endif
