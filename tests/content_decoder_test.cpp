#include "guide_web_http_shared.h"
#include "guide_web_document_storage.h"
#include "guide_web_html_parser.h"
#include "guide_web_http.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace gxos {
namespace web {

// The parser boundary test does not perform network I/O.  This stub keeps
// the test focused on source handoff while satisfying the parser's bounded
// resource-fetch linkage.
HttpResponse fetchHttpUrl(const std::string&)
{
    return HttpResponse{};
}

} // namespace web
} // namespace gxos

namespace {

bool expect(bool value, const char* name)
{
    if (!value) std::cerr << "FAIL: " << name << "\n";
    return value;
}

std::vector<uint8_t> fromHex(const char* text)
{
    std::vector<uint8_t> bytes;
    for (size_t i = 0; text[i]; i += 2) {
        auto nibble = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(10 + c - 'a');
            return static_cast<uint8_t>(10 + c - 'A');
        };
        bytes.push_back(static_cast<uint8_t>((nibble(text[i]) << 4) | nibble(text[i + 1])));
    }
    return bytes;
}

std::vector<uint8_t> makeGzip(const std::vector<uint8_t>& raw, const std::vector<uint8_t>& plain)
{
    std::vector<uint8_t> gzip = { 0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 0 };
    gzip.insert(gzip.end(), raw.begin(), raw.end());
    const uint32_t crc = gxos::web::httpSharedCrc32(plain.data(), static_cast<int>(plain.size()));
    for (int shift = 0; shift < 32; shift += 8) gzip.push_back(static_cast<uint8_t>(crc >> shift));
    const uint32_t size = static_cast<uint32_t>(plain.size());
    for (int shift = 0; shift < 32; shift += 8) gzip.push_back(static_cast<uint8_t>(size >> shift));
    return gzip;
}

std::vector<uint8_t> makeZlib(const std::vector<uint8_t>& raw, const std::vector<uint8_t>& plain)
{
    std::vector<uint8_t> zlib = { 0x78, 0xda };
    zlib.insert(zlib.end(), raw.begin(), raw.end());
    const uint32_t adler = gxos::web::httpSharedAdler32(plain.data(), static_cast<int>(plain.size()));
    for (int shift = 24; shift >= 0; shift -= 8) zlib.push_back(static_cast<uint8_t>(adler >> shift));
    return zlib;
}

std::vector<uint8_t> makeStoredDeflate(const std::vector<uint8_t>& plain)
{
    std::vector<uint8_t> raw;
    size_t offset = 0;
    if (plain.empty()) {
        raw = { 0x01, 0x00, 0x00, 0xff, 0xff };
        return raw;
    }
    while (offset < plain.size()) {
        const size_t remaining = plain.size() - offset;
        const uint16_t blockLength = static_cast<uint16_t>(remaining > 65535u ? 65535u : remaining);
        const bool finalBlock = offset + blockLength == plain.size();
        raw.push_back(finalBlock ? 0x01 : 0x00);
        raw.push_back(static_cast<uint8_t>(blockLength & 0xffu));
        raw.push_back(static_cast<uint8_t>(blockLength >> 8));
        const uint16_t inverse = static_cast<uint16_t>(~blockLength);
        raw.push_back(static_cast<uint8_t>(inverse & 0xffu));
        raw.push_back(static_cast<uint8_t>(inverse >> 8));
        raw.insert(raw.end(), plain.begin() + static_cast<std::ptrdiff_t>(offset),
                   plain.begin() + static_cast<std::ptrdiff_t>(offset + blockLength));
        offset += blockLength;
    }
    return raw;
}

struct DeflateBitWriter
{
    std::vector<uint8_t> bytes;
    int bit = 0;

    void write(uint32_t value, int count)
    {
        for (int i = 0; i < count; ++i) {
            if (bit == 0) bytes.push_back(0);
            if (value & 1u) bytes.back() = static_cast<uint8_t>(bytes.back() | (1u << bit));
            value >>= 1;
            bit = (bit + 1) & 7;
        }
    }
};

void writeFixedLiteral(DeflateBitWriter& writer, int symbol)
{
    uint32_t code = 0;
    int length = 0;
    if (symbol <= 143) {
        code = 0x30u + static_cast<uint32_t>(symbol);
        length = 8;
    } else if (symbol <= 255) {
        code = 0x190u + static_cast<uint32_t>(symbol - 144);
        length = 9;
    } else if (symbol <= 279) {
        code = static_cast<uint32_t>(symbol - 256);
        length = 7;
    } else {
        code = 0xC0u + static_cast<uint32_t>(symbol - 280);
        length = 8;
    }
    writer.write(gxos::web::httpSharedReverseBits(code, length), length);
}

void writeFixedDistance(DeflateBitWriter& writer, int symbol)
{
    writer.write(gxos::web::httpSharedReverseBits(static_cast<uint32_t>(symbol), 5), 5);
}

std::vector<uint8_t> makeFixedDeflateWithRun(const std::string& prefix,
                                             int repeatCount,
                                             const std::string& suffix)
{
    static const int lengthBase[] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
        31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
    };
    static const int lengthExtra[] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
        2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5
    };

    DeflateBitWriter writer;
    writer.write(1, 1);
    writer.write(1, 2);
    for (unsigned char value : prefix) writeFixedLiteral(writer, value);
    if (repeatCount > 0) {
        writeFixedLiteral(writer, 'A');
        int remaining = repeatCount - 1;
        while (remaining > 0) {
            int codeIndex = 0;
            for (int i = 0; i < 28; ++i) {
                const int maximum = lengthBase[i] + ((1 << lengthExtra[i]) - 1);
                if (remaining >= lengthBase[i] && remaining <= maximum) {
                    codeIndex = i;
                    break;
                }
            }
            const int length = remaining >= 258 ? 258 : remaining;
            if (length == 258) codeIndex = 27;
            writeFixedLiteral(writer, 257 + codeIndex);
            if (lengthExtra[codeIndex] > 0) {
                writer.write(static_cast<uint32_t>(length - lengthBase[codeIndex]),
                             lengthExtra[codeIndex]);
            }
            writeFixedDistance(writer, 0);
            remaining -= length;
        }
    }
    for (unsigned char value : suffix) writeFixedLiteral(writer, value);
    writeFixedLiteral(writer, 256);
    return writer.bytes;
}

std::vector<uint8_t> makeLargePlain()
{
    const std::string prefix =
        "<html><head><title>Large compression fixture</title></head><body>";
    const std::string suffix = "</body></html>";
    std::vector<uint8_t> plain;
    plain.insert(plain.end(), prefix.begin(), prefix.end());
    plain.insert(plain.end(), 300000, static_cast<uint8_t>('A'));
    plain.insert(plain.end(), suffix.begin(), suffix.end());
    return plain;
}

std::vector<uint8_t> makeLargeFixedGzip()
{
    const std::string prefix =
        "<html><head><title>Large compression fixture</title></head><body>";
    const std::string suffix = "</body></html>";
    return makeGzip(makeFixedDeflateWithRun(prefix, 300000, suffix), makeLargePlain());
}

std::vector<uint8_t> makeLargeConcatenatedDynamicGzip()
{
    // Each member is a deterministic dynamic-Huffman gzip member.  Repeating
    // members gives a large compressed stream without relying on a host zlib
    // library and also retains coverage for concatenated gzip handling.
    const auto member = fromHex(
        "1f8b080000000000020a358d4b0e80200c05afc20924ee9b6e3c89daaa4df8054aa29e5e94b89e37f3e050ef100e9e0941451d"
        "e3147dca5c8ac4603639b56606db11d83e5c225d4d1a7189351093d96f490d8e08098995b397204565fd0be65506b0a935ba6db"
        "fe70771e9919c80000000");
    std::vector<uint8_t> output;
    for (int i = 0; i < 2100; ++i) output.insert(output.end(), member.begin(), member.end());
    return output;
}

std::vector<uint8_t> makeLargeConcatenatedDynamicPlain()
{
    const std::string member =
        "<html><head><title>Compression fixture</title></head><body><h1>bounded gzip</h1>"
        "<p>deterministic fixture body.</p></body></html>";
    std::vector<uint8_t> output;
    for (int i = 0; i < 2100; ++i) output.insert(output.end(), member.begin(), member.end());
    return output;
}

std::string hexSize(size_t value)
{
    static const char* digits = "0123456789abcdef";
    char reversed[sizeof(size_t) * 2] = {};
    int count = 0;
    do {
        reversed[count++] = digits[value & 0xf];
        value >>= 4;
    } while (value != 0);
    std::string result;
    while (count > 0) result.push_back(reversed[--count]);
    return result;
}

std::vector<uint8_t> makeFixedInvalidDistance()
{
    std::vector<uint8_t> bytes(1, 0);
    int bitPosition = 0;
    auto writeBits = [&](uint32_t value, int count) {
        for (int bit = 0; bit < count; ++bit) {
            if ((value & (1u << bit)) != 0) {
                bytes[static_cast<size_t>(bitPosition / 8)] = static_cast<uint8_t>(
                    bytes[static_cast<size_t>(bitPosition / 8)] | (1u << (bitPosition % 8)));
            }
            ++bitPosition;
            if (bitPosition / 8 >= static_cast<int>(bytes.size())) bytes.push_back(0);
        }
    };

    writeBits(1, 1);       // BFINAL
    writeBits(1, 2);       // fixed Huffman block
    writeBits(64, 7);      // fixed literal/length symbol 257: length 3
    writeBits(0, 5);       // fixed distance symbol 0: distance 1
    writeBits(0, 7);       // end-of-block symbol 256
    return bytes;
}

gxos::web::HttpContentDecodeResult decode(const std::vector<uint8_t>& encoded,
                                          gxos::web::HttpContentCoding coding,
                                          std::vector<uint8_t>& decoded,
                                          int capacity = 4096)
{
    gxos::web::HttpContentDecoderWorkspace workspace{};
    decoded.assign(static_cast<size_t>(capacity), 0);
    int decodedLength = 0;
    char error[96] = {};
    const auto result = gxos::web::httpSharedDecodeContent(
        encoded.data(), static_cast<int>(encoded.size()), coding,
        decoded.data(), capacity, &decodedLength, &workspace, error, sizeof(error));
    if (result == gxos::web::HttpContentDecodeResult::Success) {
        decoded.resize(static_cast<size_t>(decodedLength));
    } else {
        decoded.clear();
    }
    return result;
}

gxos::web::HttpContentDecodeResult decodeIntoStorage(const std::vector<uint8_t>& encoded,
                                                    gxos::web::HttpContentCoding coding,
                                                    gxos::web::BoundedDocumentStorage& storage)
{
    gxos::web::HttpContentDecoderWorkspace workspace{};
    gxos::web::HttpContentDecoderSink sink = storage.decoderSink();
    int decodedLength = 0;
    char error[96] = {};
    const auto result = gxos::web::httpSharedDecodeContent(
        encoded.data(), static_cast<int>(encoded.size()), coding,
        nullptr, storage.capacityBytes(), &decodedLength, &workspace,
        error, sizeof(error), &sink);
    return result;
}

gxos::web::HttpContentDecodeResult decodeToStorage(const std::vector<uint8_t>& encoded,
                                                   gxos::web::HttpContentCoding coding,
                                                   std::vector<uint8_t>& decoded,
                                                   int* segmentsUsed = nullptr,
                                                   int* historyBytes = nullptr)
{
    gxos::web::BoundedDocumentStorage storage;
    const auto result = decodeIntoStorage(encoded, coding, storage);
    if (segmentsUsed) *segmentsUsed = storage.segmentsUsed();
    if (historyBytes) *historyBytes = storage.historyBytes();
    if (result == gxos::web::HttpContentDecodeResult::Success) {
        char* flattened = storage.allocateFlattenedCopy();
        if (!flattened) {
            decoded.clear();
            return gxos::web::HttpContentDecodeResult::OutputAllocationFailed;
        }
        decoded.assign(reinterpret_cast<uint8_t*>(flattened),
                       reinterpret_cast<uint8_t*>(flattened) + storage.size());
        delete[] flattened;
    } else {
        decoded.clear();
    }
    return result;
}

bool copyStorageToVector(gxos::web::BoundedDocumentStorage& storage,
                         std::vector<uint8_t>& decoded)
{
    char* flattened = storage.allocateFlattenedCopy();
    if (!flattened) {
        decoded.clear();
        return false;
    }
    decoded.assign(reinterpret_cast<uint8_t*>(flattened),
                  reinterpret_cast<uint8_t*>(flattened) + storage.size());
    delete[] flattened;
    return true;
}

} // namespace

int main()
{
    using gxos::web::HttpContentCoding;
    using gxos::web::HttpContentDecodeResult;

    const std::string plain =
        "<html><head><title>Compression fixture</title></head><body><h1>bounded gzip</h1>"
        "<p>deterministic fixture body.</p></body></html>";
    const std::vector<uint8_t> body(plain.begin(), plain.end());
    const auto stored = fromHex(
        "0180007fff3c68746d6c3e3c686561643e3c7469746c653e436f6d7072657373696f6e20666978747572653c2f7469746c653e"
        "3c2f686561643e3c626f64793e3c68313e626f756e64656420677a69703c2f68313e3c703e64657465726d696e697374696320"
        "6669787475726520626f64792e3c2f703e3c2f626f64793e3c2f68746d6c3e");
    const auto fixed = fromHex(
        "b3c928c9cdb1b3c9484d4cb1b329c92cc949b573cecf2d284a2d2ececccf5348cbac28292d4ab5d18748d9e8431426e5a7540"
        "23519da25e597e6a5a4a628a457651600250ded6c0aec52524b528b7233f3328b4b329361262880b4e8d9e81700cd80e8d607db0c00");
    const auto dynamic = fromHex(
        "358d4b0e80200c05afc20924ee9b6e3c89daaa4df8054aa29e5e94b89e37f3e050ef100e9e0941451de3147dca5c8ac4603639b"
        "56606db11d83e5c225d4d1a7189351093d96f490d8e08098995b397204565fd0be65506b0a935ba6dbfe707");
    const auto zlib = fromHex(
        "78da358d4b0e80200c05afc20924ee9b6e3c89daaa4df8054aa29e5e94b89e37f3e050ef100e9e0941451de3147dca5c8ac4603639b"
        "56606db11d83e5c225d4d1a7189351093d96f490d8e08098995b397204565fd0be65506b0a935ba6dbfe707c8162e1e");
    const auto zlibStored = makeZlib(stored, body);
    const auto zlibFixed = makeZlib(fixed, body);
    const auto zlibDynamic = makeZlib(dynamic, body);
    const auto gzip = fromHex(
        "1f8b080000000000020a358d4b0e80200c05afc20924ee9b6e3c89daaa4df8054aa29e5e94b89e37f3e050ef100e9e0941451d"
        "e3147dca5c8ac4603639b56606db11d83e5c225d4d1a7189351093d96f490d8e08098995b397204565fd0be65506b0a935ba6db"
        "fe70771e9919c80000000");
    const auto gzipOptional = fromHex(
        "1f8b081e0000000000ff0300666978747572652e68746d6c00636f6d6d656e7400b99b358d4b0e80200c05afc20924ee9b6e3c"
        "89daaa4df8054aa29e5e94b89e37f3e050ef100e9e0941451de3147dca5c8ac4603639b56606db11d83e5c225d4d1a71893510"
        "93d96f490d8e08098995b397204565fd0be65506b0a935ba6dbfe70771e9919c80000000");

    bool ok = true;
    std::vector<uint8_t> decoded;
    ok &= expect(decode(makeGzip(stored, body), HttpContentCoding::Gzip, decoded) == HttpContentDecodeResult::Success &&
                 std::vector<uint8_t>(decoded.begin(), decoded.end()) == body, "gzip stored block");
    ok &= expect(decode(makeGzip(fixed, body), HttpContentCoding::Gzip, decoded) == HttpContentDecodeResult::Success &&
                 decoded == body, "gzip fixed Huffman block");
    ok &= expect(decode(gzip, HttpContentCoding::Gzip, decoded) == HttpContentDecodeResult::Success &&
                 decoded == body, "gzip dynamic Huffman block");
    ok &= expect(decode(gzipOptional, HttpContentCoding::Gzip, decoded) == HttpContentDecodeResult::Success &&
                 decoded == body, "gzip optional filename/comment/FHCRC");
    ok &= expect(decode(makeGzip(stored, body), HttpContentCoding::Gzip, decoded,
                        static_cast<int>(body.size())) == HttpContentDecodeResult::Success &&
                 decoded == body, "gzip output exactly at capacity");
    ok &= expect(decode(zlib, HttpContentCoding::Deflate, decoded) == HttpContentDecodeResult::Success &&
                 decoded == body, "zlib-wrapped dynamic deflate");
    ok &= expect(decode(zlibStored, HttpContentCoding::Deflate, decoded) == HttpContentDecodeResult::Success &&
                 decoded == body, "zlib-wrapped stored block");
    ok &= expect(decode(zlibFixed, HttpContentCoding::Deflate, decoded) == HttpContentDecodeResult::Success &&
                 decoded == body, "zlib-wrapped fixed Huffman block");
    ok &= expect(decode(zlibDynamic, HttpContentCoding::Deflate, decoded) == HttpContentDecodeResult::Success &&
                 decoded == body, "zlib-wrapped dynamic block");
    const auto overCapResult = decode(makeGzip(stored, body), HttpContentCoding::Gzip, decoded, 64);
    ok &= expect(overCapResult == HttpContentDecodeResult::DecodedResponseTooLarge, "decoded output cap");
    ok &= expect(decode(std::vector<uint8_t>{ 0, 0, 0 }, HttpContentCoding::Gzip, decoded) ==
                 HttpContentDecodeResult::MalformedCompressedResponse, "bad gzip magic");
    auto badMethod = gzip;
    badMethod[2] = 9;
    ok &= expect(decode(badMethod, HttpContentCoding::Gzip, decoded) ==
                 HttpContentDecodeResult::MalformedCompressedResponse, "unsupported gzip method");
    auto badFlags = gzip;
    badFlags[3] = 0x20;
    ok &= expect(decode(badFlags, HttpContentCoding::Gzip, decoded) ==
                 HttpContentDecodeResult::MalformedCompressedResponse, "gzip reserved flags");
    auto truncatedHeader = gzip;
    truncatedHeader.resize(8);
    ok &= expect(decode(truncatedHeader, HttpContentCoding::Gzip, decoded) ==
                 HttpContentDecodeResult::MalformedCompressedResponse, "truncated gzip header");
    auto truncatedOptional = gzipOptional;
    truncatedOptional.resize(12);
    ok &= expect(decode(truncatedOptional, HttpContentCoding::Gzip, decoded) ==
                 HttpContentDecodeResult::MalformedCompressedResponse, "truncated gzip optional field");
    auto truncatedDeflate = gzip;
    truncatedDeflate.resize(truncatedDeflate.size() - 9);
    ok &= expect(decode(truncatedDeflate, HttpContentCoding::Gzip, decoded) ==
                 HttpContentDecodeResult::MalformedCompressedResponse, "truncated deflate stream");
    ok &= expect(decode(makeGzip(std::vector<uint8_t>{ 0x07 }, body), HttpContentCoding::Gzip, decoded) ==
                 HttpContentDecodeResult::MalformedCompressedResponse, "malformed Huffman stream");
    const auto invalidDistance = makeFixedInvalidDistance();
    ok &= expect(decode(makeGzip(invalidDistance, body), HttpContentCoding::Gzip, decoded) ==
                 HttpContentDecodeResult::MalformedCompressedResponse, "invalid distance");
    auto badCrc = gzip;
    badCrc[badCrc.size() - 8] ^= 1;
    ok &= expect(decode(badCrc, HttpContentCoding::Gzip, decoded) ==
                 HttpContentDecodeResult::MalformedCompressedResponse, "bad gzip CRC");
    auto badSize = gzip;
    badSize[badSize.size() - 1] ^= 1;
    ok &= expect(decode(badSize, HttpContentCoding::Gzip, decoded) ==
                 HttpContentDecodeResult::MalformedCompressedResponse, "bad gzip ISIZE");
    auto badZlibHeader = zlib;
    badZlibHeader[1] ^= 1;
    ok &= expect(decode(badZlibHeader, HttpContentCoding::Deflate, decoded) ==
                 HttpContentDecodeResult::MalformedCompressedResponse, "bad zlib FCHECK");
    auto badAdler = zlib;
    badAdler[badAdler.size() - 1] ^= 1;
    ok &= expect(decode(badAdler, HttpContentCoding::Deflate, decoded) ==
                 HttpContentDecodeResult::MalformedCompressedResponse, "bad Adler-32");
    auto badZlibMethod = zlib;
    badZlibMethod[0] = 0x79;
    ok &= expect(decode(badZlibMethod, HttpContentCoding::Deflate, decoded) ==
                 HttpContentDecodeResult::MalformedCompressedResponse, "unsupported zlib method");
    ok &= expect(decode(gzip, HttpContentCoding::Deflate, decoded) ==
                 HttpContentDecodeResult::MalformedCompressedResponse, "raw deflate rejected as zlib");
    auto dictionary = zlib;
    dictionary[1] = static_cast<uint8_t>(dictionary[1] | 0x20);
    dictionary[1] = static_cast<uint8_t>(dictionary[1] + (31 - (((dictionary[0] << 8) | dictionary[1]) % 31)) % 31);
    ok &= expect(decode(dictionary, HttpContentCoding::Deflate, decoded) ==
                 HttpContentDecodeResult::MalformedCompressedResponse, "preset dictionary rejected");
    ok &= expect(gxos::web::httpSharedParseContentCoding(" GZiP ") == HttpContentCoding::Gzip &&
                 gxos::web::httpSharedParseContentCoding("gzip, br") == HttpContentCoding::Unsupported,
                 "content encoding token parsing");
    ok &= expect(decode(gzip, HttpContentCoding::Unsupported, decoded) ==
                 HttpContentDecodeResult::UnsupportedEncoding, "unsupported content encoding");
    auto concatenated = gzip;
    concatenated.insert(concatenated.end(), gzip.begin(), gzip.end());
    ok &= expect(decode(concatenated, HttpContentCoding::Gzip, decoded) == HttpContentDecodeResult::Success &&
                 decoded.size() == body.size() * 2, "concatenated gzip members");

    const int decodedCap = gxos::web::kHttpSharedMaxBodyBytes;
    const std::vector<uint8_t> capMinusOnePlain(static_cast<size_t>(decodedCap - 1), 'A');
    const std::vector<uint8_t> exactCapPlain(static_cast<size_t>(decodedCap), 'B');
    const std::vector<uint8_t> capPlusOnePlain(static_cast<size_t>(decodedCap + 1), 'C');
    ok &= expect(decode(makeGzip(makeStoredDeflate(capMinusOnePlain), capMinusOnePlain),
                        HttpContentCoding::Gzip, decoded, decodedCap) == HttpContentDecodeResult::Success &&
                 static_cast<int>(decoded.size()) == decodedCap - 1,
                 "decoded cap minus one passes");
    ok &= expect(decode(makeGzip(makeStoredDeflate(exactCapPlain), exactCapPlain),
                        HttpContentCoding::Gzip, decoded, decodedCap) == HttpContentDecodeResult::Success &&
                 static_cast<int>(decoded.size()) == decodedCap,
                 "decoded exact cap passes inclusively");
    ok &= expect(decode(makeGzip(makeStoredDeflate(capPlusOnePlain), capPlusOnePlain),
                        HttpContentCoding::Gzip, decoded, decodedCap) == HttpContentDecodeResult::DecodedResponseTooLarge,
                 "decoded cap plus one fails before write beyond bound");

    // The document path uses the larger segmented policy, while the generic
    // contiguous decoder above intentionally retains the 256 KiB resource
    // limit.  Exercise both compressed and identity input through the sink.
    const int documentCap = gxos::web::kHttpSharedMaxDecodedDocumentBytes;
    const std::vector<uint8_t> segmentedPlain(90000, 'A');
    const auto segmentedStored = makeStoredDeflate(segmentedPlain);
    int segmentsUsed = 0;
    int historyBytes = 0;
    const auto segmentedGzipResult = decodeToStorage(makeGzip(segmentedStored, segmentedPlain), HttpContentCoding::Gzip,
                                 decoded, &segmentsUsed, &historyBytes);
    ok &= expect(segmentedGzipResult == HttpContentDecodeResult::Success &&
                 decoded == segmentedPlain && segmentsUsed >= 6 && historyBytes == 32768,
                 "segmented gzip output crosses segment boundaries");
    ok &= expect(decodeToStorage(gzip, HttpContentCoding::Gzip, decoded) == HttpContentDecodeResult::Success &&
                 decoded == body, "segmented gzip dynamic-Huffman output");
    const auto segmentedZlib = makeZlib(segmentedStored, segmentedPlain);
    const auto segmentedZlibResult = decodeToStorage(segmentedZlib, HttpContentCoding::Deflate,
                                 decoded, &segmentsUsed, &historyBytes);
    ok &= expect(segmentedZlibResult == HttpContentDecodeResult::Success &&
                 decoded == segmentedPlain && segmentsUsed >= 6 && historyBytes == 32768,
                 "segmented zlib-deflate output crosses segment boundaries");
    ok &= expect(decodeToStorage(segmentedPlain, HttpContentCoding::Identity, decoded,
                                 &segmentsUsed, &historyBytes) == HttpContentDecodeResult::Success &&
                 decoded == segmentedPlain && segmentsUsed >= 6 && historyBytes == 0,
                 "segmented identity output crosses segment boundaries");

    const std::vector<uint8_t> largePlain = makeLargePlain();
    const int largeExpectedSegments = (static_cast<int>(largePlain.size()) +
        gxos::web::kHttpSharedDecodedDocumentSegmentBytes - 1) /
        gxos::web::kHttpSharedDecodedDocumentSegmentBytes;
    gxos::web::BoundedDocumentStorage largeStorage;
    ok &= expect(static_cast<int>(largePlain.size()) > gxos::web::kHttpSharedMaxBodyBytes &&
                 static_cast<int>(largePlain.size()) <= documentCap &&
                 decodeIntoStorage(largePlain, HttpContentCoding::Identity, largeStorage) ==
                     HttpContentDecodeResult::Success &&
                 largeStorage.size() == static_cast<int>(largePlain.size()) &&
                 largeStorage.segmentsUsed() == largeExpectedSegments &&
                 largeStorage.allocatedBytes() == largeExpectedSegments *
                     gxos::web::kHttpSharedDecodedDocumentSegmentBytes &&
                 largeStorage.historyBytes() == 0 && copyStorageToVector(largeStorage, decoded) &&
                 decoded == largePlain,
                 "large identity document uses on-demand segments");
    largeStorage.reset();

    const auto largeFixedGzip = makeLargeFixedGzip();
    ok &= expect(decodeIntoStorage(largeFixedGzip, HttpContentCoding::Gzip, largeStorage) ==
                     HttpContentDecodeResult::Success &&
                 largeStorage.size() == static_cast<int>(largePlain.size()) &&
                 largeStorage.segmentsUsed() == largeExpectedSegments &&
                 largeStorage.historyBytes() == 32768 && copyStorageToVector(largeStorage, decoded) &&
                 decoded == largePlain,
                 "large fixed-Huffman gzip crosses former 256 KiB cap");
    largeStorage.reset();

    const auto largeDeflateRaw = makeFixedDeflateWithRun(
        "<html><head><title>Large compression fixture</title></head><body>",
        300000, "</body></html>");
    const auto largeZlib = makeZlib(largeDeflateRaw, largePlain);
    ok &= expect(decodeIntoStorage(largeZlib, HttpContentCoding::Deflate, largeStorage) ==
                     HttpContentDecodeResult::Success &&
                 largeStorage.size() == static_cast<int>(largePlain.size()) &&
                 largeStorage.segmentsUsed() == largeExpectedSegments &&
                 largeStorage.historyBytes() == 32768 && copyStorageToVector(largeStorage, decoded) &&
                 decoded == largePlain,
                 "large zlib-deflate document crosses former 256 KiB cap");
    largeStorage.reset();

    const auto largeDynamicGzip = makeLargeConcatenatedDynamicGzip();
    const auto largeDynamicPlain = makeLargeConcatenatedDynamicPlain();
    ok &= expect(decodeIntoStorage(largeDynamicGzip, HttpContentCoding::Gzip, largeStorage) ==
                     HttpContentDecodeResult::Success && largeStorage.size() ==
                     static_cast<int>(largeDynamicPlain.size()) && largeStorage.size() >
                     gxos::web::kHttpSharedMaxBodyBytes && largeStorage.size() <= documentCap &&
                 largeStorage.segmentsUsed() <= gxos::web::kHttpSharedMaxDecodedDocumentSegments &&
                 largeStorage.historyBytes() == 32768 && copyStorageToVector(largeStorage, decoded) &&
                 decoded == largeDynamicPlain,
                 "large concatenated dynamic-Huffman gzip document");
    largeStorage.reset();

    const std::string contentLengthText = std::to_string(largeFixedGzip.size());
    const std::string contentLengthResponse =
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Encoding: gzip\r\nContent-Length: " +
        contentLengthText + "\r\n\r\n";
    int parsedContentLength = 0;
    const size_t contentLengthHeaderStart = contentLengthResponse.find("Content-Length: ") + 16;
    const size_t contentLengthHeaderEnd = contentLengthResponse.find("\r\n", contentLengthHeaderStart);
    ok &= expect(gxos::web::httpSharedParseDecimalSize(
                     contentLengthResponse.data() + contentLengthHeaderStart,
                     contentLengthResponse.data() + contentLengthHeaderEnd,
                     &parsedContentLength) &&
                 parsedContentLength == static_cast<int>(largeFixedGzip.size()),
                 "large Content-Length framing accepts encoded fixture");
    largeStorage.reset();
    ok &= expect(decodeIntoStorage(largeFixedGzip, HttpContentCoding::Gzip, largeStorage) ==
                     HttpContentDecodeResult::Success && largeStorage.size() >
                     gxos::web::kHttpSharedMaxBodyBytes && copyStorageToVector(largeStorage, decoded) &&
                 decoded == largePlain,
                 "large Content-Length body reaches segmented decoder and parser boundary");
    largeStorage.reset();

    std::string largeChunked;
    size_t largeChunkOffset = 0;
    const size_t largeChunkPattern[] = { 1, 7, 31, 257, 4093 };
    int largeChunkPatternIndex = 0;
    while (largeChunkOffset < largeFixedGzip.size()) {
        const size_t requested = largeChunkPattern[largeChunkPatternIndex % 5];
        const size_t chunkSize = std::min(requested, largeFixedGzip.size() - largeChunkOffset);
        largeChunked += hexSize(chunkSize) + ";tls-split=fixture\r\n";
        largeChunked.append(reinterpret_cast<const char*>(largeFixedGzip.data() + largeChunkOffset), chunkSize);
        largeChunked += "\r\n";
        largeChunkOffset += chunkSize;
        ++largeChunkPatternIndex;
    }
    largeChunked += "0\r\nX-Fixture: large-chunked\r\n\r\n";
    bool chunkPrefixIncomplete = false;
    for (size_t prefix = 1; prefix < largeChunked.size(); prefix += 19) {
        bool malformed = false;
        if (!gxos::web::httpSharedChunkedBodyComplete(largeChunked.data(), static_cast<int>(prefix), &malformed)) {
            chunkPrefixIncomplete = !malformed;
            if (chunkPrefixIncomplete) break;
        }
    }
    std::vector<char> dechunkedLarge(largeFixedGzip.size() + 1, 0);
    int dechunkedLargeBytes = 0;
    char largeChunkError[96] = {};
    ok &= expect(chunkPrefixIncomplete &&
                 gxos::web::httpSharedChunkedBodyComplete(largeChunked.data(),
                     static_cast<int>(largeChunked.size()), nullptr) &&
                 gxos::web::httpSharedDecodeChunkedBody(largeChunked.data(),
                     static_cast<int>(largeChunked.size()), dechunkedLarge.data(),
                     static_cast<int>(dechunkedLarge.size()), &dechunkedLargeBytes,
                     largeChunkError, sizeof(largeChunkError)) &&
                 dechunkedLargeBytes == static_cast<int>(largeFixedGzip.size()) &&
                 decodeIntoStorage(std::vector<uint8_t>(reinterpret_cast<uint8_t*>(dechunkedLarge.data()),
                     reinterpret_cast<uint8_t*>(dechunkedLarge.data()) + dechunkedLargeBytes),
                     HttpContentCoding::Gzip, largeStorage) == HttpContentDecodeResult::Success &&
                 largeStorage.size() == static_cast<int>(largePlain.size()) &&
                 copyStorageToVector(largeStorage, decoded) && decoded == largePlain,
                 "large chunked gzip with arbitrary framing boundaries and trailers");
    largeStorage.reset();

    const std::string connectionCloseResponse =
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Encoding: gzip\r\n\r\n";
    const size_t connectionCloseBodyStart = connectionCloseResponse.size();
    std::vector<uint8_t> connectionCloseBody;
    connectionCloseBody.insert(connectionCloseBody.end(), largeFixedGzip.begin(), largeFixedGzip.end());
    ok &= expect(connectionCloseBodyStart == connectionCloseResponse.size() &&
                 decodeIntoStorage(connectionCloseBody, HttpContentCoding::Gzip, largeStorage) ==
                     HttpContentDecodeResult::Success && largeStorage.size() >
                     gxos::web::kHttpSharedMaxBodyBytes && copyStorageToVector(largeStorage, decoded) &&
                 decoded == largePlain,
                 "large connection-close gzip completes without timeout classification");
    largeStorage.reset();

    const std::vector<uint8_t> documentCapMinusOne(static_cast<size_t>(documentCap - 1), 'M');
    const std::vector<uint8_t> documentCapExact(static_cast<size_t>(documentCap), 'E');
    const std::vector<uint8_t> documentCapPlusOne(static_cast<size_t>(documentCap + 1), 'P');
    ok &= expect(decodeToStorage(makeGzip(makeStoredDeflate(documentCapMinusOne), documentCapMinusOne),
                                 HttpContentCoding::Gzip, decoded, &segmentsUsed) ==
                 HttpContentDecodeResult::Success && static_cast<int>(decoded.size()) == documentCap - 1 &&
                 segmentsUsed == gxos::web::kHttpSharedMaxDecodedDocumentSegments,
                 "segmented document cap minus one passes");
    ok &= expect(decodeToStorage(makeGzip(makeStoredDeflate(documentCapExact), documentCapExact),
                                 HttpContentCoding::Gzip, decoded, &segmentsUsed) ==
                 HttpContentDecodeResult::Success && static_cast<int>(decoded.size()) == documentCap &&
                 segmentsUsed == gxos::web::kHttpSharedMaxDecodedDocumentSegments,
                 "segmented document exact cap passes inclusively");
    ok &= expect(decodeToStorage(makeGzip(makeStoredDeflate(documentCapPlusOne), documentCapPlusOne),
                                 HttpContentCoding::Gzip, decoded) ==
                 HttpContentDecodeResult::DecodedResponseTooLarge,
                 "segmented document cap plus one fails before write beyond bound");

    gxos::web::BoundedDocumentStorage boundaryStorage;
    std::string boundaryFixture =
        "<html><head><title>boundary</title></head><body>";
    const size_t boundarySegmentBytes = gxos::web::kHttpSharedDecodedDocumentSegmentBytes;
    auto appendCrossSegmentToken = [&](const std::string& token) {
        const size_t nextBoundary = ((boundaryFixture.size() / boundarySegmentBytes) + 1) *
            boundarySegmentBytes - 1;
        if (boundaryFixture.size() < nextBoundary) boundaryFixture.append(
            nextBoundary - boundaryFixture.size(), 'x');
        boundaryFixture += token;
    };
    appendCrossSegmentToken("<a href=\"/cross-segment\">segment &amp; link</a>");
    appendCrossSegmentToken("<!-- comment crossing the segment boundary -->");
    appendCrossSegmentToken("<style>body{color:#123456}</style>");
    appendCrossSegmentToken("<p>text node UTF-8 \xCF\x80 and quoted URL</p>");
    boundaryFixture += "</body></html>";
    for (size_t offset = 0; offset < boundaryFixture.size();) {
        const size_t chunk = (offset % 17) + 1;
        const size_t count = std::min(chunk, boundaryFixture.size() - offset);
        ok &= expect(boundaryStorage.append(reinterpret_cast<const uint8_t*>(boundaryFixture.data() + offset),
                                             static_cast<int>(count)),
                     "document source append at boundary");
        offset += count;
    }
    std::vector<uint8_t> flattened(boundaryStorage.size() + 1, 0);
    ok &= expect(boundaryStorage.flatten(flattened.data(), static_cast<int>(flattened.size())) &&
                 boundaryStorage.segmentsUsed() >= 4 &&
                 std::string(reinterpret_cast<const char*>(flattened.data()), boundaryStorage.size()) == boundaryFixture,
                 "document source flatten preserves tokenizer boundary bytes");
    const gxos::web::WebDocument boundaryDocument = gxos::web::parseHtml(
        "https://fixture.invalid/boundary.html",
        std::string(reinterpret_cast<const char*>(flattened.data()), boundaryStorage.size()));
    bool boundaryLinkFound = false;
    for (const auto& block : boundaryDocument.blocks) {
        if (block.type == gxos::web::BlockType::Link && block.url.find("cross-segment") != std::string::npos) {
            boundaryLinkFound = true;
            break;
        }
    }
    boundaryStorage.release();
    ok &= expect(boundaryDocument.title == "boundary" && boundaryLinkFound && boundaryDocument.bodyStyle.hasColor,
                 "flattened boundary fixture reaches parser and preserves title/link/CSS");

    gxos::web::BoundedDocumentStorage exhaustedStorage;
    const std::vector<uint8_t> fullDocument(static_cast<size_t>(documentCap), 'X');
    ok &= expect(exhaustedStorage.append(fullDocument.data(), documentCap) &&
                 exhaustedStorage.size() == documentCap &&
                 exhaustedStorage.segmentsUsed() == gxos::web::kHttpSharedMaxDecodedDocumentSegments &&
                 !exhaustedStorage.appendByte('!') && exhaustedStorage.capHit,
                 "bounded segment exhaustion rejects without overrun");

    gxos::web::BoundedDocumentStorage faultStorage;
    const int faultOrdinals[] = { 1, (largeExpectedSegments + 1) / 2, largeExpectedSegments };
    bool allSegmentFaultsClean = true;
    for (int ordinal : faultOrdinals) {
        faultStorage.reset();
        faultStorage.setAllocationFaultInjectionForTest(ordinal, 0);
        const auto faultResult = decodeIntoStorage(largeFixedGzip, HttpContentCoding::Gzip, faultStorage);
        bool parserInvokedAfterFault = false;
        if (faultResult == HttpContentDecodeResult::Success) parserInvokedAfterFault = true;
        allSegmentFaultsClean = allSegmentFaultsClean &&
            faultResult == HttpContentDecodeResult::OutputAllocationFailed &&
            faultStorage.allocationFailed && !parserInvokedAfterFault &&
            faultStorage.segmentsUsed() == ordinal - 1;
        faultStorage.release();
        allSegmentFaultsClean = allSegmentFaultsClean && faultStorage.segmentsUsed() == 0 &&
            faultStorage.allocatedBytes() == 0 && faultStorage.historyBytes() == 0 &&
            boundaryDocument.title == "boundary" && boundaryDocument.bodyStyle.hasColor;
    }
    ok &= expect(allSegmentFaultsClean, "segment allocation failure first/middle/final is clean");

    faultStorage.reset();
    faultStorage.clearAllocationFaultInjectionForTest();
    ok &= expect(decodeIntoStorage(body, HttpContentCoding::Identity, faultStorage) ==
                     HttpContentDecodeResult::Success && copyStorageToVector(faultStorage, decoded) &&
                 decoded == body && boundaryDocument.title == "boundary",
                 "small navigation succeeds after segment allocation failure");
    faultStorage.reset();
    faultStorage.clearAllocationFaultInjectionForTest();
    ok &= expect(decodeIntoStorage(largeFixedGzip, HttpContentCoding::Gzip, faultStorage) ==
                     HttpContentDecodeResult::Success,
                 "large navigation succeeds after recovered small navigation");
    faultStorage.setAllocationFaultInjectionForTest(0, 1);
    bool parserInvokedAfterFlattenFault = false;
    char* failedFlatten = faultStorage.allocateFlattenedCopy();
    if (failedFlatten) {
        parserInvokedAfterFlattenFault = true;
        delete[] failedFlatten;
    }
    ok &= expect(!parserInvokedAfterFlattenFault && faultStorage.flattenAllocationFailed &&
                 faultStorage.allocationFailed && faultStorage.segmentsUsed() == largeExpectedSegments &&
                 faultStorage.size() == static_cast<int>(largePlain.size()),
                 "flatten allocation failure rejects complete segmentation before parser");
    faultStorage.release();
    faultStorage.clearAllocationFaultInjectionForTest();
    ok &= expect(faultStorage.segmentsUsed() == 0 && faultStorage.allocatedBytes() == 0 &&
                 decodeIntoStorage(body, HttpContentCoding::Identity, faultStorage) ==
                     HttpContentDecodeResult::Success && copyStorageToVector(faultStorage, decoded) &&
                 decoded == body && boundaryDocument.title == "boundary",
                 "Navigator remains usable after flatten allocation failure");
    faultStorage.release();

    bool lifecycleClean = true;
    for (int cycle = 0; cycle < 3; ++cycle) {
        faultStorage.reset();
        faultStorage.clearAllocationFaultInjectionForTest();
        lifecycleClean = lifecycleClean &&
            decodeIntoStorage(largeFixedGzip, HttpContentCoding::Gzip, faultStorage) ==
                HttpContentDecodeResult::Success && copyStorageToVector(faultStorage, decoded) &&
            decoded == largePlain;
        faultStorage.reset();
        lifecycleClean = lifecycleClean &&
            decodeIntoStorage(body, HttpContentCoding::Identity, faultStorage) ==
                HttpContentDecodeResult::Success && copyStorageToVector(faultStorage, decoded) &&
            decoded == body && faultStorage.segmentsUsed() == 1;
        faultStorage.reset();
        faultStorage.setAllocationFaultInjectionForTest((cycle % largeExpectedSegments) + 1, 0);
        lifecycleClean = lifecycleClean &&
            decodeIntoStorage(largeFixedGzip, HttpContentCoding::Gzip, faultStorage) ==
                HttpContentDecodeResult::OutputAllocationFailed;
        faultStorage.release();
        lifecycleClean = lifecycleClean && faultStorage.segmentsUsed() == 0 &&
            faultStorage.allocatedBytes() == 0 && faultStorage.historyBytes() == 0 &&
            boundaryDocument.title == "boundary" && boundaryDocument.bodyStyle.hasColor;
    }
    ok &= expect(lifecycleClean, "repeated large/small/fault lifecycle releases all storage");

    char chunked[256] = {};
    std::string framed;
    const size_t chunkSizes[] = { 7, 16, gzip.size() - 23 };
    size_t chunkOffset = 0;
    for (size_t chunkSize : chunkSizes) {
        framed += hexSize(chunkSize) + ";fixture=split\r\n";
        framed.append(reinterpret_cast<const char*>(gzip.data() + chunkOffset), chunkSize);
        framed += "\r\n";
        chunkOffset += chunkSize;
    }
    framed += "0\r\nX-Fixture: yes\r\n\r\n";
    int chunkedLength = 0;
    char chunkError[96] = {};
    ok &= expect(gxos::web::httpSharedDecodeChunkedBody(framed.data(), static_cast<int>(framed.size()),
        chunked, sizeof(chunked), &chunkedLength, chunkError, sizeof(chunkError)) &&
        chunkedLength == static_cast<int>(gzip.size()) &&
        decode(std::vector<uint8_t>(reinterpret_cast<uint8_t*>(chunked),
                                    reinterpret_cast<uint8_t*>(chunked) + chunkedLength),
               HttpContentCoding::Gzip, decoded) == HttpContentDecodeResult::Success && decoded == body,
        "chunked gzip framing then decoding");

    framed.clear();
    chunkOffset = 0;
    const size_t deflateChunkSizes[] = { 5, 11, zlib.size() - 16 };
    for (size_t chunkSize : deflateChunkSizes) {
        framed += hexSize(chunkSize) + ";fixture=deflate-split\r\n";
        framed.append(reinterpret_cast<const char*>(zlib.data() + chunkOffset), chunkSize);
        framed += "\r\n";
        chunkOffset += chunkSize;
    }
    framed += "0\r\nX-Fixture: deflate-chunked\r\n\r\n";
    chunkedLength = 0;
    ok &= expect(gxos::web::httpSharedDecodeChunkedBody(framed.data(), static_cast<int>(framed.size()),
        chunked, sizeof(chunked), &chunkedLength, chunkError, sizeof(chunkError)) &&
        chunkedLength == static_cast<int>(zlib.size()) &&
        decode(std::vector<uint8_t>(reinterpret_cast<uint8_t*>(chunked),
                                    reinterpret_cast<uint8_t*>(chunked) + chunkedLength),
               HttpContentCoding::Deflate, decoded) == HttpContentDecodeResult::Success && decoded == body,
        "chunked zlib-deflate framing then decoding");

    ok &= expect(decode(body, HttpContentCoding::Identity, decoded) == HttpContentDecodeResult::Success &&
                 decoded == body, "identity body regression");

    std::cout << (ok ? "Content decoder tests PASS\n" : "Content decoder tests FAIL\n");
    return ok ? 0 : 1;
}
