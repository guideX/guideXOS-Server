#include "guidexos_scheduler_wait.h"

namespace gxos {
namespace runtime {
namespace scheduler_wait {

namespace {
    SchedulerWaitHooks g_hooks = {};
    WaitNode* g_timer_head = nullptr;

    bool deadlineBefore(gxos_wait_uint64 left, gxos_wait_uint64 right) {
        return static_cast<gxos_wait_uint64>(left - right) >
            static_cast<gxos_wait_uint64>(0x7FFFFFFFFFFFFFFFULL);
    }

    bool deadlineReached(gxos_wait_uint64 now, gxos_wait_uint64 deadline) {
        return !deadlineBefore(now, deadline);
    }

    void unlinkQueue(WaitNode* node) {
        if (node == nullptr || !node->queue_linked || node->owner_queue == nullptr) {
            return;
        }

        WaitQueue* queue = node->owner_queue;
        if (node->queue_previous != nullptr) {
            node->queue_previous->queue_next = node->queue_next;
        }
        else {
            queue->head = node->queue_next;
        }
        if (node->queue_next != nullptr) {
            node->queue_next->queue_previous = node->queue_previous;
        }
        else {
            queue->tail = node->queue_previous;
        }
        if (queue->count != 0) {
            --queue->count;
        }

        node->owner_queue = nullptr;
        node->queue_previous = nullptr;
        node->queue_next = nullptr;
        node->queue_linked = false;
    }

    void unlinkTimer(WaitNode* node) {
        if (node == nullptr || !node->timer_linked) {
            return;
        }

        if (node->timer_previous != nullptr) {
            node->timer_previous->timer_next = node->timer_next;
        }
        else {
            g_timer_head = node->timer_next;
        }
        if (node->timer_next != nullptr) {
            node->timer_next->timer_previous = node->timer_previous;
        }

        node->timer_previous = nullptr;
        node->timer_next = nullptr;
        node->timer_linked = false;
    }

    void insertTimer(WaitNode* node) {
        WaitNode* current = g_timer_head;
        WaitNode* previous = nullptr;
        while (current != nullptr && !deadlineBefore(node->deadline, current->deadline)) {
            previous = current;
            current = current->timer_next;
        }

        node->timer_previous = previous;
        node->timer_next = current;
        node->timer_linked = true;
        if (previous != nullptr) {
            previous->timer_next = node;
        }
        else {
            g_timer_head = node;
        }
        if (current != nullptr) {
            current->timer_previous = node;
        }
    }

    bool complete(WaitNode* node, WakeReason reason, bool makeRunnable) {
        if (node == nullptr || node->state != WaitNodeState::Waiting ||
            reason == WakeReason::None) {
            return false;
        }

        unlinkQueue(node);
        unlinkTimer(node);
        node->reason = reason;
        node->state = WaitNodeState::Completed;

        if (makeRunnable && g_hooks.makeRunnable != nullptr) {
            g_hooks.makeRunnable(g_hooks.context, node);
        }
        return true;
    }

    WakeReason takeCompleted(WaitNode* node) {
        if (node == nullptr || node->state != WaitNodeState::Completed) {
            return WakeReason::Interrupted;
        }
        const WakeReason reason = node->reason;
        // The thread-owned node is reusable after its consumer has copied the
        // one final completion reason.  owner_thread and generation remain so
        // stale timer/wake paths cannot be confused with a later wait.
        node->owner_queue = nullptr;
        node->queue_previous = nullptr;
        node->queue_next = nullptr;
        node->timer_previous = nullptr;
        node->timer_next = nullptr;
        node->deadline = 0;
        node->reason = WakeReason::None;
        node->state = WaitNodeState::Idle;
        node->timed = false;
        node->queue_linked = false;
        node->timer_linked = false;
        return reason;
    }
}

void initializeWaitQueue(WaitQueue* queue) {
    if (queue == nullptr) {
        return;
    }
    queue->head = nullptr;
    queue->tail = nullptr;
    queue->count = 0;
}

void installSchedulerWaitHooks(const SchedulerWaitHooks* hooks) {
    g_hooks = hooks == nullptr ? SchedulerWaitHooks{} : *hooks;
    g_timer_head = nullptr;
}

bool schedulerWaitAvailable() {
    return g_hooks.enterCritical != nullptr &&
           g_hooks.leaveCritical != nullptr &&
           g_hooks.currentNode != nullptr &&
           g_hooks.nowTicks != nullptr &&
           g_hooks.durationToTicks != nullptr &&
           g_hooks.park != nullptr &&
           g_hooks.makeRunnable != nullptr;
}

bool schedulerWaitCriticalAvailable() {
    return g_hooks.enterCritical != nullptr &&
           g_hooks.leaveCritical != nullptr;
}

void* enterCritical() {
    return g_hooks.enterCritical == nullptr
        ? nullptr
        : g_hooks.enterCritical(g_hooks.context);
}

void leaveCritical(void* token) {
    if (g_hooks.leaveCritical != nullptr) {
        g_hooks.leaveCritical(g_hooks.context, token);
    }
}

bool prepareWait(WaitQueue* queue, const WaitDuration& duration, WaitNode** nodeOut) {
    if (queue == nullptr || nodeOut == nullptr ||
        g_hooks.currentNode == nullptr || g_hooks.nowTicks == nullptr ||
        g_hooks.durationToTicks == nullptr) {
        return false;
    }

    WaitNode* node = g_hooks.currentNode(g_hooks.context);
    if (node == nullptr || node->state != WaitNodeState::Idle ||
        node->queue_linked || node->timer_linked ||
        queue->count == 0xFFFFFFFFU) {
        return false;
    }

    gxos_wait_uint64 ticks = 0;
    gxos_wait_uint64 now = 0;
    if (!duration.infinite) {
        bool valid = false;
        ticks = g_hooks.durationToTicks(g_hooks.context, duration.nanoseconds, &valid);
        if (!valid || ticks == 0) {
            return false;
        }
        now = g_hooks.nowTicks(g_hooks.context);
    }

    gxos_wait_uint32 generation = node->generation + 1U;
    if (generation == 0) {
        generation = 1;
    }
    node->owner_queue = queue;
    node->queue_previous = queue->tail;
    node->queue_next = nullptr;
    node->timer_previous = nullptr;
    node->timer_next = nullptr;
    node->deadline = now + ticks;
    node->generation = generation;
    node->reason = WakeReason::None;
    node->state = WaitNodeState::Waiting;
    node->timed = !duration.infinite;
    node->queue_linked = true;
    node->timer_linked = false;

    if (queue->tail != nullptr) {
        queue->tail->queue_next = node;
    }
    else {
        queue->head = node;
    }
    queue->tail = node;
    ++queue->count;

    if (node->timed) {
        insertTimer(node);
    }

    *nodeOut = node;
    return true;
}

WakeReason parkWait(WaitNode* node) {
    if (node == nullptr) {
        return WakeReason::Interrupted;
    }
    if (node->state == WaitNodeState::Completed) {
        return takeCompleted(node);
    }
    if (node->state != WaitNodeState::Waiting || g_hooks.park == nullptr) {
        abandonWait(node);
        return WakeReason::Interrupted;
    }

    const WakeReason platformReason = g_hooks.park(g_hooks.context, node);
    if (node->state == WaitNodeState::Completed) {
        return takeCompleted(node);
    }

    // A platform park must not return while the node remains linked.  Treat
    // an unsupported/spurious return as interruption and clean up the node.
    abandonWait(node);
    (void)takeCompleted(node);
    return platformReason == WakeReason::None
        ? WakeReason::Interrupted
        : platformReason;
}

bool wakeOne(WaitQueue* queue, WakeReason reason) {
    if (queue == nullptr || queue->head == nullptr ||
        g_hooks.makeRunnable == nullptr) {
        return false;
    }
    return complete(queue->head, reason, true);
}

gxos_wait_uint32 wakeAll(WaitQueue* queue, WakeReason reason) {
    if (queue == nullptr || g_hooks.makeRunnable == nullptr) {
        return 0;
    }

    gxos_wait_uint32 awakened = 0;
    WaitNode* node = queue->head;
    while (node != nullptr) {
        WaitNode* next = node->queue_next;
        if (complete(node, reason, true)) {
            ++awakened;
        }
        node = next;
    }
    return awakened;
}

bool cancelWait(WaitNode* node, WakeReason reason) {
    return complete(node, reason, true);
}

void abandonWait(WaitNode* node) {
    if (node == nullptr) {
        return;
    }
    if (node->state == WaitNodeState::Waiting) {
        unlinkQueue(node);
        unlinkTimer(node);
        node->reason = WakeReason::Cancelled;
        node->state = WaitNodeState::Completed;
    }
}

gxos_wait_uint32 processExpired(gxos_wait_uint64 nowTicks,
                                gxos_wait_uint32 budget) {
    gxos_wait_uint32 expired = 0;
    while (g_timer_head != nullptr && expired < budget &&
           deadlineReached(nowTicks, g_timer_head->deadline)) {
        WaitNode* node = g_timer_head;
        if (complete(node, WakeReason::TimedOut, true)) {
            ++expired;
        }
        else {
            unlinkTimer(node);
        }
    }
    return expired;
}

bool waitQueueEmpty(const WaitQueue* queue) {
    return queue == nullptr || queue->head == nullptr;
}

gxos_wait_uint32 waitQueueCount(const WaitQueue* queue) {
    return queue == nullptr ? 0 : queue->count;
}

bool waitNodeIsWaiting(const WaitNode* node) {
    return node != nullptr && node->state == WaitNodeState::Waiting;
}

bool waitNodeIsCompleted(const WaitNode* node) {
    return node != nullptr && node->state == WaitNodeState::Completed;
}

bool waitNodeTimerRegistered(const WaitNode* node) {
    return node != nullptr && node->timer_linked;
}

} // namespace scheduler_wait
} // namespace runtime
} // namespace gxos
