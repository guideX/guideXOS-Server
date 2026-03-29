// PCI Audio Driver — Implementation
//
// Intel HDA controller discovery, CORB/RIRB initialisation, codec
// enumeration, and PCM stream management.
//
// The actual PCI config-space and MMIO access is delegated to
// arch-specific pci_audio backends.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/pci_audio.h"
#include "include/kernel/arch.h"

namespace kernel {
namespace pci_audio {

// ================================================================
// Internal state
// ================================================================

static AudioController s_controllers[MAX_AUDIO_CONTROLLERS];
static uint8_t         s_controllerCount = 0;

// ================================================================
// Helpers
// ================================================================

static void memzero(void* dst, uint32_t len)
{
    uint8_t* p = static_cast<uint8_t*>(dst);
    for (uint32_t i = 0; i < len; ++i) p[i] = 0;
}

// ================================================================
// MMIO register access (controller BAR0)
// ================================================================

static uint8_t hda_read8(const AudioController* ctrl, uint32_t offset)
{
    volatile uint8_t* reg = reinterpret_cast<volatile uint8_t*>(
        static_cast<uintptr_t>(ctrl->mmioBase + offset));
    return *reg;
}

static uint16_t hda_read16(const AudioController* ctrl, uint32_t offset)
{
    volatile uint16_t* reg = reinterpret_cast<volatile uint16_t*>(
        static_cast<uintptr_t>(ctrl->mmioBase + offset));
    return *reg;
}

static uint32_t hda_read32(const AudioController* ctrl, uint32_t offset)
{
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(ctrl->mmioBase + offset));
    return *reg;
}

static void hda_write8(const AudioController* ctrl, uint32_t offset, uint8_t val)
{
    volatile uint8_t* reg = reinterpret_cast<volatile uint8_t*>(
        static_cast<uintptr_t>(ctrl->mmioBase + offset));
    *reg = val;
}

static void hda_write16(const AudioController* ctrl, uint32_t offset, uint16_t val)
{
    volatile uint16_t* reg = reinterpret_cast<volatile uint16_t*>(
        static_cast<uintptr_t>(ctrl->mmioBase + offset));
    *reg = val;
}

static void hda_write32(const AudioController* ctrl, uint32_t offset, uint32_t val)
{
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(ctrl->mmioBase + offset));
    *reg = val;
}

// ================================================================
// Simple delay (spin loop)
// ================================================================

static void spin_delay(uint32_t iterations)
{
    for (volatile uint32_t i = 0; i < iterations; ++i) {}
}

// ================================================================
// PCI config space access (arch-specific, forward declared)
//
// These are thin wrappers that call arch::outl/inl (x86/amd64)
// or arch::mmio_read32 (IA-64/SPARC64).
// ================================================================

#if ARCH_HAS_PORT_IO

static uint32_t pci_cfg_read32(uint8_t bus, uint8_t dev,
                                uint8_t func, uint8_t offset)
{
    uint32_t addr = 0x80000000u |
                    (static_cast<uint32_t>(bus)  << 16) |
                    (static_cast<uint32_t>(dev)  << 11) |
                    (static_cast<uint32_t>(func) << 8)  |
                    (offset & 0xFC);
    arch::outl(0x0CF8, addr);
    return arch::inl(0x0CFC);
}

static void pci_cfg_write32(uint8_t bus, uint8_t dev,
                             uint8_t func, uint8_t offset, uint32_t val)
{
    uint32_t addr = 0x80000000u |
                    (static_cast<uint32_t>(bus)  << 16) |
                    (static_cast<uint32_t>(dev)  << 11) |
                    (static_cast<uint32_t>(func) << 8)  |
                    (offset & 0xFC);
    arch::outl(0x0CF8, addr);
    arch::outl(0x0CFC, val);
}

static uint16_t pci_cfg_read16(uint8_t bus, uint8_t dev,
                                uint8_t func, uint8_t offset)
{
    uint32_t val = pci_cfg_read32(bus, dev, func, offset & 0xFC);
    return static_cast<uint16_t>(val >> ((offset & 2) * 8));
}

#else // MMIO-based PCI config

// IA-64 and SPARC64 use memory-mapped config space.
// Use a well-known MMCFG base; real systems get this from ACPI MCFG.
#if defined(ARCH_IA64)
static const uint64_t PCI_MMCFG_BASE = 0xF0000000ULL;
#elif defined(ARCH_SPARC64)
static const uint64_t PCI_MMCFG_BASE = 0x1FE01000000ULL;
#else
static const uint64_t PCI_MMCFG_BASE = 0;
#endif

static uint32_t pci_cfg_read32(uint8_t bus, uint8_t dev,
                                uint8_t func, uint8_t offset)
{
    if (PCI_MMCFG_BASE == 0) return 0xFFFFFFFF;
    uint64_t addr = PCI_MMCFG_BASE |
                    (static_cast<uint64_t>(bus)  << 20) |
                    (static_cast<uint64_t>(dev)  << 15) |
                    (static_cast<uint64_t>(func) << 12) |
                    (offset & 0xFFC);
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(addr));
    return *reg;
}

static void pci_cfg_write32(uint8_t bus, uint8_t dev,
                             uint8_t func, uint8_t offset, uint32_t val)
{
    if (PCI_MMCFG_BASE == 0) return;
    uint64_t addr = PCI_MMCFG_BASE |
                    (static_cast<uint64_t>(bus)  << 20) |
                    (static_cast<uint64_t>(dev)  << 15) |
                    (static_cast<uint64_t>(func) << 12) |
                    (offset & 0xFFC);
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(addr));
    *reg = val;
}

static uint16_t pci_cfg_read16(uint8_t bus, uint8_t dev,
                                uint8_t func, uint8_t offset)
{
    uint32_t val = pci_cfg_read32(bus, dev, func, offset & 0xFC);
    return static_cast<uint16_t>(val >> ((offset & 2) * 8));
}

#endif // ARCH_HAS_PORT_IO

// ================================================================
// HDA controller reset
// ================================================================

static bool hda_reset(AudioController* ctrl)
{
    // Clear CRST to enter reset
    hda_write32(ctrl, HDA_GCTL, 0);
    spin_delay(100000);

    // Set CRST to come out of reset
    hda_write32(ctrl, HDA_GCTL, HDA_GCTL_CRST);
    spin_delay(100000);

    // Wait for CRST to read back 1
    for (int i = 0; i < 1000; ++i) {
        if (hda_read32(ctrl, HDA_GCTL) & HDA_GCTL_CRST) return true;
        spin_delay(1000);
    }
    return false;
}

// ================================================================
// CORB/RIRB initialisation (simplified — uses 256-entry rings)
// ================================================================

// NOTE: In a real kernel, CORB and RIRB buffers would be allocated
// from a DMA-capable physical memory allocator.  Here we use a
// static buffer for demonstration — a proper implementation would
// call the kernel memory allocator.

static uint32_t s_corbBuf[256 * MAX_AUDIO_CONTROLLERS];
static uint64_t s_rirbBuf[256 * MAX_AUDIO_CONTROLLERS]; // response + solicited flag

static bool hda_init_corb_rirb(AudioController* ctrl, uint8_t ctrlIdx)
{
    // Use static buffers (offset by controller index)
    uint32_t* corb = &s_corbBuf[ctrlIdx * 256];
    uint64_t* rirb = &s_rirbBuf[ctrlIdx * 256];

    // Zero buffers
    for (int i = 0; i < 256; ++i) { corb[i] = 0; rirb[i] = 0; }

    ctrl->corbPhysAddr = reinterpret_cast<uintptr_t>(corb);
    ctrl->rirbPhysAddr = reinterpret_cast<uintptr_t>(rirb);
    ctrl->corbSize = 256;
    ctrl->rirbSize = 256;

    // Stop CORB/RIRB
    hda_write8(ctrl, HDA_CORBCTL, 0);
    hda_write8(ctrl, HDA_RIRBCTL, 0);
    spin_delay(10000);

    // Set CORB base address
    hda_write32(ctrl, HDA_CORBLBASE, static_cast<uint32_t>(ctrl->corbPhysAddr));
    hda_write32(ctrl, HDA_CORBUBASE, static_cast<uint32_t>(ctrl->corbPhysAddr >> 32));

    // Set CORB size (select 256 entries if supported)
    uint8_t corbSizeCap = hda_read8(ctrl, HDA_CORBSIZE);
    if (corbSizeCap & 0x40) {
        hda_write8(ctrl, HDA_CORBSIZE, 0x02); // 256 entries
    } else if (corbSizeCap & 0x20) {
        hda_write8(ctrl, HDA_CORBSIZE, 0x01); // 16 entries
        ctrl->corbSize = 16;
    } else {
        hda_write8(ctrl, HDA_CORBSIZE, 0x00); // 2 entries
        ctrl->corbSize = 2;
    }

    // Reset CORB read pointer
    hda_write16(ctrl, HDA_CORBRP, 0x8000);
    spin_delay(10000);
    hda_write16(ctrl, HDA_CORBRP, 0x0000);

    // Set CORB write pointer to 0
    ctrl->corbWritePtr = 0;
    hda_write16(ctrl, HDA_CORBWP, 0);

    // Set RIRB base address
    hda_write32(ctrl, HDA_RIRBLBASE, static_cast<uint32_t>(ctrl->rirbPhysAddr));
    hda_write32(ctrl, HDA_RIRBUBASE, static_cast<uint32_t>(ctrl->rirbPhysAddr >> 32));

    // Set RIRB size
    uint8_t rirbSizeCap = hda_read8(ctrl, HDA_RIRBSIZE);
    if (rirbSizeCap & 0x40) {
        hda_write8(ctrl, HDA_RIRBSIZE, 0x02); // 256
    } else if (rirbSizeCap & 0x20) {
        hda_write8(ctrl, HDA_RIRBSIZE, 0x01); // 16
        ctrl->rirbSize = 16;
    } else {
        hda_write8(ctrl, HDA_RIRBSIZE, 0x00); // 2
        ctrl->rirbSize = 2;
    }

    // Reset RIRB write pointer
    hda_write16(ctrl, HDA_RIRBWP, 0x8000);
    ctrl->rirbReadPtr = 0;

    // Set response interrupt count
    hda_write16(ctrl, HDA_RINTCNT, 1);

    // Start CORB and RIRB
    hda_write8(ctrl, HDA_CORBCTL, HDA_CORBCTL_RUN);
    hda_write8(ctrl, HDA_RIRBCTL, HDA_RIRBCTL_RUN | HDA_RIRBCTL_OIC);

    return true;
}

// ================================================================
// Send a verb via CORB and read response from RIRB
// ================================================================

bool hda_send_verb(uint8_t ctrlIndex, uint8_t codecAddr,
                   uint8_t nodeId, uint32_t verb,
                   uint32_t* response)
{
    if (ctrlIndex >= s_controllerCount) return false;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    if (!ctrl->active || ctrl->type != AUDIO_HDA) return false;

    // Build the verb: [31:28]=codec, [27:20]=nid, [19:0]=verb
    uint32_t fullVerb = (static_cast<uint32_t>(codecAddr) << 28) |
                        (static_cast<uint32_t>(nodeId) << 20) |
                        (verb & 0xFFFFF);

    // Write to CORB
    uint16_t wp = (ctrl->corbWritePtr + 1) % ctrl->corbSize;
    uint32_t* corb = &s_corbBuf[ctrlIndex * 256];
    corb[wp] = fullVerb;
    ctrl->corbWritePtr = wp;
    hda_write16(ctrl, HDA_CORBWP, wp);

    // Poll RIRB for response
    uint64_t* rirb = &s_rirbBuf[ctrlIndex * 256];
    for (int timeout = 0; timeout < 10000; ++timeout) {
        uint16_t rirbWp = hda_read16(ctrl, HDA_RIRBWP);
        if (rirbWp != ctrl->rirbReadPtr) {
            ctrl->rirbReadPtr = (ctrl->rirbReadPtr + 1) % ctrl->rirbSize;
            uint64_t resp = rirb[ctrl->rirbReadPtr];
            if (response) {
                *response = static_cast<uint32_t>(resp);
            }
            return true;
        }
        spin_delay(100);
    }

    return false; // timeout
}

// ================================================================
// Enumerate codecs on an HDA controller
// ================================================================

static void hda_enumerate_codecs(AudioController* ctrl, uint8_t ctrlIdx)
{
    // STATESTS bits indicate which codec addresses responded after reset
    uint16_t statests = hda_read16(ctrl, HDA_STATESTS);

    for (uint8_t addr = 0; addr < 15 && ctrl->codecCount < MAX_CODECS; ++addr) {
        if (!(statests & (1 << addr))) continue;

        HDACodec* codec = &ctrl->codecs[ctrl->codecCount];
        memzero(codec, sizeof(HDACodec));
        codec->address = addr;

        // Get vendor/device ID (node 0, param 0x00)
        uint32_t vendorId = 0;
        if (!hda_send_verb(ctrlIdx, addr, 0, HDA_VERB_GET_PARAM | HDA_PARAM_VENDOR_ID, &vendorId))
            continue;
        codec->vendorId = vendorId;

        // Get revision ID
        hda_send_verb(ctrlIdx, addr, 0, HDA_VERB_GET_PARAM | HDA_PARAM_REVISION_ID,
                      &codec->revisionId);

        // Get sub-node count from the root node (node 0)
        uint32_t subNodes = 0;
        hda_send_verb(ctrlIdx, addr, 0, HDA_VERB_GET_PARAM | HDA_PARAM_SUB_NODE_COUNT, &subNodes);
        codec->startNode = static_cast<uint8_t>((subNodes >> 16) & 0xFF);
        codec->nodeCount  = static_cast<uint8_t>(subNodes & 0xFF);

        // Walk function group nodes to find audio widgets
        for (uint8_t n = codec->startNode; n < codec->startNode + codec->nodeCount; ++n) {
            uint32_t fgType = 0;
            hda_send_verb(ctrlIdx, addr, n, HDA_VERB_GET_PARAM | HDA_PARAM_FUNC_GROUP_TYPE, &fgType);

            if ((fgType & 0xFF) == 0x01) {
                // Audio function group — get its sub-nodes
                uint32_t afg_sub = 0;
                hda_send_verb(ctrlIdx, addr, n, HDA_VERB_GET_PARAM | HDA_PARAM_SUB_NODE_COUNT, &afg_sub);
                uint8_t startWidget = static_cast<uint8_t>((afg_sub >> 16) & 0xFF);
                uint8_t numWidgets  = static_cast<uint8_t>(afg_sub & 0xFF);

                for (uint8_t w = startWidget; w < startWidget + numWidgets; ++w) {
                    uint32_t audioCaps = 0;
                    hda_send_verb(ctrlIdx, addr, w, HDA_VERB_GET_PARAM | HDA_PARAM_AUDIO_CAPS, &audioCaps);
                    uint8_t widgetType = static_cast<uint8_t>((audioCaps >> 20) & 0xF);

                    if (widgetType == 0x0 && codec->dacNode == 0) {
                        codec->dacNode = w; // Audio Output (DAC)
                    }
                    else if (widgetType == 0x1 && codec->adcNode == 0) {
                        codec->adcNode = w; // Audio Input (ADC)
                    }
                    else if (widgetType == 0x4) {
                        // Pin widget — check config default
                        uint32_t pinCaps = 0;
                        hda_send_verb(ctrlIdx, addr, w, HDA_VERB_GET_PARAM | HDA_PARAM_PIN_CAPS, &pinCaps);
                        if ((pinCaps & 0x10) && codec->pinOutNode == 0) {
                            codec->pinOutNode = w; // Output capable
                        }
                        if ((pinCaps & 0x20) && codec->pinInNode == 0) {
                            codec->pinInNode = w;  // Input capable
                        }
                    }
                }
            }
        }

        codec->present = true;
        ctrl->codecCount++;
    }
}

// ================================================================
// HDA stream format register encoding
// ================================================================

static uint16_t encode_hda_format(uint16_t sampleRate, uint8_t bits, uint8_t channels)
{
    uint16_t fmt = 0;

    // Base rate and multiplier/divisor
    switch (sampleRate) {
        case 8000:  fmt = 0x0500; break; // 48000 / 6
        case 11025: fmt = 0x4300; break; // 44100 / 4
        case 16000: fmt = 0x0200; break; // 48000 / 3
        case 22050: fmt = 0x4100; break; // 44100 / 2
        case 32000: fmt = 0x0A00; break; // 48000 * 2 / 3
        case 44100: fmt = 0x4000; break; // 44100 base
        case 48000: fmt = 0x0000; break; // 48000 base
        case 88200: fmt = 0x4800; break; // 44100 * 2
        case 96000: fmt = 0x0800; break; // 48000 * 2
        case 176400: fmt = 0x5800; break; // 44100 * 4
        case 192000: fmt = 0x1800; break; // 48000 * 4
        default:    fmt = 0x0000; break; // 48 kHz default
    }

    // Bits per sample
    switch (bits) {
        case 8:  fmt |= 0x0000; break;
        case 16: fmt |= 0x0010; break;
        case 20: fmt |= 0x0020; break;
        case 24: fmt |= 0x0030; break;
        case 32: fmt |= 0x0040; break;
        default: fmt |= 0x0010; break;
    }

    // Channels (0-based)
    fmt |= static_cast<uint16_t>((channels - 1) & 0x0F);

    return fmt;
}

// ================================================================
// Configure a stream descriptor
// ================================================================

static bool configure_stream(AudioController* ctrl, PCMStream* stream,
                              bool isOutput,
                              uint16_t sampleRate, uint8_t bits, uint8_t channels)
{
    stream->isOutput      = isOutput;
    stream->sampleRate    = sampleRate;
    stream->bitsPerSample = bits;
    stream->channels      = channels;

    // Determine stream descriptor index
    // GCAP tells us the number of input/output streams
    uint16_t gcap = hda_read16(ctrl, HDA_GCAP);
    uint8_t numOutputSD = static_cast<uint8_t>((gcap >> 12) & 0x0F);
    uint8_t numInputSD  = static_cast<uint8_t>((gcap >> 8) & 0x0F);
    (void)numInputSD;

    if (isOutput) {
        stream->streamIndex = 0; // first output SD
        stream->streamTag   = 1;
    } else {
        stream->streamIndex = numOutputSD; // first input SD
        stream->streamTag   = 2;
    }

    uint32_t sdBase = HDA_SD_BASE + stream->streamIndex * HDA_SD_INTERVAL;

    // Stop the stream
    uint8_t sdCtl = hda_read8(ctrl, sdBase + HDA_SD_CTL);
    hda_write8(ctrl, sdBase + HDA_SD_CTL, sdCtl & ~0x02);
    spin_delay(10000);

    // Reset the stream
    hda_write8(ctrl, sdBase + HDA_SD_CTL, 0x01);
    spin_delay(10000);
    hda_write8(ctrl, sdBase + HDA_SD_CTL, 0x00);
    spin_delay(10000);

    // Set format
    uint16_t fmt = encode_hda_format(sampleRate, bits, channels);
    hda_write16(ctrl, sdBase + HDA_SD_FMT, fmt);

    // Set stream tag in CTL bits [23:20]
    uint32_t sdCtl32 = hda_read32(ctrl, sdBase + HDA_SD_CTL);
    sdCtl32 = (sdCtl32 & ~0x00F00000u) |
              (static_cast<uint32_t>(stream->streamTag) << 20);
    hda_write32(ctrl, sdBase + HDA_SD_CTL, sdCtl32);

    stream->active = true;
    return true;
}

// ================================================================
// PCI scan for audio controllers
// ================================================================

static void scan_pci_audio()
{
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t dev = 0; dev < 32; ++dev) {
            for (uint8_t func = 0; func < 8; ++func) {
                uint32_t id = pci_cfg_read32(static_cast<uint8_t>(bus), dev, func, 0);
                if (id == 0xFFFFFFFF || id == 0x00000000) continue;

                uint32_t classReg = pci_cfg_read32(static_cast<uint8_t>(bus), dev, func, 0x08);
                uint8_t baseClass = static_cast<uint8_t>(classReg >> 24);
                uint8_t subClass  = static_cast<uint8_t>(classReg >> 16);

                bool isHda  = (baseClass == PCI_CLASS_MULTIMEDIA && subClass == PCI_SUBCLASS_HDA);
                bool isAc97 = (baseClass == PCI_CLASS_MULTIMEDIA && subClass == PCI_SUBCLASS_AUDIO);

                if (!isHda && !isAc97) continue;
                if (s_controllerCount >= MAX_AUDIO_CONTROLLERS) return;

                AudioController* ctrl = &s_controllers[s_controllerCount];
                memzero(ctrl, sizeof(AudioController));
                ctrl->pciBus   = static_cast<uint8_t>(bus);
                ctrl->pciDev   = dev;
                ctrl->pciFun   = func;
                ctrl->vendorId = static_cast<uint16_t>(id);
                ctrl->deviceId = static_cast<uint16_t>(id >> 16);

                // Read BAR0
                uint32_t bar0 = pci_cfg_read32(static_cast<uint8_t>(bus), dev, func, 0x10);
                if (bar0 & 0x01) {
                    // I/O BAR (AC'97 NABM)
                    ctrl->ioBase = static_cast<uint16_t>(bar0 & 0xFFFE);
                    ctrl->type = AUDIO_AC97;
                } else {
                    // Memory BAR (HDA MMIO)
                    bool is64 = ((bar0 >> 1) & 0x03) == 0x02;
                    uint32_t bar0Hi = 0;
                    if (is64) {
                        bar0Hi = pci_cfg_read32(static_cast<uint8_t>(bus), dev, func, 0x14);
                    }
                    ctrl->mmioBase = (static_cast<uint64_t>(bar0Hi) << 32) |
                                     (bar0 & 0xFFFFFFF0u);
                    ctrl->type = isHda ? AUDIO_HDA : AUDIO_AC97;
                }

                // Enable bus master and memory/IO space in PCI command register
                uint16_t cmd = pci_cfg_read16(static_cast<uint8_t>(bus), dev, func, 0x04);
                cmd |= 0x0007; // IO + Memory + Bus Master
                uint32_t cmd32 = pci_cfg_read32(static_cast<uint8_t>(bus), dev, func, 0x04);
                cmd32 = (cmd32 & 0xFFFF0000u) | cmd;
                pci_cfg_write32(static_cast<uint8_t>(bus), dev, func, 0x04, cmd32);

                ctrl->active = true;
                ++s_controllerCount;
            }
        }
    }
}

// ================================================================
// Public API
// ================================================================

void init()
{
    memzero(s_controllers, sizeof(s_controllers));
    s_controllerCount = 0;

    // Scan PCI (on architectures with PCI)
#if ARCH_HAS_PORT_IO || defined(ARCH_IA64) || defined(ARCH_SPARC64)
    scan_pci_audio();
#endif

    // Initialise each HDA controller
    for (uint8_t i = 0; i < s_controllerCount; ++i) {
        AudioController* ctrl = &s_controllers[i];
        if (ctrl->type == AUDIO_HDA && ctrl->mmioBase != 0) {
            if (hda_reset(ctrl)) {
                hda_init_corb_rirb(ctrl, i);
                hda_enumerate_codecs(ctrl, i);
            }
        }
    }
}

uint8_t controller_count() { return s_controllerCount; }

const AudioController* get_controller(uint8_t index)
{
    if (index >= MAX_AUDIO_CONTROLLERS || !s_controllers[index].active)
        return nullptr;
    return &s_controllers[index];
}

// ----------------------------------------------------------------
// Volume control (HDA codec verb-based)
// ----------------------------------------------------------------

bool set_master_volume(uint8_t ctrlIndex, uint8_t percent)
{
    if (ctrlIndex >= s_controllerCount) return false;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    if (!ctrl->active || ctrl->codecCount == 0) return false;

    HDACodec* codec = &ctrl->codecs[0];
    if (codec->dacNode == 0) return false;

    // HDA amp gain: 0-127 (7 bits), set output amp, left+right
    uint8_t gain = static_cast<uint8_t>((static_cast<uint32_t>(percent) * 127) / 100);
    uint32_t verb = HDA_VERB_SET_AMP_GAIN |
                    0xB000 | // set output, left+right
                    gain;

    uint32_t resp = 0;
    return hda_send_verb(ctrlIndex, codec->address, codec->dacNode, verb, &resp);
}

bool set_pcm_volume(uint8_t ctrlIndex, uint8_t percent)
{
    return set_master_volume(ctrlIndex, percent);
}

uint8_t get_master_volume(uint8_t ctrlIndex)
{
    if (ctrlIndex >= s_controllerCount) return 0;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    if (!ctrl->active || ctrl->codecCount == 0) return 0;

    HDACodec* codec = &ctrl->codecs[0];
    if (codec->dacNode == 0) return 0;

    uint32_t verb = HDA_VERB_GET_AMP_GAIN | 0x8000; // output, left
    uint32_t resp = 0;
    if (!hda_send_verb(ctrlIndex, codec->address, codec->dacNode, verb, &resp))
        return 0;

    uint8_t gain = static_cast<uint8_t>(resp & 0x7F);
    return static_cast<uint8_t>((static_cast<uint32_t>(gain) * 100) / 127);
}

bool set_mute(uint8_t ctrlIndex, bool mute)
{
    if (ctrlIndex >= s_controllerCount) return false;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    if (!ctrl->active || ctrl->codecCount == 0) return false;

    HDACodec* codec = &ctrl->codecs[0];
    if (codec->dacNode == 0) return false;

    uint32_t verb = HDA_VERB_SET_AMP_GAIN | 0xB000;
    if (mute) verb |= 0x80; // mute bit

    uint32_t resp = 0;
    return hda_send_verb(ctrlIndex, codec->address, codec->dacNode, verb, &resp);
}

bool get_mute(uint8_t ctrlIndex)
{
    if (ctrlIndex >= s_controllerCount) return false;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    if (!ctrl->active || ctrl->codecCount == 0) return false;

    HDACodec* codec = &ctrl->codecs[0];
    if (codec->dacNode == 0) return false;

    uint32_t verb = HDA_VERB_GET_AMP_GAIN | 0x8000;
    uint32_t resp = 0;
    if (!hda_send_verb(ctrlIndex, codec->address, codec->dacNode, verb, &resp))
        return false;

    return (resp & 0x80) != 0;
}

// ----------------------------------------------------------------
// PCM streaming
// ----------------------------------------------------------------

bool configure_playback(uint8_t ctrlIndex, uint16_t sampleRate,
                        uint8_t bits, uint8_t channels)
{
    if (ctrlIndex >= s_controllerCount) return false;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    if (!ctrl->active || ctrl->type != AUDIO_HDA) return false;

    return configure_stream(ctrl, &ctrl->playback, true,
                            sampleRate, bits, channels);
}

bool configure_capture(uint8_t ctrlIndex, uint16_t sampleRate,
                       uint8_t bits, uint8_t channels)
{
    if (ctrlIndex >= s_controllerCount) return false;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    if (!ctrl->active || ctrl->type != AUDIO_HDA) return false;

    return configure_stream(ctrl, &ctrl->capture, false,
                            sampleRate, bits, channels);
}

bool start_playback(uint8_t ctrlIndex)
{
    if (ctrlIndex >= s_controllerCount) return false;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    if (!ctrl->active || !ctrl->playback.active) return false;

    uint32_t sdBase = HDA_SD_BASE + ctrl->playback.streamIndex * HDA_SD_INTERVAL;
    uint8_t sdCtl = hda_read8(ctrl, sdBase + HDA_SD_CTL);
    hda_write8(ctrl, sdBase + HDA_SD_CTL, sdCtl | 0x02); // RUN bit
    ctrl->playback.running = true;
    return true;
}

bool stop_playback(uint8_t ctrlIndex)
{
    if (ctrlIndex >= s_controllerCount) return false;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    if (!ctrl->active || !ctrl->playback.active) return false;

    uint32_t sdBase = HDA_SD_BASE + ctrl->playback.streamIndex * HDA_SD_INTERVAL;
    uint8_t sdCtl = hda_read8(ctrl, sdBase + HDA_SD_CTL);
    hda_write8(ctrl, sdBase + HDA_SD_CTL, sdCtl & ~0x02);
    ctrl->playback.running = false;
    return true;
}

bool start_capture(uint8_t ctrlIndex)
{
    if (ctrlIndex >= s_controllerCount) return false;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    if (!ctrl->active || !ctrl->capture.active) return false;

    uint32_t sdBase = HDA_SD_BASE + ctrl->capture.streamIndex * HDA_SD_INTERVAL;
    uint8_t sdCtl = hda_read8(ctrl, sdBase + HDA_SD_CTL);
    hda_write8(ctrl, sdBase + HDA_SD_CTL, sdCtl | 0x02);
    ctrl->capture.running = true;
    return true;
}

bool stop_capture(uint8_t ctrlIndex)
{
    if (ctrlIndex >= s_controllerCount) return false;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    if (!ctrl->active || !ctrl->capture.active) return false;

    uint32_t sdBase = HDA_SD_BASE + ctrl->capture.streamIndex * HDA_SD_INTERVAL;
    uint8_t sdCtl = hda_read8(ctrl, sdBase + HDA_SD_CTL);
    hda_write8(ctrl, sdBase + HDA_SD_CTL, sdCtl & ~0x02);
    ctrl->capture.running = false;
    return true;
}

bool submit_playback_buffer(uint8_t ctrlIndex, uint64_t physAddr, uint32_t length)
{
    if (ctrlIndex >= s_controllerCount) return false;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    PCMStream* s = &ctrl->playback;
    if (!s->active || s->bdlCount >= MAX_BDL_ENTRIES) return false;

    BDLEntry& e = s->bdl[s->bdlCount];
    e.address = physAddr;
    e.length  = length;
    e.flags   = BDL_FLAG_IOC;
    s->bdlCount++;

    // Update stream descriptor BDL pointer and LVI
    uint32_t sdBase = HDA_SD_BASE + s->streamIndex * HDA_SD_INTERVAL;
    uint64_t bdlAddr = reinterpret_cast<uintptr_t>(s->bdl);
    hda_write32(ctrl, sdBase + HDA_SD_BDPL, static_cast<uint32_t>(bdlAddr));
    hda_write32(ctrl, sdBase + HDA_SD_BDPU, static_cast<uint32_t>(bdlAddr >> 32));
    hda_write16(ctrl, sdBase + HDA_SD_LVI, static_cast<uint16_t>(s->bdlCount - 1));

    // Update CBL (total bytes)
    uint32_t totalBytes = 0;
    for (uint8_t i = 0; i < s->bdlCount; ++i) totalBytes += s->bdl[i].length;
    hda_write32(ctrl, sdBase + HDA_SD_CBL, totalBytes);

    return true;
}

bool submit_capture_buffer(uint8_t ctrlIndex, uint64_t physAddr, uint32_t length)
{
    if (ctrlIndex >= s_controllerCount) return false;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    PCMStream* s = &ctrl->capture;
    if (!s->active || s->bdlCount >= MAX_BDL_ENTRIES) return false;

    BDLEntry& e = s->bdl[s->bdlCount];
    e.address = physAddr;
    e.length  = length;
    e.flags   = BDL_FLAG_IOC;
    s->bdlCount++;

    uint32_t sdBase = HDA_SD_BASE + s->streamIndex * HDA_SD_INTERVAL;
    uint64_t bdlAddr = reinterpret_cast<uintptr_t>(s->bdl);
    hda_write32(ctrl, sdBase + HDA_SD_BDPL, static_cast<uint32_t>(bdlAddr));
    hda_write32(ctrl, sdBase + HDA_SD_BDPU, static_cast<uint32_t>(bdlAddr >> 32));
    hda_write16(ctrl, sdBase + HDA_SD_LVI, static_cast<uint16_t>(s->bdlCount - 1));

    uint32_t totalBytes = 0;
    for (uint8_t i = 0; i < s->bdlCount; ++i) totalBytes += s->bdl[i].length;
    hda_write32(ctrl, sdBase + HDA_SD_CBL, totalBytes);

    return true;
}

uint32_t get_playback_position(uint8_t ctrlIndex)
{
    if (ctrlIndex >= s_controllerCount) return 0;
    AudioController* ctrl = &s_controllers[ctrlIndex];
    if (!ctrl->playback.active) return 0;

    uint32_t sdBase = HDA_SD_BASE + ctrl->playback.streamIndex * HDA_SD_INTERVAL;
    return hda_read32(ctrl, sdBase + HDA_SD_LPIB);
}

} // namespace pci_audio
} // namespace kernel
