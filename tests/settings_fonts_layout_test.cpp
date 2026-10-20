#include "kernel/core/include/kernel/font_contract.h"
#include "kernel/core/include/kernel/font_truetype.h"
#include "kernel/core/include/kernel/kernel_compositor.h"
#include "kernel/core/include/kernel/settings_fonts_layout.h"
#include "kernel/core/include/kernel/vfs.h"
#include "kernel/core/include/kernel/window_geometry.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

using namespace gxos::gui;
using kernel::apps::settings_fonts::FontControlHit;
using kernel::apps::settings_fonts::Layout;
using kernel::apps::settings_fonts::Rect;

int checks = 0;
int failures = 0;

#define CHECK(expression) do { \
    ++checks; \
    if (!(expression)) { \
        std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expression); \
        ++failures; \
    } \
} while (0)

std::vector<uint8_t> readFont(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {});
}

bool rectInside(const Rect& inner, const Rect& outer)
{
    return inner.x >= outer.x && inner.y >= outer.y &&
        static_cast<uint64_t>(inner.x) + inner.width <=
            static_cast<uint64_t>(outer.x) + outer.width &&
        static_cast<uint64_t>(inner.y) + inner.height <=
            static_cast<uint64_t>(outer.y) + outer.height;
}

void checkGeometry(uint32_t outerWidth, uint32_t outerHeight,
    uint32_t expectedPreviewMinimum)
{
    CHECK(outerHeight > static_cast<uint32_t>(kernel::compositor::TITLEBAR_HEIGHT));
    const uint32_t clientHeight = outerHeight -
        static_cast<uint32_t>(kernel::compositor::TITLEBAR_HEIGHT);
    Layout layout = {};
    CHECK(kernel::apps::settings_fonts::CalculateLayout(outerWidth, clientHeight, 4u, &layout));
    CHECK(layout.clientWidth == outerWidth && layout.clientHeight == clientHeight);
    CHECK(layout.sidebarWidth == 182u);
    CHECK(layout.content.x + layout.content.width <= outerWidth);
    CHECK(layout.card.x == layout.content.x && layout.card.width == layout.content.width);
    CHECK(layout.card.y + layout.card.height + 26u == clientHeight);
    CHECK(layout.preview.height >= expectedPreviewMinimum);
    CHECK(rectInside(layout.preview, layout.card));
    CHECK(rectInside(layout.previewClip, layout.preview));
    CHECK(rectInside(layout.status, layout.card));
    for (uint32_t i = 0u; i < 4u; ++i) {
        CHECK(layout.navigation[i].x + layout.navigation[i].width <= layout.sidebarWidth);
        CHECK(layout.navigation[i].y + layout.navigation[i].height <= clientHeight);
        const int32_t navIndex = kernel::apps::settings_fonts::HitTestNavigation(
            layout, static_cast<int32_t>(layout.navigation[i].x + layout.navigation[i].width - 1u),
            static_cast<int32_t>(layout.navigation[i].y + layout.navigation[i].height - 1u));
        CHECK(navIndex == static_cast<int32_t>(i));
        CHECK(kernel::apps::settings_fonts::HitTestNavigation(layout,
            static_cast<int32_t>(layout.navigation[i].x + layout.navigation[i].width),
            static_cast<int32_t>(layout.navigation[i].y)) == -1);
    }
    for (uint32_t i = 0u; i < layout.fontChoiceCount; ++i) {
        uint32_t choice = 0xffffffffu;
        const Rect& row = layout.fontRows[i];
        CHECK(rectInside(row, layout.card));
        CHECK(kernel::apps::settings_fonts::HitTestFonts(layout,
            static_cast<int32_t>(row.x), static_cast<int32_t>(row.y), true,
            &choice) == FontControlHit::FontChoice);
        CHECK(choice == i);
        CHECK(kernel::apps::settings_fonts::HitTestFonts(layout,
            static_cast<int32_t>(row.x + row.width - 1u),
            static_cast<int32_t>(row.y + row.height - 1u), true,
            &choice) == FontControlHit::FontChoice && choice == i);
        CHECK(kernel::apps::settings_fonts::HitTestFonts(layout,
            static_cast<int32_t>(row.x + row.width), static_cast<int32_t>(row.y),
            true, &choice) != FontControlHit::FontChoice);
    }

    CHECK(kernel::apps::settings_fonts::HitTestFonts(layout,
        static_cast<int32_t>(layout.decreaseSize.x),
        static_cast<int32_t>(layout.decreaseSize.y), true, nullptr) ==
            FontControlHit::DecreaseSize);
    CHECK(kernel::apps::settings_fonts::HitTestFonts(layout,
        static_cast<int32_t>(layout.increaseSize.x + layout.increaseSize.width - 1u),
        static_cast<int32_t>(layout.increaseSize.y + layout.increaseSize.height - 1u),
        true, nullptr) == FontControlHit::IncreaseSize);
    CHECK(kernel::apps::settings_fonts::HitTestFonts(layout,
        static_cast<int32_t>(layout.decreaseSize.x),
        static_cast<int32_t>(layout.decreaseSize.y), false, nullptr) ==
            FontControlHit::None);
    const FontControlHit actions[3] = {
        FontControlHit::Apply, FontControlHit::Cancel, FontControlHit::RestoreDefault
    };
    for (uint32_t i = 0u; i < 3u; ++i) {
        const Rect& button = layout.actions[i];
        CHECK(rectInside(button, layout.card));
        CHECK(kernel::apps::settings_fonts::HitTestFonts(layout,
            static_cast<int32_t>(button.x), static_cast<int32_t>(button.y), true,
            nullptr) == actions[i]);
        CHECK(kernel::apps::settings_fonts::HitTestFonts(layout,
            static_cast<int32_t>(button.x + button.width - 1u),
            static_cast<int32_t>(button.y + button.height - 1u), true,
            nullptr) == actions[i]);
    }
    CHECK(kernel::apps::settings_fonts::HitTestFonts(layout,
        static_cast<int32_t>(layout.backButton.x),
        static_cast<int32_t>(layout.backButton.y), true, nullptr) == FontControlHit::Back);
    CHECK(kernel::apps::settings_fonts::Contains(layout.preview,
        static_cast<int32_t>(layout.preview.x), static_cast<int32_t>(layout.preview.y)));
    CHECK(!kernel::apps::settings_fonts::Contains(layout.preview,
        static_cast<int32_t>(layout.preview.x + layout.preview.width),
        static_cast<int32_t>(layout.preview.y)));
    CHECK(layout.actions[0].y == layout.actions[1].y &&
        layout.actions[1].y == layout.actions[2].y);
}

void checkRealFont(const char* path, uint64_t fontId)
{
    const std::vector<uint8_t> bytes = readFont(path);
    CHECK(!bytes.empty());
    if (bytes.empty()) return;

    FontHandle handle = {0xffffu, 0u, 0u};
    FontTrueTypeMetadata metadata = {};
    CHECK(FontTrueTypeLoadMemory({fontId}, bytes.data(), bytes.size(),
        &handle, &metadata) == FontStatus::Ok);
    if (handle.slot == 0xffffu) return;
    const uint32_t generationAfterLoad = FontManagerGeneration();

    Layout minimum = {};
    CHECK(kernel::apps::settings_fonts::CalculateLayout(560u,
        360u - kernel::compositor::TITLEBAR_HEIGHT, 4u, &minimum));
    const uint32_t sizes[4] = {8u, 18u, 24u, 48u};
    const char previewText[] = "The quick brown fox jumps over the lazy dog.";
    const char utf8Text[] = "Caf\xc3\xa9 \xe2\x80\x94 Roboto";
    const char missingGlyph[] = "\xe2\x98\x83";
    const uint32_t surfaceWidth = 384u;
    const uint32_t surfaceHeight = 96u;
    const size_t surfacePixels = static_cast<size_t>(surfaceWidth) * surfaceHeight;
    const uint32_t background = 0xff202020u;
    const uint32_t guardA = 0x13579bdfu;
    const uint32_t guardB = 0x2468ace0u;
    std::vector<uint32_t> surface(surfacePixels + 2u, background);
    surface[surfacePixels] = guardA;
    surface[surfacePixels + 1u] = guardB;

    for (uint32_t i = 0u; i < 4u; ++i) {
        const FontSize size = {sizes[i]};
        FontMetrics metrics = {};
        TextMeasureResult measured = {};
        CHECK(FontGetMetrics(handle, size, &metrics) == FontStatus::Ok);
        CHECK(metrics.pixelSize == sizes[i] && metrics.lineHeightPx > 0u &&
            metrics.ascentPx + metrics.descentPx <= metrics.lineHeightPx);
        if (sizes[i] == 18u || sizes[i] == 24u)
            CHECK(metrics.lineHeightPx <= minimum.previewClip.height);
        if (sizes[i] == 48u)
            CHECK(metrics.lineHeightPx > minimum.previewClip.height);
        CHECK(FontMeasureText(handle, size, previewText, sizeof(previewText) - 1u,
            &measured) == FontStatus::Ok);
        CHECK(measured.glyphCount == sizeof(previewText) - 1u && measured.pixelWidth > 0u);
        CHECK(FontMeasureText(handle, size, utf8Text, sizeof(utf8Text) - 1u,
            &measured) == FontStatus::Ok);
        CHECK(measured.fallbackGlyphCount == 0u && measured.pixelWidth > 0u);
        CHECK(FontMeasureText(handle, size, missingGlyph, sizeof(missingGlyph) - 1u,
            &measured) == FontStatus::FallbackUsed);
        CHECK(measured.fallbackGlyphCount == 1u);

        std::fill(surface.begin(), surface.begin() + surfacePixels, background);
        const uint32_t clipX = 16u;
        const uint32_t clipY = 18u;
        const uint32_t clipWidth = minimum.previewClip.width;
        const uint32_t clipHeight = minimum.previewClip.height;
        uint32_t* clipPixels = &surface[static_cast<size_t>(clipY) * surfaceWidth + clipX];
        const size_t clipBytes = static_cast<size_t>(clipHeight - 1u) *
            surfaceWidth * sizeof(uint32_t) + clipWidth * sizeof(uint32_t);
        const int32_t baseline = static_cast<int32_t>(metrics.ascentPx) - 2;
        const FontStatus drawStatus = FontDrawGrayscaleTextToBuffer(handle, size,
            clipPixels, clipBytes, surfaceWidth * sizeof(uint32_t), clipWidth,
            clipHeight, 4, baseline, previewText, sizeof(previewText) - 1u,
            0xfff0f0f0u, nullptr);
        CHECK(drawStatus == FontStatus::Ok);

        uint32_t changedPixels = 0u;
        for (uint32_t y = 0u; y < surfaceHeight; ++y) {
            for (uint32_t x = 0u; x < surfaceWidth; ++x) {
                const bool inside = x >= clipX && x < clipX + clipWidth &&
                    y >= clipY && y < clipY + clipHeight;
                if (surface[static_cast<size_t>(y) * surfaceWidth + x] != background) {
                    if (inside) ++changedPixels;
                    else CHECK(false);
                }
            }
        }
        CHECK(changedPixels > 0u);
        CHECK(surface[surfacePixels] == guardA && surface[surfacePixels + 1u] == guardB);

        FontGlyphCacheStatistics cold = {};
        FontGlyphCacheStatistics warm = {};
        CHECK(FontGlyphCacheGetStatistics(&cold) == FontStatus::Ok);
        CHECK(FontDrawGrayscaleTextToBuffer(handle, size, clipPixels, clipBytes,
            surfaceWidth * sizeof(uint32_t), clipWidth, clipHeight, 4, baseline,
            previewText, sizeof(previewText) - 1u, 0xfff0f0f0u, nullptr) == FontStatus::Ok);
        CHECK(FontGlyphCacheGetStatistics(&warm) == FontStatus::Ok);
        CHECK(warm.hits > cold.hits && warm.rasterizations == cold.rasterizations);
        CHECK(warm.entryCount <= warm.maxEntries && warm.bytesUsed <= warm.maxPixelBytes);
    }

    FontMetrics largeMetrics = {};
    CHECK(FontGetMetrics(handle, {48u}, &largeMetrics) == FontStatus::Ok);
    CHECK(largeMetrics.lineHeightPx > minimum.currentSelection.height);
    CHECK(FontManagerGeneration() == generationAfterLoad);
    CHECK(FontManagerUnloadFont(handle) == FontStatus::Ok);
}

} // namespace

int main()
{
    CHECK(sizeof(Layout) <= 512u);
    CHECK(kernel::app::WindowDimensionAfterResize(760, 720, 1000, 560, 200) == 560);
    CHECK(kernel::app::WindowDimensionAfterResize(500, 300, 500, 360, 100) == 360);
    CHECK(kernel::app::WindowDimensionAfterResize(760, 801, 1000, 560, 200) == 561);
    CHECK(kernel::app::WindowDimensionAfterResize(500, 701, 700, 360, 100) == 501);
    CHECK(kernel::app::ClampWindowDimensionToMinimum(480, 560, 200) == 560);
    CHECK(kernel::app::WindowDimensionAfterResize(2147483642, 2147483647,
        -2147483647, 560, 200) == 2147483647);

    checkGeometry(560u, 360u, 32u);
    checkGeometry(561u, 361u, 32u);
    checkGeometry(640u, 400u, 40u);
    checkGeometry(760u, 500u, 140u);

    Layout clampedLayout = {};
    const int32_t clampedWidth = kernel::app::WindowDimensionAfterResize(
        760, 720, 1000, 560, 200);
    const int32_t clampedHeight = kernel::app::WindowDimensionAfterResize(
        500, 300, 500, 360, 100);
    CHECK(clampedWidth == 560 && clampedHeight == 360);
    CHECK(kernel::apps::settings_fonts::CalculateLayout(
        static_cast<uint32_t>(clampedWidth),
        static_cast<uint32_t>(clampedHeight - kernel::compositor::TITLEBAR_HEIGHT),
        4u, &clampedLayout));
    CHECK(clampedLayout.preview.height == 33u);

    CHECK(!kernel::apps::settings_fonts::CalculateLayout(0u, 336u, 4u, &clampedLayout));
    CHECK(!kernel::apps::settings_fonts::CalculateLayout(560u, 336u, 0u, &clampedLayout));
    CHECK(!kernel::apps::settings_fonts::CalculateLayout(560u, 336u, 5u, &clampedLayout));
    CHECK(kernel::apps::settings_fonts::PreviewSampleStepPx(8u, false) == 13u);
    CHECK(kernel::apps::settings_fonts::PreviewSampleStepPx(18u, false) == 25u);
    CHECK(kernel::apps::settings_fonts::PreviewSampleStepPx(24u, false) == 33u);
    CHECK(kernel::apps::settings_fonts::PreviewSampleStepPx(48u, false) == 63u);
    CHECK(kernel::apps::settings_fonts::PreviewSampleStepPx(7u, true) == 10u);
    CHECK(kernel::apps::settings_fonts::MaximumPreviewScrollPx(
        clampedLayout.preview, 63u) == 156u);
    CHECK(kernel::apps::settings_fonts::MaximumPreviewScrollPx(
        clampedLayout.preview, 13u) == 6u);

    CHECK(FontManagerInitialize() == FontStatus::Ok);
    checkRealFont("assets/Fonts/Roboto-v2.138-package/Roboto-v2.138-Regular.ttf",
        0x4631345245470001ull);
    checkRealFont("assets/Fonts/Roboto-v2.138-package/Roboto-v2.138-Bold.ttf",
        0x463134424f4c4401ull);
    checkRealFont("assets/Fonts/Roboto-v2.138-package/Roboto-v2.138-Italic.ttf",
        0x4631344954414c01ull);
    FontManagerShutdown();
    CHECK(!FontManagerIsInitialized());

    std::printf("FONT14 Settings layout and package typography: %d checks, %d failures, %zu-byte layout\n",
        checks, failures, sizeof(Layout));
    return failures == 0 ? 0 : 1;
}

namespace kernel {
namespace vfs {

Status stat(const char*, FileInfo*)
{
    return VFS_ERR_NOT_FOUND;
}

int32_t read_file(const char*, void*, uint32_t)
{
    return VFS_ERR_NOT_FOUND;
}

} // namespace vfs
} // namespace kernel
