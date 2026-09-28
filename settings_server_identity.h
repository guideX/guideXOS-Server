#pragma once

namespace gxos {
namespace identity {

// The repository has no declared guideXOS Server release version. Keep the
// Settings identity honest until release tooling supplies one shared value.
static constexpr const char* kGuideXosServerProduct = "guideXOS Server";
static constexpr const char* kGuideXosServerVersion = "Development build";
static constexpr const char* kGuideXosServerVersionNote = "No release version is declared";
static constexpr const char* kGuideXosServerBuildDate = __DATE__;
static constexpr const char* kGuideXosServerBuildTime = __TIME__;

} // namespace identity
} // namespace gxos
