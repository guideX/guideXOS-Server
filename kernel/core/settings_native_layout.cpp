#include "settings_native_layout.h"

namespace gxos {
namespace apps {
namespace settings {
namespace native_layout {

namespace {

uint32_t minimum(uint32_t left, uint32_t right)
{
    return left < right ? left : right;
}

uint32_t optionWidth(uint32_t gridWidth, uint32_t columns, uint32_t gap)
{
    const uint32_t gaps = columns > 0u ? (columns - 1u) * gap : 0u;
    return gridWidth > gaps ? (gridWidth - gaps) / columns : 1u;
}

bool isSpace(char value)
{
    return value == ' ' || value == '\t' || value == '\n' || value == '\r';
}

} // namespace

bool calculatePageLayout(uint32_t width, uint32_t height, PageLayout* output)
{
    if (!output || width < kMinimumWindowWidth || height < kMinimumClientHeight) {
        return false;
    }

    PageLayout layout{};
    layout.width = width;
    layout.height = height;
    layout.sidebar = Rect{ 0u, 0u, kSidebarWidth, height };

    const uint32_t contentX = kSidebarWidth + 24u;
    const uint32_t rightMargin = 24u;
    if (width <= contentX + rightMargin) return false;
    const uint32_t contentWidth = width - contentX - rightMargin;
    layout.content = Rect{ contentX, 0u, contentWidth, height };
    layout.title = Rect{ contentX, 16u, contentWidth, 10u };
    layout.subtitle = Rect{ contentX, 34u, contentWidth, 9u };
    layout.card = Rect{ contentX, kCardTop, contentWidth,
                        height - kCardTop - kCardBottomMargin };
    layout.footer = Rect{ contentX, height - 16u, contentWidth, 8u };

    const uint32_t categoryWidth = kSidebarWidth - 24u;
    const uint32_t categoryStride = kCategoryHeight + kCategoryGap;
    for (size_t index = 0; index < kCategoryCount; ++index) {
        layout.categories[index] = Rect{
            12u,
            kCategoryTop + static_cast<uint32_t>(index) * categoryStride,
            categoryWidth,
            kCategoryHeight
        };
    }

    const Rect client{ 0u, 0u, width, height };
    if (!containedBy(layout.sidebar, client) ||
        !containedBy(layout.content, client) ||
        !containedBy(layout.title, client) ||
        !containedBy(layout.subtitle, client) ||
        !containedBy(layout.card, client) ||
        !containedBy(layout.footer, client)) {
        return false;
    }
    for (size_t index = 0; index < kCategoryCount; ++index) {
        if (!containedBy(layout.categories[index], layout.sidebar)) return false;
    }

    *output = layout;
    return true;
}

void calculatePersonalizationLayout(const PageLayout& page,
                                    PersonalizationLayout* output)
{
    if (!output) return;
    PersonalizationLayout layout{};

    const uint32_t innerX = page.card.x + 16u;
    const uint32_t innerWidth = page.card.width > 32u ? page.card.width - 32u : 1u;
    const uint32_t tabGap = 5u;
    const uint32_t tabWidth = optionWidth(innerWidth, 3u, tabGap);
    const uint32_t tabY = page.card.y + 132u;
    static const uint32_t tabHeights[3] = { 26u, 26u, 26u };
    for (uint32_t index = 0u; index < 3u; ++index) {
        layout.tabs[index] = Rect{
            innerX + index * (tabWidth + tabGap), tabY,
            tabWidth, tabHeights[index]
        };
    }

    const uint32_t gridY = page.card.y + 164u;
    const uint32_t cellGap = 6u;
    const uint32_t gradientCellWidth = optionWidth(innerWidth, 4u, cellGap);
    for (uint32_t index = 0u; index < 8u; ++index) {
        layout.gradientOptions[index] = Rect{
            innerX + (index % 4u) * (gradientCellWidth + cellGap),
            gridY + (index / 4u) * 36u,
            gradientCellWidth,
            32u
        };
    }

    const uint32_t wallpaperCellWidth = optionWidth(innerWidth, 2u, cellGap);
    for (uint32_t index = 0u; index < 4u; ++index) {
        layout.wallpaperOptions[index] = Rect{
            innerX + (index % 2u) * (wallpaperCellWidth + cellGap),
            gridY + (index / 2u) * 36u,
            wallpaperCellWidth,
            32u
        };
    }

    const uint32_t iconWidth = optionWidth(innerWidth, 3u, cellGap);
    for (uint32_t index = 0u; index < 3u; ++index) {
        layout.iconOptions[index] = Rect{
            innerX + index * (iconWidth + cellGap), gridY + 31u,
            iconWidth, 36u
        };
    }

    const uint32_t pageY = page.card.y + 239u;
    layout.previousPage = Rect{ innerX, pageY, 80u, 22u };
    layout.nextPage = Rect{ innerX + innerWidth - minimum(innerWidth, 80u),
                            pageY, minimum(innerWidth, 80u), 22u };

    const uint32_t buttonGap = 8u;
    const uint32_t buttonWidth = innerWidth > buttonGap
        ? (innerWidth - buttonGap) / 2u : 1u;
    const uint32_t buttonY = page.card.y + 270u;
    layout.apply = Rect{ innerX, buttonY, buttonWidth, 30u };
    layout.cancel = Rect{ innerX + buttonWidth + buttonGap, buttonY,
                          buttonWidth, 30u };
    layout.status = Rect{ innerX, page.card.y + 306u, innerWidth, 8u };
    layout.secondaryStatus = Rect{ innerX, page.card.y + 316u, innerWidth, 8u };

    *output = layout;
}

bool isCategoryAvailable(CategoryId category)
{
    return category == CategoryId::System ||
        category == CategoryId::Display ||
        category == CategoryId::Personalization ||
        category == CategoryId::About;
}

CategoryId categoryAt(const PageLayout& page, int x, int y)
{
    for (size_t index = 0; index < kCategoryCount; ++index) {
        if (contains(page.categories[index], x, y)) {
            return static_cast<CategoryId>(index);
        }
    }
    return CategoryId::Count;
}

bool contains(const Rect& rect, int x, int y)
{
    if (x < 0 || y < 0 || rect.width == 0u || rect.height == 0u) return false;
    const uint32_t ux = static_cast<uint32_t>(x);
    const uint32_t uy = static_cast<uint32_t>(y);
    return ux >= rect.x && uy >= rect.y &&
        ux - rect.x < rect.width && uy - rect.y < rect.height;
}

bool containedBy(const Rect& rect, const Rect& bounds)
{
    if (rect.width == 0u || rect.height == 0u ||
        rect.width > bounds.width || rect.height > bounds.height) return false;
    return rect.x >= bounds.x && rect.y >= bounds.y &&
        rect.x - bounds.x <= bounds.width - rect.width &&
        rect.y - bounds.y <= bounds.height - rect.height;
}

size_t wrapText(const char* text, uint16_t maxCharacters,
                TextLineSpan* output, size_t capacity)
{
    if (!text || maxCharacters == 0u) return 0u;

    size_t length = 0u;
    while (length <= kWrappedTextMaxLength && text[length] != '\0') ++length;
    if (length == 0u) return 0u;
    if (length > kWrappedTextMaxLength) return capacity + 1u;

    size_t position = 0u;
    size_t lineCount = 0u;
    while (position < length) {
        while (position < length && isSpace(text[position])) ++position;
        if (position >= length) break;

        size_t end = position + maxCharacters;
        if (end > length) end = length;
        if (end < length) {
            size_t breakAt = end;
            while (breakAt > position && !isSpace(text[breakAt - 1u])) --breakAt;
            if (breakAt > position) end = breakAt - 1u;
        }
        if (end <= position) end = position + 1u;

        size_t trimmedEnd = end;
        while (trimmedEnd > position && isSpace(text[trimmedEnd - 1u])) --trimmedEnd;
        if (trimmedEnd == position) {
            ++position;
            continue;
        }
        if (output && lineCount < capacity) {
            output[lineCount].offset = static_cast<uint16_t>(position);
            output[lineCount].length = static_cast<uint16_t>(trimmedEnd - position);
        }
        ++lineCount;
        position = trimmedEnd;
        while (position < length && isSpace(text[position])) ++position;
    }
    return lineCount;
}

} // namespace native_layout
} // namespace settings
} // namespace apps
} // namespace gxos
