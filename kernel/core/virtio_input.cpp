// VirtIO input provider.
//
// This is deliberately a small modern virtio-MMIO driver. It discovers the
// transport from platform data, negotiates one event queue, and polls the
// device's used ring. Device reports terminate at the common input manager;
// no desktop code knows about virtio or evdev codes.

#include "include/kernel/virtio_input.h"
#include "include/kernel/virtio.h"
#include "include/kernel/input_manager.h"
#include "include/kernel/irq_registry.h"
#include "include/kernel/serial_debug.h"

namespace kernel {
namespace virtio_input {

extern "C" uint8_t phase3_irq_enable(uint32_t irq);
extern "C" uint8_t phase3_irq_disable(uint32_t irq);

namespace {

static const uint32_t kQueueSize = 64;
static const uint32_t kMaxDevices = 4;
static const uint32_t kMaxEventsPerPoll = 256;
static const uint32_t kVirtioMagic = 0x74726976u;
static const uint32_t kVirtioVersionModern = 2u;

static const uint32_t kMmioMagic = 0x000;
static const uint32_t kMmioVersion = 0x004;
static const uint32_t kMmioDeviceId = 0x008;
static const uint32_t kMmioDeviceFeatures = 0x010;
static const uint32_t kMmioDeviceFeaturesSel = 0x014;
static const uint32_t kMmioDriverFeatures = 0x020;
static const uint32_t kMmioDriverFeaturesSel = 0x024;
static const uint32_t kMmioQueueSel = 0x030;
static const uint32_t kMmioQueueNumMax = 0x034;
static const uint32_t kMmioQueueNum = 0x038;
static const uint32_t kMmioQueueReady = 0x044;
static const uint32_t kMmioQueueNotify = 0x050;
static const uint32_t kMmioInterruptStatus = 0x060;
static const uint32_t kMmioInterruptAck = 0x064;
static const uint32_t kMmioStatus = 0x070;
static const uint32_t kMmioQueueDescLow = 0x080;
static const uint32_t kMmioQueueDescHigh = 0x084;
static const uint32_t kMmioQueueAvailLow = 0x090;
static const uint32_t kMmioQueueAvailHigh = 0x094;
static const uint32_t kMmioQueueUsedLow = 0x0a0;
static const uint32_t kMmioQueueUsedHigh = 0x0a4;
static const uint32_t kMmioConfig = 0x100;

static const uint8_t kStatusAcknowledge = 1u;
static const uint8_t kStatusDriver = 2u;
static const uint8_t kStatusFeaturesOk = 8u;
static const uint8_t kStatusDriverOk = 4u;
static const uint64_t kFeatureVersion1 = UINT64_C(1) << 32;

static const uint16_t kEvSyn = 0x00;
static const uint16_t kEvKey = 0x01;
static const uint16_t kEvRel = 0x02;
static const uint16_t kEvAbs = 0x03;
static const uint16_t kRelX = 0x00;
static const uint16_t kRelY = 0x01;
static const uint16_t kRelWheel = 0x08;
static const uint16_t kAbsX = 0x00;
static const uint16_t kAbsY = 0x01;
static const uint16_t kBtnLeft = 0x110;
static const uint16_t kBtnRight = 0x111;
static const uint16_t kBtnMiddle = 0x112;
static const uint16_t kBtnTouch = 0x14a;

// Linux input key codes are part of the virtio-input event contract.
static const uint16_t kKeyEsc = 1;
static const uint16_t kKey1 = 2;
static const uint16_t kKey0 = 11;
static const uint16_t kKeyBackspace = 14;
static const uint16_t kKeyTab = 15;
static const uint16_t kKeyEnter = 28;
static const uint16_t kKeyLeftShift = 42;
static const uint16_t kKeyRightShift = 54;
static const uint16_t kKeyLeftCtrl = 29;
static const uint16_t kKeyRightCtrl = 97;
static const uint16_t kKeyLeftAlt = 56;
static const uint16_t kKeyRightAlt = 100;
static const uint16_t kKeyCapsLock = 58;
static const uint16_t kKeyLeftMeta = 125;
static const uint16_t kKeyRightMeta = 126;
static const uint16_t kKeyUp = 103;
static const uint16_t kKeyDown = 108;
static const uint16_t kKeyLeft = 105;
static const uint16_t kKeyRight = 106;
static const uint16_t kKeyDelete = 111;
static const uint16_t kKeySpace = 57;

struct AvailStorage {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[kQueueSize];
    uint16_t usedEvent;
} __attribute__((packed, aligned(16)));

struct UsedStorage {
    uint16_t flags;
    uint16_t idx;
    virtio::VringUsedElem ring[kQueueSize];
    uint16_t availEvent;
} __attribute__((packed, aligned(16)));

struct DeviceState {
    bool active;
    bool keyboard;
    bool pointer;
    bool absolute;
    uint64_t base;
    uint32_t irq;
    uint16_t queueSize;
    uint16_t lastUsed;
    uint8_t buttons;
    volatile uint8_t interruptPending;
    uint8_t reserved[2];
    int32_t absoluteX;
    int32_t absoluteY;
    int32_t absMinX;
    int32_t absMaxX;
    int32_t absMinY;
    int32_t absMaxY;
    virtio::VringDesc desc[kQueueSize] __attribute__((aligned(16)));
    AvailStorage avail;
    UsedStorage used __attribute__((aligned(16)));
    InputEvent events[kQueueSize] __attribute__((aligned(16)));
};

static DeviceState s_devices[kMaxDevices]{};
static DeviceInfo s_deviceInfo{};
static MouseState s_mouseState{};
static KeyboardState s_keyboardState{};
static bool s_mouseDirty = false;
static bool s_keyboardDirty = false;
static int32_t s_screenWidth = 1024;
static int32_t s_screenHeight = 768;
static uint8_t s_deviceCount = 0;
static uint64_t s_hardwareEvents = 0;
static uint64_t s_pointerEvents = 0;
static uint64_t s_buttonEvents = 0;
static uint64_t s_keyboardEvents = 0;
static uint64_t s_malformedEvents = 0;
static uint64_t s_interruptsObserved = 0;
static uint64_t s_pollCount = 0;
static uint64_t s_drainedEvents = 0;
static uint64_t s_interruptStatusAcks = 0;
static bool s_irqHandlersRegistered = false;
static bool s_shift = false;
static bool s_ctrl = false;
static bool s_alt = false;
static bool s_meta = false;
static bool s_capsLock = false;

static uint32_t read32(uint64_t base, uint32_t offset)
{
    return *reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(base + offset));
}

static void write32(uint64_t base, uint32_t offset, uint32_t value)
{
    *reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(base + offset)) = value;
    __asm__ volatile("dsb sy" ::: "memory");
}

static uint8_t read8(uint64_t base, uint32_t offset)
{
    return *reinterpret_cast<volatile uint8_t*>(static_cast<uintptr_t>(base + offset));
}

static void write8(uint64_t base, uint32_t offset, uint8_t value)
{
    *reinterpret_cast<volatile uint8_t*>(static_cast<uintptr_t>(base + offset)) = value;
    __asm__ volatile("dsb sy" ::: "memory");
}

static void* virtio_input_irq_handler(uint32_t, void* frame, void* context)
{
    DeviceState* device = static_cast<DeviceState*>(context);
    if (!device || !device->active) return nullptr;
    const uint32_t status = read32(device->base, kMmioInterruptStatus);
    if (status != 0) {
        write32(device->base, kMmioInterruptAck, status);
        device->interruptPending = 1;
        // The device line is level-triggered.  Mask it until the scheduler
        // context drains the used ring; otherwise the same pending status can
        // preempt the consumer indefinitely.
        phase3_irq_disable(device->irq);
        ++s_interruptsObserved;
    }
    // Returning null tells the exception bridge to restore the interrupted
    // task in place; a non-null return is reserved for a scheduler context
    // switch, not for ordinary device IRQs.
    (void)frame;
    return nullptr;
}

static uint64_t read_features(uint64_t base)
{
    write32(base, kMmioDeviceFeaturesSel, 0);
    const uint64_t low = read32(base, kMmioDeviceFeatures);
    write32(base, kMmioDeviceFeaturesSel, 1);
    const uint64_t high = read32(base, kMmioDeviceFeatures);
    return low | (high << 32);
}

static void write_features(uint64_t base, uint64_t features)
{
    write32(base, kMmioDriverFeaturesSel, 0);
    write32(base, kMmioDriverFeatures, static_cast<uint32_t>(features));
    write32(base, kMmioDriverFeaturesSel, 1);
    write32(base, kMmioDriverFeatures, static_cast<uint32_t>(features >> 32));
}

static void write_address(uint64_t base, uint32_t lowOffset, uint64_t address)
{
    write32(base, lowOffset, static_cast<uint32_t>(address));
    write32(base, lowOffset + 4u, static_cast<uint32_t>(address >> 32));
}

static bool bit_set(const uint8_t* bitmap, uint32_t size, uint32_t bit)
{
    return bitmap != nullptr && bit / 8u < size && (bitmap[bit / 8u] & (1u << (bit & 7u))) != 0;
}

static uint8_t read_capability(uint64_t base, uint8_t select, uint8_t subselect,
                               uint8_t* bytes, uint8_t capacity)
{
    if (bytes == nullptr || capacity == 0) return 0;
    write8(base, kMmioConfig, select);
    write8(base, kMmioConfig + 1u, subselect);
    const uint8_t size = read8(base, kMmioConfig + 2u);
    const uint8_t count = size < capacity ? size : capacity;
    // virtio_input_config reserves bytes 3..7; the selected payload begins
    // at offset 8 (the common device-config base is 0x100).
    for (uint8_t i = 0; i < count; ++i) bytes[i] = read8(base, kMmioConfig + 8u + i);
    return count;
}

static void query_capabilities(DeviceState* device)
{
    uint8_t keyBits[32] = {};
    const uint8_t keyBytes = read_capability(device->base, 0x11u, 0x01u,
                                             keyBits, sizeof(keyBits));
    uint8_t relBits[32] = {};
    const uint8_t relBytes = read_capability(device->base, 0x11u, 0x02u,
                                             relBits, sizeof(relBits));
    device->keyboard = bit_set(keyBits, keyBytes, 30u) || bit_set(keyBits, keyBytes, 2u);
    device->pointer = bit_set(relBits, relBytes, kRelX) || bit_set(relBits, relBytes, kRelY);

    uint8_t absBits[32] = {};
    const uint8_t absBytes = read_capability(device->base, 0x11u, 0x03u,
                                             absBits, sizeof(absBits));
    device->absolute = bit_set(absBits, absBytes, kAbsX) || bit_set(absBits, absBytes, kAbsY);
    if (device->absolute) device->pointer = true;

    device->absMinX = 0;
    device->absMaxX = 32767;
    device->absMinY = 0;
    device->absMaxY = 32767;
    if (device->absolute) {
        uint8_t absInfo[20] = {};
        if (read_capability(device->base, 0x12u, 0x00u, absInfo, sizeof(absInfo)) >= 8u) {
            device->absMinX = static_cast<int32_t>(absInfo[0] | (absInfo[1] << 8) |
                (absInfo[2] << 16) | (absInfo[3] << 24));
            device->absMaxX = static_cast<int32_t>(absInfo[4] | (absInfo[5] << 8) |
                (absInfo[6] << 16) | (absInfo[7] << 24));
        }
        if (read_capability(device->base, 0x12u, 0x01u, absInfo, sizeof(absInfo)) >= 8u) {
            device->absMinY = static_cast<int32_t>(absInfo[0] | (absInfo[1] << 8) |
                (absInfo[2] << 16) | (absInfo[3] << 24));
            device->absMaxY = static_cast<int32_t>(absInfo[4] | (absInfo[5] << 8) |
                (absInfo[6] << 16) | (absInfo[7] << 24));
        }
        if (device->absMaxX <= device->absMinX) { device->absMinX = 0; device->absMaxX = 32767; }
        if (device->absMaxY <= device->absMinY) { device->absMinY = 0; device->absMaxY = 32767; }
    }
#if defined(GXOS_AARCH64_PHASE7)
    serial::puts("[VIRTIO] caps base="); serial::put_hex64(device->base);
    serial::puts(" keyBytes="); serial::put_hex8(keyBytes);
    serial::puts(" key2="); serial::put_hex8(bit_set(keyBits, keyBytes, 2u) ? 1u : 0u);
    serial::puts(" key30="); serial::put_hex8(bit_set(keyBits, keyBytes, 30u) ? 1u : 0u);
    serial::puts(" relBytes="); serial::put_hex8(relBytes);
    serial::puts(" relX="); serial::put_hex8(bit_set(relBits, relBytes, kRelX) ? 1u : 0u);
    serial::puts(" relY="); serial::put_hex8(bit_set(relBits, relBytes, kRelY) ? 1u : 0u);
    serial::puts(" absBytes="); serial::put_hex8(absBytes);
    serial::puts(" absX="); serial::put_hex8(bit_set(absBits, absBytes, kAbsX) ? 1u : 0u);
    serial::puts(" absY="); serial::put_hex8(bit_set(absBits, absBytes, kAbsY) ? 1u : 0u);
    serial::putc('\n');
#endif
    if (!device->keyboard && !device->pointer) device->keyboard = true;
}

static bool setup_device(DeviceState* device)
{
    const uint64_t base = device->base;
    const uint32_t magic = read32(base, kMmioMagic);
    const uint32_t version = read32(base, kMmioVersion);
    const uint32_t deviceId = read32(base, kMmioDeviceId);
#if defined(GXOS_AARCH64_PHASE7)
    serial::puts("[VIRTIO] probe base="); serial::put_hex64(base);
    serial::puts(" magic="); serial::put_hex32(magic);
    serial::puts(" version="); serial::put_hex32(version);
    serial::puts(" device="); serial::put_hex32(deviceId);
    serial::putc('\n');
#endif
    if (magic != kVirtioMagic || version != kVirtioVersionModern ||
        deviceId != virtio::DEVICE_INPUT) return false;

    write32(base, kMmioStatus, 0);
    write32(base, kMmioStatus, kStatusAcknowledge | kStatusDriver);
    const uint64_t offered = read_features(base);
    if ((offered & kFeatureVersion1) == 0) {
#if defined(GXOS_AARCH64_PHASE7)
        serial::puts("[VIRTIO] feature-version1: FAIL\n");
#endif
        return false;
    }
    write_features(base, kFeatureVersion1);
    write32(base, kMmioStatus,
            kStatusAcknowledge | kStatusDriver | kStatusFeaturesOk);
    if ((read32(base, kMmioStatus) & kStatusFeaturesOk) == 0) {
#if defined(GXOS_AARCH64_PHASE7)
        serial::puts("[VIRTIO] feature negotiation: FAIL\n");
#endif
        return false;
    }

    write32(base, kMmioQueueSel, 0);
    const uint32_t maxQueue = read32(base, kMmioQueueNumMax);
    if (maxQueue < 2) {
#if defined(GXOS_AARCH64_PHASE7)
        serial::puts("[VIRTIO] queue: FAIL\n");
#endif
        return false;
    }
    device->queueSize = static_cast<uint16_t>(maxQueue < kQueueSize ? maxQueue : kQueueSize);
    device->lastUsed = 0;
    device->avail.flags = 0;
    device->avail.idx = 0;
    device->used.flags = 0;
    device->used.idx = 0;
    for (uint16_t i = 0; i < device->queueSize; ++i) {
        device->desc[i].addr = reinterpret_cast<uint64_t>(&device->events[i]);
        device->desc[i].len = sizeof(InputEvent);
        device->desc[i].flags = virtio::VRING_DESC_F_WRITE;
        device->desc[i].next = 0;
        device->avail.ring[i] = i;
    }
    device->avail.idx = device->queueSize;
    write32(base, kMmioQueueNum, device->queueSize);
    write_address(base, kMmioQueueDescLow, reinterpret_cast<uint64_t>(device->desc));
    write_address(base, kMmioQueueAvailLow, reinterpret_cast<uint64_t>(&device->avail));
    write_address(base, kMmioQueueUsedLow, reinterpret_cast<uint64_t>(&device->used));
    write32(base, kMmioQueueReady, 1);
    write32(base, kMmioQueueNotify, 0);
    __asm__ volatile("dmb ish" ::: "memory");
    write32(base, kMmioStatus,
            kStatusAcknowledge | kStatusDriver | kStatusFeaturesOk | kStatusDriverOk);
    if ((read32(base, kMmioStatus) & kStatusDriverOk) == 0) return false;
    query_capabilities(device);
    return device->keyboard || device->pointer;
}

static void emit_pointer(DeviceState* device, int32_t dx, int32_t dy, int16_t wheel)
{
    ++s_pointerEvents;
    if (device->absolute) {
        input::submit_platform_pointer_absolute(
            device->absoluteX, device->absoluteY, device->absMinX,
            device->absMaxX, device->absMinY, device->absMaxY,
            device->buttons, wheel);
    } else {
        input::submit_platform_pointer_relative(dx, dy, device->buttons, wheel);
    }
    s_mouseState.x = device->absolute ? device->absoluteX : dx;
    s_mouseState.y = device->absolute ? device->absoluteY : dy;
    s_mouseState.buttons = device->buttons;
    s_mouseState.wheel = static_cast<int8_t>(wheel);
    s_mouseState.is_absolute = device->absolute;
    s_mouseDirty = true;
}

static bool is_button(uint16_t code, uint8_t* bit)
{
    if (code == kBtnLeft || code == kBtnTouch) { *bit = 0x01; return true; }
    if (code == kBtnRight) { *bit = 0x02; return true; }
    if (code == kBtnMiddle) { *bit = 0x04; return true; }
    return false;
}

static uint32_t special_key(uint16_t code)
{
    if (code == kKeyEsc) return 27;
    if (code == kKeyEnter) return 10;
    if (code == kKeyBackspace) return 8;
    if (code == kKeyTab) return 9;
    if (code == kKeySpace) return ' ';
    if (code == kKeyUp) return 0x100;
    if (code == kKeyDown) return 0x101;
    if (code == kKeyLeft) return 0x102;
    if (code == kKeyRight) return 0x103;
    if (code == kKeyDelete) return 0x106;
    if (code == kKeyLeftMeta || code == kKeyRightMeta) return 0x11c;
    return 0;
}

static char printable_key(uint16_t code)
{
    const char* letters = nullptr;
    uint16_t index = 0;
    if (code >= 16 && code <= 25) {
        letters = "qwertyuiop";
        index = code - 16;
    } else if (code >= 30 && code <= 38) {
        letters = "asdfghjkl";
        index = code - 30;
    } else if (code >= 44 && code <= 50) {
        letters = "zxcvbnm";
        index = code - 44;
    }
    if (letters) {
        const char lower = letters[index];
        return (s_shift || s_capsLock) ? static_cast<char>(lower - 'a' + 'A') : lower;
    }
    if (code >= kKey1 && code <= kKey0) {
        static const char normal[] = "1234567890";
        static const char shifted[] = "!@#$%^&*()";
        const uint32_t index = code - kKey1;
        return (s_shift && index < 10) ? shifted[index] : normal[index];
    }
    return 0;
}

static void process_keyboard(const InputEvent& event)
{
    const bool down = event.value != 0;
    if (event.code == kKeyLeftShift || event.code == kKeyRightShift) s_shift = down;
    if (event.code == kKeyLeftCtrl || event.code == kKeyRightCtrl) s_ctrl = down;
    if (event.code == kKeyLeftAlt || event.code == kKeyRightAlt) s_alt = down;
    if (event.code == kKeyLeftMeta || event.code == kKeyRightMeta) s_meta = down;
    if (event.code == kKeyCapsLock && down) s_capsLock = !s_capsLock;
    const uint32_t special = special_key(event.code);
    const char printable = printable_key(event.code);
    const uint32_t key = printable != 0 ? static_cast<uint32_t>(printable) : special;
    if (key == 0 && event.code != kKeyLeftShift && event.code != kKeyRightShift &&
        event.code != kKeyLeftCtrl && event.code != kKeyRightCtrl &&
        event.code != kKeyLeftAlt && event.code != kKeyRightAlt &&
        event.code != kKeyCapsLock) {
        ++s_malformedEvents;
        return;
    }
    uint8_t keyIndex = 0xff;
    for (uint8_t i = 0; i < s_keyboardState.keyCount; ++i) {
        if (s_keyboardState.keys[i] == static_cast<uint8_t>(event.code)) {
            keyIndex = i;
            break;
        }
    }
    if (down && keyIndex == 0xff && s_keyboardState.keyCount < 6) {
        s_keyboardState.keys[s_keyboardState.keyCount++] = static_cast<uint8_t>(event.code);
    } else if (!down && keyIndex != 0xff) {
        const uint8_t last = static_cast<uint8_t>(s_keyboardState.keyCount - 1u);
        s_keyboardState.keys[keyIndex] = s_keyboardState.keys[last];
        s_keyboardState.keys[last] = 0;
        --s_keyboardState.keyCount;
    }
    input::submit_platform_key(key, down);
    s_keyboardState.modifiers = static_cast<uint8_t>((s_shift ? 1u : 0u) |
        (s_capsLock ? 2u : 0u) | (s_ctrl ? 4u : 0u) |
        (s_alt ? 8u : 0u) | (s_meta ? 16u : 0u));
    s_keyboardDirty = true;
}

static void process_event(DeviceState* device, const InputEvent& event)
{
    ++s_hardwareEvents;
    if (event.type == kEvRel) {
        if (event.code == kRelX) emit_pointer(device, event.value, 0, 0);
        else if (event.code == kRelY) emit_pointer(device, 0, event.value, 0);
        else if (event.code == kRelWheel) emit_pointer(device, 0, 0,
                                                        static_cast<int16_t>(event.value));
        else ++s_malformedEvents;
        return;
    }
    if (event.type == kEvAbs) {
        if (event.code == kAbsX) device->absoluteX = event.value;
        else if (event.code == kAbsY) device->absoluteY = event.value;
        else { ++s_malformedEvents; return; }
        emit_pointer(device, 0, 0, 0);
        return;
    }
    if (event.type == kEvKey) {
        uint8_t bit = 0;
        if (is_button(event.code, &bit)) {
            ++s_buttonEvents;
            if (event.value != 0) device->buttons |= bit;
            else device->buttons &= static_cast<uint8_t>(~bit);
            emit_pointer(device, 0, 0, 0);
        } else {
            ++s_keyboardEvents;
            process_keyboard(event);
        }
        return;
    }
    if (event.type != kEvSyn) ++s_malformedEvents;
}

static void poll_device(DeviceState* device)
{
    if (!device->active) return;
    ++s_pollCount;
    const uint32_t interruptStatus = read32(device->base, kMmioInterruptStatus);
    if (interruptStatus != 0) {
        ++s_interruptStatusAcks;
        write32(device->base, kMmioInterruptAck, interruptStatus);
    }
    __asm__ volatile("dmb ish" ::: "memory");
    const uint16_t usedIndex = device->used.idx;
    uint32_t processed = 0;
    while (device->lastUsed != usedIndex && processed < kMaxEventsPerPoll) {
        const uint16_t slot = static_cast<uint16_t>(device->lastUsed % device->queueSize);
        const virtio::VringUsedElem& used = device->used.ring[slot];
        if (used.id >= device->queueSize || used.len < sizeof(InputEvent)) {
            ++s_malformedEvents;
        } else {
            process_event(device, device->events[used.id]);
            const uint16_t availSlot = static_cast<uint16_t>(device->avail.idx % device->queueSize);
            device->avail.ring[availSlot] = static_cast<uint16_t>(used.id);
            __asm__ volatile("dmb ish" ::: "memory");
            ++device->avail.idx;
        }
        ++device->lastUsed;
        ++processed;
    }
    s_drainedEvents += processed;
    if (processed != 0) write32(device->base, kMmioQueueNotify, 0);
    if (device->interruptPending) {
        device->interruptPending = 0;
        phase3_irq_enable(device->irq);
    }
}

} // namespace

void init(uint32_t screen_width, uint32_t screen_height,
          const input::PlatformInputDevice* devices, uint8_t device_count)
{
    s_screenWidth = static_cast<int32_t>(screen_width);
    s_screenHeight = static_cast<int32_t>(screen_height);
    s_deviceInfo = DeviceInfo{};
    s_mouseState = MouseState{};
    s_keyboardState = KeyboardState{};
    s_mouseState.x = s_screenWidth / 2;
    s_mouseState.y = s_screenHeight / 2;
    s_mouseState.is_absolute = false;
    s_mouseDirty = false;
    s_keyboardDirty = false;
    s_deviceCount = 0;
    s_hardwareEvents = s_pointerEvents = s_buttonEvents = s_keyboardEvents = 0;
    s_malformedEvents = s_interruptsObserved = 0;
    s_pollCount = s_drainedEvents = s_interruptStatusAcks = 0;
    s_irqHandlersRegistered = false;
    s_shift = false;
    s_ctrl = false;
    s_alt = false;
    s_meta = false;
    s_capsLock = false;
    for (uint32_t i = 0; i < kMaxDevices; ++i) s_devices[i] = DeviceState{};

    for (uint8_t i = 0; devices != nullptr && i < device_count && s_deviceCount < kMaxDevices; ++i) {
        if (devices[i].base == 0 || devices[i].size < 0x200) continue;
        DeviceState& device = s_devices[s_deviceCount];
        device.base = devices[i].base;
        device.irq = devices[i].irq;
        if (!setup_device(&device)) continue;
        device.active = true;
        ++s_deviceCount;
        if (device.keyboard) s_deviceInfo.is_keyboard = true;
        if (device.pointer) {
            s_deviceInfo.is_mouse = true;
            s_deviceInfo.is_tablet = s_deviceInfo.is_tablet || device.absolute;
            s_deviceInfo.abs_max_x = static_cast<uint32_t>(device.absMaxX);
            s_deviceInfo.abs_max_y = static_cast<uint32_t>(device.absMaxY);
        }
    }
    s_deviceInfo.present = s_deviceCount != 0 &&
        (s_deviceInfo.is_keyboard || s_deviceInfo.is_mouse);
    if (s_deviceInfo.present) {
        serial::puts("[guideXOS] input device discovery: OK\n");
        if (s_deviceInfo.is_keyboard) serial::puts("[guideXOS] keyboard device: initialized\n");
        if (s_deviceInfo.is_mouse) serial::puts("[guideXOS] pointing device: initialized\n");
    } else {
        serial::puts("[guideXOS] input device discovery: FAIL\n");
    }
}

void poll()
{
    for (uint8_t i = 0; i < s_deviceCount; ++i) poll_device(&s_devices[i]);
}

bool register_irq_handlers()
{
    bool registered = true;
    for (uint8_t i = 0; i < s_deviceCount; ++i) {
        DeviceState& device = s_devices[i];
        if (device.irq == 0 || device.irq >= 128 ||
            !kernel::irq::register_handler(device.irq, virtio_input_irq_handler, &device)) {
            registered = false;
        }
    }
    s_irqHandlersRegistered = registered && s_deviceCount != 0;
    return s_irqHandlersRegistered;
}

uint8_t active_device_count() { return s_deviceCount; }

uint32_t active_device_irq(uint8_t index)
{
    return index < s_deviceCount ? s_devices[index].irq : 0;
}

void shutdown()
{
    for (uint8_t i = 0; i < s_deviceCount; ++i) write32(s_devices[i].base, kMmioStatus, 0);
    s_deviceCount = 0;
    s_irqHandlersRegistered = false;
    s_deviceInfo = DeviceInfo{};
}

bool has_mouse() { return s_deviceInfo.present && s_deviceInfo.is_mouse; }
bool has_keyboard() { return s_deviceInfo.present && s_deviceInfo.is_keyboard; }
const DeviceInfo* get_device_info() { return &s_deviceInfo; }
const MouseState* get_mouse_state() { return &s_mouseState; }
bool mouse_dirty() { return s_mouseDirty; }
void mouse_clear_dirty() { s_mouseDirty = false; s_mouseState.x = 0; s_mouseState.y = 0; s_mouseState.wheel = 0; }
const KeyboardState* get_keyboard_state() { return &s_keyboardState; }
bool is_key_pressed(uint16_t keycode)
{
    for (uint8_t i = 0; i < s_keyboardState.keyCount; ++i) {
        if (s_keyboardState.keys[i] == static_cast<uint8_t>(keycode)) return true;
    }
    return false;
}
bool keyboard_dirty() { return s_keyboardDirty; }
void keyboard_clear_dirty() { s_keyboardDirty = false; }
uint64_t hardware_events_received() { return s_hardwareEvents; }
uint64_t hardware_pointer_events() { return s_pointerEvents; }
uint64_t hardware_button_events() { return s_buttonEvents; }
uint64_t hardware_keyboard_events() { return s_keyboardEvents; }
uint64_t malformed_events() { return s_malformedEvents; }
uint64_t device_interrupts_observed() { return s_interruptsObserved; }
uint64_t virtqueue_poll_count() { return s_pollCount; }
uint64_t virtqueue_drained_events() { return s_drainedEvents; }
uint64_t virtqueue_interrupt_status_acks() { return s_interruptStatusAcks; }

} // namespace virtio_input
} // namespace kernel
