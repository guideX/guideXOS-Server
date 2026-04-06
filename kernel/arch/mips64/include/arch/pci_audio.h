//
// MIPS64 PCI Audio Interface
//
// Provides platform-specific PCI audio device support for MIPS64.
// Stub implementation for initial kernel compile.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace mips64 {
namespace pci_audio {

// Audio device types
enum AudioDeviceType : uint8_t {
    AUDIO_NONE    = 0,
    AUDIO_AC97    = 1,    // AC'97 codec
    AUDIO_HDA     = 2,    // High Definition Audio
};

// Audio device information
struct AudioDeviceInfo {
    AudioDeviceType type;
    bool            available;
    uint64_t        base;     // MMIO base address
    uint32_t        irq;      // Interrupt line
};

// Initialize audio subsystem
void init();

// Check if audio device is available
bool is_available();

// Get audio device information
const AudioDeviceInfo* get_info();

// Playback control
bool start_playback(const void* buffer, uint32_t size, uint32_t sample_rate);
void stop_playback();
bool is_playing();

// Volume control (0-100)
void set_volume(uint8_t volume);
uint8_t get_volume();

} // namespace pci_audio
} // namespace mips64
} // namespace arch
} // namespace kernel
