#ifndef KERNEL_QEMU_ATA_PIO_QUALIFICATION_H
#define KERNEL_QEMU_ATA_PIO_QUALIFICATION_H

namespace kernel {
namespace qemu_ata_pio_qualification {

// Only called by builds that explicitly define
// GXOS_QEMU_ATA_PIO_QUALIFICATION.
bool run();

} // namespace qemu_ata_pio_qualification
} // namespace kernel

#endif // KERNEL_QEMU_ATA_PIO_QUALIFICATION_H
