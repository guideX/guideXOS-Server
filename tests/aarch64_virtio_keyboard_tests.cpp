#include "kernel/input_queue.h"
#include "kernel/virtio_key_translation.h"

using kernel::input_queue::Event;
using kernel::input_queue::EventType;

static int checks = 0;

#define CHECK(value) do { ++checks; if (!(value)) { \
    return 1; \
} } while (0)

int main()
{
    using kernel::virtio_input::native_key_for_linux_code;
    using namespace kernel::input_queue;

    initialize();
    CHECK(native_key_for_linux_code(34) == 'G');  // Linux KEY_G
    CHECK(native_key_for_linux_code(45) == 'X');  // Linux KEY_X
    CHECK(native_key_for_linux_code(57) == 32);   // Space
    CHECK(native_key_for_linux_code(7) == '6');    // Linux KEY_6
    CHECK(native_key_for_linux_code(5) == '4');    // Linux KEY_4
    CHECK(native_key_for_linux_code(28) == 13);    // Studio expects VK_RETURN
    CHECK(native_key_for_linux_code(105) == 37);   // Studio expects VK_LEFT
    CHECK(native_key_for_linux_code(42) == 16);    // Studio expects VK_SHIFT

    CHECK(push_key(EventType::KeyDown, 'X', native_key_for_linux_code(45), 1,
                   OriginVirtioHardware));
    CHECK(push_key(EventType::KeyUp, 'X', native_key_for_linux_code(45), 1,
                   OriginVirtioHardware));
    Event event{};
    CHECK(pop(&event));
    CHECK(event.type == EventType::KeyDown && event.key == 'X');
    CHECK(event.nativeKey == 'X' && event.modifiers == 1);
    const uint64_t downSequence = event.sequence;
    CHECK(pop(&event));
    CHECK(event.type == EventType::KeyUp && event.key == 'X');
    CHECK(event.nativeKey == 'X' && event.modifiers == 1);
    CHECK(event.sequence == downSequence + 1);
    CHECK(empty());

    CHECK(push_key(EventType::KeyDown, 0, native_key_for_linux_code(42), 1,
                   OriginVirtioHardware));
    CHECK(push_key(EventType::KeyUp, 0, native_key_for_linux_code(42), 0,
                   OriginVirtioHardware));
    CHECK(pop(&event));
    CHECK(event.nativeKey == 16 && event.modifiers == 1 && event.type == EventType::KeyDown);
    CHECK(pop(&event));
    CHECK(event.nativeKey == 16 && event.modifiers == 0 && event.type == EventType::KeyUp);
    CHECK(empty());

    return checks == 25 ? 0 : 1;
}
