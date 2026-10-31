#include "../src/platform/guidexos_nativeaot_threadstore_readiness.h"

#include <cstdio>
#include <cstring>

struct StockReversePInvokeFrame {
    void* savedTransitionFrame;
    void* savedThread;
};

int main() {
    alignas(void*) unsigned char runtimeCell[0x48] = {};
    auto** telemetry = reinterpret_cast<void**>(runtimeCell + 0x40);
    *telemetry = reinterpret_cast<void*>(0x1111u);
    GuideXosThreadStoreTransitionRecord records[2] = {};
    uint32_t depth = 0u;

    StockReversePInvokeFrame first{};
    first.savedTransitionFrame = reinterpret_cast<void*>(0x2222u);
    first.savedThread = reinterpret_cast<void*>(0x3333u);
    const StockReversePInvokeFrame firstStockFrame = first;
    if (!GuideXosThreadStorePushTelemetryFrame(
            &first, runtimeCell, 0x40u, records, &depth, 2u) ||
        std::memcmp(&first, &firstStockFrame, sizeof(first)) != 0 ||
        *telemetry != &first || depth != 1u) {
        std::puts("[C188R12R4R5R4-HOSTED] stock frame was changed during telemetry push");
        return 1;
    }

    StockReversePInvokeFrame nested{};
    nested.savedTransitionFrame = reinterpret_cast<void*>(0x4444u);
    nested.savedThread = reinterpret_cast<void*>(0x5555u);
    const StockReversePInvokeFrame nestedStockFrame = nested;
    if (!GuideXosThreadStorePushTelemetryFrame(
            &nested, runtimeCell, 0x40u, records, &depth, 2u) ||
        std::memcmp(&nested, &nestedStockFrame, sizeof(nested)) != 0 ||
        *telemetry != &nested || depth != 2u) {
        std::puts("[C188R12R4R5R4-HOSTED] nested stock frame was changed");
        return 2;
    }

    StockReversePInvokeFrame overflow{};
    if (GuideXosThreadStorePushTelemetryFrame(
            &overflow, runtimeCell, 0x40u, records, &depth, 2u) || depth != 2u) {
        std::puts("[C188R12R4R5R4-HOSTED] telemetry depth limit was not enforced");
        return 3;
    }
    void* restored = nullptr;
    if (GuideXosThreadStorePopTelemetryFrame(
            &first, runtimeCell, 0x40u, records, &depth, &restored) ||
        depth != 2u || *telemetry != &nested) {
        std::puts("[C188R12R4R5R4-HOSTED] non-LIFO frame return was accepted");
        return 4;
    }
    if (!GuideXosThreadStorePopTelemetryFrame(
            &nested, runtimeCell, 0x40u, records, &depth, &restored) ||
        restored != &first || *telemetry != &first || depth != 1u ||
        nested.savedTransitionFrame != reinterpret_cast<void*>(0x4444u) ||
        nested.savedThread != reinterpret_cast<void*>(0x5555u)) {
        std::puts("[C188R12R4R5R4-HOSTED] nested return did not preserve the stock frame");
        return 5;
    }
    if (!GuideXosThreadStorePopTelemetryFrame(
            &first, runtimeCell, 0x40u, records, &depth, &restored) ||
        restored != reinterpret_cast<void*>(0x1111u) || *telemetry != restored ||
        depth != 0u || first.savedTransitionFrame != firstStockFrame.savedTransitionFrame ||
        first.savedThread != firstStockFrame.savedThread) {
        std::puts("[C188R12R4R5R4-HOSTED] return did not restore telemetry and stock state");
        return 6;
    }

    std::puts("[C188R12R4R5R4-HOSTED] outcome=PASS framePreserved=2 nested=1 overflowRejected=1");
    return 0;
}
