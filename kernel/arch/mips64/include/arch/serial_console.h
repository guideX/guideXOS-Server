//
// MIPS64 Serial Console Interface
//
// Provides early serial console output via 16550-compatible UART.
//
// QEMU malta machine provides a 16550 UART at:
//   0x1F000900 (MIPS malta ISA I/O port 0x3F8 mapped to MMIO)
//
// QEMU virt machine provides a 16550 UART at:
//   0x10000000 (similar to other QEMU virt platforms)
//
// 16550 UART register offsets:
//   0x00 - RBR/THR (Receive/Transmit Buffer)
//   0x01 - IER (Interrupt Enable Register)
//   0x02 - IIR/FCR (Interrupt ID / FIFO Control)
//   0x03 - LCR (Line Control Register)
//   0x04 - MCR (Modem Control Register)
//   0x05 - LSR (Line Status Register)
//   0x06 - MSR (Modem Status Register)
//   0x07 - SCR (Scratch Register)
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace mips64 {
namespace serial_console {

// ================================================================
// UART base addresses for different machine types
// ================================================================

// QEMU malta machine (ISA UART at physical 0x1F000900)
// This maps the standard PC COM1 (0x3F8) to MIPS address space
static const uint64_t UART_BASE_MALTA = 0xFFFFFFFF9F000900ULL;  // kseg1 uncached

// QEMU virt machine
static const uint64_t UART_BASE_VIRT  = 0xFFFFFFFF90000000ULL;  // kseg1 uncached

// Physical addresses (for early boot when using unmapped addresses)
static const uint64_t UART_BASE_MALTA_PHYS = 0x1F000900ULL;
static const uint64_t UART_BASE_VIRT_PHYS  = 0x10000000ULL;

// ================================================================
// 16550 UART Register Offsets
// ================================================================

static const uint32_t UART_RBR = 0x00;  // Receive Buffer Register (read)
static const uint32_t UART_THR = 0x00;  // Transmit Holding Register (write)
static const uint32_t UART_IER = 0x01;  // Interrupt Enable Register
static const uint32_t UART_IIR = 0x02;  // Interrupt Identification Register (read)
static const uint32_t UART_FCR = 0x02;  // FIFO Control Register (write)
static const uint32_t UART_LCR = 0x03;  // Line Control Register
static const uint32_t UART_MCR = 0x04;  // Modem Control Register
static const uint32_t UART_LSR = 0x05;  // Line Status Register
static const uint32_t UART_MSR = 0x06;  // Modem Status Register
static const uint32_t UART_SCR = 0x07;  // Scratch Register

// Divisor Latch (accessible when LCR.DLAB = 1)
static const uint32_t UART_DLL = 0x00;  // Divisor Latch Low
static const uint32_t UART_DLH = 0x01;  // Divisor Latch High

// ================================================================
// Line Control Register (LCR) bits
// ================================================================

static const uint8_t LCR_WLS_5    = 0x00;  // 5 data bits
static const uint8_t LCR_WLS_6    = 0x01;  // 6 data bits
static const uint8_t LCR_WLS_7    = 0x02;  // 7 data bits
static const uint8_t LCR_WLS_8    = 0x03;  // 8 data bits
static const uint8_t LCR_STB      = 0x04;  // 2 stop bits (else 1)
static const uint8_t LCR_PEN      = 0x08;  // Parity enable
static const uint8_t LCR_EPS      = 0x10;  // Even parity select
static const uint8_t LCR_STICK    = 0x20;  // Stick parity
static const uint8_t LCR_BREAK    = 0x40;  // Break control
static const uint8_t LCR_DLAB     = 0x80;  // Divisor Latch Access Bit

// ================================================================
// Line Status Register (LSR) bits
// ================================================================

static const uint8_t LSR_DR       = 0x01;  // Data Ready
static const uint8_t LSR_OE       = 0x02;  // Overrun Error
static const uint8_t LSR_PE       = 0x04;  // Parity Error
static const uint8_t LSR_FE       = 0x08;  // Framing Error
static const uint8_t LSR_BI       = 0x10;  // Break Interrupt
static const uint8_t LSR_THRE     = 0x20;  // Transmit Holding Register Empty
static const uint8_t LSR_TEMT     = 0x40;  // Transmitter Empty
static const uint8_t LSR_FIFO_ERR = 0x80;  // Error in FIFO

// ================================================================
// FIFO Control Register (FCR) bits
// ================================================================

static const uint8_t FCR_ENABLE   = 0x01;  // FIFO enable
static const uint8_t FCR_RXRST    = 0x02;  // Receiver FIFO reset
static const uint8_t FCR_TXRST    = 0x04;  // Transmitter FIFO reset
static const uint8_t FCR_TRIGGER_1  = 0x00;  // Trigger level 1 byte
static const uint8_t FCR_TRIGGER_4  = 0x40;  // Trigger level 4 bytes
static const uint8_t FCR_TRIGGER_8  = 0x80;  // Trigger level 8 bytes
static const uint8_t FCR_TRIGGER_14 = 0xC0;  // Trigger level 14 bytes

// ================================================================
// Modem Control Register (MCR) bits
// ================================================================

static const uint8_t MCR_DTR      = 0x01;  // Data Terminal Ready
static const uint8_t MCR_RTS      = 0x02;  // Request To Send
static const uint8_t MCR_OUT1     = 0x04;  // Out 1
static const uint8_t MCR_OUT2     = 0x08;  // Out 2 (enable interrupts)
static const uint8_t MCR_LOOP     = 0x10;  // Loopback mode

// ================================================================
// Console functions
// ================================================================

// Initialize the serial console
void init();

// Output a single character
void putchar(char c);

// Output a null-terminated string
void puts(const char* str);

// Output a string of specified length
void write(const char* str, uint64_t len);

// Output a hexadecimal number
void put_hex(uint64_t value);

// Output a decimal number
void put_dec(int64_t value);

// Check if a character is available
bool char_available();

// Read a character (blocking)
char getchar();

// Read a character (non-blocking, returns -1 if none available)
int getchar_nonblock();

// Shutdown (no-op for serial)
void shutdown();

} // namespace serial_console
} // namespace mips64
} // namespace arch
} // namespace kernel
