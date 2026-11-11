#include "kernel/display_options_state.h"

#include <cstdio>
#include <string>

namespace {

int checks = 0;
int failures = 0;

void check(bool condition, const char* description)
{
    ++checks;
    if (condition) {
        std::printf("PASS: %s\n", description);
    } else {
        ++failures;
        std::printf("FAIL: %s\n", description);
    }
}

void checkConfigurationActionsDisabled(
    kernel::apps::display_options::ConfigurationAvailability availability,
    const char* label)
{
    using namespace kernel::apps::display_options;
    const bool disabled =
        !isConfigurationActionEnabled(availability, ConfigurationAction::ChangeMode) &&
        !isConfigurationActionEnabled(availability, ConfigurationAction::SelectMonitor) &&
        !isConfigurationActionEnabled(availability, ConfigurationAction::ChangeResolution) &&
        !isConfigurationActionEnabled(availability, ConfigurationAction::Apply) &&
        !isConfigurationActionEnabled(availability, ConfigurationAction::Confirm);
    check(disabled, label);
    check(isConfigurationActionEnabled(availability, ConfigurationAction::Close),
          "Close remains available while configuration is unsupported");
}

} // namespace

int main()
{
    using namespace kernel::apps::display_options;

    const ConfigurationAvailability preview =
        resolveConfigurationAvailability(true, true, true);
    check(preview == ConfigurationAvailability::UnsupportedPublicPreview,
          "public-preview policy remains unsupported even if a backend reports success");
    check(!shouldQueryConfigurationBackend(true),
          "public-preview Display does not probe an unqualified backend");
    check(std::string(configurationUnavailableMessage(preview)).find(
              "v0.3 public technical preview") != std::string::npos,
          "public-preview state names the release scope explicitly");
    checkConfigurationActionsDisabled(preview,
          "public-preview topology, monitor, resolution, Apply, and OK are disabled");

    const ConfigurationAvailability missingBackend =
        resolveConfigurationAvailability(false, false, false);
    check(missingBackend == ConfigurationAvailability::BackendUnavailable,
          "failed query with no registered backend resolves to unavailable");
    check(std::string(configurationUnavailableMessage(missingBackend)).find(
              "no supported backend") != std::string::npos,
          "missing backend state explains why configuration is unavailable");
    checkConfigurationActionsDisabled(missingBackend,
          "missing backend cannot activate configuration controls");

    const ConfigurationAvailability emptyInventory =
        resolveConfigurationAvailability(false, true, false);
    check(emptyInventory == ConfigurationAvailability::BackendUnavailable,
          "successful query without an output does not enable configuration");
    checkConfigurationActionsDisabled(emptyInventory,
          "empty display inventory cannot activate configuration controls");

    const ConfigurationAvailability supported =
        resolveConfigurationAvailability(false, true, true);
    check(supported == ConfigurationAvailability::Available,
          "developer configuration remains available after a successful backend query");
    check(isConfigurationActionEnabled(supported, ConfigurationAction::ChangeMode) &&
              isConfigurationActionEnabled(supported, ConfigurationAction::SelectMonitor) &&
              isConfigurationActionEnabled(supported, ConfigurationAction::ChangeResolution) &&
              isConfigurationActionEnabled(supported, ConfigurationAction::Apply) &&
              isConfigurationActionEnabled(supported, ConfigurationAction::Confirm),
          "supported developer backend retains existing configuration actions");

    bool repeatOpenStable = true;
    for (int cycle = 0; cycle < 8; ++cycle) {
        const ConfigurationAvailability opened =
            resolveConfigurationAvailability(true, false, false);
        repeatOpenStable = repeatOpenStable &&
            opened == ConfigurationAvailability::UnsupportedPublicPreview &&
            !isConfigurationActionEnabled(opened, ConfigurationAction::Apply) &&
            isConfigurationActionEnabled(opened, ConfigurationAction::Close);
    }
    check(repeatOpenStable,
          "reopening Display repeatedly preserves the unsupported state and safe close action");

    std::printf("Display Options preview policy tests: %d/%d passed\n",
                checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
