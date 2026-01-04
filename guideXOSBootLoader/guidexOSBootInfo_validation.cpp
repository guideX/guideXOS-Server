#include "guidexOSBootInfo.h"
#include "fb_console.h"
#include <stdint.h>

using guideXOS::BootInfo;

static void fill_screen_solid(const BootInfo* bi, uint32_t rgb)
{
    if (!bi) return;
    if (bi->FramebufferBase == 0 || bi->FramebufferWidth == 0 || bi->FramebufferHeight == 0)
        return;

    volatile uint32_t* base = (volatile uint32_t*)(uintptr_t)bi->FramebufferBase;
    uint64_t pixels = (uint64_t)bi->FramebufferWidth * (uint64_t)bi->FramebufferHeight;
    for (uint64_t i = 0; i < pixels; ++i)
    {
        base[i] = rgb;
    }
}

// Simple framebuffer-based early panic that does not rely on heap, CRT or UEFI
[[noreturn]] void guideXOS::guidexos_early_panic(const BootInfo* bi)
{
    bool used_console = false;

    if (bi && bi->FramebufferBase != 0 && bi->FramebufferWidth != 0 && bi->FramebufferHeight != 0)
    {
        BootInfo local = *bi;
        fb_console::Init(&local);
        fb_console::Clear();
        fb_console::Write("guideXOS BootInfo invalid. Halting.\n");
        used_console = true;
    }

    if (!used_console)
    {
        fill_screen_solid(bi, 0x00FF0000u);
    }

    for (;;) {
        for (volatile uint64_t i = 0; i < 100000000ULL; ++i) { }
    }
}
