#include <stdint.h>

static volatile uint32_t* g_uart = (volatile uint32_t*)0x09000000ULL;
static uint64_t g_uart_base = 0x09000000ULL;
static const uint32_t kFr = 0x018 / 4;
static const uint32_t kIbrd = 0x024 / 4;
static const uint32_t kFbrd = 0x028 / 4;
static const uint32_t kLcrH = 0x02c / 4;
static const uint32_t kCr = 0x030 / 4;
static const uint32_t kImsc = 0x038 / 4;
static const uint32_t kIcr = 0x044 / 4;
static const uint32_t kFrTxFf = 1u << 5;

extern "C" void phase3_serial_set_base(uint64_t base);
extern "C" uint64_t phase3_serial_base();

extern "C" void phase4_serial_init()
{
    g_uart[kCr] = 0;
    g_uart[kImsc] = 0;
    g_uart[kIcr] = 0x7ff;
    g_uart[kIbrd] = 13;
    g_uart[kFbrd] = 1;
    g_uart[kLcrH] = (3u << 5) | (1u << 4);
    g_uart[kCr] = (1u << 0) | (1u << 8) | (1u << 9);
    __asm__ volatile("dsb sy" ::: "memory");
}

extern "C" void phase4_serial_putc(char c)
{
    while ((g_uart[kFr] & kFrTxFf) != 0) { }
    g_uart[0] = (uint32_t)(uint8_t)c;
    __asm__ volatile("dsb sy" ::: "memory");
}

extern "C" void phase4_serial_print(const char* text)
{
    if (!text) return;
    while (*text) {
        if (*text == '\n') phase4_serial_putc('\r');
        phase4_serial_putc(*text++);
    }
}

extern "C" void phase4_serial_hex(uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    phase4_serial_print("0x");
    for (int shift = 60; shift >= 0; shift -= 4) phase4_serial_putc(digits[(value >> shift) & 0xf]);
}

extern "C" void phase4_serial_dec(uint64_t value)
{
    char digits[24];
    uint32_t count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0 && count < sizeof(digits));
    while (count != 0) phase4_serial_putc(digits[--count]);
}

extern "C" void phase4_serial_set_base(uint64_t base)
{
    if (base == 0 || (base & 0xfff) != 0) return;
    g_uart_base = base;
    g_uart = (volatile uint32_t*)(uintptr_t)base;
    phase3_serial_set_base(base);
}

extern "C" uint64_t phase4_serial_base() { return g_uart_base; }
