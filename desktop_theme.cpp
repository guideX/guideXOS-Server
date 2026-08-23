#include "desktop_theme.h"

namespace {
    const DesktopTheme kClassicTheme{
        DesktopThemeId::Classic,
        "Classic",
        0xFF142850u,
        0xFF222222u,
        0xFF333333u,
        0xFF111111u,
        0xFFF0F0F0u,
        0xFF5588AAu,
        0xFF3A3F4Au,
        0xFF1E1E26u,
        0xFF3C4350u,
        6,
        8,
        4,
        24,
        1,
        10,
        6,
        8,
        8,
        false,
        false,
        false,
        false
    };

    const DesktopTheme kSciFiTheme{
        DesktopThemeId::SciFi,
        "Sci Fi",
        0xFF0B1020u,
        0xFF11192Bu,
        0xFF2D4666u,
        0xFF0E1524u,
        0xFFE6F0FFu,
        0xFF6C79FFu,
        0xFF44506Fu,
        0xFF0A0F1Bu,
        0xFF33507Au,
        8,
        10,
        5,
        28,
        2,
        12,
        8,
        10,
        10,
        true,
        true,
        true,
        true
    };

    DesktopThemeId g_currentDesktopThemeId = DesktopThemeId::Classic;

    bool themeTokenMatches(const char* text, const char* expected)
    {
        if (!text || !expected) {
            return false;
        }

        const char* expectedCursor = expected;
        for (const char* cursor = text; *cursor; ++cursor) {
            const unsigned char ch = static_cast<unsigned char>(*cursor);
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' ||
                ch == '\f' || ch == '\v' || ch == '-' || ch == '_') {
                continue;
            }

            char normalized = static_cast<char>(ch);
            if (normalized >= 'A' && normalized <= 'Z') {
                normalized = static_cast<char>(normalized - 'A' + 'a');
            }
            if (!*expectedCursor || normalized != *expectedCursor) {
                return false;
            }
            ++expectedCursor;
        }

        return *expectedCursor == '\0';
    }
}

DesktopThemeId GetCurrentDesktopThemeId()
{
    return g_currentDesktopThemeId;
}

const DesktopTheme& GetCurrentDesktopTheme()
{
    return GetDesktopTheme(g_currentDesktopThemeId);
}

const DesktopTheme& GetDesktopTheme(DesktopThemeId id)
{
    switch (id) {
    case DesktopThemeId::SciFi:
        return kSciFiTheme;
    case DesktopThemeId::Classic:
    default:
        return kClassicTheme;
    }
}

bool SetCurrentDesktopTheme(DesktopThemeId id)
{
    if (g_currentDesktopThemeId == id) {
        return false;
    }
    g_currentDesktopThemeId = id;
    return true;
}

const char* DesktopThemeIdToString(DesktopThemeId id)
{
    switch (id) {
    case DesktopThemeId::SciFi:
        return "scifi";
    case DesktopThemeId::Classic:
    default:
        return "classic";
    }
}

bool TryParseDesktopThemeId(const char* text, DesktopThemeId* out)
{
    if (!out) {
        return false;
    }

    *out = DesktopThemeId::Classic;
    if (!text || themeTokenMatches(text, "") || themeTokenMatches(text, "classic")) {
        return true;
    }
    if (themeTokenMatches(text, "scifi")) {
        *out = DesktopThemeId::SciFi;
        return true;
    }

    return false;
}
