// SPARC v8 (Sun4m) PCI Audio Backend — Implementation
//
// No PCI on Sun4m.  Provides CS4231 SBus codec driver instead.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/pci_audio.h"
#include "include/arch/sparc.h"

namespace kernel {
namespace arch {
namespace sparc {
namespace pci_audio {

namespace {

static bool s_cs4231_available = false;

static void cs4231_write_reg(uint8_t index, uint8_t value)
{
    sparc::mmio_write8(CS4231_BASE + CS4231_IAR, index);
    sparc::mmio_write8(CS4231_BASE + CS4231_IDR, value);
}

static uint8_t cs4231_read_reg(uint8_t index)
{
    sparc::mmio_write8(CS4231_BASE + CS4231_IAR, index);
    return sparc::mmio_read8(CS4231_BASE + CS4231_IDR);
}

// Wait for the codec to leave MCE (Mode Change Enable) state
static void cs4231_wait_ready()
{
    for (int i = 0; i < 100000; ++i) {
        uint8_t status = sparc::mmio_read8(CS4231_BASE + CS4231_IAR);
        if (!(status & 0x80)) return; // not in init mode
    }
}

} // anonymous namespace

// PCI stubs
bool init() { return cs4231_init(); }
bool is_available() { return s_cs4231_available; }

// ================================================================
// CS4231 driver
// ================================================================

bool cs4231_init()
{
    // Probe: read the IAR register and check for a sane value
    uint8_t iar = sparc::mmio_read8(CS4231_BASE + CS4231_IAR);
    if (iar == 0xFF) {
        s_cs4231_available = false;
        return false;
    }

    // Enter MCE mode to configure
    sparc::mmio_write8(CS4231_BASE + CS4231_IAR, 0x40 | CS4231_IFACE_CONFIG);
    sparc::mmio_write8(CS4231_BASE + CS4231_IDR, 0x00); // default config

    // Set default format: 16-bit stereo, 8 kHz
    cs4231_configure(8000, 16, 2);

    // Enable playback
    sparc::mmio_write8(CS4231_BASE + CS4231_IAR, 0x40 | CS4231_IFACE_CONFIG);
    uint8_t cfg = cs4231_read_reg(CS4231_IFACE_CONFIG);
    cfg |= 0x01; // enable playback
    cs4231_write_reg(CS4231_IFACE_CONFIG, cfg);

    // Exit MCE mode
    sparc::mmio_write8(CS4231_BASE + CS4231_IAR, CS4231_IFACE_CONFIG);
    cs4231_wait_ready();

    // Unmute outputs
    cs4231_set_volume(32, 32);
    cs4231_set_mute(false);

    s_cs4231_available = true;
    return true;
}

bool cs4231_configure(uint16_t sampleRate, uint8_t bits, uint8_t channels)
{
    // CS4231 format byte encoding:
    //   bits [7:5] = clock/sample rate
    //   bit  [4]   = stereo (1) / mono (0)
    //   bits [3:2] = reserved
    //   bits [1:0] = format: 00=8-bit unsigned, 10=16-bit signed LE

    uint8_t fmt = 0;

    // Sample rate selection
    switch (sampleRate) {
        case 8000:  fmt = 0x00; break;
        case 11025: fmt = 0x20; break;
        case 16000: fmt = 0x40; break;
        case 22050: fmt = 0x60; break;
        case 32000: fmt = 0x80; break;
        case 44100: fmt = 0xA0; break;
        case 48000: fmt = 0xC0; break;
        default:    fmt = 0x00; break; // 8 kHz default
    }

    // Stereo
    if (channels >= 2) fmt |= 0x10;

    // Format
    if (bits == 16) fmt |= 0x02; // 16-bit signed LE

    // Enter MCE mode and write format
    sparc::mmio_write8(CS4231_BASE + CS4231_IAR, 0x40 | CS4231_PLAY_FORMAT);
    sparc::mmio_write8(CS4231_BASE + CS4231_IDR, fmt);

    // Exit MCE mode
    sparc::mmio_write8(CS4231_BASE + CS4231_IAR, CS4231_PLAY_FORMAT);
    cs4231_wait_ready();

    return true;
}

void cs4231_set_volume(uint8_t left, uint8_t right)
{
    // CS4231 output attenuation: 0 = max volume, 63 = min
    // Invert: volume 63 -> atten 0, volume 0 -> atten 63
    uint8_t attenL = (left > 63) ? 0 : (63 - left);
    uint8_t attenR = (right > 63) ? 0 : (63 - right);

    cs4231_write_reg(CS4231_LEFT_OUTPUT, attenL);
    cs4231_write_reg(CS4231_RIGHT_OUTPUT, attenR);
}

void cs4231_set_mute(bool mute)
{
    uint8_t leftOut  = cs4231_read_reg(CS4231_LEFT_OUTPUT);
    uint8_t rightOut = cs4231_read_reg(CS4231_RIGHT_OUTPUT);

    if (mute) {
        leftOut  |= 0x80;  // mute bit
        rightOut |= 0x80;
    } else {
        leftOut  &= ~0x80;
        rightOut &= ~0x80;
    }

    cs4231_write_reg(CS4231_LEFT_OUTPUT, leftOut);
    cs4231_write_reg(CS4231_RIGHT_OUTPUT, rightOut);
}

} // namespace pci_audio
} // namespace sparc
} // namespace arch
} // namespace kernel
