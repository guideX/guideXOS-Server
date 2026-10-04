#include "navigator_app_action_delivery.h"
#include "ipc.h"

#include <array>
#include <chrono>
#include <iostream>
#include <thread>
#include <utility>
#include <vector>

using gxos::apps::NavigatorAppActionDelivery;
using namespace std::chrono_literals;

namespace {
int checks = 0;
int failures = 0;

void check(bool condition, const char* description) {
    ++checks;
    if (condition) std::cout << "PASS: " << description << '\n';
    else {
        ++failures;
        std::cout << "FAIL: " << description << '\n';
    }
}
}

int main() {
    gxos::ipc::Mailbox processMailbox;
    gxos::ipc::Message mailboxMessage;
    bool mailboxFilled = true;
    for (size_t index = 0; index < processMailbox.capacity(); ++index)
        mailboxFilled = processMailbox.try_push(gxos::ipc::Message{}) && mailboxFilled;
    check(mailboxFilled && processMailbox.size() == 1024 &&
        !processMailbox.try_push(std::move(mailboxMessage)),
        "the ProcessTable normal mailbox lane is bounded at 1024 and rejects a full queue immediately");
    gxos::ipc::Message dequeued;
    check(processMailbox.try_pop(dequeued) && processMailbox.try_push(gxos::ipc::Message{}),
        "mailbox capacity becomes available again after the consumer dequeues a request");

    NavigatorAppActionDelivery delivery;
    NavigatorAppActionDelivery::Request request;

    check(!delivery.Reserve(0, 1, request) && !delivery.Reserve(10, 0, request),
        "requests require a nonzero target PID and registration generation");

    check(delivery.Reserve(10, 7, request), "request token reserves one bounded acknowledgement slot");
    check(!delivery.BeginConsumption({ 11, 7, request.token }) &&
        !delivery.BeginConsumption({ 10, 8, request.token }) &&
        !delivery.BeginConsumption({ 10, 7, request.token + 1 }) &&
        !delivery.AcknowledgeConsumed({ 10, 7, request.token + 1 }),
        "wrong target, generation, and token cannot claim an acknowledgement");
    check(delivery.BeginConsumption(request) && delivery.AcknowledgeConsumed(request) &&
        delivery.Wait(request, 10ms) == NavigatorAppActionDelivery::WaitResult::Consumed,
        "the exact target can consume and acknowledge its request");

    NavigatorAppActionDelivery::Request crossTalkA;
    NavigatorAppActionDelivery::Request crossTalkB;
    const bool crossTalkReserved = delivery.Reserve(18, 19, crossTalkA) &&
        delivery.Reserve(19, 19, crossTalkB);
    const bool crossTalkBlocked = crossTalkReserved && delivery.BeginConsumption(crossTalkA) &&
        !delivery.AcknowledgeConsumed(crossTalkB);
    const bool crossTalkCompleted = crossTalkBlocked && delivery.BeginConsumption(crossTalkB) &&
        delivery.AcknowledgeConsumed(crossTalkB) && delivery.AcknowledgeConsumed(crossTalkA) &&
        delivery.Wait(crossTalkA, 1ms) == NavigatorAppActionDelivery::WaitResult::Consumed &&
        delivery.Wait(crossTalkB, 1ms) == NavigatorAppActionDelivery::WaitResult::Consumed;
    check(crossTalkCompleted,
        "one parallel caller cannot publish another target's acknowledgement");

    NavigatorAppActionDelivery::Request delayed;
    check(delivery.Reserve(12, 9, delayed), "delayed-consumer request reserves");
    std::thread delayedConsumer([&] {
        std::this_thread::sleep_for(15ms);
        (void)delivery.BeginConsumption(delayed);
        (void)delivery.AcknowledgeConsumed(delayed);
    });
    const auto delayedResult = delivery.Wait(delayed, 250ms);
    delayedConsumer.join();
    check(delayedResult == NavigatorAppActionDelivery::WaitResult::Consumed,
        "delayed consumption inside the bound reports success");

    NavigatorAppActionDelivery::Request neverConsumed;
    check(delivery.Reserve(13, 10, neverConsumed) &&
        delivery.Wait(neverConsumed, 5ms) == NavigatorAppActionDelivery::WaitResult::TimedOut,
        "a request without a consumer times out as not consumed");
    check(!delivery.BeginConsumption(neverConsumed) && !delivery.AcknowledgeConsumed(neverConsumed),
        "a late consumer cannot claim or acknowledge an expired token");

    NavigatorAppActionDelivery::Request closedBeforeQueue;
    check(delivery.Reserve(14, 11, closedBeforeQueue) && delivery.Cancel(closedBeforeQueue) &&
        delivery.Wait(closedBeforeQueue, 1ms) == NavigatorAppActionDelivery::WaitResult::Missing,
        "target disappearance before enqueue cancels the reserved request");

    NavigatorAppActionDelivery::Request closedAfterQueue;
    check(delivery.Reserve(15, 12, closedAfterQueue) &&
        delivery.Wait(closedAfterQueue, 5ms) == NavigatorAppActionDelivery::WaitResult::TimedOut &&
        !delivery.BeginConsumption(closedAfterQueue),
        "target disappearance after enqueue cannot report consumed success");

    NavigatorAppActionDelivery::Request consumedBeforeClose;
    check(delivery.Reserve(16, 13, consumedBeforeClose) &&
        delivery.BeginConsumption(consumedBeforeClose) &&
        delivery.AcknowledgeConsumed(consumedBeforeClose) &&
        delivery.Wait(consumedBeforeClose, 1ms) == NavigatorAppActionDelivery::WaitResult::Consumed,
        "a target that closes after consumption retains legitimate success");

    NavigatorAppActionDelivery::Request rejected;
    check(delivery.Reserve(16, 13, rejected) && delivery.BeginConsumption(rejected) &&
        delivery.Reject(rejected) &&
        delivery.Wait(rejected, 1ms) == NavigatorAppActionDelivery::WaitResult::Rejected,
        "a target-side stale registration rejection returns a bounded failure");

    NavigatorAppActionDelivery::Request stale;
    NavigatorAppActionDelivery::Request current;
    const bool staleReserved = delivery.Reserve(17, 14, stale);
    const bool staleTimedOut = delivery.Wait(stale, 5ms) == NavigatorAppActionDelivery::WaitResult::TimedOut;
    const bool currentReserved = delivery.Reserve(17, 14, current);
    check(staleReserved && staleTimedOut && currentReserved && stale.token != current.token &&
        !delivery.BeginConsumption(stale) && !delivery.AcknowledgeConsumed(stale) &&
        delivery.BeginConsumption(current) && delivery.AcknowledgeConsumed(current) &&
        delivery.Wait(current, 1ms) == NavigatorAppActionDelivery::WaitResult::Consumed,
        "a stale acknowledgement cannot satisfy the next request for the same process");

    bool sequential = true;
    uint64_t previousToken = 0;
    for (uint64_t index = 0; index < 3; ++index) {
        NavigatorAppActionDelivery::Request next;
        sequential = sequential && delivery.Reserve(20, 15, next) && next.token != previousToken &&
            delivery.BeginConsumption(next) && delivery.AcknowledgeConsumed(next) &&
            delivery.Wait(next, 1ms) == NavigatorAppActionDelivery::WaitResult::Consumed;
        previousToken = next.token;
    }
    check(sequential, "sequential Home requests each receive their own consumed result");

    std::array<NavigatorAppActionDelivery::Request, NavigatorAppActionDelivery::kCapacity> full{};
    bool filled = true;
    for (size_t index = 0; index < full.size(); ++index)
        filled = delivery.Reserve(30 + index, 16, full[index]) && filled;
    NavigatorAppActionDelivery::Request overflow;
    check(filled && !delivery.Reserve(999, 16, overflow),
        "the fixed acknowledgement table rejects a caller when all 32 slots are occupied");
    for (const auto& slot : full) (void)delivery.Cancel(slot);

    size_t failedCycles = 0;
    for (uint64_t index = 0; index < 100; ++index) {
        NavigatorAppActionDelivery::Request notConsumed;
        if (delivery.Reserve(1000 + index, 18, notConsumed) &&
            delivery.Wait(notConsumed, 0ms) == NavigatorAppActionDelivery::WaitResult::TimedOut &&
            !delivery.BeginConsumption(notConsumed) && !delivery.AcknowledgeConsumed(notConsumed))
            ++failedCycles;
    }
    check(failedCycles == 100,
        "100 modeled stale or not-consumed requests time out without allowing late acknowledgements");

    constexpr size_t parallelCount = 16;
    std::array<NavigatorAppActionDelivery::Request, parallelCount> parallel{};
    bool parallelReserved = true;
    for (size_t index = 0; index < parallel.size(); ++index)
        parallelReserved = delivery.Reserve(100 + index, 17, parallel[index]) && parallelReserved;
    std::vector<std::thread> consumers;
    consumers.reserve(parallelCount);
    for (size_t index = 0; index < parallel.size(); ++index) {
        consumers.emplace_back([&, index] {
            std::this_thread::sleep_for(std::chrono::milliseconds(index % 4));
            if (delivery.BeginConsumption(parallel[index]))
                (void)delivery.AcknowledgeConsumed(parallel[index]);
        });
    }
    bool parallelConsumed = parallelReserved;
    for (auto& consumer : consumers) (void)consumer.join();
    for (const auto& item : parallel)
        parallelConsumed = delivery.Wait(item, 50ms) == NavigatorAppActionDelivery::WaitResult::Consumed && parallelConsumed;
    check(parallelConsumed, "parallel callers receive only their own target and token acknowledgements");

    std::cout << "Navigator action delivery model checks: " << (checks - failures) << '/' << checks
        << " passed; failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
