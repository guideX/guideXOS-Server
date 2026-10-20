#include "include/kernel/settings_fonts_layout.h"

namespace kernel {
namespace apps {
namespace settings_fonts {

namespace {

Rect makeRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    const Rect result = {x, y, width, height};
    return result;
}

uint32_t clampSub(uint32_t value, uint32_t amount, uint32_t minimum)
{
    return value > amount ? value - amount : minimum;
}

} // namespace

bool CalculateLayout(uint32_t clientWidth, uint32_t clientHeight,
    uint32_t fontChoiceCount, Layout* output)
{
    if (!output || clientWidth == 0u || clientHeight == 0u ||
        fontChoiceCount == 0u || fontChoiceCount > kMaximumFontChoices) return false;

    Layout layout = {};
    layout.clientWidth = clientWidth;
    layout.clientHeight = clientHeight;
    layout.fontChoiceCount = fontChoiceCount;
    layout.sidebarWidth = clientWidth < 500u ? clientWidth / 3u : 182u;

    uint32_t navigationWidth = clampSub(layout.sidebarWidth, 20u, layout.sidebarWidth);
    if (navigationWidth > 162u) navigationWidth = 162u;
    const uint32_t navigationY[4] = {60u, 106u, 152u, 198u};
    for (uint32_t i = 0u; i < 4u; ++i)
        layout.navigation[i] = makeRect(12u, navigationY[i], navigationWidth, 38u);

    layout.content = makeRect(layout.sidebarWidth + 24u, 0u,
        clientWidth > layout.sidebarWidth + 44u
            ? clientWidth - layout.sidebarWidth - 44u : 1u,
        clientHeight);
    layout.pageTitle = makeRect(layout.content.x, 18u, layout.content.width, 22u);
    layout.pageDescription = makeRect(layout.content.x, 42u, layout.content.width, 18u);
    // Preserve the original 88 x 24 input target and make its visible frame
    // use that same rectangle.
    layout.backButton = makeRect(layout.content.x + 12u, 68u, 88u, 24u);
    layout.personalizationFontsTile = makeRect(layout.content.x + 16u, 146u,
        layout.content.width > 32u ? layout.content.width - 32u : 1u, 54u);
    layout.card = makeRect(layout.content.x, 96u, layout.content.width,
        clientHeight > 140u ? clientHeight - 122u : 1u);

    const uint32_t left = layout.card.x + 12u;
    const uint32_t availableWidth = layout.content.width > 28u
        ? layout.content.width - 28u : 1u;
    layout.currentSelection = makeRect(left, layout.card.y + 14u,
        availableWidth, 16u);
    layout.fontListLabel = makeRect(left, layout.card.y + 30u,
        availableWidth, 12u);
    layout.compactFontRows = layout.card.height < 240u && fontChoiceCount > 2u;
    const uint32_t rowY = layout.card.y + 42u;
    const uint32_t rowHeight = 16u;
    if (layout.compactFontRows) {
        const uint32_t columnGap = 8u;
        const uint32_t columnWidth = availableWidth > columnGap
            ? (availableWidth - columnGap) / 2u : 1u;
        for (uint32_t i = 0u; i < fontChoiceCount; ++i) {
            const uint32_t column = i % 2u;
            const uint32_t row = i / 2u;
            layout.fontRows[i] = makeRect(left + column * (columnWidth + columnGap),
                rowY + row * 20u, columnWidth, rowHeight);
        }
    } else {
        for (uint32_t i = 0u; i < fontChoiceCount; ++i)
            layout.fontRows[i] = makeRect(left, rowY + i * rowHeight,
                availableWidth, rowHeight);
    }

    uint32_t rowsBottom = rowY;
    for (uint32_t i = 0u; i < fontChoiceCount; ++i) {
        const uint32_t bottom = layout.fontRows[i].y + layout.fontRows[i].height;
        if (bottom > rowsBottom) rowsBottom = bottom;
    }
    const uint32_t sourceY = layout.compactFontRows
        ? rowsBottom + 2u : layout.card.y + 108u;
    const uint32_t sizeY = layout.compactFontRows
        ? sourceY + 18u : layout.card.y + 126u;
    const uint32_t previewLabelY = layout.compactFontRows
        ? sizeY + 24u : layout.card.y + 150u;
    const uint32_t previewY = previewLabelY + 12u;
    layout.selectedSource = makeRect(left, sourceY, availableWidth, 16u);
    layout.sizeLabel = makeRect(left, sizeY, 44u, 22u);
    layout.decreaseSize = makeRect(left + 52u, sizeY, 24u, 22u);
    layout.increaseSize = makeRect(left + 82u, sizeY, 24u, 22u);
    layout.sizeValue = makeRect(left + 52u, sizeY,
        availableWidth > 52u ? availableWidth - 52u : 1u, 22u);
    layout.previewLabel = makeRect(left, previewLabelY, availableWidth, 12u);

    const uint32_t cardBottom = layout.card.y + layout.card.height;
    const uint32_t statusY = cardBottom > 43u ? cardBottom - 43u : layout.card.y;
    const uint32_t previewBottom = statusY > 4u ? statusY - 4u : statusY;
    layout.preview = makeRect(left, previewY, availableWidth,
        previewBottom > previewY ? previewBottom - previewY : 0u);
    layout.previewClip = makeRect(left + 2u, previewY + 2u,
        availableWidth > 4u ? availableWidth - 4u : 0u,
        layout.preview.height > 4u ? layout.preview.height - 4u : 0u);
    layout.status = makeRect(left, statusY, availableWidth, 15u);

    const uint32_t actionY = cardBottom > 26u ? cardBottom - 26u : cardBottom;
    const uint32_t actionX[3] = {left, left + 66u, left + 132u};
    const uint32_t actionWidth[3] = {60u, 60u, 102u};
    for (uint32_t i = 0u; i < 3u; ++i)
        layout.actions[i] = makeRect(actionX[i], actionY, actionWidth[i], 22u);
    layout.footer = makeRect(layout.content.x + 12u,
        clientHeight > 25u ? clientHeight - 25u : 0u,
        layout.content.width > 24u ? layout.content.width - 24u : 1u, 18u);

    *output = layout;
    return true;
}

bool Contains(const Rect& rect, int32_t x, int32_t y)
{
    if (x < 0 || y < 0) return false;
    const uint64_t right = static_cast<uint64_t>(rect.x) + rect.width;
    const uint64_t bottom = static_cast<uint64_t>(rect.y) + rect.height;
    return static_cast<uint64_t>(x) >= rect.x && static_cast<uint64_t>(x) < right &&
        static_cast<uint64_t>(y) >= rect.y && static_cast<uint64_t>(y) < bottom;
}

int32_t HitTestNavigation(const Layout& layout, int32_t x, int32_t y)
{
    for (uint32_t i = 0u; i < 4u; ++i)
        if (Contains(layout.navigation[i], x, y)) return static_cast<int32_t>(i);
    return -1;
}

FontControlHit HitTestFonts(const Layout& layout, int32_t x, int32_t y,
    bool sizeControlsVisible, uint32_t* outChoiceIndex)
{
    if (outChoiceIndex) *outChoiceIndex = 0xffffffffu;
    for (uint32_t i = 0u; i < layout.fontChoiceCount; ++i) {
        if (Contains(layout.fontRows[i], x, y)) {
            if (outChoiceIndex) *outChoiceIndex = i;
            return FontControlHit::FontChoice;
        }
    }
    if (sizeControlsVisible && Contains(layout.decreaseSize, x, y))
        return FontControlHit::DecreaseSize;
    if (sizeControlsVisible && Contains(layout.increaseSize, x, y))
        return FontControlHit::IncreaseSize;
    if (Contains(layout.actions[0], x, y)) return FontControlHit::Apply;
    if (Contains(layout.actions[1], x, y)) return FontControlHit::Cancel;
    if (Contains(layout.actions[2], x, y)) return FontControlHit::RestoreDefault;
    if (Contains(layout.backButton, x, y)) return FontControlHit::Back;
    return FontControlHit::None;
}

uint32_t PreviewSampleStepPx(uint32_t pixelSize, bool bitmapFace)
{
    return bitmapFace ? 10u : (pixelSize * 5u) / 4u + 3u;
}

uint32_t MaximumPreviewScrollPx(const Rect& preview, uint32_t sampleStepPx)
{
    const uint64_t contentHeight = static_cast<uint64_t>(sampleStepPx) * 3u;
    if (contentHeight <= preview.height) return 0u;
    const uint64_t maximum = contentHeight - preview.height;
    return maximum > 0xffffffffu ? 0xffffffffu : static_cast<uint32_t>(maximum);
}

} // namespace settings_fonts
} // namespace apps
} // namespace kernel
