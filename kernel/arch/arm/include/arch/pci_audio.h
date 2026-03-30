//
// ARM has no PCI bus.  Audio on ARM platforms is typically provided
// by an I2S controller connected to an external codec (e.g. WM8731,
// WM8960, or the Versatile/RealView PL041 AACI).
//
// This module provides a stub PCI audio backend and an I2S/codec
// audio interface for ARM.
//
// The PL041 (Advanced Audio CODEC Interface) is a full-duplex audio
// controller on ARM Versatile/RealView boards, accessible via MMIO.
// QEMU virt and versatilepb emulate a PL041 at 0x10004000.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace arm {
namespace pci_audio {

// No PCI on ARM — stubs
bool init();
bool is_available();

// ================================================================
// PL041 AACI (Advanced Audio CODEC Interface) — ARM Versatile
// ================================================================

// PL041 well-known MMIO addresses (QEMU versatilepb / virt)
static const uint32_t PL041_BASE = 0x10004000u;
static const uint32_t PL041_SIZE = 0x00001000u;

// PL041 register offsets
static const uint32_t AACI_CSCH1  = 0x00;  // Channel 1 status/control
static const uint32_t AACI_CSCH2  = 0x14;  // Channel 2 status/control
static const uint32_t AACI_CSCH3  = 0x28;  // Channel 3 status/control
static const uint32_t AACI_CSCH4  = 0x3C;  // Channel 4 status/control

// Per-channel register offsets (relative to channel base)
static const uint32_t AACI_CSR    = 0x00;  // Channel Status Register
static const uint32_t AACI_CCR    = 0x04;  // Channel Control Register
static const uint32_t AACI_DR     = 0x08;  // Data Register (FIFO)
static const uint32_t AACI_ISR    = 0x0C;  // Interrupt Status Register

// Global registers
static const uint32_t AACI_SL1RX  = 0x50;  // Slot 1 RX
static const uint32_t AACI_SL1TX  = 0x54;  // Slot 1 TX
static const uint32_t AACI_SL2RX  = 0x58;  // Slot 2 RX
static const uint32_t AACI_SL2TX  = 0x5C;  // Slot 2 TX
static const uint32_t AACI_SLFR   = 0x68;  // Slot Flag Register
static const uint32_t AACI_INTCLR = 0x6C;  // Interrupt Clear Register
static const uint32_t AACI_MAINCR = 0x78;  // Main Control Register
static const uint32_t AACI_RESET  = 0x7C;  // Reset Register
static const uint32_t AACI_SYNC   = 0x80;  // Sync Register
static const uint32_t AACI_ALLINTS= 0x84;  // All Interrupts Mask
static const uint32_t AACI_MAINFR = 0x88;  // Main Flag Register
static const uint32_t AACI_PERIPHID0 = 0xFE0; // Peripheral ID 0

// MAINCR bits
static const uint32_t AACI_MAINCR_SCRA  = (1u << 0);  // AACI clock reset
static const uint32_t AACI_MAINCR_IE    = (1u << 1);   // Interrupt enable
static const uint32_t AACI_MAINCR_SL1EN = (1u << 2);   // Slot 1 enable
static const uint32_t AACI_MAINCR_SL2EN = (1u << 3);   // Slot 2 enable

// CCR (Channel Control Register) bits
static const uint32_t AACI_CCR_EN    = (1u << 0);  // Channel enable
static const uint32_t AACI_CCR_16BIT = (0u << 1);  // 16-bit samples
static const uint32_t AACI_CCR_18BIT = (1u << 1);  // 18-bit samples
static const uint32_t AACI_CCR_20BIT = (2u << 1);  // 20-bit samples
static const uint32_t AACI_CCR_CDIEN = (1u << 4);  // Compact disc input enable
static const uint32_t AACI_CCR_FIEN  = (1u << 5);  // FIFO interrupt enable

// AC'97 codec registers accessed through PL041 slot TX/RX
static const uint16_t AC97_RESET        = 0x00;
static const uint16_t AC97_MASTER_VOL   = 0x02;
static const uint16_t AC97_PCM_OUT_VOL  = 0x18;
static const uint16_t AC97_REC_GAIN     = 0x1C;
static const uint16_t AC97_POWERDOWN    = 0x26;
static const uint16_t AC97_EXT_AUDIO_ID = 0x28;
static const uint16_t AC97_PCM_RATE     = 0x2C;

// Initialise the PL041 AACI controller.
bool pl041_init();

// Set PL041 playback sample rate and format.
bool pl041_configure(uint16_t sampleRate, uint8_t bits, uint8_t channels);

// Set PL041 output volume (0-63).
void pl041_set_volume(uint8_t left, uint8_t right);

// Mute / unmute.
void pl041_set_mute(bool mute);

} // namespace pci_audio
} // namespace arm
} // namespace arch
} // namespace kernel
