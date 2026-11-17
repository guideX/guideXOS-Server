#pragma once

#include "json_number.h"

namespace gxos {
namespace javascript {
// Compatibility adapter retained for hosted callers. Both the hosted runtime
// and freestanding parser now call the same decimal-to-binary64 core.
inline bool parseHostedJsonBinary64(SourceView input, double& result)
{
    return parsePortableJsonBinary64(input, result);
}

} // namespace javascript
} // namespace gxos
