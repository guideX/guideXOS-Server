#ifndef KERNEL_QEMU_DM9_STORAGE_PROOF_H
#define KERNEL_QEMU_DM9_STORAGE_PROOF_H

namespace kernel {
namespace qemu_dm9_storage_proof {

// Runs only in kernels built with GXOS_DM9_QEMU_STORAGE_PROOF. The harness
// accepts only the explicitly attached QEMU secondary ATA hard disk.
void run(bool rootStorageMounted);

} // namespace qemu_dm9_storage_proof
} // namespace kernel

#endif
