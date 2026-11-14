#include "include/kernel/identity_runtime.h"

namespace kernel {
namespace identity {

StorageAdapter createVfsIdentityStorageAdapter();

Result initializeRuntime()
{
    const StorageAdapter storage = createVfsIdentityStorageAdapter();
    return runtime().initialize(&storage);
}

} // namespace identity
} // namespace kernel
