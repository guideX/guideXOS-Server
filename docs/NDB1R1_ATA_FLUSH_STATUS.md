# NDB1R1 — ATA Flush Status Verification

## Scope and result

This is the bounded NDB1R1 ATA flush error-status repair. It verifies ATA
completion detection and the block/FAT/VFS status path. It does not verify
native SQL persistence, FAT32 crash consistency, sudden-power-loss behavior,
or QEMU durability.

**Result: `ATA_FLUSH_REPAIR_PARTIAL` (Outcome B).** The hosted status and
propagation gates pass, and each changed kernel translation unit compiles with
the existing freestanding flags. The full kernel build remains blocked by an
unrelated existing `-nostdinc++` / `<cstddef>` failure in
`app_model_limits.h`. ATA hardware identification and PIO read/write execution
also have no hosted regression harness in this repository. The native phase
marker therefore remains at NDB1R1.

## Audited false-success path

The call chain is `vfs::flush()` → `fs_fat::flush()` →
`block::flush()` → the registered ATA PIO flush callback → ATA status polling.
Before this repair, `ata_pio_flush()` issued `FLUSH CACHE EXT` whenever the
device reported LBA48 support and then called `ata_wait_bsy()`. That helper
also serves IDENTIFY and returns true as soon as the `BSY` bit clears. The
flush callback converted that result directly to `BLOCK_OK`; it never checked
completion `ERR` or `DF`, and did not select the intended drive before writing
the command.

`ata_wait_bsy()` was left unchanged, so IDENTIFY and other users retain their
existing polling semantics. Flush now uses a separate helper in
`kernel/core/include/kernel/ata_flush.h`.

## Corrected ATA protocol

The production ATA callback invokes the shared helper with callbacks that
write `ATA_REG_DRIVE_HEAD`, delay using alternate-status reads, read the main
`ATA_REG_STATUS` register, and write `ATA_REG_COMMAND`. The helper:

1. Selects master (`0xA0`) or slave (`0xB0`) before the command.
2. Waits up to the existing 500,000-poll bound for `BSY=0` and `DRQ=0`.
   Pre-command `ERR`/`DF` bits are treated as possibly stale and are not
   mistaken for the flush completion result.
3. Issues the supported flush command, waits 400 ns, and polls the main status
   register up to the existing 500,000-poll bound.
4. Accepts completion only when `BSY`, `ERR`, `DF`, and unexpected `DRQ` are
   all clear. Status `0x00` or `0xFF` is a transport/device-presence failure.
   A timeout remains a failure and retains the final observed status for
   diagnostics.

IDENTIFY word 82 bit 5 advertises `FLUSH CACHE`; word 83 bit 12 advertises
`FLUSH CACHE EXT`; word 83 bit 10 indicates LBA48. Flush command-set bits are
accepted only when word 83's validity bits are `01`. `FLUSH CACHE EXT` is
selected only when both LBA48 and EXT support are advertised. Otherwise the
driver uses basic `FLUSH CACHE` only when word 82 advertises it. If neither
command is supported, the callback returns `BLOCK_ERR_UNSUPPORTED` without
issuing a command.

ATA completion `ERR`, `DF`, both bits, unexpected `DRQ`, missing-device status,
and unsupported command capability map to a non-success block status. Busy
timeout maps to `BLOCK_ERR_TIMEOUT`. Diagnostics identify the failure class,
status byte, selected command, device index, and whether the command was issued.
The ATA driver rate-limits each failure class to one message per boot. The
per-call FAT flush trace lines were removed to avoid repeat-flush log spam.

## Error propagation

`block::flush()` returns the transport callback's status unchanged.
`fs_fat::flush()` returns false and preserves that block status in its output
parameter. `vfs::flush()` maps block timeout to `VFS_ERR_IO_TIMEOUT` and other
flush failures to `VFS_ERR_IO`. Successful status remains successful at each
layer. No database adapter exists yet; rejecting sync on a VFS failure remains
the future adapter contract.

## Failure-injection design and results

`tests/ata_flush_status_test.cpp` calls the same callback-template polling and
command-sequencing logic used by `ata_pio_flush()`. It does not duplicate the
status decision. The harness injects status bytes and records drive selection,
command issue, delay, and status-read ordering.

`tests/ata_flush_propagation_test.cpp` mounts an in-memory FAT32 volume on a
test block device whose flush callback can return success, I/O error, or
timeout. It calls the production block, FAT, and kernel VFS flush APIs.

Results:

- ATA status/selection cases: **21/21 passed**. This includes successful
  completion, completion `ERR`, completion `DF`, both flags, busy timeout,
  completion on the last allowed poll, completion just beyond the timeout,
  pre-command busy timeout, stale pre-command error bits, absent device,
  unexpected `DRQ`, master/slave selection, command selection, capability
  validity, and status-to-block mapping.
- Block → FAT → VFS successful flush propagation: **passed**.
- Block I/O failure → FAT failure → `VFS_ERR_IO`: **passed**.
- Block timeout → FAT failure → `VFS_ERR_IO_TIMEOUT`: **passed**.
- Block-device generation regression: **5/5 passed**.
- Existing hosted VFS read regression: **4/4 passed**.

These are simulated status and callback tests. They do not reproduce physical
disk errors.

## Native build and remaining verification

The following changed kernel files compiled individually with the repository's
AMD64 freestanding C++14 flags, including `-ffreestanding`, `-nostdlib`, and
`-nostdinc++`:

- `kernel/core/ata.cpp`
- `kernel/core/block_device.cpp`
- `kernel/core/fs_fat.cpp`
- `kernel/core/vfs.cpp`

The full `mingw32-make -C kernel ARCH=amd64 -j4` run passed the Mbed TLS profile
check using the exact pinned local dependency, then stopped in existing
unmodified code: `app_model_limits.h` includes `<cstddef>` while the kernel
build passes `-nostdinc++`. The error was `fatal error: cstddef: No such file or
directory`. No unrelated build configuration or source was changed to work
around it.

The repository contains no dedicated hosted ATA IDENTIFY or PIO read/write
test. The shared IDENTIFY wait helper and read/write routines were not changed;
`ata.cpp` compiled with the freestanding flags, but hardware execution and
physical-drive behavior remain unverified. QEMU was not run; it is optional for
NDB1R1. The shared `disks/test-fat32.img` fixture was not accessed.

## Durability limits and next phase

The repair establishes that the ATA PIO flush path rejects the tested failed
completion states and that a returned block failure is not converted to FAT or
VFS success. It does not establish that the device honors cache flushes, that
FAT32 metadata is crash-consistent, or that native SQL writes survive a reboot.

NDB1R2 should address its separately scoped filesystem durability requirement
only after this ATA gate is accepted. SQL9 remains unchanged at
`next_expected=SQL10`; this work does not advance the SQL marker.
