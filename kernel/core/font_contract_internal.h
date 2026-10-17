#pragma once

#include "include/kernel/font_contract.h"

namespace gxos {
namespace gui {
namespace internal {

// Only the system selection service may publish a scalable TrueType face as
// the manager default. General consumers keep the public selection guard.
FontStatus fontManagerSetSystemDefaultFont(FontHandle handle);

} // namespace internal
} // namespace gui
} // namespace gxos
