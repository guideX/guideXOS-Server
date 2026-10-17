#pragma once

#include <stddef.h>
#include <stdint.h>

#include "settings_presentation_core.h"

namespace gxos {
namespace apps {
namespace settings {
namespace native_layout {

enum : uint32_t {
    kMinimumWindowWidth = 660u,
    kMinimumWindowHeight = 440u,
    kWindowTitlebarHeight = 24u,
    kMinimumClientHeight = kMinimumWindowHeight - kWindowTitlebarHeight,
    kSidebarWidth = 182u,
    kCategoryTop = 44u,
    kCategoryHeight = 26u,
    kCategoryGap = 4u,
    kCardTop = 60u,
    kCardBottomMargin = 26u,
    kWrappedTextMaxLength = 192u,
    kWrappedTextMaxLines = 8u
};

struct Rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

struct PageLayout {
    uint32_t width;
    uint32_t height;
    Rect sidebar;
    Rect content;
    Rect title;
    Rect subtitle;
    Rect card;
    Rect footer;
    Rect categories[kCategoryCount];
};

struct PersonalizationLayout {
    Rect tabs[3];
    Rect gradientOptions[8];
    Rect wallpaperOptions[4];
    Rect iconOptions[3];
    Rect previousPage;
    Rect nextPage;
    Rect apply;
    Rect cancel;
    Rect status;
    Rect secondaryStatus;
};

struct TextLineSpan {
    uint16_t offset;
    uint16_t length;
};

bool calculatePageLayout(uint32_t width, uint32_t height, PageLayout* output);
void calculatePersonalizationLayout(const PageLayout& page,
                                    PersonalizationLayout* output);
bool isCategoryAvailable(CategoryId category);
CategoryId categoryAt(const PageLayout& page, int x, int y);
bool contains(const Rect& rect, int x, int y);
bool containedBy(const Rect& rect, const Rect& bounds);

// Returns the total number of rendered lines needed. It writes only up to
// capacity spans, and every span is bounded by maxCharacters.
size_t wrapText(const char* text, uint16_t maxCharacters,
                TextLineSpan* output, size_t capacity);

} // namespace native_layout
} // namespace settings
} // namespace apps
} // namespace gxos
