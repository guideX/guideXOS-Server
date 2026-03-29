// PCI Audio Driver — Intel HDA (High Definition Audio) and AC'97
//
// Supports:
//   - Intel HD Audio controller (ICH6+, vendor 8086h)
//   - CORB/RIRB command/response ring buffer protocol
//   - Codec enumeration and widget parsing
//   - PCM playback and capture stream descriptors
//   - AC'97 legacy codec interface (via NABM port I/O or MMIO)
//
// Architecture support:
//   x86/amd64 : PCI port-I/O config + MMIO BAR
//   IA-64     : PCI MMCFG + MMIO BAR
//   SPARC v9  : PCI psycho/sabre bridge + MMIO BAR
//   SPARC v8  : no PCI (CS4231 SBus audio handled separately)
//
// Reference: Intel High Definition Audio Specification Rev 1.0a
//            Intel ICH6/ICH7/ICH8 datasheets
//            AC'97 Component Specification Rev 2.3
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_PCI_AUDIO_H
#define KERNEL_PCI_AUDIO_H

#include "kernel/types.h"

namespace kernel {
namespace pci_audio {

// ================================================================
// PCI class/subclass for audio controllers
// ================================================================

static const uint8_t PCI_CLASS_MULTIMEDIA  = 0x04;
static const uint8_t PCI_SUBCLASS_AUDIO    = 0x01;   // Audio device
static const uint8_t PCI_SUBCLASS_HDA      = 0x03;   // HD Audio

// ================================================================
// Intel HDA controller register offsets (from MMIO BAR0)
// ================================================================

static const uint32_t HDA_GCAP      = 0x00;  // Global Capabilities
static const uint32_t HDA_VMIN      = 0x02;  // Minor Version
static const uint32_t HDA_VMAJ      = 0x03;  // Major Version
static const uint32_t HDA_OUTPAY    = 0x04;  // Output Payload Capability
static const uint32_t HDA_INPAY     = 0x06;  // Input Payload Capability
static const uint32_t HDA_GCTL      = 0x08;  // Global Control
static const uint32_t HDA_WAKEEN    = 0x0C;  // Wake Enable
static const uint32_t HDA_STATESTS  = 0x0E;  // State Change Status
static const uint32_t HDA_GSTS      = 0x10;  // Global Status
static const uint32_t HDA_INTCTL    = 0x20;  // Interrupt Control
static const uint32_t HDA_INTSTS    = 0x24;  // Interrupt Status

// CORB registers
static const uint32_t HDA_CORBLBASE = 0x40;
static const uint32_t HDA_CORBUBASE = 0x44;
static const uint32_t HDA_CORBWP    = 0x48;  // Write Pointer
static const uint32_t HDA_CORBRP    = 0x4A;  // Read Pointer
static const uint32_t HDA_CORBCTL   = 0x4C;  // Control
static const uint32_t HDA_CORBSTS   = 0x4D;  // Status
static const uint32_t HDA_CORBSIZE  = 0x4E;  // Size capability

// RIRB registers
static const uint32_t HDA_RIRBLBASE = 0x50;
static const uint32_t HDA_RIRBUBASE = 0x54;
static const uint32_t HDA_RIRBWP    = 0x58;
static const uint32_t HDA_RINTCNT   = 0x5A;
static const uint32_t HDA_RIRBCTL   = 0x5C;
static const uint32_t HDA_RIRBSTS   = 0x5D;
static const uint32_t HDA_RIRBSIZE  = 0x5E;

// Stream descriptor base (SD0 = output stream 0)
static const uint32_t HDA_SD_BASE      = 0x80;
static const uint32_t HDA_SD_INTERVAL  = 0x20; // each SD is 0x20 bytes

// Stream descriptor register offsets (relative to SD base)
static const uint32_t HDA_SD_CTL       = 0x00;
static const uint32_t HDA_SD_STS       = 0x03;
static const uint32_t HDA_SD_LPIB      = 0x04;
static const uint32_t HDA_SD_CBL       = 0x08;
static const uint32_t HDA_SD_LVI       = 0x0C;
static const uint32_t HDA_SD_FMT       = 0x12;
static const uint32_t HDA_SD_BDPL      = 0x18;
static const uint32_t HDA_SD_BDPU      = 0x1C;

// GCTL bits
static const uint32_t HDA_GCTL_CRST    = 0x01;  // Controller Reset

// INTCTL bits
static const uint32_t HDA_INTCTL_GIE   = 0x80000000u; // Global Interrupt Enable
static const uint32_t HDA_INTCTL_CIE   = 0x40000000u; // Controller Interrupt Enable

// CORBCTL bits
static const uint8_t  HDA_CORBCTL_RUN  = 0x02;
static const uint8_t  HDA_CORBCTL_MEIE = 0x01;

// RIRBCTL bits
static const uint8_t  HDA_RIRBCTL_RUN  = 0x02;
static const uint8_t  HDA_RIRBCTL_OIC  = 0x01; // Overrun Interrupt Control

// ================================================================
// HDA codec verb / response format
// ================================================================

// Verb format: codec_addr(4) | node_id(8) | verb(20)
// For 12-bit verb (e.g. Get Parameter): verb = verb_id(12) | payload(8)
// For 4-bit verb (e.g. Set/Get): verb = verb_id(4) | payload(16)

// Common verb IDs (12-bit)
static const uint32_t HDA_VERB_GET_PARAM        = 0xF0000;
static const uint32_t HDA_VERB_GET_CONN_SELECT  = 0xF0100;
static const uint32_t HDA_VERB_SET_CONN_SELECT  = 0x70100;
static const uint32_t HDA_VERB_GET_CONN_LIST    = 0xF0200;
static const uint32_t HDA_VERB_GET_CONV_FMT     = 0xA0000;
static const uint32_t HDA_VERB_SET_CONV_FMT     = 0x20000;
static const uint32_t HDA_VERB_GET_AMP_GAIN     = 0xB0000;
static const uint32_t HDA_VERB_SET_AMP_GAIN     = 0x30000;
static const uint32_t HDA_VERB_GET_CONV_CHAN     = 0xF0600;
static const uint32_t HDA_VERB_SET_CONV_CHAN     = 0x70600;
static const uint32_t HDA_VERB_GET_PIN_CTRL     = 0xF0700;
static const uint32_t HDA_VERB_SET_PIN_CTRL     = 0x70700;
static const uint32_t HDA_VERB_GET_EAPD_EN      = 0xF0C00;
static const uint32_t HDA_VERB_SET_EAPD_EN      = 0x70C00;
static const uint32_t HDA_VERB_GET_POWER_STATE  = 0xF0500;
static const uint32_t HDA_VERB_SET_POWER_STATE  = 0x70500;
static const uint32_t HDA_VERB_SET_CONV_STREAM  = 0x70600;

// Parameter IDs (for GET_PARAM)
static const uint8_t HDA_PARAM_VENDOR_ID       = 0x00;
static const uint8_t HDA_PARAM_REVISION_ID     = 0x02;
static const uint8_t HDA_PARAM_SUB_NODE_COUNT  = 0x04;
static const uint8_t HDA_PARAM_FUNC_GROUP_TYPE = 0x05;
static const uint8_t HDA_PARAM_AUDIO_CAPS      = 0x09;
static const uint8_t HDA_PARAM_PIN_CAPS        = 0x0C;
static const uint8_t HDA_PARAM_AMP_IN_CAPS     = 0x0D;
static const uint8_t HDA_PARAM_AMP_OUT_CAPS    = 0x12;
static const uint8_t HDA_PARAM_CONN_LIST_LEN   = 0x0E;
static const uint8_t HDA_PARAM_POWER_STATES    = 0x0F;
static const uint8_t HDA_PARAM_PCM_SIZES_RATES = 0x0A;
static const uint8_t HDA_PARAM_STREAM_FORMATS  = 0x0B;

// ================================================================
// AC'97 registers (legacy mode)
// ================================================================

static const uint16_t AC97_RESET        = 0x00;
static const uint16_t AC97_MASTER_VOL   = 0x02;
static const uint16_t AC97_HEADPHONE_VOL= 0x04;
static const uint16_t AC97_PCM_OUT_VOL  = 0x18;
static const uint16_t AC97_REC_SELECT   = 0x1A;
static const uint16_t AC97_REC_GAIN     = 0x1C;
static const uint16_t AC97_GENERAL_PURPOSE = 0x20;
static const uint16_t AC97_POWERDOWN    = 0x26;
static const uint16_t AC97_EXT_AUDIO_ID = 0x28;
static const uint16_t AC97_EXT_AUDIO_CTRL = 0x2A;
static const uint16_t AC97_PCM_FRONT_RATE = 0x2C;
static const uint16_t AC97_VENDOR_ID1   = 0x7C;
static const uint16_t AC97_VENDOR_ID2   = 0x7E;

// ================================================================
// BDL (Buffer Descriptor List) entry
// ================================================================

struct BDLEntry {
    uint64_t address;    // physical address of buffer
    uint32_t length;     // length in bytes
    uint32_t flags;      // bit 0 = IOC (Interrupt On Completion)
};

static const uint32_t BDL_FLAG_IOC = 0x01;
static const uint8_t  MAX_BDL_ENTRIES = 32;

// ================================================================
// Audio controller type
// ================================================================

enum AudioControllerType : uint8_t {
    AUDIO_NONE = 0,
    AUDIO_HDA  = 1,  // Intel High Definition Audio
    AUDIO_AC97 = 2,  // AC'97
};

// ================================================================
// HDA codec info
// ================================================================

struct HDACodec {
    bool     present;
    uint8_t  address;      // 0-14
    uint32_t vendorId;
    uint32_t revisionId;
    uint8_t  startNode;
    uint8_t  nodeCount;
    uint8_t  dacNode;       // first output DAC widget found
    uint8_t  adcNode;       // first input ADC widget found
    uint8_t  pinOutNode;    // line out / headphone pin
    uint8_t  pinInNode;     // mic in pin
};

static const uint8_t MAX_CODECS = 4;

// ================================================================
// PCM stream descriptor
// ================================================================

struct PCMStream {
    bool     active;
    bool     isOutput;      // true = playback, false = capture
    uint8_t  streamIndex;   // SD register index
    uint8_t  streamTag;     // 1-15
    uint16_t sampleRate;    // Hz
    uint8_t  bitsPerSample; // 16, 20, 24, 32
    uint8_t  channels;
    uint64_t bdlPhysAddr;   // physical address of BDL
    BDLEntry bdl[MAX_BDL_ENTRIES];
    uint8_t  bdlCount;
    bool     running;
};

// ================================================================
// PCI audio device instance
// ================================================================

struct AudioController {
    bool                active;
    AudioControllerType type;
    uint8_t             pciBus;
    uint8_t             pciDev;
    uint8_t             pciFun;
    uint16_t            vendorId;
    uint16_t            deviceId;
    uint64_t            mmioBase;     // BAR0 for HDA
    uint32_t            mmioSize;
    uint16_t            ioBase;       // for AC'97 (NABM, NAMBAR)
    HDACodec            codecs[MAX_CODECS];
    uint8_t             codecCount;
    PCMStream           playback;
    PCMStream           capture;
    // CORB/RIRB state
    uint64_t            corbPhysAddr;
    uint64_t            rirbPhysAddr;
    uint16_t            corbSize;     // number of entries
    uint16_t            rirbSize;
    uint16_t            corbWritePtr;
    uint16_t            rirbReadPtr;
};

static const uint8_t MAX_AUDIO_CONTROLLERS = 4;

// ================================================================
// Public API
// ================================================================

// Scan PCI for audio controllers and initialise them.
void init();

// Return number of discovered audio controllers.
uint8_t controller_count();

// Get controller info by index.
const AudioController* get_controller(uint8_t index);

// ----------------------------------------------------------------
// HDA codec commands
// ----------------------------------------------------------------

// Send a verb to a codec and wait for the response.
// Returns true on success, storing the response in *response.
bool hda_send_verb(uint8_t ctrlIndex, uint8_t codecAddr,
                   uint8_t nodeId, uint32_t verb,
                   uint32_t* response);

// ----------------------------------------------------------------
// Volume control
// ----------------------------------------------------------------

// Set the master output volume (0-100 linear scale).
bool set_master_volume(uint8_t ctrlIndex, uint8_t percent);

// Set the PCM output volume (0-100 linear scale).
bool set_pcm_volume(uint8_t ctrlIndex, uint8_t percent);

// Get current master volume (0-100).
uint8_t get_master_volume(uint8_t ctrlIndex);

// Mute / unmute master output.
bool set_mute(uint8_t ctrlIndex, bool mute);
bool get_mute(uint8_t ctrlIndex);

// ----------------------------------------------------------------
// PCM streaming
// ----------------------------------------------------------------

// Configure a playback stream.
bool configure_playback(uint8_t ctrlIndex,
                        uint16_t sampleRate,
                        uint8_t bits,
                        uint8_t channels);

// Configure a capture stream.
bool configure_capture(uint8_t ctrlIndex,
                       uint16_t sampleRate,
                       uint8_t bits,
                       uint8_t channels);

// Start / stop a stream.
bool start_playback(uint8_t ctrlIndex);
bool stop_playback(uint8_t ctrlIndex);
bool start_capture(uint8_t ctrlIndex);
bool stop_capture(uint8_t ctrlIndex);

// Submit a buffer for playback (adds to BDL).
bool submit_playback_buffer(uint8_t ctrlIndex,
                            uint64_t physAddr,
                            uint32_t length);

// Submit a buffer for capture (adds to BDL).
bool submit_capture_buffer(uint8_t ctrlIndex,
                           uint64_t physAddr,
                           uint32_t length);

// Get the current playback position (LPIB).
uint32_t get_playback_position(uint8_t ctrlIndex);

} // namespace pci_audio
} // namespace kernel

#endif // KERNEL_PCI_AUDIO_H
