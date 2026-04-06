//
// MIPS64 Serial Console Implementation
//
// Provides early serial console output via 16550-compatible UART.
// Works on both QEMU malta and virt machine types.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/serial_console.h"

#if defined(_MSC_VER)
#include <intrin.h>
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace mips64 {
namespace serial_console {

namespace {

// Current UART base address (determined at init)
static volatile uint8_t* s_uart_base = nullptr;

// ================================================================
// MMIO helpers
// ================================================================

static inline uint8_t mmio_read8(volatile uint8_t* addr)
{
#if GXOS_MSVC_STUB
    (void)addr;
    return 0;
#else
    return *addr;
#endif
}

static inline void mmio_write8(volatile uint8_t* addr, uint8_t value)
{
#if GXOS_MSVC_STUB
    (void)addr;
    (void)value;
#else
    *addr = value;
#endif
}

// ================================================================
// UART register access
// ================================================================

static inline uint8_t uart_read(uint32_t reg)
{
    if (!s_uart_base) return 0;
    return mmio_read8(s_uart_base + reg);
}

static inline void uart_write(uint32_t reg, uint8_t value)
{
    if (!s_uart_base) return;
    mmio_write8(s_uart_base + reg, value);
}

// ================================================================
// UART probe
// ================================================================

static bool probe_uart(uint64_t base)
{
#if GXOS_MSVC_STUB
    (void)base;
    return true;  // Always "found" in stub mode
#else
    volatile uint8_t* uart = reinterpret_cast<volatile uint8_t*>(base);
    
    // Save current scratch register value
    uint8_t scratch = mmio_read8(uart + UART_SCR);
    
    // Write test pattern
    mmio_write8(uart + UART_SCR, 0x5A);
    if (mmio_read8(uart + UART_SCR) != 0x5A) {
        mmio_write8(uart + UART_SCR, scratch);
        return false;
    }
    
    // Write inverse pattern
    mmio_write8(uart + UART_SCR, 0xA5);
    if (mmio_read8(uart + UART_SCR) != 0xA5) {
        mmio_write8(uart + UART_SCR, scratch);
        return false;
    }
    
    // Restore scratch register
    mmio_write8(uart + UART_SCR, scratch);
    return true;
#endif
}

} // anonymous namespace

// ================================================================
// Public interface
// ================================================================

void init()
{
#if GXOS_MSVC_STUB
    // In MSVC stub mode, just set a non-null base
    s_uart_base = reinterpret_cast<volatile uint8_t*>(0x1);
    return;
#else
    // Try to detect UART
    // On MIPS, we use kseg1 addresses (0xFFFFFFFF_Axxxxxxx) for uncached MMIO
    // or physical addresses with bit 29 set for kseg1 in 32-bit compat mode
    
    // For 64-bit kernel mode, we can use XKPHYS addresses:
    // 0x9000000000000000 | phys_addr for uncached access
    
    // Try malta UART first (physical 0x1F000900)
    // In kseg1: 0xFFFFFFFF_BF000900
    uint64_t malta_kseg1 = 0xFFFFFFFFBF000900ULL;
    if (probe_uart(malta_kseg1)) {
        s_uart_base = reinterpret_cast<volatile uint8_t*>(malta_kseg1);
    } else {
        // Try virt UART (physical 0x10000000)
        // In kseg1: 0xFFFFFFFF_B0000000
        uint64_t virt_kseg1 = 0xFFFFFFFFB0000000ULL;
        if (probe_uart(virt_kseg1)) {
            s_uart_base = reinterpret_cast<volatile uint8_t*>(virt_kseg1);
        } else {
            // Try XKPHYS uncached (0x90000000_1F000900 for malta)
            uint64_t malta_xkphys = 0x900000001F000900ULL;
            if (probe_uart(malta_xkphys)) {
                s_uart_base = reinterpret_cast<volatile uint8_t*>(malta_xkphys);
            } else {
                // Try XKPHYS for virt
                uint64_t virt_xkphys = 0x9000000010000000ULL;
                if (probe_uart(virt_xkphys)) {
                    s_uart_base = reinterpret_cast<volatile uint8_t*>(virt_xkphys);
                }
            }
        }
    }
    
    if (!s_uart_base) {
        // No UART found - can't output anything
        return;
    }
    
    // Initialize UART: 8N1, 115200 baud (assuming 1.8432 MHz clock)
    // Disable interrupts
    uart_write(UART_IER, 0x00);
    
    // Set DLAB to access divisor
    uart_write(UART_LCR, LCR_DLAB);
    
    // Set divisor for 115200 baud (divisor = 1 for 1.8432 MHz clock)
    // QEMU doesn't really care about baud rate
    uart_write(UART_DLL, 0x01);
    uart_write(UART_DLH, 0x00);
    
    // 8 data bits, 1 stop bit, no parity
    uart_write(UART_LCR, LCR_WLS_8);
    
    // Enable and clear FIFOs, set trigger level
    uart_write(UART_FCR, FCR_ENABLE | FCR_RXRST | FCR_TXRST | FCR_TRIGGER_14);
    
    // Set DTR, RTS, and OUT2 (enables interrupts on PC-compatible systems)
    uart_write(UART_MCR, MCR_DTR | MCR_RTS | MCR_OUT2);
#endif
}

void putchar(char c)
{
#if GXOS_MSVC_STUB
    (void)c;
    return;
#else
    if (!s_uart_base) return;
    
    // Wait for transmit holding register to be empty
    while ((uart_read(UART_LSR) & LSR_THRE) == 0) {
        // Spin
    }
    
    // Write character
    uart_write(UART_THR, static_cast<uint8_t>(c));
    
    // If newline, also send carriage return
    if (c == '\n') {
        while ((uart_read(UART_LSR) & LSR_THRE) == 0) {
            // Spin
        }
        uart_write(UART_THR, '\r');
    }
#endif
}

void puts(const char* str)
{
    if (!str) return;
    while (*str) {
        putchar(*str);
        ++str;
    }
}

void write(const char* str, uint64_t len)
{
    if (!str) return;
    for (uint64_t i = 0; i < len; ++i) {
        putchar(str[i]);
    }
}

void put_hex(uint64_t value)
{
    puts("0x");
    static const char hex[] = "0123456789ABCDEF";
    bool leading = true;
    
    for (int i = 60; i >= 0; i -= 4) {
        uint64_t nibble = (value >> i) & 0xF;
        if (nibble != 0) leading = false;
        if (!leading || i == 0) {
            putchar(hex[nibble]);
        }
    }
}

void put_dec(int64_t value)
{
    if (value < 0) {
        putchar('-');
        value = -value;
    }
    
    if (value == 0) {
        putchar('0');
        return;
    }
    
    char buf[21];  // Max 20 digits for 64-bit + null
    int i = 0;
    
    while (value > 0) {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    }
    
    // Print in reverse order
    while (i > 0) {
        putchar(buf[--i]);
    }
}

bool char_available()
{
#if GXOS_MSVC_STUB
    return false;
#else
    if (!s_uart_base) return false;
    return (uart_read(UART_LSR) & LSR_DR) != 0;
#endif
}

char getchar()
{
#if GXOS_MSVC_STUB
    return 0;
#else
    if (!s_uart_base) return 0;
    
    // Wait for data ready
    while ((uart_read(UART_LSR) & LSR_DR) == 0) {
        // Spin
    }
    
    return static_cast<char>(uart_read(UART_RBR));
#endif
}

int getchar_nonblock()
{
#if GXOS_MSVC_STUB
    return -1;
#else
    if (!s_uart_base) return -1;
    
    if ((uart_read(UART_LSR) & LSR_DR) == 0) {
        return -1;  // No character available
    }
    
    return static_cast<int>(uart_read(UART_RBR));
#endif
}

void shutdown()
{
    puts("[Serial] Shutdown\n");
    // Nothing to do for serial - it's always "on"
}

} // namespace serial_console
} // namespace mips64
} // namespace arch
} // namespace kernel
