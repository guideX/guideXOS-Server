// Polled PL011 output for QEMU virt AARCH64-1.
// 0x09000000 is a temporary platform constant, not a generic ARM64 UART.

#include <stdint.h>

static volatile uint32_t* const kUart = (volatile uint32_t*)0x09000000ULL;
static const uint32_t kFr = 0x018 / 4;
static const uint32_t kIbrd = 0x024 / 4;
static const uint32_t kFbrd = 0x028 / 4;
static const uint32_t kLcrH = 0x02c / 4;
static const uint32_t kCr = 0x030 / 4;
static const uint32_t kImsc = 0x038 / 4;
static const uint32_t kIcr = 0x044 / 4;
static const uint32_t kFrTxFf = 1u << 5;

static inline void barrier()
{
    __asm__ volatile("dsb sy" ::: "memory");
}
extern "C" void phase1_serial_init()
{
    kUart[kCr] = 0;
    kUart[kImsc] = 0;
    kUart[kIcr] = 0x7ff;
    // QEMU virt's PL011 is clocked at 24 MHz for this bring-up path.
    kUart[kIbrd] = 13;
    kUart[kFbrd] = 1;
    kUart[kLcrH] = (3u << 5) | (1u << 4); // 8 data bits, FIFO enabled
    kUart[kCr] = (1u << 0) | (1u << 8) | (1u << 9); // UARTEN|TXE|RXE
    barrier();
}

extern "C" void phase1_serial_putc(char character)
{
    while ((kUart[kFr] & kFrTxFf) != 0) { }
    kUart[0] = (uint32_t)(uint8_t)character;
    barrier();
}

extern "C" void phase1_serial_print(const char* text)
{
    if (!text) return;
    while (*text != 0) {
        if (*text == '\n') phase1_serial_putc('\r');
        phase1_serial_putc(*text++);
    }
}
