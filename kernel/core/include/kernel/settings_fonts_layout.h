#pragma once

#include <stdint.h>

namespace kernel {
namespace apps {
namespace settings_fonts {

static const uint32_t kMaximumFontChoices = 4u;

struct Rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

struct Layout {
    uint32_t clientWidth;
    uint32_t clientHeight;
    uint32_t sidebarWidth;
    uint32_t fontChoiceCount;
    uint8_t compactFontRows;
    uint8_t reserved[3];
    Rect content;
    Rect navigation[4];
    Rect pageTitle;
    Rect pageDescription;
    Rect backButton;
    Rect personalizationFontsTile;
    Rect card;
    Rect currentSelection;
    Rect fontListLabel;
    Rect fontRows[kMaximumFontChoices];
    Rect selectedSource;
    Rect sizeLabel;
    Rect decreaseSize;
    Rect increaseSize;
    Rect sizeValue;
    Rect previewLabel;
    Rect preview;
    Rect previewClip;
    Rect status;
    Rect actions[3];
    Rect footer;
};

enum class FontControlHit : uint8_t {
    None = 0,
    FontChoice,
    DecreaseSize,
    IncreaseSize,
    Apply,
    Cancel,
    RestoreDefault,
    Back,
};

// Coordinates are relative to the Settings client area. The compositor's
// draw callback supplies the outer window width and the height below its
// titlebar.
bool CalculateLayout(uint32_t clientWidth, uint32_t clientHeight,
    uint32_t fontChoiceCount, Layout* output);
bool Contains(const Rect& rect, int32_t x, int32_t y);
int32_t HitTestNavigation(const Layout& layout, int32_t x, int32_t y);
FontControlHit HitTestFonts(const Layout& layout, int32_t x, int32_t y,
    bool sizeControlsVisible, uint32_t* outChoiceIndex);
uint32_t PreviewSampleStepPx(uint32_t pixelSize, bool bitmapFace);
uint32_t MaximumPreviewScrollPx(const Rect& preview, uint32_t sampleStepPx);

} // namespace settings_fonts
} // namespace apps
} // namespace kernel
