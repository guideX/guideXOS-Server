#include "include/kernel/qemu_dm12_usb_proof.h"

#if defined(GXOS_DM12_QEMU_USB_PROOF)

#include "include/kernel/block_device.h"
#include "include/kernel/partition_table.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/vfs.h"

namespace kernel {
namespace qemu_dm12_usb_proof {
namespace {

static const char kMountPath[] = "/mnt/dm12-uhci-usb";
static const char kProofPartitionName[] = "DM9 QEMU Proof";
static const char kProofFilePath[] = "/mnt/dm12-uhci-usb/dm9/proof.bin";
static const char kExpectedPayload[] = "guideXOS DM9 QEMU proof 001\r\n";
static storage::PartitionTableModel s_partitionTable = {};

static bool text_equal(const char* left, const char* right)
{
    if (!left || !right) return left == right;
    while (*left && *left == *right) { ++left; ++right; }
    return *left == *right;
}

static bool text_equal_ascii_case_insensitive(const char* left,
                                              const char* right)
{
    if (!left || !right) return left == right;
    while (*left && *right) {
        char a = *left++;
        char b = *right++;
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return *left == *right;
}

static bool payload_matches(const char* actual, uint32_t size)
{
    if (size != sizeof(kExpectedPayload) - 1u) return false;
    for (uint32_t i = 0; i < size; ++i) {
        if (actual[i] != kExpectedPayload[i]) return false;
    }
    return true;
}

static bool directory_contains_proof()
{
    const vfs::HandleToken iterator = vfs::opendir(kMountPath);
    if (iterator == 0xFFu) {
        serial::puts("[DM12-QEMU-USB] root-directory=open-failed\n");
        return false;
    }

    bool found = false;
    vfs::DirEntry entry = {};
    while (vfs::readdir(iterator, &entry)) {
        serial::puts("[DM12-QEMU-USB] root-entry name=");
        serial::puts(entry.name);
        serial::puts(" type=");
        serial::put_hex8(entry.type);
        serial::putc('\n');
        if (text_equal_ascii_case_insensitive(entry.name, "dm9") &&
            entry.type == vfs::FILE_TYPE_DIRECTORY) {
            found = true;
            break;
        }
    }
    vfs::closedir(iterator);
    return found;
}

static bool read_proof_file()
{
    const vfs::HandleToken handle = vfs::open(kProofFilePath, vfs::OPEN_READ);
    if (handle == 0xFFu) {
        serial::puts("[DM12-QEMU-USB] proof-file=open-failed\n");
        return false;
    }

    char payload[sizeof(kExpectedPayload)] = {};
    const int32_t count = vfs::read(handle, payload,
        static_cast<uint32_t>(sizeof(kExpectedPayload) - 1u));
    const vfs::Status closed = vfs::close(handle);
    return closed == vfs::VFS_OK && count ==
        static_cast<int32_t>(sizeof(kExpectedPayload) - 1u) &&
        payload_matches(payload, static_cast<uint32_t>(count));
}

} // namespace

void run()
{
    uint8_t usbIndex = 0xFFu;
    const block::BlockDevice* usbDevice = nullptr;
    for (uint8_t i = 0; i < block::MAX_BLOCK_DEVICES; ++i) {
        const block::BlockDevice* candidate = block::get_device(i);
        if (!candidate || !candidate->active ||
            candidate->type != block::BDEV_USB_MASS) continue;
        usbIndex = i;
        usbDevice = candidate;
        break;
    }

    if (!usbDevice || !usbDevice->readFn || usbDevice->writeFn ||
        usbDevice->sectorSize != 512u ||
        usbDevice->totalSectors != 1228800ULL) {
        serial::puts("[DM12-QEMU-USB] proof=FAIL reason=expected-read-only-usb-geometry-not-found\n");
        return;
    }

    const uint64_t registrationId = usbDevice->registrationId;
    serial::puts("[DM12-QEMU-USB] capacity=PASS sectors=");
    serial::put_hex64(usbDevice->totalSectors);
    serial::puts(" sector-size=");
    serial::put_hex32(usbDevice->sectorSize);
    serial::puts(" read-callback=yes write-callback=no registration=");
    serial::put_hex64(registrationId);
    serial::putc('\n');

    if (!storage::parse_partition_table(usbIndex, s_partitionTable) ||
        s_partitionTable.state != storage::DISK_STATE_VALID_GPT ||
        !s_partitionTable.primaryGptValid || !s_partitionTable.backupGptValid ||
        !s_partitionTable.gptCopiesAgree) {
        block::OperationDiagnostic io = {};
        serial::puts("[DM12-QEMU-USB] gpt=FAIL state=");
        serial::put_hex8(static_cast<uint8_t>(s_partitionTable.state));
        serial::puts(" error=");
        serial::put_hex8(static_cast<uint8_t>(s_partitionTable.error));
        if (block::last_operation_diagnostic(io) && io.valid) {
            serial::puts(" io-operation=");
            serial::put_hex8(static_cast<uint8_t>(io.operation));
            serial::puts(" io-status=");
            serial::put_hex8(static_cast<uint8_t>(io.status));
            serial::puts(" io-lba=");
            serial::put_hex64(io.requestedLba);
            serial::puts(" io-count=");
            serial::put_hex32(io.requestedSectors);
            serial::puts(" bot-stage=");
            serial::put_hex8(io.transportDiagnostic.stage);
            serial::puts(" opcode=");
            serial::put_hex8(io.transportDiagnostic.commandOpcode);
            serial::puts(" usb-status=");
            serial::put_hex8(io.transportDiagnostic.statusCode);
        }
        serial::putc('\n');
        serial::puts("[DM12-QEMU-USB] proof=FAIL reason=gpt-validation-failed\n");
        return;
    }

    const storage::PartitionEntry* proofPartition = nullptr;
    for (uint16_t i = 0; i < s_partitionTable.partitionCount; ++i) {
        if (text_equal(s_partitionTable.partitions[i].name,
                       kProofPartitionName)) {
            proofPartition = &s_partitionTable.partitions[i];
            break;
        }
    }
    if (!proofPartition) {
        serial::puts("[DM12-QEMU-USB] proof=FAIL reason=proof-partition-not-found\n");
        return;
    }

    serial::puts("[DM12-QEMU-USB] partition=PASS name=DM9-QEMU-Proof lba=");
    serial::put_hex64(proofPartition->startLba);
    serial::puts(" sectors=");
    serial::put_hex64(proofPartition->sectorCount);
    serial::putc('\n');

    const uint8_t rootMountCount = vfs::mount_count();
    const vfs::PartitionMountResult mounted = vfs::mount_partition_detailed(
        kMountPath, usbIndex, proofPartition->partitionNumber,
        registrationId, proofPartition);
    if (mounted.error != vfs::PARTITION_MOUNT_OK ||
        !vfs::mount_identity_valid(mounted.mountIndex)) {
        serial::puts("[DM12-QEMU-USB] mount=FAIL reason=");
        serial::puts(vfs::partition_mount_error_name(mounted.error));
        serial::putc('\n');
        return;
    }

    const vfs::MountPoint* mount = vfs::get_mount_by_index(mounted.mountIndex);
    const bool readOnly = mount && mount->readOnly &&
        mount->parentRegistrationId == registrationId;
    const bool directoryOk = readOnly && directory_contains_proof();
    const bool fileOk = directoryOk && read_proof_file();
    const vfs::Status unmounted = vfs::unmount(kMountPath);
    const bool clean = unmounted == vfs::VFS_OK &&
        vfs::mount_count() == rootMountCount;

    serial::puts("[DM12-QEMU-USB] mount-read-only=");
    serial::puts(readOnly ? "PASS" : "FAIL");
    serial::puts(" directory-dm9=");
    serial::puts(directoryOk ? "PASS" : "FAIL");
    serial::puts(" file-bytes=");
    serial::puts(fileOk ? "PASS" : "FAIL");
    serial::puts(" unmount-cleanup=");
    serial::puts(clean ? "PASS" : "FAIL");
    serial::putc('\n');

    serial::puts(readOnly && directoryOk && fileOk && clean
        ? "[DM12-QEMU-USB] proof=PASS read-only-mount=PASS file-read=PASS unmount=PASS\n"
        : "[DM12-QEMU-USB] proof=FAIL read-only-mount-or-file-read-or-unmount-failed\n");
}

} // namespace qemu_dm12_usb_proof
} // namespace kernel

#endif
