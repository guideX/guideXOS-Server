#include "gxos_tls_foundation.h"
#include "gxos_tls_prerequisites.h"

#if defined(GXOS_BARE_METAL)
#include "kernel/core/include/kernel/pit.h"
#include "kernel/core/include/kernel/serial_debug.h"
#include "kernel/core/include/kernel/tcp.h"
#include "kernel/core/include/kernel/vfs.h"
#endif

#if defined(__has_include)
#if __has_include("third_party/mbedtls/include/mbedtls/version.h")
#define GXOS_TLS_MBEDTLS_SOURCE_PRESENT 1
#endif
#if __has_include("third_party/mbedtls/tf-psa-crypto/include/mbedtls/platform.h")
#define GXOS_TLS_MBEDTLS_PLATFORM_HEADER_PRESENT 1
#endif
#if __has_include("third_party/mbedtls/tf-psa-crypto/include/mbedtls/platform_util.h")
#define GXOS_TLS_MBEDTLS_PLATFORM_UTIL_HEADER_PRESENT 1
#endif
#if __has_include("third_party/mbedtls/tf-psa-crypto/include/mbedtls/constant_time.h")
#define GXOS_TLS_MBEDTLS_CONSTANT_TIME_HEADER_PRESENT 1
#endif
#if __has_include("third_party/mbedtls/tf-psa-crypto/include/mbedtls/psa_util.h")
#define GXOS_TLS_MBEDTLS_PSA_UTIL_HEADER_PRESENT 1
#endif
#if __has_include("third_party/mbedtls/tf-psa-crypto/drivers/builtin/src/md_psa.h")
#define GXOS_TLS_MBEDTLS_MD_PSA_HEADER_PRESENT 1
#endif
#if __has_include("third_party/mbedtls/tf-psa-crypto/include/tf-psa-crypto/build_info.h")
#define GXOS_TLS_MBEDTLS_TF_PSA_BUILD_INFO_PRESENT 1
#endif
#if __has_include("third_party/mbedtls/tf-psa-crypto/include/psa/crypto_config.h")
#define GXOS_TLS_MBEDTLS_TF_PSA_CRYPTO_CONFIG_PRESENT 1
#endif
#if __has_include("third_party/mbedtls/tf-psa-crypto/core/psa_crypto.c")
#define GXOS_TLS_MBEDTLS_TF_PSA_CORE_PRESENT 1
#endif
#if __has_include("third_party/mbedtls/tf-psa-crypto/core/psa_crypto_driver_wrappers.h")
#define GXOS_TLS_MBEDTLS_TF_PSA_WRAPPERS_PRESENT 1
#endif
#if __has_include("third_party/mbedtls/library/mbedtls_config_check_before.h")
#define GXOS_TLS_MBEDTLS_CONFIG_CHECKS_PRESENT 1
#endif
#if __has_include("third_party/mbedtls/tf-psa-crypto/core/tf_psa_crypto_config_check_before.h")
#define GXOS_TLS_MBEDTLS_TF_PSA_CONFIG_CHECKS_PRESENT 1
#endif
#if __has_include("third_party/mbedtls/guidexos/mbedtls_config.h")
#define GXOS_TLS_MBEDTLS_CONFIG_PRESENT 1
#endif
#if __has_include("third_party/mbedtls/guidexos/crypto_config.h")
#define GXOS_TLS_MBEDTLS_CRYPTO_CONFIG_PRESENT 1
#endif
#if defined(GXOS_TLS_MBEDTLS_SOURCE_PRESENT) && defined(GXOS_TLS_MBEDTLS_TF_PSA_BUILD_INFO_PRESENT)
#define GXOS_TLS_MBEDTLS_VERSION_INCLUDED 1
#define GXOS_TLS_TF_PSA_VERSION_INCLUDED 1
#include "third_party/mbedtls/include/mbedtls/version.h"
#include "third_party/mbedtls/tf-psa-crypto/include/tf-psa-crypto/build_info.h"
#endif
#endif

#if defined(GXOS_BARE_METAL) && \
    GXOS_TLS_MBEDTLS_SOURCE_PRESENT && \
    GXOS_TLS_MBEDTLS_PLATFORM_HEADER_PRESENT && \
    GXOS_TLS_MBEDTLS_PLATFORM_UTIL_HEADER_PRESENT && \
    GXOS_TLS_MBEDTLS_PSA_UTIL_HEADER_PRESENT && \
    GXOS_TLS_MBEDTLS_CONFIG_PRESENT && \
    GXOS_TLS_MBEDTLS_CRYPTO_CONFIG_PRESENT
#define GXOS_TLS_MBEDTLS_RUNTIME_INCLUDED 1
#include "mbedtls/memory_buffer_alloc.h"
#include "mbedtls/ssl.h"
#include "mbedtls/platform.h"
#include "mbedtls/platform_time.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/x509.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"
#include "psa/crypto_extra.h"
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#else
#define GXOS_TLS_MBEDTLS_RUNTIME_INCLUDED 0
#endif

#ifndef GXOS_TLS_MBEDTLS_SOURCE_PRESENT
#define GXOS_TLS_MBEDTLS_SOURCE_PRESENT 0
#endif

#ifndef GXOS_TLS_MBEDTLS_PLATFORM_HEADER_PRESENT
#define GXOS_TLS_MBEDTLS_PLATFORM_HEADER_PRESENT 0
#endif

#ifndef GXOS_TLS_MBEDTLS_PLATFORM_UTIL_HEADER_PRESENT
#define GXOS_TLS_MBEDTLS_PLATFORM_UTIL_HEADER_PRESENT 0
#endif

#ifndef GXOS_TLS_MBEDTLS_CONSTANT_TIME_HEADER_PRESENT
#define GXOS_TLS_MBEDTLS_CONSTANT_TIME_HEADER_PRESENT 0
#endif

#ifndef GXOS_TLS_MBEDTLS_PSA_UTIL_HEADER_PRESENT
#define GXOS_TLS_MBEDTLS_PSA_UTIL_HEADER_PRESENT 0
#endif

#ifndef GXOS_TLS_MBEDTLS_MD_PSA_HEADER_PRESENT
#define GXOS_TLS_MBEDTLS_MD_PSA_HEADER_PRESENT 0
#endif

#ifndef GXOS_TLS_MBEDTLS_TF_PSA_BUILD_INFO_PRESENT
#define GXOS_TLS_MBEDTLS_TF_PSA_BUILD_INFO_PRESENT 0
#endif

#ifndef GXOS_TLS_MBEDTLS_TF_PSA_CRYPTO_CONFIG_PRESENT
#define GXOS_TLS_MBEDTLS_TF_PSA_CRYPTO_CONFIG_PRESENT 0
#endif

#ifndef GXOS_TLS_MBEDTLS_TF_PSA_CORE_PRESENT
#define GXOS_TLS_MBEDTLS_TF_PSA_CORE_PRESENT 0
#endif

#ifndef GXOS_TLS_MBEDTLS_TF_PSA_WRAPPERS_PRESENT
#define GXOS_TLS_MBEDTLS_TF_PSA_WRAPPERS_PRESENT 0
#endif

#ifndef GXOS_TLS_MBEDTLS_CONFIG_CHECKS_PRESENT
#define GXOS_TLS_MBEDTLS_CONFIG_CHECKS_PRESENT 0
#endif

#ifndef GXOS_TLS_MBEDTLS_TF_PSA_CONFIG_CHECKS_PRESENT
#define GXOS_TLS_MBEDTLS_TF_PSA_CONFIG_CHECKS_PRESENT 0
#endif

#ifndef GXOS_TLS_MBEDTLS_CONFIG_PRESENT
#define GXOS_TLS_MBEDTLS_CONFIG_PRESENT 0
#endif

#ifndef GXOS_TLS_MBEDTLS_CRYPTO_CONFIG_PRESENT
#define GXOS_TLS_MBEDTLS_CRYPTO_CONFIG_PRESENT 0
#endif

#ifndef GXOS_TLS_MBEDTLS_VERSION_INCLUDED
#define GXOS_TLS_MBEDTLS_VERSION_INCLUDED 0
#endif

#ifndef GXOS_TLS_TF_PSA_VERSION_INCLUDED
#define GXOS_TLS_TF_PSA_VERSION_INCLUDED 0
#endif

#if GXOS_TLS_MBEDTLS_RUNTIME_INCLUDED
extern "C" {
void* gxos_mbedtls_platform_calloc_uninit(size_t, size_t)
{
    return nullptr;
}

void gxos_mbedtls_platform_free_uninit(void*)
{
}

int gxos_mbedtls_platform_snprintf_noop(char* s, size_t n, const char*, ...)
{
    if (s != nullptr && n != 0) s[0] = '\0';
    return 0;
}

int gxos_mbedtls_platform_vsnprintf_noop(char* s, size_t n, const char*, va_list)
{
    if (s != nullptr && n != 0) s[0] = '\0';
    return 0;
}

int gxos_mbedtls_platform_fprintf_noop(FILE*, const char*, ...)
{
    return 0;
}

void gxos_mbedtls_platform_exit_noop(int)
{
}

void gxos_tls_smoke_debug_trace(const char* event, uint32_t value)
{
    (void) event;
    (void) value;
}
}
#endif

#if GXOS_TLS_MBEDTLS_RUNTIME_INCLUDED
extern "C" void mbedtls_platform_zeroize(void* buf, size_t len)
{
    volatile unsigned char* bytes = static_cast<volatile unsigned char*>(buf);
    while (len-- > 0) {
        *bytes++ = 0;
    }
}

extern "C" void mbedtls_zeroize_and_free(void* buf, size_t len)
{
    if (buf != nullptr) {
        mbedtls_platform_zeroize(buf, len);
        mbedtls_free(buf);
    }
}

static bool gxos_mbedtls_is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int gxos_mbedtls_days_in_month(int year, int month)
{
    static const int kDays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month == 2 && gxos_mbedtls_is_leap_year(year)) return 29;
    return kDays[month - 1];
}

static bool gxos_mbedtls_utc_tm_from_unix_seconds(long long seconds, struct tm* out_tm)
{
    if (!out_tm || seconds < 0) return false;

    long long days = seconds / 86400ll;
    long long rem = seconds % 86400ll;
    if (rem < 0) {
        rem += 86400ll;
        --days;
    }

    out_tm->tm_hour = static_cast<int>(rem / 3600ll);
    rem %= 3600ll;
    out_tm->tm_min = static_cast<int>(rem / 60ll);
    out_tm->tm_sec = static_cast<int>(rem % 60ll);

    int year = 1970;
    while (true) {
        const int year_days = gxos_mbedtls_is_leap_year(year) ? 366 : 365;
        if (days < year_days) break;
        days -= year_days;
        ++year;
    }

    int month = 1;
    while (true) {
        const int month_days = gxos_mbedtls_days_in_month(year, month);
        if (days < month_days) break;
        days -= month_days;
        ++month;
    }

    out_tm->tm_year = year - 1900;
    out_tm->tm_mon = month - 1;
    out_tm->tm_mday = static_cast<int>(days) + 1;
    out_tm->tm_wday = static_cast<int>((4 + (seconds / 86400ll)) % 7ll);
    if (out_tm->tm_wday < 0) out_tm->tm_wday += 7;
    out_tm->tm_yday = 0;
    for (int m = 1; m < month; ++m) {
        out_tm->tm_yday += gxos_mbedtls_days_in_month(year, m);
    }
    out_tm->tm_yday += out_tm->tm_mday - 1;
    out_tm->tm_isdst = 0;
    return true;
}

extern "C" mbedtls_time_t gxos_mbedtls_time_callback(mbedtls_time_t* timer)
{
    int64_t seconds = 0;
    if (!gxos::gxos_wall_clock_unix_seconds(&seconds)) {
        if (timer) *timer = static_cast<mbedtls_time_t>(0);
        return static_cast<mbedtls_time_t>(0);
    }

    const mbedtls_time_t value = static_cast<mbedtls_time_t>(seconds);
    if (timer) *timer = value;
    return value;
}

extern "C" mbedtls_ms_time_t mbedtls_ms_time(void)
{
    int64_t seconds = 0;
    if (!gxos::gxos_wall_clock_unix_seconds(&seconds) || seconds <= 0) {
        return static_cast<mbedtls_ms_time_t>(0);
    }

    return static_cast<mbedtls_ms_time_t>(seconds) * 1000;
}

extern "C" struct tm* mbedtls_platform_gmtime_r(const mbedtls_time_t* tt, struct tm* tm_buf)
{
    if (!tt || !tm_buf) return nullptr;
    return gxos_mbedtls_utc_tm_from_unix_seconds(static_cast<long long>(*tt), tm_buf) ? tm_buf : nullptr;
}

extern "C" psa_status_t mbedtls_psa_external_get_random(
    mbedtls_psa_external_random_context_t*,
    uint8_t* output,
    size_t output_size,
    size_t* output_length)
{
    if (!output_length) return PSA_ERROR_INVALID_ARGUMENT;
    *output_length = 0;

    if ((output == nullptr && output_size != 0) || gxos::gxos_random_quality() != gxos::GxosRandomQuality::Secure) {
        return PSA_ERROR_INSUFFICIENT_ENTROPY;
    }
    if (!gxos::gxos_random_bytes(output, output_size)) {
        return PSA_ERROR_HARDWARE_FAILURE;
    }

    *output_length = output_size;
    return PSA_SUCCESS;
}

extern "C" int gxos_mbedtls_ssl_random(void*, unsigned char* output, size_t output_len)
{
    if ((output == nullptr && output_len != 0) || gxos::gxos_random_quality() != gxos::GxosRandomQuality::Secure) {
        return -1;
    }
    return gxos::gxos_random_bytes(output, output_len) ? 0 : -1;
}
#endif

namespace gxos {
namespace {

constexpr const char* kBareMetalCaBundlePath = "/certs/ca-bundle.pem";
constexpr const char* kBareMetalCaBundleManifestPath = "/certs/ca-bundle.manifest";
constexpr const char* kBareMetalUserCaBundlePath = "/config/certs/ca-bundle.pem";
constexpr const char* kBareMetalUserCaBundleManifestPath = "/config/certs/ca-bundle.manifest";
constexpr const char* kBareMetalHttpsPolicyPath = "/config/navigator/https-policy.txt";
constexpr const char* kBareMetalCaBundleManifestCompatPath = "/certs/CABUNDLE.MAN";
constexpr const char* kBareMetalUserCaBundleManifestCompatPath = "/config/certs/CABUNDLE.MAN";
constexpr const char* kBareMetalUserCaBundleCompatPath = "/config/certs/CABUNDLE.PEM";
constexpr const char* kBareMetalHttpsPolicyCompatPath = "/config/navigator/HTTPSPOL.TXT";
constexpr const char* kBareMetalMissingCaProbePath = "/certs/ca-bundle.missing";
constexpr const char* kBareMetalSmokeFixtureMarker = "guideXOS Navigator smoke-only root CA fixture";
constexpr const char* kBareMetalPublicInternetTrustMarker = "guideXOS Navigator real public HTTPS probe trust bundle";
constexpr const char* kBareMetalPublicInternetTrustOptInPath = "/config/navigator/real-public-https-ca-bundle-enabled.txt";
constexpr const char* kBareMetalPublicInternetTrustOptInCompatPath = "/config/navigator/RPUBCAEN.TXT";
constexpr const char* kHostedCaBundlePath = "(Windows trust store)";
constexpr const char* kHostedTrustStoreDetail = "Windows system trust store managed by Schannel";
constexpr const char* kSmokeFixtureTrustStoreDetail = "Navigator smoke fixture staged at /certs/ca-bundle.pem";
constexpr const char* kUserProvidedTrustStoreDetail = "User-provided trust store loaded from /config/certs/ca-bundle.pem";
constexpr const char* kProductionPublicProbeTrustStoreDetail =
    "Production public-probe trust bundle loaded from /certs/ca-bundle.pem via explicit opt-in public-root staging";
constexpr const char* kShippedRootCandidateTrustStoreDetail =
    "Shipped-root candidate trust bundle loaded from /certs/ca-bundle.pem for reviewed runtime verification only.";
constexpr const char* kProductionRootStoreTrustStoreDetail =
    "Production root store loaded from /certs/ca-bundle.pem.";
constexpr const char* kNoTrustStoreDetail = "No bare-metal trust store is provisioned at the selected CA bundle path.";
constexpr const char* kCaBundleManifestSchemaVersion = "guidexos.navigator.ca-bundle-manifest.v0.1";
constexpr const char* kShippedRootCandidateBundleType = "shipped-root-candidate";
constexpr const char* kLocalSmokeOnlyPolicyBlocker =
    "Broad validated Navigator https:// remains disabled until a non-smoke trust store policy is explicitly enabled.";
constexpr const char* kBareMetalHttpsPolicyDefaultSource = "default-safe policy (no /config/navigator/https-policy.txt)";
constexpr const char* kBareMetalHttpsPolicyFileSource = "VFS config file /config/navigator/https-policy.txt";
constexpr const char* kHostedHttpsPolicySource = "hosted Schannel default";
constexpr const char* kBareMetalMbedTlsImportPath = "third_party/mbedtls";
constexpr const char* kBareMetalMbedTlsExpectedVersion =
    "official Mbed TLS 4.1.0 source tree with populated TF-PSA-Crypto dependency";
constexpr const char* kBareMetalConfigPath = "third_party/mbedtls/guidexos/mbedtls_config.h";
constexpr const char* kBareMetalCryptoConfigPath = "third_party/mbedtls/guidexos/crypto_config.h";
constexpr const char* kBareMetalTfPsaPath = "third_party/mbedtls/tf-psa-crypto";
constexpr const char* kBareMetalBuildPlanPath = "third_party/mbedtls/guidexos/mbedtls_sources.mk";
constexpr const char* kBareMetalPlannedSubset =
    "runtime-linked Mbed TLS 4.1.0 TLS/X.509 subset with bounded allocator hooks, PSA external RNG, wall-clock callbacks, one-shot CA parsing, smoke-only local handshake coverage, and explicit-policy validated HTTPS enablement.";
constexpr size_t kBareMetalPlannedSourceCount = 55;

size_t token_length(const char* token)
{
    if (!token) return 0;
    size_t len = 0;
    while (token[len]) ++len;
    return len;
}

bool text_equals(const char* a, const char* b)
{
    if (!a || !b) return false;
    size_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return false;
        ++i;
    }
    return a[i] == '\0' && b[i] == '\0';
}

bool text_equals_insensitive(const char* a, const char* b)
{
    if (!a || !b) return false;
    size_t i = 0;
    while (a[i] && b[i]) {
        if (web::httpSharedLower(a[i]) != web::httpSharedLower(b[i])) return false;
        ++i;
    }
    return a[i] == '\0' && b[i] == '\0';
}

void copy_trimmed_ascii_span(char* dst, size_t dstSize, const char* begin, const char* end)
{
    if (!dst || dstSize == 0) return;
    dst[0] = '\0';
    if (!begin || !end || end <= begin) return;

    while (begin < end &&
        (*begin == ' ' || *begin == '\t' || *begin == '\r' || *begin == '\n')) {
        ++begin;
    }
    while (end > begin &&
        (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        --end;
    }

    size_t count = 0;
    while (begin + count < end && count + 1 < dstSize) {
        const char ch = begin[count];
        if (static_cast<unsigned char>(ch) < 0x20 || static_cast<unsigned char>(ch) > 0x7e) {
            break;
        }
        dst[count] = ch;
        ++count;
    }
    dst[count] = '\0';
}

void copy_text(char* dst, size_t dst_size, const char* src);

bool is_decimal_digit(char ch)
{
    return ch >= '0' && ch <= '9';
}

bool is_lower_hex_char(char ch)
{
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
}

bool is_upper_hex_char(char ch)
{
    return ch >= 'A' && ch <= 'F';
}

char lower_ascii_hex(char ch)
{
    if (is_upper_hex_char(ch)) {
        return static_cast<char>(ch - 'A' + 'a');
    }
    return ch;
}

void bytes_to_lower_hex(const uint8_t* bytes, size_t byteCount, char* dst, size_t dstSize)
{
    static constexpr char kHex[] = "0123456789abcdef";
    if (!dst || dstSize == 0) return;
    dst[0] = '\0';
    if (!bytes || dstSize < (byteCount * 2u + 1u)) return;

    for (size_t i = 0; i < byteCount; ++i) {
        dst[i * 2u] = kHex[(bytes[i] >> 4) & 0x0F];
        dst[i * 2u + 1u] = kHex[bytes[i] & 0x0F];
    }
    dst[byteCount * 2u] = '\0';
}

bool normalize_sha256_hex_in_place(char* text)
{
    if (!text) return false;
    size_t length = 0;
    while (text[length] != '\0') {
        text[length] = lower_ascii_hex(text[length]);
        if (!is_lower_hex_char(text[length])) {
            return false;
        }
        ++length;
    }
    return length == 64u;
}

bool json_is_whitespace(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

void json_skip_whitespace(const char*& cursor, const char* end)
{
    while (cursor < end && json_is_whitespace(*cursor)) {
        ++cursor;
    }
}

bool json_consume_literal(const char*& cursor, const char* end, const char* literal)
{
    if (!literal) return false;
    const char* probe = cursor;
    while (*literal != '\0') {
        if (probe >= end || *probe != *literal) {
            return false;
        }
        ++probe;
        ++literal;
    }
    cursor = probe;
    return true;
}

bool json_parse_string(const char*& cursor, const char* end, char* dst, size_t dstSize)
{
    if (!dst || dstSize == 0 || cursor >= end || *cursor != '"') return false;
    dst[0] = '\0';
    ++cursor;

    size_t written = 0;
    while (cursor < end) {
        char ch = *cursor++;
        if (ch == '"') {
            dst[written] = '\0';
            return true;
        }
        if (ch == '\\') {
            if (cursor >= end) return false;
            const char esc = *cursor++;
            switch (esc) {
            case '"': ch = '"'; break;
            case '\\': ch = '\\'; break;
            case '/': ch = '/'; break;
            case 'b': ch = '\b'; break;
            case 'f': ch = '\f'; break;
            case 'n': ch = '\n'; break;
            case 'r': ch = '\r'; break;
            case 't': ch = '\t'; break;
            case 'u':
                for (int i = 0; i < 4; ++i) {
                    if (cursor >= end) return false;
                    const char hex = *cursor++;
                    if (!is_decimal_digit(hex) &&
                        !(hex >= 'a' && hex <= 'f') &&
                        !(hex >= 'A' && hex <= 'F')) {
                        return false;
                    }
                }
                ch = '?';
                break;
            default:
                return false;
            }
        }
        if (written + 1u >= dstSize) return false;
        dst[written++] = ch;
    }
    return false;
}

bool json_skip_string(const char*& cursor, const char* end)
{
    if (cursor >= end || *cursor != '"') return false;
    ++cursor;
    while (cursor < end) {
        const char ch = *cursor++;
        if (ch == '"') {
            return true;
        }
        if (ch == '\\') {
            if (cursor >= end) return false;
            const char esc = *cursor++;
            if (esc == 'u') {
                for (int i = 0; i < 4; ++i) {
                    if (cursor >= end) return false;
                    const char hex = *cursor++;
                    if (!is_decimal_digit(hex) &&
                        !(hex >= 'a' && hex <= 'f') &&
                        !(hex >= 'A' && hex <= 'F')) {
                        return false;
                    }
                }
            }
        }
    }
    return false;
}

bool json_parse_unsigned(const char*& cursor, const char* end, uint64_t* valueOut)
{
    if (!valueOut || cursor >= end || !is_decimal_digit(*cursor)) return false;

    uint64_t value = 0;
    while (cursor < end && is_decimal_digit(*cursor)) {
        value = value * 10u + static_cast<uint64_t>(*cursor - '0');
        ++cursor;
    }
    *valueOut = value;
    return true;
}

bool json_skip_number(const char*& cursor, const char* end)
{
    if (cursor >= end) return false;
    const char* start = cursor;
    if (*cursor == '-') ++cursor;
    if (cursor >= end || !is_decimal_digit(*cursor)) return false;
    while (cursor < end && is_decimal_digit(*cursor)) ++cursor;
    if (cursor < end && *cursor == '.') {
        ++cursor;
        if (cursor >= end || !is_decimal_digit(*cursor)) return false;
        while (cursor < end && is_decimal_digit(*cursor)) ++cursor;
    }
    if (cursor < end && (*cursor == 'e' || *cursor == 'E')) {
        ++cursor;
        if (cursor < end && (*cursor == '+' || *cursor == '-')) ++cursor;
        if (cursor >= end || !is_decimal_digit(*cursor)) return false;
        while (cursor < end && is_decimal_digit(*cursor)) ++cursor;
    }
    return cursor > start;
}

bool json_skip_value(const char*& cursor, const char* end, int depth);

bool json_skip_array(const char*& cursor, const char* end, int depth)
{
    if (cursor >= end || *cursor != '[' || depth > 8) return false;
    ++cursor;
    json_skip_whitespace(cursor, end);
    if (cursor < end && *cursor == ']') {
        ++cursor;
        return true;
    }
    while (cursor < end) {
        if (!json_skip_value(cursor, end, depth + 1)) return false;
        json_skip_whitespace(cursor, end);
        if (cursor >= end) return false;
        if (*cursor == ']') {
            ++cursor;
            return true;
        }
        if (*cursor != ',') return false;
        ++cursor;
        json_skip_whitespace(cursor, end);
    }
    return false;
}

bool json_skip_object(const char*& cursor, const char* end, int depth)
{
    if (cursor >= end || *cursor != '{' || depth > 8) return false;
    ++cursor;
    json_skip_whitespace(cursor, end);
    if (cursor < end && *cursor == '}') {
        ++cursor;
        return true;
    }
    while (cursor < end) {
        if (!json_skip_string(cursor, end)) return false;
        json_skip_whitespace(cursor, end);
        if (cursor >= end || *cursor != ':') return false;
        ++cursor;
        json_skip_whitespace(cursor, end);
        if (!json_skip_value(cursor, end, depth + 1)) return false;
        json_skip_whitespace(cursor, end);
        if (cursor >= end) return false;
        if (*cursor == '}') {
            ++cursor;
            return true;
        }
        if (*cursor != ',') return false;
        ++cursor;
        json_skip_whitespace(cursor, end);
    }
    return false;
}

bool json_skip_value(const char*& cursor, const char* end, int depth)
{
    json_skip_whitespace(cursor, end);
    if (cursor >= end || depth > 8) return false;
    if (*cursor == '"') return json_skip_string(cursor, end);
    if (*cursor == '{') return json_skip_object(cursor, end, depth);
    if (*cursor == '[') return json_skip_array(cursor, end, depth);
    if (*cursor == '-' || is_decimal_digit(*cursor)) return json_skip_number(cursor, end);
    if (json_consume_literal(cursor, end, "true")) return true;
    if (json_consume_literal(cursor, end, "false")) return true;
    if (json_consume_literal(cursor, end, "null")) return true;
    return false;
}

bool json_parse_yes_no_or_bool(const char*& cursor, const char* end, bool* valueOut)
{
    if (!valueOut) return false;
    if (cursor >= end) return false;

    if (*cursor == '"') {
        char token[8];
        if (!json_parse_string(cursor, end, token, sizeof(token))) return false;
        if (text_equals_insensitive(token, "yes") || text_equals_insensitive(token, "true")) {
            *valueOut = true;
            return true;
        }
        if (text_equals_insensitive(token, "no") || text_equals_insensitive(token, "false")) {
            *valueOut = false;
            return true;
        }
        return false;
    }
    if (json_consume_literal(cursor, end, "true")) {
        *valueOut = true;
        return true;
    }
    if (json_consume_literal(cursor, end, "false")) {
        *valueOut = false;
        return true;
    }
    return false;
}

struct ParsedCaBundleManifest {
    bool hasSchemaVersion = false;
    bool hasBundleType = false;
    bool hasSha256 = false;
    bool hasRootCount = false;
    bool hasPemBytes = false;
    bool hasProductionReady = false;
    bool hasTestOnly = false;
    char schemaVersion[64] = {};
    char bundleType[64] = {};
    char rotationId[96] = {};
    char sha256[65] = {};
    uint64_t rootCount = 0;
    uint64_t pemBytes = 0;
    bool productionReady = false;
    bool testOnly = false;
};

bool parse_ca_bundle_manifest_json(const uint8_t* bytes,
                                   size_t byteCount,
                                   ParsedCaBundleManifest* manifest,
                                   char* error,
                                   size_t errorSize)
{
    if (!bytes || !manifest || !error || errorSize == 0) return false;
    error[0] = '\0';

    const char* cursor = reinterpret_cast<const char*>(bytes);
    const char* end = cursor + byteCount;
    json_skip_whitespace(cursor, end);
    if (cursor >= end || *cursor != '{') {
        copy_text(error, errorSize, "CA bundle manifest is not a JSON object.");
        return false;
    }
    ++cursor;
    json_skip_whitespace(cursor, end);

    while (cursor < end && *cursor != '}') {
        char key[64];
        if (!json_parse_string(cursor, end, key, sizeof(key))) {
            copy_text(error, errorSize, "CA bundle manifest has an invalid property name.");
            return false;
        }
        json_skip_whitespace(cursor, end);
        if (cursor >= end || *cursor != ':') {
            copy_text(error, errorSize, "CA bundle manifest is missing ':' after a property name.");
            return false;
        }
        ++cursor;
        json_skip_whitespace(cursor, end);

        if (text_equals(key, "schema_version")) {
            if (!json_parse_string(cursor, end, manifest->schemaVersion, sizeof(manifest->schemaVersion))) {
                copy_text(error, errorSize, "CA bundle manifest schema_version must be a JSON string.");
                return false;
            }
            manifest->hasSchemaVersion = true;
        } else if (text_equals(key, "bundle_type")) {
            if (!json_parse_string(cursor, end, manifest->bundleType, sizeof(manifest->bundleType))) {
                copy_text(error, errorSize, "CA bundle manifest bundle_type must be a JSON string.");
                return false;
            }
            manifest->hasBundleType = true;
        } else if (text_equals(key, "sha256")) {
            if (!json_parse_string(cursor, end, manifest->sha256, sizeof(manifest->sha256))) {
                copy_text(error, errorSize, "CA bundle manifest sha256 must be a JSON string.");
                return false;
            }
            manifest->hasSha256 = true;
        } else if (text_equals(key, "root_count")) {
            if (!json_parse_unsigned(cursor, end, &manifest->rootCount)) {
                copy_text(error, errorSize, "CA bundle manifest root_count must be an unsigned integer.");
                return false;
            }
            manifest->hasRootCount = true;
        } else if (text_equals(key, "pem_bytes")) {
            if (!json_parse_unsigned(cursor, end, &manifest->pemBytes)) {
                copy_text(error, errorSize, "CA bundle manifest pem_bytes must be an unsigned integer.");
                return false;
            }
            manifest->hasPemBytes = true;
        } else if (text_equals(key, "production_ready")) {
            if (!json_parse_yes_no_or_bool(cursor, end, &manifest->productionReady)) {
                copy_text(error, errorSize, "CA bundle manifest production_ready must be yes/no.");
                return false;
            }
            manifest->hasProductionReady = true;
        } else if (text_equals(key, "test_only")) {
            if (!json_parse_yes_no_or_bool(cursor, end, &manifest->testOnly)) {
                copy_text(error, errorSize, "CA bundle manifest test_only must be yes/no.");
                return false;
            }
            manifest->hasTestOnly = true;
        } else if (text_equals(key, "rotation_id")) {
            if (!json_parse_string(cursor, end, manifest->rotationId, sizeof(manifest->rotationId))) {
                copy_text(error, errorSize, "CA bundle manifest rotation_id must be a JSON string.");
                return false;
            }
        } else {
            if (!json_skip_value(cursor, end, 0)) {
                copy_text(error, errorSize, "CA bundle manifest contains an unsupported JSON value.");
                return false;
            }
        }

        json_skip_whitespace(cursor, end);
        if (cursor >= end) {
            copy_text(error, errorSize, "CA bundle manifest ended unexpectedly.");
            return false;
        }
        if (*cursor == ',') {
            ++cursor;
            json_skip_whitespace(cursor, end);
            continue;
        }
        if (*cursor != '}') {
            copy_text(error, errorSize, "CA bundle manifest is missing ',' or '}'.");
            return false;
        }
    }

    if (cursor >= end || *cursor != '}') {
        copy_text(error, errorSize, "CA bundle manifest is missing its closing '}'.");
        return false;
    }
    ++cursor;
    json_skip_whitespace(cursor, end);
    if (cursor != end) {
        copy_text(error, errorSize, "CA bundle manifest has trailing data.");
        return false;
    }

    if (!manifest->hasSchemaVersion || !text_equals(manifest->schemaVersion, kCaBundleManifestSchemaVersion)) {
        copy_text(error, errorSize, "CA bundle manifest schema_version is missing or unsupported.");
        return false;
    }
    if (!manifest->hasBundleType || manifest->bundleType[0] == '\0') {
        copy_text(error, errorSize, "CA bundle manifest bundle_type is missing.");
        return false;
    }
    if (!manifest->hasSha256 || !normalize_sha256_hex_in_place(manifest->sha256)) {
        copy_text(error, errorSize, "CA bundle manifest sha256 must be a 64-character lowercase hex digest.");
        return false;
    }
    if (!manifest->hasRootCount) {
        copy_text(error, errorSize, "CA bundle manifest root_count is missing.");
        return false;
    }
    if (!manifest->hasPemBytes) {
        copy_text(error, errorSize, "CA bundle manifest pem_bytes is missing.");
        return false;
    }
    if (!manifest->hasProductionReady) {
        copy_text(error, errorSize, "CA bundle manifest production_ready is missing.");
        return false;
    }
    if (!manifest->hasTestOnly) {
        copy_text(error, errorSize, "CA bundle manifest test_only is missing.");
        return false;
    }

    return true;
}

#if defined(GXOS_BARE_METAL)
bool buffer_contains_token(const uint8_t* buffer, size_t buffer_len, const char* token)
{
    const size_t len = token_length(token);
    if (!buffer || len == 0 || buffer_len < len) return false;

    for (size_t i = 0; i + len <= buffer_len; ++i) {
        size_t matched = 0;
        while (matched < len &&
            buffer[i + matched] == static_cast<uint8_t>(token[matched])) {
            ++matched;
        }
        if (matched == len) return true;
    }
    return false;
}

size_t count_token_occurrences(const uint8_t* buffer, size_t buffer_len, const char* token)
{
    const size_t len = token_length(token);
    if (!buffer || len == 0 || buffer_len < len) return 0;

    size_t count = 0;
    for (size_t i = 0; i + len <= buffer_len; ++i) {
        size_t matched = 0;
        while (matched < len &&
            buffer[i + matched] == static_cast<uint8_t>(token[matched])) {
            ++matched;
        }
        if (matched == len) {
            ++count;
            i += len - 1;
        }
    }
    return count;
}

bool is_clock_ready(GxosClockStatus status)
{
    return status == GxosClockStatus::Plausible || status == GxosClockStatus::Verified;
}

const char* detected_mbedtls_version()
{
#if GXOS_TLS_MBEDTLS_VERSION_INCLUDED
#if defined(MBEDTLS_VERSION_STRING_FULL)
    return MBEDTLS_VERSION_STRING_FULL;
#elif defined(MBEDTLS_VERSION_STRING)
    return MBEDTLS_VERSION_STRING;
#else
    return "source-present (version macro unavailable)";
#endif
#elif GXOS_TLS_MBEDTLS_SOURCE_PRESENT
    return "version.h present, but compile-time version import is blocked by missing include paths or split-config prerequisites";
#else
    return "(not imported)";
#endif
}

const char* detected_tf_psa_version()
{
#if GXOS_TLS_TF_PSA_VERSION_INCLUDED
#if defined(TF_PSA_CRYPTO_VERSION_STRING_FULL)
    return TF_PSA_CRYPTO_VERSION_STRING_FULL;
#elif defined(TF_PSA_CRYPTO_VERSION_STRING)
    return TF_PSA_CRYPTO_VERSION_STRING;
#else
    return "source-present (TF-PSA version macro unavailable)";
#endif
#elif GXOS_TLS_MBEDTLS_TF_PSA_BUILD_INFO_PRESENT
    return "build_info.h present, but compile-time TF-PSA version import is blocked by missing split-config prerequisites";
#else
    return "(not imported)";
#endif
}
#endif

constexpr bool kBareMetalCoreCompileHeadersPresent =
    GXOS_TLS_MBEDTLS_PLATFORM_HEADER_PRESENT &&
    GXOS_TLS_MBEDTLS_PLATFORM_UTIL_HEADER_PRESENT &&
    GXOS_TLS_MBEDTLS_CONSTANT_TIME_HEADER_PRESENT &&
    GXOS_TLS_MBEDTLS_PSA_UTIL_HEADER_PRESENT &&
    GXOS_TLS_MBEDTLS_MD_PSA_HEADER_PRESENT &&
    GXOS_TLS_MBEDTLS_CONFIG_CHECKS_PRESENT &&
    GXOS_TLS_MBEDTLS_TF_PSA_CONFIG_CHECKS_PRESENT;

constexpr bool kBareMetalTfPsaDependencyPresent =
    GXOS_TLS_MBEDTLS_TF_PSA_BUILD_INFO_PRESENT &&
    GXOS_TLS_MBEDTLS_TF_PSA_CRYPTO_CONFIG_PRESENT &&
    GXOS_TLS_MBEDTLS_TF_PSA_CORE_PRESENT &&
    GXOS_TLS_MBEDTLS_TF_PSA_WRAPPERS_PRESENT;

constexpr bool kBareMetalSourceReadyForCompile =
    GXOS_TLS_MBEDTLS_SOURCE_PRESENT &&
    kBareMetalCoreCompileHeadersPresent &&
    kBareMetalTfPsaDependencyPresent &&
    GXOS_TLS_MBEDTLS_CONFIG_PRESENT &&
    GXOS_TLS_MBEDTLS_CRYPTO_CONFIG_PRESENT;

GxosTlsMbedTlsImportInfo make_mbedtls_import_info()
{
#if defined(GXOS_BARE_METAL)
    if (!GXOS_TLS_MBEDTLS_SOURCE_PRESENT &&
        !GXOS_TLS_MBEDTLS_CONFIG_PRESENT &&
        !GXOS_TLS_MBEDTLS_CRYPTO_CONFIG_PRESENT) {
        return {
            false,
            false,
            false,
            false,
            false,
            kBareMetalMbedTlsImportPath,
            kBareMetalConfigPath,
            kBareMetalCryptoConfigPath,
            kBareMetalTfPsaPath,
            kBareMetalBuildPlanPath,
            kBareMetalMbedTlsExpectedVersion,
            "(not imported)",
            "(not imported)",
            kBareMetalPlannedSourceCount,
            kBareMetalPlannedSubset,
            "Official Mbed TLS source and the guideXOS 4.x config pair are both missing."
        };
    }
    if (!GXOS_TLS_MBEDTLS_SOURCE_PRESENT) {
        return {
            false,
            false,
            true,
            GXOS_TLS_MBEDTLS_CRYPTO_CONFIG_PRESENT,
            false,
            kBareMetalMbedTlsImportPath,
            kBareMetalConfigPath,
            kBareMetalCryptoConfigPath,
            kBareMetalTfPsaPath,
            kBareMetalBuildPlanPath,
            kBareMetalMbedTlsExpectedVersion,
            "(not imported)",
            "(not imported)",
            kBareMetalPlannedSourceCount,
            kBareMetalPlannedSubset,
            "guideXOS 4.x config scaffolding is present, but the official Mbed TLS 4.x source tree has not been imported yet."
        };
    }
    if (!GXOS_TLS_MBEDTLS_CONFIG_PRESENT || !GXOS_TLS_MBEDTLS_CRYPTO_CONFIG_PRESENT) {
        return {
            true,
            false,
            GXOS_TLS_MBEDTLS_CONFIG_PRESENT,
            GXOS_TLS_MBEDTLS_CRYPTO_CONFIG_PRESENT,
            kBareMetalTfPsaDependencyPresent,
            kBareMetalMbedTlsImportPath,
            kBareMetalConfigPath,
            kBareMetalCryptoConfigPath,
            kBareMetalTfPsaPath,
            kBareMetalBuildPlanPath,
            kBareMetalMbedTlsExpectedVersion,
            detected_mbedtls_version(),
            detected_tf_psa_version(),
            kBareMetalPlannedSourceCount,
            kBareMetalPlannedSubset,
            "Official Mbed TLS 4.x headers were found, but the guideXOS split config pair is incomplete."
        };
    }
    if (!kBareMetalCoreCompileHeadersPresent || !kBareMetalTfPsaDependencyPresent) {
        return {
            true,
            false,
            true,
            true,
            kBareMetalTfPsaDependencyPresent,
            kBareMetalMbedTlsImportPath,
            kBareMetalConfigPath,
            kBareMetalCryptoConfigPath,
            kBareMetalTfPsaPath,
            kBareMetalBuildPlanPath,
            kBareMetalMbedTlsExpectedVersion,
            detected_mbedtls_version(),
            detected_tf_psa_version(),
            kBareMetalPlannedSourceCount,
            kBareMetalPlannedSubset,
            "Mbed TLS 4.x source import is incomplete for the freestanding runtime-linked subset: required helper headers, generated config-check headers, or TF-PSA-Crypto support files are missing."
        };
    }
    return {
        true,
        true,
        true,
        true,
        true,
        kBareMetalMbedTlsImportPath,
        kBareMetalConfigPath,
        kBareMetalCryptoConfigPath,
        kBareMetalTfPsaPath,
        kBareMetalBuildPlanPath,
        kBareMetalMbedTlsExpectedVersion,
        detected_mbedtls_version(),
        detected_tf_psa_version(),
        kBareMetalPlannedSourceCount,
        kBareMetalPlannedSubset,
        "Official Mbed TLS 4.1.0 source, generated runtime helpers, and the guideXOS split config are present; the freestanding runtime-linked subset can build while general Navigator https:// remains gated."
    };
#else
    return {
        false,
        false,
        false,
        false,
        false,
        "(not applicable in hosted Schannel mode)",
        "(not applicable in hosted Schannel mode)",
        "(not applicable in hosted Schannel mode)",
        "(not applicable in hosted Schannel mode)",
        "(not applicable in hosted Schannel mode)",
        "(not applicable in hosted Schannel mode)",
        "Schannel hosted",
        "(not applicable in hosted Schannel mode)",
        0,
        "Hosted Navigator does not compile the bare-metal Mbed TLS subset.",
        "Hosted Navigator uses Schannel; Mbed TLS import scaffolding is bare-metal only."
    };
#endif
}

void copy_text(char* dst, size_t dst_size, const char* src)
{
    if (!dst || dst_size == 0) return;
    if (!src) src = "";

    size_t i = 0;
    while (i + 1 < dst_size && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

void zero_local_handshake_result(GxosTlsLocalHandshakeResult* result)
{
    if (!result) return;
    result->attempted = false;
    result->tcpConnected = false;
    result->handshakeSuccess = false;
    result->certificateValidationSuccess = false;
    result->hostnameValidationSuccess = false;
    result->requestWriteSuccess = false;
    result->responseReadSuccess = false;
    result->parserAcceptedResponse = false;
    result->usedSniHostname = false;
    result->requestBytesWritten = 0;
    result->responseBytesRead = 0;
    result->verifyFlags = 0;
    result->transportError = 0;
    result->mbedtlsError = 0;
    result->mbedtlsState = 0;
    result->transportStatus = gxos::web::HttpByteStreamTlsStatus::NotStarted;
    result->sniHost[0] = '\0';
    result->stage[0] = '\0';
    result->protocol[0] = '\0';
    result->cipherSuite[0] = '\0';
    result->error[0] = '\0';
}

#if defined(GXOS_BARE_METAL)

struct BareMetalTlsRuntimeState {
    bool hooksAttempted = false;
    bool allocatorInitialized = false;
    bool psaInitAttempted = false;
    bool psaInitialized = false;
    bool caChainInitialized = false;
    bool allocatorExhausted = false;
    GxosTlsHookStatus allocatorStatus = GxosTlsHookStatus::Pending;
    GxosTlsHookStatus rngStatus = GxosTlsHookStatus::Pending;
    GxosTlsHookStatus timeStatus = GxosTlsHookStatus::Pending;
    GxosTlsHookStatus psaStatus = GxosTlsHookStatus::Pending;
    char allocatorDetail[160] = "Allocator hook has not been attempted yet.";
    char rngDetail[160] = "RNG callback has not been evaluated yet.";
    char timeDetail[160] = "Time callback has not been evaluated yet.";
    char psaDetail[160] = "psa_crypto_init() is deferred until CA parsing starts.";
    uint8_t arena[kGxosTlsArenaCapacityBytes] = {};
    uint8_t bytes[kGxosMaxCaStoreBytes + 1] = {};
#if GXOS_TLS_MBEDTLS_RUNTIME_INCLUDED
    mbedtls_x509_crt caChain;
#endif
};

struct BareMetalCaStoreState {
    bool attempted = false;
    GxosCaStoreInfo info{
        GxosCaStoreStatus::Missing,
        GxosCaParseStatus::NotAttempted,
        0,
        0,
        0,
        false,
        kBareMetalCaBundlePath,
        "Root CA bundle has not been checked yet.",
        {
            GxosCaManifestStatus::Missing,
            nullptr,
            0,
            false,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            0,
            0,
            false,
            false,
            false,
            "CA bundle manifest has not been checked yet."
        }
    };
    char manifestPath[96] = {};
    char manifestSchemaVersion[64] = {};
    char manifestBundleType[64] = {};
    char manifestRotationId[96] = {};
    char manifestSha256[65] = {};
    char computedSha256[65] = {};
    char manifestError[160] = {};
    uint8_t manifestBytes[kGxosMaxCaManifestBytes + 1] = {};
};

struct BareMetalHttpsPolicyConfigInfo {
    bool explicitSelection = false;
    bool invalid = false;
    GxosValidatedHttpsPolicyState selectedState = GxosValidatedHttpsPolicyState::Disabled;
    bool publicHttpsPilotRequested = false;
    const char* configPath = kBareMetalHttpsPolicyPath;
    const char* configSource = kBareMetalHttpsPolicyDefaultSource;
    const char* error = nullptr;
};

struct BareMetalHttpsPolicyConfigState {
    bool attempted = false;
    BareMetalHttpsPolicyConfigInfo info{};
    char token[64] = {};
    char key[64] = {};
    char value[64] = {};
    char error[160] = {};
};

BareMetalTlsRuntimeState& runtime_state()
{
    static BareMetalTlsRuntimeState state;
    return state;
}

bool is_smoke_only_ca_fixture(const uint8_t* buffer, size_t buffer_len)
{
    return buffer_contains_token(buffer, buffer_len, kBareMetalSmokeFixtureMarker);
}

bool is_public_internet_trust_bundle(const uint8_t* buffer, size_t buffer_len)
{
    return buffer_contains_token(buffer, buffer_len, kBareMetalPublicInternetTrustMarker);
}

BareMetalCaStoreState& ca_store_state()
{
    static BareMetalCaStoreState state;
    return state;
}

BareMetalHttpsPolicyConfigState& https_policy_config_state()
{
    static BareMetalHttpsPolicyConfigState state;
    return state;
}

void reset_ca_manifest_info(BareMetalCaStoreState& state, const char* manifestPath)
{
    state.manifestPath[0] = '\0';
    state.manifestSchemaVersion[0] = '\0';
    state.manifestBundleType[0] = '\0';
    state.manifestRotationId[0] = '\0';
    state.manifestSha256[0] = '\0';
    state.computedSha256[0] = '\0';
    state.manifestError[0] = '\0';
    state.manifestBytes[0] = 0;

    if (manifestPath) {
        copy_text(state.manifestPath, sizeof(state.manifestPath), manifestPath);
    }

    state.info.manifest = {
        GxosCaManifestStatus::Missing,
        state.manifestPath[0] ? state.manifestPath : nullptr,
        0,
        false,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        0,
        0,
        false,
        false,
        false,
        "CA bundle manifest has not been checked yet."
    };
}

void set_ca_manifest_error(BareMetalCaStoreState& state,
                           GxosCaManifestStatus status,
                           bool present,
                           const char* message)
{
    state.info.manifest.status = status;
    state.info.manifest.present = present;
    copy_text(state.manifestError, sizeof(state.manifestError), message);
    state.info.manifest.error = state.manifestError;
}

const char* fallback_path_if_missing(const char* primaryPath,
                                     const char* compatPath,
                                     kernel::vfs::FileInfo* info,
                                     kernel::vfs::Status* statStatus);

bool compute_loaded_ca_bundle_sha256(const uint8_t* bytes,
                                     size_t byteCount,
                                     char* outHex,
                                     size_t outHexSize)
{
#if GXOS_TLS_MBEDTLS_RUNTIME_INCLUDED
    if (!bytes || !outHex || outHexSize < 65u) return false;
    uint8_t digest[32] = {};
    size_t digestLength = 0;
    const psa_status_t status = psa_hash_compute(
        PSA_ALG_SHA_256,
        bytes,
        byteCount,
        digest,
        sizeof(digest),
        &digestLength);
    if (status != PSA_SUCCESS || digestLength != sizeof(digest)) {
        outHex[0] = '\0';
        return false;
    }
    bytes_to_lower_hex(digest, sizeof(digest), outHex, outHexSize);
    return true;
#else
    (void)outHex;
    (void)outHexSize;
    return false;
#endif
}

bool load_selected_ca_bundle_manifest(BareMetalCaStoreState& state,
                                      const char* manifestPath,
                                      const char* compatManifestPath)
{
    reset_ca_manifest_info(state, manifestPath);

    if (!manifestPath || !manifestPath[0]) {
        set_ca_manifest_error(state, GxosCaManifestStatus::Missing, false,
            "CA bundle manifest path is unavailable.");
        return false;
    }

    kernel::vfs::FileInfo manifestFileInfo{};
    kernel::vfs::Status manifestStatus = kernel::vfs::VFS_ERR_INVALID;
    const char* manifestReadPath = fallback_path_if_missing(
        manifestPath,
        compatManifestPath,
        &manifestFileInfo,
        &manifestStatus);
    if (manifestStatus == kernel::vfs::VFS_ERR_NOT_FOUND ||
        manifestStatus == kernel::vfs::VFS_ERR_NOT_MOUNT) {
        char buffer[160];
        copy_text(buffer, sizeof(buffer), "CA bundle manifest not found at ");
        size_t used = token_length(buffer);
        if (used + token_length(manifestPath) + 2u < sizeof(buffer)) {
            copy_text(buffer + used, sizeof(buffer) - used, manifestPath);
            used = token_length(buffer);
            copy_text(buffer + used, sizeof(buffer) - used, ".");
        }
        set_ca_manifest_error(state, GxosCaManifestStatus::Missing, false, buffer);
        return false;
    }
    if (manifestStatus != kernel::vfs::VFS_OK) {
        set_ca_manifest_error(state, GxosCaManifestStatus::ReadError, false,
            "CA bundle manifest could not be stat()'d through the VFS.");
        return false;
    }
    if (manifestFileInfo.type != kernel::vfs::FILE_TYPE_REGULAR) {
        set_ca_manifest_error(state, GxosCaManifestStatus::Invalid, true,
            "CA bundle manifest path does not point to a regular file.");
        return false;
    }
    if (manifestFileInfo.size == 0) {
        set_ca_manifest_error(state, GxosCaManifestStatus::Invalid, true,
            "CA bundle manifest is empty.");
        return false;
    }
    if (manifestFileInfo.size > static_cast<uint64_t>(kGxosMaxCaManifestBytes)) {
        set_ca_manifest_error(state, GxosCaManifestStatus::TooLarge, true,
            "CA bundle manifest exceeds the 16 KiB safety cap.");
        return false;
    }

    const int32_t manifestBytesRead = kernel::vfs::read_file(
        manifestReadPath,
        state.manifestBytes,
        static_cast<uint32_t>(kGxosMaxCaManifestBytes));
    if (manifestBytesRead < 0 || static_cast<uint64_t>(manifestBytesRead) != manifestFileInfo.size) {
        set_ca_manifest_error(state, GxosCaManifestStatus::ReadError, true,
            "CA bundle manifest read did not complete successfully.");
        return false;
    }

    const size_t manifestLoaded = static_cast<size_t>(manifestBytesRead);
    state.manifestBytes[manifestLoaded] = 0;
    state.info.manifest.status = GxosCaManifestStatus::Loaded;
    state.info.manifest.bytesLoaded = manifestLoaded;
    state.info.manifest.present = true;

    ParsedCaBundleManifest parsed{};
    if (!parse_ca_bundle_manifest_json(state.manifestBytes,
            manifestLoaded,
            &parsed,
            state.manifestError,
            sizeof(state.manifestError))) {
        state.info.manifest.status = GxosCaManifestStatus::Invalid;
        state.info.manifest.error = state.manifestError;
        return false;
    }

    copy_text(state.manifestSchemaVersion, sizeof(state.manifestSchemaVersion), parsed.schemaVersion);
    copy_text(state.manifestBundleType, sizeof(state.manifestBundleType), parsed.bundleType);
    copy_text(state.manifestRotationId, sizeof(state.manifestRotationId), parsed.rotationId);
    copy_text(state.manifestSha256, sizeof(state.manifestSha256), parsed.sha256);

    state.info.manifest.schemaVersion = state.manifestSchemaVersion;
    state.info.manifest.bundleType = state.manifestBundleType;
    state.info.manifest.rotationId = state.manifestRotationId[0] ? state.manifestRotationId : nullptr;
    state.info.manifest.manifestSha256 = state.manifestSha256;
    state.info.manifest.rootCount = static_cast<size_t>(parsed.rootCount);
    state.info.manifest.pemBytes = static_cast<size_t>(parsed.pemBytes);
    state.info.manifest.productionReady = parsed.productionReady;
    state.info.manifest.testOnly = parsed.testOnly;
    state.info.manifest.error = nullptr;
    return true;
}

bool hook_ready(GxosTlsHookStatus status)
{
    return status == GxosTlsHookStatus::Ready;
}

#if defined(GXOS_BARE_METAL)
const char* compat_ca_bundle_path_for_policy(GxosValidatedHttpsPolicyState state);
const char* fallback_path_if_missing(const char* primaryPath,
                                     const char* compatPath,
                                     kernel::vfs::FileInfo* info,
                                     kernel::vfs::Status* statStatus);
bool public_internet_trust_opt_in_enabled();
#endif

#if GXOS_TLS_MBEDTLS_RUNTIME_INCLUDED
size_t count_ca_chain(const mbedtls_x509_crt* crt)
{
    size_t count = 0;
    for (const mbedtls_x509_crt* cur = crt; cur != nullptr; cur = cur->next) {
        if (cur->raw.p != nullptr && cur->raw.len != 0) {
            ++count;
        }
    }
    return count;
}
#endif

bool policy_state_supports_user_trust(GxosValidatedHttpsPolicyState state)
{
    return state == GxosValidatedHttpsPolicyState::UserTrustStoreDevMode;
}

GxosValidatedHttpsPolicyState parse_https_policy_token(const char* token, bool* valid)
{
    if (valid) *valid = true;
    if (text_equals_insensitive(token, "disabled")) {
        return GxosValidatedHttpsPolicyState::Disabled;
    }
    if (text_equals_insensitive(token, "local-smoke-only") ||
        text_equals_insensitive(token, "localsmokeonly")) {
        return GxosValidatedHttpsPolicyState::LocalSmokeOnly;
    }
    if (text_equals_insensitive(token, "user-trust-dev-mode") ||
        text_equals_insensitive(token, "usertruststoredevmode") ||
        text_equals_insensitive(token, "user-trust-store-dev-mode")) {
        return GxosValidatedHttpsPolicyState::UserTrustStoreDevMode;
    }
    if (text_equals_insensitive(token, "production-validated") ||
        text_equals_insensitive(token, "productionvalidated")) {
        return GxosValidatedHttpsPolicyState::ProductionValidated;
    }
    if (valid) *valid = false;
    return GxosValidatedHttpsPolicyState::Disabled;
}

BareMetalHttpsPolicyConfigInfo bare_metal_https_policy_config_info()
{
    BareMetalHttpsPolicyConfigState& state = https_policy_config_state();
    if (state.attempted) {
        return state.info;
    }
    state.attempted = true;
    state.info.configPath = kBareMetalHttpsPolicyPath;
    state.info.configSource = kBareMetalHttpsPolicyDefaultSource;
    state.info.selectedState = GxosValidatedHttpsPolicyState::Disabled;

    kernel::vfs::FileInfo info{};
    kernel::vfs::Status statStatus = kernel::vfs::VFS_ERR_INVALID;
    const char* configReadPath = fallback_path_if_missing(
        kBareMetalHttpsPolicyPath,
        kBareMetalHttpsPolicyCompatPath,
        &info,
        &statStatus);
    if (statStatus == kernel::vfs::VFS_ERR_NOT_FOUND || statStatus == kernel::vfs::VFS_ERR_NOT_MOUNT) {
        return state.info;
    }
    if (statStatus != kernel::vfs::VFS_OK) {
        copy_text(state.error, sizeof(state.error),
            "HTTPS policy config could not be stat()'d; falling back to the default-safe policy.");
        state.info.invalid = true;
        state.info.error = state.error;
        return state.info;
    }
    if (info.type != kernel::vfs::FILE_TYPE_REGULAR || info.size == 0 || info.size > 4096u) {
        copy_text(state.error, sizeof(state.error),
            "HTTPS policy config is missing, empty, or too large; falling back to the default-safe policy.");
        state.info.invalid = true;
        state.info.error = state.error;
        return state.info;
    }

    char buffer[4097];
    const int32_t bytesRead = kernel::vfs::read_file(configReadPath,
        reinterpret_cast<uint8_t*>(buffer), 4096u);
    if (bytesRead <= 0) {
        copy_text(state.error, sizeof(state.error),
            "HTTPS policy config could not be read; falling back to the default-safe policy.");
        state.info.invalid = true;
        state.info.error = state.error;
        return state.info;
    }
    buffer[bytesRead < 4096 ? bytesRead : 4096] = '\0';

    bool sawContent = false;
    bool sawPolicy = false;
    bool sawPublicPilot = false;
    GxosValidatedHttpsPolicyState parsedPolicy = GxosValidatedHttpsPolicyState::Disabled;
    const char* cursor = buffer;
    while (*cursor != '\0') {
        const char* lineStart = cursor;
        while (*cursor != '\0' && *cursor != '\r' && *cursor != '\n') {
            ++cursor;
        }
        const char* lineEnd = cursor;
        if (*cursor == '\r') ++cursor;
        if (*cursor == '\n') ++cursor;

        state.token[0] = '\0';
        copy_trimmed_ascii_span(state.token, sizeof(state.token), lineStart, lineEnd);
        if (state.token[0] == '\0') {
            continue;
        }

        sawContent = true;
        const char* equals = nullptr;
        for (const char* scan = state.token; *scan; ++scan) {
            if (*scan == '=') {
                equals = scan;
                break;
            }
        }

        if (!equals) {
            if (sawPolicy) {
                copy_text(state.error, sizeof(state.error),
                    "HTTPS policy config contains multiple policy tokens; falling back to the default-safe policy.");
                state.info.invalid = true;
                state.info.error = state.error;
                return state.info;
            }
            bool valid = false;
            parsedPolicy = parse_https_policy_token(state.token, &valid);
            if (!valid) {
                copy_text(state.error, sizeof(state.error),
                    "HTTPS policy config is invalid; falling back to the default-safe policy.");
                state.info.invalid = true;
                state.info.error = state.error;
                return state.info;
            }
            sawPolicy = true;
            continue;
        }

        copy_trimmed_ascii_span(state.key, sizeof(state.key), state.token, equals);
        const char* tokenEnd = state.token;
        while (*tokenEnd != '\0') ++tokenEnd;
        copy_trimmed_ascii_span(state.value, sizeof(state.value), equals + 1, tokenEnd);
        if (state.key[0] == '\0' || state.value[0] == '\0') {
            copy_text(state.error, sizeof(state.error),
                "HTTPS policy config contains an empty key/value setting; falling back to the default-safe policy.");
            state.info.invalid = true;
            state.info.error = state.error;
            return state.info;
        }

        if (text_equals_insensitive(state.key, "public-https-pilot") ||
            text_equals_insensitive(state.key, "publichttpspilot")) {
            if (sawPublicPilot) {
                copy_text(state.error, sizeof(state.error),
                    "HTTPS policy config repeats public-https-pilot; falling back to the default-safe policy.");
                state.info.invalid = true;
                state.info.error = state.error;
                return state.info;
            }
            if (text_equals_insensitive(state.value, "enabled")) {
                state.info.publicHttpsPilotRequested = true;
            } else if (text_equals_insensitive(state.value, "disabled")) {
                state.info.publicHttpsPilotRequested = false;
            } else {
                copy_text(state.error, sizeof(state.error),
                    "HTTPS policy config has an invalid public-https-pilot value; falling back to the default-safe policy.");
                state.info.invalid = true;
                state.info.error = state.error;
                return state.info;
            }
            sawPublicPilot = true;
            continue;
        }

        copy_text(state.error, sizeof(state.error),
            "HTTPS policy config contains an unknown setting; falling back to the default-safe policy.");
        state.info.invalid = true;
        state.info.error = state.error;
        return state.info;
    }

    if (!sawContent || !sawPolicy) {
        copy_text(state.error, sizeof(state.error),
            "HTTPS policy config is empty after trimming; falling back to the default-safe policy.");
        state.info.invalid = true;
        state.info.error = state.error;
        return state.info;
    }

    if (state.info.publicHttpsPilotRequested &&
        parsedPolicy != GxosValidatedHttpsPolicyState::ProductionValidated) {
        copy_text(state.error, sizeof(state.error),
            "HTTPS policy config is invalid: public-https-pilot requires ProductionValidated.");
        state.info.invalid = true;
        state.info.error = state.error;
        state.info.publicHttpsPilotRequested = false;
        return state.info;
    }

    state.info.explicitSelection = true;
    state.info.selectedState = parsedPolicy;
    state.info.configSource = kBareMetalHttpsPolicyFileSource;
    state.info.error = nullptr;
    return state.info;
}

const char* selected_ca_bundle_path_for_policy(GxosValidatedHttpsPolicyState state)
{
    return policy_state_supports_user_trust(state)
        ? kBareMetalUserCaBundlePath
        : kBareMetalCaBundlePath;
}

const char* selected_ca_bundle_manifest_path_for_policy(GxosValidatedHttpsPolicyState state)
{
    return policy_state_supports_user_trust(state)
        ? kBareMetalUserCaBundleManifestPath
        : kBareMetalCaBundleManifestPath;
}

const char* compat_ca_bundle_manifest_path_for_policy(GxosValidatedHttpsPolicyState state)
{
    return policy_state_supports_user_trust(state)
        ? kBareMetalUserCaBundleManifestCompatPath
        : kBareMetalCaBundleManifestCompatPath;
}

const char* compat_ca_bundle_path_for_policy(GxosValidatedHttpsPolicyState state)
{
    return policy_state_supports_user_trust(state)
        ? kBareMetalUserCaBundleCompatPath
        : nullptr;
}

const char* fallback_path_if_missing(const char* primaryPath,
                                     const char* compatPath,
                                     kernel::vfs::FileInfo* info,
                                     kernel::vfs::Status* statStatus)
{
    if (!primaryPath || !info || !statStatus) return primaryPath;

    *statStatus = kernel::vfs::stat(primaryPath, info);
    if ((*statStatus == kernel::vfs::VFS_ERR_NOT_FOUND ||
         *statStatus == kernel::vfs::VFS_ERR_NOT_MOUNT) &&
        compatPath && compatPath[0] != '\0') {
        *statStatus = kernel::vfs::stat(compatPath, info);
        if (*statStatus == kernel::vfs::VFS_OK) {
            return compatPath;
        }
    }
    return primaryPath;
}

bool public_internet_trust_opt_in_enabled()
{
    kernel::vfs::FileInfo info{};
    kernel::vfs::Status statStatus = kernel::vfs::VFS_ERR_INVALID;
    const char* readPath = fallback_path_if_missing(
        kBareMetalPublicInternetTrustOptInPath,
        kBareMetalPublicInternetTrustOptInCompatPath,
        &info,
        &statStatus);
    if (statStatus != kernel::vfs::VFS_OK ||
        info.type != kernel::vfs::FILE_TYPE_REGULAR ||
        info.size == 0 ||
        info.size > 32u) {
        return false;
    }

    char buffer[33];
    const int32_t bytesRead = kernel::vfs::read_file(
        readPath,
        reinterpret_cast<uint8_t*>(buffer),
        32u);
    if (bytesRead <= 0) return false;
    buffer[bytesRead < 32 ? bytesRead : 32] = '\0';

    const char* begin = buffer;
    const char* end = buffer;
    while (*end != '\0') ++end;
    while (begin < end &&
        (*begin == ' ' || *begin == '\t' || *begin == '\r' || *begin == '\n')) {
        ++begin;
    }
    while (end > begin &&
        (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        --end;
    }

    char token[33];
    copy_trimmed_ascii_span(token, sizeof(token), begin, end);
    return text_equals_insensitive(token, "enabled") ||
        text_equals_insensitive(token, "1") ||
        text_equals_insensitive(token, "true") ||
        text_equals_insensitive(token, "yes");
}
#endif

const char* readiness_blocker_for_ca_store(const GxosCaStoreInfo& info)
{
    switch (info.status) {
    case GxosCaStoreStatus::Missing:
        return info.error ? info.error : "Root CA bundle is missing.";
    case GxosCaStoreStatus::TooLarge:
        return info.error ? info.error : "Root CA bundle exceeds the 512 KiB safety cap";
    case GxosCaStoreStatus::ReadError:
        return info.error ? info.error : "Root CA bundle could not be read from the VFS";
    case GxosCaStoreStatus::ParseUnsupported:
        return info.error ? info.error : "Root CA bundle parser is not wired yet";
    case GxosCaStoreStatus::Invalid:
        return info.error ? info.error : "Root CA bundle contents are invalid";
    default:
        break;
    }

    switch (info.parseStatus) {
    case GxosCaParseStatus::SourceMissing:
        return "Mbed TLS source import is incomplete at third_party/mbedtls";
    case GxosCaParseStatus::ConfigMissing:
        return "guideXOS Mbed TLS 4.x config pair is incomplete under third_party/mbedtls/guidexos";
    case GxosCaParseStatus::ParseError:
        return info.error ? info.error : "Root CA bundle could not be parsed";
    default:
        return nullptr;
    }
}

#if defined(GXOS_BARE_METAL)
bool trust_source_is_production_policy_source(GxosTrustStoreSource source)
{
    return source == GxosTrustStoreSource::ProductionPublicProbeTrust ||
        source == GxosTrustStoreSource::ShippedRootCandidate ||
        source == GxosTrustStoreSource::ProductionRootStore;
}

GxosTrustStoreSource trust_store_source_from_ca_info(const GxosCaStoreInfo& info, bool publicProbeBundle)
{
    if (info.status == GxosCaStoreStatus::Missing && info.bytesLoaded == 0 && !info.testOnlyFixture) {
        return GxosTrustStoreSource::None;
    }
    if (info.testOnlyFixture) {
        return GxosTrustStoreSource::SmokeFixtureTrust;
    }
    if (text_equals(info.path, kBareMetalUserCaBundlePath)) {
        return GxosTrustStoreSource::UserProvidedTrustStore;
    }
    if (publicProbeBundle ||
        (info.manifest.status == GxosCaManifestStatus::Loaded &&
            info.manifest.bundleType &&
            text_equals(info.manifest.bundleType, "production-public-probe-merged") &&
            info.manifest.productionReady &&
            !info.manifest.testOnly &&
            info.manifest.hashMatch)) {
        return GxosTrustStoreSource::ProductionPublicProbeTrust;
    }
    if (info.manifest.bundleType &&
        text_equals(info.manifest.bundleType, kShippedRootCandidateBundleType)) {
        return GxosTrustStoreSource::ShippedRootCandidate;
    }
    return GxosTrustStoreSource::ProductionRootStore;
}

const char* trust_store_source_detail(GxosTrustStoreSource source)
{
    switch (source) {
    case GxosTrustStoreSource::SmokeFixtureTrust: return kSmokeFixtureTrustStoreDetail;
    case GxosTrustStoreSource::UserProvidedTrustStore: return kUserProvidedTrustStoreDetail;
    case GxosTrustStoreSource::ProductionPublicProbeTrust: return kProductionPublicProbeTrustStoreDetail;
    case GxosTrustStoreSource::ShippedRootCandidate: return kShippedRootCandidateTrustStoreDetail;
    case GxosTrustStoreSource::ProductionRootStore: return kProductionRootStoreTrustStoreDetail;
    case GxosTrustStoreSource::WindowsSystemTrustStore: return kHostedTrustStoreDetail;
    case GxosTrustStoreSource::None:
    default:
        return kNoTrustStoreDetail;
    }
}

const char* trust_store_manifest_blocker(const GxosCaStoreInfo& caInfo)
{
    const GxosCaManifestInfo& manifest = caInfo.manifest;
    switch (manifest.status) {
    case GxosCaManifestStatus::Missing:
    case GxosCaManifestStatus::TooLarge:
    case GxosCaManifestStatus::ReadError:
    case GxosCaManifestStatus::Invalid:
        return manifest.error ? manifest.error : "CA bundle manifest is unavailable.";
    case GxosCaManifestStatus::Loaded:
        if (!manifest.hashMatch) {
            return manifest.error ? manifest.error : "CA bundle manifest sha256 does not match the loaded PEM bytes.";
        }
        if (manifest.testOnly) {
            return "CA bundle manifest is marked test_only=yes; broader validated HTTPS remains fail-closed.";
        }
        return nullptr;
    case GxosCaManifestStatus::NotApplicable:
    default:
        return nullptr;
    }
}
#endif

GxosTrustStorePolicyInfo make_trust_store_policy_info()
{
#if defined(GXOS_BARE_METAL)
    const GxosCaStoreInfo caInfo = gxos_ca_store_info();
    const bool publicProbeBundle = is_public_internet_trust_bundle(runtime_state().bytes, caInfo.bytesLoaded);
    const GxosTrustStoreSource source = trust_store_source_from_ca_info(caInfo, publicProbeBundle);

    if (caInfo.status == GxosCaStoreStatus::Missing) {
        return {
            GxosTrustStorePolicyState::ProductionTrustStoreUnavailable,
            GxosTrustStoreSource::None,
            caInfo.path,
            kNoTrustStoreDetail,
            0,
            0,
            false,
            false,
            false,
            caInfo.error ? caInfo.error : "Root CA bundle not found."
        };
    }

    if (caInfo.status == GxosCaStoreStatus::Loaded &&
        caInfo.parseStatus == GxosCaParseStatus::Parsed) {
        const char* manifestBlocker =
            source == GxosTrustStoreSource::SmokeFixtureTrust
                ? nullptr
                : trust_store_manifest_blocker(caInfo);
        const bool publicInternetReady =
            source == GxosTrustStoreSource::ProductionPublicProbeTrust &&
            manifestBlocker == nullptr &&
            public_internet_trust_opt_in_enabled() &&
            caInfo.manifest.productionReady;
        if (manifestBlocker) {
            return {
                GxosTrustStorePolicyState::TrustStoreMalformed,
                source,
                caInfo.path,
                trust_store_source_detail(source),
                caInfo.bytesLoaded,
                caInfo.parsedCertificateCount,
                caInfo.testOnlyFixture || caInfo.manifest.testOnly,
                false,
                false,
                manifestBlocker
            };
        }
        return {
            GxosTrustStorePolicyState::TrustStoreParsed,
            source,
            caInfo.path,
            trust_store_source_detail(source),
            caInfo.bytesLoaded,
            caInfo.parsedCertificateCount,
            caInfo.testOnlyFixture || caInfo.manifest.testOnly,
            trust_source_is_production_policy_source(source),
            publicInternetReady,
            caInfo.error
        };
    }

    return {
        GxosTrustStorePolicyState::TrustStoreMalformed,
        source,
        caInfo.path,
        trust_store_source_detail(source),
        caInfo.bytesLoaded,
        caInfo.parsedCertificateCount,
        caInfo.testOnlyFixture || caInfo.manifest.testOnly,
        false,
        false,
        caInfo.error ? caInfo.error : "Trust store is present but not usable."
    };
#else
    return {
        GxosTrustStorePolicyState::TrustStoreParsed,
        GxosTrustStoreSource::WindowsSystemTrustStore,
        kHostedCaBundlePath,
        kHostedTrustStoreDetail,
        0,
        0,
        false,
        true,
        true,
        nullptr
    };
#endif
}

const char* compute_local_smoke_https_blocker()
{
#if defined(GXOS_BARE_METAL)
    const GxosTlsBackendInfo backend = gxos_tls_backend_info();
    if (!gxos_tls_backend_available()) {
        return backend.error ? backend.error : "Controlled local HTTPS prerequisites are not ready yet.";
    }

    const GxosTrustStorePolicyInfo trustPolicy = make_trust_store_policy_info();
    if (trustPolicy.state != GxosTrustStorePolicyState::TrustStoreParsed) {
        return trustPolicy.error ? trustPolicy.error : "Controlled local HTTPS trust store is unavailable.";
    }

    const GxosTlsHostnameValidationInfo hostnameInfo = gxos_tls_hostname_validation_info();
    if (!hostnameInfo.available) {
        return "Controlled local HTTPS hostname validation is not active yet.";
    }

    return nullptr;
#else
    return nullptr;
#endif
}

bool non_smoke_https_prerequisites_ready(const GxosTrustStorePolicyInfo& trustPolicy)
{
#if defined(GXOS_BARE_METAL)
    const GxosTlsHostnameValidationInfo hostnameInfo = gxos_tls_hostname_validation_info();
    return gxos_random_quality() == GxosRandomQuality::Secure &&
        is_clock_ready(gxos_wall_clock_status()) &&
        gxos_tls_backend_available() &&
        trustPolicy.state == GxosTrustStorePolicyState::TrustStoreParsed &&
        hostnameInfo.available;
#else
    (void)trustPolicy;
    return true;
#endif
}

GxosValidatedHttpsPolicyInfo make_validated_https_policy_info()
{
#if defined(GXOS_BARE_METAL)
    const BareMetalHttpsPolicyConfigInfo config = bare_metal_https_policy_config_info();
    const GxosTrustStorePolicyInfo trustPolicy = make_trust_store_policy_info();
    const bool localSmokeReady = compute_local_smoke_https_blocker() == nullptr;
    const bool nonSmokeReady = non_smoke_https_prerequisites_ready(trustPolicy);
    const GxosValidatedHttpsPolicyState reportedSelectedState = config.selectedState;
    GxosValidatedHttpsPolicyState effectiveSelection = config.selectedState;
    if (!config.explicitSelection &&
        trustPolicy.state == GxosTrustStorePolicyState::TrustStoreParsed &&
        trustPolicy.source == GxosTrustStoreSource::SmokeFixtureTrust &&
        localSmokeReady) {
        effectiveSelection = GxosValidatedHttpsPolicyState::LocalSmokeOnly;
    }

    GxosValidatedHttpsPolicyState effectiveState = GxosValidatedHttpsPolicyState::Disabled;
    bool localAllowlistEnabled = false;
    bool validatedNavigationEnabled = false;
    bool broadPublicHttpsEnabled = false;
    const bool publicHttpsPilotRequested = config.publicHttpsPilotRequested;
    bool productionReady = false;
    const char* localAllowReason = localSmokeReady
        ? "Local smoke HTTPS prerequisites are satisfied."
        : compute_local_smoke_https_blocker();
    const char* detail = "Validated bare-metal HTTPS stays fail-closed until an explicit policy and trust-store prerequisites are satisfied.";
    const char* publicHttpsPilotReason = "Public HTTPS pilot is off by default.";
    const char* blocker = trustPolicy.error ? trustPolicy.error : "Trust-store policy is not ready.";
    const char* error = config.error;

    if (effectiveSelection == GxosValidatedHttpsPolicyState::LocalSmokeOnly &&
        trustPolicy.state == GxosTrustStorePolicyState::TrustStoreParsed &&
        trustPolicy.source == GxosTrustStoreSource::SmokeFixtureTrust &&
        localSmokeReady) {
        effectiveState = GxosValidatedHttpsPolicyState::LocalSmokeOnly;
        localAllowlistEnabled = true;
        detail = "Controlled guidexos.test HTTPS is validated through the smoke-only trust fixture; broader bare-metal https:// remains disabled.";
        publicHttpsPilotReason = "Public HTTPS pilot is disabled while the smoke-only trust fixture is active.";
        blocker = kLocalSmokeOnlyPolicyBlocker;
        localAllowReason = "Smoke fixture trust and the controlled local HTTPS allowlist are both active.";
    } else if (effectiveSelection == GxosValidatedHttpsPolicyState::UserTrustStoreDevMode) {
        if (trustPolicy.source != GxosTrustStoreSource::UserProvidedTrustStore) {
            detail = "User/dev HTTPS policy is selected, but the active trust store is not the user bundle path.";
            publicHttpsPilotReason = "Public HTTPS pilot requires a production CA bundle at /certs/ca-bundle.pem.";
            blocker = "User/dev HTTPS policy requires /config/certs/ca-bundle.pem.";
        } else if (!nonSmokeReady) {
            detail = "User/dev HTTPS policy is selected, but bare-metal TLS prerequisites are not yet complete.";
            publicHttpsPilotReason = "Public HTTPS pilot requires ProductionValidated and complete production TLS prerequisites.";
            blocker = trustPolicy.error ? trustPolicy.error : "User/dev HTTPS prerequisites are incomplete.";
        } else {
            effectiveState = GxosValidatedHttpsPolicyState::UserTrustStoreDevMode;
            validatedNavigationEnabled = true;
            detail = "User/dev HTTPS policy is selected, the user CA bundle parsed successfully, and explicit validated fixture HTTPS navigation is enabled with dev-mode trust diagnostics while public HTTPS pilot stays disabled.";
            publicHttpsPilotReason = "Public HTTPS pilot is unavailable in UserTrustStoreDevMode.";
            blocker = nullptr;
        }
    } else if (effectiveSelection == GxosValidatedHttpsPolicyState::ProductionValidated) {
        if (!trust_source_is_production_policy_source(trustPolicy.source)) {
            detail = "Production HTTPS policy is selected, but the active trust store is not a production bundle.";
            publicHttpsPilotReason = "Public HTTPS pilot requires a non-smoke production CA bundle at /certs/ca-bundle.pem.";
            blocker = "Production HTTPS policy requires a non-smoke bundle at /certs/ca-bundle.pem.";
        } else if (!nonSmokeReady) {
            detail = "Production HTTPS policy is selected, but bare-metal TLS prerequisites are not yet complete.";
            publicHttpsPilotReason = trustPolicy.error ? trustPolicy.error : "Production HTTPS prerequisites are incomplete.";
            blocker = trustPolicy.error ? trustPolicy.error : "Production HTTPS prerequisites are incomplete.";
        } else {
            effectiveState = GxosValidatedHttpsPolicyState::ProductionValidated;
            validatedNavigationEnabled = true;
            productionReady = true;
            if (publicHttpsPilotRequested) {
                broadPublicHttpsEnabled = true;
                detail = "Production HTTPS prerequisites are satisfied, explicit validated fixture HTTPS navigation is enabled, and the controlled public HTTPS pilot is enabled.";
                publicHttpsPilotReason = "Public HTTPS pilot is enabled for hostname-only HTTPS targets under ProductionValidated.";
            } else {
                detail = "Production HTTPS prerequisites are satisfied and explicit validated fixture HTTPS navigation is enabled; public HTTPS pilot remains disabled until public-https-pilot=enabled.";
                publicHttpsPilotReason = "Public HTTPS pilot requires public-https-pilot=enabled under ProductionValidated.";
            }
            blocker = nullptr;
        }
    } else {
        if (trustPolicy.state == GxosTrustStorePolicyState::TrustStoreParsed &&
            trustPolicy.source == GxosTrustStoreSource::UserProvidedTrustStore) {
            detail = "A user CA bundle is parsed, but UserTrustStoreDevMode is not selected.";
            publicHttpsPilotReason = "Public HTTPS pilot requires ProductionValidated.";
            blocker = "User/dev HTTPS policy is off by default.";
        } else if (trustPolicy.state == GxosTrustStorePolicyState::TrustStoreParsed &&
            trust_source_is_production_policy_source(trustPolicy.source)) {
            detail = "A production CA bundle is parsed, but ProductionValidated is not selected.";
            publicHttpsPilotReason = "Public HTTPS pilot requires ProductionValidated and public-https-pilot=enabled.";
            blocker = "Production HTTPS policy is off by default.";
        }
    }

    return {
        effectiveState,
        reportedSelectedState,
        localAllowlistEnabled,
        localSmokeReady,
        validatedNavigationEnabled,
        broadPublicHttpsEnabled,
        publicHttpsPilotRequested,
        productionReady,
        config.configPath,
        config.configSource,
        localAllowReason ? localAllowReason : "(none)",
        detail,
        publicHttpsPilotReason,
        blocker,
        error
    };
#else
    return {
        GxosValidatedHttpsPolicyState::ProductionValidated,
        GxosValidatedHttpsPolicyState::ProductionValidated,
        true,
        true,
        true,
        true,
        true,
        true,
        "(Windows trust store)",
        kHostedHttpsPolicySource,
        "Hosted HTTPS is enabled through Schannel.",
        "Hosted Navigator uses Schannel with the Windows trust store for validated HTTPS.",
        "Hosted public HTTPS is enabled through Schannel.",
        nullptr,
        nullptr
    };
#endif
}

GxosTlsRuntimeHookInfo make_runtime_hook_info()
{
#if defined(GXOS_BARE_METAL)
    const GxosTlsMbedTlsImportInfo importInfo = make_mbedtls_import_info();
    if (!importInfo.sourcePresent) {
        return {
            GxosTlsHookStatus::Unavailable,
            GxosTlsHookStatus::Unavailable,
            GxosTlsHookStatus::Unavailable,
            GxosTlsHookStatus::Unavailable,
            "Allocator hook is unavailable until the vendored Mbed TLS source tree is imported.",
            "RNG callback is unavailable until the vendored Mbed TLS source tree is imported.",
            "Time callback is unavailable until the vendored Mbed TLS source tree is imported.",
            "psa_crypto_init() is unavailable until the vendored Mbed TLS source tree is imported."
        };
    }
    if (!importInfo.sourceReadyForCompile || !importInfo.configPresent || !importInfo.cryptoConfigPresent ||
        !GXOS_TLS_MBEDTLS_RUNTIME_INCLUDED) {
        return {
            GxosTlsHookStatus::Unavailable,
            GxosTlsHookStatus::Unavailable,
            GxosTlsHookStatus::Unavailable,
            GxosTlsHookStatus::Unavailable,
            "Allocator hook is unavailable while the Mbed TLS 4.x runtime-linked subset is incomplete.",
            "RNG callback is unavailable while the Mbed TLS 4.x runtime-linked subset is incomplete.",
            "Time callback is unavailable while the Mbed TLS 4.x runtime-linked subset is incomplete.",
            "psa_crypto_init() is unavailable while the Mbed TLS 4.x runtime-linked subset is incomplete."
        };
    }

    BareMetalTlsRuntimeState& state = runtime_state();
    if (!state.hooksAttempted) {
        state.hooksAttempted = true;
#if GXOS_TLS_MBEDTLS_RUNTIME_INCLUDED
        if (!state.caChainInitialized) {
            mbedtls_x509_crt_init(&state.caChain);
            state.caChainInitialized = true;
        }
        mbedtls_memory_buffer_alloc_init(state.arena, sizeof(state.arena));
        mbedtls_platform_set_fprintf(gxos_mbedtls_platform_fprintf_noop);
        mbedtls_platform_set_exit(gxos_mbedtls_platform_exit_noop);
        mbedtls_platform_set_time(gxos_mbedtls_time_callback);
        state.allocatorInitialized = mbedtls_memory_buffer_alloc_verify() == 0;
        if (state.allocatorInitialized) {
            state.allocatorStatus = GxosTlsHookStatus::Ready;
            copy_text(state.allocatorDetail, sizeof(state.allocatorDetail),
                "Bounded Mbed TLS memory_buffer_alloc arena is active for bare-metal TLS prerequisites.");
        } else {
            state.allocatorStatus = GxosTlsHookStatus::Error;
            copy_text(state.allocatorDetail, sizeof(state.allocatorDetail),
                "Bounded Mbed TLS arena initialization failed integrity verification.");
        }
#endif
    }

    if (!state.allocatorInitialized) {
        state.allocatorStatus = GxosTlsHookStatus::Unavailable;
        if (state.allocatorDetail[0] == '\0') {
            copy_text(state.allocatorDetail, sizeof(state.allocatorDetail),
                "Allocator hook is not active.");
        }
    }

    if (gxos_random_quality() == GxosRandomQuality::Secure) {
        state.rngStatus = GxosTlsHookStatus::Ready;
        copy_text(state.rngDetail, sizeof(state.rngDetail),
            "PSA external RNG callback is wired to gxos_random_bytes() and requires Secure entropy.");
    } else {
        state.rngStatus = GxosTlsHookStatus::Unavailable;
        copy_text(state.rngDetail, sizeof(state.rngDetail),
            "PSA external RNG callback is wired, but guideXOS does not currently report Secure entropy.");
    }

    if (is_clock_ready(gxos_wall_clock_status())) {
        state.timeStatus = GxosTlsHookStatus::Ready;
        copy_text(state.timeDetail, sizeof(state.timeDetail),
            "mbedtls_time() and UTC gmtime_r() are wired to the guideXOS wall clock and fail closed when time is implausible.");
    } else {
        state.timeStatus = GxosTlsHookStatus::Unavailable;
        copy_text(state.timeDetail, sizeof(state.timeDetail),
            "Wall clock callback is wired, but guideXOS does not currently report a plausible UTC time.");
    }

    return {
        state.allocatorStatus,
        state.rngStatus,
        state.timeStatus,
        state.psaStatus,
        state.allocatorDetail,
        state.rngDetail,
        state.timeDetail,
        state.psaDetail
    };
#else
    return {
        GxosTlsHookStatus::NotApplicable,
        GxosTlsHookStatus::NotApplicable,
        GxosTlsHookStatus::NotApplicable,
        GxosTlsHookStatus::NotApplicable,
        "Hosted Schannel path does not use the bare-metal Mbed TLS allocator hook.",
        "Hosted Schannel path does not use the bare-metal PSA RNG callback.",
        "Hosted Schannel path does not use the bare-metal Mbed TLS time callback.",
        "Hosted Schannel path does not use bare-metal psa_crypto_init()."
    };
#endif
}

#if defined(GXOS_BARE_METAL) && GXOS_TLS_MBEDTLS_RUNTIME_INCLUDED
bool ensure_psa_initialized()
{
    BareMetalTlsRuntimeState& state = runtime_state();
    const GxosTlsRuntimeHookInfo hooks = make_runtime_hook_info();

    if (!hook_ready(hooks.allocatorStatus)) {
        state.psaStatus = GxosTlsHookStatus::Unavailable;
        copy_text(state.psaDetail, sizeof(state.psaDetail),
            hooks.allocatorDetail ? hooks.allocatorDetail : "Allocator hook is unavailable.");
        return false;
    }
    if (!hook_ready(hooks.rngCallbackStatus)) {
        state.psaStatus = GxosTlsHookStatus::Unavailable;
        copy_text(state.psaDetail, sizeof(state.psaDetail),
            hooks.rngDetail ? hooks.rngDetail : "RNG callback is unavailable.");
        return false;
    }

    if (state.psaInitialized) {
        state.psaStatus = GxosTlsHookStatus::Ready;
        copy_text(state.psaDetail, sizeof(state.psaDetail),
            "psa_crypto_init() completed successfully for CA parsing.");
        return true;
    }

    state.psaInitAttempted = true;
    const psa_status_t status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        state.psaStatus = GxosTlsHookStatus::Error;
        copy_text(state.psaDetail, sizeof(state.psaDetail),
            "psa_crypto_init() failed, so CA parsing remains disabled.");
        return false;
    }

    state.psaInitialized = true;
    state.psaStatus = GxosTlsHookStatus::Ready;
    copy_text(state.psaDetail, sizeof(state.psaDetail),
        "psa_crypto_init() completed successfully for CA parsing.");
    return true;
}
#endif

void tls_set_stage(GxosTlsLocalHandshakeResult* result, const char* stage)
{
    if (!result) return;
    copy_text(result->stage, sizeof(result->stage), stage ? stage : "(none)");
}

void tls_trace_stage(const char* stage);
void tls_set_transport_status(GxosTlsLocalHandshakeResult* result,
                              gxos::web::HttpByteStreamTlsStatus status,
                              const char* errorText = nullptr);

#if defined(GXOS_BARE_METAL) && GXOS_TLS_MBEDTLS_RUNTIME_INCLUDED
constexpr uint32_t kGxosTlsSmokeHandshakeTimeoutMs = 5000;
constexpr uint32_t kGxosTlsSmokeIoTimeoutMs = 5000;

struct GxosTlsSmokeIoContext {
    GxosTlsByteStream stream{};
    int lastTransportError = 0;
    bool tracedSend = false;
    bool tracedRecv = false;
    uint32_t sendCalls = 0;
    uint32_t recvCalls = 0;
};

void tls_trace_stage(const char*) {}
void tls_trace_stream_io(const char*, uint32_t, int, int) {}

uint32_t tls_timeout_ticks(uint32_t timeoutMs)
{
    return static_cast<uint32_t>(timeoutMs / 10u + 1u);
}

bool tls_wait_until_ready(GxosTlsByteStream* stream, uint32_t startTicks, uint32_t timeoutTicks)
{
    if (stream && stream->poll) stream->poll(stream->context);
    return (static_cast<uint32_t>(kernel::pit::ticks()) - startTicks) <= timeoutTicks;
}

int gxos_tls_stream_send(void* context, const unsigned char* buffer, size_t length)
{
    GxosTlsSmokeIoContext* io = static_cast<GxosTlsSmokeIoContext*>(context);
    if (!io || !io->stream.write || (buffer == nullptr && length != 0)) return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    ++io->sendCalls;
    if (!io->tracedSend) {
        io->tracedSend = true;
        tls_trace_stage("stream_send");
    }
    int requestLength = static_cast<int>(length > 0x7FFFu ? 0x7FFFu : length);
    if (requestLength <= 0 && length != 0) requestLength = 0x7FFF;
    const int sent = io->stream.write(io->stream.context, buffer, requestLength);
    if (io->sendCalls <= 8u) {
        tls_trace_stream_io("stream_send_io", io->sendCalls, requestLength, sent);
    }
    if (sent > 0) return sent;
    if (sent == 0) return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    io->lastTransportError = sent;
    return sent == kernel::tcp::TCP_ERR_WOULDBLOCK ? MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

int gxos_tls_stream_recv(void* context, unsigned char* buffer, size_t length)
{
    GxosTlsSmokeIoContext* io = static_cast<GxosTlsSmokeIoContext*>(context);
    if (!io || !io->stream.read || (buffer == nullptr && length != 0)) return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    ++io->recvCalls;
    if (!io->tracedRecv) {
        io->tracedRecv = true;
        tls_trace_stage("stream_recv");
    }
    int requestLength = static_cast<int>(length > 0x7FFFu ? 0x7FFFu : length);
    if (requestLength <= 0 && length != 0) requestLength = 0x7FFF;
    const int received = io->stream.read(io->stream.context, buffer, requestLength);
    if (io->recvCalls <= 16u || received > 0) {
        tls_trace_stream_io("stream_recv_io", io->recvCalls, requestLength, received);
    }
    if (received > 0) return received;
    if (received == 0) return MBEDTLS_ERR_SSL_CONN_EOF;
    io->lastTransportError = received;
    return received == kernel::tcp::TCP_ERR_WOULDBLOCK ? MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

void tls_copy_runtime_strings(mbedtls_ssl_context* ssl, GxosTlsLocalHandshakeResult* result)
{
    if (!ssl || !result) return;
    copy_text(result->protocol, sizeof(result->protocol), mbedtls_ssl_get_version(ssl));
    copy_text(result->cipherSuite, sizeof(result->cipherSuite), mbedtls_ssl_get_ciphersuite(ssl));
}

struct GxosTlsHttpByteStreamSession {
    GxosTlsByteStream tcpStream{};
    GxosTlsLocalHandshakeResult* result = nullptr;
    GxosTlsSmokeIoContext io{};
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    bool sslInitialized = false;
    bool confInitialized = false;
    bool closed = false;
};

void tls_set_transport_status(GxosTlsLocalHandshakeResult* result,
                              gxos::web::HttpByteStreamTlsStatus status,
                              const char* errorText)
{
    if (!result) return;
    result->transportStatus = status;
    if (errorText && errorText[0]) {
        copy_text(result->error, sizeof(result->error), errorText);
    }
}

gxos::web::HttpByteStreamTlsStatus tls_status_from_backend_status(GxosTlsBackendStatus status)
{
    switch (status) {
    case GxosTlsBackendStatus::RngCallbackUnavailable:
        return gxos::web::HttpByteStreamTlsStatus::RngUnavailable;
    case GxosTlsBackendStatus::ClockCallbackUnavailable:
        return gxos::web::HttpByteStreamTlsStatus::ClockUnavailable;
    case GxosTlsBackendStatus::CaMissing:
        return gxos::web::HttpByteStreamTlsStatus::CaMissing;
    case GxosTlsBackendStatus::CaParseFailed:
    case GxosTlsBackendStatus::CaParsed:
        return gxos::web::HttpByteStreamTlsStatus::CaParseFailed;
    default:
        return gxos::web::HttpByteStreamTlsStatus::HandshakeFailed;
    }
}

gxos::web::HttpByteStreamTlsStatus tls_status_from_verify_flags(uint32_t verifyFlags)
{
#ifdef MBEDTLS_X509_BADCERT_CN_MISMATCH
    return (verifyFlags & MBEDTLS_X509_BADCERT_CN_MISMATCH) != 0
        ? gxos::web::HttpByteStreamTlsStatus::HostnameMismatch
        : gxos::web::HttpByteStreamTlsStatus::CertificateVerifyFailed;
#else
    (void)verifyFlags;
    return gxos::web::HttpByteStreamTlsStatus::CertificateVerifyFailed;
#endif
}

void tls_close_http_byte_stream_session(GxosTlsHttpByteStreamSession* session)
{
    if (!session || session->closed) return;
    session->closed = true;
    // Failed handshakes and validation failures may not have a usable TLS session
    // to shut down cleanly; aborting the TCP stream is sufficient for those cases.
    if (session->result && session->result->handshakeSuccess) {
        (void)mbedtls_ssl_close_notify(&session->ssl);
    }
    if (session->tcpStream.close) session->tcpStream.close(session->tcpStream.context);
    if (session->sslInitialized) {
        mbedtls_ssl_free(&session->ssl);
        session->sslInitialized = false;
    }
    if (session->confInitialized) {
        mbedtls_ssl_config_free(&session->conf);
        session->confInitialized = false;
    }
    mbedtls_free(session);
}

int tls_http_byte_stream_read(void* context, uint8_t* buffer, int length)
{
    GxosTlsHttpByteStreamSession* session = static_cast<GxosTlsHttpByteStreamSession*>(context);
    if (!session || !session->result || !buffer || length <= 0) return -1;

    tls_set_stage(session->result, "response_read");
    uint32_t ioStartTicks = static_cast<uint32_t>(kernel::pit::ticks());
    const uint32_t ioTimeout = tls_timeout_ticks(kGxosTlsSmokeIoTimeoutMs);
    for (;;) {
        const int ret = mbedtls_ssl_read(&session->ssl,
            reinterpret_cast<unsigned char*>(buffer), (size_t)length);
        if (ret > 0) {
            session->result->responseReadSuccess = true;
            session->result->responseBytesRead += (size_t)ret;
            session->result->mbedtlsError = 0;
            session->result->mbedtlsState = session->ssl.MBEDTLS_PRIVATE(state);
            session->result->transportStatus = gxos::web::HttpByteStreamTlsStatus::Success;
            return ret;
        }

        session->result->mbedtlsError = ret;
        session->result->mbedtlsState = session->ssl.MBEDTLS_PRIVATE(state);
        if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || ret == MBEDTLS_ERR_SSL_CONN_EOF) {
            session->result->mbedtlsError = 0;
            return 0;
        }
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (!tls_wait_until_ready(&session->tcpStream, ioStartTicks, ioTimeout)) {
                tls_set_transport_status(session->result, gxos::web::HttpByteStreamTlsStatus::TlsReadFailed,
                    "TLS response read timed out.");
                return -1;
            }
            continue;
        }

        session->result->transportError = session->io.lastTransportError;
        tls_set_transport_status(session->result, gxos::web::HttpByteStreamTlsStatus::TlsReadFailed,
            "TLS response read failed.");
        return -1;
    }
}

int tls_http_byte_stream_write(void* context, const uint8_t* buffer, int length)
{
    GxosTlsHttpByteStreamSession* session = static_cast<GxosTlsHttpByteStreamSession*>(context);
    if (!session || !session->result || !buffer || length <= 0) return -1;

    tls_set_stage(session->result, "request_write");
    int offset = 0;
    uint32_t ioStartTicks = static_cast<uint32_t>(kernel::pit::ticks());
    const uint32_t ioTimeout = tls_timeout_ticks(kGxosTlsSmokeIoTimeoutMs);
    while (offset < length) {
        const int ret = mbedtls_ssl_write(&session->ssl,
            reinterpret_cast<const unsigned char*>(buffer + offset),
            (size_t)(length - offset));
        if (ret > 0) {
            offset += ret;
            session->result->requestWriteSuccess = true;
            session->result->requestBytesWritten += (size_t)ret;
            session->result->mbedtlsError = 0;
            session->result->mbedtlsState = session->ssl.MBEDTLS_PRIVATE(state);
            session->result->transportStatus = gxos::web::HttpByteStreamTlsStatus::Success;
            ioStartTicks = static_cast<uint32_t>(kernel::pit::ticks());
            continue;
        }

        session->result->mbedtlsError = ret;
        session->result->mbedtlsState = session->ssl.MBEDTLS_PRIVATE(state);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (!tls_wait_until_ready(&session->tcpStream, ioStartTicks, ioTimeout)) {
                tls_set_transport_status(session->result, gxos::web::HttpByteStreamTlsStatus::TlsWriteFailed,
                    "TLS request write timed out.");
                return -1;
            }
            continue;
        }

        session->result->transportError = session->io.lastTransportError;
        tls_set_transport_status(session->result, gxos::web::HttpByteStreamTlsStatus::TlsWriteFailed,
            "TLS request write failed.");
        return -1;
    }
    return offset;
}

void tls_http_byte_stream_close(void* context)
{
    tls_close_http_byte_stream_session(static_cast<GxosTlsHttpByteStreamSession*>(context));
}
#endif

#if !defined(GXOS_BARE_METAL) || !GXOS_TLS_MBEDTLS_RUNTIME_INCLUDED
void tls_trace_stage(const char*) {}

void tls_set_transport_status(GxosTlsLocalHandshakeResult* result,
                              gxos::web::HttpByteStreamTlsStatus status,
                              const char* errorText)
{
    if (!result) return;
    result->transportStatus = status;
    if (errorText && errorText[0]) {
        copy_text(result->error, sizeof(result->error), errorText);
    }
}
#endif

GxosTlsArenaInfo make_tls_arena_info()
{
#if defined(GXOS_BARE_METAL)
    const GxosTlsRuntimeHookInfo hooks = make_runtime_hook_info();
    if (!hook_ready(hooks.allocatorStatus)) {
        return {
            hooks.allocatorStatus == GxosTlsHookStatus::Pending
                ? GxosTlsArenaStatus::Pending
                : GxosTlsArenaStatus::Unavailable,
            kGxosTlsArenaCapacityBytes,
            0,
            0,
            hooks.allocatorDetail
        };
    }

#if GXOS_TLS_MBEDTLS_RUNTIME_INCLUDED
    return {
        GxosTlsArenaStatus::Ready,
        kGxosTlsArenaCapacityBytes,
        0,
        0,
        "Bounded TLS arena is initialized; live usage counters stay disabled in the freestanding build."
    };
#else
    return {
        GxosTlsArenaStatus::Unavailable,
        kGxosTlsArenaCapacityBytes,
        0,
        0,
        "Allocator telemetry is unavailable because the Mbed TLS runtime-linked subset is incomplete."
    };
#endif
#else
    return {
        GxosTlsArenaStatus::NotApplicable,
        0,
        0,
        0,
        "Hosted Schannel path does not use the guideXOS TLS arena."
    };
#endif
}

GxosTlsHostnameValidationInfo make_hostname_validation_info()
{
#if defined(GXOS_BARE_METAL)
    const GxosTlsMbedTlsImportInfo importInfo = make_mbedtls_import_info();
    if (!importInfo.sourcePresent) {
        return {
            false,
            true,
            true,
            false,
            "scaffolded only; original URL host is retained for future SNI and certificate checks, but Mbed TLS source import is still missing"
        };
    }
    if (!importInfo.sourceReadyForCompile) {
        return {
            false,
            true,
            true,
            false,
            "scaffolded only; original URL host is retained, but the Mbed TLS 4.x import is still incomplete for freestanding compile"
        };
    }
    return {
        true,
        true,
        true,
        false,
        "scaffold ready; original URL host and future SNI host are retained, numeric-IP validation stays disabled, and bare-metal hostname enforcement stays gated outside the controlled local-only HTTPS path"
    };
#else
    return {
        true,
        true,
        true,
        true,
        "enabled via Schannel using the original URL host for SNI and hostname validation"
    };
#endif
}

GxosTlsBackendInfo make_backend_info()
{
#if defined(GXOS_BARE_METAL)
    const GxosTlsMbedTlsImportInfo importInfo = make_mbedtls_import_info();
    const GxosTlsRuntimeHookInfo hooks = make_runtime_hook_info();
    const GxosTlsArenaInfo arenaInfo = make_tls_arena_info();

    if (!importInfo.sourcePresent) {
        return {
            GxosTlsBackendStatus::SourceMissing,
            "Mbed TLS bare-metal scaffold",
            importInfo.detectedVersion,
            "Vendored Mbed TLS source tree is missing at third_party/mbedtls; handshake support stays disabled."
        };
    }
    if (!importInfo.sourceReadyForCompile) {
        return {
            GxosTlsBackendStatus::SourceMissing,
            "Mbed TLS bare-metal scaffold",
            importInfo.detectedVersion,
            "Vendored Mbed TLS 4.x import is incomplete; required compile headers, generated helpers, or TF-PSA-Crypto support files are missing, so handshake support stays disabled."
        };
    }
    if (!importInfo.configPresent || !importInfo.cryptoConfigPresent) {
        return {
            GxosTlsBackendStatus::Error,
            "Mbed TLS bare-metal scaffold",
            importInfo.detectedVersion,
            "guideXOS split Mbed TLS 4.x config is missing; handshake support stays disabled."
        };
    }

    if (!hook_ready(hooks.allocatorStatus)) {
        return {
            GxosTlsBackendStatus::AllocatorUnavailable,
            "Mbed TLS bare-metal scaffold",
            importInfo.detectedVersion,
            hooks.allocatorDetail ? hooks.allocatorDetail : "Bounded TLS allocator hook is unavailable."
        };
    }
    if (!hook_ready(hooks.rngCallbackStatus)) {
        return {
            GxosTlsBackendStatus::RngCallbackUnavailable,
            "Mbed TLS bare-metal scaffold",
            importInfo.detectedVersion,
            hooks.rngDetail ? hooks.rngDetail : "Secure RNG callback is unavailable."
        };
    }
    if (!hook_ready(hooks.timeCallbackStatus)) {
        return {
            GxosTlsBackendStatus::ClockCallbackUnavailable,
            "Mbed TLS bare-metal scaffold",
            importInfo.detectedVersion,
            hooks.timeDetail ? hooks.timeDetail : "Wall clock callback is unavailable."
        };
    }
    if (arenaInfo.status != GxosTlsArenaStatus::Ready) {
        return {
            GxosTlsBackendStatus::AllocatorUnavailable,
            "Mbed TLS bare-metal scaffold",
            importInfo.detectedVersion,
            arenaInfo.error ? arenaInfo.error : "TLS arena is unavailable."
        };
    }

    const GxosCaStoreInfo caInfo = gxos_ca_store_info();
    if (caInfo.status == GxosCaStoreStatus::Missing) {
        return {
            GxosTlsBackendStatus::CaMissing,
            "Mbed TLS bare-metal scaffold",
            importInfo.detectedVersion,
            caInfo.error ? caInfo.error : "Root CA bundle is missing."
        };
    }
    if (caInfo.status != GxosCaStoreStatus::Loaded || caInfo.parseStatus != GxosCaParseStatus::Parsed) {
        return {
            GxosTlsBackendStatus::CaParseFailed,
            "Mbed TLS bare-metal scaffold",
            importInfo.detectedVersion,
            caInfo.error ? caInfo.error : "Root CA bundle is not parsed."
        };
    }

    const GxosTlsHostnameValidationInfo hostnameInfo = gxos_tls_hostname_validation_info();
    if (hostnameInfo.available) {
        return {
            GxosTlsBackendStatus::ReadyForLocalHandshake,
            "Mbed TLS bare-metal scaffold",
            importInfo.detectedVersion,
            "Allocator, PSA RNG/time callbacks, root CA parsing, hostname validation, and the shared bare-metal TLS transport path are ready; explicit validated HTTPS still requires policy selection."
        };
    }

    if (caInfo.status == GxosCaStoreStatus::Loaded && caInfo.parseStatus == GxosCaParseStatus::Parsed) {
        return {
            GxosTlsBackendStatus::CaParsed,
            "Mbed TLS bare-metal scaffold",
            importInfo.detectedVersion,
            "Root CA bundle is parsed, but Navigator handshake wiring remains disabled."
        };
    }
    return {
        GxosTlsBackendStatus::ClockCallbackReady,
        "Mbed TLS bare-metal scaffold",
        importInfo.detectedVersion,
        "Allocator, RNG callback, and wall-clock callback are ready; CA parsing has not completed yet."
    };
#else
    return {
        GxosTlsBackendStatus::ReadyForValidatedNavigation,
        "Schannel hosted",
        "Windows Schannel",
        nullptr
    };
#endif
}

const char* compute_tls_prerequisites_blocker()
{
    const GxosTlsBackendInfo backend = gxos_tls_backend_info();
    if (!gxos_tls_backend_available()) {
        switch (backend.status) {
        case GxosTlsBackendStatus::SourceMissing:
        case GxosTlsBackendStatus::CompileProbeReady:
        case GxosTlsBackendStatus::AllocatorUnavailable:
        case GxosTlsBackendStatus::AllocatorReady:
        case GxosTlsBackendStatus::RngCallbackUnavailable:
        case GxosTlsBackendStatus::RngCallbackReady:
        case GxosTlsBackendStatus::ClockCallbackUnavailable:
        case GxosTlsBackendStatus::ClockCallbackReady:
        case GxosTlsBackendStatus::CaMissing:
        case GxosTlsBackendStatus::CaParseFailed:
        case GxosTlsBackendStatus::CaParsed:
        case GxosTlsBackendStatus::HostnameValidationReady:
        case GxosTlsBackendStatus::Error:
            return backend.error ? backend.error : "TLS backend is not ready";
        case GxosTlsBackendStatus::Unavailable:
            return "TLS backend is unavailable";
        default:
            return "TLS backend is not ready";
        }
    }

    const GxosCaStoreInfo caStore = gxos_ca_store_info();
    if (caStore.status != GxosCaStoreStatus::Loaded || caStore.parseStatus != GxosCaParseStatus::Parsed) {
        return readiness_blocker_for_ca_store(caStore);
    }

    const GxosTlsHostnameValidationInfo hostnameInfo = gxos_tls_hostname_validation_info();
    if (!hostnameInfo.available) {
        return "Certificate hostname validation is not active yet";
    }

    const GxosValidatedHttpsPolicyInfo httpsPolicy = make_validated_https_policy_info();
    if (httpsPolicy.state != GxosValidatedHttpsPolicyState::ProductionValidated &&
        httpsPolicy.state != GxosValidatedHttpsPolicyState::UserTrustStoreDevMode) {
        return httpsPolicy.blocker ? httpsPolicy.blocker : "Broader validated HTTPS policy is disabled.";
    }

    return nullptr;
}

} // namespace

const char* gxos_tls_backend_status_name(GxosTlsBackendStatus status)
{
    switch (status) {
    case GxosTlsBackendStatus::Unavailable: return "Unavailable";
    case GxosTlsBackendStatus::SourceMissing: return "SourceMissing";
    case GxosTlsBackendStatus::CompileProbeReady: return "CompileProbeReady";
    case GxosTlsBackendStatus::AllocatorUnavailable: return "AllocatorUnavailable";
    case GxosTlsBackendStatus::AllocatorReady: return "AllocatorReady";
    case GxosTlsBackendStatus::RngCallbackUnavailable: return "RngCallbackUnavailable";
    case GxosTlsBackendStatus::RngCallbackReady: return "RngCallbackReady";
    case GxosTlsBackendStatus::ClockCallbackUnavailable: return "ClockCallbackUnavailable";
    case GxosTlsBackendStatus::ClockCallbackReady: return "ClockCallbackReady";
    case GxosTlsBackendStatus::CaMissing: return "CaMissing";
    case GxosTlsBackendStatus::CaParseFailed: return "CaParseFailed";
    case GxosTlsBackendStatus::CaParsed: return "CaParsed";
    case GxosTlsBackendStatus::HostnameValidationReady: return "HostnameValidationReady";
    case GxosTlsBackendStatus::ReadyForLocalHandshake: return "ReadyForLocalHandshake";
    case GxosTlsBackendStatus::ReadyForValidatedNavigation: return "ReadyForValidatedNavigation";
    case GxosTlsBackendStatus::Error: return "Error";
    default: return "Unknown";
    }
}

bool gxos_tls_backend_available()
{
    const GxosTlsBackendStatus status = gxos_tls_backend_info().status;
    return status == GxosTlsBackendStatus::ReadyForLocalHandshake ||
        status == GxosTlsBackendStatus::ReadyForValidatedNavigation;
}

GxosTlsBackendInfo gxos_tls_backend_info()
{
    return make_backend_info();
}

GxosTlsMbedTlsImportInfo gxos_tls_mbedtls_import_info()
{
    return make_mbedtls_import_info();
}

const char* gxos_ca_store_status_name(GxosCaStoreStatus status)
{
    switch (status) {
    case GxosCaStoreStatus::Missing: return "Missing";
    case GxosCaStoreStatus::Loaded: return "Loaded";
    case GxosCaStoreStatus::TooLarge: return "TooLarge";
    case GxosCaStoreStatus::ReadError: return "ReadError";
    case GxosCaStoreStatus::ParseUnsupported: return "ParseUnsupported";
    case GxosCaStoreStatus::Invalid: return "Invalid";
    default: return "Unknown";
    }
}

const char* gxos_ca_parse_status_name(GxosCaParseStatus status)
{
    switch (status) {
    case GxosCaParseStatus::NotApplicable: return "NotApplicable";
    case GxosCaParseStatus::NotAttempted: return "NotAttempted";
    case GxosCaParseStatus::SourceMissing: return "SourceMissing";
    case GxosCaParseStatus::ConfigMissing: return "ConfigMissing";
    case GxosCaParseStatus::Parsed: return "Parsed";
    case GxosCaParseStatus::ParseError: return "ParseError";
    default: return "Unknown";
    }
}

const char* gxos_ca_manifest_status_name(GxosCaManifestStatus status)
{
    switch (status) {
    case GxosCaManifestStatus::NotApplicable: return "NotApplicable";
    case GxosCaManifestStatus::Missing: return "Missing";
    case GxosCaManifestStatus::Loaded: return "Loaded";
    case GxosCaManifestStatus::TooLarge: return "TooLarge";
    case GxosCaManifestStatus::ReadError: return "ReadError";
    case GxosCaManifestStatus::Invalid: return "Invalid";
    default: return "Unknown";
    }
}

bool gxos_ca_store_load_once()
{
#if defined(GXOS_BARE_METAL)
    BareMetalCaStoreState& state = ca_store_state();
    if (state.attempted) {
        return state.info.status == GxosCaStoreStatus::Loaded &&
            state.info.parseStatus == GxosCaParseStatus::Parsed;
    }
    state.attempted = true;
    const BareMetalHttpsPolicyConfigInfo config = bare_metal_https_policy_config_info();
    const GxosValidatedHttpsPolicyState selectedPolicyState =
        config.explicitSelection ? config.selectedState : GxosValidatedHttpsPolicyState::Disabled;
    const char* activeCaBundlePath = selected_ca_bundle_path_for_policy(selectedPolicyState);
    const char* activeManifestPath = selected_ca_bundle_manifest_path_for_policy(selectedPolicyState);
    const char* activeCompatManifestPath = compat_ca_bundle_manifest_path_for_policy(selectedPolicyState);
    const char* activeCaBundleReadPath = activeCaBundlePath;
    reset_ca_manifest_info(state, activeManifestPath);

    kernel::vfs::FileInfo info{};
    kernel::vfs::Status statStatus = kernel::vfs::VFS_ERR_INVALID;
    activeCaBundleReadPath = fallback_path_if_missing(
        activeCaBundlePath,
        compat_ca_bundle_path_for_policy(selectedPolicyState),
        &info,
        &statStatus);
    if (statStatus == kernel::vfs::VFS_ERR_NOT_FOUND ||
        statStatus == kernel::vfs::VFS_ERR_NOT_MOUNT) {
        state.info = {
            GxosCaStoreStatus::Missing,
            GxosCaParseStatus::NotAttempted,
            0,
            0,
            0,
            false,
            activeCaBundlePath,
            policy_state_supports_user_trust(selectedPolicyState)
                ? "Root CA bundle not found at /config/certs/ca-bundle.pem."
                : "Root CA bundle not found at /certs/ca-bundle.pem.",
            state.info.manifest
        };
        return false;
    }
    if (statStatus != kernel::vfs::VFS_OK) {
        state.info = {
            GxosCaStoreStatus::ReadError,
            GxosCaParseStatus::NotAttempted,
            0,
            0,
            0,
            false,
            activeCaBundlePath,
            "Could not stat the root CA bundle through the VFS.",
            state.info.manifest
        };
        return false;
    }
    if (info.type != kernel::vfs::FILE_TYPE_REGULAR) {
        state.info = {
            GxosCaStoreStatus::Invalid,
            GxosCaParseStatus::NotAttempted,
            0,
            0,
            0,
            false,
            activeCaBundlePath,
            "Root CA bundle path does not point to a regular file.",
            state.info.manifest
        };
        return false;
    }
    if (info.size == 0) {
        state.info = {
            GxosCaStoreStatus::Invalid,
            GxosCaParseStatus::NotAttempted,
            0,
            0,
            0,
            false,
            activeCaBundlePath,
            "Root CA bundle is empty.",
            state.info.manifest
        };
        return false;
    }
    if (info.size > static_cast<uint64_t>(kGxosMaxCaStoreBytes)) {
        state.info = {
            GxosCaStoreStatus::TooLarge,
            GxosCaParseStatus::NotAttempted,
            0,
            0,
            0,
            false,
            activeCaBundlePath,
            "Root CA bundle exceeds the 512 KiB safety cap.",
            state.info.manifest
        };
        return false;
    }

    const int32_t bytesRead = kernel::vfs::read_file(
        activeCaBundleReadPath,
        runtime_state().bytes,
        static_cast<uint32_t>(kGxosMaxCaStoreBytes));
    if (bytesRead < 0 || static_cast<uint64_t>(bytesRead) != info.size) {
        state.info = {
            GxosCaStoreStatus::ReadError,
            GxosCaParseStatus::NotAttempted,
            0,
            0,
            0,
            false,
            activeCaBundlePath,
            "Root CA bundle read did not complete successfully.",
            state.info.manifest
        };
        return false;
    }

    const size_t loaded = static_cast<size_t>(bytesRead);
    runtime_state().bytes[loaded] = 0;
    const bool testOnlyFixture = is_smoke_only_ca_fixture(runtime_state().bytes, loaded);
    const size_t pemBegins = count_token_occurrences(runtime_state().bytes, loaded, "-----BEGIN CERTIFICATE-----");
    const size_t pemEnds = count_token_occurrences(runtime_state().bytes, loaded, "-----END CERTIFICATE-----");
    if (pemBegins == 0 || pemBegins != pemEnds ||
        !buffer_contains_token(runtime_state().bytes, loaded, "-----BEGIN CERTIFICATE-----") ||
        !buffer_contains_token(runtime_state().bytes, loaded, "-----END CERTIFICATE-----")) {
        state.info = {
            GxosCaStoreStatus::Invalid,
            GxosCaParseStatus::NotAttempted,
            loaded,
            pemBegins,
            0,
            testOnlyFixture,
            activeCaBundlePath,
            "Root CA bundle does not look like a PEM certificate bundle.",
            state.info.manifest
        };
        return false;
    }

    load_selected_ca_bundle_manifest(state, activeManifestPath, activeCompatManifestPath);

    const GxosTlsMbedTlsImportInfo importInfo = make_mbedtls_import_info();
    if (!importInfo.sourcePresent || !importInfo.sourceReadyForCompile) {
        state.info = {
            GxosCaStoreStatus::Loaded,
            GxosCaParseStatus::SourceMissing,
            loaded,
            pemBegins,
            0,
            testOnlyFixture,
            activeCaBundlePath,
            "Root CA bundle is loaded, but the Mbed TLS 4.x source import is incomplete so X.509 parsing cannot begin.",
            state.info.manifest
        };
        return true;
    }
    if (!importInfo.configPresent || !importInfo.cryptoConfigPresent) {
        state.info = {
            GxosCaStoreStatus::Loaded,
            GxosCaParseStatus::ConfigMissing,
            loaded,
            pemBegins,
            0,
            testOnlyFixture,
            activeCaBundlePath,
            "Root CA bundle is loaded, but the guideXOS Mbed TLS 4.x config pair is incomplete so X.509 parsing cannot begin.",
            state.info.manifest
        };
        return true;
    }
#if GXOS_TLS_MBEDTLS_RUNTIME_INCLUDED
    const GxosTlsRuntimeHookInfo hooks = gxos_tls_runtime_hook_info();
    if (!hook_ready(hooks.allocatorStatus)) {
        state.info = {
            GxosCaStoreStatus::Loaded,
            GxosCaParseStatus::ParseError,
            loaded,
            pemBegins,
            0,
            testOnlyFixture,
            activeCaBundlePath,
            hooks.allocatorDetail,
            state.info.manifest
        };
        return false;
    }
    if (!hook_ready(hooks.rngCallbackStatus)) {
        state.info = {
            GxosCaStoreStatus::Loaded,
            GxosCaParseStatus::ParseError,
            loaded,
            pemBegins,
            0,
            testOnlyFixture,
            activeCaBundlePath,
            hooks.rngDetail,
            state.info.manifest
        };
        return false;
    }
    if (!hook_ready(hooks.timeCallbackStatus)) {
        state.info = {
            GxosCaStoreStatus::Loaded,
            GxosCaParseStatus::ParseError,
            loaded,
            pemBegins,
            0,
            testOnlyFixture,
            activeCaBundlePath,
            hooks.timeDetail,
            state.info.manifest
        };
        return false;
    }
    if (!ensure_psa_initialized()) {
        state.info = {
            GxosCaStoreStatus::Loaded,
            GxosCaParseStatus::ParseError,
            loaded,
            pemBegins,
            0,
            testOnlyFixture,
            activeCaBundlePath,
            runtime_state().psaDetail,
            state.info.manifest
        };
        return false;
    }

    if (compute_loaded_ca_bundle_sha256(runtime_state().bytes, loaded, state.computedSha256, sizeof(state.computedSha256))) {
        state.info.manifest.computedSha256 = state.computedSha256;
        if (state.info.manifest.status == GxosCaManifestStatus::Loaded &&
            state.info.manifest.manifestSha256 &&
            text_equals(state.info.manifest.manifestSha256, state.info.manifest.computedSha256)) {
            state.info.manifest.hashMatch = true;
            state.info.manifest.error = nullptr;
        } else if (state.info.manifest.status == GxosCaManifestStatus::Loaded) {
            state.info.manifest.hashMatch = false;
            copy_text(state.manifestError, sizeof(state.manifestError),
                "CA bundle manifest sha256 does not match the loaded PEM bytes.");
            state.info.manifest.error = state.manifestError;
        }
    } else {
        copy_text(state.manifestError, sizeof(state.manifestError),
            "Runtime SHA-256 verification for the loaded CA bundle is unavailable.");
        state.info.manifest.error = state.manifestError;
    }

    BareMetalTlsRuntimeState& runtime = runtime_state();
    mbedtls_x509_crt_free(&runtime.caChain);
    mbedtls_x509_crt_init(&runtime.caChain);

    const int parseResult = mbedtls_x509_crt_parse(&runtime.caChain, runtime.bytes, loaded + 1);
    const size_t parsedCount = count_ca_chain(&runtime.caChain);
    if (parseResult != 0 || parsedCount == 0) {
        if (parseResult == MBEDTLS_ERR_ASN1_ALLOC_FAILED) {
            runtime.allocatorExhausted = true;
        }
        mbedtls_x509_crt_free(&runtime.caChain);
        mbedtls_x509_crt_init(&runtime.caChain);
        state.info = {
            GxosCaStoreStatus::Loaded,
            GxosCaParseStatus::ParseError,
            loaded,
            pemBegins,
            0,
            testOnlyFixture,
            activeCaBundlePath,
            parseResult > 0
                ? "Root CA bundle was only partially parsed; guideXOS fails closed until every certificate parses cleanly."
                : "Mbed TLS rejected the root CA bundle during X.509 parsing.",
            state.info.manifest
        };
        return false;
    }

    state.info = {
        GxosCaStoreStatus::Loaded,
        GxosCaParseStatus::Parsed,
        loaded,
        pemBegins,
        parsedCount,
        testOnlyFixture,
        activeCaBundlePath,
        testOnlyFixture
            ? "Root CA bundle loaded once and parsed successfully through Mbed TLS (smoke-only test fixture; not production trust)."
            : (state.info.manifest.hashMatch
                ? "Root CA bundle manifest matched the loaded PEM bytes and the CA bundle parsed successfully through Mbed TLS."
                : "Root CA bundle loaded once and parsed successfully through Mbed TLS."),
        state.info.manifest
    };
    return true;
#else
    state.info = {
        GxosCaStoreStatus::Loaded,
        GxosCaParseStatus::ParseError,
        loaded,
        pemBegins,
        0,
        testOnlyFixture,
        activeCaBundlePath,
        "Root CA bundle is loaded, but the Mbed TLS runtime-linked parser subset is unavailable in this build.",
        state.info.manifest
    };
    return false;
#endif
#else
    return true;
#endif
}

GxosCaStoreInfo gxos_ca_store_info()
{
#if defined(GXOS_BARE_METAL)
    gxos_ca_store_load_once();
    return ca_store_state().info;
#else
    static const GxosCaStoreInfo info = {
        GxosCaStoreStatus::Loaded,
        GxosCaParseStatus::NotApplicable,
        0,
        0,
        0,
        false,
        kHostedCaBundlePath,
        nullptr,
        {
            GxosCaManifestStatus::NotApplicable,
            "(not applicable in hosted Schannel mode)",
            0,
            false,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            0,
            0,
            false,
            false,
            false,
            nullptr
        }
    };
    return info;
#endif
}

const char* gxos_trust_store_source_name(GxosTrustStoreSource source)
{
    switch (source) {
    case GxosTrustStoreSource::None: return "None";
    case GxosTrustStoreSource::SmokeFixtureTrust: return "SmokeFixtureTrust";
    case GxosTrustStoreSource::UserProvidedTrustStore: return "UserProvidedTrustStore";
    case GxosTrustStoreSource::ProductionPublicProbeTrust: return "ProductionPublicProbeTrust";
    case GxosTrustStoreSource::ShippedRootCandidate: return "ShippedRootCandidate";
    case GxosTrustStoreSource::ProductionRootStore: return "ProductionRootStore";
    case GxosTrustStoreSource::WindowsSystemTrustStore: return "WindowsSystemTrustStore";
    default: return "Unknown";
    }
}

const char* gxos_trust_store_policy_state_name(GxosTrustStorePolicyState state)
{
    switch (state) {
    case GxosTrustStorePolicyState::NoTrustStore: return "NoTrustStore";
    case GxosTrustStorePolicyState::ProductionTrustStoreUnavailable: return "ProductionTrustStoreUnavailable";
    case GxosTrustStorePolicyState::TrustStoreMalformed: return "TrustStoreMalformed";
    case GxosTrustStorePolicyState::TrustStoreParsed: return "TrustStoreParsed";
    default: return "Unknown";
    }
}

GxosTrustStorePolicyInfo gxos_tls_trust_store_policy_info()
{
    return make_trust_store_policy_info();
}

const char* gxos_validated_https_policy_state_name(GxosValidatedHttpsPolicyState state)
{
    switch (state) {
    case GxosValidatedHttpsPolicyState::Disabled: return "Disabled";
    case GxosValidatedHttpsPolicyState::LocalSmokeOnly: return "LocalSmokeOnly";
    case GxosValidatedHttpsPolicyState::UserTrustStoreDevMode: return "UserTrustStoreDevMode";
    case GxosValidatedHttpsPolicyState::ProductionValidated: return "ProductionValidated";
    default: return "Unknown";
    }
}

GxosValidatedHttpsPolicyInfo gxos_validated_https_policy_info()
{
    return make_validated_https_policy_info();
}

bool gxos_tls_local_smoke_https_ready()
{
    return compute_local_smoke_https_blocker() == nullptr;
}

const char* gxos_tls_local_smoke_https_blocker_reason()
{
    const char* blocker = compute_local_smoke_https_blocker();
    return blocker ? blocker : "none";
}

const char* gxos_tls_arena_status_name(GxosTlsArenaStatus status)
{
    switch (status) {
    case GxosTlsArenaStatus::NotApplicable: return "NotApplicable";
    case GxosTlsArenaStatus::Pending: return "Pending";
    case GxosTlsArenaStatus::Ready: return "Ready";
    case GxosTlsArenaStatus::Unavailable: return "Unavailable";
    case GxosTlsArenaStatus::Exhausted: return "Exhausted";
    default: return "Unknown";
    }
}

GxosTlsArenaInfo gxos_tls_arena_info()
{
    return make_tls_arena_info();
}

const char* gxos_tls_hook_status_name(GxosTlsHookStatus status)
{
    switch (status) {
    case GxosTlsHookStatus::NotApplicable: return "NotApplicable";
    case GxosTlsHookStatus::Pending: return "Pending";
    case GxosTlsHookStatus::Ready: return "Ready";
    case GxosTlsHookStatus::Unavailable: return "Unavailable";
    case GxosTlsHookStatus::Error: return "Error";
    default: return "Unknown";
    }
}

GxosTlsRuntimeHookInfo gxos_tls_runtime_hook_info()
{
    return make_runtime_hook_info();
}

GxosTlsHostnameValidationInfo gxos_tls_hostname_validation_info()
{
    return make_hostname_validation_info();
}

bool gxos_tls_open_http_byte_stream(const char* sniHostname,
                                    GxosTlsByteStream tcpStream,
                                    gxos::web::HttpByteStream* outStream,
                                    GxosTlsLocalHandshakeResult* result)
{
    if (outStream) {
        outStream->context = nullptr;
        outStream->read = nullptr;
        outStream->write = nullptr;
        outStream->close = nullptr;
    }
    zero_local_handshake_result(result);
    if (!result || !outStream || !sniHostname || !sniHostname[0] ||
        !tcpStream.read || !tcpStream.write || !tcpStream.close) {
        if (result) {
            tls_set_transport_status(result, gxos::web::HttpByteStreamTlsStatus::HandshakeFailed,
                "Invalid TLS byte-stream parameters.");
        }
        return false;
    }

    result->attempted = true;
    copy_text(result->sniHost, sizeof(result->sniHost), sniHostname);
    result->tcpConnected = true;
    tls_set_stage(result, "parameter_validation");

#if defined(GXOS_BARE_METAL) && GXOS_TLS_MBEDTLS_RUNTIME_INCLUDED
    const GxosTlsBackendInfo backend = gxos_tls_backend_info();
    tls_set_stage(result, "backend_ready_check");
    if (backend.status != GxosTlsBackendStatus::ReadyForLocalHandshake &&
        backend.status != GxosTlsBackendStatus::ReadyForValidatedNavigation) {
        tls_set_transport_status(result, tls_status_from_backend_status(backend.status),
            backend.error ? backend.error : "Bare-metal TLS backend is unavailable.");
        return false;
    }

    const GxosCaStoreInfo caInfo = gxos_ca_store_info();
    tls_set_stage(result, "ca_ready_check");
    if (caInfo.status != GxosCaStoreStatus::Loaded || caInfo.parseStatus != GxosCaParseStatus::Parsed) {
        tls_set_transport_status(result,
            caInfo.status == GxosCaStoreStatus::Missing
                ? gxos::web::HttpByteStreamTlsStatus::CaMissing
                : gxos::web::HttpByteStreamTlsStatus::CaParseFailed,
            caInfo.error ? caInfo.error : "Root CA bundle is unavailable for TLS byte-stream setup.");
        return false;
    }

    tls_set_stage(result, "psa_ready_check");
    if (!ensure_psa_initialized()) {
        tls_set_transport_status(result, gxos::web::HttpByteStreamTlsStatus::HandshakeFailed,
            runtime_state().psaDetail);
        return false;
    }

    BareMetalTlsRuntimeState& runtime = runtime_state();
    GxosTlsHttpByteStreamSession* session =
        static_cast<GxosTlsHttpByteStreamSession*>(mbedtls_calloc(1, sizeof(GxosTlsHttpByteStreamSession)));
    if (!session) {
        tls_set_transport_status(result, gxos::web::HttpByteStreamTlsStatus::HandshakeFailed,
            "TLS byte-stream allocation failed.");
        if (tcpStream.close) tcpStream.close(tcpStream.context);
        return false;
    }

    session->tcpStream = tcpStream;
    session->result = result;
    session->io.stream = tcpStream;
    mbedtls_ssl_init(&session->ssl);
    session->sslInitialized = true;
    mbedtls_ssl_config_init(&session->conf);
    session->confInitialized = true;

    bool success = false;
    int ret = 0;

    do {
        tls_set_stage(result, "config_defaults");
        tls_trace_stage("config_defaults");
        ret = mbedtls_ssl_config_defaults(&session->conf, MBEDTLS_SSL_IS_CLIENT,
                                          MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT);
        if (ret != 0) {
            result->mbedtlsError = ret;
            tls_set_transport_status(result, gxos::web::HttpByteStreamTlsStatus::HandshakeFailed,
                "Mbed TLS client config defaults failed for the TLS byte-stream.");
            break;
        }

        mbedtls_ssl_conf_authmode(&session->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&session->conf, &runtime.caChain, nullptr);

        tls_set_stage(result, "ssl_setup");
        tls_trace_stage("ssl_setup");
        ret = mbedtls_ssl_setup(&session->ssl, &session->conf);
        if (ret != 0) {
            result->mbedtlsError = ret;
            tls_set_transport_status(result, gxos::web::HttpByteStreamTlsStatus::HandshakeFailed,
                "Mbed TLS SSL setup failed for the TLS byte-stream.");
            break;
        }

        tls_set_stage(result, "set_hostname");
        tls_trace_stage("set_hostname");
        ret = mbedtls_ssl_set_hostname(&session->ssl, sniHostname);
        if (ret != 0) {
            result->mbedtlsError = ret;
            tls_set_transport_status(result, gxos::web::HttpByteStreamTlsStatus::HandshakeFailed,
                "Mbed TLS could not set the TLS SNI/hostname.");
            break;
        }
        result->usedSniHostname = true;

        mbedtls_ssl_set_bio(&session->ssl, &session->io, gxos_tls_stream_send, gxos_tls_stream_recv, nullptr);

        tls_set_stage(result, "handshake");
        tls_trace_stage("handshake");
        uint32_t startTicks = static_cast<uint32_t>(kernel::pit::ticks());
        const uint32_t handshakeTimeout = tls_timeout_ticks(kGxosTlsSmokeHandshakeTimeoutMs);
        while ((ret = mbedtls_ssl_handshake(&session->ssl)) != 0) {
            result->mbedtlsError = ret;
            result->mbedtlsState = session->ssl.MBEDTLS_PRIVATE(state);
            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
                if (!tls_wait_until_ready(&session->tcpStream, startTicks, handshakeTimeout)) {
                    tls_set_transport_status(result, gxos::web::HttpByteStreamTlsStatus::HandshakeFailed,
                        "TLS handshake timed out.");
                    ret = -1;
                    break;
                }
                continue;
            }
            break;
        }
        if (ret != 0) {
            result->mbedtlsState = session->ssl.MBEDTLS_PRIVATE(state);
            result->verifyFlags = static_cast<uint32_t>(mbedtls_ssl_get_verify_result(&session->ssl));
            result->certificateValidationSuccess = result->verifyFlags == 0;
            result->hostnameValidationSuccess = result->certificateValidationSuccess &&
                (result->verifyFlags & MBEDTLS_X509_BADCERT_CN_MISMATCH) == 0;
            if (result->transportStatus == gxos::web::HttpByteStreamTlsStatus::NotStarted ||
                result->transportStatus == gxos::web::HttpByteStreamTlsStatus::Success) {
                if (ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED && result->verifyFlags != 0) {
                    tls_set_transport_status(result, tls_status_from_verify_flags(result->verifyFlags),
                        (result->verifyFlags & MBEDTLS_X509_BADCERT_CN_MISMATCH) != 0
                            ? "TLS hostname validation failed."
                            : "TLS certificate validation failed.");
                } else {
                    tls_set_transport_status(result, gxos::web::HttpByteStreamTlsStatus::HandshakeFailed,
                        "TLS handshake failed.");
                }
            }
            break;
        }

        result->handshakeSuccess = true;
        result->mbedtlsState = session->ssl.MBEDTLS_PRIVATE(state);
        tls_copy_runtime_strings(&session->ssl, result);
        tls_set_stage(result, "peer_validation");
        tls_trace_stage("peer_validation");
        result->verifyFlags = static_cast<uint32_t>(mbedtls_ssl_get_verify_result(&session->ssl));
        result->certificateValidationSuccess = result->verifyFlags == 0;
        result->hostnameValidationSuccess =
            result->certificateValidationSuccess &&
            (result->verifyFlags & MBEDTLS_X509_BADCERT_CN_MISMATCH) == 0;
        if (!result->certificateValidationSuccess) {
            tls_set_transport_status(result, tls_status_from_verify_flags(result->verifyFlags),
                (result->verifyFlags & MBEDTLS_X509_BADCERT_CN_MISMATCH) != 0
                    ? "TLS hostname validation failed."
                    : "TLS certificate validation failed.");
            break;
        }

        result->transportStatus = gxos::web::HttpByteStreamTlsStatus::Success;
        outStream->context = session;
        outStream->read = tls_http_byte_stream_read;
        outStream->write = tls_http_byte_stream_write;
        outStream->close = tls_http_byte_stream_close;
        success = true;
    } while (false);

    if (!success) {
        if (session->io.lastTransportError != 0) result->transportError = session->io.lastTransportError;
        tls_close_http_byte_stream_session(session);
    }
    return success;
#else
    tls_set_transport_status(result, gxos::web::HttpByteStreamTlsStatus::HandshakeFailed,
        "Local TLS byte-stream is unavailable in this runtime.");
    return false;
#endif
}

bool gxos_tls_smoke_https_request(const char* sniHostname,
                                  const char* requestBytes,
                                  size_t requestLength,
                                  GxosTlsByteStream stream,
                                  char* responseBuffer,
                                  size_t responseBufferSize,
                                  size_t* responseBytesOut,
                                  GxosTlsLocalHandshakeResult* result)
{
    if (responseBytesOut) *responseBytesOut = 0;
    if (responseBuffer && responseBufferSize > 0) responseBuffer[0] = '\0';
    if (!result || !requestBytes || requestLength == 0 || !responseBuffer || responseBufferSize < 2) {
        if (result) {
            zero_local_handshake_result(result);
            tls_set_transport_status(result, gxos::web::HttpByteStreamTlsStatus::HandshakeFailed,
                "Invalid TLS smoke request parameters.");
        }
        return false;
    }

    gxos::web::HttpByteStream tlsStream{};
    if (!gxos_tls_open_http_byte_stream(sniHostname, stream, &tlsStream, result)) {
        return false;
    }

    bool success = false;
    size_t totalRead = 0;
    do {
        int sent = 0;
        while ((size_t)sent < requestLength) {
            const int written = tlsStream.write(tlsStream.context,
                reinterpret_cast<const uint8_t*>(requestBytes + sent),
                (int)(requestLength - (size_t)sent));
            if (written <= 0) {
                if (!result->error[0]) {
                    tls_set_transport_status(result, gxos::web::HttpByteStreamTlsStatus::TlsWriteFailed,
                        "TLS smoke request write failed.");
                }
                break;
            }
            sent += written;
        }
        if ((size_t)sent != requestLength) break;

        while (true) {
            if (totalRead + 1 >= responseBufferSize) {
                tls_set_transport_status(result, gxos::web::HttpByteStreamTlsStatus::ResponseTooLarge,
                    "TLS smoke response exceeded the bounded buffer.");
                break;
            }
            const int received = tlsStream.read(tlsStream.context,
                reinterpret_cast<uint8_t*>(responseBuffer + totalRead),
                (int)(responseBufferSize - totalRead - 1));
            if (received < 0) break;
            if (received == 0) {
                responseBuffer[totalRead] = '\0';
                if (totalRead == 0) {
                    tls_set_transport_status(result, gxos::web::HttpByteStreamTlsStatus::TlsReadFailed,
                        "TLS smoke response was empty.");
                    break;
                }
                if (responseBytesOut) *responseBytesOut = totalRead;
                tls_set_stage(result, "complete");
                tls_trace_stage("complete");
                result->transportStatus = gxos::web::HttpByteStreamTlsStatus::Success;
                success = true;
                break;
            }
            totalRead += (size_t)received;
            responseBuffer[totalRead] = '\0';
        }
    } while (false);

    tlsStream.close(tlsStream.context);
    return success;
}

bool gxos_tls_certificate_validation_policy_enabled()
{
#if defined(GXOS_BARE_METAL)
    return gxos_validated_https_policy_info().state != GxosValidatedHttpsPolicyState::Disabled;
#else
    return true;
#endif
}

const char* gxos_tls_certificate_validation_policy()
{
#if defined(GXOS_BARE_METAL)
    const GxosValidatedHttpsPolicyInfo policy = gxos_validated_https_policy_info();
    if (policy.state == GxosValidatedHttpsPolicyState::LocalSmokeOnly) {
        return "local-smoke-only; controlled guidexos.test HTTPS enforces CA and hostname validation, but broader bare-metal https:// remains disabled until a non-smoke trust store policy is enabled";
    }
    if (policy.selectedState == GxosValidatedHttpsPolicyState::UserTrustStoreDevMode &&
        policy.state == GxosValidatedHttpsPolicyState::UserTrustStoreDevMode) {
        return "dev-mode enabled; explicit validated fixture HTTPS requires the user trust store, secure RNG, plausible wall clock, CA chain validation, hostname validation, and explicit policy selection, while public HTTPS pilot remains disabled";
    }
    if (policy.selectedState == GxosValidatedHttpsPolicyState::ProductionValidated &&
        policy.productionReady &&
        policy.broadPublicHttpsEnabled) {
        return "production validated HTTPS pilot enabled; production CA, hostname validation, secure RNG, plausible wall clock, and explicit public-pilot selection are required, and plaintext fallback remains disabled";
    }
    if (policy.selectedState == GxosValidatedHttpsPolicyState::ProductionValidated &&
        policy.productionReady) {
        return "production validated fixture HTTPS enabled; production CA, hostname validation, secure RNG, and plausible wall clock are required, public HTTPS pilot remains opt-in, and plaintext fallback remains disabled";
    }
    if (policy.selectedState == GxosValidatedHttpsPolicyState::ProductionValidated) {
        return "production-selected but not effective; broader validated bare-metal https:// remains fail-closed until the production CA bundle and TLS prerequisites are complete";
    }
    if (policy.selectedState == GxosValidatedHttpsPolicyState::UserTrustStoreDevMode) {
        return "dev-mode selected but not effective; broader validated bare-metal https:// remains fail-closed until the user CA bundle and TLS prerequisites are complete";
    }
    if (policy.state == GxosValidatedHttpsPolicyState::ProductionValidated) {
        return "enabled for validated bare-metal HTTPS";
    }

    const GxosTlsMbedTlsImportInfo importInfo = gxos_tls_mbedtls_import_info();
    if (!importInfo.sourcePresent) {
        return "disabled; original URL host is retained for future SNI and hostname checks, but Mbed TLS source import is missing";
    }
    if (!importInfo.sourceReadyForCompile) {
        return "disabled; original URL host retention is scaffolded, but the Mbed TLS 4.x source import is incomplete";
    }
    if (!importInfo.configPresent || !importInfo.cryptoConfigPresent) {
        return "disabled; original URL host retention is scaffolded, but the guideXOS Mbed TLS 4.x config pair is incomplete";
    }
    return "disabled; trust-store policy is not production-ready, so broader bare-metal https:// remains fail-closed";
#else
    return "enabled via Schannel, Windows trust, and hostname validation";
#endif
}

bool gxos_tls_prerequisites_ready()
{
    return compute_tls_prerequisites_blocker() == nullptr;
}

const char* gxos_tls_prerequisites_blocker_reason()
{
    const char* blocker = compute_tls_prerequisites_blocker();
    return blocker ? blocker : "none";
}

} // namespace gxos
