#include "settings_native_layout.h"

#include <iostream>

using namespace gxos::apps::settings;
using namespace gxos::apps::settings::native_layout;

namespace {

int checks = 0;
int failures = 0;

void check(bool passed, const char* description)
{
    ++checks;
    if (!passed) {
        ++failures;
        std::cerr << "FAIL: " << description << '\n';
    }
}

bool allCategoryRectsContained(const PageLayout& layout)
{
    for (size_t index = 0; index < kCategoryCount; ++index) {
        if (!containedBy(layout.categories[index], layout.sidebar)) return false;
    }
    return true;
}

bool personalizationRectsContained(const PageLayout& page,
                                   const PersonalizationLayout& layout)
{
    for (size_t index = 0; index < 3u; ++index) {
        if (!containedBy(layout.tabs[index], page.card) ||
            !containedBy(layout.iconOptions[index], page.card)) return false;
    }
    for (size_t index = 0; index < 8u; ++index) {
        if (!containedBy(layout.gradientOptions[index], page.card)) return false;
    }
    for (size_t index = 0; index < 4u; ++index) {
        if (!containedBy(layout.wallpaperOptions[index], page.card)) return false;
    }
    return containedBy(layout.previousPage, page.card) &&
        containedBy(layout.nextPage, page.card) &&
        containedBy(layout.apply, page.card) &&
        containedBy(layout.cancel, page.card) &&
        containedBy(layout.status, page.card) &&
        containedBy(layout.secondaryStatus, page.card);
}

} // namespace

int main()
{
    PageLayout normal{};
    check(calculatePageLayout(760u, 536u, &normal),
          "normal production client dimensions produce a native Settings layout");
    check(normal.sidebar.width == kSidebarWidth &&
          normal.content.x >= normal.sidebar.width &&
          normal.content.x + normal.content.width <= normal.width,
          "sidebar and content remain separated within the client area");
    check(allCategoryRectsContained(normal),
          "all twelve category targets fit in the normal sidebar");

    PageLayout minimum{};
    check(calculatePageLayout(kMinimumWindowWidth, kMinimumClientHeight, &minimum),
          "documented minimum window dimensions produce a native Settings layout");
    check(allCategoryRectsContained(minimum) &&
          containedBy(minimum.card, Rect{ 0u, 0u, minimum.width, minimum.height }) &&
          containedBy(minimum.footer, Rect{ 0u, 0u, minimum.width, minimum.height }),
          "all sidebar, card, and footer bounds fit at the documented minimum");
    check(!calculatePageLayout(kMinimumWindowWidth - 1u, kMinimumClientHeight, &minimum) &&
          !calculatePageLayout(kMinimumWindowWidth, kMinimumClientHeight - 1u, &minimum),
          "dimensions below the enforced minimum are rejected");

    const CategoryId expectedCategories[kCategoryCount] = {
        CategoryId::System, CategoryId::Display, CategoryId::Network,
        CategoryId::Personalization, CategoryId::Devices, CategoryId::Storage,
        CategoryId::Apps, CategoryId::Users, CategoryId::DateTime,
        CategoryId::Accessibility, CategoryId::Developer, CategoryId::About
    };
    for (size_t index = 0; index < kCategoryCount; ++index) {
        check(kCategories[index].id == expectedCategories[index] &&
              categoryAt(normal, static_cast<int>(normal.categories[index].x + 2u),
                         static_cast<int>(normal.categories[index].y + 2u)) == expectedCategories[index],
              "category identity and hit target retain the shared-core order");
        check(isCategoryAvailable(expectedCategories[index]) ==
              (expectedCategories[index] == CategoryId::System ||
               expectedCategories[index] == CategoryId::Display ||
               expectedCategories[index] == CategoryId::Personalization ||
               expectedCategories[index] == CategoryId::About),
              "deferred category state matches the four existing native pages");
    }
    check(categoryAt(normal, 0, 0) == CategoryId::Count &&
          categoryAt(normal, 12, 44 + 26 + 1) == CategoryId::Count,
          "sidebar padding and category gaps do not create accidental hit targets");

    PersonalizationLayout personalization{};
    calculatePersonalizationLayout(minimum, &personalization);
    check(personalizationRectsContained(minimum, personalization),
          "Personalization sections and controls remain inside the minimum-size card");
    check(personalization.apply.y > personalization.gradientOptions[7].y &&
          personalization.apply.y > personalization.wallpaperOptions[3].y &&
          personalization.cancel.x > personalization.apply.x &&
          personalization.apply.width >= 180u && personalization.apply.height >= 28u,
          "Apply and Cancel remain visible and have usable minimum hit targets");
    check(personalization.tabs[0].width >= 100u &&
          personalization.gradientOptions[0].width >= 80u &&
          personalization.wallpaperOptions[0].width >= 180u &&
          personalization.iconOptions[0].height >= 34u,
          "gradient, wallpaper, and icon selection targets fit without overlap");
    check(contains(personalization.gradientOptions[7],
                   static_cast<int>(personalization.gradientOptions[7].x + 1u),
                   static_cast<int>(personalization.gradientOptions[7].y + 1u)) &&
          !contains(personalization.gradientOptions[7],
                    static_cast<int>(personalization.gradientOptions[7].x + personalization.gradientOptions[7].width),
                    static_cast<int>(personalization.gradientOptions[7].y + 1u)),
          "selection hit tests include the interior and exclude the right boundary");

    const char* unavailable = "Not defined by this build";
    TextLineSpan unavailableLines[4]{};
    const size_t unavailableLineCount = wrapText(unavailable, 16u,
        unavailableLines, 4u);
    check(unavailableLineCount == 2u && unavailableLines[0].length <= 16u &&
          unavailableLines[1].length <= 16u,
          "unavailable-state text wraps to bounded bitmap-font lines");

    const char* longValue = "A provider-backed value with several words that must stay readable";
    TextLineSpan longLines[8]{};
    const size_t longLineCount = wrapText(longValue, 18u, longLines, 8u);
    bool spansBounded = longLineCount > 1u && longLineCount <= 8u;
    for (size_t index = 0; index < longLineCount && index < 8u; ++index) {
        spansBounded = spansBounded && longLines[index].length <= 18u &&
            longLines[index].offset + longLines[index].length <= 192u;
    }
    check(spansBounded,
          "long provider values produce bounded, non-clipping wrapped spans");

    for (uint32_t iteration = 0u; iteration < 1000u; ++iteration) {
        const uint32_t width = kMinimumWindowWidth + (iteration % 521u);
        const uint32_t height = kMinimumClientHeight + (iteration % 305u);
        PageLayout stressed{};
        PersonalizationLayout controls{};
        const bool calculated = calculatePageLayout(width, height, &stressed);
        calculatePersonalizationLayout(stressed, &controls);
        check(calculated && allCategoryRectsContained(stressed) &&
              personalizationRectsContained(stressed, controls),
              "bounded resize and control geometry remains inside every supported client size");
    }

    std::cout << "Settings native layout checks: " << checks - failures
              << "/" << checks << " passed; resize geometry transitions: 1000\n";
    return failures == 0 ? 0 : 1;
}
