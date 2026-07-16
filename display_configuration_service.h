#pragma once

#include "display_configuration_command.h"

namespace gxos {
namespace display {

// Public guest endpoint used by hosted and bare-metal Display Options, plus
// the QEMU-only proof coordinator.  The current app model invokes callbacks
// synchronously on the desktop owner path, so submit() drains the single
// bounded mutation slot at that safe point.  A later process-IPC adapter can
// preserve this exact contract and use processPendingAtSafePoint().
class DisplayConfigurationService {
public:
    static uint64_t nextRequestId();
    static bool submit(const DisplayConfigurationCommand& command,
                       DisplayConfigurationResponse& response);
    static void processPendingAtSafePoint();
    static DisplayConfigurationResponse lastResult();
    static bool isBusy();

    // Compact diagnostics for smoke harnesses and serial logs.
    static const char* resultCodeName(uint32_t resultCode);
};

} // namespace display
} // namespace gxos

