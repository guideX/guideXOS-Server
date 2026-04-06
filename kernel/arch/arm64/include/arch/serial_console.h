// ARM64 Serial Console Interface
//
// Provides early serial console output via PL011 UART.
//
// QEMU virt machine provides a PL011 UART at:
//   0x09000000 (MMIO base address)
//
// PL011 UART register offsets:
//   0x000 - UARTDR     (Data Register)
//   0x004 - UARTRSR    (Receive Status / Error Clear)
//   0x018 - UARTFR     (Flag Register)
//   0x020 - UARTILPR   (IrDA Low Power Counter)
//   0x024 - UARTIBRD   (Integer Baud Rate Divisor)
//   0x028 - UARTFBRD   (Fractional Baud Rate Divisor)
//   0x02C - UARTLCR_H  (Line Control Register)
//   0x030 - UARTCR     (Control Register)
//   0x034 - UARTIFLS   (Interrupt FIFO Level Select)
//   0x038 - UARTIMSC   (Interrupt Mask Set/Clear)
//   0x03C - UARTRIS    (Raw Interrupt Status)
//   0x040 - UARTMIS    (Masked Interrupt Status)
//   0x044 - UARTICR    (Interrupt Clear Register)
//   0x048 - UARTDMACR  (DMA Control Register)
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace arm64 {
namespace serial_console {

// ================================================================
// PL011 UART base addresses
// ================================================================

// QEMU virt machine PL011 UART
static const uint64_t UART_BASE_VIRT = 0x09000000ULL;

// Alternative UART addresses for other platforms
static const uint64_t UART_BASE_RPI4 = 0xFE201000ULL;   // Raspberry Pi 4

// ================================================================
// PL011 UART Register Offsets
// ================================================================

static const uint32_t UART_DR     = 0x000;  // Data Register
static const uint32_t UART_RSR    = 0x004;  // Receive Status / Error Clear
static const uint32_t UART_ECR    = 0x004;  // Error Clear Register (write)
static const uint32_t UART_FR     = 0x018;  // Flag Register
static const uint32_t UART_ILPR   = 0x020;  // IrDA Low Power Counter
static const uint32_t UART_IBRD   = 0x024;  // Integer Baud Rate Divisor
static const uint32_t UART_FBRD   = 0x028;  // Fractional Baud Rate Divisor
static const uint32_t UART_LCR_H  = 0x02C;  // Line Control Register
static const uint32_t UART_CR     = 0x030;  // Control Register
static const uint32_t UART_IFLS   = 0x034;  // Interrupt FIFO Level Select
static const uint32_t UART_IMSC   = 0x038;  // Interrupt Mask Set/Clear
static const uint32_t UART_RIS    = 0x03C;  // Raw Interrupt Status
static const uint32_t UART_MIS    = 0x040;  // Masked Interrupt Status
static const uint32_t UART_ICR    = 0x044;  // Interrupt Clear Register
static const uint32_t UART_DMACR  = 0x048;  // DMA Control Register

// ================================================================
// Flag Register (FR) bits
// ================================================================

static const uint32_t FR_CTS   = (1 << 0);   // Clear to send
static const uint32_t FR_DSR   = (1 << 1);   // Data set ready
static const uint32_t FR_DCD   = (1 << 2);   // Data carrier detect
static const uint32_t FR_BUSY  = (1 << 3);   // UART busy
static const uint32_t FR_RXFE  = (1 << 4);   // Receive FIFO empty
static const uint32_t FR_TXFF  = (1 << 5);   // Transmit FIFO full
static const uint32_t FR_RXFF  = (1 << 6);   // Receive FIFO full
static const uint32_t FR_TXFE  = (1 << 7);   // Transmit FIFO empty
static const uint32_t FR_RI    = (1 << 8);   // Ring indicator

// ================================================================
// Line Control Register (LCR_H) bits
// ================================================================

static const uint32_t LCR_H_BRK   = (1 << 0);   // Send break
static const uint32_t LCR_H_PEN   = (1 << 1);   // Parity enable
static const uint32_t LCR_H_EPS   = (1 << 2);   // Even parity select
static const uint32_t LCR_H_STP2  = (1 << 3);   // Two stop bits
static const uint32_t LCR_H_FEN   = (1 << 4);   // FIFO enable
static const uint32_t LCR_H_WLEN5 = (0 << 5);   // 5 bit word length
static const uint32_t LCR_H_WLEN6 = (1 << 5);   // 6 bit word length
static const uint32_t LCR_H_WLEN7 = (2 << 5);   // 7 bit word length
static const uint32_t LCR_H_WLEN8 = (3 << 5);   // 8 bit word length
static const uint32_t LCR_H_SPS   = (1 << 7);   // Stick parity select

// ================================================================
// Control Register (CR) bits
// ================================================================

static const uint32_t CR_UARTEN = (1 << 0);    // UART enable
static const uint32_t CR_SIREN  = (1 << 1);    // SIR enable
static const uint32_t CR_SIRLP  = (1 << 2);    // SIR low power mode
static const uint32_t CR_LBE    = (1 << 7);    // Loopback enable
static const uint32_t CR_TXE    = (1 << 8);    // Transmit enable
static const uint32_t CR_RXE    = (1 << 9);    // Receive enable
static const uint32_t CR_DTR    = (1 << 10);   // Data transmit ready
static const uint32_t CR_RTS    = (1 << 11);   // Request to send
static const uint32_t CR_OUT1   = (1 << 12);   // Out1
static const uint32_t CR_OUT2   = (1 << 13);   // Out2
static const uint32_t CR_RTSEN  = (1 << 14);   // RTS hardware flow control
static const uint32_t CR_CTSEN  = (1 << 15);   // CTS hardware flow control

// ================================================================
// Interrupt bits (IMSC, RIS, MIS, ICR)
// ================================================================

static const uint32_t INT_OE   = (1 << 10);    // Overrun error
static const uint32_t INT_BE   = (1 << 9);     // Break error
static const uint32_t INT_PE   = (1 << 8);     // Parity error
static const uint32_t INT_FE   = (1 << 7);     // Framing error
static const uint32_t INT_RT   = (1 << 6);     // Receive timeout
static const uint32_t INT_TX   = (1 << 5);     // Transmit
static const uint32_t INT_RX   = (1 << 4);     // Receive
static const uint32_t INT_DSR  = (1 << 3);     // DSR modem
static const uint32_t INT_DCD  = (1 << 2);     // DCD modem
static const uint32_t INT_CTS  = (1 << 1);     // CTS modem
static const uint32_t INT_RI   = (1 << 0);     // RI modem

// ================================================================
// Serial console functions
// ================================================================

// Initialize the serial console
void init();

// Output a single character
void putc(char c);

// Output a null-terminated string
void print(const char* str);

// Output a string with specified length
void write(const char* str, size_t len);

// Print a hexadecimal number
void print_hex(uint64_t value);

// Print a decimal number
void print_dec(int64_t value);

// Check if a character is available for reading
bool can_read();

// Read a single character (blocking)
char getc();

// Read a single character (non-blocking, returns -1 if none)
int try_getc();

// Flush output buffers
void flush();

} // namespace serial_console
} // namespace arm64
} // namespace arch
} // namespace kernel
