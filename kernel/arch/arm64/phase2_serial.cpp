#include <stdint.h>

// The fallback is retained until the validated DTB has supplied the active
// console address.  This keeps the first post-EBS diagnostic available even
// when platform parsing fails.
static volatile uint32_t* gUart = (volatile uint32_t*)0x09000000ULL;
static uint64_t gUartBase = 0x09000000ULL;
static const uint32_t kFr = 0x018 / 4;
static const uint32_t kIbrd = 0x024 / 4;
static const uint32_t kFbrd = 0x028 / 4;
static const uint32_t kLcrH = 0x02c / 4;
static const uint32_t kCr = 0x030 / 4;
static const uint32_t kImsc = 0x038 / 4;
static const uint32_t kIcr = 0x044 / 4;
static const uint32_t kFrTxFf = 1u << 5;

static inline void serial_barrier()
{
    __asm__ volatile("dsb sy" ::: "memory");
}

extern "C" void phase2_serial_set_base(uint64_t base)
{
    if (base == 0 || (base & 0xfff) != 0) return;
    gUartBase = base;
    gUart = (volatile uint32_t*)(uintptr_t)base;
}

extern "C" uint64_t phase2_serial_base()
{
    return gUartBase;
}

extern "C" void phase2_serial_init()
{
    gUart[kCr] = 0;
    gUart[kImsc] = 0;
    gUart[kIcr] = 0x7ff;
    gUart[kIbrd] = 13;
    gUart[kFbrd] = 1;
    gUart[kLcrH] = (3u << 5) | (1u << 4);
    gUart[kCr] = (1u << 0) | (1u << 8) | (1u << 9);
    serial_barrier();
}

extern "C" void phase2_serial_putc(char character)
{
    while ((gUart[kFr] & kFrTxFf) != 0) { }
    gUart[0] = (uint32_t)(uint8_t)character;
    serial_barrier();
}

extern "C" void phase2_serial_print(const char* text)
{
    if (!text) return;
    while (*text != 0) {
        if (*text == '\n') phase2_serial_putc('\r');
        phase2_serial_putc(*text++);
    }
}

extern "C" void phase2_serial_hex(uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    phase2_serial_print("0x");
    for (int shift = 60; shift >= 0; shift -= 4) phase2_serial_putc(digits[(value >> shift) & 0xf]);
}

extern "C" void phase2_serial_dec(uint64_t value)
{
    char digits[24];
    uint32_t count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0 && count < sizeof(digits));
    while (count != 0) phase2_serial_putc(digits[--count]);
}
