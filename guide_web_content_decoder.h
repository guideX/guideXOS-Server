#pragma once

// Bounded HTTP Content-Encoding decoder.
//
// This is deliberately freestanding-friendly.  The caller owns the output
// buffer and the decoder workspace, so compressed network input never gets to
// choose an allocation size and the DEFLATE/Huffman scratch never lands on a
// kernel boot stack.  The implementation is iterative and has no recursion.

#include <stdint.h>

namespace gxos {
namespace web {

enum class HttpContentCoding : uint8_t {
    Identity = 0,
    Gzip,
    Deflate,
    Unsupported,
};

enum class HttpContentDecodeResult : uint8_t {
    Success = 0,
    UnsupportedEncoding,
    MalformedCompressedResponse,
    DecodedResponseTooLarge,
    OutputAllocationFailed,
};

static const int kHttpContentDecoderFastBits = 9;
static const int kHttpContentDecoderFastSize = 1 << kHttpContentDecoderFastBits;
static const int kHttpContentDecoderMaxSymbols = 288;

struct HttpContentDecoderHuffmanTable {
    uint16_t fast[kHttpContentDecoderFastSize];
    uint16_t reversedCodes[kHttpContentDecoderMaxSymbols];
    uint8_t codeLengths[kHttpContentDecoderMaxSymbols];
    int symbolCount;
};

// The caller owns the bounded destination.  A sink keeps segmented document
// storage out of this freestanding decoder while preserving a hard aggregate
// output limit and a caller-provided DEFLATE history window.
struct HttpContentDecoderSink {
    void* context;
    bool (*writeByte)(void* context, uint8_t value);
    bool (*prepareHistory)(void* context, uint8_t** history, int* historyCapacity);
    int capacityBytes;
    int bytesWritten;
    uint8_t* history;
    int historyCapacity;
    bool capacityExceeded;
    bool allocationFailed;
};

struct HttpContentDecoderWorkspace {
    HttpContentDecoderHuffmanTable codeLength;
    HttpContentDecoderHuffmanTable literalLength;
    HttpContentDecoderHuffmanTable distance;
    uint8_t codeLengths[288 + 32];
    uint32_t lastChecksum;
    int lastChecksumMode;
};

inline void httpSharedDecoderCopyLiteral(const char* value, char* out, int outSize)
{
    if (!out || outSize <= 0) return;
    int i = 0;
    if (value) {
        while (value[i] && i < outSize - 1) {
            out[i] = value[i];
            ++i;
        }
    }
    out[i] = '\0';
}

inline bool httpSharedDecoderTokenEquals(const char* start, const char* end, const char* token)
{
    if (!start || !end || !token || end < start) return false;
    while (start < end && (*start == ' ' || *start == '\t')) ++start;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) --end;
    int i = 0;
    while (start < end && token[i]) {
        char a = *start++;
        char b = token[i++];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return start == end && token[i] == '\0';
}

// HTTP Content-Encoding is intentionally limited to one supported coding in
// this milestone.  A comma is not silently ignored: stacked or duplicated
// codings must be rejected before compressed bytes reach a parser/decoder.
inline HttpContentCoding httpSharedParseContentCoding(const char* value)
{
    if (!value) return HttpContentCoding::Identity;
    const char* start = value;
    while (*start == ' ' || *start == '\t') ++start;
    if (!*start) return HttpContentCoding::Identity;
    const char* end = start;
    while (*end && *end != ',') ++end;
    const char* trimmedEnd = end;
    while (trimmedEnd > start && (trimmedEnd[-1] == ' ' || trimmedEnd[-1] == '\t')) --trimmedEnd;
    if (*end == ',') return HttpContentCoding::Unsupported;
    if (httpSharedDecoderTokenEquals(start, trimmedEnd, "identity")) return HttpContentCoding::Identity;
    if (httpSharedDecoderTokenEquals(start, trimmedEnd, "gzip")) return HttpContentCoding::Gzip;
    if (httpSharedDecoderTokenEquals(start, trimmedEnd, "deflate")) return HttpContentCoding::Deflate;
    return HttpContentCoding::Unsupported;
}

inline const char* httpSharedContentCodingName(HttpContentCoding coding)
{
    switch (coding) {
    case HttpContentCoding::Identity: return "identity";
    case HttpContentCoding::Gzip: return "gzip";
    case HttpContentCoding::Deflate: return "deflate";
    case HttpContentCoding::Unsupported: return "unsupported";
    }
    return "unsupported";
}

inline const char* httpSharedContentDecodeResultName(HttpContentDecodeResult result)
{
    switch (result) {
    case HttpContentDecodeResult::Success: return "Success";
    case HttpContentDecodeResult::UnsupportedEncoding: return "Unsupported Content Encoding";
    case HttpContentDecodeResult::MalformedCompressedResponse: return "Malformed Compressed Response";
    case HttpContentDecodeResult::DecodedResponseTooLarge: return "Decoded Response Too Large";
    case HttpContentDecodeResult::OutputAllocationFailed: return "Document Storage Allocation Failed";
    }
    return "Unknown";
}

inline uint32_t httpSharedCrc32Update(uint32_t crc, uint8_t value)
{
    crc ^= value;
    for (int i = 0; i < 8; ++i) {
        crc = (crc & 1u) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
    }
    return crc;
}

inline uint32_t httpSharedCrc32(const uint8_t* bytes, int length)
{
    uint32_t crc = 0xFFFFFFFFu;
    if (!bytes || length < 0) return 0;
    for (int i = 0; i < length; ++i) crc = httpSharedCrc32Update(crc, bytes[i]);
    return crc ^ 0xFFFFFFFFu;
}

inline uint32_t httpSharedAdler32(const uint8_t* bytes, int length)
{
    const uint32_t mod = 65521u;
    uint32_t a = 1u;
    uint32_t b = 0u;
    if (!bytes || length < 0) return 0;
    for (int i = 0; i < length; ++i) {
        a += bytes[i];
        if (a >= mod) a %= mod;
        b += a;
        if (b >= mod) b %= mod;
    }
    return (b << 16) | a;
}

inline void httpSharedAdler32Update(uint32_t* a, uint32_t* b, uint8_t value)
{
    if (!a || !b) return;
    const uint32_t mod = 65521u;
    *a += value;
    if (*a >= mod) *a %= mod;
    *b += *a;
    if (*b >= mod) *b %= mod;
}

inline uint32_t httpSharedReadLe32(const uint8_t* bytes)
{
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
}

inline uint32_t httpSharedReadBe32(const uint8_t* bytes)
{
    return ((uint32_t)bytes[0] << 24) |
        ((uint32_t)bytes[1] << 16) |
        ((uint32_t)bytes[2] << 8) |
        (uint32_t)bytes[3];
}

inline uint32_t httpSharedReverseBits(uint32_t value, int count)
{
    uint32_t result = 0;
    for (int i = 0; i < count; ++i) {
        result = (result << 1) | (value & 1u);
        value >>= 1;
    }
    return result;
}

inline bool httpSharedBuildHuffman(HttpContentDecoderHuffmanTable* table,
                                   const uint8_t* lengths, int symbolCount,
                                   bool allowEmpty)
{
    if (!table || !lengths || symbolCount <= 0 || symbolCount > kHttpContentDecoderMaxSymbols) return false;
    table->symbolCount = symbolCount;
    int counts[16];
    int nextCode[16];
    for (int i = 0; i < kHttpContentDecoderFastSize; ++i) table->fast[i] = 0;
    for (int i = 0; i < 16; ++i) {
        counts[i] = 0;
        nextCode[i] = 0;
    }
    bool any = false;
    for (int symbol = 0; symbol < symbolCount; ++symbol) {
        const int length = lengths[symbol];
        if (length > 15) return false;
        table->codeLengths[symbol] = (uint8_t)length;
        table->reversedCodes[symbol] = 0;
        if (length > 0) {
            ++counts[length];
            any = true;
        }
    }
    if (!any) return allowEmpty;

    int left = 1;
    for (int length = 1; length <= 15; ++length) {
        left <<= 1;
        left -= counts[length];
        if (left < 0) return false;
    }

    int code = 0;
    for (int length = 1; length <= 15; ++length) {
        code = (code + counts[length - 1]) << 1;
        nextCode[length] = code;
    }
    for (int symbol = 0; symbol < symbolCount; ++symbol) {
        const int length = table->codeLengths[symbol];
        if (length == 0) continue;
        const uint32_t reversed = httpSharedReverseBits((uint32_t)nextCode[length]++, length);
        table->reversedCodes[symbol] = (uint16_t)reversed;
        if (length <= kHttpContentDecoderFastBits) {
            const int step = 1 << length;
            for (int index = (int)reversed; index < kHttpContentDecoderFastSize; index += step) {
                table->fast[index] = (uint16_t)((length << kHttpContentDecoderFastBits) | (symbol + 1));
            }
        }
    }
    return true;
}

struct HttpSharedDeflateReader {
    const uint8_t* input;
    int inputLength;
    int inputPosition;
    uint32_t bitBuffer;
    int bitCount;
    uint8_t* output;
    int outputCapacity;
    int outputLength;
    int outputOffset;
    HttpContentDecoderSink* sink;
    uint8_t* history;
    int historyCapacity;
    int checksumMode;
    uint32_t checksumCrc;
    uint32_t checksumA;
    uint32_t checksumB;
    uint32_t operations;
    uint32_t operationLimit;
};

inline bool httpSharedDeflateEnsureBits(HttpSharedDeflateReader* reader, int count)
{
    if (!reader || count < 0 || count > 24) return false;
    while (reader->bitCount < count) {
        if (reader->inputPosition >= reader->inputLength) return false;
        reader->bitBuffer |= (uint32_t)reader->input[reader->inputPosition++] << reader->bitCount;
        reader->bitCount += 8;
    }
    return true;
}

inline bool httpSharedDeflateReadBits(HttpSharedDeflateReader* reader, int count, uint32_t* value)
{
    if (!reader || !value || count < 0 || count > 24) return false;
    if (!httpSharedDeflateEnsureBits(reader, count)) return false;
    const uint32_t mask = count == 0 ? 0u : ((1u << count) - 1u);
    *value = reader->bitBuffer & mask;
    reader->bitBuffer >>= count;
    reader->bitCount -= count;
    return true;
}

inline bool httpSharedDeflateDecodeSymbol(HttpSharedDeflateReader* reader,
                                          const HttpContentDecoderHuffmanTable* table,
                                          int* symbol)
{
    if (!reader || !table || !symbol || table->symbolCount <= 0) return false;
    if (reader->bitCount >= kHttpContentDecoderFastBits) {
        const uint16_t fast = table->fast[reader->bitBuffer & (kHttpContentDecoderFastSize - 1)];
        if (fast) {
            const int length = fast >> kHttpContentDecoderFastBits;
            uint32_t ignored = 0;
            if (!httpSharedDeflateReadBits(reader, length, &ignored)) return false;
            *symbol = (fast & ((1 << kHttpContentDecoderFastBits) - 1)) - 1;
            return true;
        }
    }

    uint32_t code = 0;
    for (int length = 1; length <= 15; ++length) {
        uint32_t bit = 0;
        if (!httpSharedDeflateReadBits(reader, 1, &bit)) return false;
        code |= bit << (length - 1);
        for (int candidate = 0; candidate < table->symbolCount; ++candidate) {
            if (table->codeLengths[candidate] == length && table->reversedCodes[candidate] == code) {
                *symbol = candidate;
                return true;
            }
        }
    }
    return false;
}

inline bool httpSharedDeflateAlignByte(HttpSharedDeflateReader* reader)
{
    if (!reader) return false;
    const int discard = reader->bitCount & 7;
    uint32_t ignored = 0;
    return httpSharedDeflateReadBits(reader, discard, &ignored);
}

inline bool httpSharedDeflateWriteByte(HttpSharedDeflateReader* reader, uint8_t value)
{
    if (!reader) return false;
    if (reader->outputLength >= reader->outputCapacity) {
        if (reader->sink) reader->sink->capacityExceeded = true;
        reader->outputLength = reader->outputCapacity + 1;
        return false;
    }
    if (reader->sink) {
        if (!reader->sink->writeByte ||
            !reader->sink->writeByte(reader->sink->context, value)) {
            if (reader->sink->bytesWritten >= reader->sink->capacityBytes) {
                reader->sink->capacityExceeded = true;
            } else {
                reader->sink->allocationFailed = true;
            }
            reader->outputLength = reader->outputCapacity + 1;
            return false;
        }
        ++reader->sink->bytesWritten;
    } else {
        if (!reader->output) return false;
        reader->output[reader->outputLength] = value;
    }

    const int streamOffset = reader->outputLength - reader->outputOffset;
    if (reader->history && reader->historyCapacity > 0) {
        reader->history[streamOffset % reader->historyCapacity] = value;
    }
    if (reader->checksumMode == 1) {
        reader->checksumCrc = httpSharedCrc32Update(reader->checksumCrc, value);
    } else if (reader->checksumMode == 2) {
        httpSharedAdler32Update(&reader->checksumA, &reader->checksumB, value);
    }
    ++reader->outputLength;
    return true;
}

inline bool httpSharedDeflateReadBackByte(const HttpSharedDeflateReader* reader,
                                          int distance, uint8_t* value)
{
    if (!reader || !value || distance <= 0 || distance > reader->outputLength - reader->outputOffset) {
        return false;
    }
    const int source = reader->outputLength - distance;
    if (reader->sink) {
        if (!reader->history || reader->historyCapacity <= 0) return false;
        const int streamOffset = source - reader->outputOffset;
        *value = reader->history[streamOffset % reader->historyCapacity];
        return true;
    }
    if (!reader->output) return false;
    *value = reader->output[source];
    return true;
}

inline bool httpSharedDeflateBuildFixed(HttpContentDecoderWorkspace* workspace)
{
    if (!workspace) return false;
    for (int i = 0; i < 288; ++i) {
        if (i <= 143) workspace->codeLengths[i] = 8;
        else if (i <= 255) workspace->codeLengths[i] = 9;
        else if (i <= 279) workspace->codeLengths[i] = 7;
        else workspace->codeLengths[i] = 8;
    }
    if (!httpSharedBuildHuffman(&workspace->literalLength, workspace->codeLengths, 288, false)) return false;
    for (int i = 0; i < 32; ++i) workspace->codeLengths[i] = 5;
    return httpSharedBuildHuffman(&workspace->distance, workspace->codeLengths, 32, false);
}

inline bool httpSharedDeflateDecode(const uint8_t* input, int inputLength,
                                    uint8_t* output, int outputCapacity,
                                    int outputOffset, int* outputLength,
                                    int* consumed,
                                    HttpContentDecoderWorkspace* workspace,
                                    HttpContentDecoderSink* sink = nullptr,
                                    int checksumMode = 0)
{
    if (outputLength) *outputLength = outputOffset;
    if (consumed) *consumed = 0;
    if (!input || inputLength < 0 || (!output && !sink) || outputCapacity < 0 ||
        outputOffset < 0 || outputOffset > outputCapacity || !workspace) return false;

    HttpSharedDeflateReader reader{};
    reader.input = input;
    reader.inputLength = inputLength;
    reader.output = output;
    reader.outputCapacity = outputCapacity;
    reader.outputLength = outputOffset;
    reader.outputOffset = outputOffset;
    reader.sink = sink;
    reader.history = sink ? sink->history : nullptr;
    reader.historyCapacity = sink ? sink->historyCapacity : 0;
    reader.checksumMode = checksumMode;
    reader.checksumCrc = 0xFFFFFFFFu;
    reader.checksumA = 1u;
    reader.checksumB = 0u;
    reader.operationLimit = (uint32_t)inputLength * 64u + (uint32_t)outputCapacity * 4u + 1024u;
    if (reader.operationLimit < 1024u) reader.operationLimit = 1024u;

    static const int lengthBase[29] = {
        3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,
        67,83,99,115,131,163,195,227,258
    };
    static const int lengthExtra[29] = {
        0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
    };
    static const int distanceBase[30] = {
        1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,
        1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
    };
    static const int distanceExtra[30] = {
        0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
    };

    bool finalBlock = false;
    while (!finalBlock) {
        if (++reader.operations > reader.operationLimit) return false;
        uint32_t finalValue = 0;
        uint32_t typeValue = 0;
        if (!httpSharedDeflateReadBits(&reader, 1, &finalValue) ||
            !httpSharedDeflateReadBits(&reader, 2, &typeValue)) return false;
        finalBlock = finalValue != 0;
        if (typeValue == 0) {
            if (!httpSharedDeflateAlignByte(&reader)) return false;
            uint32_t lenValue = 0;
            uint32_t inverseValue = 0;
            if (!httpSharedDeflateReadBits(&reader, 16, &lenValue) ||
                !httpSharedDeflateReadBits(&reader, 16, &inverseValue) ||
                ((lenValue ^ 0xFFFFu) != inverseValue)) return false;
            if (lenValue > (uint32_t)(outputCapacity - reader.outputLength)) {
                reader.outputLength = outputCapacity + 1;
                if (outputLength) *outputLength = reader.outputLength;
                return false;
            }
            if (lenValue > (uint32_t)(reader.inputLength - reader.inputPosition)) return false;
            for (uint32_t i = 0; i < lenValue; ++i) {
                if (!httpSharedDeflateWriteByte(&reader, reader.input[reader.inputPosition++])) {
                    if (outputLength) *outputLength = reader.outputLength;
                    return false;
                }
                if (++reader.operations > reader.operationLimit) return false;
            }
            continue;
        }
        if (typeValue == 3) return false;

        if (typeValue == 1) {
            if (!httpSharedDeflateBuildFixed(workspace)) return false;
        } else {
            uint32_t hlitValue = 0;
            uint32_t hdistValue = 0;
            uint32_t hclenValue = 0;
            if (!httpSharedDeflateReadBits(&reader, 5, &hlitValue) ||
                !httpSharedDeflateReadBits(&reader, 5, &hdistValue) ||
                !httpSharedDeflateReadBits(&reader, 4, &hclenValue)) return false;
            const int literalCount = (int)hlitValue + 257;
            const int distanceCount = (int)hdistValue + 1;
            const int codeLengthCount = (int)hclenValue + 4;
            if (literalCount > 288 || distanceCount > 32 || codeLengthCount > 19) return false;
            static const uint8_t order[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
            uint8_t codeLengthLengths[19];
            for (int i = 0; i < 19; ++i) codeLengthLengths[i] = 0;
            for (int i = 0; i < codeLengthCount; ++i) {
                uint32_t length = 0;
                if (!httpSharedDeflateReadBits(&reader, 3, &length)) return false;
                codeLengthLengths[order[i]] = (uint8_t)length;
            }
            if (!httpSharedBuildHuffman(&workspace->codeLength, codeLengthLengths, 19, false)) return false;
            const int totalLengths = literalCount + distanceCount;
            int lengthIndex = 0;
            uint8_t previous = 0;
            while (lengthIndex < totalLengths) {
                int symbol = 0;
                if (!httpSharedDeflateDecodeSymbol(&reader, &workspace->codeLength, &symbol)) return false;
                if (symbol <= 15) {
                    workspace->codeLengths[lengthIndex++] = (uint8_t)symbol;
                    previous = (uint8_t)symbol;
                } else if (symbol == 16) {
                    if (lengthIndex == 0) return false;
                    uint32_t repeatValue = 0;
                    if (!httpSharedDeflateReadBits(&reader, 2, &repeatValue)) return false;
                    const int repeat = (int)repeatValue + 3;
                    if (repeat > totalLengths - lengthIndex) return false;
                    for (int i = 0; i < repeat; ++i) workspace->codeLengths[lengthIndex++] = previous;
                } else if (symbol == 17 || symbol == 18) {
                    uint32_t repeatValue = 0;
                    if (!httpSharedDeflateReadBits(&reader, symbol == 17 ? 3 : 7, &repeatValue)) return false;
                    const int repeat = (int)repeatValue + (symbol == 17 ? 3 : 11);
                    if (repeat > totalLengths - lengthIndex) return false;
                    for (int i = 0; i < repeat; ++i) workspace->codeLengths[lengthIndex++] = 0;
                    previous = 0;
                } else {
                    return false;
                }
                if (++reader.operations > reader.operationLimit) return false;
            }
            if (!httpSharedBuildHuffman(&workspace->literalLength, workspace->codeLengths, literalCount, false) ||
                !httpSharedBuildHuffman(&workspace->distance, workspace->codeLengths + literalCount, distanceCount, true)) return false;
            if (workspace->codeLengths[256] == 0) return false;
        }

        bool endOfBlock = false;
        while (!endOfBlock) {
            int symbol = 0;
            if (!httpSharedDeflateDecodeSymbol(&reader, &workspace->literalLength, &symbol)) return false;
            if (++reader.operations > reader.operationLimit) return false;
            if (symbol < 256) {
                if (!httpSharedDeflateWriteByte(&reader, (uint8_t)symbol)) {
                    if (outputLength) *outputLength = reader.outputLength;
                    return false;
                }
            } else if (symbol == 256) {
                endOfBlock = true;
            } else {
                if (symbol < 257 || symbol > 285) return false;
                const int lengthIndex = symbol - 257;
                int length = lengthBase[lengthIndex];
                if (lengthExtra[lengthIndex] > 0) {
                    uint32_t extra = 0;
                    if (!httpSharedDeflateReadBits(&reader, lengthExtra[lengthIndex], &extra)) return false;
                    length += (int)extra;
                }
                int distanceSymbol = 0;
                if (!httpSharedDeflateDecodeSymbol(&reader, &workspace->distance, &distanceSymbol) ||
                    distanceSymbol < 0 || distanceSymbol >= 30) return false;
                int distance = distanceBase[distanceSymbol];
                if (distanceExtra[distanceSymbol] > 0) {
                    uint32_t extra = 0;
                    if (!httpSharedDeflateReadBits(&reader, distanceExtra[distanceSymbol], &extra)) return false;
                    distance += (int)extra;
                }
                if (distance <= 0 || distance > 32768 ||
                    distance > reader.outputLength - reader.outputOffset) return false;
                if (length > reader.outputCapacity - reader.outputLength) {
                    reader.outputLength = reader.outputCapacity + 1;
                    if (outputLength) *outputLength = reader.outputLength;
                    return false;
                }
                for (int i = 0; i < length; ++i) {
                    uint8_t backReference = 0;
                    if (!httpSharedDeflateReadBackByte(&reader, distance, &backReference) ||
                        !httpSharedDeflateWriteByte(&reader, backReference)) {
                        if (outputLength) *outputLength = reader.outputLength;
                        return false;
                    }
                    if (++reader.operations > reader.operationLimit) return false;
                }
            }
        }
    }

    if (!httpSharedDeflateAlignByte(&reader)) return false;
    if (outputLength) *outputLength = reader.outputLength;
    workspace->lastChecksumMode = checksumMode;
    workspace->lastChecksum = checksumMode == 1
        ? (reader.checksumCrc ^ 0xFFFFFFFFu)
        : (checksumMode == 2 ? ((reader.checksumB << 16) | reader.checksumA) : 0u);
    if (consumed) {
        const int unreadWholeBytes = reader.bitCount / 8;
        *consumed = reader.inputPosition - unreadWholeBytes;
    }
    return true;
}

inline bool httpSharedGzipHeaderByte(const uint8_t* input, int inputLength, int* position,
                                     uint32_t* headerCrc, uint8_t* value)
{
    if (!input || !position || !headerCrc || !value || *position < 0 || *position >= inputLength) return false;
    *value = input[(*position)++];
    *headerCrc = httpSharedCrc32Update(*headerCrc, *value);
    return true;
}

inline HttpContentDecodeResult httpSharedDecodeContent(const uint8_t* encoded, int encodedLength,
                                                       HttpContentCoding coding,
                                                       uint8_t* decoded, int decodedCapacity,
                                                       int* decodedLength,
                                                       HttpContentDecoderWorkspace* workspace,
                                                       char* error, int errorLength,
                                                       HttpContentDecoderSink* sink = nullptr)
{
    if (decodedLength) *decodedLength = 0;
    if (error && errorLength > 0) error[0] = '\0';
    if (!encoded || encodedLength < 0 || (!decoded && !sink) || decodedCapacity < 0 || !workspace) {
        httpSharedDecoderCopyLiteral("Malformed compressed response", error, errorLength);
        return HttpContentDecodeResult::MalformedCompressedResponse;
    }
    if (sink) {
        sink->capacityExceeded = false;
        sink->allocationFailed = false;
        sink->bytesWritten = 0;
        if (!sink->writeByte || sink->capacityBytes < 0) {
            httpSharedDecoderCopyLiteral("Document storage sink was invalid", error, errorLength);
            return HttpContentDecodeResult::OutputAllocationFailed;
        }
    }
    if (coding == HttpContentCoding::Unsupported) {
        httpSharedDecoderCopyLiteral("Unsupported Content-Encoding", error, errorLength);
        return HttpContentDecodeResult::UnsupportedEncoding;
    }
    if (coding == HttpContentCoding::Identity) {
        if (encodedLength > decodedCapacity) {
            httpSharedDecoderCopyLiteral("Decoded response exceeded the safety limit", error, errorLength);
            return HttpContentDecodeResult::DecodedResponseTooLarge;
        }
        for (int i = 0; i < encodedLength; ++i) {
            if (sink) {
                if (sink->bytesWritten >= sink->capacityBytes ||
                    !sink->writeByte(sink->context, encoded[i])) {
                    if (sink->bytesWritten >= sink->capacityBytes) sink->capacityExceeded = true;
                    else sink->allocationFailed = true;
                    httpSharedDecoderCopyLiteral(
                        sink->capacityExceeded ? "Decoded response exceeded the safety limit" :
                            "Document storage allocation failed", error, errorLength);
                    return sink->capacityExceeded
                        ? HttpContentDecodeResult::DecodedResponseTooLarge
                        : HttpContentDecodeResult::OutputAllocationFailed;
                }
                ++sink->bytesWritten;
            } else {
                decoded[i] = encoded[i];
            }
        }
        if (decodedLength) *decodedLength = encodedLength;
        return HttpContentDecodeResult::Success;
    }

    if (sink) {
        if (!sink->prepareHistory ||
            !sink->prepareHistory(sink->context, &sink->history, &sink->historyCapacity) ||
            !sink->history || sink->historyCapacity < 32768) {
            sink->allocationFailed = true;
            httpSharedDecoderCopyLiteral("Document storage allocation failed", error, errorLength);
            return HttpContentDecodeResult::OutputAllocationFailed;
        }
    }

    if (coding == HttpContentCoding::Deflate) {
        if (encodedLength < 6) {
            httpSharedDecoderCopyLiteral("Malformed zlib-wrapped deflate response", error, errorLength);
            return HttpContentDecodeResult::MalformedCompressedResponse;
        }
        const uint8_t cmf = encoded[0];
        const uint8_t flg = encoded[1];
        if ((cmf & 0x0Fu) != 8u || (cmf >> 4) > 7u ||
            ((((uint32_t)cmf << 8) | flg) % 31u) != 0u) {
            httpSharedDecoderCopyLiteral("Malformed zlib header", error, errorLength);
            return HttpContentDecodeResult::MalformedCompressedResponse;
        }
        if (flg & 0x20u) {
            httpSharedDecoderCopyLiteral("Preset dictionary is unsupported", error, errorLength);
            return HttpContentDecodeResult::MalformedCompressedResponse;
        }
        const int deflateLength = encodedLength - 2 - 4;
        int outputLengthValue = 0;
        int consumed = 0;
        if (!httpSharedDeflateDecode(encoded + 2, deflateLength, decoded, decodedCapacity, 0,
                                     &outputLengthValue, &consumed, workspace, sink, 2)) {
            if (sink && sink->allocationFailed && !sink->capacityExceeded) {
                httpSharedDecoderCopyLiteral("Document storage allocation failed", error, errorLength);
                return HttpContentDecodeResult::OutputAllocationFailed;
            }
            if (outputLengthValue > decodedCapacity) {
                if (decodedLength) *decodedLength = outputLengthValue;
                httpSharedDecoderCopyLiteral("Decoded response exceeded the safety limit", error, errorLength);
                return HttpContentDecodeResult::DecodedResponseTooLarge;
            }
            httpSharedDecoderCopyLiteral("Malformed deflate stream", error, errorLength);
            return HttpContentDecodeResult::MalformedCompressedResponse;
        }
        if (consumed != deflateLength ||
            httpSharedReadBe32(encoded + encodedLength - 4) != workspace->lastChecksum) {
            httpSharedDecoderCopyLiteral("Invalid zlib Adler-32 or trailing data", error, errorLength);
            return HttpContentDecodeResult::MalformedCompressedResponse;
        }
        if (decodedLength) *decodedLength = outputLengthValue;
        return HttpContentDecodeResult::Success;
    }

    int position = 0;
    int memberCount = 0;
    int totalOutputLength = 0;
    while (position < encodedLength) {
        if (++memberCount > 64 || encodedLength - position < 18 ||
            encoded[position] != 0x1Fu || encoded[position + 1] != 0x8Bu || encoded[position + 2] != 8u) {
            httpSharedDecoderCopyLiteral("Malformed gzip member", error, errorLength);
            return HttpContentDecodeResult::MalformedCompressedResponse;
        }
        const int memberStart = position;
        uint32_t headerCrc = 0xFFFFFFFFu;
        uint8_t value = 0;
        for (int i = 0; i < 10; ++i) {
            if (!httpSharedGzipHeaderByte(encoded, encodedLength, &position, &headerCrc, &value)) {
                httpSharedDecoderCopyLiteral("Truncated gzip header", error, errorLength);
                return HttpContentDecodeResult::MalformedCompressedResponse;
            }
        }
        const uint8_t flags = encoded[memberStart + 3];
        if (flags & 0xE0u) {
            httpSharedDecoderCopyLiteral("Gzip reserved flags are set", error, errorLength);
            return HttpContentDecodeResult::MalformedCompressedResponse;
        }
        if (flags & 0x04u) {
            uint8_t lo = 0;
            uint8_t hi = 0;
            if (!httpSharedGzipHeaderByte(encoded, encodedLength, &position, &headerCrc, &lo) ||
                !httpSharedGzipHeaderByte(encoded, encodedLength, &position, &headerCrc, &hi)) {
                httpSharedDecoderCopyLiteral("Truncated gzip extra field length", error, errorLength);
                return HttpContentDecodeResult::MalformedCompressedResponse;
            }
            const int extraLength = (int)lo | ((int)hi << 8);
            if (extraLength > encodedLength - position) {
                httpSharedDecoderCopyLiteral("Truncated gzip extra field", error, errorLength);
                return HttpContentDecodeResult::MalformedCompressedResponse;
            }
            for (int i = 0; i < extraLength; ++i) {
                if (!httpSharedGzipHeaderByte(encoded, encodedLength, &position, &headerCrc, &value)) {
                    httpSharedDecoderCopyLiteral("Truncated gzip extra field", error, errorLength);
                    return HttpContentDecodeResult::MalformedCompressedResponse;
                }
            }
        }
        if (flags & 0x08u) {
            do {
                if (!httpSharedGzipHeaderByte(encoded, encodedLength, &position, &headerCrc, &value)) {
                    httpSharedDecoderCopyLiteral("Truncated gzip filename", error, errorLength);
                    return HttpContentDecodeResult::MalformedCompressedResponse;
                }
            } while (value != 0);
        }
        if (flags & 0x10u) {
            do {
                if (!httpSharedGzipHeaderByte(encoded, encodedLength, &position, &headerCrc, &value)) {
                    httpSharedDecoderCopyLiteral("Truncated gzip comment", error, errorLength);
                    return HttpContentDecodeResult::MalformedCompressedResponse;
                }
            } while (value != 0);
        }
        if (flags & 0x02u) {
            if (position + 2 > encodedLength) {
                httpSharedDecoderCopyLiteral("Truncated gzip header CRC", error, errorLength);
                return HttpContentDecodeResult::MalformedCompressedResponse;
            }
            const uint16_t expected = (uint16_t)encoded[position] | ((uint16_t)encoded[position + 1] << 8);
            const uint16_t actual = (uint16_t)((headerCrc ^ 0xFFFFFFFFu) & 0xFFFFu);
            position += 2;
            if (expected != actual) {
                httpSharedDecoderCopyLiteral("Invalid gzip header CRC", error, errorLength);
                return HttpContentDecodeResult::MalformedCompressedResponse;
            }
        }

        const int memberOutputStart = totalOutputLength;
        int outputLengthValue = memberOutputStart;
        int consumed = 0;
        if (!httpSharedDeflateDecode(encoded + position, encodedLength - position, decoded,
                                     decodedCapacity, memberOutputStart, &outputLengthValue,
                                     &consumed, workspace, sink, 1)) {
            if (sink && sink->allocationFailed && !sink->capacityExceeded) {
                httpSharedDecoderCopyLiteral("Document storage allocation failed", error, errorLength);
                return HttpContentDecodeResult::OutputAllocationFailed;
            }
            if (outputLengthValue > decodedCapacity) {
                if (decodedLength) *decodedLength = outputLengthValue;
                httpSharedDecoderCopyLiteral("Decoded response exceeded the safety limit", error, errorLength);
                return HttpContentDecodeResult::DecodedResponseTooLarge;
            }
            httpSharedDecoderCopyLiteral("Malformed deflate stream in gzip member", error, errorLength);
            return HttpContentDecodeResult::MalformedCompressedResponse;
        }
        if (consumed < 0 || consumed > encodedLength - position || position + consumed > encodedLength ||
            encodedLength - (position + consumed) < 8) {
            httpSharedDecoderCopyLiteral("Truncated gzip trailer", error, errorLength);
            return HttpContentDecodeResult::MalformedCompressedResponse;
        }
        const int trailer = position + consumed;
        const uint32_t expectedCrc = httpSharedReadLe32(encoded + trailer);
        const uint32_t expectedSize = httpSharedReadLe32(encoded + trailer + 4);
        const uint32_t actualCrc = workspace->lastChecksum;
        const uint32_t actualSize = (uint32_t)(outputLengthValue - memberOutputStart);
        if (expectedCrc != actualCrc) {
            httpSharedDecoderCopyLiteral("Invalid gzip CRC32", error, errorLength);
            return HttpContentDecodeResult::MalformedCompressedResponse;
        }
        if (expectedSize != actualSize) {
            httpSharedDecoderCopyLiteral("Invalid gzip ISIZE", error, errorLength);
            return HttpContentDecodeResult::MalformedCompressedResponse;
        }
        totalOutputLength = outputLengthValue;
        position = trailer + 8;
        if (position < encodedLength &&
            (encodedLength - position < 2 || encoded[position] != 0x1Fu || encoded[position + 1] != 0x8Bu)) {
            httpSharedDecoderCopyLiteral("Unexpected trailing bytes after gzip member", error, errorLength);
            return HttpContentDecodeResult::MalformedCompressedResponse;
        }
    }
    if (decodedLength) *decodedLength = totalOutputLength;
    return HttpContentDecodeResult::Success;
}

} // namespace web
} // namespace gxos
