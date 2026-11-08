#include "../src/platform/guidexos_nativeaot_threadstore_readiness.h"

#include <cstdio>
#include <cstring>

struct StockReversePInvokeFrame {
    void* savedTransitionFrame;
    void* savedThread;
};

// This is a small hosted model of the pinned Thread::InlineTryFastReversePInvoke
// and InlineReversePInvokeReturn contract. Source-contract assertions in the
// PowerShell suite bind this model to thread.inl; this is not native acceptance.
struct ReversePInvokeModeModel {
    uint32_t flags = 0u;
    void* transitionFrame = nullptr;
    void* palFlsValue = nullptr;
    bool threadStorePublished = false;
    uintptr_t nativeThreadId = 0u;
};

struct ReversePInvokeModeFrame {
    void* savedTransitionFrame = nullptr;
};

constexpr uint32_t kModelAttached = 0x1u;
void* const kModelTopOfStack = reinterpret_cast<void*>(~static_cast<uintptr_t>(0));

bool tryFastReversePInvoke(
    ReversePInvokeModeModel& thread, ReversePInvokeModeFrame& frame) {
    frame.savedTransitionFrame = thread.transitionFrame;
    if ((thread.flags & kModelAttached) == 0u || thread.transitionFrame == nullptr) {
        return false;
    }
    thread.transitionFrame = nullptr;
    return true;
}

void reversePInvokeReturn(
    ReversePInvokeModeModel& thread, const ReversePInvokeModeFrame& frame) {
    thread.transitionFrame = frame.savedTransitionFrame;
}

bool modelNativeEntry(
    ReversePInvokeModeModel& thread, ReversePInvokeModeFrame& frame) {
    if ((thread.flags & kModelAttached) == 0u) {
        // Mirrors Thread::Construct followed by PAL FLS and ThreadStore publication.
        thread.transitionFrame = kModelTopOfStack;
        thread.flags |= kModelAttached;
        thread.palFlsValue = &thread;
        thread.threadStorePublished = true;
        thread.nativeThreadId = 1u;
    }
    return tryFastReversePInvoke(thread, frame);
}

int main() {
    uint32_t modeAssertions = 0u;
    ReversePInvokeModeModel thread{};
    if (thread.flags != 0u || thread.transitionFrame != nullptr ||
        thread.palFlsValue != nullptr || thread.threadStorePublished) {
        std::puts("[C188R12R4R5R4-HOSTED] initial unattached state was not empty");
        return 7;
    }
    ++modeAssertions;

    ReversePInvokeModeFrame initializeModulesFrame{};
    if (!modelNativeEntry(thread, initializeModulesFrame) ||
        thread.flags != kModelAttached || thread.transitionFrame != nullptr ||
        thread.palFlsValue != &thread || !thread.threadStorePublished ||
        thread.nativeThreadId != 1u) {
        std::puts("[C188R12R4R5R4-HOSTED] first native entry did not attach and enter cooperative mode");
        return 8;
    }
    ++modeAssertions;
    if (initializeModulesFrame.savedTransitionFrame != kModelTopOfStack) {
        std::puts("[C188R12R4R5R4-HOSTED] first reverse entry did not save the constructed top marker");
        return 9;
    }
    ++modeAssertions;

    // The generated initializer returns through its own reverse frame.
    reversePInvokeReturn(thread, initializeModulesFrame);
    if (thread.transitionFrame != kModelTopOfStack || thread.palFlsValue != &thread ||
        !thread.threadStorePublished) {
        std::puts("[C188R12R4R5R4-HOSTED] initializer return did not restore preemptive mode and identity");
        return 10;
    }
    ++modeAssertions;

    ReversePInvokeModeFrame managedMainFrame{};
    if (!tryFastReversePInvoke(thread, managedMainFrame) ||
        managedMainFrame.savedTransitionFrame != kModelTopOfStack ||
        thread.transitionFrame != nullptr) {
        std::puts("[C188R12R4R5R4-HOSTED] valid second reverse entry was rejected");
        return 11;
    }
    ++modeAssertions;
    reversePInvokeReturn(thread, managedMainFrame);
    if (thread.transitionFrame != kModelTopOfStack || thread.palFlsValue != &thread ||
        !thread.threadStorePublished) {
        std::puts("[C188R12R4R5R4-HOSTED] second reverse return did not restore saved state");
        return 12;
    }
    ++modeAssertions;

    thread.transitionFrame = nullptr;
    ReversePInvokeModeFrame invalidCooperativeFrame{};
    if (tryFastReversePInvoke(thread, invalidCooperativeFrame) ||
        invalidCooperativeFrame.savedTransitionFrame != nullptr ||
        thread.transitionFrame != nullptr) {
        std::puts("[C188R12R4R5R4-HOSTED] cooperative entry was accepted or a frame was fabricated");
        return 13;
    }
    ++modeAssertions;

    uint32_t telemetryAssertions = 0u;
    GuideXosThreadStoreTransitionRecord records[2] = {};
    uint32_t depth = 0u;
    ReversePInvokeModeModel telemetryThread{};
    telemetryThread.flags = kModelAttached;
    telemetryThread.transitionFrame = kModelTopOfStack;

    StockReversePInvokeFrame first{};
    first.savedTransitionFrame = reinterpret_cast<void*>(0x2222u);
    first.savedThread = reinterpret_cast<void*>(0x3333u);
    const StockReversePInvokeFrame firstStockFrame = first;
    if (!GuideXosThreadStorePushTelemetryFrame(
            &first, records, &depth, 2u) ||
        std::memcmp(&first, &firstStockFrame, sizeof(first)) != 0 ||
        records[0].frame != &first || records[0].previousTelemetryFrame != nullptr ||
        telemetryThread.transitionFrame != kModelTopOfStack || depth != 1u) {
        std::puts("[C188R12R4R5R4-HOSTED] telemetry push changed the stock frame or live transition mode");
        return 14;
    }
    ++telemetryAssertions;

    StockReversePInvokeFrame nested{};
    nested.savedTransitionFrame = reinterpret_cast<void*>(0x4444u);
    nested.savedThread = reinterpret_cast<void*>(0x5555u);
    const StockReversePInvokeFrame nestedStockFrame = nested;
    if (!GuideXosThreadStorePushTelemetryFrame(
            &nested, records, &depth, 2u) ||
        std::memcmp(&nested, &nestedStockFrame, sizeof(nested)) != 0 ||
        records[1].frame != &nested || records[1].previousTelemetryFrame != &first ||
        telemetryThread.transitionFrame != kModelTopOfStack || depth != 2u) {
        std::puts("[C188R12R4R5R4-HOSTED] nested telemetry changed the stock frame or live mode");
        return 15;
    }
    ++telemetryAssertions;

    StockReversePInvokeFrame overflow{};
    if (GuideXosThreadStorePushTelemetryFrame(
            &overflow, records, &depth, 2u) || depth != 2u ||
        telemetryThread.transitionFrame != kModelTopOfStack) {
        std::puts("[C188R12R4R5R4-HOSTED] telemetry depth limit was not enforced");
        return 16;
    }
    ++telemetryAssertions;
    void* restored = nullptr;
    if (GuideXosThreadStorePopTelemetryFrame(
            &first, records, &depth, &restored) ||
        depth != 2u || telemetryThread.transitionFrame != kModelTopOfStack) {
        std::puts("[C188R12R4R5R4-HOSTED] non-LIFO frame return was accepted");
        return 17;
    }
    ++telemetryAssertions;
    if (!GuideXosThreadStorePopTelemetryFrame(
            &nested, records, &depth, &restored) ||
        restored != &first || depth != 1u ||
        telemetryThread.transitionFrame != kModelTopOfStack ||
        nested.savedTransitionFrame != reinterpret_cast<void*>(0x4444u) ||
        nested.savedThread != reinterpret_cast<void*>(0x5555u)) {
        std::puts("[C188R12R4R5R4-HOSTED] nested telemetry return changed the live mode or stock frame");
        return 18;
    }
    ++telemetryAssertions;
    if (!GuideXosThreadStorePopTelemetryFrame(
            &first, records, &depth, &restored) ||
        restored != nullptr ||
        depth != 0u || first.savedTransitionFrame != firstStockFrame.savedTransitionFrame ||
        first.savedThread != firstStockFrame.savedThread ||
        telemetryThread.transitionFrame != kModelTopOfStack) {
        std::puts("[C188R12R4R5R4-HOSTED] outer telemetry return changed the live mode or stock frame");
        return 19;
    }
    ++telemetryAssertions;

    std::printf("[C188R12R4R5R4-HOSTED] outcome=PASS modeModelAssertions=%u telemetryAssertions=%u nativeGuest=not_attempted framePreserved=2 nested=1 overflowRejected=1\n",
        modeAssertions, telemetryAssertions);
    return 0;
}
