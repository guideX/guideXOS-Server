#include "guidexos_nativeaot_critical_section_adapter.h"

#include <new>

namespace guidexos {
namespace nativeaot {

struct CriticalSectionHandle {
    CriticalSectionHandle()
        : mutex(gxos::runtime::MutexMode::Recursive) {
    }

    gxos::runtime::Mutex mutex;
};

CriticalSectionHandle* initializeCriticalSection() {
    CriticalSectionHandle* handle = new (std::nothrow) CriticalSectionHandle();
    if (handle == nullptr || !handle->mutex.isInitialized()) {
        delete handle;
        return nullptr;
    }
    return handle;
}

gxos::runtime::MutexResult enterCriticalSection(CriticalSectionHandle* handle) {
    return handle == nullptr
        ? gxos::runtime::MutexResult::Invalid
        : handle->mutex.lock();
}

gxos::runtime::MutexResult tryEnterCriticalSection(CriticalSectionHandle* handle) {
    return handle == nullptr
        ? gxos::runtime::MutexResult::Invalid
        : handle->mutex.tryLock();
}

gxos::runtime::MutexResult leaveCriticalSection(CriticalSectionHandle* handle) {
    return handle == nullptr
        ? gxos::runtime::MutexResult::Invalid
        : handle->mutex.unlock();
}

gxos::runtime::MutexStatus deleteCriticalSection(CriticalSectionHandle* handle) {
    if (handle == nullptr) {
        return gxos::runtime::MutexStatus::Invalid;
    }
    const gxos::runtime::MutexStatus status = handle->mutex.destroy();
    if (status != gxos::runtime::MutexStatus::Ok) {
        return status;
    }
    delete handle;
    return gxos::runtime::MutexStatus::Ok;
}

} // namespace nativeaot
} // namespace guidexos
