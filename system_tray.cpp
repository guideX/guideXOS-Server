#include "system_tray.h"

namespace gxos { namespace gui {
    int SystemTray::s_netAnimPhase = 0;
    uint64_t SystemTray::s_lastAnimTick = 0;
    bool SystemTray::s_netActive = false;
    uint64_t SystemTray::s_netActivityEnd = 0;
}} // namespace gxos::gui
