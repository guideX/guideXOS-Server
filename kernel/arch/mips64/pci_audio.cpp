//
// MIPS64 PCI Audio Implementation (Stub)
//
// Provides PCI audio device support for MIPS64 platforms.
// Currently a stub implementation for initial kernel compile.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/pci_audio.h"
#include "include/arch/serial_console.h"

namespace kernel {
namespace arch {
namespace mips64 {
namespace pci_audio {

namespace {

static AudioDeviceInfo s_info = {
    AUDIO_NONE, // type
    false,      // available
    0,          // base
    0           // irq
};

static uint8_t s_volume = 50;

} // anonymous namespace

void init()
{
    serial_console::puts("[Audio] MIPS64 audio init (stub)\n");
    
    // TODO: Probe PCI bus for audio devices
    s_info.available = false;
    s_info.type = AUDIO_NONE;
    
    serial_console::puts("[Audio] No audio device detected\n");
}

bool is_available()
{
    return s_info.available;
}

const AudioDeviceInfo* get_info()
{
    return &s_info;
}

bool start_playback(const void* buffer, uint32_t size, uint32_t sample_rate)
{
    (void)buffer;
    (void)size;
    (void)sample_rate;
    return false;  // Not implemented
}

void stop_playback()
{
    // Not implemented
}

bool is_playing()
{
    return false;
}

void set_volume(uint8_t volume)
{
    s_volume = (volume > 100) ? 100 : volume;
}

uint8_t get_volume()
{
    return s_volume;
}

} // namespace pci_audio
} // namespace mips64
} // namespace arch
} // namespace kernel
