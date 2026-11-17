#pragma once

#include "source.h"

namespace gxos {
namespace javascript {

// Converts a decimal JSON token that has already passed the shared strict
// JSON grammar. The implementation is locale independent and accepts the
// existing 64 KiB JSON input bound.
bool parsePortableJsonBinary64(SourceView token, double& result);

} // namespace javascript
} // namespace gxos
