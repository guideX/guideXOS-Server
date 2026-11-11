#ifndef KERNEL_DISPLAY_OPTIONS_STATE_H
#define KERNEL_DISPLAY_OPTIONS_STATE_H

namespace kernel {
namespace apps {
namespace display_options {

enum class ConfigurationAvailability : unsigned char {
    UnsupportedPublicPreview,
    BackendUnavailable,
    Available
};

enum class ConfigurationAction : unsigned char {
    ChangeMode,
    SelectMonitor,
    ChangeResolution,
    Apply,
    Confirm,
    Close
};

inline constexpr ConfigurationAvailability resolveConfigurationAvailability(
    bool publicPreview,
    bool querySucceeded,
    bool hasOutput)
{
    return publicPreview
        ? ConfigurationAvailability::UnsupportedPublicPreview
        : (querySucceeded && hasOutput
            ? ConfigurationAvailability::Available
            : ConfigurationAvailability::BackendUnavailable);
}

inline constexpr bool shouldQueryConfigurationBackend(bool publicPreview)
{
    return !publicPreview;
}

inline constexpr bool isConfigurationActionEnabled(
    ConfigurationAvailability availability,
    ConfigurationAction action)
{
    return action == ConfigurationAction::Close ||
        availability == ConfigurationAvailability::Available;
}

inline constexpr const char* configurationUnavailableMessage(
    ConfigurationAvailability availability)
{
    return availability == ConfigurationAvailability::UnsupportedPublicPreview
        ? "Display configuration is not supported in the v0.3 public technical preview."
        : "Display configuration is unavailable because no supported backend is available.";
}

inline bool isCurrentAppearanceOption(const char* currentId,
                                      const char* optionId)
{
    if (!currentId || !optionId) return false;
    while (*currentId && *optionId) {
        if (*currentId != *optionId) return false;
        ++currentId;
        ++optionId;
    }
    return *currentId == '\0' && *optionId == '\0';
}

} // namespace display_options
} // namespace apps
} // namespace kernel

#endif
