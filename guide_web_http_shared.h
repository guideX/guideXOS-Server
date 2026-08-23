#pragma once
// guide_web_http_shared.h
//
// Freestanding-friendly HTTP parsing helpers shared by hosted guideWeb HTTP
// and the bare-metal Navigator adapter.  Keep this file free of STL/Win32
// dependencies so kernel code can include it directly.

#include <stdint.h>

#include "guide_web_content_decoder.h"

namespace gxos {
namespace web {

static const int kHttpSharedMaxHeaderBytes = 32 * 1024;
// The encoded HTTP transaction remains deliberately bounded at 256 KiB.
// Decoded HTML has a separate, larger semantic limit because compression can
// make an ordinary document substantially larger than its wire representation.
static const int kHttpSharedMaxBodyBytes = 256 * 1024;
static const int kHttpSharedDecodedDocumentSegmentBytes = 16 * 1024;
static const int kHttpSharedMaxDecodedDocumentSegments = 32;
static const int kHttpSharedMaxDecodedDocumentBytes =
    kHttpSharedDecodedDocumentSegmentBytes * kHttpSharedMaxDecodedDocumentSegments;
static const int kHttpSharedMaxUrlBytes = 2048;
static const int kHttpSharedMaxHostnameBytes = 253;
static const int kHttpSharedMaxRemoteResources = 32;
static const int kHttpSharedConnectTimeoutMs = 5000;
static const int kHttpSharedReadTimeoutMs = 5000;
static const int kHttpSharedMaxRedirects = 5;

// One production compression policy is shared by hosted, bare-metal, and
// fixture-visible Navigator request construction.  Only codings handled by
// the bounded Content-Encoding decoder may be advertised here.
static const char kHttpSharedProductionAcceptEncoding[] = "gzip, deflate";

// Plain HTTP and future TLS adapters expose the same bounded byte-stream
// contract. TLS can later wrap a connected plain TCP stream without changing
// the HTTP parser or the hosted/bare-metal request loops.
struct HttpByteStream {
    void* context;
    int (*read)(void* context, uint8_t* buffer, int length);
    int (*write)(void* context, const uint8_t* buffer, int length);
    void (*close)(void* context);
};

enum class HttpByteStreamTransportSelection {
    UnsupportedScheme = 0,
    PlainTcpHttp,
    LocalAllowlistedTlsHttps,
    PolicyValidatedTlsHttps,
    BlockedHttpsGeneral,
    BlockedPolicy,
};

enum class HttpByteStreamTlsStatus {
    NotApplicable = 0,
    NotStarted,
    PolicyBlocked,
    RngUnavailable,
    ClockUnavailable,
    CaMissing,
    CaParseFailed,
    TcpConnectFailed,
    HandshakeFailed,
    CertificateVerifyFailed,
    HostnameMismatch,
    TlsWriteFailed,
    TlsReadFailed,
    CapabilityContractFailure,
    ResponseTooLarge,
    Success,
};

struct HttpTransportPolicyDecision {
    HttpByteStreamTransportSelection selection;
    HttpByteStreamTlsStatus tlsStatus;
    bool allowlistMatched;
    bool tcpAttemptAllowed;
    bool tlsAttemptAllowed;
    bool hostnameValidationRequired;
    bool certificateValidationRequired;
    const char* expectedTlsBackend;
    const char* reason;
};

inline const char* httpSharedTransportSelectionName(HttpByteStreamTransportSelection selection)
{
    switch (selection) {
    case HttpByteStreamTransportSelection::UnsupportedScheme: return "UnsupportedScheme";
    case HttpByteStreamTransportSelection::PlainTcpHttp: return "PlainTcpHttp";
    case HttpByteStreamTransportSelection::LocalAllowlistedTlsHttps: return "LocalAllowlistedTlsHttps";
    case HttpByteStreamTransportSelection::PolicyValidatedTlsHttps: return "PolicyValidatedTlsHttps";
    case HttpByteStreamTransportSelection::BlockedHttpsGeneral: return "BlockedHttpsGeneral";
    case HttpByteStreamTransportSelection::BlockedPolicy: return "BlockedPolicy";
    default: return "Unknown";
    }
}

inline const char* httpSharedTlsStatusName(HttpByteStreamTlsStatus status)
{
    switch (status) {
    case HttpByteStreamTlsStatus::NotApplicable: return "NotApplicable";
    case HttpByteStreamTlsStatus::NotStarted: return "NotStarted";
    case HttpByteStreamTlsStatus::PolicyBlocked: return "PolicyBlocked";
    case HttpByteStreamTlsStatus::RngUnavailable: return "RngUnavailable";
    case HttpByteStreamTlsStatus::ClockUnavailable: return "ClockUnavailable";
    case HttpByteStreamTlsStatus::CaMissing: return "CaMissing";
    case HttpByteStreamTlsStatus::CaParseFailed: return "CaParseFailed";
    case HttpByteStreamTlsStatus::TcpConnectFailed: return "TcpConnectFailed";
    case HttpByteStreamTlsStatus::HandshakeFailed: return "HandshakeFailed";
    case HttpByteStreamTlsStatus::CertificateVerifyFailed: return "CertificateVerifyFailed";
    case HttpByteStreamTlsStatus::HostnameMismatch: return "HostnameMismatch";
    case HttpByteStreamTlsStatus::TlsWriteFailed: return "TlsWriteFailed";
    case HttpByteStreamTlsStatus::TlsReadFailed: return "TlsReadFailed";
    case HttpByteStreamTlsStatus::CapabilityContractFailure: return "CapabilityContractFailure";
    case HttpByteStreamTlsStatus::ResponseTooLarge: return "ResponseTooLarge";
    case HttpByteStreamTlsStatus::Success: return "Success";
    default: return "Unknown";
    }
}

enum HttpSharedError {
    HTTP_SHARED_OK = 0,
    HTTP_SHARED_HEADER_TOO_LARGE,
    HTTP_SHARED_BODY_TOO_LARGE,
    HTTP_SHARED_MALFORMED_RESPONSE,
    HTTP_SHARED_UNSUPPORTED_TRANSFER_ENCODING,
    HTTP_SHARED_UNSUPPORTED_CONTENT_ENCODING,
    HTTP_SHARED_MALFORMED_CHUNKED,
    HTTP_SHARED_REDIRECT_LIMIT,
};

inline char httpSharedLower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

inline bool httpSharedCharEqualsInsensitive(char a, char b)
{
    return httpSharedLower(a) == httpSharedLower(b);
}

inline bool httpSharedEqualsInsensitive(const char* a, const char* b)
{
    if (!a || !b) return false;
    int i = 0;
    while (a[i] && b[i]) {
        if (!httpSharedCharEqualsInsensitive(a[i], b[i])) return false;
        ++i;
    }
    return a[i] == '\0' && b[i] == '\0';
}

inline bool httpSharedStartsWithInsensitive(const char* value, const char* prefix)
{
    if (!value || !prefix) return false;
    for (int i = 0; prefix[i]; ++i) {
        if (!value[i] || !httpSharedCharEqualsInsensitive(value[i], prefix[i])) return false;
    }
    return true;
}

inline bool httpSharedIsSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

inline void httpSharedCopyTrimmed(const char* start, const char* end, char* out, int outSize, bool lower)
{
    if (!out || outSize <= 0) return;
    if (!start || !end || end < start) {
        out[0] = '\0';
        return;
    }
    while (start < end && httpSharedIsSpace(*start)) ++start;
    while (end > start && httpSharedIsSpace(end[-1])) --end;
    int oi = 0;
    for (const char* p = start; p < end && oi < outSize - 1; ++p) {
        out[oi++] = lower ? httpSharedLower(*p) : *p;
    }
    out[oi] = '\0';
}

inline void httpSharedCopyLiteral(const char* value, char* out, int outSize)
{
    if (!out || outSize <= 0) return;
    int oi = 0;
    if (value) {
        while (value[oi] && oi < outSize - 1) {
            out[oi] = value[oi];
            ++oi;
        }
    }
    out[oi] = '\0';
}

inline void httpSharedNormalizeContentType(const char* start, const char* end, char* out, int outSize)
{
    if (!out || outSize <= 0) return;
    if (!start || !end || end < start) {
        out[0] = '\0';
        return;
    }
    while (start < end && httpSharedIsSpace(*start)) ++start;
    while (end > start && httpSharedIsSpace(end[-1])) --end;
    int oi = 0;
    for (const char* p = start; p < end && oi < outSize - 1; ++p) {
        if (*p == ';') break;
        out[oi++] = httpSharedLower(*p);
    }
    while (oi > 0 && httpSharedIsSpace(out[oi - 1])) --oi;
    out[oi] = '\0';
}

inline bool httpSharedHeaderHasToken(const char* value, const char* token)
{
    if (!value || !token) return false;
    const char* start = value;
    for (;;) {
        while (*start == ' ' || *start == '\t' || *start == ',') ++start;
        const char* end = start;
        while (*end && *end != ',' && *end != ';') ++end;
        while (end > start && httpSharedIsSpace(end[-1])) --end;

        const char* a = start;
        const char* b = token;
        bool equal = true;
        while (a < end || *b) {
            if (a >= end || !*b || !httpSharedCharEqualsInsensitive(*a, *b)) {
                equal = false;
                break;
            }
            ++a;
            ++b;
        }
        if (equal) return true;
        while (*end && *end != ',') ++end;
        if (!*end) break;
        start = end + 1;
    }
    return false;
}

inline bool httpSharedIsRedirectStatus(int statusCode)
{
    return statusCode == 301 || statusCode == 302 || statusCode == 303 ||
        statusCode == 307 || statusCode == 308;
}

inline bool httpSharedHexValue(char c, int* out)
{
    if (!out) return false;
    if (c >= '0' && c <= '9') {
        *out = c - '0';
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        *out = 10 + c - 'a';
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        *out = 10 + c - 'A';
        return true;
    }
    return false;
}

inline bool httpSharedParseChunkSize(const char* start, const char* end, int* outSize)
{
    if (!start || !end || !outSize || end < start) return false;
    while (start < end && httpSharedIsSpace(*start)) ++start;
    while (end > start && httpSharedIsSpace(end[-1])) --end;
    const char* semi = start;
    while (semi < end && *semi != ';') ++semi;
    end = semi;
    while (end > start && httpSharedIsSpace(end[-1])) --end;
    if (start == end) return false;
    int value = 0;
    for (const char* p = start; p < end; ++p) {
        int digit = 0;
        if (!httpSharedHexValue(*p, &digit)) return false;
        if (value > ((0x7fffffff - digit) >> 4)) return false;
        value = (value << 4) + digit;
    }
    *outSize = value;
    return true;
}

inline bool httpSharedParseDecimalSize(const char* start, const char* end, int* outSize)
{
    if (!start || !end || !outSize || end < start) return false;
    while (start < end && httpSharedIsSpace(*start)) ++start;
    while (end > start && httpSharedIsSpace(end[-1])) --end;
    if (start == end) return false;
    int value = 0;
    for (const char* p = start; p < end; ++p) {
        if (*p < '0' || *p > '9') return false;
        const int digit = *p - '0';
        if (value > (0x7fffffff - digit) / 10) return false;
        value = value * 10 + digit;
    }
    *outSize = value;
    return true;
}

inline bool httpSharedDecodeChunkedBody(const char* encoded, int encodedLen,
                                        char* decoded, int decodedCapacity,
                                        int* decodedLen,
                                        char* error, int errorLen)
{
    if (decodedLen) *decodedLen = 0;
    if (error && errorLen > 0) error[0] = '\0';
    if (!encoded || encodedLen < 0 || !decoded || decodedCapacity <= 0) return false;

    int pos = 0;
    int out = 0;
    for (;;) {
        int lineEnd = -1;
        int delimiterLen = 0;
        for (int i = pos; i < encodedLen; ++i) {
            if (i + 1 < encodedLen && encoded[i] == '\r' && encoded[i + 1] == '\n') {
                lineEnd = i;
                delimiterLen = 2;
                break;
            }
            if (encoded[i] == '\n') {
                lineEnd = i;
                delimiterLen = 1;
                break;
            }
        }
        if (lineEnd < 0) {
            httpSharedCopyLiteral("Chunked response ended before a chunk-size line completed.",
                error, errorLen);
            return false;
        }

        int chunkSize = 0;
        if (!httpSharedParseChunkSize(encoded + pos, encoded + lineEnd, &chunkSize)) {
            httpSharedCopyLiteral("Chunked response included an invalid chunk size.",
                error, errorLen);
            return false;
        }
        pos = lineEnd + delimiterLen;
        if (chunkSize == 0) {
            if (decodedLen) *decodedLen = out;
            if (out < decodedCapacity) decoded[out] = '\0';
            return true;
        }
        if (chunkSize > decodedCapacity - 1 - out || chunkSize > kHttpSharedMaxBodyBytes - out) {
            httpSharedCopyLiteral("Decoded chunked response body exceeded the safety limit.",
                error, errorLen);
            return false;
        }
        if (chunkSize > encodedLen - pos) {
            httpSharedCopyLiteral("Chunked response ended in the middle of a chunk.",
                error, errorLen);
            return false;
        }
        for (int i = 0; i < chunkSize; ++i) decoded[out++] = encoded[pos + i];
        pos += chunkSize;
        if (encodedLen - pos >= 2 && encoded[pos] == '\r' && encoded[pos + 1] == '\n') {
            pos += 2;
        } else if (pos < encodedLen && encoded[pos] == '\n') {
            pos += 1;
        } else {
            httpSharedCopyLiteral("Chunked response was missing the chunk terminator.",
                error, errorLen);
            return false;
        }
    }
}

// Transfer framing can be removed in-place in the raw transaction buffer:
// chunk framing always advances the input cursor farther than the compacted
// output cursor.  This keeps the encoded Content-Encoding stream available
// without adding another 256 KiB transaction buffer.
inline bool httpSharedDecodeChunkedBodyInPlace(char* encoded, int encodedLen,
                                               int decodedCapacity, int* decodedLen,
                                               char* error, int errorLen)
{
    if (decodedLen) *decodedLen = 0;
    if (error && errorLen > 0) error[0] = '\0';
    if (!encoded || encodedLen < 0 || decodedCapacity <= 0) return false;

    int pos = 0;
    int out = 0;
    for (;;) {
        int lineEnd = -1;
        int delimiterLen = 0;
        for (int i = pos; i < encodedLen; ++i) {
            if (i + 1 < encodedLen && encoded[i] == '\r' && encoded[i + 1] == '\n') {
                lineEnd = i;
                delimiterLen = 2;
                break;
            }
            if (encoded[i] == '\n') {
                lineEnd = i;
                delimiterLen = 1;
                break;
            }
        }
        if (lineEnd < 0) {
            httpSharedCopyLiteral("Chunked response ended before a chunk-size line completed.", error, errorLen);
            return false;
        }
        int chunkSize = 0;
        if (!httpSharedParseChunkSize(encoded + pos, encoded + lineEnd, &chunkSize)) {
            httpSharedCopyLiteral("Chunked response included an invalid chunk size.", error, errorLen);
            return false;
        }
        pos = lineEnd + delimiterLen;
        if (chunkSize == 0) {
            if (decodedLen) *decodedLen = out;
            if (out < decodedCapacity) encoded[out] = '\0';
            return true;
        }
        if (chunkSize > decodedCapacity - out || chunkSize > kHttpSharedMaxBodyBytes - out) {
            httpSharedCopyLiteral("Decoded chunked response body exceeded the safety limit.", error, errorLen);
            return false;
        }
        if (chunkSize > encodedLen - pos) {
            httpSharedCopyLiteral("Chunked response ended in the middle of a chunk.", error, errorLen);
            return false;
        }
        for (int i = 0; i < chunkSize; ++i) encoded[out++] = encoded[pos + i];
        pos += chunkSize;
        if (pos + 1 < encodedLen && encoded[pos] == '\r' && encoded[pos + 1] == '\n') {
            pos += 2;
        } else if (pos < encodedLen && encoded[pos] == '\n') {
            ++pos;
        } else {
            httpSharedCopyLiteral("Chunked response was missing the chunk terminator.", error, errorLen);
            return false;
        }
    }
}

// Inspect only the framing state of a bounded chunked body. This lets the
// receive loop stop at the terminating zero chunk even when the server keeps
// the HTTP/1.1 connection alive. A false result means either more bytes are
// needed or the framing is malformed; malformed is reported separately.
inline bool httpSharedChunkedBodyComplete(const char* encoded, int encodedLen,
                                          bool* malformed)
{
    if (malformed) *malformed = false;
    if (!encoded || encodedLen < 0) return false;

    int pos = 0;
    for (;;) {
        int lineEnd = -1;
        int delimiterLen = 0;
        for (int i = pos; i < encodedLen; ++i) {
            if (i + 1 < encodedLen && encoded[i] == '\r' && encoded[i + 1] == '\n') {
                lineEnd = i;
                delimiterLen = 2;
                break;
            }
            if (encoded[i] == '\n') {
                lineEnd = i;
                delimiterLen = 1;
                break;
            }
        }
        if (lineEnd < 0) return false;

        int chunkSize = 0;
        if (!httpSharedParseChunkSize(encoded + pos, encoded + lineEnd, &chunkSize)) {
            if (malformed) *malformed = true;
            return false;
        }
        pos = lineEnd + delimiterLen;
        if (chunkSize == 0) return true;
        if (chunkSize > encodedLen - pos) return false;

        pos += chunkSize;
        if (pos + 1 < encodedLen && encoded[pos] == '\r' && encoded[pos + 1] == '\n') {
            pos += 2;
        } else if (pos < encodedLen && encoded[pos] == '\n') {
            ++pos;
        } else if (pos < encodedLen && encoded[pos] == '\r' && pos + 1 == encodedLen) {
            // The CRLF terminator itself may be split across receive reads.
            // A trailing CR is incomplete framing, not malformed framing.
            return false;
        } else {
            if (pos == encodedLen) return false;
            if (malformed) *malformed = true;
            return false;
        }
    }
}

inline const char* httpSharedErrorName(HttpSharedError error)
{
    switch (error) {
    case HTTP_SHARED_OK: return "None";
    case HTTP_SHARED_HEADER_TOO_LARGE: return "HeaderTooLarge";
    case HTTP_SHARED_BODY_TOO_LARGE: return "BodyTooLarge";
    case HTTP_SHARED_MALFORMED_RESPONSE: return "MalformedResponse";
    case HTTP_SHARED_UNSUPPORTED_TRANSFER_ENCODING: return "UnsupportedTransferEncoding";
    case HTTP_SHARED_UNSUPPORTED_CONTENT_ENCODING: return "UnsupportedContentEncoding";
    case HTTP_SHARED_MALFORMED_CHUNKED: return "MalformedChunkedEncoding";
    case HTTP_SHARED_REDIRECT_LIMIT: return "RedirectLimitExceeded";
    }
    return "Unknown";
}

} // namespace web
} // namespace gxos
