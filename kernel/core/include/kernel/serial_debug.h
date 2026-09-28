//
// Minimal Serial Debug Output (COM1, 0x3F8)
//
// Used for diagnosing kernel boot and IRQ flow via QEMU's -serial stdio.
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_SERIAL_DEBUG_H
#define KERNEL_SERIAL_DEBUG_H

#include "kernel/types.h"
#include "kernel/arch.h"

// Define ARCH_HAS_PORT_IO for x86/amd64 architectures if not already defined
#ifndef ARCH_HAS_PORT_IO
#if defined(ARCH_X86) || defined(ARCH_AMD64) || defined(__i386__) || defined(__x86_64__)
    #define ARCH_HAS_PORT_IO 1
#else
    #define ARCH_HAS_PORT_IO 0
#endif
#endif

namespace kernel {
namespace serial {

#if ARCH_HAS_PORT_IO

static const uint16_t kCOM1 = 0x3F8;
static const uint16_t kDebugcon = 0xE9;
static bool s_txTimeoutReported = false;

inline void debugcon_putc(char c)
{
    arch::outb(kDebugcon, static_cast<uint8_t>(c));
}

inline void debugcon_puts(const char* s)
{
    if (!s) return;
    while (*s) debugcon_putc(*s++);
}

inline void init()
{
    arch::outb(kCOM1 + 1, 0x00); // Disable interrupts
    arch::outb(kCOM1 + 3, 0x80); // Enable DLAB
    arch::outb(kCOM1 + 0, 0x01); // Divisor = 1 (115200 baud)
    arch::outb(kCOM1 + 1, 0x00);
    arch::outb(kCOM1 + 3, 0x03); // 8N1
    arch::outb(kCOM1 + 2, 0xC7); // Enable FIFO
    arch::outb(kCOM1 + 4, 0x0B); // RTS/DSR set
}

inline void putc(char c)
{
    // Bound every UART write so early boot cannot hang forever on a missing
    // transmitter. Keep the dropped serial byte on QEMU debugcon for triage.
    for (uint32_t attempt = 0; attempt < 65536U; ++attempt) {
        if ((arch::inb(kCOM1 + 5) & 0x20) != 0) {
            arch::outb(kCOM1, static_cast<uint8_t>(c));
            return;
        }
    }
    if (!s_txTimeoutReported) {
        s_txTimeoutReported = true;
        debugcon_puts("\nP29J UART serial_tx_timeout; continuing on debugcon\n");
    }
    debugcon_putc(c);
}

// Best-effort output for application callbacks. Unlike putc(), this never
// waits for the UART; callers may retry under their own explicit work budget.
inline bool try_putc(char c)
{
    if ((arch::inb(kCOM1 + 5) & 0x20) == 0) return false;
    arch::outb(kCOM1, static_cast<uint8_t>(c));
    return true;
}

inline void puts(const char* s)
{
    while (*s) {
        if (*s == '\n') putc('\r');
        putc(*s++);
    }
}

inline void put_hex8(uint8_t v)
{
    const char hex[] = "0123456789ABCDEF";
    putc(hex[(v >> 4) & 0xF]);
    putc(hex[v & 0xF]);
}

inline void put_hex32(uint32_t v)
{
    for (int i = 28; i >= 0; i -= 4) {
        const char hex[] = "0123456789ABCDEF";
        putc(hex[(v >> i) & 0xF]);
    }
}

inline void put_hex16(uint16_t v)
{
    const char hex[] = "0123456789ABCDEF";
    putc(hex[(v >> 12) & 0xF]);
    putc(hex[(v >> 8) & 0xF]);
    putc(hex[(v >> 4) & 0xF]);
    putc(hex[v & 0xF]);
}

inline void put_hex64(uint64_t v)
{
    for (int i = 60; i >= 0; i -= 4) {
        const char hex[] = "0123456789ABCDEF";
        putc(hex[(v >> i) & 0xF]);
    }
}

#else

inline void init() { }
inline void debugcon_putc(char) { }
inline void debugcon_puts(const char*) { }
inline void putc(char) { }
inline bool try_putc(char) { return false; }
inline void puts(const char*) { }
inline void put_hex8(uint8_t) { }
inline void put_hex32(uint32_t) { }
inline void put_hex16(uint16_t) { }
inline void put_hex64(uint64_t) { }

#endif // ARCH_HAS_PORT_IO

} // namespace serial
} // namespace kernel

#endif // KERNEL_SERIAL_DEBUG_H
