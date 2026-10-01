#ifndef GUIDEXOS_AMD64_UHCI_TRANSFER_LOGIC_H
#define GUIDEXOS_AMD64_UHCI_TRANSFER_LOGIC_H

#include <stddef.h>
#include <stdint.h>

namespace kernel {
namespace arch {
namespace amd64 {
namespace uhci {

struct alignas(16) TransferDescriptor {
    uint32_t link;
    uint32_t status;
    uint32_t token;
    uint32_t buffer;
};

struct alignas(16) QueueHead {
    uint32_t headLink;
    uint32_t elementLink;
    uint32_t reserved[2];
};

static_assert(sizeof(TransferDescriptor) == 16,
              "UHCI TD is four 32-bit words");
static_assert(alignof(TransferDescriptor) == 16,
              "UHCI TDs require 16-byte alignment");
static_assert(offsetof(TransferDescriptor, status) == 4,
              "UHCI TD status offset");
static_assert(offsetof(TransferDescriptor, token) == 8,
              "UHCI TD token offset");
static_assert(offsetof(TransferDescriptor, buffer) == 12,
              "UHCI TD buffer offset");
static_assert(sizeof(QueueHead) == 16, "UHCI QH occupies one aligned slot");
static_assert(alignof(QueueHead) == 16, "UHCI QHs require 16-byte alignment");

static const uint32_t TD_ACTIVE = 1u << 23;
static const uint32_t TD_STALLED = 1u << 22;
static const uint32_t TD_DATA_BUFFER_ERROR = 1u << 21;
static const uint32_t TD_BABBLE = 1u << 20;
static const uint32_t TD_NAK = 1u << 19;
static const uint32_t TD_CRC_TIMEOUT = 1u << 18;
static const uint32_t TD_BITSTUFF = 1u << 17;
static const uint32_t TD_SHORT_PACKET_DETECT = 1u << 29;
static const uint32_t TD_DATA_TOGGLE = 1u << 19;
static const uint32_t TD_ERROR_COUNT = 3u << 27;
static const uint32_t TD_INITIAL_ACTUAL_LENGTH = 0x7FFu;
static const uint32_t TD_ACTUAL_LENGTH_MASK = 0x7FFu;
static const uint32_t FRAME_NUMBER_MASK = 0x07FFu;
static const uint8_t CSW_DMA_SENTINEL = 0xA5u;

struct SampleHistory {
    uint32_t values[16];
    uint8_t count;
    uint8_t next;
};

inline void clear_history(SampleHistory& history)
{
    history.count = 0;
    history.next = 0;
}

inline void record_distinct(SampleHistory& history, uint32_t value)
{
    if (history.count != 0) {
        const uint8_t last = static_cast<uint8_t>(
            (history.next + 15u) & 15u);
        if (history.values[last] == value) return;
    }
    history.values[history.next] = value;
    history.next = static_cast<uint8_t>((history.next + 1u) & 15u);
    if (history.count < 16u) ++history.count;
}

inline uint32_t history_value(const SampleHistory& history, uint8_t index)
{
    const uint8_t first = history.count == 16u ? history.next : 0u;
    return history.values[static_cast<uint8_t>((first + index) & 15u)];
}

enum CswBufferClassification : uint8_t {
    CSW_BUFFER_UNTOUCHED_SENTINEL = 0,
    CSW_BUFFER_PARTIALLY_CHANGED,
    CSW_BUFFER_COMPLETE_EXPECTED_CSW,
    CSW_BUFFER_UNRELATED_BYTES,
};

inline CswBufferClassification classify_csw_buffer(const uint8_t* bytes,
                                                     uint8_t length,
                                                     uint16_t actualLength,
                                                     uint8_t sentinel,
                                                     uint32_t expectedTag)
{
    if (!bytes || length != 13u) return CSW_BUFFER_UNRELATED_BYTES;
    if (actualLength == 0) {
        for (uint8_t i = 0; i < length; ++i)
            if (bytes[i] != sentinel) return CSW_BUFFER_UNRELATED_BYTES;
        return CSW_BUFFER_UNTOUCHED_SENTINEL;
    }
    if (actualLength < length) return CSW_BUFFER_PARTIALLY_CHANGED;
    if (actualLength > length) return CSW_BUFFER_UNRELATED_BYTES;
    const uint32_t signature = static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8) |
        (static_cast<uint32_t>(bytes[2]) << 16) |
        (static_cast<uint32_t>(bytes[3]) << 24);
    const uint32_t tag = static_cast<uint32_t>(bytes[4]) |
        (static_cast<uint32_t>(bytes[5]) << 8) |
        (static_cast<uint32_t>(bytes[6]) << 16) |
        (static_cast<uint32_t>(bytes[7]) << 24);
    if (signature == 0x53425355u && tag == expectedTag && bytes[12] <= 2u)
        return CSW_BUFFER_COMPLETE_EXPECTED_CSW;
    return CSW_BUFFER_UNRELATED_BYTES;
}

inline uint16_t decode_max_length(uint32_t token)
{
    const uint16_t encoded = static_cast<uint16_t>((token >> 21) & 0x7FFu);
    return encoded == 0x7FFu ? 0u : static_cast<uint16_t>(encoded + 1u);
}

static const uint16_t STS_HOST_SYSTEM_ERROR = 1u << 3;
static const uint16_t STS_HOST_PROCESS_ERROR = 1u << 4;
static const uint16_t STS_HALTED = 1u << 5;

enum Observation : uint8_t {
    OBSERVATION_PENDING = 0,
    OBSERVATION_SUCCESS,
    OBSERVATION_STALL,
    OBSERVATION_DATA_BUFFER_ERROR,
    OBSERVATION_BABBLE,
    OBSERVATION_CRC_TIMEOUT,
    OBSERVATION_BITSTUFF,
    OBSERVATION_NAK,
    OBSERVATION_CONTROLLER_ERROR,
    OBSERVATION_QH_ADVANCED_ACTIVE,
};

inline bool virtual_to_dma(uint64_t kernelPhysicalBase,
                           uint64_t virtualAddress,
                           uint32_t& outPhysicalAddress)
{
    uint64_t physicalAddress = virtualAddress;
    if (virtualAddress >= 0x100000ULL) {
        const uint64_t offset = virtualAddress - 0x100000ULL;
        if (offset > UINT64_MAX - kernelPhysicalBase) return false;
        physicalAddress = kernelPhysicalBase + offset;
    }
    if (physicalAddress > 0xFFFFFFFFULL) return false;
    outPhysicalAddress = static_cast<uint32_t>(physicalAddress);
    return true;
}

inline uint32_t make_token(uint8_t pid, uint8_t address, uint8_t endpoint,
                           uint8_t dataToggle, uint16_t maxLength)
{
    const uint32_t encodedLength = maxLength ? maxLength - 1u : 0x7FFu;
    return (encodedLength << 21) |
           (static_cast<uint32_t>(dataToggle & 1u) << 19) |
           (static_cast<uint32_t>(endpoint & 0x0Fu) << 15) |
           (static_cast<uint32_t>(address & 0x7Fu) << 8) | pid;
}

inline uint32_t frame_list_qh_link(uint32_t physicalAddress)
{
    return physicalAddress | 0x02u;
}

inline uint32_t td_depth_link(uint32_t physicalAddress)
{
    return physicalAddress | 0x04u;
}

inline uint32_t initial_active_status(bool shortPacketDetect = false)
{
    return TD_ERROR_COUNT | TD_INITIAL_ACTUAL_LENGTH | TD_ACTIVE |
        (shortPacketDetect ? TD_SHORT_PACKET_DETECT : 0u);
}

inline void prepare_descriptor(volatile TransferDescriptor* descriptor,
                               uint32_t link, uint32_t token,
                               uint32_t buffer)
{
    descriptor->status = 0;
    descriptor->link = link;
    descriptor->token = token;
    descriptor->buffer = buffer;
}

inline uint16_t decode_actual_length(uint32_t status)
{
    const uint16_t encoded = static_cast<uint16_t>(
        status & TD_ACTUAL_LENGTH_MASK);
    return encoded == TD_ACTUAL_LENGTH_MASK
        ? 0u : static_cast<uint16_t>(encoded + 1u);
}

inline bool short_packet(uint32_t status, uint16_t requestedLength)
{
    return decode_actual_length(status) < requestedLength;
}

inline bool short_packet(uint32_t status, uint16_t actualTransferred,
                         uint16_t requestedLength)
{
    return (status & TD_SHORT_PACKET_DETECT) != 0 &&
           actualTransferred < requestedLength;
}

inline uint16_t elapsed_frames(uint16_t startFrame, uint16_t currentFrame)
{
    return static_cast<uint16_t>((currentFrame - startFrame) &
                                  FRAME_NUMBER_MASK);
}

inline bool frame_deadline_expired(uint16_t startFrame, uint16_t currentFrame,
                                   uint16_t timeoutFrames)
{
    return elapsed_frames(startFrame, currentFrame) >= timeoutFrames;
}

inline Observation observe(uint32_t tdStatus, uint32_t qhElement,
                           uint32_t tdAddress, uint32_t tdLink,
                           uint16_t controllerStatus)
{
    if ((tdStatus & TD_ACTIVE) == 0) {
        if (tdStatus & TD_STALLED) return OBSERVATION_STALL;
        if (tdStatus & TD_DATA_BUFFER_ERROR)
            return OBSERVATION_DATA_BUFFER_ERROR;
        if (tdStatus & TD_BABBLE) return OBSERVATION_BABBLE;
        if (tdStatus & TD_CRC_TIMEOUT) return OBSERVATION_CRC_TIMEOUT;
        if (tdStatus & TD_BITSTUFF) return OBSERVATION_BITSTUFF;
        if (tdStatus & TD_NAK) return OBSERVATION_NAK;
        return OBSERVATION_SUCCESS;
    }

    if (controllerStatus & (STS_HOST_SYSTEM_ERROR |
                            STS_HOST_PROCESS_ERROR | STS_HALTED))
        return OBSERVATION_CONTROLLER_ERROR;

    // A terminated TD legitimately advances the QH element link to 0x1.
    // Only flag an inconsistent advance when this TD links to another TD.
    const bool qhAdvanced = (tdLink & 1u) == 0 &&
        qhElement != tdAddress &&
        ((qhElement & ~0x0Fu) == (tdLink & ~0x0Fu));
    return qhAdvanced ? OBSERVATION_QH_ADVANCED_ACTIVE
                      : OBSERVATION_PENDING;
}

} // namespace uhci
} // namespace amd64
} // namespace arch
} // namespace kernel

#endif
