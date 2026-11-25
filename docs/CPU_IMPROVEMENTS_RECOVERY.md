# CPU Improvements storage recovery

Recovery started November 24, 2026 (Pacific). The E: checkout now contains the original CPU7B2 implementation tree and the restored historical qualification report. The implementation has a new local commit ID because the original Git commit object was unavailable. Fresh guest qualification remains open.

## Starting repository state

- Checkout `E:\dev\guideXOSServer_CPU_IMPROVEMENTS`, branch `CPU_IMPROVEMENTS`. At intake it was the only attached worktree, with a clean index and no Git-visible untracked files. `HEAD` and `origin/CPU_IMPROVEMENTS` both pointed to `b5c840bfb1977bca0a758474f21298796a6a43a0`, tree `1b63257d7b5ea635081f6e05cd306820a659700e`.
- The configured remote is `git@github.com:guideX/guideXOS-Server.git`. SSH authentication failed. A read-only `git ls-remote` over HTTPS independently confirmed the same branch tip. The configured remote URL was retained.
- The checkout was a full clone with 52,122 packed objects, no alternate object store and clone-only branch and HEAD reflogs at intake. The existing Codex capture ref pointed to the CPU7B1 tree. `git fsck --full --no-reflogs --unreachable` completed without output. `git cat-file` could resolve `b5c840bf`; it could resolve neither `8bcd23c8` nor `12845378`.
- Neither CPU7B2 commit resolved in the Server clones at `E:\dev\guideXOSServer` and `C:\dev\guideXOSServer-E-20261124-recovery`, the CPU3R1–CPU6 build backups or the S22R22/S22R23 source backups. Several older backups used an alternate Git object store on D:. The exact `8bcd23c8b3366c6169adf2ee17a372d05ddb103a` request in a scratch clone was rejected by GitHub's upload-pack. D: was absent from the host filesystem drive inventory. The surviving E: CPU7B2 build checkouts had `.git` pointer files to worktree metadata on D:. No usable copy of that object store or the original CPU7B2 commits was located.

## Surviving milestone history

| Milestone | Commits already present at intake | Surviving work |
| --- | --- | --- |
| CPU1 | `f8556637` | AMD64 CPUID capabilities and address widths. |
| CPU2 | `1892297e` | ACPI MADT processor inventory. |
| CPU3 / CPU3R1 | `34be9d34`, `6a8263d2` | BSP-only per-CPU roster and the original guest reproduction. |
| CPU4 | `39776d8b`, `9d824721` | Bounded boot paging, task-arena isolation and guest qualification. |
| CPU5 | `8c5db589`, `3d698165` | MADT interrupt topology, resolved LAPIC address and topology qualification. |
| CPU6 | `6d60f0d6`, `372c81aa` | BSP APIC characterization and qualification. The independent CPU6 flag uses its original pre-IDT reader. |
| CPU7B1 | `b36d6672`, `b5c840bf` | Evidence-gated xAPIC admission and guest qualification. The standalone boot producer lacks a safe current-boot sample. |

The implementation and qualification records remain under `docs/CPU_IMPROVEMENTS_CPU*.md`. The CPU7B1 report also preserves the CPU7A review findings about entry frames, NMI, vector ownership and interrupt acknowledgment. [CPU7B1 report](CPU_IMPROVEMENTS_CPU7B1.md)

## CPU7B2 source and report recovery

The three historical build checkouts at `E:\CodexArtifacts\CPU7B2-b5c840bf\{stock,diagnostic,cpl3}` still contained the CPU7B2 source. The archived November 23 session recorded the 12 paths staged for `8bcd23c8`, the 580 insertions and 19 deletions, and the output of `git show -s --format=%T 8bcd23c8`. Starting from the CPU7B1 tree, each lane independently produced `c4f1e6fad9b0fce5488a3e01521d7f690956bd8e` after applying just those paths. That is the original implementation tree ID recorded in the session. Git also computed the same tree in the live E: checkout before committing. `96342c70c1f394a00dabcc65bc7b28a89206701c` records that tree with CPU7B1 `b5c840bf` as its direct parent.

The historical qualification report was restored by replaying the archived file creation and seven edits. The resulting 95 lines agree with the original `12845378` commit summary. The restored report blob is `a423cb0c1229a91916d8c18f2bdf4025d5da121d`. It retains the November 23 observations, old path references and original revision names. [Historical CPU7B2 report](CPU_IMPROVEMENTS_CPU7B2.md)

The recovered opt-in probe samples `IA32_APIC_BASE` on the BSP after IDT initialization. Its exact-site `#GP(0)` handler accepts only the expected active context and redirects to the controlled cleanup path. The historical post-IDT sample `0x00000000FEE00900` showed BSP xAPIC at `0xFEE00000`, matching the MADT. CPU7B1 reported `ownership-unverified`, `mapping=not-installed`, and `controller-access=not-attempted`. The historical lane retained PIC/PIT delivery and one online CPU. Historical desktop, input, PacMan, CPL3 and reservation-release evidence belongs to those candidate runs.

## Paths and tools

| Purpose | E: recovery location or configuration |
| --- | --- |
| Source | `E:\dev\guideXOSServer_CPU_IMPROVEMENTS` |
| Fresh outputs and logs | `E:\CodexArtifacts\CPU7B2-recovery-20261124`, with separate stock, diagnostic and CPL3 build clones |
| Historical evidence and media | `E:\CodexArtifacts\CPU7B2-b5c840bf`, `E:\CodexArtifacts\CPU7B1-372c81aa`, `E:\CodexArtifacts\S22R22` and `E:\CodexArtifacts\S22R23` |
| PacMan build input | Set `GUIDEXOS_PACMAN_SOURCE_ROOT` to `E:\CodexArtifacts\CPU7B2-recovery-20261124\pacman\guidexos`. The local clone resolves the repository's pinned revision `8fc2aedd603ddc57e492bdc02c99442bb165109b`. |
| Toolchain | MinGW GCC 15.2.0, GNU ld 2.46.0.20260210, GNU Make 4.4.1 and NASM 2.16.03 under `C:\mingw64\bin`. LLVM 22.1.8 built the PacMan Native ELF, and Visual Studio MSBuild 18.11.1.51303 built the bootloader. Python 3.12.10 used the pinned `pyfatfs` 1.1.0, `fs` 2.4.16 and `pycdlib` 1.16.0 packaging environment from the preserved CPU3R1 build. |
| QEMU and firmware | QEMU 11.0.0 at `C:\Program Files\qemu\qemu-system-x86_64.exe`. `C:\Program Files\qemu\share\edk2-x86_64-code.fd` hashes to the historical `33090cc07675baa5190d9f1e84bf5176b33bcbfa9bacac522961150cdb6dbb2a`. |

E: reported healthy with about 5.68 TB free at intake. `tracked-d-drive-paths.txt` in the recovery evidence lists tracked executable and build files containing D: references. The CPU hosted runners resolve their inputs relative to the repo. The release build needed the PacMan source override because `scripts/build-pacman-package.ps1` defaults to `D:\dev\pacman\guidexos`. The remaining D: references include x86 toolchain fallbacks, direct-QEMU filesystem and desktop smoke scripts, asset converters and historical generated runners. The archived CPU7B2 QAC wrappers also hard-code the old CLI path. They were kept as evidence.

## Fresh hosted validation

All ten AMD64 hosted runners in `scripts/test-amd64-*.ps1` exited 0. CPU1 passed 8 CPUID fixture groups plus the native CPUID and NX check. CPU2/CPU5 passed 10 processor and topology groups, and CPU3 passed 7 roster groups. CPU4 passed exact limits 511, 512, 513 and 1,024, rejection of page 1,025, and the 605-page alias, self-map and cleanup fixture. CPU6 and CPU7B1 each passed 6 groups. CPU7B2 passed 4 groups, including the NASM frame matcher, synthetic read outcomes, reentry and cleanup. The memory foundation, protected-task contract and voluntary context switch also passed. Detailed transcripts are in `hosted` under the recovery output directory.

Additional hosted checks passed for PS/2 modifiers (39/39), compositor pointer buttons, Native ABI layout, Native filesystem behavior, Native ELF security policy (29 checks, 4 authorized entry calls), and the MMIO source contract. Release integrity passed 37 assertions, synthetic OpenSSH authentication passed 10 cases, and synthetic FAT packaging passed with the pinned Python environment.

Fresh NASM assembly and `objdump` inspection of the production fixup confirmed a single `rdmsr` with ECX set to `0x1B`, an `iretq` on the accepted path and a relocation to the ordinary vector 13 stub on rejection. The object contained no `call` or `wrmsr`. Hosted frame matching and disassembly leave actual fault delivery, the stack return and continuation open for CPU7B2R1.

## Fresh AMD64 build configuration

The isolated clones were detached at `96342c70`. In each clone, the canonical command was `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/create-release-iso.ps1 -Version 0.3.0 -Arch amd64 -ReleaseProfile ServerV03PacManOnly -Clean -RequireCleanWorktree -PythonPath <pinned-python>`. The build environment placed `C:\mingw64\bin` first and excluded the Git `usr\bin` and `ucrt64\bin` paths so Windows Make recipes use `cmd.exe`. The three clones received independent copies of the pinned Mbed TLS 4.1.0 and TF-PSA-Crypto 1.1.0 input. The profile verifier passed with all three project patches. To provision a new clone independently, use `scripts/bootstrap-mbedtls.ps1 -Install`.

| Lane | `BSP_APIC_BASE_PROBE_DIAGNOSTIC` | `CPU_INVENTORY_DIAGNOSTIC` | `CPL3_DIAGNOSTIC` |
| --- | --- | --- | --- |
| Stock | 0 | 0 | 0 |
| CPU7B2 diagnostic | 1 | 0 | 0 |
| CPU7B2 and CPL3 control | 1 | 1 | 1 |

All three lanes set `BSP_APIC_DIAGNOSTIC=0` and `BSP_XAPIC_ADMISSION_DIAGNOSTIC=0` explicitly. CPU7B2 enables the admission evaluator in its own lane. A fresh dry-run with both the CPU6 pre-IDT reader and the CPU7B2 probe enabled was rejected before compilation with the expected Makefile error.

While the UEFI loader builds ran, separate `mingw32-make -j2 ARCH=amd64 BUILD_DIR=build/recovery-amd64 all` runs built and linked all three kernels with the same flags and clean source tree. They exited 0. The ELF and symbol audit confirmed AMD64 entry `0x100000`, no relocations, the expected PIC/PIT entry references, and diagnostic symbols gated by lane. Each fresh kernel matched the corresponding historical CPU7B2 kernel byte for byte.

| Lane | Fresh kernel SHA-256 |
| --- | --- |
| Stock | `c3d69e0f7f63b9fab00895f0d646c8da87959858f3a7915638002349ed1fc595` |
| CPU7B2 diagnostic | `1ae63ca330cb4f90eabad3997a0abb972e338f8693f5c26d9952d00a1a5af3f4` |
| CPU7B2 and CPL3 control | `1da2c68b447d55f0b3c0253d49b1b63e78260a52bf1dc06ae8831984149f7f69` |

All three fresh PacMan packages also reproduced the historical Native ELF SHA-256 `eeb544bbb2e2574eeaed7bb7f0bad61ea24f6945231b7757223a225b30b8e1cc`. The kernel logs, symbol audits and checksums are under the recovery evidence directory in `*-kernel-only.log`, `kernel-only-audit.log` and `fresh-kernels.json`.

## Fresh release artifacts and provenance

All three canonical release builds exited 0 from clean checkouts and produced 90,245,120-byte AMD64 candidate ISOs. Independent inspection verified each ISO's SHA-256, 16 staged input files against the release manifest, and the embedded FAT32 image through its UEFI El Torito records. The manifests bind the output to `96342c70` and the per-lane build ID. The packaged kernels matched both the standalone fresh builds and the corresponding historical CPU7B2 kernels. The ramdisk and loader `.text` matched the historical CPU7B2 files in every lane.

| Lane | Release build ID | Fresh ISO SHA-256 |
| --- | --- | --- |
| Stock | `c88e05c8-8469-41bc-9678-bf148ec6f303` | `6bf9101103f12feb45c81695628cabd016fea936647cd29aa15abeb26927b521` |
| CPU7B2 diagnostic | `4b18ee8c-5651-4a63-a7c1-34a526d17fbc` | `ae3c40de10ab6c96546a75fa2875a200968a6ca57ba66c1f1fff8982a0eb69a6` |
| CPU7B2 and CPL3 control | `521fc21d-76c8-4fe4-bf49-4a7aa617449f` | `6c3889b634589c644a50b10e070adbdb47337d38a57c2c462b2310283c79eb6a` |

The full EFI loaders and ISOs have new hashes. The EFI loaders differ in the PE header, including the timestamp, and in `.rdata` and `.pdata`. Their `.text`, `.data` and `.rsrc` bytes match the historical binaries. The media contain the new build identities. `fresh-provenance-and-comparison.json` records manifest and artifact paths and hashes. `bootloader-pe-comparison.txt` records the per-section comparison. The fresh artifacts carry offline build and packaging evidence. Their guest qualification is open.

## Preserved media and missing items

All four files in each historical CPU7B2 candidate receipt matched freshly computed hashes. The 34-entry preservation list also matched, including the CPU7B1 candidate files and the former D: checkout's ESP inputs and test disks at their E: counterparts. The historical evidence directories and release media were read in place. The two accepted ISOs, two persistent images, and the earlier S22R22 candidate had matching before/after recovery hashes.

| Preserved item | SHA-256 |
| --- | --- |
| S22R22 accepted ISO | `efa290538ef3155d264bf0b26923933bd2c9332aa5574e5be977be9d7a9c948c` |
| S22R23 accepted ISO | `8d397659e0a6ec8f1be113ae4f5be14dbcaf1ef22ccda41a6e5ee5014919cf40` |
| S22R22 accepted `persistent-data\guidexos-user-data.img` | `3ee67553ab9bece4130e4aa84ec4ed0d341cb29d7fd78798884c5ae3b0b7b148` |
| S22R23 historical writable test image `persistent-data\guidexos-user-data-s22r23-appdata-test.img` | `a07ae96fb401e7abf6405e8c8e9d944bf56ca2aa62f4e7f031947d76f5c35b7c` |

The original CPU7B2 commit objects, their old checkout Git metadata and the QAC source and runnable coordinator from D: are the outstanding losses. The S22R23 test image is stored beside the accepted source image under `E:\CodexArtifacts\S22R22\persistent-data`. [S22R22 acceptance](RELEASE_S22R22R1_PERSISTENCE_GUEST_ACCEPTANCE_CLOSURE.md) · [S22R23 acceptance](RELEASE_S22R23_APPDATA_GUEST_QUALIFICATION.md)

## Stock-kernel byte comparison

An independent byte comparison of the two preserved stock kernels reproduced the single difference at file offset `0x77C7B`. At virtual address `0x176C79` in `kernel::interrupts::init()`, CPU7B1 encodes `cmp rsi,rax` (`48 39 c6`) and CPU7B2 encodes `cmp rax,rsi` (`48 39 f0`). The following `jne` consumes only the equality flag, which has the same value for both operand orders. The historical source audit attributes the different instruction encoding to the IDT descriptor destination refactor. The fresh comparison is in `historical-stock-comparison.log`.

## QAC status and CPU7B2R1

The QAC development launcher contract passed 80 checks, ISO-only profile 7, persistent-data profile 8 and UEFI artifact isolation 36. The S22R10 launcher suite stopped after assertion 71 at the hard-coded `D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\Program.cs` read. The archived suite also identifies a multiline source regex that assumes LF at assertion 134. Point the source assertion at the restored QAC checkout and make the regex independent of Windows line endings. The managed runner can already resolve a relocated apphost via `-QacCliPath` or `GUIDEXOS_QAC_CLI`.

No usable CLI or coordinator was located for a verified current reservation. The surviving QAC state and log files under `%LOCALAPPDATA%\guideXOS\QAC` provide historical records. No guest was started during recovery. Fresh PIC/PIT, desktop, keyboard and pointer, PacMan gameplay and cleanup, and three-cycle CPL3 acceptance remain pending in a managed guest.

The next action is to restore the QAC CLI, coordinator and source checkout, then verify current coordinator status, reservation list, queue and child ownership. Requalify the hash-verified stock, CPU7B2 diagnostic and CPL3 control candidates in the ISO-only lane. For CPU7B2R1, use a separate disposable, deadline-bounded lane for controlled hardware delivery of the exact-site `#GP(0)`. Capture continuation and confirm recovery state is disarmed, the original IDTR and expected machine state are restored, and an unrelated exception follows the ordinary path. Keep LAPIC MMIO mapping and interrupt-controller activation gated on fault-entry, ownership, alias, cache-type, permissions, paging-budget and controller prerequisites.
