//
// LoongArch Early Console Implementation
//
// Uses the NS16550 UART for early kernel output. This is the
// standard early console for both QEMU loongarch64-virt and
// real Loongson hardware.
//
// The UART is configured for 115200 baud, 8N1 (8 data bits,
// no parity, 1 stop bit). The baud rate divisor is calculated
// assuming a 1.8432 MHz crystal (standard for 16550).
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/loongarch_console.h"
#include "include/arch/loongarch64.h"
#if defined(_MSC_VER)
#include <intrin.h>
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace loongarch64 {
namespace loongarch_console {

namespace {

// Current UART base address (can be changed during init)
static uint64_t s_uart_base = UART_BASE;
static bool s_initialized = false;

// ================================================================
// MMIO helpers for UART access
//
// LoongArch UART registers are typically byte-width but may be
// aligned at 4-byte boundaries on some platforms. We use byte
// access for maximum compatibility.
// ================================================================

static uint8_t uart_rd(uint32_t reg)
{
#if GXOS_MSVC_STUB
    (void)reg;
    return 0;
#else
    volatile uint8_t* addr = reinterpret_cast<volatile uint8_t*>(s_uart_base + reg);
    return *addr;
#endif
}

static void uart_wr(uint32_t reg, uint8_t val)
{
#if GXOS_MSVC_STUB
    (void)reg;
    (void)val;
#else
    volatile uint8_t* addr = reinterpret_cast<volatile uint8_t*>(s_uart_base + reg);
    *addr = val;
#endif
}

// Wait for transmit holding register to be empty
static void uart_wait_tx_empty()
{
#if GXOS_MSVC_STUB
    return;
#else
    while ((uart_rd(UART_LSR) & LSR_THRE) == 0) {
        // Busy wait
    }
#endif
}

// Wait for data to be available
static void uart_wait_rx_ready()
{
#if GXOS_MSVC_STUB
    return;
#else
    while ((uart_rd(UART_LSR) & LSR_DR) == 0) {
        // Busy wait
    }
#endif
}

} // anonymous namespace

// ================================================================
// Public API implementation
// ================================================================

void init()
{
    init(UART_BASE);
}

void init(uint64_t uart_base)
{
#if GXOS_MSVC_STUB
    (void)uart_base;
    s_initialized = true;
    return;
#else
    s_uart_base = uart_base;
    
    // Disable all interrupts
    uart_wr(UART_IER, 0x00);
    
    // Enable DLAB (Divisor Latch Access Bit) to set baud rate
    uart_wr(UART_LCR, LCR_DLAB);
    
    // Set divisor for 115200 baud
    // Divisor = Clock / (16 * Baud Rate)
    // For 1.8432 MHz clock: 1843200 / (16 * 115200) = 1
    // For QEMU, the divisor may be ignored or the clock may differ
    uart_wr(UART_DLL, 1);   // Divisor low byte
    uart_wr(UART_DLH, 0);   // Divisor high byte
    
    // Configure line: 8 data bits, 1 stop bit, no parity
    uart_wr(UART_LCR, LCR_WLS_8);
    
    // Enable and reset FIFOs, set trigger level to 1 byte
    uart_wr(UART_FCR, FCR_ENABLE | FCR_RXRST | FCR_TXRST | FCR_TRIG_1);
    
    // Enable DTR and RTS (some terminals need these)
    uart_wr(UART_MCR, 0x03);
    
    s_initialized = true;
#endif
}

void putchar(char c)
{
#if GXOS_MSVC_STUB
    (void)c;
    return;
#else
    if (!s_initialized) {
        init();
    }
    
    // Handle newline: send CR+LF for proper line endings
    if (c == '\n') {
        uart_wait_tx_empty();
        uart_wr(UART_THR, '\r');
    }
    
    uart_wait_tx_empty();
    uart_wr(UART_THR, static_cast<uint8_t>(c));
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

void put_hex32(uint32_t value)
{
    puts("0x");
    static const char hex[] = "0123456789ABCDEF";
    
    for (int i = 28; i >= 0; i -= 4) {
        uint32_t nibble = (value >> i) & 0xF;
        putchar(hex[nibble]);
    }
}

char getchar()
{
#if GXOS_MSVC_STUB
    return '\0';
#else
    if (!s_initialized) {
        init();
    }
    
    uart_wait_rx_ready();
    return static_cast<char>(uart_rd(UART_RBR));
#endif
}

bool char_available()
{
#if GXOS_MSVC_STUB
    return false;
#else
    if (!s_initialized) {
        return false;
    }
    
    return (uart_rd(UART_LSR) & LSR_DR) != 0;
#endif
}

int try_getchar()
{
#if GXOS_MSVC_STUB
    return -1;
#else
    if (!s_initialized) {
        return -1;
    }
    
    if ((uart_rd(UART_LSR) & LSR_DR) == 0) {
        return -1;
    }
    
    return static_cast<int>(uart_rd(UART_RBR));
#endif
}

} // namespace loongarch_console
} // namespace loongarch64
} // namespace arch
} // namespace kernel
