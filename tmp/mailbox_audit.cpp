#include "../ipc.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using gxos::ipc::Mailbox;
using gxos::ipc::Message;

static Message message(uint32_t type) {
    Message result;
    result.type = type;
    return result;
}

static bool waitFor(const std::atomic<bool>& value, int milliseconds) {
    for (int elapsed = 0; elapsed < milliseconds; elapsed += 5) {
        if (value.load()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return value.load();
}

int main() {
    bool ok = true;

    Mailbox bounded(2);
    bounded.push(message(1));
    bounded.push(message(2));
    ok &= bounded.size() == 2;

    std::atomic<bool> producerFinished{false};
    std::thread producer([&] {
        bounded.push(message(3));
        producerFinished.store(true);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    ok &= !producerFinished.load() && bounded.size() == 2;

    Message removed;
    ok &= bounded.try_pop_type(1, removed) && removed.type == 1;
    ok &= waitFor(producerFinished, 250) && bounded.size() == 2;
    producer.join();

    Message first;
    ok &= bounded.try_pop(first) && first.type == 2;
    ok &= bounded.try_pop_type(3, removed) && removed.type == 3;
    const bool exactTypeRemovalOk = first.type == 2 && removed.type == 3;
    ok &= bounded.size() == 0;

    Mailbox typed(1);
    typed.push(message(10));
    std::atomic<bool> typedProducerFinished{false};
    std::thread typedProducer([&] {
        typed.push(message(20));
        typedProducerFinished.store(true);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    ok &= !typedProducerFinished.load() && typed.size() == 1;
    ok &= typed.try_pop_type(10, removed) && removed.type == 10;
    ok &= waitFor(typedProducerFinished, 250) && typed.size() == 1;
    typedProducer.join();

    Message last;
    ok &= typed.pop(last, 250) && last.type == 20 && typed.size() == 0;

    std::cout << "mailbox.capacity_bounded=" << (bounded.capacity() == 2 ? "true" : "false") << "\n";
    std::cout << "mailbox.exact_type_removal=" << (exactTypeRemovalOk ? "true" : "false") << "\n";
    std::cout << "mailbox.producer_wakeup_after_try_pop_type=" << (typedProducerFinished.load() ? "true" : "false") << "\n";
    std::cout << "mailbox.result=" << (ok ? "PASS" : "FAIL") << "\n";
    return ok ? 0 : 1;
}
