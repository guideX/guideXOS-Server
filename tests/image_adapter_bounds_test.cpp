#include "kernel/core/include/kernel/image_adapter.h"
#include "vfs.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

int checks = 0;
int failures = 0;

void check(bool condition, const char* name) {
    ++checks;
    if (condition) {
        std::cout << "PASS: " << name << "\n";
    } else {
        ++failures;
        std::cout << "FAIL: " << name << "\n";
    }
}

std::vector<uint8_t> readBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void setBe32(std::vector<uint8_t>& bytes, size_t position, uint32_t value) {
    bytes[position] = static_cast<uint8_t>(value >> 24);
    bytes[position + 1] = static_cast<uint8_t>(value >> 16);
    bytes[position + 2] = static_cast<uint8_t>(value >> 8);
    bytes[position + 3] = static_cast<uint8_t>(value);
}

} // namespace

int main() {
    using namespace gxos::gui;
    using gxos::Vfs;
    using gxos::VfsReadFileStatus;
    const ImageSafetyLimits limits = DefaultImageSafetyLimits();
    check(limits.maxBytes == 4u * 1024u * 1024u && limits.maxWidth == 4096u &&
        limits.maxHeight == 4096u && limits.maxPixels == 4096u * 4096u &&
        limits.maxDecodedBytes == 4096u * 4096u * 4u,
        "production decoder retains the 4 MiB, 4096x4096, 16.7M-pixel, 64 MiB decoded bounds");

    const std::vector<uint8_t> valid = readBytes("assets/Backgrounds/blueflower_thumb.png");
    const ImageBitmap validImage = ImageAdapter::LoadFromBytes(valid, "known-good fixture");
    check(!valid.empty() && validImage.status == ImageLoadStatus::Ok && validImage.image &&
        validImage.width == 126 && validImage.height == 84 && validImage.image->isValid(),
        "existing production PNG adapter decodes the known-good 126x84 fixture");

    const ImageBitmap emptyImage = ImageAdapter::LoadFromBytes(nullptr, 0, "empty.png");
    check(emptyImage.status == ImageLoadStatus::NotFound && !emptyImage.image,
        "zero-byte image input fails without creating image state");

    const std::vector<uint8_t> corrupt = { 'n', 'o', 't', ' ', 'a', ' ', 'p', 'n', 'g' };
    const ImageBitmap corruptImage = ImageAdapter::LoadFromBytes(corrupt, "corrupt.png");
    check(corruptImage.status == ImageLoadStatus::UnsupportedFormat && !corruptImage.image,
        "corrupt non-PNG contents fail without creating image state");

    const ImageBitmap bmpPath = ImageAdapter::LoadFromFile("unsupported.bmp");
    const ImageBitmap gifPath = ImageAdapter::LoadFromFile("unsupported.gif");
    check(bmpPath.status == ImageLoadStatus::UnsupportedFormat && !bmpPath.image &&
        gifPath.status == ImageLoadStatus::UnsupportedFormat && !gifPath.image,
        "production ImageAdapter rejects BMP and GIF suffixes as UnsupportedFormat before file reads");

    const std::vector<uint8_t> signatureOnly(valid.begin(), valid.begin() + std::min<size_t>(8, valid.size()));
    const ImageBitmap signatureOnlyImage = ImageAdapter::LoadFromBytes(signatureOnly, "signature-only.png");
    check(signatureOnlyImage.status == ImageLoadStatus::DecodeFailed && !signatureOnlyImage.image,
        "PNG-signature-only input reaches bounded decoder failure");

    const std::vector<uint8_t> truncatedHeader(valid.begin(), valid.begin() + std::min<size_t>(31, valid.size()));
    const ImageBitmap truncatedHeaderImage = ImageAdapter::LoadFromBytes(truncatedHeader, "truncated-header.png");
    check(truncatedHeaderImage.status == ImageLoadStatus::DecodeFailed && !truncatedHeaderImage.image,
        "truncated PNG header or first chunk fails through the PNG decoder");

    const std::vector<uint8_t> truncatedData(valid.begin(), valid.begin() + std::min<size_t>(64, valid.size()));
    const ImageBitmap truncatedDataImage = ImageAdapter::LoadFromBytes(truncatedData, "truncated-data.png");
    check(truncatedDataImage.status == ImageLoadStatus::DecodeFailed && !truncatedDataImage.image,
        "truncated PNG data fails without returning a partial image");

    std::vector<uint8_t> oversizedDimensions(24, 0);
    const uint8_t signature[] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    std::copy(signature, signature + sizeof(signature), oversizedDimensions.begin());
    oversizedDimensions[12] = 'I'; oversizedDimensions[13] = 'H';
    oversizedDimensions[14] = 'D'; oversizedDimensions[15] = 'R';
    setBe32(oversizedDimensions, 16, limits.maxWidth + 1u);
    setBe32(oversizedDimensions, 20, 1u);
    const ImageBitmap oversizedImage = ImageAdapter::LoadFromBytes(oversizedDimensions, "oversized-dimensions.png");
    check(oversizedImage.status == ImageLoadStatus::TooLarge && !oversizedImage.image,
        "declared width above the supported limit is rejected before decoder allocation");

    std::vector<uint8_t> oversizedFile(limits.maxBytes + 1u, 0);
    Vfs::instance().writeFile("/appmodel-phase11-oversized.png", oversizedFile);
    std::vector<uint8_t> boundedRead;
    const VfsReadFileStatus oversizedVfsStatus = Vfs::instance().readFileBounded(
        "/appmodel-phase11-oversized.png", boundedRead, limits.maxBytes);
    const ImageBitmap oversizedVfsImage = ImageAdapter::LoadFromFile("/appmodel-phase11-oversized.png");
    check(oversizedVfsStatus == VfsReadFileStatus::TooLarge && boundedRead.empty() &&
        oversizedVfsImage.status == ImageLoadStatus::TooLarge && !oversizedVfsImage.image,
        "oversized VFS image input is rejected before the adapter copies its bytes");

    const std::filesystem::path fixtureDirectory = std::filesystem::path("tmp") /
        ("phase11-image-adapter-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(fixtureDirectory);
    const std::filesystem::path oversizedHostPath = fixtureDirectory / "host-oversized.png";
    {
        std::ofstream file(oversizedHostPath, std::ios::binary);
        std::vector<char> zeros(static_cast<size_t>(limits.maxBytes) + 1u, 0);
        file.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
    }
    const std::string virtualOversizedHostPath = "/" + oversizedHostPath.generic_string();
    std::vector<uint8_t> boundedHostRead;
    const VfsReadFileStatus hostStatus = Vfs::instance().readFileBounded(
        virtualOversizedHostPath, boundedHostRead, limits.maxBytes);
    const ImageBitmap hostOversizedImage = ImageAdapter::LoadFromFile(virtualOversizedHostPath);
    check(hostStatus == VfsReadFileStatus::TooLarge && boundedHostRead.empty() &&
        hostOversizedImage.status == ImageLoadStatus::TooLarge && !hostOversizedImage.image,
        "hosted VFS refuses an oversized host file before allocating the read buffer");

    std::error_code ignored;
    std::filesystem::remove("appmodel-phase11-oversized.png", ignored);
    std::filesystem::remove(oversizedHostPath, ignored);
    std::filesystem::remove(fixtureDirectory, ignored);
    std::cout << "imageAdapterBoundsChecks=" << (checks - failures) << "/" << checks << "\n";
    return failures == 0 ? 0 : 1;
}
