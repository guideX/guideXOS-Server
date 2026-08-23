#include "kernel/core/include/kernel/image_adapter.h"
#include "guide_web_http_shared.h"
#include "jpeg_loader.h"
#include "jpeg_test_fixtures.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using gxos::gui::ImageAdapter;
using gxos::gui::ImageLoadStatus;
using gxos::gui::ImageSafetyLimits;
using gxos::gui::JpegHeaderInfo;
using gxos::gui::JpegProbeStatus;

namespace {

bool expect(bool condition, const char* label)
{
    if (!condition) std::cerr << "FAIL: " << label << "\n";
    return condition;
}

std::vector<uint8_t> readFixture(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

int base64Value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

std::vector<uint8_t> decodeFixture(const char* encoded)
{
    std::vector<uint8_t> result;
    if (!encoded) return result;
    int value = 0;
    int bits = -8;
    for (const char* p = encoded; *p; ++p) {
        if (*p == '=') break;
        const int digit = base64Value(*p);
        if (digit < 0) continue;
        value = (value << 6) | digit;
        bits += 6;
        if (bits >= 0) {
            result.push_back(static_cast<uint8_t>((value >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return result;
}

size_t findMarker(const std::vector<uint8_t>& bytes, uint8_t marker)
{
    for (size_t i = 1; i < bytes.size(); ++i) {
        if (bytes[i - 1] == 0xFF && bytes[i] == marker) return i - 1;
    }
    return bytes.size();
}

std::vector<uint8_t> withMetadata(const std::vector<uint8_t>& source)
{
    std::vector<uint8_t> result;
    const uint8_t app1[] = { 0xFF, 0xE1, 0x00, 0x08, 'E', 'x', 'i', 'f', 0, 0 };
    const uint8_t com[] = { 0xFF, 0xFE, 0x00, 0x06, 't', 'e', 's', 't' };
    result.insert(result.end(), source.begin(), source.begin() + 2);
    result.insert(result.end(), app1, app1 + sizeof(app1));
    result.insert(result.end(), com, com + sizeof(com));
    result.insert(result.end(), source.begin() + 2, source.end());
    return result;
}

std::vector<uint8_t> gzipStored(const std::vector<uint8_t>& plain)
{
    const uint16_t length = static_cast<uint16_t>(plain.size());
    const uint16_t inverse = static_cast<uint16_t>(~length);
    std::vector<uint8_t> gzip = { 0x1F, 0x8B, 8, 0, 0, 0, 0, 0, 0, 0,
                                  0x01,
                                  static_cast<uint8_t>(length & 0xFFu),
                                  static_cast<uint8_t>(length >> 8),
                                  static_cast<uint8_t>(inverse & 0xFFu),
                                  static_cast<uint8_t>(inverse >> 8) };
    gzip.insert(gzip.end(), plain.begin(), plain.end());
    const uint32_t crc = gxos::web::httpSharedCrc32(plain.data(), static_cast<int>(plain.size()));
    for (int shift = 0; shift < 32; shift += 8) gzip.push_back(static_cast<uint8_t>(crc >> shift));
    const uint32_t size = static_cast<uint32_t>(plain.size());
    for (int shift = 0; shift < 32; shift += 8) gzip.push_back(static_cast<uint8_t>(size >> shift));
    return gzip;
}

ImageSafetyLimits limitsFor(uint32_t width, uint32_t height)
{
    ImageSafetyLimits limits{};
    limits.maxBytes = 256u * 1024u;
    limits.maxWidth = width;
    limits.maxHeight = height;
    limits.maxPixels = width * height;
    limits.maxDecodedBytes = width * height * 4u;
    return limits;
}

std::string hexSize(size_t value)
{
    std::ostringstream stream;
    stream << std::hex << value;
    return stream.str();
}

} // namespace

int main()
{
    bool ok = true;
    const std::vector<uint8_t> fixture = readFixture("bkup/appdemo.jpg");
    ok &= expect(!fixture.empty(), "tracked JPEG fixture is available");
    if (fixture.empty()) return 1;

    const std::vector<uint8_t> grayscale = decodeFixture(gxos_test_fixtures::kGrayJpeg);
    const std::vector<uint8_t> color444 = decodeFixture(gxos_test_fixtures::k444RestartJpeg);
    const std::vector<uint8_t> color422 = decodeFixture(gxos_test_fixtures::k422Jpeg);
    const std::vector<uint8_t> progressive = decodeFixture(gxos_test_fixtures::kProgressiveJpeg);
    ok &= expect(!grayscale.empty() && !color444.empty() && !color422.empty() && !progressive.empty(),
                 "embedded JPEG mode fixtures are available");

    auto expectMode = [&](const std::vector<uint8_t>& bytes, uint32_t width, uint32_t height,
                          uint8_t components, uint8_t maxH, uint8_t maxV, bool isProgressive,
                          const char* label) {
        JpegHeaderInfo info{};
        const JpegProbeStatus probe = gxos::gui::InspectJpeg(bytes.data(), bytes.size(), info);
        ok &= expect(probe == JpegProbeStatus::Valid, label);
        ok &= expect(info.width == width && info.height == height && info.components == components &&
                     info.maxHorizontalSampling == maxH && info.maxVerticalSampling == maxV &&
                     info.progressive == isProgressive, "JPEG mode header fields");
        const auto decoded = ImageAdapter::LoadFromBytes(bytes, label, limitsFor(width, height));
        ok &= expect(decoded.status == ImageLoadStatus::Ok && decoded.width == static_cast<int>(width) &&
                     decoded.height == static_cast<int>(height) && decoded.format == gxos::gui::ImageFormat::Jpeg,
                     "JPEG mode decodes through ImageAdapter");
    };

    expectMode(grayscale, 16, 24, 1, 2, 2, false, "grayscale JPEG fixture");
    expectMode(color444, 33, 33, 3, 1, 1, false, "4:4:4 JPEG fixture");
    expectMode(color422, 640, 480, 3, 2, 1, false, "4:2:2 JPEG fixture");
    expectMode(progressive, 16, 16, 3, 2, 2, true, "progressive JPEG fixture");

    gxos::gui::JpegHeaderInfo header{};
    ok &= expect(gxos::gui::InspectJpeg(fixture.data(), fixture.size(), header) == gxos::gui::JpegProbeStatus::Valid,
                 "baseline JPEG header is valid");
    ok &= expect(header.width == 326 && header.height == 86 && header.components == 3,
                 "baseline JPEG dimensions/components");
    ok &= expect(header.maxHorizontalSampling == 2 && header.maxVerticalSampling == 2,
                 "baseline JPEG uses common 4:2:0 sampling");
    ok &= expect(!header.progressive, "fixture is baseline sequential");

    const ImageSafetyLimits exactLimits = limitsFor(326, 86);
    const auto decoded = ImageAdapter::LoadFromBytes(fixture, "fixture.jpg", exactLimits);
    ok &= expect(decoded.status == ImageLoadStatus::Ok && decoded.width == 326 && decoded.height == 86,
                 "baseline JPEG decodes through ImageAdapter");
    ok &= expect(decoded.format == gxos::gui::ImageFormat::Jpeg, "JPEG format ownership is retained");

    const auto metadataDecoded = ImageAdapter::LoadFromBytes(withMetadata(fixture), "metadata.jpg", exactLimits);
    ok &= expect(metadataDecoded.status == ImageLoadStatus::Ok,
                 "APP1/Exif and COM metadata are skipped safely");

    const std::vector<uint8_t> gzipJpeg = gzipStored(fixture);
    gxos::web::HttpContentDecoderWorkspace decoderWorkspace{};
    std::vector<uint8_t> inflated(gxos::web::kHttpSharedMaxBodyBytes);
    int inflatedBytes = 0;
    char decoderError[96] = {};
    const auto gzipResult = gxos::web::httpSharedDecodeContent(
        gzipJpeg.data(), static_cast<int>(gzipJpeg.size()), gxos::web::HttpContentCoding::Gzip,
        inflated.data(), static_cast<int>(inflated.size()), &inflatedBytes,
        &decoderWorkspace, decoderError, sizeof(decoderError));
    inflated.resize(static_cast<size_t>(inflatedBytes));
    ok &= expect(gzipResult == gxos::web::HttpContentDecodeResult::Success && inflated == fixture,
                 "gzip-wrapped JPEG inflates to the original bytes");
    ok &= expect(ImageAdapter::LoadFromBytes(inflated, "gzip-fixture.jpg", exactLimits).status == ImageLoadStatus::Ok,
                 "inflated JPEG decodes through the image adapter");

    std::vector<uint8_t> split(fixture.begin(), fixture.begin() + fixture.size() / 2);
    split.insert(split.end(), fixture.begin() + fixture.size() / 2, fixture.end());
    ok &= expect(ImageAdapter::LoadFromBytes(split, "split.jpg", exactLimits).status == ImageLoadStatus::Ok,
                 "JPEG assembled from split HTTP reads decodes");

    const size_t chunkMidpoint = fixture.size() / 2;
    std::string chunked = hexSize(chunkMidpoint) + "\r\n";
    chunked.append(reinterpret_cast<const char*>(fixture.data()), chunkMidpoint);
    chunked += "\r\n";
    chunked += hexSize(fixture.size() - chunkMidpoint) + "\r\n";
    chunked.append(reinterpret_cast<const char*>(fixture.data() + chunkMidpoint), fixture.size() - chunkMidpoint);
    chunked += "\r\n0\r\nX-JPEG-Fixture: chunked\r\n\r\n";
    std::vector<char> dechunked(fixture.size() + 1, 0);
    int dechunkedBytes = 0;
    char chunkError[96] = {};
    const bool chunkedOk = gxos::web::httpSharedDecodeChunkedBody(
        chunked.data(), static_cast<int>(chunked.size()), dechunked.data(),
        static_cast<int>(dechunked.size()), &dechunkedBytes, chunkError, sizeof(chunkError));
    ok &= expect(chunkedOk && dechunkedBytes == static_cast<int>(fixture.size()) &&
                 std::equal(fixture.begin(), fixture.end(), reinterpret_cast<uint8_t*>(dechunked.data())),
                 "chunked JPEG body is de-framed before decode");
    ok &= expect(ImageAdapter::LoadFromBytes(reinterpret_cast<const uint8_t*>(dechunked.data()),
                 static_cast<size_t>(dechunkedBytes), "chunked-fixture.jpg", exactLimits).status == ImageLoadStatus::Ok,
                 "de-framed chunked JPEG decodes through the image adapter");

    ImageSafetyLimits onePixelLess = exactLimits;
    onePixelLess.maxPixels = 326u * 86u - 1u;
    ok &= expect(ImageAdapter::LoadFromBytes(fixture, "pixel-cap.jpg", onePixelLess).status == ImageLoadStatus::TooLarge,
                 "pixel-count cap rejects max plus one");
    ImageSafetyLimits oneByteLess = exactLimits;
    oneByteLess.maxDecodedBytes = 326u * 86u * 4u - 1u;
    ok &= expect(ImageAdapter::LoadFromBytes(fixture, "decoded-byte-cap.jpg", oneByteLess).status == ImageLoadStatus::TooLarge,
                 "decoded-pixel-byte cap rejects max plus one");
    ImageSafetyLimits widthOneLess = exactLimits;
    widthOneLess.maxWidth = 325;
    ok &= expect(ImageAdapter::LoadFromBytes(fixture, "width-cap.jpg", widthOneLess).status == ImageLoadStatus::TooLarge,
                 "maximum-width boundary rejects one over the configured width");
    ImageSafetyLimits heightOneLess = exactLimits;
    heightOneLess.maxHeight = 85;
    ok &= expect(ImageAdapter::LoadFromBytes(fixture, "height-cap.jpg", heightOneLess).status == ImageLoadStatus::TooLarge,
                 "maximum-height boundary rejects one over the configured height");

    for (uint32_t failAfter = 0; failAfter < 4; ++failAfter) {
        gxos::gui::SetJpegAllocationFailureInjection(failAfter);
        const auto injected = ImageAdapter::LoadFromBytes(fixture, "injected-allocation-failure.jpg", exactLimits);
        ok &= expect(injected.status == ImageLoadStatus::OutOfMemory,
                     "injected JPEG allocation failure reports bounded OOM");
    }
    gxos::gui::SetJpegAllocationFailureInjection(0xFFFFFFFFu);
    ok &= expect(ImageAdapter::LoadFromBytes(fixture, "post-injection-recovery.jpg", exactLimits).status == ImageLoadStatus::Ok,
                 "valid JPEG recovers after allocation-failure injection");

    gxos::gui::SetImageAllocationFailureInjection(0);
    ok &= expect(ImageAdapter::LoadFromBytes(fixture, "final-pixel-allocation-failure.jpg", exactLimits).status == ImageLoadStatus::OutOfMemory,
                 "final JPEG pixel allocation failure reports bounded OOM");
    gxos::gui::SetImageAllocationFailureInjection(0xFFFFFFFFu);
    const std::vector<uint8_t> pngFixture = readFixture("assets/Images/NuoveXT/PNG/48/edit_add_10261.png");
    ok &= expect(!pngFixture.empty() && ImageAdapter::LoadFromBytes(pngFixture, "recovery.png").status == ImageLoadStatus::Ok,
                 "PNG remains valid after JPEG allocation failure");

    std::vector<uint8_t> missingSoi = fixture;
    missingSoi[0] = 0;
    ok &= expect(ImageAdapter::LoadFromBytes(missingSoi, "missing-soi.jpg", exactLimits).status == ImageLoadStatus::UnsupportedFormat,
                 "missing SOI is rejected as unsupported input");
    ok &= expect(ImageAdapter::LoadFromBytes(std::vector<uint8_t>{0xFF, 0xD8, 0xFF}, "truncated-marker.jpg", exactLimits).status == ImageLoadStatus::DecodeFailed,
                 "truncated marker fails cleanly");

    std::vector<uint8_t> invalidSegmentLength = fixture;
    const size_t app0 = findMarker(invalidSegmentLength, 0xE0);
    if (app0 + 3 < invalidSegmentLength.size()) {
        invalidSegmentLength[app0 + 2] = 0;
        invalidSegmentLength[app0 + 3] = 1;
    }
    ok &= expect(ImageAdapter::LoadFromBytes(invalidSegmentLength, "invalid-segment-length.jpg", exactLimits).status == ImageLoadStatus::DecodeFailed,
                 "invalid segment length fails cleanly");

    std::vector<uint8_t> truncatedDqt = fixture;
    const size_t dqt = findMarker(truncatedDqt, 0xDB);
    if (dqt + 3 < truncatedDqt.size()) truncatedDqt.resize(dqt + 3);
    ok &= expect(ImageAdapter::LoadFromBytes(truncatedDqt, "truncated-dqt.jpg", exactLimits).status == ImageLoadStatus::DecodeFailed,
                 "truncated quantization table fails cleanly");

    std::vector<uint8_t> truncatedDht = fixture;
    const size_t dht = findMarker(truncatedDht, 0xC4);
    if (dht + 3 < truncatedDht.size()) truncatedDht.resize(dht + 3);
    ok &= expect(ImageAdapter::LoadFromBytes(truncatedDht, "truncated-dht.jpg", exactLimits).status == ImageLoadStatus::DecodeFailed,
                 "truncated Huffman table fails cleanly");

    std::vector<uint8_t> malformedScan = fixture;
    const size_t sos = findMarker(malformedScan, 0xDA);
    if (sos + 3 < malformedScan.size()) {
        malformedScan[sos + 2] = 0;
        malformedScan[sos + 3] = 1;
    }
    ok &= expect(ImageAdapter::LoadFromBytes(malformedScan, "malformed-scan.jpg", exactLimits).status == ImageLoadStatus::DecodeFailed,
                 "malformed scan header fails cleanly");

    std::vector<uint8_t> missingEoi = fixture;
    if (missingEoi.size() > 2) missingEoi.resize(missingEoi.size() - 2);
    ok &= expect(ImageAdapter::LoadFromBytes(missingEoi, "missing-eoi.jpg", exactLimits).status == ImageLoadStatus::DecodeFailed,
                 "missing EOI fails cleanly");

    const size_t sof = findMarker(fixture, 0xC0);
    std::vector<uint8_t> zeroDimensions = fixture;
    if (sof + 8 < zeroDimensions.size()) {
        zeroDimensions[sof + 5] = 0;
        zeroDimensions[sof + 6] = 0;
        zeroDimensions[sof + 7] = 0;
        zeroDimensions[sof + 8] = 0;
    }
    ok &= expect(ImageAdapter::LoadFromBytes(zeroDimensions, "zero-dimensions.jpg", exactLimits).status == ImageLoadStatus::DecodeFailed,
                 "zero JPEG dimensions fail cleanly");

    std::vector<uint8_t> unsupportedComponents = fixture;
    const size_t unsupportedSof = findMarker(unsupportedComponents, 0xC0);
    if (unsupportedSof + 9 < unsupportedComponents.size()) {
        const uint16_t oldLength = static_cast<uint16_t>((unsupportedComponents[unsupportedSof + 2] << 8) |
                                                          unsupportedComponents[unsupportedSof + 3]);
        const size_t componentEnd = unsupportedSof + 2 + oldLength;
        unsupportedComponents[unsupportedSof + 9] = 4;
        unsupportedComponents[unsupportedSof + 2] = static_cast<uint8_t>((oldLength + 3) >> 8);
        unsupportedComponents[unsupportedSof + 3] = static_cast<uint8_t>(oldLength + 3);
        unsupportedComponents.insert(unsupportedComponents.begin() + componentEnd, { 4, 0x11, 0 });
    }
    ok &= expect(ImageAdapter::LoadFromBytes(unsupportedComponents, "unsupported-components.jpg", exactLimits).status == ImageLoadStatus::UnsupportedFormat,
                 "unsupported four-component JPEG is rejected without color corruption");

    std::vector<uint8_t> impossibleDimensions = fixture;
    if (sof + 8 < impossibleDimensions.size()) {
        impossibleDimensions[sof + 5] = 0xFF;
        impossibleDimensions[sof + 6] = 0xFF;
        impossibleDimensions[sof + 7] = 0xFF;
        impossibleDimensions[sof + 8] = 0xFF;
    }
    ok &= expect(ImageAdapter::LoadFromBytes(impossibleDimensions, "impossible-dimensions.jpg", exactLimits).status == ImageLoadStatus::TooLarge,
                 "dimensions over the configured policy reject before decode");

    // Decoder state must not leak across failure.  A valid JPEG still works
    // after malformed input and repeated decode/reset cycles.
    for (int i = 0; i < 3; ++i) {
        ok &= expect(ImageAdapter::LoadFromBytes(malformedScan, "failure-reset.jpg", exactLimits).status == ImageLoadStatus::DecodeFailed,
                     "malformed decode remains contained");
        ok &= expect(ImageAdapter::LoadFromBytes(fixture, "recovery.jpg", exactLimits).status == ImageLoadStatus::Ok,
                     "valid JPEG recovers after malformed decode");
    }

    std::cout << (ok ? "JPEG codec tests PASS\n" : "JPEG codec tests FAIL\n");
    return ok ? 0 : 1;
}
