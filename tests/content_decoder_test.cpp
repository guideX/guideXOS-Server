#include "guide_web_http_shared.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

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
