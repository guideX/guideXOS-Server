#pragma once

#include "app_activation.h"

#include <string>

namespace gxos { namespace apps {

enum class NavigatorUriRouteKind {
    Invalid = 0,
    NavigatorInternal,
    AppModelProtocol
};

inline NavigatorUriRouteKind ClassifyNavigatorUriRoute(const std::string& uri,
                                                        std::string& normalizedScheme)
{
    normalizedScheme.clear();
    if (!GetUriActivationScheme(uri, normalizedScheme)) return NavigatorUriRouteKind::Invalid;
    if (normalizedScheme == "http" || normalizedScheme == "https" ||
        normalizedScheme == "file" || normalizedScheme == "about") {
        return NavigatorUriRouteKind::NavigatorInternal;
    }
    return NavigatorUriRouteKind::AppModelProtocol;
}

}} // namespace gxos::apps
