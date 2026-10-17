#pragma once

#include "include/kernel/system_font_selection.h"

namespace gxos {
namespace gui {
namespace internal {

// Desktop-only bridge. Applications receive names and stable identity values,
// never font-source pointers or manager handles. The selection generation is
// returned when the service is initialized even if bitmap is active and the
// function returns false because there is no scalable render handle.
bool systemFontSelectionGetRenderState(FontHandle* outHandle,
	FontSize* outSize, uint32_t* outSelectionGeneration);

} // namespace internal
} // namespace gui
} // namespace gxos
