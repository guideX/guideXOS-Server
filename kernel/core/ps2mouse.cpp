//
// PS/2 Mouse Driver Implementation
//
// Implements the standard PS/2 mouse protocol with IntelliMouse
// scroll-wheel extension (4-byte packets).
//
// Ported from guideXOS Legacy PS2Mouse.cs
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/ps2mouse.h"
#include "include/kernel/arch.h"
#include "include/kernel/serial_debug.h"

namespace kernel {
namespace ps2mouse {

#if ARCH_HAS_PS2

// PS/2 controller ports
static const uint16_t kDataPort    = 0x60;
static const uint16_t kCommandPort = 0x64;

// PS/2 mouse commands
static const uint8_t kSetDefaults        = 0xF6;
static const uint8_t kEnableDataReporting = 0xF4;
static const uint8_t kSetSampleRate      = 0xF3;
static const uint8_t kGetDeviceId        = 0xF2;

// State
static int32_t  s_mouseX    = 0;
static int32_t  s_mouseY    = 0;
static uint8_t  s_buttons   = 0;
static int8_t   s_scrollZ   = 0;
static bool     s_dirty     = false;

static int      s_phase     = 0;     // packet assembly phase
static uint8_t  s_packet[4] = {0};   // 4-byte packet buffer (IntelliMouse)
static bool     s_hasWheel  = false;

static int32_t  s_screenW   = 1024;
static int32_t  s_screenH   = 768;

// Touchpad filtering
// kNoiseThreshold = 0 disables filtering (important for QEMU where
// small deltas of ±1 are common and must not be discarded)
static const int kNoiseThreshold   = 0;
static const int kMaxDeltaPerPacket = 50;

// Small I/O delay for PS/2 controller timing
static void io_wait()
{
    // Writing to port 0x80 is a common way to introduce a small delay
    // (approximately 1 microsecond on older hardware)
    arch::outb(0x80, 0);
}

// Wait until the PS/2 controller input buffer is clear (ready for command)
static void wait_write()
{
    for (int i = 0; i < 100000; ++i) {
        io_wait();
        if ((arch::inb(kCommandPort) & 0x02) == 0) return;
    }
}

// Wait until the PS/2 controller output buffer has data
static void wait_read()
{
    for (int i = 0; i < 100000; ++i) {
        io_wait();
        if ((arch::inb(kCommandPort) & 0x01) != 0) return;
    }
}

// Send a command byte to the PS/2 controller command port
static void controller_cmd(uint8_t cmd)
{
    wait_write();
    arch::outb(kCommandPort, cmd);
    io_wait();
}

// Send a byte to the mouse (via PS/2 controller's second port)
static void mouse_write(uint8_t data)
{
    wait_write();
    arch::outb(kCommandPort, 0xD4); // tell controller next byte goes to mouse
    io_wait();
    wait_write();
    arch::outb(kDataPort, data);
    io_wait();
    // Read ACK
    wait_read();
    arch::inb(kDataPort);
}

// Read a byte from the PS/2 data port
static uint8_t mouse_read()
{
    wait_read();
    return arch::inb(kDataPort);
}

// Attempt to enable IntelliMouse mode (scroll wheel, 4-byte packets)
// Magic sequence: set sample rate to 200, 100, 80, then query device ID
static bool try_enable_intellimouse()
{
    mouse_write(kSetSampleRate); mouse_write(200);
    mouse_write(kSetSampleRate); mouse_write(100);
    mouse_write(kSetSampleRate); mouse_write(80);
    mouse_write(kGetDeviceId);
    uint8_t id = mouse_read();
    return (id == 3); // IntelliMouse returns device ID 3
}

// Clamp helper (C++14 compatible, no <algorithm>)
static int32_t clamp(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

void init(uint32_t screen_width, uint32_t screen_height)
{
    s_screenW = static_cast<int32_t>(screen_width);
    s_screenH = static_cast<int32_t>(screen_height);
    s_mouseX  = s_screenW / 2;
    s_mouseY  = s_screenH / 2;
    s_phase   = 0;

    serial::puts("[PS2] Starting PS/2 mouse init\n");

    // Disable both PS/2 ports while we configure
    controller_cmd(0xAD); // disable keyboard port
    controller_cmd(0xA7); // disable mouse port

    // Flush any data in the output buffer
    while (arch::inb(kCommandPort) & 0x01) {
        arch::inb(kDataPort);
        io_wait();
    }

    // Read current controller config
    controller_cmd(0x20);
    uint8_t config = mouse_read();
    serial::puts("[PS2] Controller config before: 0x");
    serial::put_hex8(config);
    serial::putc('\n');

    // Enable IRQ1 (keyboard) and IRQ12 (mouse), disable translation
    config |= 0x03;       // bit 0 = IRQ1 enable, bit 1 = IRQ12 enable
    config &= ~0x20;      // bit 5 = disable mouse clock (clear it)
    config &= ~0x40;      // bit 6 = translation (disable for clean scancodes)

    // Write updated config
    controller_cmd(0x60);
    wait_write();
    arch::outb(kDataPort, config);
    io_wait();

    serial::puts("[PS2] Controller config after: 0x");
    serial::put_hex8(config);
    serial::putc('\n');

    // Enable the auxiliary (mouse) PS/2 port
    controller_cmd(0xA8);

    // Re-enable keyboard port
    controller_cmd(0xAE);

    // Send mouse reset (0xFF) for a clean state after UEFI
    serial::puts("[PS2] Sending mouse reset (0xFF)...\n");
    wait_write();
    arch::outb(kCommandPort, 0xD4);
    io_wait();
    wait_write();
    arch::outb(kDataPort, 0xFF);
    io_wait();
    // Wait for ACK (0xFA) then self-test result (0xAA)
    wait_read();
    uint8_t ack = arch::inb(kDataPort);
    serial::puts("[PS2] Reset ACK: 0x");
    serial::put_hex8(ack);
    serial::putc('\n');
    wait_read();
    uint8_t selftest = arch::inb(kDataPort);
    serial::puts("[PS2] Self-test result: 0x");
    serial::put_hex8(selftest);
    serial::putc('\n');
    // Read device ID (0x00 for standard mouse)
    wait_read();
    uint8_t devid = arch::inb(kDataPort);
    serial::puts("[PS2] Device ID: 0x");
    serial::put_hex8(devid);
    serial::putc('\n');

    // Reset & configure the mouse
    mouse_write(kSetDefaults);
    s_hasWheel = try_enable_intellimouse();
    serial::puts("[PS2] IntelliMouse (scroll wheel): ");
    serial::puts(s_hasWheel ? "yes\n" : "no\n");
    mouse_write(kSetSampleRate); mouse_write(100); // 100 samples/sec
    mouse_write(kEnableDataReporting);

    // Flush any stale data left in the PS/2 output buffer
    for (int i = 0; i < 16; ++i) {
        io_wait();
        if ((arch::inb(kCommandPort) & 0x01) == 0) break;
        arch::inb(kDataPort);
    }

    s_phase = 0;
    serial::puts("[PS2] Mouse init complete\n");
}

static uint32_t s_irqCount = 0;

void irq_handler()
{
    // Verify data is available and is from the auxiliary (mouse) port.
    // Status register bit 0 = output buffer full, bit 5 = auxiliary data.
    uint8_t status = arch::inb(kCommandPort);
    if ((status & 0x01) == 0) return;  // no data available
    if ((status & 0x20) == 0) {
        // Data is from keyboard, not mouse — read and discard to clear buffer
        arch::inb(kDataPort);
        return;
    }

    // Log first few IRQs for debugging
    s_irqCount++;
    if (s_irqCount <= 5) {
        serial::puts("[PS2] IRQ12 #");
        serial::put_hex32(s_irqCount);
        serial::puts(" status=0x");
        serial::put_hex8(status);
        serial::putc('\n');
    }

    uint8_t d = arch::inb(kDataPort);

    if (s_phase == 0) {
        // Waiting for first byte with bit 3 set (always-1 bit)
        if ((d & 0x08) != 0) {
            s_packet[0] = d;
            s_phase = 1;
        }
    } else if (s_phase == 1) {
        s_packet[1] = d;
        s_phase = 2;
    } else if (s_phase == 2) {
        s_packet[2] = d;
        if (!s_hasWheel) {
            // 3-byte standard mouse — process now
            s_phase = 0;
        } else {
            s_phase = 3; // wait for 4th byte (scroll)
            return;
        }
    } else if (s_phase == 3) {
        s_packet[3] = d;
        s_phase = 0;
    } else {
        s_phase = 0;
        return;
    }

    // Only process when we've collected a full packet
    if (s_phase != 0) return;

    // Validate: bit 3 of byte 0 must be set
    if ((s_packet[0] & 0x08) == 0) return;

    // Extract sign bits
    bool xSign = (s_packet[0] & 0x10) != 0;
    bool ySign = (s_packet[0] & 0x20) != 0;

    // Buttons
    uint8_t oldButtons = s_buttons;
    s_buttons = 0;
    if (s_packet[0] & 0x01) s_buttons |= Left;
    if (s_packet[0] & 0x02) s_buttons |= Right;
    if (s_packet[0] & 0x04) s_buttons |= Middle;

    // Delta X/Y as signed
    int32_t dx = static_cast<int32_t>(s_packet[1]);
    int32_t dy = static_cast<int32_t>(s_packet[2]);
    if (xSign) dx -= 256;
    if (ySign) dy -= 256;
    dy = -dy; // invert Y for screen coordinates

    // Touchpad-style filtering
    dx = clamp(dx, -kMaxDeltaPerPacket, kMaxDeltaPerPacket);
    dy = clamp(dy, -kMaxDeltaPerPacket, kMaxDeltaPerPacket);
    if (iabs(dx) < kNoiseThreshold) dx = 0;
    if (iabs(dy) < kNoiseThreshold) dy = 0;

    // Discard corrupted packets
    if (iabs(dx) >= kMaxDeltaPerPacket && iabs(dy) >= kMaxDeltaPerPacket) return;

    // Update position
    if (dx != 0 || dy != 0 || s_buttons != oldButtons) s_dirty = true;
    s_mouseX = clamp(s_mouseX + dx, 0, s_screenW - 1);
    s_mouseY = clamp(s_mouseY + dy, 0, s_screenH - 1);

    // Scroll wheel (4th byte, signed)
    if (s_hasWheel) {
        s_scrollZ = static_cast<int8_t>(s_packet[3]);
    }
}

int32_t get_x()       { return s_mouseX; }
int32_t get_y()       { return s_mouseY; }
uint8_t get_buttons() { return s_buttons; }
int8_t  get_scroll_delta() { int8_t z = s_scrollZ; s_scrollZ = 0; return z; }
bool    is_dirty()    { return s_dirty; }
void    clear_dirty() { s_dirty = false; }

#else // !ARCH_HAS_PS2

// Stub implementation for platforms without PS/2 hardware
void    init(uint32_t, uint32_t) { }
void    irq_handler()           { }
int32_t get_x()                 { return 0; }
int32_t get_y()                 { return 0; }
uint8_t get_buttons()           { return 0; }
int8_t  get_scroll_delta()      { return 0; }
bool    is_dirty()              { return false; }
void    clear_dirty()           { }

#endif // ARCH_HAS_PS2

} // namespace ps2mouse
} // namespace kernel
