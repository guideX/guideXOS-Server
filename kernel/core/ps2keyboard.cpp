// PS/2 Keyboard Driver Implementation
//
// Handles PS/2 keyboard input via IRQ1 and converts scancodes to ASCII/keycodes.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/ps2keyboard.h"
#include "include/kernel/arch.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/shell.h"

namespace kernel {
namespace ps2keyboard {

#if ARCH_HAS_PS2

// PS/2 controller ports
static const uint16_t kDataPort    = 0x60;
static const uint16_t kStatusPort  = 0x64;

// Key state
static ShiftState s_shiftState;
static bool s_ctrlDown = false;
static bool s_altDown = false;
static bool s_rightAltDown = false;
static bool s_f4Down = false;
static bool s_capsLock = false;
static const uint8_t kEventQueueCapacity = 64;
static const uint16_t kPairingKeyCapacity = 512;

struct EventQueue {
    KeyEvent events[kEventQueueCapacity];
    volatile uint8_t head;
    volatile uint8_t tail;
    volatile uint32_t receivedEvents;
    volatile uint32_t enqueuedEvents;
    volatile uint32_t dequeuedEvents;
    volatile uint32_t droppedEvents;
    volatile uint32_t overflowDrops;
    volatile uint32_t overflowKeyDownDrops;
    volatile uint32_t overflowKeyUpDrops;
    volatile uint32_t maximumOccupancy;
    volatile uint32_t wraparounds;
    volatile uint32_t keyDownEnqueued;
    volatile uint32_t keyUpEnqueued;
    volatile uint32_t keyDownDequeued;
    volatile uint32_t keyUpDequeued;
    volatile uint32_t pairedKeyTransitions;
    volatile uint32_t unmatchedKeyUps;
    volatile uint32_t outstandingKeyDowns;
    volatile uint32_t lastDequeuedSequence;
    volatile uint32_t sequenceGaps;
    bool deliveredKeyDown[kPairingKeyCapacity];
};

static EventQueue s_legacyQueue = {};
static EventQueue s_nativeQueue = {};
static uint32_t s_nextEventSequence = 1u;
static bool s_lastKeyAltF4Candidate = false;
static bool s_lastKeyAltLeftDown = false;
static bool s_lastKeyAltRightDown = false;

static void compiler_memory_barrier()
{
#if defined(__GNUC__) || defined(__clang__)
    __asm__ __volatile__("" ::: "memory");
#endif
}

static uint8_t queue_load_index(const volatile uint8_t* index)
{
#if defined(__GNUC__) || defined(__clang__)
    return __atomic_load_n(index, __ATOMIC_ACQUIRE);
#else
    compiler_memory_barrier();
    return *index;
#endif
}

static void queue_store_index(volatile uint8_t* index, uint8_t value)
{
#if defined(__GNUC__) || defined(__clang__)
    __atomic_store_n(index, value, __ATOMIC_RELEASE);
#else
    compiler_memory_barrier();
    *index = value;
#endif
}

static bool shift_down()
{
    return s_shiftState.isDown();
}

static uint32_t modifier_state()
{
    return (shift_down() ? KEY_MOD_SHIFT : 0u) |
        (s_ctrlDown ? KEY_MOD_CTRL : 0u) |
        ((s_altDown || s_rightAltDown) ? KEY_MOD_ALT : 0u);
}

static uint8_t queue_occupancy(const EventQueue& queue)
{
    const uint8_t head = queue_load_index(&queue.head);
    const uint8_t tail = queue_load_index(&queue.tail);
    return head >= tail
        ? static_cast<uint8_t>(head - tail)
        : static_cast<uint8_t>(kEventQueueCapacity - tail + head);
}

static void update_dequeued_pairing(EventQueue& queue, const KeyEvent& event)
{
    if (event.key >= kPairingKeyCapacity) return;
    if (event.action == KeyAction::Down) {
        ++queue.keyDownDequeued;
        if (!queue.deliveredKeyDown[event.key]) {
            queue.deliveredKeyDown[event.key] = true;
            ++queue.outstandingKeyDowns;
        }
    } else {
        ++queue.keyUpDequeued;
        if (queue.deliveredKeyDown[event.key]) {
            queue.deliveredKeyDown[event.key] = false;
            if (queue.outstandingKeyDowns > 0u) --queue.outstandingKeyDowns;
            ++queue.pairedKeyTransitions;
        } else {
            ++queue.unmatchedKeyUps;
        }
    }
}

static void enqueue_to_queue(EventQueue& queue, const KeyEvent& event)
{
    ++queue.receivedEvents;
    const uint8_t head = queue_load_index(&queue.head);
    const uint8_t tail = queue_load_index(&queue.tail);
    const uint8_t next = static_cast<uint8_t>((head + 1u) % kEventQueueCapacity);
    if (next == tail) {
        // Drop-new preserves the order and contents of older pending events.
        // If a release arrives while full and the newest pending entry is a
        // make for the same key, replace that final make with its release.
        // This is the one bounded recovery exception that reduces stuck-key
        // risk without displacing an unrelated key; the lost make remains
        // visible in overflow and sequence-gap counters.
        ++queue.overflowDrops;
        ++queue.droppedEvents;
        if (event.action == KeyAction::Up && head != tail) {
            const uint8_t last = static_cast<uint8_t>(
                (head + kEventQueueCapacity - 1u) % kEventQueueCapacity);
            if (queue.events[last].action == KeyAction::Down &&
                queue.events[last].key == event.key) {
                ++queue.overflowKeyDownDrops;
                queue.events[last] = event;
                compiler_memory_barrier();
                ++queue.enqueuedEvents;
                ++queue.keyUpEnqueued;
                return;
            }
        }
        if (event.action == KeyAction::Down) ++queue.overflowKeyDownDrops;
        else ++queue.overflowKeyUpDrops;
        return;
    }

    queue.events[head] = event;
    compiler_memory_barrier();
    queue_store_index(&queue.head, next);
    ++queue.enqueuedEvents;
    if (event.action == KeyAction::Down) ++queue.keyDownEnqueued;
    else ++queue.keyUpEnqueued;
    if (next == 0u) ++queue.wraparounds;
    const uint8_t occupancy = queue_occupancy(queue);
    if (occupancy > queue.maximumOccupancy) queue.maximumOccupancy = occupancy;
}

static void enqueue_event(uint32_t key, bool keyUp, bool altF4Candidate,
                          bool altF4LeftDown, bool altF4RightDown)
{
    if (key == 0) return;
    KeyEvent event;
    event.key = key;
    event.action = keyUp ? KeyAction::Up : KeyAction::Down;
    event.modifiers = modifier_state();
    event.sequence = s_nextEventSequence++;
    event.altF4Candidate = altF4Candidate;
    event.altLeftDown = altF4LeftDown;
    event.altRightDown = altF4RightDown;

    enqueue_to_queue(s_legacyQueue, event);
    // Native ELF transitions remain independent from the legacy desktop
    // consumer and have separate overflow and delivery diagnostics.
    enqueue_to_queue(s_nativeQueue, event);
}

static bool dequeue_event(EventQueue& queue, KeyEvent* outEvent)
{
    const uint8_t tail = queue_load_index(&queue.tail);
    if (tail == queue_load_index(&queue.head)) return false;
    compiler_memory_barrier();
    const KeyEvent event = queue.events[tail];
    compiler_memory_barrier();
    queue_store_index(&queue.tail,
                      static_cast<uint8_t>((tail + 1u) % kEventQueueCapacity));
    ++queue.dequeuedEvents;
    if (queue.lastDequeuedSequence != 0u && event.sequence > queue.lastDequeuedSequence + 1u) {
        queue.sequenceGaps += event.sequence - queue.lastDequeuedSequence - 1u;
    }
    queue.lastDequeuedSequence = event.sequence;
    update_dequeued_pairing(queue, event);
    if (outEvent) *outEvent = event;
    return true;
}

static void reset_queue(EventQueue& queue)
{
    queue_store_index(&queue.head, 0u);
    queue_store_index(&queue.tail, 0u);
    queue.receivedEvents = 0u;
    queue.enqueuedEvents = 0u;
    queue.dequeuedEvents = 0u;
    queue.droppedEvents = 0u;
    queue.overflowDrops = 0u;
    queue.overflowKeyDownDrops = 0u;
    queue.overflowKeyUpDrops = 0u;
    queue.maximumOccupancy = 0u;
    queue.wraparounds = 0u;
    queue.keyDownEnqueued = 0u;
    queue.keyUpEnqueued = 0u;
    queue.keyDownDequeued = 0u;
    queue.keyUpDequeued = 0u;
    queue.pairedKeyTransitions = 0u;
    queue.unmatchedKeyUps = 0u;
    queue.outstandingKeyDowns = 0u;
    queue.lastDequeuedSequence = 0u;
    queue.sequenceGaps = 0u;
    for (uint16_t key = 0u; key < kPairingKeyCapacity; ++key) {
        queue.deliveredKeyDown[key] = false;
    }
}

static void copy_diagnostics(const EventQueue& queue, QueueDiagnostics* outDiagnostics)
{
    if (!outDiagnostics) return;
    outDiagnostics->receivedEvents = queue.receivedEvents;
    outDiagnostics->enqueuedEvents = queue.enqueuedEvents;
    outDiagnostics->dequeuedEvents = queue.dequeuedEvents;
    outDiagnostics->droppedEvents = queue.droppedEvents;
    outDiagnostics->overflowDrops = queue.overflowDrops;
    outDiagnostics->overflowKeyDownDrops = queue.overflowKeyDownDrops;
    outDiagnostics->overflowKeyUpDrops = queue.overflowKeyUpDrops;
    outDiagnostics->maximumOccupancy = queue.maximumOccupancy;
    outDiagnostics->currentOccupancy = queue_occupancy(queue);
    outDiagnostics->wraparounds = queue.wraparounds;
    outDiagnostics->keyDownEnqueued = queue.keyDownEnqueued;
    outDiagnostics->keyUpEnqueued = queue.keyUpEnqueued;
    outDiagnostics->keyDownDequeued = queue.keyDownDequeued;
    outDiagnostics->keyUpDequeued = queue.keyUpDequeued;
    outDiagnostics->pairedKeyTransitions = queue.pairedKeyTransitions;
    outDiagnostics->unmatchedKeyUps = queue.unmatchedKeyUps;
    outDiagnostics->outstandingKeyDowns = queue.outstandingKeyDowns;
    outDiagnostics->sequenceGaps = queue.sequenceGaps;
}

#if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
static void log_key_transition(const char* label, uint8_t scancode, bool keyUp, bool extended)
{
    serial::puts("[kbd] ");
    serial::puts(label);
    serial::puts(keyUp ? " up" : " down");
    serial::puts(" raw=");
    if (extended) {
        serial::puts("E0 ");
    }
    if (keyUp) {
        serial::puts("F0 ");
    }
    serial::puts("0x");
    serial::put_hex8(scancode);
    serial::puts(" ext=");
    serial::puts(extended ? "1" : "0");
    serial::puts(" leftAlt=");
    serial::puts(s_altDown ? "1" : "0");
    serial::puts(" rightAlt=");
    serial::puts(s_rightAltDown ? "1" : "0");
    serial::puts(" f4=");
    serial::puts(s_f4Down ? "1" : "0");
    serial::putc('\n');
}
#endif

// Scancode set 2 to ASCII mapping (US QWERTY keyboard layout)
// Many emulators use scancode set 2 (translated mode)
// Index = scancode, value = ASCII (lowercase)
static const char s_scancodeToAscii[256] = {
//   0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,  '\t',  '`',   0,   // 0x00-0x0F
    0,    0,    0,    0,    0,   'q',  '1',   0,    0,    0,   'z',  's',  'a',  'w',  '2',   0,   // 0x10-0x1F
    0,   'c',  'x',  'd',  'e',  '4',  '3',   0,    0,   ' ',  'v',  'f',  't',  'r',  '5',   0,   // 0x20-0x2F
    0,   'n',  'b',  'h',  'g',  'y',  '6',   0,    0,    0,   'm',  'j',  'u',  '7',  '8',   0,   // 0x30-0x3F
    0,   ',',  'k',  'i',  'o',  '0',  '9',   0,    0,   '.',  '/',  'l',  ';',  'p',  '-',   0,   // 0x40-0x4F
    0,    0,  '\'',   0,   '[',  '=',   0,    0,    0,    0,  '\n',  ']',   0,  '\\',   0,    0,   // 0x50-0x5F
    0,    0,    0,    0,    0,    0,  '\b',   0,    0,   '1',   0,   '4',  '7',   0,    0,    0,   // 0x60-0x6F
   '0',  '.',  '2',  '5',  '6',  '8',  27,    0,    0,   '+',  '3',  '-',  '*',  '9',   0,    0,   // 0x70-0x7F
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   // 0x80-0x8F
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   // 0x90-0x9F
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   // 0xA0-0xAF
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   // 0xB0-0xBF
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   // 0xC0-0xCF
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   // 0xD0-0xDF
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   // 0xE0-0xEF
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0    // 0xF0-0xFF
};

// Shifted characters (scancode set 2)
static const char s_scancodeToAsciiShift[256] = {
//   0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,  '\t',  '~',   0,   // 0x00-0x0F
    0,    0,    0,    0,    0,   'Q',  '!',   0,    0,    0,   'Z',  'S',  'A',  'W',  '@',   0,   // 0x10-0x1F
    0,   'C',  'X',  'D',  'E',  '$',  '#',   0,    0,   ' ',  'V',  'F',  'T',  'R',  '%',   0,   // 0x20-0x2F
    0,   'N',  'B',  'H',  'G',  'Y',  '^',   0,    0,    0,   'M',  'J',  'U',  '&',  '*',   0,   // 0x30-0x3F
    0,   '<',  'K',  'I',  'O',  ')',  '(',   0,    0,   '>',  '?',  'L',  ':',  'P',  '_',   0,   // 0x40-0x4F
    0,    0,   '"',   0,   '{',  '+',   0,    0,    0,    0,  '\n',  '}',   0,   '|',   0,    0,   // 0x50-0x5F
    0,    0,    0,    0,    0,    0,  '\b',   0,    0,   '1',   0,   '4',  '7',   0,    0,    0,   // 0x60-0x6F
   '0',  '.',  '2',  '5',  '6',  '8',  27,    0,    0,   '+',  '3',  '-',  '*',  '9',   0,    0,   // 0x70-0x7F
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   // 0x80-0x8F
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   // 0x90-0x9F
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   // 0xA0-0xAF
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   // 0xB0-0xBF
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   // 0xC0-0xCF
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   // 0xD0-0xDF
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   // 0xE0-0xEF
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0    // 0xF0-0xFF
};

// Special key codes (returned for non-ASCII keys)
static const uint32_t KEY_UP     = 0x100;
static const uint32_t KEY_DOWN   = 0x101;
static const uint32_t KEY_LEFT   = 0x102;
static const uint32_t KEY_RIGHT  = 0x103;
static const uint32_t KEY_HOME   = 0x104;
static const uint32_t KEY_END    = 0x105;
static const uint32_t KEY_DELETE = 0x106;
static const uint32_t KEY_PGUP   = 0x108;
static const uint32_t KEY_PGDN   = 0x109;
static const uint32_t KEY_F1     = 0x110;
static const uint32_t KEY_F11    = 0x11A;
static const uint32_t KEY_F12    = 0x11B;

// Scancode set 2 modifier scancodes
static const uint8_t SC2_LSHIFT = 0x12;
static const uint8_t SC2_RSHIFT = 0x59;
static const uint8_t SC2_LCTRL  = 0x14;
static const uint8_t SC2_LALT   = 0x11;
static const uint8_t SC2_CAPS   = 0x58;

// Extended scancode flag and break code flag
static bool s_extendedKey = false;
static bool s_breakCode = false;  // Scancode set 2 uses 0xF0 prefix for key release

void init()
{
    serial::puts("[PS2KB] Initializing PS/2 keyboard...\n");
    
    // Keyboard should already be initialized by BIOS/UEFI
    // Just clear any pending data
    while (arch::inb(kStatusPort) & 0x01) {
        arch::inb(kDataPort);
    }
    
    s_shiftState = ShiftState();
    s_ctrlDown = false;
    s_altDown = false;
    s_rightAltDown = false;
    s_f4Down = false;
    s_lastKeyAltF4Candidate = false;
    s_lastKeyAltLeftDown = false;
    s_lastKeyAltRightDown = false;
    s_capsLock = false;
    s_extendedKey = false;
    s_breakCode = false;
    s_nextEventSequence = 1u;
    reset_queue(s_legacyQueue);
    reset_queue(s_nativeQueue);
    
    serial::puts("[PS2KB] Keyboard initialized (scancode set 2)\n");
#if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
    serial::puts("[kbd] decoder=set2 controller-translation-off\n");
#endif
}

static void process_scancode(uint8_t scancode)
{
    // Scancode set 2: Handle special prefixes
    // 0xE0 = extended key prefix
    // 0xF0 = break (key release) prefix
    if (scancode == 0xE0) {
        s_extendedKey = true;
        return;
    }
    if (scancode == 0xF0) {
        s_breakCode = true;
        return;
    }
    
    bool keyUp = s_breakCode;
    s_breakCode = false;  // Reset for next scancode
    bool altF4Candidate = false;
    bool altF4LeftDown = false;
    bool altF4RightDown = false;
    
    // Handle modifier keys (scancode set 2 values)
    if (scancode == SC2_LSHIFT || scancode == SC2_RSHIFT) {
        if (scancode == SC2_LSHIFT) s_shiftState.setLeftDown(!keyUp);
        else s_shiftState.setRightDown(!keyUp);
#if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
        log_key_transition(scancode == SC2_LSHIFT ? "shift-left" : "shift-right", scancode, keyUp, s_extendedKey);
#endif
        s_extendedKey = false;
        return;
    }
    if (scancode == SC2_LCTRL) {
        s_ctrlDown = !keyUp;
#if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
        log_key_transition("ctrl-left", scancode, keyUp, s_extendedKey);
#endif
        s_extendedKey = false;
        return;
    }
    if (scancode == SC2_LALT) {
        if (s_extendedKey) {
            s_rightAltDown = !keyUp;
#if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
            log_key_transition("alt-right", scancode, keyUp, true);
#endif
        } else {
            s_altDown = !keyUp;
#if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
            log_key_transition("alt-left", scancode, keyUp, false);
#endif
        }
        s_extendedKey = false;
        return;
    }
    if (scancode == SC2_CAPS && !keyUp) {  // Caps Lock (toggle on press)
        s_capsLock = !s_capsLock;
        s_extendedKey = false;
        return;
    }
    if (scancode == 0x0C) {  // F4
        const bool wasDown = s_f4Down;
        s_f4Down = !keyUp;
#if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
        log_key_transition("f4", scancode, keyUp, s_extendedKey);
#endif
        if (!keyUp && !wasDown && (s_altDown || s_rightAltDown)) {
            altF4Candidate = true;
            altF4LeftDown = s_altDown;
            altF4RightDown = s_rightAltDown;
#if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
            serial::puts("[kbd] shortcut alt-f4 candidate armed leftAlt=");
            serial::puts(altF4LeftDown ? "1" : "0");
            serial::puts(" rightAlt=");
            serial::puts(altF4RightDown ? "1" : "0");
            serial::putc('\n');
#endif
        }
        s_extendedKey = false;
        if (keyUp) {
            return;
        }
    }
    
    uint32_t key = 0;
    
    // Handle extended keys (arrow keys, etc.) - scancode set 2 values
    if (s_extendedKey) {
        switch (scancode) {
            case 0x75: key = KEY_UP; break;
            case 0x72: key = KEY_DOWN; break;
            case 0x6B: key = KEY_LEFT; break;
            case 0x74: key = KEY_RIGHT; break;
            case 0x6C: key = KEY_HOME; break;
            case 0x69: key = KEY_END; break;
            case 0x71: key = KEY_DELETE; break;
            case 0x7D: key = KEY_PGUP; break;
            case 0x7A: key = KEY_PGDN; break;
            case 0x1F: key = shell::KEY_SUPER; break; // Left Windows / Super
            case 0x27: key = shell::KEY_SUPER; break; // Right Windows / Super
            default: break;
        }
        s_extendedKey = false;
    } else {
        // Handle function keys (scancode set 2)
        // F1=0x05, F2=0x06, F3=0x04, F4=0x0C, F5=0x03, F6=0x0B, F7=0x83, F8=0x0A, F9=0x01, F10=0x09
        switch (scancode) {
            case 0x05: key = KEY_F1; break;
            case 0x06: key = KEY_F1 + 1; break;
            case 0x04: key = KEY_F1 + 2; break;
            case 0x0C: key = KEY_F1 + 3; break;
            case 0x03: key = KEY_F1 + 4; break;
            case 0x0B: key = KEY_F1 + 5; break;
            case 0x83: key = KEY_F1 + 6; break;
            case 0x0A: key = KEY_F1 + 7; break;
            case 0x01: key = KEY_F1 + 8; break;
            case 0x09: key = KEY_F1 + 9; break;
            case 0x78: key = KEY_F11; break;
            case 0x07: key = KEY_F12; break;
            default:
                // Regular ASCII keys
                bool shift = shift_down();
                char c = s_scancodeToAscii[scancode];
                if (s_capsLock && c >= 'a' && c <= 'z') {
                    shift = !shift;
                }
                key = shift ? s_scancodeToAsciiShift[scancode] : s_scancodeToAscii[scancode];
                break;
        }
    }
    
    if (key != 0) {
        enqueue_event(key, keyUp, altF4Candidate, altF4LeftDown, altF4RightDown);
    }
}

void handle_data_byte(uint8_t scancode)
{
    process_scancode(scancode);
}

void irq_handler()
{
    const uint8_t status = arch::inb(kStatusPort);
    if (!(status & 0x01u)) return;
    if (status & 0x20u) return; // IRQ1 leaves auxiliary data for IRQ12.
    process_scancode(arch::inb(kDataPort));
}

bool has_key()
{
    uint8_t index = queue_load_index(&s_legacyQueue.tail);
    const uint8_t head = queue_load_index(&s_legacyQueue.head);
    compiler_memory_barrier();
    while (index != head) {
        if (s_legacyQueue.events[index].action == KeyAction::Down) return true;
        index = static_cast<uint8_t>((index + 1u) % kEventQueueCapacity);
    }
    return false;
}

uint32_t get_key()
{
    return get_key_with_modifiers(nullptr);
}

uint32_t get_key_with_modifiers(uint32_t* outModifiers)
{
    if (outModifiers) *outModifiers = 0u;
    KeyEvent event;
    while (dequeue_event(s_legacyQueue, &event)) {
        if (event.action == KeyAction::Up) continue;

        s_lastKeyAltF4Candidate = event.altF4Candidate;
        s_lastKeyAltLeftDown = event.altLeftDown;
        s_lastKeyAltRightDown = event.altRightDown;
        if (outModifiers) *outModifiers = event.modifiers;

        // The legacy desktop API dispatches key-downs only. Drain releases
        // that follow this make now so idle typing cannot fill the ring with
        // ignored breaks. Stop before a later make to preserve FIFO order.
        while (queue_load_index(&s_legacyQueue.tail) !=
               queue_load_index(&s_legacyQueue.head)) {
            const uint8_t tail = queue_load_index(&s_legacyQueue.tail);
            compiler_memory_barrier();
            if (s_legacyQueue.events[tail].action != KeyAction::Up) break;
            dequeue_event(s_legacyQueue, nullptr);
        }
        return event.key;
    }
    return 0u;
}

bool has_event()
{
    return queue_load_index(&s_nativeQueue.tail) !=
        queue_load_index(&s_nativeQueue.head);
}

bool get_event(KeyEvent* outEvent)
{
    if (!outEvent) return false;
    return dequeue_event(s_nativeQueue, outEvent);
}

void clear()
{
    s_legacyQueue.droppedEvents += queue_occupancy(s_legacyQueue);
    queue_store_index(&s_legacyQueue.tail, queue_load_index(&s_legacyQueue.head));
    s_nativeQueue.droppedEvents += queue_occupancy(s_nativeQueue);
    queue_store_index(&s_nativeQueue.tail, queue_load_index(&s_nativeQueue.head));
    s_lastKeyAltF4Candidate = false;
    s_lastKeyAltLeftDown = false;
    s_lastKeyAltRightDown = false;
}

void get_legacy_queue_diagnostics(QueueDiagnostics* outDiagnostics)
{
    copy_diagnostics(s_legacyQueue, outDiagnostics);
}

void get_native_queue_diagnostics(QueueDiagnostics* outDiagnostics)
{
    copy_diagnostics(s_nativeQueue, outDiagnostics);
}

bool is_ctrl_down()
{
    return s_ctrlDown;
}

bool is_shift_down()
{
    return shift_down();
}

bool is_alt_down()
{
    return s_altDown || s_rightAltDown;
}

bool is_left_alt_down()
{
    return s_altDown;
}

bool is_right_alt_down()
{
    return s_rightAltDown;
}

bool is_f4_down()
{
    return s_f4Down;
}

bool was_alt_f4_shortcut_candidate()
{
    return s_lastKeyAltF4Candidate;
}

bool last_key_alt_left_down()
{
    return s_lastKeyAltLeftDown;
}

bool last_key_alt_right_down()
{
    return s_lastKeyAltRightDown;
}

#else
// Stub implementation for non-x86 architectures
void init() {}
void irq_handler() {}
void handle_data_byte(uint8_t) {}
bool has_key() { return false; }
uint32_t get_key() { return 0; }
uint32_t get_key_with_modifiers(uint32_t* outModifiers) { if (outModifiers) *outModifiers = 0u; return 0; }
bool has_event() { return false; }
bool get_event(KeyEvent*) { return false; }
void get_legacy_queue_diagnostics(QueueDiagnostics* out) { if (out) *out = QueueDiagnostics{}; }
void get_native_queue_diagnostics(QueueDiagnostics* out) { if (out) *out = QueueDiagnostics{}; }
void clear() {}
bool is_ctrl_down() { return false; }
bool is_shift_down() { return false; }
bool is_alt_down() { return false; }
bool is_left_alt_down() { return false; }
bool is_right_alt_down() { return false; }
bool is_f4_down() { return false; }
bool was_alt_f4_shortcut_candidate() { return false; }
bool last_key_alt_left_down() { return false; }
bool last_key_alt_right_down() { return false; }
#endif

} // namespace ps2keyboard
} // namespace kernel
