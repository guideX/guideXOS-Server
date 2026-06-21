#include "desktop_theme.h"

#include <cctype>
#include <string>

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
        24,
        6,
        true,
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
        24,
        8,
        true,
        true,
        true,
        true
    };

    DesktopThemeId g_currentDesktopThemeId = DesktopThemeId::Classic;

    std::string normalizeThemeToken(const char* text)
    {
        std::string out;
        if (!text) {
            return out;
        }
        for (const char* p = text; *p; ++p) {
            const unsigned char ch = static_cast<unsigned char>(*p);
            if (std::isspace(ch) || ch == '-' || ch == '_') {
                continue;
            }
            out.push_back(static_cast<char>(std::tolower(ch)));
        }
        return out;
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
    const std::string token = normalizeThemeToken(text);
    if (token.empty() || token == "classic") {
        return true;
    }
    if (token == "scifi") {
        *out = DesktopThemeId::SciFi;
        return true;
    }

    return false;
}
