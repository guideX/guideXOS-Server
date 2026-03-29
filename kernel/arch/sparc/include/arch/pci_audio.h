// SPARC v8 (Sun4m) PCI Audio Backend
//
// Sun4m machines have no PCI bus.  Audio on SPARCstation systems
// is provided by the AMD AM79C30 / CS4231 codec on the SBus.
// This module provides a stub PCI audio backend and an SBus
// CS4231 audio interface for SPARC v8.
//
// The CS4231 (Crystal Semiconductor) is a full-duplex codec
// supporting 8/16-bit PCM at 8-48 kHz, accessible via MMIO
// at a well-known SBus address.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace sparc {
namespace pci_audio {

// No PCI on SPARC v8 — stubs
bool init();
bool is_available();

// ================================================================
// CS4231 SBus audio codec interface (Sun4m)
// ================================================================

// CS4231 well-known SBus MMIO addresses (QEMU SS-5)
static const uint32_t CS4231_BASE = 0x71000000u;
static const uint32_t CS4231_SIZE = 0x00000040u;

// CS4231 register offsets
static const uint32_t CS4231_IAR    = 0x10;  // Index Address Register
static const uint32_t CS4231_IDR    = 0x14;  // Indexed Data Register
static const uint32_t CS4231_STATUS = 0x18;  // Status Register
static const uint32_t CS4231_PIO    = 0x1C;  // PIO Data Register

// CS4231 indexed registers
static const uint8_t CS4231_LEFT_INPUT    = 0x00;
static const uint8_t CS4231_RIGHT_INPUT   = 0x01;
static const uint8_t CS4231_LEFT_AUX1     = 0x02;
static const uint8_t CS4231_RIGHT_AUX1    = 0x03;
static const uint8_t CS4231_LEFT_AUX2     = 0x04;
static const uint8_t CS4231_RIGHT_AUX2    = 0x05;
static const uint8_t CS4231_LEFT_OUTPUT   = 0x06;
static const uint8_t CS4231_RIGHT_OUTPUT  = 0x07;
static const uint8_t CS4231_PLAY_FORMAT   = 0x08;
static const uint8_t CS4231_IFACE_CONFIG  = 0x09;
static const uint8_t CS4231_PIN_CONTROL   = 0x0A;
static const uint8_t CS4231_REC_FORMAT    = 0x1C;

// Initialise the CS4231 SBus codec.
bool cs4231_init();

// Set CS4231 playback sample rate and format.
bool cs4231_configure(uint16_t sampleRate, uint8_t bits, uint8_t channels);

// Set CS4231 output volume (0-63).
void cs4231_set_volume(uint8_t left, uint8_t right);

// Mute / unmute.
void cs4231_set_mute(bool mute);

} // namespace pci_audio
} // namespace sparc
} // namespace arch
} // namespace kernel
