#include "guidexos_native_thread.h"

#if !defined(GXOS_BARE_METAL)

#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <thread>

namespace gxos {
namespace runtime {
namespace {
    constexpr gxos_thread_uint32 kMaximumSlots = 64;

    enum class ObjectState {
        Unused,
        Created,
        Running,
        Exited,
        Joined,
        Detached,
        Reclaimed
    };

    struct ThreadSlot {
        gxos_thread_uint32 generation = 1;
        bool allocated = false;
        bool exitSignaled = false;
        bool workerFinished = false;
        bool joinConsumed = false;
        bool detached = false;
        ObjectState state = ObjectState::Unused;
        NativeThreadEntry entry = nullptr;
        void* context = nullptr;
        gxos_thread_uintptr exitResult = 0;
        const char* debugName = nullptr;
        std::unique_ptr<Event> completion;
        std::thread worker;
    };

    std::mutex g_mutex;
    std::array<ThreadSlot, kMaximumSlots> g_slots{};
    std::atomic<gxos_thread_uint32> g_liveSlots{0};
    thread_local ThreadHandle g_currentHandle{};

    bool validStackSize(gxos_thread_size value) {
        return value >= kNativeThreadMinimumStackSize &&
               value <= kNativeThreadMaximumStackSize &&
               (value % 16u) == 0;
    }

    ThreadSlot* lookupLocked(ThreadHandle handle) {
        if (!handle.isValid() || handle.slot >= kMaximumSlots) {
            return nullptr;
        }
        ThreadSlot& slot = g_slots[handle.slot];
        if (!slot.allocated || slot.generation != handle.generation) {
            return nullptr;
        }
        return &slot;
    }

    void reclaimLocked(ThreadSlot& slot) {
        if (!slot.allocated || !slot.workerFinished ||
            slot.state == ObjectState::Running ||
            slot.state == ObjectState::Created || slot.worker.joinable()) {
            return;
        }

        slot.completion.reset();
        slot.allocated = false;
        slot.exitSignaled = false;
        slot.workerFinished = false;
        slot.joinConsumed = false;
        slot.detached = false;
        slot.state = ObjectState::Reclaimed;
        slot.entry = nullptr;
        slot.context = nullptr;
        slot.exitResult = 0;
        slot.debugName = nullptr;
        if (slot.generation != std::numeric_limits<gxos_thread_uint32>::max()) {
            ++slot.generation;
        }
        g_liveSlots.fetch_sub(1, std::memory_order_relaxed);
    }

    void workerMain(ThreadSlot* slot, ThreadHandle handle) {
        g_currentHandle = handle;
        const gxos_thread_uintptr result = slot->entry(slot->context);

        std::unique_lock<std::mutex> lock(g_mutex);
        // The slot cannot be reused before this worker exits: join and detach
        // both retain the slot until the worker has published this result.
        if (!slot->allocated || slot->generation != handle.generation) {
            return;
        }
        slot->exitResult = result;
        slot->state = ObjectState::Exited;
        if (!slot->exitSignaled) {
            slot->exitSignaled = true;
            Event* completion = slot->completion.get();
            lock.unlock();
            (void)completion->signal();
            lock.lock();
        }

        slot->workerFinished = true;
        if (slot->detached) {
            // This worker is no longer using its slot after this function
            // returns.  The std::thread object was detached by detachThread.
            if (slot->worker.joinable()) {
                // The create(detached) path can race a very short worker
                // before the creator reaches std::thread::detach().  The
                // worker may safely detach its own host object while holding
                // the slot lock; the object is then no longer joinable before
                // the slot is reclaimed.
                slot->worker.detach();
            }
            reclaimLocked(*slot);
        }
    }

    ThreadResult createHosted(NativeThreadEntry entry,
                              void* context,
                              const ThreadCreateOptions& options,
                              ThreadHandle* result) {
        if (result == nullptr || entry == nullptr) {
            return ThreadResult::InvalidArgument;
        }
        *result = ThreadHandle{};
        if (!validStackSize(options.stackSize)) {
            return ThreadResult::InvalidStackSize;
        }

        std::unique_lock<std::mutex> lock(g_mutex);
        ThreadSlot* slot = nullptr;
        gxos_thread_uint32 slotIndex = 0;
        for (; slotIndex < kMaximumSlots; ++slotIndex) {
            ThreadSlot& candidate = g_slots[slotIndex];
            if (!candidate.allocated &&
                candidate.generation != std::numeric_limits<gxos_thread_uint32>::max()) {
                slot = &candidate;
                break;
            }
        }
        if (slot == nullptr) {
            return ThreadResult::NoResources;
        }

        slot->allocated = true;
        slot->exitSignaled = false;
        slot->workerFinished = false;
        slot->joinConsumed = false;
        slot->detached = options.detached;
        slot->state = ObjectState::Running;
        slot->entry = entry;
        slot->context = context;
        slot->exitResult = 0;
        slot->debugName = options.debugName;
        slot->completion.reset(new (std::nothrow) Event(EventMode::ManualReset, false));
        if (!slot->completion || !slot->completion->isInitialized()) {
            slot->completion.reset();
            slot->allocated = false;
            slot->state = ObjectState::Reclaimed;
            return ThreadResult::NoResources;
        }

        const ThreadHandle handle{ slotIndex, slot->generation };
        g_liveSlots.fetch_add(1, std::memory_order_relaxed);
        try {
            slot->worker = std::thread(workerMain, slot, handle);
        }
        catch (...) {
            slot->completion.reset();
            slot->allocated = false;
            slot->state = ObjectState::Reclaimed;
            g_liveSlots.fetch_sub(1, std::memory_order_relaxed);
            return ThreadResult::NoResources;
        }
        *result = handle;

        if (options.detached) {
            slot->worker.detach();
        }
        return ThreadResult::Ok;
    }

    WaitResult joinHosted(ThreadHandle handle,
                          const WaitTimeout& timeout,
                          gxos_thread_uintptr* exitResult) {
        if (exitResult != nullptr) {
            *exitResult = 0;
        }
        if (handle.slot == g_currentHandle.slot &&
            handle.generation == g_currentHandle.generation) {
            return WaitResult::Invalid;
        }

        Event* completion = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            ThreadSlot* slot = lookupLocked(handle);
            if (slot == nullptr || slot->detached || slot->joinConsumed) {
                return WaitResult::Invalid;
            }
            completion = slot->completion.get();
        }

        const WaitResult waitResult = completion->wait(timeout);
        if (waitResult != WaitResult::Signaled) {
            return waitResult;
        }

        std::thread* worker = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            ThreadSlot* slot = lookupLocked(handle);
            if (slot == nullptr || slot->detached || slot->joinConsumed ||
                slot->state != ObjectState::Exited) {
                return WaitResult::Invalid;
            }
            worker = &slot->worker;
        }
        if (worker->joinable()) {
            worker->join();
        }

        std::lock_guard<std::mutex> lock(g_mutex);
        ThreadSlot* slot = lookupLocked(handle);
        if (slot == nullptr || slot->detached || slot->joinConsumed) {
            return WaitResult::Invalid;
        }
        if (exitResult != nullptr) {
            *exitResult = slot->exitResult;
        }
        slot->joinConsumed = true;
        slot->state = ObjectState::Joined;
        reclaimLocked(*slot);
        return WaitResult::Signaled;
    }

    ThreadResult detachHosted(ThreadHandle handle) {
        std::lock_guard<std::mutex> lock(g_mutex);
        ThreadSlot* slot = lookupLocked(handle);
        if (slot == nullptr) {
            return ThreadResult::InvalidHandle;
        }
        if (slot->detached) {
            return ThreadResult::AlreadyDetached;
        }
        if (slot->joinConsumed) {
            return ThreadResult::AlreadyJoined;
        }

        const bool exited = slot->state == ObjectState::Exited;
        slot->detached = true;
        if (exited) {
            slot->state = ObjectState::Detached;
        }
        if (slot->worker.joinable()) {
            slot->worker.detach();
        }
        if (exited && slot->workerFinished) {
            reclaimLocked(*slot);
        }
        return ThreadResult::Ok;
    }
}

ThreadResult createThread(NativeThreadEntry entry,
                          void* context,
                          const ThreadCreateOptions& options,
                          ThreadHandle* result) {
    return createHosted(entry, context, options, result);
}

WaitResult joinThread(ThreadHandle thread,
                      const WaitTimeout& timeout,
                      gxos_thread_uintptr* exitResult) {
    return joinHosted(thread, timeout, exitResult);
}

ThreadResult detachThread(ThreadHandle thread) {
    return detachHosted(thread);
}

extern "C" gxos_thread_uint32 gxos_native_thread_live_count_for_test() {
    return g_liveSlots.load(std::memory_order_relaxed);
}

} // namespace runtime
} // namespace gxos

#endif // !GXOS_BARE_METAL
