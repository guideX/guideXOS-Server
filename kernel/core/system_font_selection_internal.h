#pragma once

#include "include/kernel/system_font_selection.h"

namespace gxos {
namespace gui {
namespace internal {

// Desktop-only bridge. Applications receive names and stable identity values,
// never font-source pointers or manager handles.
bool systemFontSelectionGetRenderState(FontHandle* outHandle,
	FontSize* outSize, uint32_t* outSelectionGeneration);

} // namespace internal
} // namespace gui
} // namespace gxos
