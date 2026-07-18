#pragma once

#include <cstdint>

#include "../../../../../runtime/synchronization/guidexos_mutex.h"

namespace guidexos {
namespace nativeaot {

struct CriticalSectionHandle;

// Inactive platform-adapter operations matching the Crst/CLRCriticalSection
// lifetime shape.  The selected mode is recursive because the matching
// NativeAOT source wraps the host's recursive critical-section primitive.
CriticalSectionHandle* initializeCriticalSection();
gxos::runtime::MutexResult enterCriticalSection(CriticalSectionHandle* handle);
gxos::runtime::MutexResult tryEnterCriticalSection(CriticalSectionHandle* handle);
gxos::runtime::MutexResult leaveCriticalSection(CriticalSectionHandle* handle);
gxos::runtime::MutexStatus deleteCriticalSection(CriticalSectionHandle* handle);

} // namespace nativeaot
} // namespace guidexos
