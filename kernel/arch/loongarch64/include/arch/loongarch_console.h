//
// LoongArch Early Console
//
// LoongArch systems typically use one of these for early console output:
//
//   1. UART (NS16550 compatible)
//      QEMU loongarch64-virt machine provides a 16550 UART at
//      0x1FE001E0 (or via device tree). This is the most common
//      early console option.
//
//   2. BIOS/Firmware console (on real hardware)
//      Early Loongson firmware may provide a simple console
//      service, though this is less standardized than RISC-V SBI.
//
//   3. IOCSR-based debug port (implementation-specific)
//      Some Loongson implementations provide debug output via
//      I/O Configuration Space Registers (IOCSR).
//
// This implementation uses the 16550 UART for maximum compatibility
// with QEMU and real hardware that has standard UART.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace loongarch64 {
namespace loongarch_console {

// ================================================================
// UART base addresses
//
// QEMU loongarch64-virt UART is at 0x1FE001E0 (check device tree).
// Real Loongson hardware may have UART at different addresses.
// ================================================================

// QEMU loongarch64-virt UART0 (NS16550 compatible)
static const uint64_t UART_BASE = 0x1FE001E0ULL;

// Alternative UART addresses (for different platforms)
static const uint64_t UART_LOONGSON_3A5000 = 0x1FE001E0ULL;

// ================================================================
// 16550 UART register offsets
// ================================================================

static const uint32_t UART_RBR = 0x00;  // Receive Buffer Register (read)
static const uint32_t UART_THR = 0x00;  // Transmit Holding Register (write)
static const uint32_t UART_IER = 0x01;  // Interrupt Enable Register
static const uint32_t UART_IIR = 0x02;  // Interrupt Identification (read)
static const uint32_t UART_FCR = 0x02;  // FIFO Control Register (write)
static const uint32_t UART_LCR = 0x03;  // Line Control Register
static const uint32_t UART_MCR = 0x04;  // Modem Control Register
static const uint32_t UART_LSR = 0x05;  // Line Status Register
static const uint32_t UART_MSR = 0x06;  // Modem Status Register
static const uint32_t UART_SCR = 0x07;  // Scratch Register

// Divisor Latch registers (when LCR.DLAB = 1)
static const uint32_t UART_DLL = 0x00;  // Divisor Latch Low
static const uint32_t UART_DLH = 0x01;  // Divisor Latch High

// ================================================================
// Line Status Register bits
// ================================================================

static const uint8_t LSR_DR   = (1 << 0);  // Data Ready
static const uint8_t LSR_OE   = (1 << 1);  // Overrun Error
static const uint8_t LSR_PE   = (1 << 2);  // Parity Error
static const uint8_t LSR_FE   = (1 << 3);  // Framing Error
static const uint8_t LSR_BI   = (1 << 4);  // Break Interrupt
static const uint8_t LSR_THRE = (1 << 5);  // Transmit Holding Register Empty
static const uint8_t LSR_TEMT = (1 << 6);  // Transmitter Empty
static const uint8_t LSR_FIFO = (1 << 7);  // FIFO Error

// ================================================================
// Line Control Register bits
// ================================================================

static const uint8_t LCR_WLS_5  = 0x00;    // 5 data bits
static const uint8_t LCR_WLS_6  = 0x01;    // 6 data bits
static const uint8_t LCR_WLS_7  = 0x02;    // 7 data bits
static const uint8_t LCR_WLS_8  = 0x03;    // 8 data bits
static const uint8_t LCR_STB    = (1 << 2);  // 2 stop bits
static const uint8_t LCR_PEN    = (1 << 3);  // Parity Enable
static const uint8_t LCR_EPS    = (1 << 4);  // Even Parity Select
static const uint8_t LCR_STICK  = (1 << 5);  // Stick Parity
static const uint8_t LCR_BREAK  = (1 << 6);  // Break Control
static const uint8_t LCR_DLAB   = (1 << 7);  // Divisor Latch Access Bit

// ================================================================
// FIFO Control Register bits
// ================================================================

static const uint8_t FCR_ENABLE = (1 << 0);  // FIFO Enable
static const uint8_t FCR_RXRST  = (1 << 1);  // Receiver FIFO Reset
static const uint8_t FCR_TXRST  = (1 << 2);  // Transmit FIFO Reset
static const uint8_t FCR_TRIG_1 = 0x00;      // Trigger at 1 byte
static const uint8_t FCR_TRIG_4 = 0x40;      // Trigger at 4 bytes
static const uint8_t FCR_TRIG_8 = 0x80;      // Trigger at 8 bytes
static const uint8_t FCR_TRIG_14= 0xC0;      // Trigger at 14 bytes

// ================================================================
// Public API
// ================================================================

// Initialize the UART console
// This configures the UART for 115200 baud, 8N1
void init();

// Initialize with specific UART base address
void init(uint64_t uart_base);

// Write a single character
void putchar(char c);

// Write a null-terminated string
void puts(const char* str);

// Write a string with explicit length
void write(const char* str, uint64_t len);

// Print a 64-bit value in hexadecimal (for diagnostics)
void put_hex(uint64_t value);

// Print a 32-bit value in hexadecimal
void put_hex32(uint32_t value);

// Read a character (blocking)
char getchar();

// Check if a character is available (non-blocking)
bool char_available();

// Try to read a character (non-blocking, returns -1 if none)
int try_getchar();

} // namespace loongarch_console
} // namespace loongarch64
} // namespace arch
} // namespace kernel
