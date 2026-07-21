#include "image_viewer.h"

#include "open_dialog.h"
#include "save_dialog.h"
#include "desktop_theme.h"
#include "desktop_service.h"
#include "gui_protocol.h"
#include "kernel/core/include/kernel/image_adapter.h"
#include "logger.h"
#include "png_codec.h"
#include "vfs.h"

#include <algorithm>
#include <cstddef>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <system_error>
#include <utility>
#include <thread>

namespace gxos { namespace apps {

uint64_t ImageViewer::s_windowId = 0;
int ImageViewer::s_windowW = ImageViewer::kWinW;
int ImageViewer::s_windowH = ImageViewer::kWinH;
std::string ImageViewer::s_filePath;
std::string ImageViewer::s_originalPath;
std::string ImageViewer::s_displayPath;
std::string ImageViewer::s_currentDirectory;
std::string ImageViewer::s_fileName;
std::string ImageViewer::s_windowTitle = "Image Viewer";
std::string ImageViewer::s_statusText = "No image loaded";
std::string ImageViewer::s_errorText;
std::string ImageViewer::s_noticeText;
gui::ImagePtr ImageViewer::s_image;
int ImageViewer::s_originalW = 0;
int ImageViewer::s_originalH = 0;
bool ImageViewer::s_isDirty = false;
float ImageViewer::s_zoomLevel = 1.0f;
ImageViewer::ZoomMode ImageViewer::s_zoomMode = ImageViewer::ZoomMode::FitToWindow;
int ImageViewer::s_panX = 0;
int ImageViewer::s_panY = 0;
bool ImageViewer::s_hasTransparency = false;
ImageViewer::BackgroundMode ImageViewer::s_backgroundMode = ImageViewer::BackgroundMode::Solid;
std::vector<std::string> ImageViewer::s_folderImages;
int ImageViewer::s_currentImageIndex = -1;
bool ImageViewer::s_leftMouseDown = false;
bool ImageViewer::s_dragPending = false;
bool ImageViewer::s_dragging = false;
int ImageViewer::s_dragStartX = 0;
int ImageViewer::s_dragStartY = 0;
int ImageViewer::s_dragStartPanX = 0;
int ImageViewer::s_dragStartPanY = 0;
int ImageViewer::s_lastKeyCode = 0;
bool ImageViewer::s_keyDown = false;
bool ImageViewer::s_chromeWidgetsBuilt = false;
int ImageViewer::s_chromeWidgetsW = 0;
int ImageViewer::s_chromeWidgetsH = 0;
ImageViewer::HistorySnapshot ImageViewer::s_originalSnapshot{};
bool ImageViewer::s_hasOriginalSnapshot = false;
std::vector<ImageViewer::HistorySnapshot> ImageViewer::s_undoStack;
std::vector<ImageViewer::HistorySnapshot> ImageViewer::s_redoStack;

namespace {

static void publishMessage(gui::MsgType type, const std::string& payload) {
    ipc::Message m;
    m.type = static_cast<uint32_t>(type);
    m.data.assign(payload.begin(), payload.end());
    ipc::Bus::publish("gui.input", std::move(m), false);
}

static void publishWindowText(uint64_t windowId, int x, int y, const std::string& text, bool colored = false) {
    if (windowId == 0) return;
    if (colored) {
        publishMessage(gui::MsgType::MT_DrawTextAtColor, gui::packDrawTextAtColor(windowId, x, y, 235, 235, 235, text));
    } else {
        publishMessage(gui::MsgType::MT_DrawTextAt, gui::packDrawTextAt(windowId, x, y, text));
    }
}

static void publishWindowTextColor(uint64_t windowId, int x, int y, uint8_t r, uint8_t g, uint8_t b, const std::string& text) {
    if (windowId == 0) return;
    publishMessage(gui::MsgType::MT_DrawTextAtColor, gui::packDrawTextAtColor(windowId, x, y, r, g, b, text));
}

static void publishWindowRect(uint64_t windowId, int x, int y, int w, int h, int r, int g, int b) {
    if (windowId == 0) return;
    std::ostringstream oss;
    oss << windowId << "|" << x << "|" << y << "|" << w << "|" << h << "|" << r << "|" << g << "|" << b;
    publishMessage(gui::MsgType::MT_DrawRect, oss.str());
}

static uint8_t colorR(uint32_t color) {
    return static_cast<uint8_t>((color >> 16) & 0xFFu);
}

static uint8_t colorG(uint32_t color) {
    return static_cast<uint8_t>((color >> 8) & 0xFFu);
}

static uint8_t colorB(uint32_t color) {
    return static_cast<uint8_t>(color & 0xFFu);
}

static void publishWindowRectColor(uint64_t windowId, int x, int y, int w, int h, uint32_t color) {
    publishWindowRect(windowId, x, y, w, h, colorR(color), colorG(color), colorB(color));
}

static void publishWindowFrameColor(uint64_t windowId, int x, int y, int w, int h, uint32_t color) {
    if (windowId == 0 || w <= 0 || h <= 0) {
        return;
    }

    publishWindowRectColor(windowId, x, y, w, 1, color);
    if (h > 1) {
        publishWindowRectColor(windowId, x, y + h - 1, w, 1, color);
    }
    if (w > 1 && h > 2) {
        publishWindowRectColor(windowId, x, y + 1, 1, h - 2, color);
        publishWindowRectColor(windowId, x + w - 1, y + 1, 1, h - 2, color);
    }
}

static uint32_t blendThemeColor(uint32_t baseColor, uint32_t overlayColor, int overlayPercent) {
    if (overlayPercent <= 0) {
        return baseColor;
    }
    if (overlayPercent >= 100) {
        return overlayColor;
    }

    const int baseR = static_cast<int>(colorR(baseColor));
    const int baseG = static_cast<int>(colorG(baseColor));
    const int baseB = static_cast<int>(colorB(baseColor));
    const int overR = static_cast<int>(colorR(overlayColor));
    const int overG = static_cast<int>(colorG(overlayColor));
    const int overB = static_cast<int>(colorB(overlayColor));
    const int keepPercent = 100 - overlayPercent;

    return 0xFF000000u |
        (static_cast<uint32_t>((baseR * keepPercent + overR * overlayPercent) / 100) << 16) |
        (static_cast<uint32_t>((baseG * keepPercent + overG * overlayPercent) / 100) << 8) |
        static_cast<uint32_t>((baseB * keepPercent + overB * overlayPercent) / 100);
}

static uint32_t ImageViewerBodyColor(const DesktopTheme& theme) {
    if (theme.id != DesktopThemeId::SciFi) {
        return 0xFF1E1E1Eu;
    }
    return blendThemeColor(theme.windowBackground, theme.taskbarBackground, 22);
}

static uint32_t ImageViewerPanelColor(const DesktopTheme& theme) {
    if (theme.id != DesktopThemeId::SciFi) {
        return 0xFF1E1E1Eu;
    }
    return blendThemeColor(theme.windowBackground, theme.taskbarBackground, 12);
}

static uint32_t ImageViewerStatusColor(const DesktopTheme& theme) {
    if (theme.id != DesktopThemeId::SciFi) {
        return 0xFF1E1E1Eu;
    }
    return blendThemeColor(theme.taskbarBackground, theme.windowBorder, 16);
}

static uint32_t ImageViewerPreviewBorderColor(const DesktopTheme& theme) {
    if (theme.id != DesktopThemeId::SciFi) {
        return 0xFF2D2D2Du;
    }
    return blendThemeColor(theme.windowBorder, theme.taskbarBorder, 22);
}

static uint32_t ImageViewerSeparatorColor(const DesktopTheme& theme) {
    if (theme.id != DesktopThemeId::SciFi) {
        return 0xFF2A2A2Au;
    }
    return blendThemeColor(theme.windowBorder, theme.taskbarBackground, 26);
}

static uint32_t ImageViewerTextColor(const DesktopTheme& theme) {
    if (theme.id != DesktopThemeId::SciFi) {
        return 0xFFEAEAEAu;
    }
    return theme.titleBarText;
}

static uint32_t ImageViewerMutedTextColor(const DesktopTheme& theme) {
    if (theme.id != DesktopThemeId::SciFi) {
        return 0xFFC7C7C7u;
    }
    return blendThemeColor(theme.titleBarText, theme.mutedAccent, 30);
}

static uint32_t ImageViewerAccentColor(const DesktopTheme& theme) {
    if (theme.id != DesktopThemeId::SciFi) {
        return 0xFFF06060u;
    }
    return theme.accent;
}

static int clampInt(int value, int minimum, int maximum) {
    if (maximum < minimum) return minimum;
    return std::max(minimum, std::min(value, maximum));
}

static float clampFloat(float value, float minimum, float maximum) {
    if (maximum < minimum) return minimum;
    return std::max(minimum, std::min(value, maximum));
}

static bool clampImageDimensions(int& width, int& height) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    const gui::ImageSafetyLimits limits = gui::DefaultImageSafetyLimits();
    if (width > static_cast<int>(limits.maxWidth)) {
        width = static_cast<int>(limits.maxWidth);
    }
    if (height > static_cast<int>(limits.maxHeight)) {
        height = static_cast<int>(limits.maxHeight);
    }

    const uint64_t pixelCount = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
    return pixelCount > 0 && pixelCount <= static_cast<uint64_t>(limits.maxPixels);
}

static const char* zoomModeName(ImageViewer::ZoomMode mode) {
    switch (mode) {
    case ImageViewer::ZoomMode::FitToWindow: return "Fit";
    case ImageViewer::ZoomMode::ActualSize: return "Actual";
    case ImageViewer::ZoomMode::Custom: return "Custom";
    default: return "Custom";
    }
}

static std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

static std::string joinStatusText(const std::string& base, const std::string& extra) {
    if (base.empty()) return extra;
    if (extra.empty()) return base;
    return base + " | " + extra;
}

static void appendByte(std::vector<uint8_t>& out, uint8_t value) {
    out.push_back(value);
}

static void appendUint16LE(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

static void appendUint32BE(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

static uint32_t crc32ForBytes(const uint8_t* data, size_t size) {
    static uint32_t table[256];
    static bool initialized = false;
    if (!initialized) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int bit = 0; bit < 8; ++bit) {
                if (c & 1u) {
                    c = 0xEDB88320u ^ (c >> 1);
                } else {
                    c >>= 1;
                }
            }
            table[i] = c;
        }
        initialized = true;
    }

    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

static uint32_t adler32ForBytes(const uint8_t* data, size_t size) {
    const uint32_t kMod = 65521u;
    uint32_t a = 1u;
    uint32_t b = 0u;
    for (size_t i = 0; i < size; ++i) {
        a = (a + data[i]) % kMod;
        b = (b + a) % kMod;
    }
    return (b << 16) | a;
}

static void appendPngChunk(std::vector<uint8_t>& out, const char type[4], const std::vector<uint8_t>& data) {
    appendUint32BE(out, static_cast<uint32_t>(data.size()));
    const size_t start = out.size();
    out.push_back(static_cast<uint8_t>(type[0]));
    out.push_back(static_cast<uint8_t>(type[1]));
    out.push_back(static_cast<uint8_t>(type[2]));
    out.push_back(static_cast<uint8_t>(type[3]));
    out.insert(out.end(), data.begin(), data.end());
    const uint32_t crc = crc32ForBytes(out.data() + start, 4u + data.size());
    appendUint32BE(out, crc);
}

static std::string sanitizePathComponent(std::string value) {
    for (char& ch : value) {
        switch (ch) {
        case '/':
        case '\\':
        case ':':
        case '*':
        case '?':
        case '"':
        case '<':
        case '>':
        case '|':
            ch = '_';
            break;
        default:
            break;
        }
    }
    return value;
}

static std::string makeEditedPreviewPath(const std::string& originalPath, uint64_t windowId) {
    std::filesystem::path source(originalPath.empty() ? "image.png" : originalPath);
    std::string baseName = source.stem().string();
    if (baseName.empty()) baseName = "image";
    baseName = sanitizePathComponent(baseName);
    std::filesystem::path preview = std::filesystem::path("tmp") / "imageviewer" / (baseName + "-" + std::to_string(windowId) + "-preview.png");
    return preview.generic_string();
}

static gui::ImagePtr rotateImageLeft(const gui::ImagePtr& image) {
    if (!image || !image->isValid() || image->Channels < 4) return nullptr;
    gui::ImagePtr rotated = std::make_shared<gui::Image>(image->Height, image->Width, image->Channels);
    if (!rotated || !rotated->isValid()) return nullptr;
    const int channels = image->Channels;
    for (int y = 0; y < image->Height; ++y) {
        for (int x = 0; x < image->Width; ++x) {
            const uint8_t* src = image->Pixels + (static_cast<size_t>(y) * image->Width + x) * channels;
            const int dstX = y;
            const int dstY = image->Width - 1 - x;
            uint8_t* dst = rotated->Pixels + (static_cast<size_t>(dstY) * rotated->Width + dstX) * channels;
            for (int c = 0; c < channels; ++c) dst[c] = src[c];
        }
    }
    return rotated;
}

static gui::ImagePtr rotateImageRight(const gui::ImagePtr& image) {
    if (!image || !image->isValid() || image->Channels < 4) return nullptr;
    gui::ImagePtr rotated = std::make_shared<gui::Image>(image->Height, image->Width, image->Channels);
    if (!rotated || !rotated->isValid()) return nullptr;
    const int channels = image->Channels;
    for (int y = 0; y < image->Height; ++y) {
        for (int x = 0; x < image->Width; ++x) {
            const uint8_t* src = image->Pixels + (static_cast<size_t>(y) * image->Width + x) * channels;
            const int dstX = image->Height - 1 - y;
            const int dstY = x;
            uint8_t* dst = rotated->Pixels + (static_cast<size_t>(dstY) * rotated->Width + dstX) * channels;
            for (int c = 0; c < channels; ++c) dst[c] = src[c];
        }
    }
    return rotated;
}

static gui::ImagePtr flipImageHorizontal(const gui::ImagePtr& image) {
    if (!image || !image->isValid() || image->Channels < 4) return nullptr;
    gui::ImagePtr flipped = std::make_shared<gui::Image>(image->Width, image->Height, image->Channels);
    if (!flipped || !flipped->isValid()) return nullptr;
    const int channels = image->Channels;
    for (int y = 0; y < image->Height; ++y) {
        for (int x = 0; x < image->Width; ++x) {
            const uint8_t* src = image->Pixels + (static_cast<size_t>(y) * image->Width + x) * channels;
            const int dstX = image->Width - 1 - x;
            const int dstY = y;
            uint8_t* dst = flipped->Pixels + (static_cast<size_t>(dstY) * flipped->Width + dstX) * channels;
            for (int c = 0; c < channels; ++c) dst[c] = src[c];
        }
    }
    return flipped;
}

static gui::ImagePtr flipImageVertical(const gui::ImagePtr& image) {
    if (!image || !image->isValid() || image->Channels < 4) return nullptr;
    gui::ImagePtr flipped = std::make_shared<gui::Image>(image->Width, image->Height, image->Channels);
    if (!flipped || !flipped->isValid()) return nullptr;
    const int channels = image->Channels;
    for (int y = 0; y < image->Height; ++y) {
        for (int x = 0; x < image->Width; ++x) {
            const uint8_t* src = image->Pixels + (static_cast<size_t>(y) * image->Width + x) * channels;
            const int dstX = x;
            const int dstY = image->Height - 1 - y;
            uint8_t* dst = flipped->Pixels + (static_cast<size_t>(dstY) * flipped->Width + dstX) * channels;
            for (int c = 0; c < channels; ++c) dst[c] = src[c];
        }
    }
    return flipped;
}

static bool clampCropRectToImage(const gui::ImagePtr& image, int& x, int& y, int& width, int& height) {
    if (!image || !image->isValid() || image->Width <= 0 || image->Height <= 0) {
        return false;
    }
    if (width <= 0 || height <= 0) {
        return false;
    }

    x = clampInt(x, 0, image->Width - 1);
    y = clampInt(y, 0, image->Height - 1);

    const int maxWidth = image->Width - x;
    const int maxHeight = image->Height - y;
    width = std::min(width, maxWidth);
    height = std::min(height, maxHeight);
    return width > 0 && height > 0;
}

static bool encodePng(const gui::ImagePtr& image, std::vector<uint8_t>& bytes, std::string& error) {
    bytes.clear();
    error.clear();

    if (!image || !image->isValid() || image->Channels < 4) {
        error = "Current image is not a valid RGBA image";
        return false;
    }
    if (image->Width <= 0 || image->Height <= 0) {
        error = "Current image has invalid dimensions";
        return false;
    }

    bytes.insert(bytes.end(), { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' });

    std::vector<uint8_t> ihdr;
    ihdr.reserve(13);
    appendUint32BE(ihdr, static_cast<uint32_t>(image->Width));
    appendUint32BE(ihdr, static_cast<uint32_t>(image->Height));
    appendByte(ihdr, 8);  // bit depth
    appendByte(ihdr, 6);  // color type RGBA
    appendByte(ihdr, 0);  // compression
    appendByte(ihdr, 0);  // filter
    appendByte(ihdr, 0);  // interlace
    appendPngChunk(bytes, "IHDR", ihdr);

    std::vector<uint8_t> raw;
    const size_t rowBytes = 1u + static_cast<size_t>(image->Width) * 4u;
    raw.reserve(rowBytes * static_cast<size_t>(image->Height));
    for (int y = 0; y < image->Height; ++y) {
        raw.push_back(0);  // filter type 0
        for (int x = 0; x < image->Width; ++x) {
            const uint8_t* src = image->Pixels + (static_cast<size_t>(y) * image->Width + x) * image->Channels;
            raw.push_back(src[0]);
            raw.push_back(src[1]);
            raw.push_back(src[2]);
            raw.push_back(src[3]);
        }
    }

    std::vector<uint8_t> zlibData;
    zlibData.reserve(raw.size() + (raw.size() / 65535u + 2u) * 5u + 6u);
    zlibData.push_back(0x78);
    zlibData.push_back(0x01);

    size_t offset = 0;
    while (offset < raw.size()) {
        const size_t blockLen = std::min<size_t>(65535u, raw.size() - offset);
        const bool finalBlock = offset + blockLen >= raw.size();
        zlibData.push_back(finalBlock ? 0x01 : 0x00);
        appendUint16LE(zlibData, static_cast<uint16_t>(blockLen));
        appendUint16LE(zlibData, static_cast<uint16_t>(~static_cast<uint16_t>(blockLen)));
        zlibData.insert(zlibData.end(), raw.begin() + offset, raw.begin() + offset + blockLen);
        offset += blockLen;
    }

    appendUint32BE(zlibData, adler32ForBytes(raw.data(), raw.size()));
    appendPngChunk(bytes, "IDAT", zlibData);
    appendPngChunk(bytes, "IEND", {});
    return true;
}

static bool writePngToVfs(const std::string& path, const gui::ImagePtr& image, std::string& error) {
    std::vector<uint8_t> bytes;
    if (!encodePng(image, bytes, error)) {
        return false;
    }
    if (!Vfs::instance().writeFile(path, bytes)) {
        error = "Failed to write PNG preview to VFS path: " + path;
        return false;
    }
    return true;
}

static gxos::apps::ImageViewer::HistorySnapshot captureSnapshot(const gui::ImagePtr& image) {
    gxos::apps::ImageViewer::HistorySnapshot snapshot;
    if (!image || !image->isValid() || image->Width <= 0 || image->Height <= 0 || image->Channels <= 0 || !image->Pixels) {
        return snapshot;
    }

    snapshot.width = image->Width;
    snapshot.height = image->Height;
    snapshot.channels = image->Channels;
    const size_t byteCount = static_cast<size_t>(snapshot.width) * static_cast<size_t>(snapshot.height) * static_cast<size_t>(snapshot.channels);
    snapshot.pixels.assign(image->Pixels, image->Pixels + byteCount);
    return snapshot;
}

static bool snapshotEquals(const gxos::apps::ImageViewer::HistorySnapshot& a, const gxos::apps::ImageViewer::HistorySnapshot& b) {
    return a.width == b.width &&
        a.height == b.height &&
        a.channels == b.channels &&
        a.pixels == b.pixels;
}

static gui::ImagePtr imageFromSnapshot(const gxos::apps::ImageViewer::HistorySnapshot& snapshot) {
    if (snapshot.width <= 0 || snapshot.height <= 0 || snapshot.channels <= 0 || snapshot.pixels.empty()) {
        return nullptr;
    }

    gui::ImagePtr image = std::make_shared<gui::Image>(snapshot.width, snapshot.height, snapshot.channels);
    if (!image || !image->isValid() || !image->Pixels) {
        return nullptr;
    }

    const size_t byteCount = static_cast<size_t>(snapshot.width) * static_cast<size_t>(snapshot.height) * static_cast<size_t>(snapshot.channels);
    if (snapshot.pixels.size() < byteCount) {
        return nullptr;
    }

    std::copy(snapshot.pixels.begin(), snapshot.pixels.begin() + byteCount, image->Pixels);
    return image;
}

} // namespace

uint64_t ImageViewer::Launch(const std::string& filePath) {
    s_filePath = filePath;
    s_originalPath = filePath;
    s_displayPath = filePath;
    s_currentDirectory = normalizeFolderPath(filePath);
    s_fileName = displayNameForPath(filePath);
    s_windowId = 0;
    s_windowW = kWinW;
    s_windowH = kWinH;
    s_isDirty = false;
    s_zoomMode = ZoomMode::FitToWindow;
    s_zoomLevel = 1.0f;
    s_panX = 0;
    s_panY = 0;
    s_hasTransparency = false;
    s_backgroundMode = BackgroundMode::Solid;
    s_hasOriginalSnapshot = false;
    s_originalSnapshot = {};
    s_undoStack.clear();
    s_redoStack.clear();
    s_folderImages.clear();
    s_currentImageIndex = -1;
    s_leftMouseDown = false;
    s_dragPending = false;
    s_dragging = false;
    s_image.reset();
    s_originalW = 0;
    s_originalH = 0;
    s_chromeWidgetsBuilt = false;
    s_chromeWidgetsW = 0;
    s_chromeWidgetsH = 0;
    s_errorText.clear();
    s_noticeText.clear();
    s_statusText = s_fileName.empty() ? "No image loaded" : "Loading " + s_fileName + "...";
    s_windowTitle = s_fileName.empty() ? "Image Viewer" : "Image Viewer - " + s_fileName;

    ProcessSpec spec{"ImageViewer", &ImageViewer::main};
    spec.appId = "gxos.builtin.imageviewer";
    std::vector<std::string> args;
    if (!filePath.empty()) args.push_back(filePath);
    return ProcessTable::spawn(spec, args);
}

std::string ImageViewer::displayNameForPath(const std::string& path) {
    if (path.empty()) return std::string();
    std::filesystem::path filePath(path);
    std::string name = filePath.filename().string();
    return name.empty() ? path : name;
}

std::string ImageViewer::normalizeFolderPath(const std::string& path) {
    if (path.empty()) return std::string();
    std::filesystem::path p(path);
    std::filesystem::path folder = p.has_filename() ? p.parent_path() : p;
    if (folder.empty()) return std::string();
    return folder.lexically_normal().generic_string();
}

std::string ImageViewer::normalizeCaseForSort(const std::string& value) {
    return lowerCopy(std::filesystem::path(value).lexically_normal().generic_string());
}

bool ImageViewer::safeEqualsPath(const std::string& a, const std::string& b) {
    return normalizeCaseForSort(a) == normalizeCaseForSort(b);
}

bool ImageViewer::isPngPath(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    return lowerCopy(ext) == ".png";
}

float ImageViewer::fitScaleForClientArea(int clientWidth, int clientHeight) {
    if (!s_image || s_originalW <= 0 || s_originalH <= 0) return 1.0f;
    if (clientWidth <= 0 || clientHeight <= 0) return 1.0f;

    const float scaleX = static_cast<float>(clientWidth) / static_cast<float>(s_originalW);
    const float scaleY = static_cast<float>(clientHeight) / static_cast<float>(s_originalH);
    return std::min(scaleX, scaleY);
}

float ImageViewer::effectiveScaleForCurrentMode() {
    if (!s_image) return 1.0f;

    int contentLeft = 0;
    int contentTop = 0;
    int contentWidth = 1;
    int contentHeight = 1;
    contentMetrics(contentLeft, contentTop, contentWidth, contentHeight);

    const float fitScale = fitScaleForClientArea(contentWidth, contentHeight);
    if (s_zoomMode == ZoomMode::FitToWindow) {
        return fitScale * s_zoomLevel;
    }
    return s_zoomLevel;
}

void ImageViewer::clampZoomForCurrentMode() {
    if (!s_image) return;

    int contentLeft = 0;
    int contentTop = 0;
    int contentWidth = 1;
    int contentHeight = 1;
    contentMetrics(contentLeft, contentTop, contentWidth, contentHeight);

    const float fitScale = fitScaleForClientArea(contentWidth, contentHeight);
    const float minimumScale = kMinZoom;
    const float maximumScale = kMaxZoom;

    if (s_zoomMode == ZoomMode::FitToWindow) {
        const float baseScale = std::max(0.001f, fitScale);
        const float effectiveScale = clampFloat(baseScale * s_zoomLevel, minimumScale, maximumScale);
        s_zoomLevel = effectiveScale / baseScale;
    } else {
        s_zoomLevel = clampFloat(s_zoomLevel, minimumScale, maximumScale);
    }
}

void ImageViewer::clampPanForCurrentImage() {
    if (!s_image) return;

    int contentLeft = 0;
    int contentTop = 0;
    int contentWidth = 1;
    int contentHeight = 1;
    int drawX = 0;
    int drawY = 0;
    int drawW = 1;
    int drawH = 1;
    imageMetrics(drawX, drawY, drawW, drawH, contentLeft, contentTop, contentWidth, contentHeight);
    (void)drawX;
    (void)drawY;

    if (drawW <= contentWidth) {
        s_panX = 0;
    } else {
        s_panX = clampInt(s_panX, contentWidth - drawW, 0);
    }

    if (drawH <= contentHeight) {
        s_panY = 0;
    } else {
        s_panY = clampInt(s_panY, contentHeight - drawH, 0);
    }
}

void ImageViewer::contentMetrics(int& contentLeft, int& contentTop, int& contentWidth, int& contentHeight) {
    const int titleBarH = 32;
    const int statusBarH = 22;
    const int buttonBarH = 70;
    const int margin = 12;

    contentTop = titleBarH + margin;
    const int contentBottom = std::max(contentTop + 1, s_windowH - statusBarH - buttonBarH - margin);
    contentLeft = margin;
    const int contentRight = std::max(contentLeft + 1, s_windowW - margin);
    contentWidth = std::max(1, contentRight - contentLeft);
    contentHeight = std::max(1, contentBottom - contentTop);
}

void ImageViewer::imageMetrics(int& drawX, int& drawY, int& drawW, int& drawH, int& contentLeft, int& contentTop, int& contentWidth, int& contentHeight) {
    contentMetrics(contentLeft, contentTop, contentWidth, contentHeight);

    if (!s_image || s_originalW <= 0 || s_originalH <= 0) {
        drawX = contentLeft;
        drawY = contentTop;
        drawW = 1;
        drawH = 1;
        return;
    }

    const float fitScale = fitScaleForClientArea(contentWidth, contentHeight);
    const float effectiveScale = s_zoomMode == ZoomMode::FitToWindow
        ? fitScale * s_zoomLevel
        : s_zoomLevel;
    const float clampedScale = clampFloat(effectiveScale, kMinZoom, kMaxZoom);

    drawW = std::max(1, static_cast<int>(static_cast<float>(s_originalW) * clampedScale + 0.5f));
    drawH = std::max(1, static_cast<int>(static_cast<float>(s_originalH) * clampedScale + 0.5f));

    const int centeredX = contentLeft + (contentWidth - drawW) / 2;
    const int centeredY = contentTop + (contentHeight - drawH) / 2;

    int panX = s_panX;
    int panY = s_panY;
    if (drawW <= contentWidth) {
        panX = 0;
    } else {
        panX = clampInt(panX, contentWidth - drawW, 0);
    }
    if (drawH <= contentHeight) {
        panY = 0;
    } else {
        panY = clampInt(panY, contentHeight - drawH, 0);
    }

    drawX = centeredX + panX;
    drawY = centeredY + panY;
}

bool ImageViewer::pointInsideCurrentImage(int x, int y) {
    int drawX = 0;
    int drawY = 0;
    int drawW = 1;
    int drawH = 1;
    int contentLeft = 0;
    int contentTop = 0;
    int contentWidth = 1;
    int contentHeight = 1;
    imageMetrics(drawX, drawY, drawW, drawH, contentLeft, contentTop, contentWidth, contentHeight);
    return x >= drawX && x < drawX + drawW && y >= drawY && y < drawY + drawH;
}

bool ImageViewer::detectTransparency(const gui::ImagePtr& image) {
    if (!image || !image->Pixels || image->Width <= 0 || image->Height <= 0 || image->Channels < 4) {
        return false;
    }

    const size_t pixelCount = static_cast<size_t>(image->Width) * static_cast<size_t>(image->Height);
    const uint8_t* pixels = image->Pixels;
    for (size_t idx = 0; idx < pixelCount; ++idx) {
        if (pixels[idx * 4 + 3] < 255) {
            return true;
        }
    }
    return false;
}

void ImageViewer::drawCheckerboardBackground(int x, int y, int w, int h) {
    if (s_windowId == 0 || w <= 0 || h <= 0) return;

    const int tileSize = 16;
    for (int row = 0; row < h; row += tileSize) {
        for (int col = 0; col < w; col += tileSize) {
            const int tileW = std::min(tileSize, w - col);
            const int tileH = std::min(tileSize, h - row);
            const bool darkTile = (((row / tileSize) + (col / tileSize)) % 2) == 0;
            publishWindowRect(s_windowId, x + col, y + row, tileW, tileH,
                darkTile ? 74 : 104,
                darkTile ? 74 : 104,
                darkTile ? 78 : 108);
        }
    }
}

std::string ImageViewer::currentImagePositionText() {
    if (s_folderImages.size() <= 1 || s_currentImageIndex < 0 || s_currentImageIndex >= static_cast<int>(s_folderImages.size())) {
        return std::string();
    }

    std::ostringstream oss;
    oss << (s_currentImageIndex + 1) << " of " << s_folderImages.size();
    return oss.str();
}

std::string ImageViewer::modeText() {
    switch (s_zoomMode) {
    case ZoomMode::FitToWindow: return "Fit";
    case ZoomMode::ActualSize: return "Actual";
    case ZoomMode::Custom: return "Custom";
    default: return "Custom";
    }
}

std::string ImageViewer::statusText() {
    if (!s_image) {
        if (!s_errorText.empty()) return joinStatusText(s_errorText, s_noticeText);
        if (!s_noticeText.empty()) return s_noticeText;
        return s_statusText.empty() ? "No image loaded" : s_statusText;
    }

    const float effectiveScale = effectiveScaleForCurrentMode();
    const int zoomPct = static_cast<int>((effectiveScale * 100.0f) + 0.5f);
    std::ostringstream oss;
    oss << (s_fileName.empty() ? s_filePath : s_fileName)
        << " | " << s_originalW << "x" << s_originalH
        << " | " << zoomPct << "%"
        << " | " << modeText();

    const std::string position = currentImagePositionText();
    if (!position.empty()) {
        oss << " | " << position;
    }

    if (s_isDirty) {
        oss << " | Modified";
    }

    if (!s_noticeText.empty()) {
        oss << " | " << s_noticeText;
    }

    if (!s_errorText.empty()) {
        oss << " | error: " << s_errorText;
    }

    return oss.str();
}

void ImageViewer::refreshWindowTitle() {
    if (s_windowId == 0) return;
    s_windowTitle = s_fileName.empty() ? "Image Viewer" : "Image Viewer - " + s_fileName;
    if (s_isDirty) {
        s_windowTitle += " *";
    }
    publishMessage(gui::MsgType::MT_SetTitle, std::to_string(s_windowId) + "|" + s_windowTitle);
}

void ImageViewer::setNoticeText(const std::string& text) {
    s_noticeText = text;
}

void ImageViewer::showUnsupportedFormat(const std::string& path) {
    const bool hadImage = static_cast<bool>(s_image);
    s_errorText = "Unsupported image format: only PNG is supported in this version";
    s_noticeText.clear();

    if (!hadImage) {
        s_filePath = path;
        s_originalPath = path;
        s_displayPath.clear();
        s_currentDirectory = normalizeFolderPath(path);
        s_fileName = displayNameForPath(path);
        s_image.reset();
        s_originalW = 0;
        s_originalH = 0;
        s_isDirty = false;
        s_hasOriginalSnapshot = false;
        s_originalSnapshot = {};
        ClearEditHistory();
        s_zoomMode = ZoomMode::FitToWindow;
        s_zoomLevel = 1.0f;
        s_panX = 0;
        s_panY = 0;
        s_hasTransparency = false;
        s_backgroundMode = BackgroundMode::Solid;
        refreshFolderImageList(path);
        s_statusText = s_errorText;
        refreshWindowTitle();
        updateImageStatus();
        updateDisplay();
        return;
    }

    updateDisplayImage();
}

bool ImageViewer::refreshFolderImageList(const std::string& path) {
    s_folderImages.clear();
    s_currentImageIndex = -1;
    s_currentDirectory = normalizeFolderPath(path);
    if (s_currentDirectory.empty()) {
        return false;
    }

    std::error_code ec;
    std::filesystem::path folderPath(s_currentDirectory);
    if (!std::filesystem::exists(folderPath, ec) || !std::filesystem::is_directory(folderPath, ec)) {
        return false;
    }

    for (const auto& entry : std::filesystem::directory_iterator(folderPath, ec)) {
        if (ec) break;
        std::error_code entryEc;
        if (!entry.is_regular_file(entryEc) || entryEc) continue;
        const std::string candidate = entry.path().string();
        if (!isPngPath(candidate)) continue;
        s_folderImages.push_back(candidate);
    }

    std::sort(s_folderImages.begin(), s_folderImages.end(), [](const std::string& a, const std::string& b) {
        const std::string left = normalizeCaseForSort(a);
        const std::string right = normalizeCaseForSort(b);
        if (left == right) return a < b;
        return left < right;
    });

    for (size_t i = 0; i < s_folderImages.size(); ++i) {
        if (safeEqualsPath(s_folderImages[i], path)) {
            s_currentImageIndex = static_cast<int>(i);
            break;
        }
    }

    return !s_folderImages.empty();
}

void ImageViewer::updateImageStatus() {
    if (!s_image) {
        if (!s_errorText.empty()) {
            s_statusText = joinStatusText(s_errorText, s_noticeText);
        } else if (!s_noticeText.empty()) {
            s_statusText = s_noticeText;
        } else if (s_statusText.empty()) {
            s_statusText = "No image loaded";
        }
    }
}

bool ImageViewer::loadImagePath(const std::string& path, bool refreshFolderList, bool preserveZoomMode) {
    const bool hadImage = static_cast<bool>(s_image);
    const ZoomMode previousZoomMode = s_zoomMode;
    const float previousZoomLevel = s_zoomLevel;

    gui::ImageBitmap loaded = gui::ImageAdapter::LoadFromFile(path);
    if (!loaded.image) {
        const std::string name = displayNameForPath(path);
        s_errorText = "Failed to load " + (name.empty() ? path : name) + " (" + gui::ImageLoadStatusName(loaded.status) + ")";
        s_noticeText.clear();
        if (!hadImage || !preserveZoomMode) {
            s_filePath = path;
            s_originalPath = path;
            s_displayPath.clear();
            s_fileName = name;
            s_image.reset();
            s_originalW = 0;
            s_originalH = 0;
            s_isDirty = false;
            s_hasOriginalSnapshot = false;
            s_originalSnapshot = {};
            ClearEditHistory();
            s_zoomMode = ZoomMode::FitToWindow;
            s_zoomLevel = 1.0f;
            s_panX = 0;
            s_panY = 0;
            s_hasTransparency = false;
            s_backgroundMode = BackgroundMode::Solid;
            if (refreshFolderList) {
                refreshFolderImageList(path);
            }
            s_statusText = s_errorText;
            refreshWindowTitle();
            updateImageStatus();
            updateDisplay();
        } else {
            s_statusText = s_errorText;
            updateDisplayImage();
        }
        Logger::write(LogLevel::Warn, "ImageViewer: image load failed: " + path +
            " status=" + gui::ImageLoadStatusName(loaded.status));
        return false;
    }

    s_filePath = path;
    s_originalPath = path;
    s_displayPath = path;
    s_fileName = displayNameForPath(path);
    s_image = loaded.image;
    s_originalW = s_image->Width;
    s_originalH = s_image->Height;
    s_hasTransparency = detectTransparency(s_image);
    s_backgroundMode = s_hasTransparency ? BackgroundMode::Checkerboard : BackgroundMode::Solid;
    s_originalSnapshot = captureSnapshot(s_image);
    s_hasOriginalSnapshot = true;
    ClearEditHistory();
    s_isDirty = false;
    s_errorText.clear();
    s_noticeText.clear();

    if (refreshFolderList) {
        refreshFolderImageList(path);
    }
    if (s_currentImageIndex < 0 && !s_folderImages.empty()) {
        for (size_t i = 0; i < s_folderImages.size(); ++i) {
            if (safeEqualsPath(s_folderImages[i], path)) {
                s_currentImageIndex = static_cast<int>(i);
                break;
            }
        }
    }

    if (preserveZoomMode) {
        s_zoomMode = previousZoomMode;
        s_zoomLevel = previousZoomLevel;
    } else {
        s_zoomMode = ZoomMode::FitToWindow;
        s_zoomLevel = 1.0f;
    }
    s_panX = 0;
    s_panY = 0;
    clampZoomForCurrentMode();
    clampPanForCurrentImage();
    updateImageStatus();
    refreshWindowTitle();
    updateDisplay();

    Logger::write(LogLevel::Info, "ImageViewer loaded PNG: " + s_filePath +
        " (" + std::to_string(s_originalW) + "x" + std::to_string(s_originalH) + ")" +
        " transparency=" + (s_hasTransparency ? "true" : "false") +
        " folderImages=" + std::to_string(s_folderImages.size()));
    return true;
}

void ImageViewer::handleWindowResize(int width, int height) {
    if (width > 0) s_windowW = width;
    if (height > 0) s_windowH = height;
    if (s_chromeWidgetsW != s_windowW || s_chromeWidgetsH != s_windowH) {
        s_chromeWidgetsBuilt = false;
    }
    clampZoomForCurrentMode();
    clampPanForCurrentImage();
    updateDisplay();
}

void ImageViewer::fitToWindow() {
    if (!s_image) return;
    s_zoomMode = ZoomMode::FitToWindow;
    s_zoomLevel = 1.0f;
    s_panX = 0;
    s_panY = 0;
    clampZoomForCurrentMode();
    clampPanForCurrentImage();
    updateDisplayImage();
}

bool ImageViewer::navigateRelative(int delta) {
    if (delta == 0) return false;
    if (!CanNavigateAwayFromDirtyDocument(delta < 0 ? "moving to the previous image" : "moving to the next image")) {
        return false;
    }
    if (s_folderImages.size() <= 1) return false;
    if (s_currentImageIndex < 0) return false;

    const int nextIndex = s_currentImageIndex + delta;
    if (nextIndex < 0 || nextIndex >= static_cast<int>(s_folderImages.size())) {
        return false;
    }

    const std::string nextPath = s_folderImages[static_cast<size_t>(nextIndex)];
    return loadImagePath(nextPath, true, true);
}

void ImageViewer::previousImage() {
    (void)navigateRelative(-1);
}

void ImageViewer::nextImage() {
    (void)navigateRelative(1);
}

void ImageViewer::openImageFromDialog() {
    if (!CanNavigateAwayFromDirtyDocument("opening another image")) {
        return;
    }

    const std::string startPath = s_currentDirectory.empty() ? std::string("/") : s_currentDirectory;
    dialogs::OpenDialog::Show(0, 0, startPath, [](const std::string& path) {
        if (path.empty()) {
            return;
        }
        if (!isPngPath(path)) {
            showUnsupportedFormat(path);
            return;
        }
        (void)loadImagePath(path, true, false);
    });
}

bool ImageViewer::trySetCurrentImageAsWallpaper() {
    if (!s_image || s_originalPath.empty()) {
        setNoticeText("Load a PNG first");
        updateDisplayImage();
        return false;
    }

    if (!isPngPath(s_originalPath)) {
        showUnsupportedFormat(s_originalPath);
        return false;
    }

    std::string error;
    if (!gui::DesktopService::DispatchSetAsDesktopBackground(s_originalPath, "ImageViewer", error)) {
        setNoticeText(error.empty() ? "Unable to set desktop background" : error);
        updateDisplayImage();
        return false;
    }
    setNoticeText("Desktop background updated");
    updateDisplayImage();
    return true;
}

int ImageViewer::main(int argc, char** argv) {
    Logger::write(LogLevel::Info, "ImageViewer starting");

    if (argc > 1 && argv[1]) {
        s_filePath = argv[1];
        s_originalPath = s_filePath;
        s_displayPath = s_filePath;
        s_currentDirectory = normalizeFolderPath(s_filePath);
        s_fileName = displayNameForPath(s_filePath);
    }

    s_windowTitle = s_fileName.empty() ? "Image Viewer" : "Image Viewer - " + s_fileName;
    s_statusText = s_fileName.empty() ? "No image loaded" : "Loading " + s_fileName + "...";
    s_errorText.clear();
    s_noticeText.clear();
    s_isDirty = false;
    s_hasOriginalSnapshot = false;
    s_originalSnapshot = {};
    s_undoStack.clear();
    s_redoStack.clear();

    {
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_Create);
        std::string payload = s_windowTitle + "|" + std::to_string(kWinW) + "|" + std::to_string(kWinH);
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    {
        ipc::Message m;
        if (ipc::Bus::pop("gui.output", m, 200)) {
            std::string s(m.data.begin(), m.data.end());
            try {
                s_windowId = std::stoull(s);
            } catch (...) {
                s_windowId = 0;
            }
        }
    }

    if (!s_filePath.empty()) {
        if (!isPngPath(s_filePath)) {
            showUnsupportedFormat(s_filePath);
        } else {
            loadImagePath(s_filePath, true, false);
        }
    } else {
        updateImageStatus();
        updateDisplay();
    }

    bool running = true;
    while (running) {
        ipc::Message ev;
        if (ipc::Bus::pop("gui.output", ev, 150)) {
            if (ev.type == static_cast<uint32_t>(gui::MsgType::MT_Close)) {
                running = false;
            } else if (ev.type == static_cast<uint32_t>(gui::MsgType::MT_Resize)) {
                std::string payload(ev.data.begin(), ev.data.end());
                std::istringstream iss(payload);
                std::string winIdStr, widthStr, heightStr;
                std::getline(iss, winIdStr, '|');
                std::getline(iss, widthStr, '|');
                std::getline(iss, heightStr, '|');
                try {
                    if (std::stoull(winIdStr) == s_windowId) {
                        handleWindowResize(std::stoi(widthStr), std::stoi(heightStr));
                    }
                } catch (...) {
                }
            } else if (ev.type == static_cast<uint32_t>(gui::MsgType::MT_InputKey)) {
                std::string payload(ev.data.begin(), ev.data.end());
                std::istringstream iss(payload);
                std::string keyStr;
                std::string action;
                std::getline(iss, keyStr, '|');
                std::getline(iss, action);

                int keyCode = 0;
                try { keyCode = std::stoi(keyStr); } catch (...) {}
                if (action.empty() || action == "down") {
                    handleKeyPress(keyCode);
                }
            } else if (ev.type == static_cast<uint32_t>(gui::MsgType::MT_InputMouse)) {
                std::string payload(ev.data.begin(), ev.data.end());
                std::istringstream iss(payload);
                std::string xStr, yStr, buttonStr, action;
                std::getline(iss, xStr, '|');
                std::getline(iss, yStr, '|');
                std::getline(iss, buttonStr, '|');
                std::getline(iss, action);
                try {
                    handleMouseInput(std::stoi(xStr), std::stoi(yStr), std::stoi(buttonStr), action);
                } catch (...) {
                }
            } else if (ev.type == static_cast<uint32_t>(gui::MsgType::MT_WidgetEvt)) {
                std::string payload(ev.data.begin(), ev.data.end());
                std::istringstream iss(payload);
                std::string winIdStr, widgetIdStr, event;
                std::getline(iss, winIdStr, '|');
                std::getline(iss, widgetIdStr, '|');
                std::getline(iss, event, '|');
                if (!winIdStr.empty() && !widgetIdStr.empty()) {
                    try {
                        uint64_t winId = std::stoull(winIdStr);
                        int widgetId = std::stoi(widgetIdStr);
                        if (winId == s_windowId && event == "click") {
                            switch (widgetId) {
                            case 1: openImageFromDialog(); break;
                            case 2: previousImage(); break;
                            case 3: nextImage(); break;
                            case 4: zoomIn(); break;
                            case 5: zoomOut(); break;
                            case 6: fitToWindow(); break;
                            case 7: resetZoom(); break;
                            case 8: RotateCurrentImageLeft(); break;
                            case 9: RotateCurrentImageRight(); break;
                            case 10: FlipCurrentImageHorizontal(); break;
                            case 11: FlipCurrentImageVertical(); break;
                            case 12: SaveCurrentImageAsCopy(); break;
                            case 13: (void)trySetCurrentImageAsWallpaper(); break;
                            case 14: UndoEdit(); break;
                            case 15: RedoEdit(); break;
                            case 16: DiscardChanges(); break;
                            case 17: {
                                if (!s_image) {
                                    setNoticeText("Load a PNG first");
                                    updateDisplayImage();
                                    break;
                                }
                                const int targetW = std::max(1, (s_image->Width + 1) / 2);
                                const int targetH = std::max(1, (s_image->Height + 1) / 2);
                                if (targetW == s_image->Width && targetH == s_image->Height) {
                                    setNoticeText("Resize made no change");
                                    updateDisplayImage();
                                    break;
                                }
                                gui::ImagePtr resized = ImageViewer::ResizeCurrentImageNearestNeighbor(targetW, targetH);
                                if (!resized) {
                                    setNoticeText("Resize failed: unable to generate resized image");
                                    updateDisplayImage();
                                    break;
                                }
                                CaptureHistoryBeforeEdit();
                                (void)commitEditedImage(resized, "Resized to " + std::to_string(targetW) + "x" + std::to_string(targetH));
                                break;
                            }
                            case 18: {
                                if (!s_image) {
                                    setNoticeText("Load a PNG first");
                                    updateDisplayImage();
                                    break;
                                }
                                const int cropW = std::max(1, (s_image->Width + 1) / 2);
                                const int cropH = std::max(1, (s_image->Height + 1) / 2);
                                if (cropW == s_image->Width && cropH == s_image->Height) {
                                    setNoticeText("Crop made no change");
                                    updateDisplayImage();
                                    break;
                                }
                                const int cropX = std::max(0, (s_image->Width - cropW) / 2);
                                const int cropY = std::max(0, (s_image->Height - cropH) / 2);
                                gui::ImagePtr cropped = ImageViewer::CropCurrentImageRect(cropX, cropY, cropW, cropH);
                                if (!cropped) {
                                    setNoticeText("Crop failed: unable to generate cropped image");
                                    updateDisplayImage();
                                    break;
                                }
                                CaptureHistoryBeforeEdit();
                                (void)commitEditedImage(cropped, "Cropped to " + std::to_string(cropW) + "x" + std::to_string(cropH));
                                break;
                            }
                            default: break;
                            }
                        }
                    } catch (...) {
                    }
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    if (s_windowId != 0) {
        publishMessage(gui::MsgType::MT_Close, std::to_string(s_windowId));
    }

    Logger::write(LogLevel::Info, "ImageViewer exiting");
    return 0;
}

void ImageViewer::zoomIn() {
    if (!s_image) return;
    if (s_zoomMode == ZoomMode::FitToWindow) {
        s_zoomLevel = effectiveScaleForCurrentMode();
        s_zoomMode = ZoomMode::Custom;
    } else if (s_zoomMode == ZoomMode::ActualSize) {
        s_zoomMode = ZoomMode::Custom;
        s_zoomLevel = 1.0f;
    }

    s_zoomLevel *= 1.25f;
    clampZoomForCurrentMode();
    clampPanForCurrentImage();
    updateDisplayImage();
}

void ImageViewer::zoomOut() {
    if (!s_image) return;
    if (s_zoomMode == ZoomMode::FitToWindow) {
        s_zoomLevel = effectiveScaleForCurrentMode();
        s_zoomMode = ZoomMode::Custom;
    } else if (s_zoomMode == ZoomMode::ActualSize) {
        s_zoomMode = ZoomMode::Custom;
        s_zoomLevel = 1.0f;
    }

    s_zoomLevel /= 1.25f;
    clampZoomForCurrentMode();
    clampPanForCurrentImage();
    updateDisplayImage();
}

void ImageViewer::resetZoom() {
    s_zoomMode = ZoomMode::ActualSize;
    s_zoomLevel = 1.0f;
    s_panX = 0;
    s_panY = 0;
    clampZoomForCurrentMode();
    clampPanForCurrentImage();
    updateDisplayImage();
}

void ImageViewer::CaptureHistoryBeforeEdit() {
    if (!s_image) {
        return;
    }

    HistorySnapshot snapshot = captureSnapshot(s_image);
    if (snapshot.width <= 0 || snapshot.height <= 0 || snapshot.channels <= 0 || snapshot.pixels.empty()) {
        return;
    }

    if (s_undoStack.size() >= kHistoryLimit) {
        s_undoStack.erase(s_undoStack.begin());
    }
    s_undoStack.push_back(std::move(snapshot));
    s_redoStack.clear();
}

void ImageViewer::ClearEditHistory() {
    s_undoStack.clear();
    s_redoStack.clear();
}

bool ImageViewer::CanNavigateAwayFromDirtyDocument(const std::string& actionName) {
    if (!s_isDirty) {
        return true;
    }

    std::string message = "Unsaved changes: use Save As Copy, Undo, or discard before ";
    message += actionName.empty() ? "continuing" : actionName;
    setNoticeText(message);
    updateDisplayImage();
    return false;
}

bool ImageViewer::RestoreHistorySnapshot(const HistorySnapshot& snapshot) {
    gui::ImagePtr restored = imageFromSnapshot(snapshot);
    if (!restored) {
        return false;
    }

    s_image = restored;
    s_originalW = s_image->Width;
    s_originalH = s_image->Height;
    s_hasTransparency = detectTransparency(s_image);
    s_backgroundMode = s_hasTransparency ? BackgroundMode::Checkerboard : BackgroundMode::Solid;
    s_panX = 0;
    s_panY = 0;
    s_noticeText.clear();
    s_errorText.clear();

    const bool isOriginalState = s_hasOriginalSnapshot && snapshotEquals(snapshot, s_originalSnapshot);
    if (isOriginalState) {
        s_displayPath = s_originalPath.empty() ? s_filePath : s_originalPath;
        s_isDirty = false;
    } else {
        const std::string previewPath = makeEditedPreviewPath(s_originalPath.empty() ? s_filePath : s_originalPath, s_windowId);
        std::string error;
        if (writePngToVfs(previewPath, s_image, error)) {
            s_displayPath = previewPath;
            s_isDirty = true;
        } else {
            s_displayPath = s_originalPath.empty() ? s_filePath : s_originalPath;
            s_errorText = error;
            s_isDirty = true;
        }
    }

    clampZoomForCurrentMode();
    clampPanForCurrentImage();
    refreshWindowTitle();
    updateDisplayImage();
    return true;
}

void ImageViewer::UndoEdit() {
    if (s_undoStack.empty()) {
        return;
    }

    HistorySnapshot current = captureSnapshot(s_image);
    if (current.width > 0 && current.height > 0 && current.channels > 0 && !current.pixels.empty()) {
        if (s_redoStack.size() >= kHistoryLimit) {
            s_redoStack.erase(s_redoStack.begin());
        }
        s_redoStack.push_back(std::move(current));
    }

    HistorySnapshot snapshot = s_undoStack.back();
    s_undoStack.pop_back();
    (void)RestoreHistorySnapshot(snapshot);
}

void ImageViewer::RedoEdit() {
    if (s_redoStack.empty()) {
        return;
    }

    HistorySnapshot current = captureSnapshot(s_image);
    if (current.width > 0 && current.height > 0 && current.channels > 0 && !current.pixels.empty()) {
        if (s_undoStack.size() >= kHistoryLimit) {
            s_undoStack.erase(s_undoStack.begin());
        }
        s_undoStack.push_back(std::move(current));
    }

    HistorySnapshot snapshot = s_redoStack.back();
    s_redoStack.pop_back();
    (void)RestoreHistorySnapshot(snapshot);
}

void ImageViewer::DiscardChanges() {
    if (s_originalPath.empty()) {
        setNoticeText("Discard Changes unavailable: no original image is loaded");
        updateDisplayImage();
        return;
    }

    if (!loadImagePath(s_originalPath, true, true)) {
        setNoticeText("Discard Changes failed: unable to reload the original image");
        updateDisplayImage();
        return;
    }

    setNoticeText("Changes discarded");
    updateDisplayImage();
}

bool ImageViewer::commitEditedImage(const gui::ImagePtr& image, const std::string& notice) {
    if (!image || !image->isValid()) {
        return false;
    }

    s_image = image;
    s_originalW = s_image->Width;
    s_originalH = s_image->Height;
    s_hasTransparency = detectTransparency(s_image);
    s_backgroundMode = s_hasTransparency ? BackgroundMode::Checkerboard : BackgroundMode::Solid;
    s_panX = 0;
    s_panY = 0;

    const HistorySnapshot currentSnapshot = captureSnapshot(s_image);
    const bool matchesOriginal = s_hasOriginalSnapshot && snapshotEquals(currentSnapshot, s_originalSnapshot);
    if (matchesOriginal) {
        s_displayPath = s_originalPath.empty() ? s_filePath : s_originalPath;
        s_errorText.clear();
    } else {
        const std::string previewPath = makeEditedPreviewPath(s_originalPath.empty() ? s_filePath : s_originalPath, s_windowId);
        std::string error;
        if (writePngToVfs(previewPath, s_image, error)) {
            s_displayPath = previewPath;
            s_errorText.clear();
        } else {
            s_displayPath = s_originalPath.empty() ? s_filePath : s_originalPath;
            s_errorText = error;
        }
    }

    s_noticeText = notice;
    s_isDirty = !matchesOriginal;
    clampZoomForCurrentMode();
    clampPanForCurrentImage();
    refreshWindowTitle();
    updateDisplayImage();
    return true;
}

void ImageViewer::MarkModified() {
    s_isDirty = true;
    refreshWindowTitle();
}

void ImageViewer::UpdateModifiedTitleStatus() {
    refreshWindowTitle();
    if (s_image) {
        updateDisplayImage();
    } else {
        updateDisplay();
    }
}

void ImageViewer::RotateCurrentImageLeft() {
    if (!s_image) return;
    gui::ImagePtr rotated = rotateImageLeft(s_image);
    if (!rotated) return;
    CaptureHistoryBeforeEdit();
    (void)commitEditedImage(rotated, "Rotated left");
}

void ImageViewer::RotateCurrentImageRight() {
    if (!s_image) return;
    gui::ImagePtr rotated = rotateImageRight(s_image);
    if (!rotated) return;
    CaptureHistoryBeforeEdit();
    (void)commitEditedImage(rotated, "Rotated right");
}

void ImageViewer::FlipCurrentImageHorizontal() {
    if (!s_image) return;
    gui::ImagePtr flipped = flipImageHorizontal(s_image);
    if (!flipped) return;
    CaptureHistoryBeforeEdit();
    (void)commitEditedImage(flipped, "Flipped horizontally");
}

void ImageViewer::FlipCurrentImageVertical() {
    if (!s_image) return;
    gui::ImagePtr flipped = flipImageVertical(s_image);
    if (!flipped) return;
    CaptureHistoryBeforeEdit();
    (void)commitEditedImage(flipped, "Flipped vertically");
}

gui::ImagePtr ImageViewer::ResizeCurrentImageNearestNeighbor(int newWidth, int newHeight) {
    if (!s_image || !s_image->isValid() || s_image->Channels < 4) {
        return nullptr;
    }
    if (!clampImageDimensions(newWidth, newHeight)) {
        return nullptr;
    }

    gui::ImagePtr resized = std::make_shared<gui::Image>(newWidth, newHeight, s_image->Channels);
    if (!resized || !resized->isValid() || !resized->Pixels) {
        return nullptr;
    }

    const int channels = s_image->Channels;
    for (int y = 0; y < newHeight; ++y) {
        const int srcY = static_cast<int>((static_cast<uint64_t>(y) * static_cast<uint64_t>(s_image->Height)) / static_cast<uint64_t>(newHeight));
        const uint8_t* srcRow = s_image->Pixels + static_cast<size_t>(srcY) * static_cast<size_t>(s_image->Width) * static_cast<size_t>(channels);
        uint8_t* dstRow = resized->Pixels + static_cast<size_t>(y) * static_cast<size_t>(newWidth) * static_cast<size_t>(channels);
        for (int x = 0; x < newWidth; ++x) {
            const int srcX = static_cast<int>((static_cast<uint64_t>(x) * static_cast<uint64_t>(s_image->Width)) / static_cast<uint64_t>(newWidth));
            const uint8_t* src = srcRow + static_cast<size_t>(srcX) * static_cast<size_t>(channels);
            uint8_t* dst = dstRow + static_cast<size_t>(x) * static_cast<size_t>(channels);
            for (int c = 0; c < channels; ++c) {
                dst[c] = src[c];
            }
        }
    }

    return resized;
}

gui::ImagePtr ImageViewer::CropCurrentImageRect(int x, int y, int width, int height) {
    if (!s_image || !s_image->isValid() || s_image->Channels < 4) {
        return nullptr;
    }
    if (!clampCropRectToImage(s_image, x, y, width, height)) {
        return nullptr;
    }

    gui::ImagePtr cropped = std::make_shared<gui::Image>(width, height, s_image->Channels);
    if (!cropped || !cropped->isValid() || !cropped->Pixels) {
        return nullptr;
    }

    const int channels = s_image->Channels;
    const size_t srcStride = static_cast<size_t>(s_image->Width) * static_cast<size_t>(channels);
    const size_t dstStride = static_cast<size_t>(width) * static_cast<size_t>(channels);
    for (int row = 0; row < height; ++row) {
        const uint8_t* src = s_image->Pixels + (static_cast<size_t>(y + row) * srcStride) + (static_cast<size_t>(x) * static_cast<size_t>(channels));
        uint8_t* dst = cropped->Pixels + static_cast<size_t>(row) * dstStride;
        std::copy_n(src, static_cast<size_t>(width) * static_cast<size_t>(channels), dst);
    }

    return cropped;
}

void ImageViewer::SaveCurrentImageAsCopy() {
    if (!s_image) {
        setNoticeText("Load a PNG first");
        updateDisplayImage();
        return;
    }

    const std::string startPath = s_currentDirectory.empty() ? std::string("/") : s_currentDirectory;
    std::filesystem::path defaultNamePath(s_fileName.empty() ? "image.png" : s_fileName);
    std::string defaultFileName = defaultNamePath.stem().string();
    if (defaultFileName.empty()) {
        defaultFileName = "image";
    }
    defaultFileName += "-copy.png";

    dialogs::SaveDialog::Show(0, 0, startPath, defaultFileName, [](const std::string& path) {
        if (path.empty()) {
            return;
        }

        std::filesystem::path chosen(path);
        chosen.replace_extension(".png");
        const std::string finalPath = chosen.generic_string();
        if (ImageViewer::safeEqualsPath(finalPath, ImageViewer::s_originalPath)) {
            ImageViewer::setNoticeText("Save As Copy refused: cannot overwrite the original file");
            ImageViewer::updateDisplayImage();
            return;
        }

        std::string error;
        if (!writePngToVfs(finalPath, ImageViewer::s_image, error)) {
            ImageViewer::setNoticeText("Save As Copy failed: " + error);
            ImageViewer::updateDisplayImage();
            return;
        }

        // Save As Copy writes a duplicate and intentionally leaves the working image dirty
        // because the original source file was not overwritten.
        ImageViewer::setNoticeText("Saved copy to " + finalPath);
        ImageViewer::updateDisplayImage();
    });
}

void ImageViewer::updateDisplayImage() {
    Logger::write(LogLevel::Info, "ImageViewer mode=" + std::string(zoomModeName(s_zoomMode)) +
        " scale=" + std::to_string(static_cast<int>((effectiveScaleForCurrentMode() * 100.0f) + 0.5f)) + "%");
    updateDisplay();
}

void ImageViewer::handleMouseInput(int x, int y, int button, const std::string& action) {
    if (!s_image) return;

    if (button == 1 && action == "down") {
        s_leftMouseDown = true;
        s_dragPending = pointInsideCurrentImage(x, y);
        s_dragging = false;
        s_dragStartX = x;
        s_dragStartY = y;
        s_dragStartPanX = s_panX;
        s_dragStartPanY = s_panY;
        return;
    }

    if (button == 1 && action == "up") {
        s_leftMouseDown = false;
        s_dragPending = false;
        s_dragging = false;
        return;
    }

    if (button == 0 && action == "move") {
        if (s_leftMouseDown && s_dragPending) {
            if (std::abs(x - s_dragStartX) >= 3 || std::abs(y - s_dragStartY) >= 3) {
                s_dragging = true;
                s_dragPending = false;
            }
        }

        if (s_dragging) {
            s_panX = s_dragStartPanX + (x - s_dragStartX);
            s_panY = s_dragStartPanY + (y - s_dragStartY);
            clampZoomForCurrentMode();
            clampPanForCurrentImage();
            updateDisplayImage();
        }
    }
}

void ImageViewer::handleKeyPress(int keyCode) {
    if (keyCode == '+' || keyCode == '=') {
        zoomIn();
    } else if (keyCode == '-') {
        zoomOut();
    } else if (keyCode == '0' || keyCode == '1') {
        resetZoom();
    } else if (keyCode == 'f' || keyCode == 'F') {
        fitToWindow();
    } else if (keyCode == 37 || keyCode == 0x102) {
        previousImage();
    } else if (keyCode == 39 || keyCode == 0x103) {
        nextImage();
    }
    s_lastKeyCode = keyCode;
}

void ImageViewer::updateDisplay() {
    if (s_windowId == 0) return;
    const DesktopTheme& theme = GetCurrentDesktopTheme();
    const uint32_t bodyColor = ImageViewerBodyColor(theme);
    const uint32_t statusColor = ImageViewerStatusColor(theme);
    const uint32_t panelColor = ImageViewerPanelColor(theme);
    const uint32_t borderColor = ImageViewerPreviewBorderColor(theme);
    const uint32_t separatorColor = ImageViewerSeparatorColor(theme);
    const uint32_t textColor = ImageViewerTextColor(theme);
    const uint32_t mutedTextColor = ImageViewerMutedTextColor(theme);
    const uint32_t accentColor = ImageViewerAccentColor(theme);
    const bool rebuildChrome = !s_chromeWidgetsBuilt ||
        s_chromeWidgetsW != s_windowW ||
        s_chromeWidgetsH != s_windowH;

    int contentLeft = 0;
    int contentTop = 0;
    int contentWidth = 1;
    int contentHeight = 1;
    contentMetrics(contentLeft, contentTop, contentWidth, contentHeight);

    int drawX = contentLeft;
    int drawY = contentTop;
    int drawW = 1;
    int drawH = 1;
    imageMetrics(drawX, drawY, drawW, drawH, contentLeft, contentTop, contentWidth, contentHeight);

    publishMessage(gui::MsgType::MT_DrawText, std::to_string(s_windowId) + "|\f");
    publishWindowRectColor(s_windowId, 0, 0, s_windowW, s_windowH, bodyColor);
    publishWindowRectColor(s_windowId, 0, 31, s_windowW, 1, separatorColor);

    if (s_image) {
        if (s_backgroundMode == BackgroundMode::Checkerboard) {
            drawCheckerboardBackground(contentLeft, contentTop, contentWidth, contentHeight);
        }
        publishMessage(gui::MsgType::MT_DrawImage,
            gui::packDrawImage(s_windowId, drawX, drawY, drawW, drawH, s_displayPath.empty() ? s_filePath : s_displayPath));
    } else {
        const std::string message = s_statusText.empty() ? "No image loaded" : s_statusText;
        const int approxTextWidth = static_cast<int>(message.size()) * 7;
        const int centeredX = contentLeft + std::max(0, (contentWidth - approxTextWidth) / 2);
        const int centeredY = contentTop + std::max(0, contentHeight / 2);
        const uint32_t emptyColor = s_errorText.empty() ? mutedTextColor : accentColor;
        publishWindowTextColor(s_windowId, centeredX, centeredY, colorR(emptyColor), colorG(emptyColor), colorB(emptyColor), message);
    }

    const int footerTop = std::min(s_windowH, contentTop + contentHeight);
    const int statusStripHeight = 22;
    const int statusStripBottom = std::min(s_windowH, footerTop + statusStripHeight);
    if (statusStripBottom > footerTop) {
        publishWindowRectColor(s_windowId, 0, footerTop, s_windowW, statusStripBottom - footerTop, statusColor);
    }
    if (statusStripBottom < s_windowH) {
        publishWindowRectColor(s_windowId, 0, statusStripBottom, s_windowW, s_windowH - statusStripBottom, panelColor);
        publishWindowRectColor(s_windowId, 0, statusStripBottom, s_windowW, 1, separatorColor);
    }
    publishWindowFrameColor(s_windowId, std::max(0, contentLeft - 1), std::max(0, contentTop - 1), contentWidth + 2, contentHeight + 2, borderColor);

    const std::string info = statusText();
    const bool hasError = !s_errorText.empty();
    const bool hasNotice = !s_noticeText.empty();
    const uint32_t infoColor = hasError ? accentColor : ((hasNotice || s_image) ? textColor : mutedTextColor);
    publishWindowTextColor(s_windowId, 12, footerTop + 4, colorR(infoColor), colorG(infoColor), colorB(infoColor), info);

    if (rebuildChrome) {
        const int btnH = 24;
        const int gap = 8;
        const int row1Y = s_windowH - 54;
        const int row2Y = s_windowH - 26;

        auto addBtn = [&](int rowY, int id, const std::string& label, int& x) {
            int btnW = std::max(44, static_cast<int>(label.size()) * 7 + 18);
            if (rowY == row2Y && (label == "Save As Copy" || label == "Set as Wallpaper")) {
                btnW = std::min(btnW, 90);
            }
            publishMessage(gui::MsgType::MT_WidgetAdd, gui::packWidgetAdd(s_windowId, 1, id, x, rowY, btnW, btnH, label));
            x += btnW + gap;
        };

        int row1X = 12;
        addBtn(row1Y, 1, "Open", row1X);
        addBtn(row1Y, 2, "Previous", row1X);
        addBtn(row1Y, 3, "Next", row1X);
        addBtn(row1Y, 4, "Zoom In", row1X);
        addBtn(row1Y, 5, "Zoom Out", row1X);
        addBtn(row1Y, 6, "Fit to Window", row1X);
        addBtn(row1Y, 7, "100%", row1X);
        addBtn(row1Y, 14, "Undo", row1X);
        addBtn(row1Y, 15, "Redo", row1X);
        addBtn(row1Y, 17, "Resize", row1X);
        addBtn(row1Y, 18, "Crop", row1X);

        int row2X = 12;
        addBtn(row2Y, 8, "Rotate Left", row2X);
        addBtn(row2Y, 9, "Rotate Right", row2X);
        addBtn(row2Y, 10, "Flip Horizontal", row2X);
        addBtn(row2Y, 11, "Flip Vertical", row2X);
        addBtn(row2Y, 12, "Save As Copy", row2X);
        addBtn(row2Y, 13, "Set as Wallpaper", row2X);
        addBtn(row2Y, 16, "Discard Changes", row2X);

        s_chromeWidgetsBuilt = true;
        s_chromeWidgetsW = s_windowW;
        s_chromeWidgetsH = s_windowH;
    }
}

}} // namespace gxos::apps
