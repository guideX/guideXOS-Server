// ARM64 Serial Console Implementation
//
// Provides early serial console output via PL011 UART.
// Targets QEMU virt machine's PL011 at 0x09000000.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/serial_console.h"

#if defined(_MSC_VER)
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace arm64 {
namespace serial_console {

// ================================================================
// Internal state
// ================================================================

static uint64_t s_uartBase = UART_BASE_VIRT;
static bool s_initialized = false;

// ================================================================
// MMIO helpers
// ================================================================

static inline void mmio_write32(uint64_t addr, uint32_t value)
{
#if GXOS_MSVC_STUB
    (void)addr;
    (void)value;
#else
    asm volatile ("str %w0, [%1]" : : "r"(value), "r"(addr) : "memory");
#endif
}

static inline uint32_t mmio_read32(uint64_t addr)
{
#if GXOS_MSVC_STUB
    (void)addr;
    return FR_TXFE;  // Pretend TX FIFO is empty for MSVC builds
#else
    uint32_t value;
    asm volatile ("ldr %w0, [%1]" : "=r"(value) : "r"(addr) : "memory");
    return value;
#endif
}

// ================================================================
// UART register access
// ================================================================

static inline void uart_write(uint32_t reg, uint32_t value)
{
    mmio_write32(s_uartBase + reg, value);
}

static inline uint32_t uart_read(uint32_t reg)
{
    return mmio_read32(s_uartBase + reg);
}

// ================================================================
// Initialization
// ================================================================

void init()
{
    if (s_initialized) return;
    
#if !GXOS_MSVC_STUB
    // On QEMU virt, the UART is pre-initialized by the firmware/QEMU
    // We just need to ensure it's enabled with correct settings
    
    // Disable UART during configuration
    uart_write(UART_CR, 0);
    
    // Wait for any pending transmission
    while (uart_read(UART_FR) & FR_BUSY) {
        // Spin
    }
    
    // Clear pending interrupts
    uart_write(UART_ICR, 0x7FF);
    
    // Set baud rate (assuming 24MHz UARTCLK, 115200 baud)
    // Divisor = UARTCLK / (16 * BaudRate) = 24000000 / (16 * 115200) = 13.0208
    // IBRD = 13, FBRD = round(0.0208 * 64) = 1
    uart_write(UART_IBRD, 13);
    uart_write(UART_FBRD, 1);
    
    // Configure line control: 8N1, enable FIFOs
    uart_write(UART_LCR_H, LCR_H_WLEN8 | LCR_H_FEN);
    
    // Disable all interrupts (we poll)
    uart_write(UART_IMSC, 0);
    
    // Enable UART, TX, and RX
    uart_write(UART_CR, CR_UARTEN | CR_TXE | CR_RXE);
#endif
    
    s_initialized = true;
}

// ================================================================
// Output functions
// ================================================================

void putc(char c)
{
    if (!s_initialized) {
        init();
    }
    
#if GXOS_MSVC_STUB
    // For MSVC builds, could redirect to Windows console
    (void)c;
#else
    // Wait until TX FIFO has space
    while (uart_read(UART_FR) & FR_TXFF) {
        // Spin
    }
    
    // Write character to data register
    uart_write(UART_DR, static_cast<uint32_t>(c));
    
    // Handle newline -> CRLF conversion
    if (c == '\n') {
        while (uart_read(UART_FR) & FR_TXFF) {
            // Spin
        }
        uart_write(UART_DR, '\r');
    }
#endif
}

void print(const char* str)
{
    if (!str) return;
    
    while (*str) {
        putc(*str++);
    }
}

void write(const char* str, size_t len)
{
    if (!str) return;
    
    for (size_t i = 0; i < len; ++i) {
        putc(str[i]);
    }
}

void print_hex(uint64_t value)
{
    print("0x");
    
    // Print all 16 hex digits
    for (int i = 60; i >= 0; i -= 4) {
        int digit = (value >> i) & 0xF;
        char c = (digit < 10) ? ('0' + digit) : ('A' + digit - 10);
        putc(c);
    }
}

void print_dec(int64_t value)
{
    if (value < 0) {
        putc('-');
        value = -value;
    }
    
    if (value == 0) {
        putc('0');
        return;
    }
    
    // Build digits in reverse
    char buf[21];  // Max 20 digits for int64 + null
    int i = 0;
    
    while (value > 0 && i < 20) {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    }
    
    // Print in correct order
    while (i > 0) {
        putc(buf[--i]);
    }
}

// ================================================================
// Input functions
// ================================================================

bool can_read()
{
    if (!s_initialized) {
        init();
    }
    
#if GXOS_MSVC_STUB
    return false;
#else
    // Check if RX FIFO is not empty
    return (uart_read(UART_FR) & FR_RXFE) == 0;
#endif
}

char getc()
{
    if (!s_initialized) {
        init();
    }
    
#if GXOS_MSVC_STUB
    return 0;
#else
    // Wait until RX FIFO has data
    while (uart_read(UART_FR) & FR_RXFE) {
        // Spin
    }
    
    // Read and return character
    return static_cast<char>(uart_read(UART_DR) & 0xFF);
#endif
}

int try_getc()
{
    if (!s_initialized) {
        init();
    }
    
#if GXOS_MSVC_STUB
    return -1;
#else
    // Check if RX FIFO has data
    if (uart_read(UART_FR) & FR_RXFE) {
        return -1;  // No data available
    }
    
    // Read and return character
    return static_cast<int>(uart_read(UART_DR) & 0xFF);
#endif
}

void flush()
{
    if (!s_initialized) return;
    
#if !GXOS_MSVC_STUB
    // Wait until TX FIFO is empty and transmitter is idle
    while ((uart_read(UART_FR) & FR_TXFE) == 0 ||
           (uart_read(UART_FR) & FR_BUSY)) {
        // Spin
    }
#endif
}

} // namespace serial_console
} // namespace arm64
} // namespace arch
} // namespace kernel

// ================================================================
// C linkage for early boot
// ================================================================

extern "C" void early_print(const char* msg)
{
    kernel::arch::arm64::serial_console::print(msg);
}
