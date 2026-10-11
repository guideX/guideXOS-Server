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

## Continuation 1 — freestanding build and QEMU ATA PIO qualification

The earlier **Outcome B** record above is preserved. Continuation 1 closed the
two remaining NDB1R1 gates without changing the accepted ATA flush
implementation.

**Continuation result: `ATA_FLUSH_STATUS_VERIFIED` (Outcome A).** The normal
AMD64 kernel now builds and links. The production ATA PIO driver discovered a
strictly identified disposable QEMU IDE disk, completed PIO reads and writes,
issued the advertised cache-flush command successfully, and passed a fresh
QEMU-process readback. Hosted status-injection and propagation regressions
remain green. Native SQL durability and physical ATA behavior remain outside
this result.

### Freestanding-header blocker and repair

The reproduced error was:

```text
core/desktop.cpp:22
  -> core/include/kernel/kernel_apps.h:20
  -> guide_web_http_shared.h:11
  -> app_model_limits.h:3
fatal error: cstddef: No such file or directory
```

The failing kernel compile used C++14 with `-ffreestanding -nostdlib
-nostdinc++ -fno-exceptions -fno-rtti`. `app_model_limits.h` uses only the
global C `size_t` name; it does not use `std::size_t` or another C++ standard
library facility. The one-line compatibility repair changed its include from
`<cstddef>` to `<stddef.h>`. No constants, checks, Settings behavior, or ATA
code changed. The hosted App Model identity consumer still builds and passes
**25/25** checks.

### Full kernel build

The successful normal build command was:

```text
mingw32-make -C kernel ARCH=amd64 -j4
```

Toolchain: `g++ (MinGW-W64 x86_64-msvcrt-posix-seh, r7) 15.2.0`, using the
repository's AMD64 MinGW configuration and pinned local Mbed TLS profile
(`MBEDTLS 4.1.0`, TF-PSA Crypto `1.1.0`, three profile patches). The profile
check reported `MBEDTLS_PROFILE_VERIFY=PASS`. The full build exited **0** and
produced `kernel/build/amd64/bin/kernel.elf` (2,650,480 bytes; SHA-256
`55A5D88CAA3AFE4A732A05FAC2AFF123C05EDBE736029B4FD0704B522AC99494`). It
linked successfully with no errors. Existing warnings remain, including
unused parameters/variables, signedness and member-order warnings, and a
desktop buffer-bound warning. The warning set did not prevent linking.

The QEMU qualification entry and sector-writing code are compiled only when
`GXOS_QEMU_ATA_PIO_QUALIFICATION` and `GXOS_QEMU_ATA_MULTIBOOT_BOOT` are
explicitly defined. A separate production-configuration build was run after
QEMU testing; a byte-string check confirmed the normal `kernel.elf` contains
no `[NDB1R1-ATA-QUAL]` test entry strings.

### Hosted regressions

The final rerun retained the accepted counts:

- ATA status and capability injection: **21/21 passed**.
- Block → FAT → VFS flush propagation: **12/12 passed**.
- Block-device generation regression: **5/5 passed**.
- Hosted VFS read regression: **4/4 passed**.
- Hosted App Model limits consumer: **25/25 passed**.

The ATA injection cases include completed `ERR`, `DF`, and `ERR+DF`, busy
timeout, missing-device status, unexpected `DRQ`, unsupported flush capability,
and command-selection checks. These hosted tests prove error detection and
propagation; they do not claim QEMU generated those fault states.

### QEMU controller and isolated disk

Runtime qualification used QEMU `11.0.0` with `-machine pc,usb=off,accel=tcg`
and `-m 1024M`. The `pc` machine exposed its PIIX-compatible legacy IDE
controller. The guest used the production `kernel/core/ata.cpp` PIO driver and
its `BDEV_ATA_PIO` block callbacks. The test-only Multiboot loader in
`tests/ndb1r1_qemu_multiboot_loader.asm` loads the full kernel test build as a
module and enters `kernel_main`; the test entry initializes the block and ATA
layers, runs the bounded proof, and exits through QEMU's `isa-debug-exit`
device. This headless qualification path does not exercise UEFI or desktop
startup. The regular full-kernel build above used the standard boot object and
has the test entry disabled.

The disposable disk was created empty with this command:

```powershell
& 'C:\Program Files\qemu\qemu-img.exe' create -f raw 'D:\dev\guideXOSServer_NATIVE_DB_STORAGE\out\validation\NDB1R1_ATA_PIO_20261011\ata-pio-disposable-32MiB.raw' 32M
```

The image was stored at:

```text
out/validation/NDB1R1_ATA_PIO_20261011/ata-pio-disposable-32MiB.raw
```

It was exactly **32 MiB / 33,554,432 bytes / 65,536 sectors** at 512 bytes per
sector. QEMU attached only this raw test image as `bus=ide.1,unit=0`, the
secondary-channel master. No physical host drive was exposed, and
`disks/test-fat32.img` was not accessed. The runtime command was:

```powershell
& 'C:\Program Files\qemu\qemu-system-x86_64.exe' -machine pc,usb=off,accel=tcg -m 1024M -kernel 'D:\dev\guideXOSServer_NATIVE_DB_STORAGE\out\validation\NDB1R1_ATA_PIO_20261011\qemu_multiboot_loader.elf' -initrd 'D:\dev\guideXOSServer_NATIVE_DB_STORAGE\out\validation\NDB1R1_ATA_PIO_20261011\kernel-ata-pio-qualification-test.elf' -drive 'if=none,id=ndb1r1,file=D:\dev\guideXOSServer_NATIVE_DB_STORAGE\out\validation\NDB1R1_ATA_PIO_20261011\ata-pio-disposable-32MiB.raw,format=raw,cache=writeback' -device 'ide-hd,drive=ndb1r1,bus=ide.1,unit=0,serial=GXOSNDB1R1TEST01' -device 'isa-debug-exit,iobase=0xf4,iosize=0x04' -display none -serial 'file:D:\dev\guideXOSServer_NATIVE_DB_STORAGE\out\validation\NDB1R1_ATA_PIO_20261011\write-phase.serial.log' -monitor none -no-reboot
```

The independent-process read-only run used the same arguments and disk, with
`-serial` changed to
`file:D:\dev\guideXOSServer_NATIVE_DB_STORAGE\out\validation\NDB1R1_ATA_PIO_20261011\cross-process-readback-final.serial.log`.
The wrong-identity run used the same disk and kernel, with the IDE serial
changed to `GXOSNDB1R1WRONG01`; it rejected the device before I/O and wrote
`negative-identity.serial.log`.
Before any guest write, the production IDENTIFY result was checked against the
configured disposable identity and exact capacity. It reported:

```text
name=ata1m model=QEMU HARDDISK serial=GXOSNDB1R1TEST01
sectors=65536 sectorSize=512 ioBase=0x0170 role=master transport=PIO
lba48=1 flushCache=1 flushCacheExt=1
```

The block descriptor mapped back to that ATA PIO driver index and provided
read, write, and flush callbacks. The guest rejected an out-of-range read and
write at `LBA=totalSectors` before ATA I/O. A separate wrong-serial run used
`GXOSNDB1R1WRONG01`; the guest rejected it before sector reads or writes, and
the disk SHA-256 remained the original all-zero image hash
`83EE47245398ADEE79BD9C0A8BC57B821E92ABA10F5F9ADE8A5D1FAE4D8C4302`.

### PIO, flush, and relaunch results

The guest first read sectors **4096–4099** and **4100–4103** and verified their
initial bytes were zero. These ranges start at 2 MiB on an empty raw disk and
contain no boot sector, partition table, or filesystem metadata. It wrote two
deterministic four-sector patterns with sector/LBA/byte-position variation,
then performed production ATA PIO readback and exact byte comparisons.

Pattern A passed exact readback before flush. The production block flush
callback selected **`FLUSH CACHE EXT` (`0xEA`)** from IDENTIFY capabilities and
returned `BLOCK_OK`; exact readback passed after flush. A repeated flush also
returned `BLOCK_OK`. Pattern B was then written and read successfully after
the flush, flushed successfully, and both ranges passed final exact comparison.
No read/write or flush failure was observed. QEMU exited through the configured
debug-exit device after the result, avoiding an unbounded or unattended guest.

After that QEMU process closed, a fresh process launched with the same disk.
It detected the exact patterns, performed read-only PIO reads, and reported
`CROSS_PROCESS_READBACK PASS`; the disk SHA-256 stayed
`AB16AF0AEA38E13FA177DD872591B93681256B32651B4467C9A295BAC5CC9B96` across
the read-only relaunch. A host-side raw-image inspection independently
confirmed the pattern bytes and found **zero nonzero bytes outside sectors
4096–4103**. This is normal QEMU close/relaunch evidence, not crash testing,
arbitrary power-loss durability, or proof of real hardware cache behavior.

Serial logs, the qualification kernel, the small Multiboot loader, and the
disposable image are retained under
`out/validation/NDB1R1_ATA_PIO_20261011/`. Physical bare-metal ATA remains
untested. FAT32 metadata durability, native SQL integration and recovery,
truncate/resize, provider cleanup, and the other limitations listed above
remain open for their later milestones.

### Continuation 2 — unexpected merge audit and formal closeout

**NDB1R1 result: `ATA_FLUSH_STATUS_VERIFIED` (Outcome A; formally accepted).**
The unexpected merge was retained after a complete parent and file-delta audit.
The full AMD64 kernel was rebuilt, all hosted NDB1R1 regressions passed, and the
existing QEMU PIO/flush qualification was verified against the unchanged
production ATA source. No history topology operation was performed during this
continuation.

#### Merge provenance and scope

The qualification checkout was on `main` at `c69b2e97546a7b2c2d91c8f731f670e9c141ffc6`
when this continuation began. The merge parents are:

```text
first parent:  b75a0cb18bd985cd0c6bd50afcdebeeec61a9441
second parent: c642f5ca1887123f766059fb1502b0e1dbd13e71
merge base:    51b28451dabffc6275bb8092ac5c9f18c3dfcbe0
```

The first parent descends from NDB1R1 commit
`70e0022c807752786df052eef45ba69c1726afcf` (ATA flush completion-status
correction). The second parent is “Document S9R native Settings runtime
boundary” and is an ancestor of the principal Server checkout's audited
`main` HEAD `c51ace40f5932a70e301f7e259e455f0667c8cea`.

The complete first-parent delta of `c69b2e97` is exactly these three Settings
documentation files:

```text
A docs/SETTINGS_S9R_NATIVE_INTEGRATION_ASSESSMENT_20261011.md
M docs/SETTINGS_S9_1_0_ACCEPTANCE.md
M docs/SETTINGS_S9_ACCEPTANCE_RECOVERY_20261011.md
```

The assessment documents that the hosted Settings Center has no current native
kernel entry point or provider bridge. The other two files link and summarize
that assessment. The merge introduced no executable source, build setting,
test-harness, ATA, block, VFS, or qualification change. In particular,
`kernel/core/ata.cpp`, the flush-status and propagation implementation, and the
`<stddef.h>` App Model compatibility fix are unchanged by the merge. No
NDB1R1 qualification path was lost or overwritten. The merge is explicitly
retained; the three Settings documents are not part of the NDB1R1 closeout
commit.

HEAD remained at `c69b2e97` throughout the audit and requalification; no
concurrent history change was observed. Only the authorized NDB1R1 closeout and
phase-metadata commits below advance it.

The pending NDB1R1 worktree inventory was limited to the documentation update,
the compile-time-gated QEMU qualification hook in `kernel/core/main.cpp`, its
header and implementation, and the two test-only Multiboot loader files. The
disposable disk, generated ELFs, test executables, build logs, and serial logs
remain under the ignored `out/validation/NDB1R1_ATA_PIO_20261011/` directory and
are not staged. No other checkout was modified.

#### Requalification on the retained merge

The normal AMD64 freestanding kernel was force-rebuilt from this source state:

```text
mingw32-make -B -C kernel ARCH=amd64 -j4
```

It exited **0** with MinGW x86_64 g++ **15.2.0**, Mbed TLS **4.1.0**, and the
pinned TF-PSA Crypto **1.1.0** profile (`MBEDTLS_PROFILE_VERIFY=PASS`). The
resulting `kernel/build/amd64/bin/kernel.elf` is 2,650,480 bytes and has SHA-256
`55A5D88CAA3AFE4A732A05FAC2AFF123C05EDBE736029B4FD0704B522AC99494`, matching
the historical normal-build hash. The normal production ELF contains no
`[NDB1R1-ATA-QUAL]` test-entry strings. The forced build log is
`out/validation/NDB1R1_ATA_PIO_20261011/kernel-build-normal-forced-merge-recheck.log`.

All hosted suites were rebuilt and rerun successfully on this source state:

- ATA status/capability injection: **21/21**.
- Flush propagation through block, FAT, and VFS: **12/12**.
- Block-device generation regression: **5/5**.
- Hosted VFS read regression: **4/4**.
- App Model identity consumer: **25/25**.

The corresponding `*-merge-recheck.log` records are in the validation
directory. Existing compiler warnings remain non-fatal; no suite failed.

#### QEMU qualification evidence and limits

The existing qualification build log records the explicit
`GXOS_QEMU_ATA_PIO_QUALIFICATION` and `GXOS_QEMU_ATA_MULTIBOOT_BOOT` flags for
the test-only kernel. Its serial evidence remains under
`out/validation/NDB1R1_ATA_PIO_20261011/`:

```text
write-phase.serial.log
negative-identity.serial.log
cross-process-readback-final.serial.log
boot-probe-no-disk.serial.log
```

The logs show QEMU **11.0.0**, a `pc` machine with the PIIX-compatible IDE
controller under TCG, and the production ATA PIO callbacks bound to the
secondary-channel master identified as `QEMU HARDDISK`, serial
`GXOSNDB1R1TEST01`, 65,536 sectors. The run rejected a wrong serial before
sector I/O; rejected out-of-range reads and writes; verified sectors
4096–4103 initially zero; wrote and exactly read back two deterministic
four-sector patterns; completed `FLUSH CACHE EXT` (`0xEA`) and repeated flushes;
and passed a fresh-process read-only pattern readback.

The retained disposable raw disk is 32 MiB / 65,536 sectors. A read-only host
inspection during this continuation confirmed its SHA-256 remains
`AB16AF0AEA38E13FA177DD872591B93681256B32651B4467C9A295BAC5CC9B96`, both
expected patterns exactly occupy sectors 4096–4103, and every byte outside
those sectors is zero. The retained QEMU evidence remains valid because the
merge changed only the three Settings documentation files and the production
ATA and qualification source used by the recorded run is unchanged. No new QEMU
guest run was needed for this documentation-only merge.

This proves QEMU TCG behavior and clean-shutdown/relaunch persistence only. It
does not establish arbitrary power-loss durability or physical bare-metal ATA
behavior.

#### Acceptance and phase state

The reviewed NDB1R1 closeout commit contains only the documentation, optional
qualification hook and module, and test-only loader sources listed above. The
generated QEMU disk, kernel binaries, caches, output logs, and unrelated
Settings documentation are excluded. The phase metadata is advanced separately
to:

```text
project=guideXOSNativeDatabaseStorage
last_completed=NDB1R1
next_expected=NDB1R2
```

The principal Server checkout and its pre-existing untracked `out/` artifacts
were left unchanged. The separate SQL checkout remains clean at
`a4baf7d37c2476840dde20880761c3a70206b51a`; its `.phase` remains
`last_completed=SQL9`, `next_expected=SQL10`.

NDB1R1 is formally closed. Physical bare-metal ATA testing, VFS truncate/resize,
FAT32 metadata durability, SQL provider cleanup and partial-page recovery,
native SQL9 runtime integration, `GxosDatabaseFile`, three-boot SQL persistence,
and native WAL crash recovery remain outside this acceptance. NDB1R2 may begin
as the next phase; it was not started in this continuation.
