#include "include/kernel/usb_storage.h"
#include "include/kernel/usb.h"
#include "include/kernel/block_device.h"
#include "include/kernel/serial_debug.h"

namespace kernel {
namespace usb_storage {

namespace {

static StorageDevice s_devices[MAX_STORAGE_DEVICES];
static uint8_t s_deviceCount = 0;

enum BotStage : uint8_t {
    BOT_STAGE_NONE = 0,
    BOT_STAGE_CBW = 1,
    BOT_STAGE_DATA = 2,
    BOT_STAGE_CSW = 3,
    BOT_STAGE_VALIDATE = 4,
    BOT_STAGE_RESET = 5,
    BOT_STAGE_CLEAR_IN = 6,
    BOT_STAGE_CLEAR_OUT = 7,
    BOT_STAGE_SENSE = 8,
};

static void zero(void* dst, size_t len)
{
    uint8_t* out = static_cast<uint8_t*>(dst);
    for (size_t i = 0; i < len; ++i) out[i] = 0;
}

static void copy(void* dst, const void* src, size_t len)
{
    uint8_t* out = static_cast<uint8_t*>(dst);
    const uint8_t* in = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < len; ++i) out[i] = in[i];
}

static uint16_t be16(const uint8_t* p)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

static uint32_t be32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

static uint32_t le32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

static void put_le32(uint8_t* p, uint32_t value)
{
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
    p[2] = static_cast<uint8_t>(value >> 16);
    p[3] = static_cast<uint8_t>(value >> 24);
}

static uint64_t be64(const uint8_t* p)
{
    return (static_cast<uint64_t>(be32(p)) << 32) | be32(p + 4);
}

static void put_be16(uint8_t* p, uint16_t value)
{
    p[0] = static_cast<uint8_t>(value >> 8);
    p[1] = static_cast<uint8_t>(value);
}

static void put_be32(uint8_t* p, uint32_t value)
{
    p[0] = static_cast<uint8_t>(value >> 24);
    p[1] = static_cast<uint8_t>(value >> 16);
    p[2] = static_cast<uint8_t>(value >> 8);
    p[3] = static_cast<uint8_t>(value);
}

static void put_be64(uint8_t* p, uint64_t value)
{
    put_be32(p, static_cast<uint32_t>(value >> 32));
    put_be32(p + 4, static_cast<uint32_t>(value));
}

static bool valid_block_size(uint32_t size)
{
    return size >= 512 && size <= 4096 && (size & (size - 1)) == 0;
}

static bool valid_range(const StorageDevice& dev, uint64_t lba,
                        uint32_t count)
{
    return dev.capacityValid && count != 0 && lba <= dev.lastLBA &&
        static_cast<uint64_t>(count - 1) <= dev.lastLBA - lba;
}

static void set_last_transfer(StorageDevice* dev, BotStage stage,
                              usb::TransferStatus status)
{
    if (!dev) return;
    dev->lastBotStage = static_cast<uint8_t>(stage);
    dev->lastTransferStatus = static_cast<uint8_t>(status);
}

static bool clear_endpoint_halt(StorageDevice* dev, uint8_t endpoint,
                                BotStage stage)
{
    usb::SetupPacket setup = {};
    setup.bmRequestType = 0x02; // Host-to-device, standard, endpoint.
    setup.bRequest = usb::REQ_CLEAR_FEATURE;
    setup.wValue = 0; // ENDPOINT_HALT
    setup.wIndex = endpoint;
    setup.wLength = 0;
    const usb::TransferStatus status = usb::control_transfer(
        dev->usbAddress, &setup, nullptr, 0);
    set_last_transfer(dev, stage, status);
    return status == usb::XFER_SUCCESS;
}

static bool bot_reset_recovery(StorageDevice* dev)
{
    usb::SetupPacket reset = {};
    reset.bmRequestType = 0x21; // Host-to-device, class, interface.
    reset.bRequest = 0xFF;      // Mass Storage Reset.
    reset.wValue = 0;
    reset.wIndex = dev->interfaceNum;
    reset.wLength = 0;
    const usb::TransferStatus resetStatus = usb::control_transfer(
        dev->usbAddress, &reset, nullptr, 0);
    set_last_transfer(dev, BOT_STAGE_RESET, resetStatus);

    const bool inCleared = clear_endpoint_halt(dev, dev->bulkInEP,
                                                BOT_STAGE_CLEAR_IN);
    const bool outCleared = clear_endpoint_halt(dev, dev->bulkOutEP,
                                                 BOT_STAGE_CLEAR_OUT);
    const bool recovered = resetStatus == usb::XFER_SUCCESS && inCleared &&
                           outCleared;
    if (!recovered) dev->transportFaulted = true;
    return recovered;
}

static void build_cbw(StorageDevice* dev, CommandBlockWrapper* cbw,
                      uint8_t direction, uint32_t dataLength,
                      const uint8_t* command, uint8_t commandLength)
{
    zero(cbw, sizeof(*cbw));
    uint8_t* bytes = reinterpret_cast<uint8_t*>(cbw);
    put_le32(bytes, CBW_SIGNATURE);
    put_le32(bytes + 4, dev->cbwTag++);
    if (dev->cbwTag == 0) dev->cbwTag = 1;
    put_le32(bytes + 8, dataLength);
    bytes[12] = direction;
    bytes[13] = dev->lun;
    bytes[14] = commandLength;
    copy(bytes + 15, command, commandLength);
}

static usb::TransferStatus transfer_data(StorageDevice* dev, uint8_t direction,
                                         void* data, uint32_t dataLength,
                                         uint32_t& transferred)
{
    transferred = 0;
    uint8_t* cursor = static_cast<uint8_t*>(data);
    uint32_t remaining = dataLength;
    while (remaining != 0) {
        const uint16_t chunk = remaining > 0xFFFFu
            ? 0xFFFFu : static_cast<uint16_t>(remaining);
        uint16_t amount = 0;
        const usb::TransferStatus status = direction == 0x80
            ? usb::hci::bulk_transfer(dev->usbAddress, dev->bulkInEP,
                                      cursor, chunk, &amount)
            : usb::hci::bulk_transfer(dev->usbAddress, dev->bulkOutEP,
                                      cursor, chunk, &amount);
        if (amount > chunk || amount > remaining) return usb::XFER_DATA_OVERRUN;
        transferred += amount;
        cursor += amount;
        remaining -= amount;
        if (status != usb::XFER_SUCCESS) return status;
        if (amount < chunk) {
            // A short IN phase is legal only when the CSW residue accounts
            // for the missing bytes. An OUT short transfer is a transport error.
            return direction == 0x80 ? usb::XFER_SUCCESS
                                     : usb::XFER_DATA_UNDERRUN;
        }
    }
    return usb::XFER_SUCCESS;
}

static usb::TransferStatus bot_transfer(StorageDevice* dev,
                                        uint8_t direction,
                                        const uint8_t* command,
                                        uint8_t commandLength,
                                        void* data,
                                        uint32_t dataLength,
                                        uint32_t* actualDataLength = nullptr)
{
    if (actualDataLength) *actualDataLength = 0;
    if (!dev || !dev->active || dev->transportFaulted ||
        command == nullptr || commandLength == 0 || commandLength > 16 ||
        (dataLength != 0 && data == nullptr) ||
        (direction != 0x00 && direction != 0x80))
        return usb::XFER_ERROR;

    dev->lastOpcode = command[0];
    const bool writeCommand = command[0] == SCSI_WRITE_10 ||
                              command[0] == SCSI_WRITE_16;
    if (writeCommand) dev->lastWriteOutcome = block::USB_WRITE_NOT_SUBMITTED;

    dev->lastCswStatus = 0xFF;
    dev->lastSenseValid = false;
    CommandBlockWrapper cbw;
    build_cbw(dev, &cbw, direction, dataLength, command, commandLength);

    dev->lastBotStage = BOT_STAGE_CBW;
    uint16_t sent = 0;
    if (writeCommand)
        dev->lastWriteOutcome = block::USB_WRITE_SUBMITTED_UNKNOWN;
    usb::TransferStatus status = usb::hci::bulk_transfer(
        dev->usbAddress, dev->bulkOutEP, &cbw, sizeof(cbw), &sent);
    if (status != usb::XFER_SUCCESS || sent != sizeof(cbw)) {
        if (writeCommand) {
            if (status == usb::XFER_CANCELLED ||
                !usb::get_device(dev->usbAddress))
                dev->lastWriteOutcome = sent == 0
                    ? block::USB_WRITE_REMOVED
                    : block::USB_WRITE_REMOVED_UNKNOWN;
            else
                dev->lastWriteOutcome = sent == 0
                    ? block::USB_WRITE_NOT_SUBMITTED
                    : block::USB_WRITE_SUBMITTED_UNKNOWN;
        }
        if (status == usb::XFER_SUCCESS) status = usb::XFER_DATA_UNDERRUN;
        set_last_transfer(dev, BOT_STAGE_CBW, status);
        (void)bot_reset_recovery(dev);
        return status;
    }

    uint32_t dataTransferred = 0;
    if (dataLength != 0) {
        dev->lastBotStage = BOT_STAGE_DATA;
        status = transfer_data(dev, direction, data, dataLength,
                               dataTransferred);
        if (status != usb::XFER_SUCCESS) {
            if (writeCommand && (status == usb::XFER_CANCELLED ||
                                 !usb::get_device(dev->usbAddress)))
                dev->lastWriteOutcome = block::USB_WRITE_REMOVED_UNKNOWN;
            set_last_transfer(dev, BOT_STAGE_DATA, status);
            if (status != usb::XFER_STALL ||
                !clear_endpoint_halt(dev,
                    direction == 0x80 ? dev->bulkInEP : dev->bulkOutEP,
                    direction == 0x80 ? BOT_STAGE_CLEAR_IN : BOT_STAGE_CLEAR_OUT)) {
                (void)bot_reset_recovery(dev);
                return status;
            }
        }
    }

    uint8_t csw[sizeof(CommandStatusWrapper)] = {};
    uint16_t received = 0;
    dev->lastBotStage = BOT_STAGE_CSW;
    status = usb::hci::bulk_transfer(dev->usbAddress, dev->bulkInEP,
                                     csw, sizeof(csw), &received);
    if (status != usb::XFER_SUCCESS || received != sizeof(csw)) {
        if (writeCommand && (status == usb::XFER_CANCELLED ||
                             !usb::get_device(dev->usbAddress)))
            dev->lastWriteOutcome = block::USB_WRITE_REMOVED_UNKNOWN;
        if (status == usb::XFER_SUCCESS) status = usb::XFER_DATA_UNDERRUN;
        set_last_transfer(dev, BOT_STAGE_CSW, status);
        (void)bot_reset_recovery(dev);
        return status;
    }

    dev->lastBotStage = BOT_STAGE_VALIDATE;
    const uint32_t cswSignature = le32(csw);
    const uint32_t cswTag = le32(csw + 4);
    const uint32_t cswResidue = le32(csw + 8);
    const uint32_t cbwTag = le32(reinterpret_cast<const uint8_t*>(&cbw) + 4);
    dev->lastCswStatus = csw[12];
    if (cswSignature != CSW_SIGNATURE ||
        cswTag != cbwTag || csw[12] > CSW_STATUS_PHASE_ERROR ||
        cswResidue > dataLength ||
        dataTransferred > dataLength ||
        cswResidue != dataLength - dataTransferred) {
        status = usb::XFER_ERROR;
        set_last_transfer(dev, BOT_STAGE_VALIDATE, status);
        (void)bot_reset_recovery(dev);
        return status;
    }
    if (csw[12] == CSW_STATUS_PHASE_ERROR) {
        if (writeCommand) dev->lastWriteOutcome = block::USB_WRITE_SUBMITTED_UNKNOWN;
        status = usb::XFER_ERROR;
        set_last_transfer(dev, BOT_STAGE_VALIDATE, status);
        (void)bot_reset_recovery(dev);
        return status;
    }
    if (csw[12] == CSW_STATUS_FAILED) {
        if (writeCommand) dev->lastWriteOutcome = dataTransferred != 0
            ? block::USB_WRITE_PARTIAL : block::USB_WRITE_FAILED;
        set_last_transfer(dev, BOT_STAGE_VALIDATE, usb::XFER_STALL);
        return usb::XFER_STALL;
    }
    if (writeCommand && (dataTransferred != dataLength || cswResidue != 0)) {
        dev->lastWriteOutcome = block::USB_WRITE_PARTIAL;
        set_last_transfer(dev, BOT_STAGE_VALIDATE, usb::XFER_DATA_UNDERRUN);
        return usb::XFER_DATA_UNDERRUN;
    }
    if (writeCommand) dev->lastWriteOutcome = block::USB_WRITE_COMPLETED;
    if (actualDataLength) *actualDataLength = dataTransferred;
    set_last_transfer(dev, BOT_STAGE_VALIDATE, usb::XFER_SUCCESS);
    return usb::XFER_SUCCESS;
}

static usb::TransferStatus request_sense_raw(StorageDevice* dev,
                                              SCSISenseData* sense)
{
    uint8_t command[6] = {};
    command[0] = SCSI_REQUEST_SENSE;
    command[4] = sizeof(SCSISenseData);
    zero(sense, sizeof(*sense));
    dev->lastBotStage = BOT_STAGE_SENSE;
    const usb::TransferStatus status = bot_transfer(
        dev, 0x80, command, sizeof(command), sense, sizeof(*sense));
    return status;
}

static usb::TransferStatus run_command(StorageDevice* dev, uint8_t direction,
                                       const uint8_t* command,
                                       uint8_t commandLength,
                                       void* data, uint32_t dataLength,
                                       uint32_t* actualDataLength = nullptr)
{
    if (actualDataLength) *actualDataLength = 0;
    const uint8_t failedOpcode = command ? command[0] : 0;
    usb::TransferStatus status = bot_transfer(dev, direction, command,
        commandLength, data, dataLength, actualDataLength);
    if (status != usb::XFER_STALL || dev->lastCswStatus != CSW_STATUS_FAILED ||
        failedOpcode == SCSI_REQUEST_SENSE) return status;

    SCSISenseData sense = {};
    const usb::TransferStatus senseStatus = request_sense_raw(dev, &sense);
    dev->lastOpcode = failedOpcode;
    dev->lastCswStatus = CSW_STATUS_FAILED;
    if (senseStatus == usb::XFER_SUCCESS) {
        dev->lastSense = sense;
        dev->lastSenseValid = true;
    }
    set_last_transfer(dev, BOT_STAGE_VALIDATE, usb::XFER_STALL);
    return usb::XFER_STALL;
}

static block::Status map_transfer_status(const StorageDevice& dev,
                                         usb::TransferStatus status)
{
    if (!usb::get_device(dev.usbAddress) || !dev.active)
        return block::BLOCK_ERR_NO_MEDIA;
    if (status == usb::XFER_SUCCESS) return block::BLOCK_OK;
    if (status == usb::XFER_TIMEOUT) return block::BLOCK_ERR_TIMEOUT;
    if (status == usb::XFER_NOT_SUPPORTED) return block::BLOCK_ERR_UNSUPPORTED;
    if (status == usb::XFER_READ_ONLY) return block::BLOCK_ERR_READ_ONLY;
    if (status == usb::XFER_CANCELLED) return block::BLOCK_ERR_NO_MEDIA;
    if (status == usb::XFER_STALL && dev.lastCswStatus == CSW_STATUS_FAILED &&
        dev.lastSenseValid) {
        const uint8_t response = dev.lastSense.responseCode & 0x7Fu;
        uint8_t key = 0, asc = 0, ascq = 0;
        if (response == 0x70 || response == 0x71) {
            key = dev.lastSense.senseKey & 0x0Fu;
            asc = dev.lastSense.asc;
            ascq = dev.lastSense.ascq;
        } else if (response == 0x72 || response == 0x73) {
            key = dev.lastSense.obsolete & 0x0Fu;
            asc = dev.lastSense.senseKey;
            ascq = dev.lastSense.information[0];
        }
        if (key == 0x02) {
            if (asc == 0x3A) return block::BLOCK_ERR_NO_MEDIA;
            return block::BLOCK_ERR_NOT_READY;
        }
        if (key == 0x07) return block::BLOCK_ERR_READ_ONLY;
        if (key == 0x05) return block::BLOCK_ERR_UNSUPPORTED;
        (void)ascq;
    }
    return block::BLOCK_ERR_IO;
}

static usb::TransferStatus do_inquiry(StorageDevice* dev)
{
    uint8_t command[6] = {};
    command[0] = SCSI_INQUIRY;
    command[4] = sizeof(SCSIInquiryData);
    zero(&dev->inquiry, sizeof(dev->inquiry));
    uint32_t actual = 0;
    const usb::TransferStatus status = run_command(
        dev, 0x80, command, sizeof(command), &dev->inquiry,
        sizeof(dev->inquiry), &actual);
    if (status == usb::XFER_SUCCESS &&
        (actual < sizeof(dev->inquiry) || dev->inquiry.additionalLength < 31))
        return usb::XFER_DATA_UNDERRUN;
    return status;
}

static usb::TransferStatus do_vpd_serial(StorageDevice* dev)
{
    uint8_t supportedCommand[6] = {};
    supportedCommand[0] = SCSI_INQUIRY;
    supportedCommand[1] = 1; // EVPD
    supportedCommand[2] = 0x00;
    supportedCommand[4] = 64;
    uint8_t supportedPages[64] = {};
    uint32_t supportedActual = 0;
    usb::TransferStatus status = run_command(dev, 0x80, supportedCommand,
        sizeof(supportedCommand), supportedPages, sizeof(supportedPages),
        &supportedActual);
    if (status != usb::XFER_SUCCESS || supportedActual < 4 ||
        supportedPages[1] != 0x00)
        return status == usb::XFER_SUCCESS ? usb::XFER_ERROR : status;
    uint16_t supportedLength = be16(&supportedPages[2]);
    if (supportedLength > supportedActual - 4)
        supportedLength = static_cast<uint16_t>(supportedActual - 4);
    bool serialPageSupported = false;
    for (uint16_t i = 0; i < supportedLength; ++i)
        if (supportedPages[4 + i] == 0x80) serialPageSupported = true;
    if (!serialPageSupported) return usb::XFER_NOT_SUPPORTED;

    uint8_t command[6] = {};
    command[0] = SCSI_INQUIRY;
    command[1] = 1; // EVPD
    command[2] = 0x80;
    command[4] = 96;
    uint8_t response[96] = {};
    uint32_t responseActual = 0;
    status = run_command(
        dev, 0x80, command, sizeof(command), response, sizeof(response),
        &responseActual);
    if (status != usb::XFER_SUCCESS || responseActual < 4 ||
        response[1] != 0x80) return status;
    uint16_t length = be16(&response[2]);
    if (length > responseActual - 4)
        length = static_cast<uint16_t>(responseActual - 4);
    size_t begin = 0;
    while (begin < length && response[4 + begin] == ' ') ++begin;
    size_t end = length;
    while (end > begin && (response[4 + end - 1] == ' ' ||
           response[4 + end - 1] == 0)) --end;
    size_t out = 0;
    for (size_t i = begin; i < end && out + 1 < sizeof(dev->serial); ++i) {
        const uint8_t c = response[4 + i];
        dev->serial[out++] = c >= 0x20 && c <= 0x7E ? static_cast<char>(c) : '?';
    }
    dev->serial[out] = '\0';
    return usb::XFER_SUCCESS;
}

static void make_model(StorageDevice* dev)
{
    size_t out = 0;
    const uint8_t* fields[2] = { dev->inquiry.vendorId, dev->inquiry.productId };
    const size_t lengths[2] = { sizeof(dev->inquiry.vendorId),
                                sizeof(dev->inquiry.productId) };
    for (uint8_t field = 0; field < 2; ++field) {
        size_t begin = 0, end = lengths[field];
        while (begin < end && fields[field][begin] == ' ') ++begin;
        while (end > begin && fields[field][end - 1] == ' ') --end;
        if (end == begin) continue;
        if (out != 0 && out + 1 < sizeof(dev->model))
            dev->model[out++] = ' ';
        for (size_t i = begin; i < end && out + 1 < sizeof(dev->model); ++i) {
            const uint8_t c = fields[field][i];
            dev->model[out++] = c >= 0x20 && c <= 0x7E
                ? static_cast<char>(c) : '?';
        }
    }
    if (out == 0) {
        const char fallback[] = "USB Mass Storage";
        copy(dev->model, fallback, sizeof(fallback));
    } else {
        dev->model[out] = '\0';
    }
}

static usb::TransferStatus do_test_unit_ready(StorageDevice* dev)
{
    uint8_t command[6] = {};
    command[0] = SCSI_TEST_UNIT_READY;
    return run_command(dev, 0x00, command, sizeof(command), nullptr, 0);
}

static usb::TransferStatus do_read_capacity(StorageDevice* dev)
{
    uint8_t command[10] = {};
    command[0] = SCSI_READ_CAPACITY_10;
    uint8_t response[8] = {};
    uint32_t actual = 0;
    usb::TransferStatus status = run_command(
        dev, 0x80, command, sizeof(command), response, sizeof(response),
        &actual);
    if (status != usb::XFER_SUCCESS) return status;
    if (actual != sizeof(response)) return usb::XFER_DATA_UNDERRUN;

    dev->lastLBA = be32(response);
    dev->blockSize = be32(response + 4);
    if (dev->lastLBA == UINT32_MAX) {
        uint8_t command16[16] = {};
        command16[0] = SCSI_SERVICE_ACTION_IN_16;
        command16[1] = SCSI_SERVICE_ACTION_READ_CAPACITY_16;
        put_be32(&command16[10], 32);
        uint8_t response16[32] = {};
        actual = 0;
        status = run_command(dev, 0x80, command16, sizeof(command16),
                             response16, sizeof(response16), &actual);
        if (status != usb::XFER_SUCCESS) return status;
        if (actual != sizeof(response16)) return usb::XFER_DATA_UNDERRUN;
        dev->lastLBA = be64(response16);
        dev->blockSize = be32(response16 + 8);
    }
    if (dev->lastLBA == UINT64_MAX || !valid_block_size(dev->blockSize) ||
        dev->lastLBA + 1 > UINT64_MAX / dev->blockSize)
        return usb::XFER_ERROR;
    dev->capacityValid = true;
    return usb::XFER_SUCCESS;
}

static bool find_bulk_endpoints(const usb::Device* usbDev,
                                uint8_t interfaceNumber,
                                uint8_t* bulkIn, uint16_t* bulkInPkt,
                                uint8_t* bulkOut, uint16_t* bulkOutPkt)
{
    bool foundIn = false, foundOut = false;
    for (uint8_t i = 0; i < usb::MAX_ENDPOINTS * 2; ++i) {
        const usb::Endpoint& endpoint = usbDev->endpoints[i];
        if (!endpoint.active || endpoint.type != usb::TRANSFER_BULK ||
            endpoint.interfaceNumber != interfaceNumber ||
            endpoint.maxPacketSize == 0) continue;
        if (endpoint.dir == usb::DIR_DEVICE_TO_HOST && !foundIn) {
            *bulkIn = endpoint.address;
            *bulkInPkt = endpoint.maxPacketSize;
            foundIn = true;
        } else if (endpoint.dir == usb::DIR_HOST_TO_DEVICE && !foundOut) {
            *bulkOut = endpoint.address;
            *bulkOutPkt = endpoint.maxPacketSize;
            foundOut = true;
        }
    }
    return foundIn && foundOut;
}

static int find_free_slot()
{
    for (uint8_t i = 0; i < MAX_STORAGE_DEVICES; ++i) {
        StorageDevice& dev = s_devices[i];
        if (dev.active) continue;
        if (dev.blockRegistrationId != 0 &&
            block::registration_is_present(dev.blockDeviceIndex,
                                           dev.blockRegistrationId))
            continue;
        return i;
    }
    return -1;
}

static void make_block_name(const StorageDevice& dev, char* out, size_t cap)
{
    if (!cap) return;
    size_t at = 0;
    const char prefix[] = "usb-";
    for (size_t i = 0; i + 1 < sizeof(prefix) && at + 1 < cap; ++i)
        out[at++] = prefix[i];
    const char hex[] = "0123456789ABCDEF";
    const uint16_t ids[2] = { dev.usbVendorId, dev.usbProductId };
    for (uint8_t id = 0; id < 2; ++id) {
        for (int shift = 12; shift >= 0; shift -= 4)
            if (at + 1 < cap) out[at++] = hex[(ids[id] >> shift) & 0x0F];
        if (id == 0 && at + 1 < cap) out[at++] = ':';
    }
    const char* labels[3] = { "-p", "-i", "-l" };
    const uint8_t values[3] = { dev.usbPort, dev.interfaceNum, dev.lun };
    for (uint8_t i = 0; i < 3; ++i) {
        for (size_t j = 0; labels[i][j] && at + 1 < cap; ++j)
            out[at++] = labels[i][j];
        char digits[3];
        uint8_t n = 0, value = values[i];
        do { digits[n++] = static_cast<char>('0' + (value % 10)); value /= 10; }
        while (value && n < sizeof(digits));
        while (n && at + 1 < cap) out[at++] = digits[--n];
    }
    out[at] = '\0';
}

static block::Status block_read(uint8_t index, uint64_t lba,
                                uint32_t count, void* buffer)
{
    if (index >= MAX_STORAGE_DEVICES || !buffer || count == 0)
        return block::BLOCK_ERR_INVALID;
    StorageDevice& dev = s_devices[index];
    if (!dev.active || !dev.ready || dev.transportFaulted)
        return block::BLOCK_ERR_NO_MEDIA;
    if (!valid_range(dev, lba, count)) return block::BLOCK_ERR_INVALID;
    const usb::TransferStatus transfer = read_sectors(index, lba, count, buffer);
    const block::Status status = map_transfer_status(dev, transfer);
    dev.lastBlockStatus = static_cast<uint8_t>(status);
    if (dev.transportFaulted && dev.blockRegistered)
        (void)block::mark_device_offline(dev.blockDeviceIndex,
                                         dev.blockRegistrationId);
    return status;
}

static block::Status block_flush(uint8_t index)
{
    if (index >= MAX_STORAGE_DEVICES) return block::BLOCK_ERR_INVALID;
    StorageDevice& dev = s_devices[index];
    if (!dev.active || !dev.ready || dev.transportFaulted)
        return dev.writeCompletedAwaitingFlush
            ? block::BLOCK_ERR_DURABILITY_UNVERIFIED
            : block::BLOCK_ERR_NO_MEDIA;
    if (dev.syncCacheState == SYNC_CACHE_UNSUPPORTED)
        return dev.writeCompletedAwaitingFlush
            ? block::BLOCK_ERR_DURABILITY_UNVERIFIED
            : block::BLOCK_ERR_UNSUPPORTED;
    const usb::TransferStatus transfer = synchronize_cache(index);
    block::Status status = map_transfer_status(dev, transfer);
    if (transfer == usb::XFER_SUCCESS) {
        dev.writeCompletedAwaitingFlush = false;
    } else if (dev.writeCompletedAwaitingFlush) {
        status = block::BLOCK_ERR_DURABILITY_UNVERIFIED;
    }
    dev.lastBlockStatus = static_cast<uint8_t>(status);
    if (dev.transportFaulted && dev.blockRegistered)
        (void)block::mark_device_offline(dev.blockDeviceIndex,
                                         dev.blockRegistrationId);
    return status;
}

static block::Status block_write(uint8_t index, uint64_t lba,
                                 uint32_t count, const void* buffer)
{
    if (index >= MAX_STORAGE_DEVICES || !buffer || count == 0)
        return block::BLOCK_ERR_INVALID;
    StorageDevice& dev = s_devices[index];
    if (!dev.active || !dev.ready || dev.transportFaulted)
        return block::BLOCK_ERR_NO_MEDIA;
    const usb::TransferStatus transfer = write_sectors(index, lba, count, buffer);
    block::Status status = map_transfer_status(dev, transfer);

    const uint8_t senseKey = dev.lastSenseValid
        ? static_cast<uint8_t>(dev.lastSense.senseKey & 0x0Fu) : 0xFFu;
    if (senseKey == 0x07u && transfer != usb::XFER_SUCCESS) {
        // DATA PROTECT is authoritative even if MODE SENSE said writable.
        dev.writeProtected = true;
        (void)block::mark_device_read_only(dev.blockDeviceIndex,
                                           dev.blockRegistrationId);
        status = block::BLOCK_ERR_READ_ONLY;
    } else if (dev.lastWriteOutcome == block::USB_WRITE_SUBMITTED_UNKNOWN ||
               dev.lastWriteOutcome == block::USB_WRITE_REMOVED_UNKNOWN ||
               dev.lastWriteOutcome == block::USB_WRITE_PARTIAL) {
        status = block::BLOCK_ERR_WRITE_UNCERTAIN;
    } else if (dev.lastWriteOutcome == block::USB_WRITE_REMOVED) {
        status = block::BLOCK_ERR_NO_MEDIA;
    }

    dev.lastBlockStatus = static_cast<uint8_t>(status);
    if (dev.lastWriteOutcome == block::USB_WRITE_REMOVED_UNKNOWN ||
        dev.lastWriteOutcome == block::USB_WRITE_REMOVED) {
        dev.transportFaulted = true;
        if (dev.blockRegistered)
            (void)block::mark_device_offline(dev.blockDeviceIndex,
                                             dev.blockRegistrationId);
    } else if (dev.transportFaulted && dev.blockRegistered) {
        (void)block::mark_device_offline(dev.blockDeviceIndex,
                                         dev.blockRegistrationId);
    }
    return status;
}

static bool get_io_diagnostic(uint8_t index,
                              block::TransportIoDiagnostic& out)
{
    if (index >= MAX_STORAGE_DEVICES) return false;
    const StorageDevice& dev = s_devices[index];
    zero(&out, sizeof(out));
    out.valid = true;
    out.stage = dev.lastBotStage;
    out.statusCode = dev.lastTransferStatus;
    out.commandOpcode = dev.lastOpcode;
    out.cswStatus = dev.lastCswStatus;
    out.senseKey = dev.lastSenseValid ? dev.lastSense.senseKey & 0x0F : 0xFF;
    out.senseAsc = dev.lastSenseValid ? dev.lastSense.asc : 0;
    out.senseAscq = dev.lastSenseValid ? dev.lastSense.ascq : 0;
    out.usbSyncCacheState = static_cast<uint8_t>(dev.syncCacheState);
    out.usbWriteOutcome = static_cast<uint8_t>(dev.lastWriteOutcome);
    out.usbVendorId = dev.usbVendorId;
    out.usbProductId = dev.usbProductId;
    out.usbInterface = dev.interfaceNum;
    out.usbLun = dev.lun;
    return true;
}

static bool register_block_device(uint8_t index)
{
    StorageDevice& dev = s_devices[index];
    block::BlockDevice descriptor = {};
    descriptor.type = block::BDEV_USB_MASS;
    descriptor.driverIndex = index;
    descriptor.totalSectors = dev.lastLBA + 1;
    descriptor.sectorSize = dev.blockSize;
    make_block_name(dev, descriptor.name, sizeof(descriptor.name));
    descriptor.readFn = block_read;
    // DM13 proved the private BOT/SCSI production write path before enabling
    // this callback. Persistence remains separately gated by a trusted
    // SYNCHRONIZE CACHE result.
    descriptor.writeFn = dev.writeProtected ? nullptr : block_write;
    descriptor.flushFn = dev.syncCacheState == SYNC_CACHE_SUCCEEDED
        ? block_flush : nullptr;
    descriptor.flushSemanticsKnown =
        dev.syncCacheState == SYNC_CACHE_SUCCEEDED;
    descriptor.writeCompletionDurable = false;
    descriptor.removableKnown = true;
    descriptor.removable = (dev.inquiry.rmb & 0x80u) != 0;
    copy(descriptor.model, dev.model, sizeof(descriptor.model));
    copy(descriptor.serial, dev.serial, sizeof(descriptor.serial));
    descriptor.bootProvenance = block::BOOT_PROVENANCE_UNKNOWN;
    descriptor.usbIdentityValid = true;
    descriptor.usbVendorId = dev.usbVendorId;
    descriptor.usbProductId = dev.usbProductId;
    descriptor.usbPort = dev.usbPort;
    descriptor.pciLocationValid = dev.controllerPciLocationValid;
    descriptor.pciSegment = dev.controllerPciSegment;
    descriptor.pciBus = dev.controllerPciBus;
    descriptor.pciDevice = dev.controllerPciDevice;
    descriptor.pciFunction = dev.controllerPciFunction;
    descriptor.usbInterface = dev.interfaceNum;
    descriptor.usbLun = dev.lun;
    descriptor.usbSyncCacheState = static_cast<uint8_t>(dev.syncCacheState);
    descriptor.maxTransferBytes = MAX_BOT_TRANSFER_BYTES;
    descriptor.getIoDiagnosticFn = get_io_diagnostic;

    const uint8_t blockIndex = block::register_device(descriptor);
    if (blockIndex == 0xFF) return false;
    block::BlockDevice registered = {};
    if (!block::copy_device(blockIndex, registered)) {
        (void)block::unregister_device(blockIndex);
        return false;
    }
    dev.blockDeviceIndex = blockIndex;
    dev.blockRegistrationId = registered.registrationId;
    dev.blockRegistered = true;
    return true;
}

} // namespace

void init()
{
    for (uint8_t i = 0; i < MAX_STORAGE_DEVICES; ++i) {
        if (s_devices[i].active) release(s_devices[i].usbAddress);
    }
    s_deviceCount = 0;
    for (uint8_t i = 0; i < MAX_STORAGE_DEVICES; ++i) {
        if (s_devices[i].blockRegistrationId != 0 &&
            block::registration_is_present(s_devices[i].blockDeviceIndex,
                                           s_devices[i].blockRegistrationId))
            continue;
        zero(&s_devices[i], sizeof(s_devices[i]));
    }
}

bool probe(uint8_t usbAddress)
{
    const usb::Device* usbDev = usb::get_device(usbAddress);
    if (!usbDev) return false;
    bool claimed = false;
    for (uint8_t iface = 0; iface < usbDev->numInterfaces; ++iface) {
        if (usbDev->interfaceClass[iface] != usb::CLASS_MASS_STORAGE ||
            usbDev->interfaceSubClass[iface] != usb::MSC_SUBCLASS_SCSI_TRANSPARENT ||
            usbDev->interfaceProtocol[iface] != usb::MSC_PROTOCOL_BULK_ONLY)
            continue;
        const uint8_t interfaceNumber = usbDev->interfaceNumber[iface];
        bool alreadyProbed = false;
        for (uint8_t i = 0; i < MAX_STORAGE_DEVICES; ++i)
            if (s_devices[i].active && s_devices[i].usbAddress == usbAddress &&
                s_devices[i].interfaceNum == interfaceNumber) alreadyProbed = true;
        if (alreadyProbed) { claimed = true; continue; }

        const int slot = find_free_slot();
        if (slot < 0) continue;
        StorageDevice& dev = s_devices[slot];
        zero(&dev, sizeof(dev));
        dev.active = true;
        dev.usbAddress = usbAddress;
        dev.interfaceNum = interfaceNumber;
        dev.usbPort = usbDev->hubPort;
        dev.controllerPciLocationValid = usbDev->controllerPciLocationValid;
        dev.controllerPciSegment = usbDev->controllerPciSegment;
        dev.controllerPciBus = usbDev->controllerPciBus;
        dev.controllerPciDevice = usbDev->controllerPciDevice;
        dev.controllerPciFunction = usbDev->controllerPciFunction;
        dev.usbVendorId = usbDev->devDesc.idVendor;
        dev.usbProductId = usbDev->devDesc.idProduct;
        dev.lun = 0;
        dev.cbwTag = 1;
        if (!find_bulk_endpoints(usbDev, interfaceNumber,
                &dev.bulkInEP, &dev.bulkInMaxPkt,
                &dev.bulkOutEP, &dev.bulkOutMaxPkt)) {
            dev.active = false;
            continue;
        }

        usb::TransferStatus status = do_inquiry(&dev);
        if (status != usb::XFER_SUCCESS) {
            dev.active = false;
            continue;
        }
        make_model(&dev);
        (void)do_vpd_serial(&dev);

        for (uint8_t retry = 0; retry < 5; ++retry) {
            status = do_test_unit_ready(&dev);
            if (status == usb::XFER_SUCCESS) break;
            if (dev.transportFaulted) break;
            for (volatile uint32_t delay = 0; delay < 100000; ++delay) {}
        }
        if (status != usb::XFER_SUCCESS || dev.transportFaulted) {
            dev.active = false;
            continue;
        }
        dev.ready = true;
        if (do_read_capacity(&dev) != usb::XFER_SUCCESS ||
            !dev.capacityValid) {
            dev.active = false;
            continue;
        }

        // Probe read-only state conservatively. If MODE SENSE is absent or
        // fails, private writes are treated as protected.
        uint8_t modeCmd[6] = {};
        uint8_t modeData[4] = {};
        uint32_t modeActual = 0;
        modeCmd[0] = SCSI_MODE_SENSE_6;
        modeCmd[2] = 0x3F;
        modeCmd[4] = sizeof(modeData);
        const usb::TransferStatus modeStatus = run_command(&dev, 0x80, modeCmd,
            sizeof(modeCmd), modeData, sizeof(modeData), &modeActual);
        dev.writeProtected = modeStatus != usb::XFER_SUCCESS || modeActual < 4 ||
            (modeData[2] & 0x80u) != 0;

        // Only a successful SCSI cache synchronization establishes a known
        // flush callback. Unsupported/failed devices remain readable but do
        // not gain durable-write eligibility.
        const usb::TransferStatus cacheStatus =
            synchronize_cache(static_cast<uint8_t>(slot));
        if (cacheStatus != usb::XFER_SUCCESS) {
            const bool illegalRequest = dev.lastSenseValid &&
                (dev.lastSense.senseKey & 0x0Fu) == 0x05 &&
                (dev.lastSense.asc == 0x20 || dev.lastSense.asc == 0x24);
            dev.syncCacheState = illegalRequest ? SYNC_CACHE_UNSUPPORTED
                                                : SYNC_CACHE_FAILED;
        }
        if (!register_block_device(static_cast<uint8_t>(slot))) {
            dev.active = false;
            continue;
        }
        ++s_deviceCount;
        serial::puts("[USB-MSC] registered vid:pid=");
        serial::put_hex16(dev.usbVendorId);
        serial::putc(':');
        serial::put_hex16(dev.usbProductId);
        serial::puts(" port=");
        serial::put_hex8(dev.usbPort);
        serial::puts(" interface=");
        serial::put_hex8(dev.interfaceNum);
        serial::puts(" lun=");
        serial::put_hex8(dev.lun);
        serial::puts(" blocks=");
        serial::put_hex64(dev.lastLBA + 1);
        serial::puts(" block-size=");
        serial::put_hex32(dev.blockSize);
        serial::puts(" removable=");
        serial::puts((dev.inquiry.rmb & 0x80u) ? "yes" : "no");
        serial::puts(" sync-cache=");
        serial::puts(dev.syncCacheState == SYNC_CACHE_SUCCEEDED ? "succeeded" :
                     dev.syncCacheState == SYNC_CACHE_UNSUPPORTED ? "unsupported" :
                     dev.syncCacheState == SYNC_CACHE_FAILED ? "failed" : "unknown");
        serial::puts(" access=");
        serial::puts(dev.writeProtected ? "read-only" : "read-write");
        serial::puts(" model=");
        serial::puts(dev.model);
        serial::putc('\n');
        claimed = true;
    }
    return claimed;
}

void release(uint8_t usbAddress)
{
    for (uint8_t i = 0; i < MAX_STORAGE_DEVICES; ++i) {
        StorageDevice& dev = s_devices[i];
        if (!dev.active || dev.usbAddress != usbAddress) continue;
        dev.active = false;
        dev.ready = false;
        dev.transportFaulted = true;
        if (s_deviceCount) --s_deviceCount;
        serial::puts("[USB-MSC] disconnected vid:pid=");
        serial::put_hex16(dev.usbVendorId);
        serial::putc(':');
        serial::put_hex16(dev.usbProductId);
        serial::puts(" port=");
        serial::put_hex8(dev.usbPort);
        serial::puts(" interface=");
        serial::put_hex8(dev.interfaceNum);
        serial::puts(" lun=");
        serial::put_hex8(dev.lun);
        serial::putc('\n');
        if (dev.blockRegistered) {
            (void)block::mark_device_offline(dev.blockDeviceIndex,
                                             dev.blockRegistrationId);
        }
    }
}

uint8_t device_count() { return s_deviceCount; }

const StorageDevice* get_device(uint8_t index)
{
    if (index >= MAX_STORAGE_DEVICES || !s_devices[index].active) return nullptr;
    return &s_devices[index];
}

usb::TransferStatus read_sectors(uint8_t devIndex, uint64_t lba,
                                 uint32_t count, void* buffer)
{
    if (devIndex >= MAX_STORAGE_DEVICES || !buffer || count == 0)
        return usb::XFER_ERROR;
    StorageDevice* dev = &s_devices[devIndex];
    if (!dev->active || !dev->ready || dev->transportFaulted)
        return usb::XFER_CANCELLED;
    if (!valid_range(*dev, lba, count)) return usb::XFER_ERROR;
    const uint64_t totalBytes = static_cast<uint64_t>(count) * dev->blockSize;
    if (static_cast<uint64_t>(static_cast<size_t>(totalBytes)) != totalBytes)
        return usb::XFER_BUFFER_ERROR;

    uint8_t* cursor = static_cast<uint8_t*>(buffer);
    uint32_t remaining = count;
    uint64_t currentLba = lba;
    while (remaining != 0) {
        uint32_t chunk = MAX_BOT_TRANSFER_BYTES / dev->blockSize;
        if (chunk == 0) return usb::XFER_ERROR;
        if (chunk > remaining) chunk = remaining;
        uint8_t command[16] = {};
        uint8_t commandLength = 10;
        if (currentLba <= UINT32_MAX && chunk <= UINT16_MAX &&
            currentLba + chunk - 1 <= UINT32_MAX) {
            command[0] = SCSI_READ_10;
            put_be32(&command[2], static_cast<uint32_t>(currentLba));
            put_be16(&command[7], static_cast<uint16_t>(chunk));
        } else {
            command[0] = SCSI_READ_16;
            commandLength = 16;
            put_be64(&command[2], currentLba);
            put_be32(&command[10], chunk);
        }
        const uint32_t bytes = chunk * dev->blockSize;
        uint32_t actual = 0;
        const usb::TransferStatus status = run_command(
            dev, 0x80, command, commandLength, cursor, bytes, &actual);
        if (status != usb::XFER_SUCCESS) return status;
        if (actual != bytes) return usb::XFER_DATA_UNDERRUN;
        cursor += bytes;
        currentLba += chunk;
        remaining -= chunk;
    }
    return usb::XFER_SUCCESS;
}

usb::TransferStatus write_sectors(uint8_t devIndex, uint64_t lba,
                                  uint32_t count, const void* buffer)
{
    if (devIndex >= MAX_STORAGE_DEVICES || !buffer || count == 0)
        return usb::XFER_ERROR;
    StorageDevice* dev = &s_devices[devIndex];
    if (!dev->active || !dev->ready || dev->transportFaulted)
        return usb::XFER_CANCELLED;
    dev->lastWriteOutcome = block::USB_WRITE_NOT_SUBMITTED;
    dev->lastWriteCompletedSectors = 0;
    if (dev->writeProtected) return usb::XFER_READ_ONLY;
    if (!valid_block_size(dev->blockSize) || !valid_range(*dev, lba, count))
        return usb::XFER_ERROR;
    const uint64_t totalBytes = static_cast<uint64_t>(count) * dev->blockSize;
    if (totalBytes > MAX_BOT_TRANSFER_BYTES * 0x10000ull ||
        static_cast<uint64_t>(static_cast<size_t>(totalBytes)) != totalBytes)
        return usb::XFER_BUFFER_ERROR;

    const uint8_t* cursor = static_cast<const uint8_t*>(buffer);
    uint32_t remaining = count;
    uint64_t currentLba = lba;
    while (remaining != 0) {
        uint32_t chunk = MAX_BOT_TRANSFER_BYTES / dev->blockSize;
        if (chunk > remaining) chunk = remaining;
        uint8_t command[16] = {};
        uint8_t commandLength = 10;
        if (currentLba <= UINT32_MAX && chunk <= UINT16_MAX &&
            currentLba + chunk - 1 <= UINT32_MAX) {
            command[0] = SCSI_WRITE_10;
            put_be32(&command[2], static_cast<uint32_t>(currentLba));
            put_be16(&command[7], static_cast<uint16_t>(chunk));
        } else {
            command[0] = SCSI_WRITE_16;
            commandLength = 16;
            put_be64(&command[2], currentLba);
            put_be32(&command[10], chunk);
        }
        const uint32_t bytes = chunk * dev->blockSize;
        uint32_t actual = 0;
        const usb::TransferStatus status = run_command(
            dev, 0x00, command, commandLength,
            const_cast<uint8_t*>(cursor), bytes, &actual);
        if (status != usb::XFER_SUCCESS || actual != bytes) {
            if (dev->lastWriteCompletedSectors != 0 &&
                dev->lastWriteOutcome != block::USB_WRITE_REMOVED &&
                dev->lastWriteOutcome != block::USB_WRITE_REMOVED_UNKNOWN)
                dev->lastWriteOutcome = block::USB_WRITE_PARTIAL;
            if (status == usb::XFER_SUCCESS) return usb::XFER_DATA_UNDERRUN;
            return status;
        }
        cursor += bytes;
        currentLba += chunk;
        remaining -= chunk;
        dev->lastWriteCompletedSectors += chunk;
    }
    dev->writeCompletedAwaitingFlush = true;
    // The caller must still issue SYNCHRONIZE CACHE after the full write.
    return usb::XFER_SUCCESS;
}

const char* write_outcome_name(block::UsbWriteOutcome outcome)
{
    switch (outcome) {
        case block::USB_WRITE_OUTCOME_NONE: return "none";
        case block::USB_WRITE_NOT_SUBMITTED: return "not submitted";
        case block::USB_WRITE_SUBMITTED_UNKNOWN: return "completion unknown";
        case block::USB_WRITE_COMPLETED: return "completed; durability pending";
        case block::USB_WRITE_FAILED: return "failed";
        case block::USB_WRITE_REMOVED: return "removed before submission";
        case block::USB_WRITE_REMOVED_UNKNOWN: return "removed; completion unknown";
        case block::USB_WRITE_PARTIAL: return "partial or uncertain";
        default: return "unknown";
    }
}

usb::TransferStatus test_unit_ready(uint8_t devIndex)
{
    if (devIndex >= MAX_STORAGE_DEVICES || !s_devices[devIndex].active)
        return usb::XFER_ERROR;
    return do_test_unit_ready(&s_devices[devIndex]);
}

usb::TransferStatus request_sense(uint8_t devIndex, SCSISenseData* sense)
{
    if (devIndex >= MAX_STORAGE_DEVICES || !sense ||
        !s_devices[devIndex].active) return usb::XFER_ERROR;
    const usb::TransferStatus status = request_sense_raw(&s_devices[devIndex], sense);
    if (status == usb::XFER_SUCCESS) {
        s_devices[devIndex].lastSense = *sense;
        s_devices[devIndex].lastSenseValid = true;
    }
    return status;
}

usb::TransferStatus synchronize_cache(uint8_t devIndex)
{
    if (devIndex >= MAX_STORAGE_DEVICES || !s_devices[devIndex].active)
        return usb::XFER_ERROR;
    uint8_t command[10] = {};
    command[0] = SCSI_SYNCHRONIZE_CACHE_10;
    StorageDevice& dev = s_devices[devIndex];
    const usb::TransferStatus status = run_command(&dev, 0x00, command,
        sizeof(command), nullptr, 0);
    if (status == usb::XFER_SUCCESS) {
        dev.syncCacheState = SYNC_CACHE_SUCCEEDED;
        dev.writeCompletedAwaitingFlush = false;
    } else {
        const bool illegalRequest = dev.lastSenseValid &&
            (dev.lastSense.senseKey & 0x0Fu) == 0x05 &&
            (dev.lastSense.asc == 0x20 || dev.lastSense.asc == 0x24);
        dev.syncCacheState = illegalRequest ? SYNC_CACHE_UNSUPPORTED
                                            : SYNC_CACHE_FAILED;
    }
    return status;
}

} // namespace usb_storage
} // namespace kernel
