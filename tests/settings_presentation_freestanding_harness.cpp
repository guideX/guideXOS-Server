#include "settings_presentation_adapter.h"

extern "C" int settings_presentation_freestanding_probe()
{
    gxos::apps::settings::SettingsPresentationCore presentation;
    if (!presentation.navigate({ gxos::apps::settings::CategoryId::Display,
                                 gxos::apps::settings::TargetId::Resolution })) return 1;
    if (!presentation.setFocusedControl(gxos::apps::settings::FocusControl::DisplayResolution)) return 2;

    const gxos::apps::settings::CapabilitySnapshot snapshot = {
        gxos::apps::settings::CapabilityId::Display,
        gxos::apps::settings::CapabilityAvailability::Writable,
        7
    };
    if (!presentation.applyCapabilitySnapshot(snapshot)) return 3;

    gxos::apps::settings::OperationRequest request{};
    if (!presentation.beginOperation(gxos::apps::settings::CapabilityId::Display, 1, request)) return 4;
    if (!presentation.completeOperation({ request.requestId,
            gxos::apps::settings::OperationCompletion::Success })) return 5;
    return presentation.searchResults().count == 0 ? 0 : 6;
}
