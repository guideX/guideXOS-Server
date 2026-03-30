//
// No PCI on ARM.  Provides PL041 AACI (Advanced Audio CODEC
// Interface) driver for ARM Versatile/RealView boards.
//
// The PL041 is an AC'97-compatible controller that communicates
// with an external codec via AC-link slots.  QEMU's versatilepb
// and virt machines emulate this controller at 0x10004000.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/pci_audio.h"
#include "include/arch/arm.h"

namespace kernel {
namespace arch {
namespace arm {
namespace pci_audio {

namespace {

static bool s_pl041_available = false;

static void mmio_write32(uint32_t addr, uint32_t value)
{
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(addr);
    *reg = value;
}

static uint32_t mmio_read32(uint32_t addr)
{
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(addr);
    return *reg;
}

static void spin_delay(uint32_t iterations)
{
    for (volatile uint32_t i = 0; i < iterations; ++i) {}
}

// Write an AC'97 codec register through PL041 slot 1 TX
static void ac97_write(uint16_t reg, uint16_t value)
{
    // AC'97 slot 1 command: bit 19 = write, bits [18:12] = register index,
    // bits [11:0] = reserved, bits [15:0] = value in slot 2
    uint32_t cmd = (0u << 19) |                                    // write
                   (static_cast<uint32_t>(reg >> 1) << 12) |       // register index
                   value;
    mmio_write32(PL041_BASE + AACI_SL1TX, cmd);
    spin_delay(1000);
}

// Read an AC'97 codec register through PL041 slot 1 RX
static uint16_t ac97_read(uint16_t reg)
{
    // Issue a read command
    uint32_t cmd = (1u << 19) |                                    // read
                   (static_cast<uint32_t>(reg >> 1) << 12);
    mmio_write32(PL041_BASE + AACI_SL1TX, cmd);
    spin_delay(1000);

    uint32_t resp = mmio_read32(PL041_BASE + AACI_SL1RX);
    return static_cast<uint16_t>(resp & 0xFFFF);
}

} // anonymous namespace

// PCI stubs
bool init() { return pl041_init(); }
bool is_available() { return s_pl041_available; }

// ================================================================
// PL041 AACI driver
// ================================================================

bool pl041_init()
{
    // Probe: read the peripheral ID register
    uint32_t periph_id = mmio_read32(PL041_BASE + AACI_PERIPHID0);
    if (periph_id == 0 || periph_id == 0xFFFFFFFF) {
        s_pl041_available = false;
        return false;
    }

    // Reset the AACI controller
    mmio_write32(PL041_BASE + AACI_RESET, 0x01);
    spin_delay(10000);
    mmio_write32(PL041_BASE + AACI_RESET, 0x00);
    spin_delay(10000);

    // Enable the controller: slot 1+2, interrupts off initially
    mmio_write32(PL041_BASE + AACI_MAINCR,
                 AACI_MAINCR_SCRA | AACI_MAINCR_SL1EN | AACI_MAINCR_SL2EN);
    spin_delay(10000);

    // Reset the AC'97 codec
    ac97_write(AC97_RESET, 0x0000);
    spin_delay(50000);

    // Power up all DAC/ADC sections
    ac97_write(AC97_POWERDOWN, 0x0000);
    spin_delay(10000);

    // Set default format: 48 kHz stereo 16-bit
    pl041_configure(48000, 16, 2);

    // Unmute outputs and set reasonable default volume
    pl041_set_volume(48, 48);
    pl041_set_mute(false);

    s_pl041_available = true;
    return true;
}

bool pl041_configure(uint16_t sampleRate, uint8_t bits, uint8_t channels)
{
    (void)channels; // AC'97 is always stereo on the link

    // Set sample rate via AC'97 extended audio register
    ac97_write(AC97_PCM_RATE, sampleRate);

    // Configure channel 1 (playback) in the PL041
    uint32_t ccr = AACI_CCR_EN | AACI_CCR_FIEN;

    // Bits per sample
    if (bits == 18) {
        ccr |= AACI_CCR_18BIT;
    } else if (bits == 20) {
        ccr |= AACI_CCR_20BIT;
    } else {
        ccr |= AACI_CCR_16BIT; // default 16-bit
    }

    mmio_write32(PL041_BASE + AACI_CSCH1 + AACI_CCR, ccr);

    return true;
}

void pl041_set_volume(uint8_t left, uint8_t right)
{
    // AC'97 master volume: bits [5:0] = attenuation (0 = max, 63 = min)
    // Invert: volume 63 -> atten 0, volume 0 -> atten 63
    uint8_t attenL = (left > 63) ? 0 : (63 - left);
    uint8_t attenR = (right > 63) ? 0 : (63 - right);

    uint16_t vol = static_cast<uint16_t>((attenL << 8) | attenR);
    ac97_write(AC97_MASTER_VOL, vol);
    ac97_write(AC97_PCM_OUT_VOL, vol);
}

void pl041_set_mute(bool mute)
{
    uint16_t vol = ac97_read(AC97_MASTER_VOL);

    if (mute) {
        vol |= 0x8000;  // mute bit
    } else {
        vol &= ~0x8000u;
    }

    ac97_write(AC97_MASTER_VOL, vol);
}

} // namespace pci_audio
} // namespace arm
} // namespace arch
} // namespace kernel
