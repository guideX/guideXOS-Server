# Stellar Navigator SN10 — Native Application Bring-up

### SN10R12 offline fault localization (recorded before repair)

**Outcome B — the first native fault is localized to an ELF load-alignment defect.** This localization was recorded before changing production code. Starting branch/HEAD/phase are `STELLAR_NAVIGATOR`, `007f1924105173a315a437653076cd963fa9d30e`, and `SN10`. The full porcelain census is 1,982 dirty entries (235 modified tracked, 1,747 untracked); all were preserved. A snapshot is at `%TEMP%\stellar-sn10r12-starting-status.txt` (SHA-256 `182853baf712ea3dbefb7101ca8f221e1d9577490e500de35bb8ced26adaa4c9`).

#### SN10R11 artifact and guest evidence

The exact SN10R11 evidence directory is `E:\bkup\artifacts\SN10R11_20261022\qemu\20261022T142554330`. Its structured result records the exact working-tree production kernel (`kernel/build/amd64/bin/kernel.elf`, 2,650,304 bytes, SHA-256 `3aea5f9f10734dc6f248c6bdc05379d3b69a8d0d4b2ad3ec689f4dc71cb9ea1a`) and native application (`Apps/StellarNavigator/bin/amd64/stellarnavigator.elf`, 15,544 bytes, SHA-256 `6d5f1ca1b04a9ddbaf08e2eed8600ffaf776c06b6e45aab435f61fda9c2e66eb`), both matching the accepted candidate manifest. The MBR/FAT32 candidate and QAC disposable clone each had SHA-256 `29d060d068edc81daf720ca314e7b9dd9a3bbd376a053d1c0cb903de0c00a8a5`; the run confirmed the source disk was unchanged. The recorded invocation is `qemu-invocation.json`; QAC granted reservation `c5c5a9c0045c4e4ba22377dc20adae50`, directly owned QEMU PID 3948, then released it after the guest exited. No launch is being repeated as part of this offline report.

`readelf` identifies the native ELF as static ELF64 AMD64 `ET_EXEC`, entry `0x201a40`, with `PT_LOAD` ranges `0x200000..0x200a38` (R), `0x201a40..0x2044be` (R-X), and `0x2054c0..0x216000` (RW), each with `p_align=0x1000`. The loader logged the runtime image range `0x60c1f8..0x6221f8`, entry `0x60dc38`, executable range derived from `0x201000..0x205000`, and 23 rebased absolute words. Thus the fault is inside the app image and its executable subrange. Numerically it is also inside the kernel ELF's broad RW/BSS `PT_LOAD` range `0x312000..0x22b5aa60`, because this loader allocates app bytes from the kernel heap in that range; it is not in the kernel's file-backed executable range `0x100000..0x31103c`.

#### Fault address, instruction, and cause

The saved RIP `0x60df8d` maps through the loader's actual relocation delta (`runtime base 0x60c1f8 - ELF minimum 0x200000 = 0x40c1f8`) to link-time address `0x201d95`; it is not an offset from `gx_main`. `objdump` and the ELF symbol table identify the instruction as the first instruction of local function `(anonymous namespace)::evaluate_fixed_state(gx_app_context*, AppData*, FileSource*)`, called by `gx_main`:

```text
201d95: 0f 28 05 44 e4 ff ff    movaps -0x1bbc(%rip),%xmm0  # 2001e0
```

At runtime the RIP-relative operand is `0x60c3d8`, which is 8 modulo 16. The original operand `0x2001e0` is 16-byte aligned, but adding the loader's `0x40c1f8` relocation delta makes it misaligned. Intel's instruction reference specifies that a memory `MOVAPS` operand must be 16-byte aligned and that an unaligned operand raises `#GP` ([Intel SDM, Volume 1](https://cdrdv2-public.intel.com/843827/253665-sdm-vol-1-dec-24.pdf)). This exactly accounts for vector 13 with error code zero at this instruction. The code uses a compiler-generated aligned read from static constant data; the failure is not an invalid opcode, a bad app stack, or a failed service call.

The kernel loader in `kernel/core/native_elf_baremetal.cpp` reads each program header but computes `minimum`/`maximum` using only `kPageSize`, allocates exactly `imageBytes` with `new uint8_t[]`, places the ELF at that arbitrary heap pointer, and derives the relocation delta from it. It does not validate or honor `Elf64ProgramHeader::align`. The observed base `0x60c1f8` violates the ELF's 4 KiB load alignment and loses the compiler-provided 16-byte constant alignment. The faulting source function is in `sdk/samples/stellarnavigator/main.cpp`; there is no DWARF line table in the shipped ELF, so `addr2line` reports function/file (`main.cpp`) with line `?`. The preserved `.symtab` is sufficient for function attribution.

#### Captured CPU context and separate desktop warning

The serial log captured vector `0x0d` (`#GP`), error `0`, RIP `0x60df8d`, CS `0x08`, RSP `0x641da0`, SS `0x10`, RBP `0x641e00`, RFLAGS `0x10202`, CR2 `0`, and CR3 `0x3d9a7000`. CS/SS show CPL 0, consistent with the kernel's in-process native loader. All general registers were captured: `RAX=0xfffffffd`, `RBX=0x642040`, `RCX=0x22b57a18`, `RDX=0x6420d0`, `RSI=0x642040`, `RDI=0x22b57a18`, `R8=0x642218`, `R9=0`, `R10=0x1008`, `R11=0x60dc38`, `R12=0x22b57a30`, `R13=0x22b57a18`, `R14=0x22b57a18`, `R15=0x22b577a8`. RSP is within the app stack `0x622218..0x642218` and 16-byte aligned. The stack dump contains return address `0x60dd6d` (link-time `0x201b75`), confirming the caller is `gx_main`. Runtime metadata identifies app `Stellar Navigator`, runtime 1, no window, zero ABI calls, and zero app frames. The exception handler did not record XMM state or CR0/CR4, but neither is needed to explain this aligned-load fault; earlier SSE instructions executed successfully.

The independent `/Desktop` warning preceded native entry: serial line 313 reports `VFS_MKDIR` failure with `FAT_CREATE_IO_ERROR` and block timeout `0x02`. It is a separate FAT32 directory-write issue. The faulting instruction reads the app's rebased static constant before any host ABI call, so the recorded desktop timeout is not on the faulting instruction path and is not established as its cause.

**Offline conclusion before repair:** a specific loader defect is established with high confidence: the app image base fails the ELF `p_align` contract, misaligns a compiler-emitted `MOVAPS` operand, and produces the observed `#GP(0)`. No code change, repair test, rebuild, or follow-up guest run had occurred at the time this localization was recorded.

**Current result: Outcome B — SN10R12 localized the SN10R11 `#GP` to a violated ELF `p_align` contract, corrected the native ELF loader, and passed offline regression, build, and candidate checks. The single authorized QAC attempt stopped at a fresh occupancy check: another project held the managed acceptance lane, so no reservation was requested and no guest was launched. Guest verification remains outstanding; `.phase` remains `SN10`.**

### SN10R12 repair and offline qualification update (2026-10-22)

**Outcome B — the loader alignment correction is validated offline; guest acceptance remains unverified.** The offline localization above was completed before any source changes. The production fix is limited to `kernel/core/native_elf_baremetal.cpp` and its small freestanding helper `kernel/core/include/kernel/native_elf_alignment.h`: the loader validates each `PT_LOAD` alignment and file/virtual-address congruence, allocates the required padding, and chooses an image base whose residue matches the ELF minimum virtual address. It frees the original allocation pointer during every cleanup path. The ELF static constants therefore retain their link-time alignment after relocation. The AppModel ABI, native application, and supported ELF contract are unchanged.

The focused regression in `kernel/tests/native_elf_alignment_test.cpp` covers the SN10R11 base and `MOVAPS` operand, `p_align` validation, congruence failures, larger alignments, and allocation/address overflow. `scripts/run-native-elf-alignment-test.ps1` builds it with `-std=c++11 -Wall -Wextra -Werror` and runs it outside the repository. It passed. The hosted `StellarNavigator.Tests` xUnit suite passed **323/323** (net10.0); the native ephemeris core tests passed **15/15**. The full AMD64 freestanding kernel build passed, including its freestanding compile probe. The isolated kernel ELF is 2,646,208 bytes, ELF64 AMD64 `ET_EXEC`, entry `0x100000`, with R-X and RW `PT_LOAD`s both `p_align=0x1000`; SHA-256 is `bd44eba206db542c48426f0d01af67fdbe89f3916d3ce18c525c8deb0c83b1d5`. The production native application was rebuilt with its existing flags and remains byte-identical to the accepted static AMD64 `ET_EXEC`: 15,544 bytes, entry `0x201a40`, no dynamic interpreter or relocation tables, SHA-256 `6d5f1ca1b04a9ddbaf08e2eed8600ffaf776c06b6e45aab435f61fda9c2e66eb`.

The candidate is a separate copy under `E:\bkup\artifacts\SN10R12_20261022\candidate`; only its `kernel.elf` differs from the accepted SN10R2 ESP. Its staged app ELF, manifest, DE440s dataset, and UEFI bootloader hashes remain `6d5f1ca1b04a9ddbaf08e2eed8600ffaf776c06b6e45aab435f61fda9c2e66eb`, `08b43a2e0ddb05550d8ee02733b0ebbcc109556a29f685d6cfe54280b6a8df7c`, `d52b91573c29e6a4e5b6dc19c4af4d9747f6da2122f5b3aff4503684a96d2c0a`, and `07e5350656e93e28f30cda66bbb924aef7d3d7053498547b27cc469b88d21783`. The reproducible MBR/FAT32 candidate passed FAT read-back, path traversal, MBR geometry, partition-type, and candidate-only runner verification. It is 270,532,608 bytes with SHA-256 `3012be96730d09f2818e2002acba0a02c9f2f0f7cc4c9265b2b8e67c6eb3b2b4`; the source ESP, accepted SN10R6 GPT, accepted SN10R8 disk, and checked-in production build artifacts retained their original hashes. The kernel-specific digest override is confined to the runner invocation and the saved R12 packaging wrapper; the repository media builder and accepted manifests were not changed.

The updated QAC acceptance runner's Windows PowerShell 5.1 self-tests passed, including candidate hash overrides, command construction, serial handling, candidate integrity, and fail-fast lane checks. Candidate-only preflight also passed. One bounded acceptance invocation ran at `E:\bkup\artifacts\SN10R12_20261022\qac-run\qemu\20261022T144852333`. Its complete artifact-integrity check passed, then the fresh QAC census reported one managed session and one QEMU process, with no queued requests. The occupied reservation was `7a621de28b7d48869a88dbbfbb7d7197`, owned by the separate `guideXOSServer` acceptance run (owner PID 24308, child QEMU PID 23924). QAC was online; no reservation was requested, no foreign process/session was touched, and this turn launched no QEMU guest. The structured result is `acceptance-result.json`; the preflight status/list/queue snapshots are saved beside it.

Consequently, SN10R12 has no new guest serial output or screenshots. It does not establish whether the alignment fix prevents the fault at runtime, whether `/Desktop` creation now succeeds, or whether the native window, authentic DE440s ET/TDB 0 evaluation, coordinate tolerance, close/reopen lifecycle, and desktop stability pass. No ephemeris values or interactive UI qualification are claimed. The `/Desktop` FAT32 write timeout from SN10R11 remains separately unresolved and was not shown to cause the original `#GP`. QAC occupancy is the only reason the targeted guest verification was not attempted. `.phase` remains `SN10`.

All 1,982 pre-existing dirty entries from the starting census were retained. Before the local commit, the worktree showed those entries plus the six SN10R12 source/documentation changes and a generated Python bytecode cache at `scripts/__pycache__/prepare-stellar-navigator-sn10-mbr-media.cpython-312.pyc`; that cache is also preserved and excluded from the commit.

### SN10R10 qualification update (2026-10-21)

**Outcome B — PowerShell 5.1 runner repair and offline preflight passed; native guest qualification was blocked by QAC occupancy.** Starting branch and HEAD were `STELLAR_NAVIGATOR` and `e8d9fc61cd7cc33c23e0aa62859257415471094f`; `.phase` was `SN10`. The full porcelain recount (`git status --porcelain=v1 --untracked-files=all`) found **1,982 pre-existing dirty file entries**: 235 modified tracked files and 1,747 untracked files. They were preserved. The only intended repository changes in this qualification are this report and `scripts/run-stellar-navigator-qac-acceptance.ps1`; no production kernel, application, or media artifact was rebuilt.

#### PowerShell 5.1 compatibility repair

The initial real Windows PowerShell 5.1 run failed while `Add-Type` compiled the embedded C# helper. The CodeDOM diagnostic was `error ... (75) : { expected` at `catch (ObjectDisposedException) when (_stop.IsCancellationRequested)`, followed by the same diagnostic for the `SocketException` filter. Windows PowerShell 5.1's .NET Framework C# compiler does not accept C# exception filters. Inspection also found .NET Core-only calls elsewhere in the runner: cancellable `TcpListener.AcceptTcpClientAsync`, `Memory<T>` stream overloads, `ProcessStartInfo.ArgumentList`/`Environment`, and `Path.GetRelativePath`.

The repair replaces the serial reader's async Core APIs with a background .NET Framework thread and blocking TCP reads. It keeps typed exception classification and checks cancellation explicitly in the `ObjectDisposedException`, `SocketException`, and `IOException` handlers. Each serial read is written and flushed immediately; unexpected errors remain visible in `ErrorMessage`. Stopping closes the listener and accepted client, waits for the worker, and reports a worker-stop timeout. Process invocation now uses tested Windows command-line quoting with `ProcessStartInfo.Arguments` and `EnvironmentVariables`; candidate-relative paths use the already-validated ESP prefix. QEMU arguments remain a structured vector with doubled filename commas and one writable raw IDE disk.

#### Required PowerShell 5.1 preflight

The actual executable was `C:\WINDOWS\System32\WindowsPowerShell\v1.0\powershell.exe`. The preflight recorded PowerShell `5.1.26100.9711` (Desktop), CLR `4.0.30319.42000`, .NET Framework `4.8.9363.0`, and `C:\WINDOWS\Microsoft.NET\Framework64\v4.0.30319\csc.exe` file version `4.8.9221.0`. The PowerShell 5.1 parser reported **0 errors**. Its real `Add-Type -Language CSharp` call compiled and loaded `SN10R3SerialCapture` and `SN10R3ManagedProcess` without compiler errors or warnings; the self-test instantiated and invoked both helpers.

The complete offline runner self-test passed at `E:\bkup\artifacts\SN10R10_20261021\qemu\20261021T214119185\runner-selftest.json`. It verified exact process argument boundaries including an embedded quote, trailing backslash, and empty argument; redirected stdout/stderr and exit code 23; a bounded process wait timing out while the child was active followed by normal exit; incremental TCP markers and preservation of partial output after abnormal exit; separate no-client capture, connected zero-byte output, timeout, stream-disconnect, and guest-failure classifications; ordered first-close, AppModel relaunch, second evaluation, and cleanup markers; and release/rebind of capture ports and exclusive reopening of logs.

The same test made a writable disposable copy of the actual candidate disk, confirmed its initial SHA-256 matched the source and that a write-open succeeded, checked the effective raw IDE argument, and removed the test copy without changing the source. The focused result records no QEMU launch and no QAC request. The full candidate-only verification passed separately at `E:\bkup\artifacts\SN10R10_20261021\qemu\20261021T214142526\candidate-verification.json`.

#### Candidate identity and QAC gate

The original SN10R8 raw MBR/FAT32 image remains unchanged at SHA-256 `29d060d068edc81daf720ca314e7b9dd9a3bbd376a053d1c0cb903de0c00a8a5` (270,532,608 bytes). The verified layout has FAT32 partition 1 at LBA 2048, type `0x0C`, 262,144 sectors, and FAT32 EFI partition 2 at LBA 264192, type `0xEF`, 262,144 sectors. Candidate verification confirmed the MBR bytes, partition extents, filesystem BPBs, and 15 payload hashes. Source artifact hashes remain kernel `3aea5f9f10734dc6f248c6bdc05379d3b69a8d0d4b2ad3ec689f4dc71cb9ea1a`, native ELF `6d5f1ca1b04a9ddbaf08e2eed8600ffaf776c06b6e45aab435f61fda9c2e66eb`, app manifest `08b43a2e0ddb05550d8ee02733b0ebbcc109556a29f685d6cfe54280b6a8df7c`, DE440s `d52b91573c29e6a4e5b6dc19c4af4d9747f6da2122f5b3aff4503684a96d2c0a`, UEFI bootloader `07e5350656e93e28f30cda66bbb924aef7d3d7053498547b27cc469b88d21783`, and OVMF `2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d34733393ab2792`.

After the offline gates passed, the fresh QAC snapshot found the coordinator online with **1 QEMU process, 1 managed session, 1 running reservation, and 0 queued reservations**. Reservation `a9a9ca24e49041c290b65f418e50e06a` belongs to project `guideXOSServer`, mode `acceptance`, owned by PID 13744, with QEMU child PID 20948. The runner recorded `QAC_OCCUPIED`, requested no reservation, and did not launch QEMU. The reservation and session were left untouched. The captured status, list, queue, and structured decision are in `E:\bkup\artifacts\SN10R10_20261021\qemu\20261021T214142526\qac-status-before.log`, `qac-list-before.log`, `qac-queue-before.log`, and `qac-preflight.json`.

No SN10R10 guest boot was attempted. QEMU initialization, firmware boot, kernel startup, filesystem mount, AppModel discovery, native ELF loading, application initialization, ephemeris evaluation, UI presentation, and application lifecycle therefore remain unobserved in this run. There is no guest serial output, screenshot, guest coordinate tuple, or close/reopen evidence. Do not infer guest acceptance from the candidate or offline tests. Keep `.phase` at `SN10`; the next guest attempt requires a fresh empty QAC lane and the single authorized managed launch. No push was made.

### SN10R8 qualification update (2026-10-20)

**Outcome B — media validated; managed guest qualification blocked by QAC occupancy.** This work started on `STELLAR_NAVIGATOR` at `58ca4fca2c211c13fcad0069d897959e5c26861f`, with `.phase` `SN10` and 239 pre-existing dirty entries (235 modified tracked paths and 4 untracked paths). Those unrelated entries were preserved. The qualification attempt ended at the same HEAD and phase. The validated media builder, runner changes, and focused tests were committed locally as `73e294cfb77e4aa2647a0fa0b6b1a8d8bb0a8e6e`; the report is a separate closeout commit. No kernel or application production source was changed.

#### Boot and mount topology

SN10R7's recorded QEMU vector attached one raw hard disk as `if=ide,index=0`, with OVMF in a separate read-only pflash drive. Serial evidence shows UEFI loaded `EFI/BOOT/BOOTX64.EFI` from that IDE hard disk. The UEFI bootloader then loaded `kernel.elf` from the same EFI filesystem. After kernel startup, ATA enumerated the same raw IDE disk as `ata0m`, and `mount_persistent_storage()` tried that whole block device for `/`. Thus UEFI boot and the kernel root mount used one physical image, but through separate firmware and block-device paths. The old GPT image's protective MBR type `0xEE` did not lead the kernel FAT probe to the GPT EFI partition at LBA 2048.

#### MBR candidate and offline validation

The original GPT source remains unchanged at `E:\bkup\artifacts\SN10R6_20261020\media\stellar-navigator-sn10r6.raw`, SHA-256 `79dc215fb2fd6f4ade3d1a6e56eeed0297ca733adc241769b3b0a76422450b8a` before and after preparation. The distinct SN10R8 candidate is `E:\bkup\artifacts\SN10R8_20261020\media\stellar-navigator-sn10r8-mbr.raw` (270,532,608 bytes), SHA-256 `29d060d068edc81daf720ca314e7b9dd9a3bbd376a053d1c0cb903de0c00a8a5`.

The raw image has a valid `55 AA` MBR signature and two non-overlapping, 1 MiB-aligned primary FAT32 partitions using 512-byte sectors:

- Partition 1: type `0x0C`, LBA 2048, 262,144 sectors. The kernel's current `is_fat_partition_type()` explicitly accepts `0x0C`. The BPB has 512-byte sectors, one sector per cluster, two FAT copies of 2,033 sectors each, 258,046 data clusters, and allocated root cluster 2. Offline FAT traversal found `/Apps/StellarNavigator`, `app.json`, the native AMD64 ELF, and DE440s data.
- Partition 2: type `0xEF`, LBA 264192, 262,144 sectors. This is the MBR EFI System Partition type used by UEFI. Its FAT32 volume contains `EFI/BOOT/BOOTX64.EFI`, `kernel.elf`, and `ramdisk.img`, which the existing bootloader loads from its own firmware volume.

The accepted staged source tree was checked against `full-tree-expected-files.json`. All 15 payload hashes match, including kernel `3aea5f9f10734dc6f248c6bdc05379d3b69a8d0d4b2ad3ec689f4dc71cb9ea1a`, UEFI bootloader `07e5350656e93e28f30cda66bbb924aef7d3d7053498547b27cc469b88d21783`, Stellar Navigator ELF `6d5f1ca1b04a9ddbaf08e2eed8600ffaf776c06b6e45aab435f61fda9c2e66eb`, manifest `08b43a2e0ddb05550d8ee02733b0ebbcc109556a29f685d6cfe54280b6a8df7c`, and DE440s `d52b91573c29e6a4e5b6dc19c4af4d9747f6da2122f5b3aff4503684a96d2c0a`. The candidate is marked read-only; the acceptance runner only creates a byte-identical writable per-run copy after confirming the QAC lane is empty.

Reproduction uses `scripts/prepare-stellar-navigator-sn10-mbr-media.py` with the pinned `pyfatfs==1.1.0` environment and `tools/release_esp.py`. It preserves the input GPT image, validates each FAT volume, checks the actual kernel partition-type probe, and records hashes and geometry in `stellar-navigator-sn10r8-verification.json`. The focused media regression tests passed (3 tests). The runner self-tests passed, including argument construction, serial capture, writable-copy isolation, and QAC-lane fail-fast behavior. Candidate-only runner preflight passed against the new MBR image.

#### QAC blocker and guest result

The fresh QAC census at 9:41 PM PDT on 2026-10-20 found the coordinator online with one managed QEMU process, one managed session, one active reservation, and no queued requests. The active reservation was `eb86ad86e6614aa497c5718a82910ccc` for project `guideXOSServer`, mode `acceptance`, with QEMU child PID `35140`. It was not owned by this qualification. The acceptance runner recorded `QAC_OCCUPIED` and `reservationNotRequested=true` before creating a per-run disk copy or invoking `qac run`. It left the active session and reservation untouched.

The QEMU argument builder passed its offline vector check, but no guest QEMU command was launched. There is no SN10R8 guest serial, mount marker, AppModel discovery, native launch, ET/TDB 0 evaluation, numerical comparison, lifecycle, screenshot, or stability evidence. Those gates remain unverified; the MBR media checks establish compatibility by structure and source inspection only. Full guest qualification still requires one run after a fresh empty-lane census and exclusive QAC reservation.

#### Closeout

`.phase` remains `SN10`; the phase does not advance while guest qualification is incomplete. The outcome is partial rather than a kernel/filesystem defect: the selected MBR layout is compatible with the existing mount probe, while the authorized guest gate was unavailable because QAC was occupied. The media, runner, and test changes are in implementation commit `73e294cfb77e4aa2647a0fa0b6b1a8d8bb0a8e6e`; this report is recorded in the following local closeout commit. Nothing was pushed.

### SN10R7 qualification update (2026-10-20)

### SN10R7 qualification update (2026-10-20)

**Outcome C — technical blocker: the guest FAT32 mount path does not read GPT partition entries.** The one authorized guest attempt started and ended on `STELLAR_NAVIGATOR` at `e142c4c7072ecb72052602d25814e28658e99304`; `.phase` was `SN10` and remains `SN10`. The starting worktree contained 1,982 pre-existing dirty entries (235 modified tracked paths and 1,747 untracked paths), rather than the 239 described in the authorization. All were preserved. The QEMU acceptance run changed no repository files. The follow-up runner repair is committed locally as `e467f5d71b6d910ce6ae003182dbdd75c761b7ee`; no kernel or application production source changed.

#### Writable-disk preflight and runner checks

The accepted raw disk is `E:\bkup\artifacts\SN10R6_20261020\media\stellar-navigator-sn10r6.raw` (270,532,608 bytes), SHA-256 `79dc215fb2fd6f4ade3d1a6e56eeed0297ca733adc241769b3b0a76422450b8a`. Candidate-only verification passed and matched the accepted manifest. Kernel `3aea5f9f10734dc6f248c6bdc05379d3b69a8d0d4b2ad3ec689f4dc71cb9ea1a`, native ELF `6d5f1ca1b04a9ddbaf08e2eed8600ffaf776c06b6e45aab435f61fda9c2e66eb`, app manifest `08b43a2e0ddb05550d8ee02733b0ebbcc109556a29f685d6cfe54280b6a8df7c`, DE440s data `d52b91573c29e6a4e5b6dc19c4af4d9747f6da2122f5b3aff4503684a96d2c0a`, OVMF `2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d34733393ab2792`, and bootloader `07e5350656e93e28f30cda66bbb924aef7d3d7053498547b27cc469b88d21783` all matched.

The full preflight copy at `E:\bkup\artifacts\SN10R7_20261020\preflight\qac-disposable-disk-preflight.raw` had the same SHA-256, was not marked read-only, and opened for writing as `LEON2_RDT\guideX`. The source retained its `Archive` attribute and its original SHA-256. The IDE attachment was validated offline as `file=<per-run-copy>.raw,format=raw,if=ide,index=0`; it had no read-only property and was a hard disk. The only read-only drive was the separate OVMF pflash firmware. QAC CLI source inspection and the runner's vector check confirmed that QEMU arguments pass through unchanged. Evidence is in `preflight\writable-disk-preflight.json` and `preflight\offline-command-validation\qemu-command-offline-validation.json`.

The offline acceptance-runner self-tests passed twice, including after the final runner changes. They cover structured argument boundaries, live TCP serial capture, marker detection, timeout and abnormal-exit handling, disk copying and source isolation, close/reopen key automation, QAC queue ownership, and endpoint cleanup. The disk-copy test sets the source read-only, verifies the copy is writable, then simulates a guest write and confirms that only the copy changes. The runner now also classifies `NO_APPS_DIRECTORY` and `appmodel-launch result=failed` as guest acceptance failures. These are runner-only tests, not guest evidence. Candidate verification, self-test output, and the QAC attempt evidence are under `E:\bkup\artifacts\SN10R7_20261020\`.

#### QAC reservation and guest boot

A fresh QAC census at approximately 9:12 PM PDT showed the coordinator online with zero QEMU processes, reservations, managed sessions, or queued requests. The runner acquired exclusive acceptance reservation `cb4470bac23b496ba22dd7c5810bf1a2` and launched its direct QEMU child, PID `33260`. QAC held the reservation through the run and released it after QEMU exited with code 0. No foreign process or reservation was touched.

The effective QEMU command used `-drive if=pflash,format=raw,readonly=on,file=D:\dev\guideXOSServer_STELLAR_NAVIGATOR\OVMF.fd` and `-drive file=E:\bkup\artifacts\SN10R7_20261020\qemu\20261020T211310690\qac-disposable-disk.raw,format=raw,if=ide,index=0`. QEMU emitted no block-device startup error. UEFI executed the bootloader, the AMD64 kernel started, and ATA detected one drive (`ata0m`). The kernel tried that drive as persistent storage, but VFS reported `Filesystem mount failed` and `failed to mount boot runtime image at /`. The desktop rendered and continued producing kernel heartbeats. A 1280×800 desktop screenshot is `qemu\20261020T211310690\desktop-after-appmodel-failure.png` (original QEMU PPM is preserved beside it).

#### First failing component and application result

The first failed guest component was the root filesystem mount, before App Model discovery. Guest serial reports `[KERNEL] Trying persistent storage device ata0m at /`, `[KERNEL] WARNING: No writable persistent storage filesystem found`, and `[desktop] failed to mount boot runtime image at /`. App Model discovery then reported `[NATIVE-ELF] discovery result=NO_APPS_DIRECTORY`; the production dispatcher returned `usage=blocked-unknown-fallback`, followed by `[SN10-GUEST] appmodel-launch result=failed`.

The source explains the failure: `kernel/core/fs_fat.cpp` probes a FAT boot sector at LBA 0 or scans four MBR primary entries for legacy FAT partition types. It does not parse a GPT header or GPT partition entries. The verified candidate uses a protective MBR (partition type `0xEE`) and a GPT EFI System Partition beginning at LBA 2048, so the FAT mount path cannot reach the FAT32 boot sector. The IDE drive itself was detected and the candidate copy was writable; disk permissions and QEMU's IDE attachment were not the remaining blocker. The current evidence supports a kernel GPT mount-support gap as the exact root cause.

The guest never launched the native ELF. It produced no native shell, DE440s ET/TDB 0 computation, coordinate comparison, normal application close, production-shell relaunch, second instance, or application stability evidence. The runner result records `SERIAL_CAPTURE_FAILURE` because this task sent `quit` to its verified QAC-owned QEMU child after the app-discovery failure; QAC records the orderly code-0 exit and reservation release. The guest failure and screenshot precede that cleanup. No second launch was made.

#### Closeout

The run-copy SHA-256 changed from the verified initial hash to `b2c092b3d2512871c88d3dbe44a98a4a8aa9437ccf93bad9f3c620fb838b66da` across 1,983 bytes in 30 sectors, while the original candidate remained unchanged at `79dc215fb2fd6f4ade3d1a6e56eeed0297ca733adc241769b3b0a76422450b8a`. The final runner self-test passed at `qemu\20261020T211808951\runner-selftest.json`; the candidate verification and actual serial log, QAC reservation records, QEMU argument vector, screenshot, and disk-copy comparison are preserved under `E:\bkup\artifacts\SN10R7_20261020\`.

The phase remains `SN10`; no acceptance claim or phase advance is made. The remaining gate is GPT-aware FAT partition mounting (or an equivalently supported guest boot-disk layout), followed by a separately authorized guest acceptance run for native App Model launch, authentic ephemeris computation and tolerance, normal close/reopen, and stability. The runner-only repair commit is `e467f5d71b6d910ce6ae003182dbdd75c761b7ee`; this task did not push to GitHub.

### SN10R6 qualification update (2026-10-20)

**Outcome B — validated media and runner, guest qualification incomplete.** SN10R6 started on branch `STELLAR_NAVIGATOR` at expected HEAD `cb74725bdeb299c1760bfd04729e2a5f7cccfdb3`, with `.phase` `SN10` and 241 dirty worktree entries: the 239 unrelated entries plus the existing report and runner modifications. Those unrelated entries and earlier SN10 report history were preserved. No kernel or application source changed.

#### Storage root cause and corrected disk candidate

QEMU is `11.0.0 (v11.0.0-12122-ga4bb4b10c9)`. SN10R5 had attached `file=fat:<ESP>,format=raw,if=ide,index=0,readonly=on` and QEMU exited before serial with `Block node is read-only`. The December 2025 QEMU developer discussion documents the same failure from the read-only-by-default `vvfat` backend, including raw-format auto-detection restrictions; omitting the redundant `readonly=on` does not make that backend writable ([discussion](https://www.mail-archive.com/qemu-devel@nongnu.org/msg1156649.html)).

SN10R6 replaced `vvfat` with a real raw disk image. The first QAC attempt still used `readonly=on` on the IDE hard disk and QEMU again emitted `Block node is read-only`. QEMU's own `qemu-iotests/051.pc` fixture reproduces this exact error for `-drive ... if=ide,readonly=on`; the same fixture accepts a read-only CD-ROM. The common root cause is a read-only block backend attached as an emulated IDE hard disk. The read-only `vvfat` limitation can independently trigger the same error, so simply removing the option from the old directory backend was not a sufficient repair ([QEMU IDE read-only regression fixture](https://git.zx2c4.com/qemu/tree/tests/qemu-iotests/051.pc.out?id=b300c134465465385045ab705b68a42699688332#n740), [QEMU drive documentation](https://www.qemu.org/docs/master/system/qemu-manpage.html)).

The accepted staging directory was not modified. The existing `tools/release_esp.py` formatter/verifier, using its pinned `pyfatfs==1.1.0` and `fs==2.4.16` in an isolated evidence-directory venv, built and verified a 256 MiB FAT32 volume. It was embedded at LBA 2048 in a 258 MiB raw GPT disk with a protective MBR, EFI System Partition type GUID, 512-byte sectors, and valid primary/backup GPT CRCs. The FAT32 boot sector reports 512-byte sectors and 524,288 volume sectors. All 15 staged files (104,271,128 payload bytes) were read back and SHA-256 checked, including:

- EFI bootloader: `07e5350656e93e28f30cda66bbb924aef7d3d7053498547b27cc469b88d21783`
- proof kernel: `3aea5f9f10734dc6f248c6bdc05379d3b69a8d0d4b2ad3ec689f4dc71cb9ea1a`
- Stellar Navigator native ELF: `6d5f1ca1b04a9ddbaf08e2eed8600ffaf776c06b6e45aab435f61fda9c2e66eb`
- application manifest: `08b43a2e0ddb05550d8ee02733b0ebbcc109556a29f685d6cfe54280b6a8df7c`
- DE440s payload: `d52b91573c29e6a4e5b6dc19c4af4d9747f6da2122f5b3aff4503684a96d2c0a`

The validated raw disk is `E:\bkup\artifacts\SN10R6_20261020\media\stellar-navigator-sn10r6.raw`, SHA-256 `79dc215fb2fd6f4ade3d1a6e56eeed0297ca733adc241769b3b0a76422450b8a`. Its partition payload SHA-256 is `30257ce858c3781869331a261ca21da5edb1e89eba24bd7fd6b870fd85438315`. The complete FAT file manifest, geometry, EFI boot path `EFI/BOOT/BOOTX64.EFI`, accepted-candidate provenance, and disk hash are in `stellar-navigator-sn10r6-verification.json`; the formatter's read-back report is `fat-volume-verification.json` in the same `media` directory. The evidence directory also retains `prepare_sn10r6_disk.py`, the full-tree expected hash manifest, and the isolated pinned formatter environment for reproduction.

#### One QAC attempt and actual startup result

Candidate verification and the final offline runner self-test passed at `E:\bkup\artifacts\SN10R6_20261020\qemu\20261020T195911116\candidate-verification.json` and `E:\bkup\artifacts\SN10R6_20261020\qemu\20261020T200040460\runner-selftest.json`. One bounded QAC request was made after a fresh online/empty preflight. QAC granted reservation `6a6f3e6b28634c06867cf8e78bee9000` to runner PID `3620` and launched QEMU PID `24656`; the QEMU process exited with code 1 and QAC released that reservation. The exact argument vector and output are retained under `E:\bkup\artifacts\SN10R6_20261020\qemu\20261020T195636574\`.

That attempt used the verified GPT/FAT32 raw image through `-drive file=<image>,format=raw,if=ide,index=0,readonly=on`. QEMU connected to the serial listener but sent zero bytes, then closed the connection. The available stderr is `C:\Program Files\qemu\qemu-system-x86_64.exe: Block node is read-only`; `guest-serial.log` and `qemu.log` are empty. This is a **preboot storage/IDE initialization failure**, not a guest boot or application failure. The initial runner result missed the quick-exit race and recorded a generic QAC ownership error; the runner now classifies this early QEMU exit separately.

The runner now validates the immutable raw candidate, makes a byte-identical `qac-disposable-disk.raw` in the unique run-evidence directory, checks its initial SHA-256, and gives only that per-run copy to the QAC-owned IDE drive without `readonly=on`. QEMU can modify that disposable copy; the verified disk and original staged ESP remain unchanged. This follows QEMU's IDE hard-disk write-permission requirement without sharing the disk or granting write access to source artifacts. The corrected writable-clone launch is **not QEMU-verified** because the one bounded QAC attempt was used on the read-only variant; do not claim the raw disk has booted successfully.

No guest boot marker, desktop observation, AppModel launch, native window, real DE440s ET/TDB 0 tuple, coordinate comparison, first close, production-shell relaunch, second close, screenshot, or post-close stability evidence was collected. These checks are **unverified, not passed**. QAC contention was not the blocker: the reservation was granted and released. The next required gate is a new exclusive QAC attempt with the writable disposable raw-disk copy, then the full native guest acceptance flow. `.phase` remains `SN10`; no production artifacts were rebuilt and no push was made.

### SN10R5 qualification update (2026-10-20)

**Outcome B — guest qualification remains incomplete because the post-repair QAC acceptance lane is occupied.** SN10R5 started on branch `STELLAR_NAVIGATOR` at expected HEAD `cb74725bdeb299c1760bfd04729e2a5f7cccfdb3`, with `.phase` set to `SN10`. Preflight found 240 dirty entries: 239 unrelated pre-existing entries and this uncommitted report modification. The acceptance report remains uncommitted and `.phase` remains `SN10`. No production application or kernel source changed.

#### Candidate and first QAC-managed launch

Candidate-only verification passed at `E:\bkup\artifacts\SN10R5_20261020\qemu\20261020T143631166\candidate-verification.json`. Source and staged ESP hashes matched the accepted manifests: proof kernel `3aea5f9f10734dc6f248c6bdc05379d3b69a8d0d4b2ad3ec689f4dc71cb9ea1a`, native ELF `6d5f1ca1b04a9ddbaf08e2eed8600ffaf776c06b6e45aab435f61fda9c2e66eb`, DE440s data `d52b91573c29e6a4e5b6dc19c4af4d9747f6da2122f5b3aff4503684a96d2c0a`, app manifest `08b43a2e0ddb05550d8ee02733b0ebbcc109556a29f685d6cfe54280b6a8df7c`, OVMF `2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d34733393ab2792`, and UEFI bootloader `07e5350656e93e28f30cda66bbb924aef7d3d7053498547b27cc469b88d21783`. The existing staged ESP was reused unchanged.

A fresh QAC snapshot showed the coordinator online with zero QEMU processes, reservations, or queued requests. One managed request was made. QAC granted reservation `20e46155756b4cb383d0a7198b86b21e` to runner PID `16240` and launched QEMU PID `13280`, but QEMU 11 exited with code 1 before boot, reporting `Block node is read-only`. QAC released the reservation. The captured serial stream connected but contained zero bytes; there is no guest serial, desktop, application, ephemeris, lifecycle, or screenshot evidence. The argument vector is in `qemu-invocation.json`; QAC output and the structured result are in `E:\bkup\artifacts\SN10R5_20261020\qemu\20261020T143647060\`.

The failed option was the explicit `readonly=on` on QEMU's `file=fat:<ESP>` virtual FAT directory drive. QEMU documents that this backend is read-only by default and that `:rw:` enables writes ([QEMU virtual FAT documentation](https://www.qemu.org/docs/master/system/images.html)). The acceptance runner now omits the redundant block-level `readonly=on`, retaining the read-only FAT backend. Its offline runner self-test passed at `E:\bkup\artifacts\SN10R5_20261020\qemu\20261020T143806436\runner-selftest.json`; this validates the runner only and is not guest evidence.

#### QAC lane after runner repair

The fresh post-repair snapshot in `E:\bkup\artifacts\SN10R5_20261020\qemu\20261020T143806436\qac-status-before-retry.log` and its matching list/queue files found one active managed acceptance reservation for another `guideXOSServer` run: reservation `9a43f9515dd74529b552f4d1c0501f78`, verified owner PID `29312`, QEMU PID `18148`, and no queued reservations. That reservation was left untouched. No acceptance request or guest boot was started after this occupied-lane finding.

SN10 remains open. Native execution, interactive UI inspection, genuine DE440s ET/TDB 0 output and numerical comparison, normal close, production-shell reopen, second-instance calculation and close, and post-close guest stability are all **unverified, not failed**. The QEMU option repair has not yet been exercised against a guest. The next attempt must wait for an exclusive QAC acceptance reservation; do not queue while this lane is occupied. No push or commit was made, and `.phase` stays `SN10`.

The phase remains `SN10`. SN10R1 started on branch `STELLAR_NAVIGATOR` at the expected HEAD `b6996a4e5e24e928deec552e1a530a2f1f678add`, with `.phase` set to `SN10`. The 239 pre-existing unrelated worktree entries were preserved.

### SN10R4 qualification update (2026-10-20)

**Outcome B — the runner's close/reopen control path is implemented and offline-validated; native guest qualification remains blocked by the occupied QAC acceptance lane.** SN10R4 started on `STELLAR_NAVIGATOR` at `629ac89824fc975418e662cdd17a337227f69dc3` and ended at `cb74725bdeb299c1760bfd04729e2a5f7cccfdb3`; `.phase` was and remains `SN10`. The starting worktree had 240 dirty entries: 239 unrelated pre-existing entries and this outstanding report modification. Those unrelated entries and the earlier SN10R1–SN10R3 report history were preserved. The ending HEAD is the local runner-only commit `cb74725bdeb299c1760bfd04729e2a5f7cccfdb3`; the report update remains in the worktree.

#### Runner close/reopen path and offline checks

The previous runner stopped after the first normal close. It now opens the existing guest shell with QEMU's documented `sendkey` mechanism and submits `desktop.launch com.guidexos.stellarnavigator`. The shell command calls `desktop::launch_app`, the same production desktop/AppModel dispatch path used by the boot proof. The runner uses serial-buffer offsets so a second window-created, initial-content-drawn, and ephemeris marker must occur after the first instance exits. It then sends Escape, requires the second native loader cleanup marker, captures the returned shell and desktop, and verifies three seconds of continued guest/QAC ownership and serial-stream stability. No kernel or Stellar Navigator application source changed.

The runner now classifies fatal kernel/native serial markers and nonzero QEMU exit as guest failure, a missing marker after a clean guest exit, a live guest timeout, serial stream disconnect, no guest serial output, serial capture failure, and QAC coordination failure separately. Its mock-backed offline self-test passed after the final runner change at `E:\bkup\artifacts\SN10R4_20261020\qemu\20261020T085220323\runner-selftest.json`. It checks the shell key mapping, serial cursor handling, fatal marker recognition, bounded timeouts, stream disconnect classification, queue cancellation ownership, resource release, and argument construction. Candidate-only verification passed at `E:\bkup\artifacts\SN10R4_20261020\qemu\20261020T084757454\candidate-verification.json`.

The existing candidate was reused without rebuilding. Verified SHA-256 values remain: AMD64 kernel `3aea5f9f10734dc6f248c6bdc05379d3b69a8d0d4b2ad3ec689f4dc71cb9ea1a`; native ELF `6d5f1ca1b04a9ddbaf08e2eed8600ffaf776c06b6e45aab435f61fda9c2e66eb`; DE440s data `d52b91573c29e6a4e5b6dc19c4af4d9747f6da2122f5b3aff4503684a96d2c0a`; app manifest `08b43a2e0ddb05550d8ee02733b0ebbcc109556a29f685d6cfe54280b6a8df7c`; OVMF `2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d34733393ab2792`; and UEFI bootloader `07e5350656e93e28f30cda66bbb924aef7d3d7053498547b27cc469b88d21783`. The structured QEMU argument vector is recorded in the run evidence, but QEMU was not started during SN10R4.

#### QAC reservation and guest qualification

A fresh QAC 2.0.0 check at `2026-10-20 08:48:49 PDT` found one active exclusive acceptance reservation for project `guideXOSServer`: reservation `1951474742474bc9a6a0d222b41a491e`, verified owner PID `2780`, and QEMU PID `18800`. The coordinator reported one managed session, zero foreign QEMU processes, and no queued request before SN10R4. The documented FIFO acceptance queue supports a bounded QAC-managed request, so the runner made one `qac run` attempt with a 20-second acquisition bound. QAC assigned this attempt reservation `cae97fc2fc114387b11e18c1a90d7378` to runner CLI PID `4900` in `Waiting` at queue position 1. The lane remained occupied; the runner cancelled only that exact childless reservation through QAC and verified its terminal state was `Cancelled`. The post-run status still showed the original reservation and QEMU PID `18800`; the SN10R4 queue was empty. No foreign process or reservation was changed.

The complete structured attempt result is `E:\bkup\artifacts\SN10R4_20261020\qemu\20261020T084849892\acceptance-result.json`. It records `EXTERNAL_QEMU_COORDINATION_BLOCKER`, the effective QEMU argument vector that would have been passed to QAC, and a serial listener that received no QEMU client. No SN10R4 QEMU child or guest serial output exists. Therefore desktop boot, AppModel discovery/launch, native shell output, the ET/TDB 0 Earth coordinate tuple and tolerance comparison, unavailable-data handling, first close, second launch, second close, screenshots, and post-close guest stability remain **unverified, not failed**. No numerical parity is claimed.

The next guest session must begin only after QAC grants an exclusive acceptance reservation. The automated reopen path should be observed against that guest. If the guest does not emit the second instance's new UI markers within the run's bounded timeout, a human may use one bounded fallback in an operator-visible QAC-managed QEMU session: after the first normal close, press the guest backtick key, enter `desktop.launch com.guidexos.stellarnavigator`, and press Enter; verify the second native UI and serial ephemeris result, press Escape, verify the lifecycle and loader cleanup markers, then observe the desktop for three seconds. Stop and record the failed or missing step rather than retrying indefinitely. This procedure is a fallback only and is not SN10R4 acceptance evidence.

### SN10R3 qualification update (2026-10-20)

**Outcome B — runner repair committed; native guest qualification is blocked.** SN10R3 started on `STELLAR_NAVIGATOR` at `d5d944b69c55b0cd74dd8b5ba9a6d5ea31305467`, with `.phase` set to `SN10`. The initial worktree had 240 dirty entries: 239 unrelated pre-existing entries and this already-modified report. Those entries and the report's earlier SN10R2 content were preserved. The only commit created is `629ac89824fc975418e662cdd17a337227f69dc3` (`fix(stellar-navigator): stream acceptance serial capture`), containing the new runner script. This report remains uncommitted.

#### Runner repair and offline validation

The SN10R2 runner gave QEMU `-serial file:` and then polled that same file with `ReadAllText()` while QEMU held it open. Windows denied the read, so no serial bytes could be consumed; this was a capture failure, not evidence of a guest failure. The replacement runner uses a loopback TCP socket chardev, incrementally captures and flushes serial bytes, and distinguishes an absent connection, a connected stream with no bytes, and a stream error. It also builds QEMU arguments as a structured argument vector and doubles commas in QEMU key-value paths. The `-drive` value no longer contains literal quote characters, addressing the earlier QEMU argument parsing failure. The QEMU invocation and virtual FAT settings follow the [QEMU system invocation and chardev documentation](https://www.qemu.org/docs/master/system/qemu-manpage.html) and [virtual FAT documentation](https://www.qemu.org/docs/master/system/images.html).

PowerShell parsing, candidate-only verification, and the runner's offline self-test passed. The self-test verified argument boundaries, incremental serial recognition and trailing-byte preservation, abnormal producer exit reporting, bounded missing-marker classification, separate no-output/no-client classifications, listener cleanup, and port reuse. It launched no QEMU or QAC process. Candidate-only verification matched the recorded kernel, native ELF, DE440s data, application manifest, OVMF, and bootloader hashes. Artifacts are under `E:\bkup\artifacts\SN10R3_20261020\qemu\`, including `20261020T050640530\runner-selftest.json`, `20261020T050404681\candidate-verification.json`, and the actual preflight record at `20261020T050527100\acceptance-result.json`.

#### QAC coordination and guest gates

The fresh QAC status and reservation list showed an active exclusive acceptance reservation for another project, `guideXOSServer` (reservation `3e8f7054c5a94cdba2fa112764ade7f9`, owner PID `15388`, QEMU PID `23008`), running from `D:\dev\guideXOSServer_DiskManagerImprovements`. QAC reported zero foreign QEMU processes, one managed session and one queued session. The runner stopped before requesting a reservation or launching QEMU and recorded `EXTERNAL_QEMU_COORDINATION_BLOCKER`. The other managed run was left untouched.

No SN10R3 QEMU invocation occurred. Therefore this run produced no guest serial output, desktop screenshot, AppModel discovery or launch evidence, native execution evidence, ephemeris coordinate tuple or hosted-reference delta, or close/reopen lifecycle evidence. These guest gates remain unverified, not failed. The actual-run path can observe first launch, computation, and normal close, then returns `GUEST_REOPEN_INTERACTION_UNVERIFIED`; it captures the Start Menu but does not select the package for a second launch. That reopen interaction remains an incomplete gate even after QAC becomes available. The current result is Outcome B, `.phase` remains `SN10`, and no push was made.

### SN10R2 qualification update (2026-10-20)

**Outcome B — SN10 partial.** Starting and ending branch: `STELLAR_NAVIGATOR`. Starting and ending HEAD: `d5d944b69c55b0cd74dd8b5ba9a6d5ea31305467`. `.phase` was `SN10` at both ends and remains `SN10`. The initial worktree had 239 entries; no source or build output was changed by SN10R2, and those entries were preserved. This report is the only repository file changed for closeout. No commit was created.

The SN10R1 production guest candidate remains byte-for-byte identified by the requested hashes:

- Proof kernel: `kernel/build/amd64/bin/kernel.elf`, 2,650,304 bytes, SHA-256 `3aea5f9f10734dc6f248c6bdc05379d3b69a8d0d4b2ad3ec689f4dc71cb9ea1a`.
- Native package: `Apps/StellarNavigator/bin/amd64/stellarnavigator.elf`, 15,544 bytes, SHA-256 `6d5f1ca1b04a9ddbaf08e2eed8600ffaf776c06b6e45aab435f61fda9c2e66eb`. It is ELF64 AMD64 static `ET_EXEC`, with ELF entry `0x201a40` and `gx_main` at that address. The package manifest names `gx_main` and `guidexos-c-abi-v1`; the bare-metal loader requires those conventions.
- DE440s-derived asset: `Apps/StellarNavigator/Astronomy.Ephemeris/Data/ephemeris_de440s_chebyshev.sn4bin`, 32,117,373 bytes, SHA-256 `d52b91573c29e6a4e5b6dc19c4af4d9747f6da2122f5b3aff4503684a96d2c0a`.

The preserved SN10R2 private ESP stages the proof kernel, UEFI bootloader, `app.json`, native ELF, and data under `Apps/StellarNavigator`. The staged kernel, ELF, and data hashes match their recorded values. The kernel discovers `/Apps` after VFS mount and the existing Desktop/AppModel dispatcher launches the package. The guest did not provide serial output in the attempted run, so guest-side mounting, package discovery, and data access are **not verified**. Staging is not being reported as guest execution.

#### QAC lane and attempt result

A fresh census first found no QEMU processes. The visible QAC GUI was not treated as proof of service: the QAC 2.0.0 CLI at `D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe` returned `Coordinator: Online`, with zero QEMU processes, reservations, or managed sessions. A QAC-managed direct-child launch was used.

The initial private-runner launch exited before guest qualification. A subsequent captured QAC child run exposed a quoting error in the inherited runner copy: literal quotes inside the `-drive` value made QEMU exit with code 1 before boot. The private evidence runner removed those quotes and reused the verified kernel. The next managed QEMU start encountered a separate runner limitation: QEMU holds the `-serial file:` output exclusively while running, but the runner calls `ReadAllText` on that file to poll markers. That file-sharing error ended the attempt before any serial marker or screenshot could be collected. QAC later showed no active Stellar Navigator reservation; no guest result is claimed.

The lane was then acquired by another project's managed acceptance run. At the 2026-10-20 04:24:52 PDT census, QAC reported reservation `efd6f47b438742b1b314d99577f1d45c`, project `guideXOSServer`, mode `acceptance`, owner PID `15052`, and QEMU PID `13684` at `C:\Program Files\qemu\qemu-system-x86_64.exe`. The command line references the other checkout `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT` and its `focused-attempt-03-qac-sequence` guest. This is the actual external coordination conflict. It was left untouched, no further QEMU was started, and guest qualification stopped with `EXTERNAL_QEMU_COORDINATION_BLOCKER`.

#### Guest evidence and remaining acceptance gate

SN10R2 produced no guest serial log content or application screenshot. No guest coordinate tuple or numerical delta against the hosted reference was collected. The following remain unobserved: usable desktop, AppModel discovery and launch, native window rendering, authentic guest DE440s evaluation, displayed frame and units, missing or invalid-data handling, close/reopen behavior, and desktop or lifecycle stability. No managed/hosted regressions were rerun because no source or build input changed; SN10R1's recorded 15/15 native focused tests, 323/323 Windows regression, and Release GUI build remain the available host evidence.

The smallest remaining gate is to wait for the recorded QAC acceptance reservation to release, update the preserved acceptance runner to consume serial output through a readable live stream, then rerun the complete guest acceptance flow in an exclusive QAC lane. The staged candidate, private runner, preflight and blocked QAC snapshots, staged hashes, and launch notes are preserved under `E:\bkup\artifacts\SN10R2_20261020\`. `.phase` must remain `SN10` until the guest gates pass. No commit SHA exists for SN10R2.

## SN10R1 qualification update (2026-10-20)

### Freestanding header root cause and repair

The proof-kernel include trace was `kernel/core/desktop.cpp` → `kernel/core/include/kernel/kernel_apps.h` → `guide_web_http_shared.h` → `app_model_limits.h`. The final header included `<cstddef>`, which is a C++ standard-library header unavailable under the AMD64 kernel's existing `-nostdinc++` flag. This was a hosted header dependency in a shared bounds header, not a need for C++ library functionality.

`app_model_limits.h` uses global `size_t` and namespace-scope `constexpr` bounds only. It contains no `std::` names, hosted facilities, structs, or ABI layout declarations. All seven direct references are from C++ headers or sources; the file is C++-only because it declares a namespace and `constexpr` values. The kernel's established `kernel/core/include/kernel/types.h` convention uses `<stddef.h>` for freestanding `size_t`. The compatibility fix therefore changes only the include from `<cstddef>` to `<stddef.h>`. Constant values, public names, layouts, and calling conventions are unchanged.

With the original freestanding restrictions, syntax compilation passed for `core/desktop.cpp`, `core/kernel_apps.cpp`, `core/main.cpp`, and `core/gxos_tls_foundation.cpp`. The real proof-kernel build also passed with the guest proof flag and no weakened flags:

```text
C:\mingw64\bin\mingw32-make.exe -C kernel ARCH=amd64 EXTRA_CFLAGS=-DGXOS_STELLAR_NAVIGATOR_SN10_GUEST_PROOF -B -j4
```

The build exited 0 and produced `kernel/build/amd64/bin/kernel.elf` (SHA-256 `3aea5f9f10734dc6f248c6bdc05379d3b69a8d0d4b2ad3ec689f4dc71cb9ea1a`), an ELF64 AMD64 `ET_EXEC` with entry `0x100000`. It emitted warnings in unchanged kernel sources, including unused parameters in `kernel_app.h`, an array-bounds warning in `shell.cpp`, and a type-limits warning in `tcp.cpp`; no warning points to the changed header. Full compiler and linker output is preserved at `E:\bkup\artifacts\SN10R1_20261020\proof-kernel-build.log`.

The hosted C++ AppModel consumers `app_manifest_loader.cpp` and `app_manifest_validator.cpp` also passed syntax compilation with `g++ -std=c++17`, the language standard set by `guideXOSServer.vcxproj`. This confirms the C-header include continues to compile in the hosted configuration.

### Native package and regressions

The native package build completed with the existing sample command, `powershell -ExecutionPolicy Bypass -File sdk/samples/stellarnavigator/build.ps1`. Candidate identity:

- Path: `Apps/StellarNavigator/bin/amd64/stellarnavigator.elf`
- SHA-256: `6d5f1ca1b04a9ddbaf08e2eed8600ffaf776c06b6e45aab435f61fda9c2e66eb`
- Format: ELF64 AMD64 static `ET_EXEC`, entry symbol `gx_main` at `0x201a40`
- Dependencies: no interpreter, dynamic section, or relocations
- Manifest: `com.guidexos.stellarnavigator`, `kind: NativeElf`, `runtime: native-elf`, AMD64 entry `gx_main`, ABI `guidexos-c-abi-v1`, visible in the Start Menu

The existing launch registration resolves the validated NativeElf package ID to the bare-metal native ELF launcher through the Desktop/AppModel path. This confirms the package wiring in source and manifest; it is not guest launch evidence.

The focused native ephemeris suite passed **15/15**. It includes the fixed ET/TDB 0 Earth state, authenticated 13-segment DE440s index, deterministic repeat, invalid input, coverage, short-read, hash, size, teardown, and reinitialization checks. The current candidate matches the prior SN10 package hash. ELF inspection is recorded in `E:\bkup\artifacts\SN10R1_20261020\native-elf-inspection.txt`; native build and test logs are beside it.

The accepted SN9 Release regression was rerun from an isolated validation copy so the pre-existing build-output worktree entries stayed intact. Results were **323 passed, 0 failed, 0 skipped**. The clean test compile emitted the known inherited xUnit2002 warning at `StellarNavigator.Tests/EphemerisTests.cs:51`; that unrelated assertion was not changed. The SN9 Release GUI build passed with **0 warnings and 0 errors**. Logs are `E:\bkup\artifacts\SN10R1_20261020\SN9-Release-tests.log` and `E:\bkup\artifacts\SN10R1_20261020\SN9-GUI-Release-build.log`.

### Numerical comparison method

The trusted hosted SN4 reference for Earth (NAIF 399) at ET/TDB `0` seconds past J2000, in the J2000 SSB frame and kilometres, is `(-27566740.483, 132361381.154, 57418653.286)`. The native core test compares each computed double component against these reference coordinates with an absolute tolerance of `0.0005 km`, corresponding to the documented three-decimal display precision (`0.001 km`). The native focused test passed. There is no guest observation or guest numerical delta because QEMU was not launched.

### QEMU census and guest status

A fresh system-wide process census at `2026-10-20T10:40:40Z` found QEMU PID `2956`, executable `C:\Program Files\qemu\qemu-system-x86_64.exe`, using firmware and guest files from `D:\dev\guideXOSServerV0.5_DEVELOPER_STUDIO`. No `qac` command or QAC process was available. The session was identified as another project's guest and was left untouched. The census is recorded in `E:\bkup\artifacts\SN10R1_20261020\qemu-census-blocked.json`.

No SN10R1 guest boot was attempted. Therefore there is no serial log, screenshot, guest AppModel launch result, guest ephemeris output, guest invalid-data behavior, close/reopen evidence, or post-exit desktop-stability evidence. The existing QEMU checks remain open until an exclusive lane is available. This is the reason for Outcome B; native build success is not being presented as native guest execution or interactive qualification.

### Outcome and remaining work

**Outcome B — SN10 partial.** The header compatibility failure is repaired; the proof kernel, native package, focused tests, SN9 regression, and SN9 GUI build pass. `.phase` remains `SN10`. Do not advance it until an exclusive guest run verifies normal desktop initialization, AppModel package discovery and launch, the visible responsive shell, the authenticated ET/TDB 0 result and units/frame, corrupt or missing-data handling, normal close and reopen, and desktop stability. Capture serial output, screenshots, candidate hashes, and measured numerical differences from that run.

After SN10 guest acceptance, suggested SN11 scope remains a read-only native viewer with user-selected target and ET epoch, coverage-aware errors, and parity checks against the managed provider.

The project phase remains `SN10`. SN9 remains accepted as the Windows-hosted functionality milestone recorded by commit `c7993bc6bc536a2c673839e20af8c8dd4eb93253`; this work does not reopen SN9.

## Phase reconciliation

The requested starting HEAD was `c7993bc6bc536a2c673839e20af8c8dd4eb93253` on branch `STELLAR_NAVIGATOR`. The working `.phase` had regressed to `SN9`, while the accepted commit itself records `.phase` as `SN10` and accepts SN9 on Windows with 323 passing tests and live Windows UI evidence. The marker was restored to `SN10`, and the SN9R3 report was reconciled to preserve its supplemental host evidence and keep guest work under SN10.

The reconciliation was committed locally as `397a7c8d` (`docs: preserve SN9R3 evidence under SN10 scope`). The 239 unrelated pre-existing worktree entries were preserved and excluded from that commit.

## Runtime architecture discovered

The server contains a bare-metal AMD64 Native ELF package loader in `kernel/core/native_elf_baremetal.cpp`. It discovers packages from mounted `/Apps`, validates `app.json` metadata and executable paths, and accepts static ELF64 `ET_EXEC` images with the `guidexos-c-abi-v1` ABI and `gx_main` entry point. The loader rejects dynamic interpreters and relocations. It supplies app-owned windows, text and rectangle drawing, event polling, logging, file reads, and cleanup.

Packages use `kind: NativeElf`, architecture `amd64`, an ABI entry for `gx_main`, and package-relative files. The existing Desktop/App Model resolver maps a validated package ID to `desktop::launch_app`, which dispatches to the bare-metal loader. The normal hosted runtime does not execute Native ELF packages; the implemented boundary is the guideXOS bare-metal guest.

The SDK contract is defined by `sdk/include/guidexos/abi.h`, `app.h`, and `ui.h`. The implementation uses the existing ABI and loader without changing the server ABI or Windows Forms application.

## Native application delivered

The first native package is in `sdk/samples/stellarnavigator/`. The app provides a read-only window for Earth (NAIF 399) at ET/TDB `0` seconds past J2000. It presents the target, epoch, J2000 SSB frame, kilometre units, position, and authenticated-data status. If the asset is missing, unreadable, corrupt, or outside coverage, it presents a bounded error state. Escape or window close triggers shutdown.

The freestanding C++ core uses fixed-size structures, caller-owned storage, 64 KiB reads, and a bounded segment index. It has no STL, heap allocation, exceptions, RTTI, or dynamic ELF dependencies. It authenticates the full asset before parsing and evaluation, validates index and center-chain bounds, evaluates the indexed Type-2 Chebyshev records, and sums Earth → Earth-Moon barycenter → solar-system barycenter states.

The package manifest and built candidate are `Apps/StellarNavigator/app.json` and `Apps/StellarNavigator/bin/amd64/stellarnavigator.elf`. `scripts/run-stellar-navigator-sn10-qemu.ps1` stages a private ESP and has an opt-in kernel launch hook compiled only with `GXOS_STELLAR_NAVIGATOR_SN10_GUEST_PROOF`. The hook calls the real Desktop/App Model dispatch after VFS mount and package discovery; ordinary kernel builds do not launch the app automatically.

## Data and numerical conventions

The application reads the existing `ephemeris_de440s_chebyshev.sn4bin` at package-relative path `Astronomy.Ephemeris/Data/ephemeris_de440s_chebyshev.sn4bin`. The expected size is 32,117,373 bytes and the trusted SHA-256 is `d52b91573c29e6a4e5b6dc19c4af4d9747f6da2122f5b3aff4503684a96d2c0a`. The native parser validates the SNEPH04B v1 index (13 segments in the bundled asset) and does not extrapolate outside segment coverage.

The state is geometric position in the J2000 frame with SSB origin, in km, at ET/TDB zero. The native test matches the existing managed SN4 provider output to 0.001 km, which is the provider's displayed three-decimal precision. The expected display is:

```text
(-27566740.483, 132361381.154, 57418653.286) km
```

The Windows Forms application and its host-only behavior were not changed.

## Prior SN10 report evidence (before SN10R1)

- Native package build passed using LLVM `clang++` and `ld.lld`. The output is ELF64 AMD64 static `ET_EXEC`, has the `gx_main` entry point, and has no `PT_INTERP` or relocations. Candidate SHA-256: `6d5f1ca1b04a9ddbaf08e2eed8600ffaf776c06b6e45aab435f61fda9c2e66eb`.
- Focused portable native-core tests: **15 passed, 0 failed**. They cover authenticated open and index validation, the fixed-ET Earth state and managed parity, deterministic repetition, invalid and unsupported inputs, coverage bounds, short reads, hash and size failures, and reinitialization after teardown.
- At the time of the earlier SN10 report, the accepted SN9 regression baseline was documented as **323 passed, 0 failed, 0 skipped** and had not been rerun for that native-only slice. SN10R1 reran it; see the current results above.
- The repository’s pinned Mbed TLS/TF-PSA dependency bootstrap completed and `verify-mbedtls-profile.ps1` passed for the locked Mbed TLS 4.1.0 and TF-PSA-Crypto 1.1.0 revisions. This populated the ignored `third_party/mbedtls` dependency tree.
- In the earlier SN10 attempt, the proof-kernel build stopped at `<cstddef>` under `-nostdinc++`; SN10R1 resolves this failure as recorded above. The historical guest census found another project's QEMU session and did not interact with it.

The QEMU runner uses OVMF and the repository's UEFI bootloader path, an isolated writable FAT ESP, serial logs, and the AppModel proof hook. The earlier report had no guest evidence because the proof kernel had not built and another project's QEMU occupied the lane. SN10R1 fixes the kernel build; the foreign QEMU session still prevents this task from launching a guest.

## Remaining SN10 gates

1. Run the already-built proof kernel and native package through an available exclusive QEMU lane.
2. Confirm package discovery and AppModel dispatch, authentic guest ephemeris evaluation and numerical parity, successful first draw, controlled invalid-data behavior, close/reopen, loader cleanup, and desktop stability from guest evidence.

Keep `.phase` at `SN10` until these gates pass. Once SN10 is accepted, a suitable SN11 scope is adding user-selected targets and ET epochs to the native read-only viewer, with coverage-aware errors and parity checks against the managed provider.
