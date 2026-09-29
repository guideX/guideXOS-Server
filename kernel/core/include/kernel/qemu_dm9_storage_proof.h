#ifndef KERNEL_QEMU_DM9_STORAGE_PROOF_H
#define KERNEL_QEMU_DM9_STORAGE_PROOF_H

namespace kernel {
namespace qemu_dm9_storage_proof {

// Runs only in explicitly opted-in DM9/DM15/DM16 QEMU proof kernels. The
// harness accepts only the configured disposable secondary storage target.
void run(bool rootStorageMounted);

} // namespace qemu_dm9_storage_proof
} // namespace kernel

#endif
