# RX12R1 Artifact Isolation and Qualification Closeout

**Outcome A — RX12R1 corrective engineering and qualification accepted.** This outcome records successful preservation controls and fresh qualification. It does not erase RX12's historical artifact-preservation violation.

## Repository and historical state

Qualification began in `D:\dev\guideXOSServer_REXX` on branch `REXX_SUPPORT`, at the required starting HEAD `6245bedcf0ca57814fb1fc9cee6db3103d407333`. The starting worktree had the expected 28 untracked root `.obj`/`.pdb` files, existing RX12 source and evidence, no prior RX12R1 completion, and no `.phase` file. No branch, HEAD, production ESP, or production disk image was changed. The RX12 incident record and RX12 report remain unchanged in Git.

The historical record at [root-artifact-preservation-incident.md](evidence/rx12/root-artifact-preservation-incident.md) says the first RX12 MSVC sanitizer invocations ran `cl.exe` without `/Fo` and `/Fd`, so MSVC emitted object and PDB files in the repository root and overwrote several pre-existing files. The incident does not identify which of the 28 files changed or provide their pre-incident hashes. No original copy or shadow copy was available, so those original bytes are irrecoverable from current evidence. No reconstruction or restoration was attempted. RX12's source and functional test results remain valid as recorded; the original artifact-byte preservation claim remains invalid.

The exact culprit command lines and filenames cannot be recovered. The unsafe behavior is established at the command level: implicit MSVC object/PDB output went to the runner's inherited working directory. After RX12 added `/Fo` and `/Fd`, its sanitizer runner still lacked a fail-closed path ownership check and explicit linker `/PDB` output. The hosted runner passed explicit GCC `-o` paths but did not isolate its working directory or temporary-file environment. The kernel Makefile used the shared `kernel/build/$(ARCH)` tree, and its `clean` target could remove that shared tree.

## Baseline and final preservation result

Before any RX12R1 build or test, a post-incident baseline was captured at `2026-10-18T00:48:47Z`. It contains all 31 root `.obj`/`.pdb` files: the 28 protected untracked files and three tracked files. The JSON and CSV record relative path, type, size, SHA-256, tracked state, Git status, and timestamp. These hashes are explicitly a **current post-incident baseline**, not recovered pre-RX12 values.

- Baseline: [JSON](evidence/rx12r1/protected-artifacts-baseline.json), [CSV](evidence/rx12r1/protected-artifacts-baseline.csv)
- Final comparison: [JSON](evidence/rx12r1/protected-artifacts-final-comparison.json), [CSV](evidence/rx12r1/protected-artifacts-final-comparison.csv), [human report](evidence/rx12r1/protected-artifacts-final-comparison.txt)
- Independent final guard report: [JSON](evidence/rx12r1/protected-artifacts-final-guard.json), [human report](evidence/rx12r1/protected-artifacts-final-guard.txt)

The final comparison at `2026-10-18T01:18:02Z` matched all 31/31 baseline paths and SHA-256 values. All 28/28 protected untracked files remain present with identical size and hash; no root `.obj`/`.pdb` path was missing, modified, or unexpected. The guard never restored or deleted files. Intermediate checks after hosted builds, sanitizer builds/tests/corpus runs, native build, and QEMU also matched 31/31.

## Output audit and isolation design

RX12R1 owns new outputs only below `build/rexx/rx12r1/`, a narrow `.gitignore` addition. Each concurrent-capable run uses a fresh GUID-named child directory. `tests/rexx_artifact_guard.psm1` validates the new run-root location, rejects an existing run root, requires the compiler working directory to be inside it, validates every planned output path, and hashes the baseline before execution. Its post-run check reports missing, modified, and unexpected root compiler artifacts, writes JSON and text reports, returns failure on a mismatch, and never repairs files.

The audited output map is:

| Flow | Owned output paths and controls |
| --- | --- |
| Hosted regressions | `build/rexx/rx12r1/hosted-<id>/bin` for explicit GCC/G++ `-o` outputs; sibling `temp` is the working directory and receives `TEMP`, `TMP`, and `TMPDIR`. Test scripts, logs, and preservation reports stay under the same run root or the RX12R1 evidence directory. |
| ASan and UBSan | Separate `sanitizer-address-<id>` and `sanitizer-undefined-<id>` roots. Each test plan has its own output directory. MSVC uses explicit `/Fo`, `/Fd`, `/Fe`, `/link /INCREMENTAL:NO`, and `/PDB`; compiler and linker symbols, objects, executables, temp files, test logs, corpus results, and reports remain inside that sanitizer run. |
| Differential corpus | The harness compiles with explicit `-o` and runs its compiler from the caller-owned output directory. Per-case results and output hashes are recorded beneath each sanitizer run. |
| Native AMD64 kernel | `kernel/Makefile` now accepts `BUILD_ROOT` while retaining `build` as the default. RX12R1 passed a unique `BUILD_ROOT=../build/rexx/rx12r1/native-amd64-<id>` to a forced `all` build, used explicit makefile object and link targets, and did not call `clean`. The pre-build source/output audit enumerated 655 source mappings and 1,310 possible output paths, including dependency files, all Mbed TLS sources, all nine REXX C objects, and both kernel images. Full map: [native output audit](evidence/rx12r1/native-build/output-audit-5b7e16afb07c4c4a8fe06df33e199d12.json). |
| QEMU | A unique `qemu-<id>` run root holds copied disposable ESP and firmware, the staged kernel, private QMP port, serial and QEMU logs, temporary files, raw captures, and screenshots. Guest networking was disabled. No production media was used. |

The source change to the differential harness also keeps its compiler working directory inside the caller-owned output directory. The qualification scripts require `REXX_SUPPORT`; they do not pin the starting HEAD so the committed runners remain usable on later commits on that branch. The required starting HEAD was checked before edits and is recorded above and in the baseline manifest.

One audit probe deserves explicit mention: a `mingw32-make -n -B` dry-run unexpectedly launched compiler commands. It was interrupted and confined to the isolated `build/rexx/rx12r1/native-audit-dryrun` directory; it did not write to the repository root or the shared kernel build tree. The accepted native build used the audited source/output map and a separate unique output root. An independent shared-kernel build was observed during qualification; RX12R1 did not stop it or direct outputs into its tree.

## Guard self-test

The reusable guard's synthetic self-test used a disposable temporary `protected.obj`, never a repository artifact. It confirmed that an explicit output targeting the protected fixture is rejected, an unsafe implicit-output working directory is rejected, and a post-run byte mutation produces a nonzero failure. Results: [JSON](evidence/rx12r1/guard-synthetic-selftest.json) and [human report](evidence/rx12r1/guard-synthetic-selftest.txt).

## Fresh hosted qualification

The isolated hosted runner completed successfully. Fresh results include:

- RX12 built-in C assertions: **320 passed, 0 failed**; RX12 REXX fixtures: **30/30**.
- Hosted CLI cases: **20/20**.
- Inherited fixtures: RX3 **13/13**, RX4 **11/11**, RX5 **13/13**, RX6 **25/25**, RX7 **33/33**, RX8 **26/26**, RX9 **24/24**, and RX10 INTERPRET **19/19**.
- RX2 hosted integration, numeric, and shell bridge assertions: **45**, **18**, and **7** passed. The runner also passed the RX3–RX10 decimal, allocation-failure, routine, condition, and interpreter C assertions recorded in the raw log.
- The 108-case local compatibility corpus completed with expected local exit results in each sanitizer run; its independent Regina classifications remain separate.

The current hosted runner includes ARG omission and option errors, dynamic INTERPRET with the new built-ins, syntax-condition recovery, allocation-failure cases, nested routines and EXPOSE, RX11's INTERPRET lifetime regression, and bounded-resource cases. Raw record: [hosted-run.raw.log](evidence/rx12r1/hosted-run.raw.log). Per-stage preservation reports are under `evidence/rx12r1/preservation/`.

## ASan, UBSan, and Regina

Both sanitizer families were freshly built and executed in separate isolated run directories. MSVC AddressSanitizer used compiler version 19.51.36260; Clang UBSan used clang-cl 22.1.8. Each family built and passed **14/14 hosted C executables**, including the RX12 built-in and allocation-failure suites, and ran all **108 local compatibility cases** with their expected exit behavior. All captured build/run statuses were zero, the sanitizer finding-pattern scan found no reports, and preservation checks matched 31/31 after builds, hosted tests, corpus execution, and finalization.

Regina was not available on PATH or at a supplied executable path. No software was installed. The 108 reference comparisons are recorded `NOT_RUN`; no independent compatibility success is claimed. Raw output and per-target status logs are in [sanitizers-run.raw.log](evidence/rx12r1/sanitizers-run.raw.log), the [ASan evidence directory](evidence/rx12r1/sanitizer-address-412222499eef4501aab73cf5be1dc255/), and the [UBSan evidence directory](evidence/rx12r1/sanitizer-undefined-cdf8921ce9434813bab14497ced0e31f/). The case-level results files also retain all local stdout/stderr hashes and expected exit outcomes.

## Native AMD64 and guest qualification

The full freestanding AMD64 build and link passed with exit code 0. It compiled all nine REXX C translation units and preserved the project's freestanding flags (`-ffreestanding`, `-nostdlib`, `-nostdinc++`, `-fno-builtin`, `-fno-exceptions`, `-fno-rtti`). The fresh `kernel.elf` SHA-256 is `ce835ef3b77aa066db46f9cb9964b6bb0d7c589b4202a2b1965129854fd80dee`, independently re-read and equal to the staged guest kernel hash. It matches the historical RX12 hash. Link evidence: [native-link-evidence](evidence/rx12r1/native-build/native-link-evidence-5b7e16afb07c4c4a8fe06df33e199d12.json); raw build output: [stdout](evidence/rx12r1/native-build/native-build-5b7e16afb07c4c4a8fe06df33e199d12.stdout.log), [stderr](evidence/rx12r1/native-build/native-build-5b7e16afb07c4c4a8fe06df33e199d12.stderr.log), and [status](evidence/rx12r1/native-build/native-build-5b7e16afb07c4c4a8fe06df33e199d12.status.json).

QEMU 11.0.0 booted the fresh kernel from copied disposable media with `-net none`. The guest ran the string built-ins, ARG omissions, trapped SYNTAX and recovery, output-resource rejection and post-error recovery, then `EXIT 23` followed by `echo $?`, which printed `23`. The serial log recorded all six REXX script opens and 43 heartbeats after the suite, up from one before it. The matching QMP shutdown acknowledgement is retained. A process census captured three QEMU processes before launch and two after; commands and QMP shutdown were directed only to the RX12-owned PID and port recorded in the invocation. No foreign QEMU was targeted.

Fresh screenshots show [string built-ins](evidence/rx12r1/native-guest/run-9936ea4ad4744f05b24b8d5855529793/rx12r1-01-string-builtins.png), [ARG omissions](evidence/rx12r1/native-guest/run-9936ea4ad4744f05b24b8d5855529793/rx12r1-02-arg-omissions.png), [SYNTAX recovery](evidence/rx12r1/native-guest/run-9936ea4ad4744f05b24b8d5855529793/rx12r1-03-syntax-recovery.png), [resource rejection](evidence/rx12r1/native-guest/run-9936ea4ad4744f05b24b8d5855529793/rx12r1-04-resource-recovery.png), [post-error recovery](evidence/rx12r1/native-guest/run-9936ea4ad4744f05b24b8d5855529793/rx12r1-05-post-error-command.png), [script exit](evidence/rx12r1/native-guest/run-9936ea4ad4744f05b24b8d5855529793/rx12r1-06-exit-status-script.png), and [status 23](evidence/rx12r1/native-guest/run-9936ea4ad4744f05b24b8d5855529793/rx12r1-07-exit-status-check.png). The [serial log](evidence/rx12r1/native-guest/run-9936ea4ad4744f05b24b8d5855529793/serial.log), [Console transcript](evidence/rx12r1/native-guest/run-9936ea4ad4744f05b24b8d5855529793/console-transcript.txt), [QEMU result](evidence/rx12r1/native-guest/run-9936ea4ad4744f05b24b8d5855529793/qemu-smoke-result.json), invocation, censuses, and QMP shutdown receipt are retained under the same run directory. The first post-fix attempt exposed that a Start-menu sequence had not opened the Console; it was excluded from acceptance. The runner now opens the Console through the verified terminal hotkey, and the accepted captures visibly contain the expected result markers and shell status.

## Status, limits, and next scope

RX12 remains a functionally qualified implementation with its original preservation incident intact. RX12R1 adds build isolation and verifies the current files remain unchanged; it cannot recover or certify the unknown pre-RX12 bytes. Regina comparison, Unicode/DBCS character semantics, and the unimplemented classic/SAA built-in inventory remain outside this closeout. No REXX runtime source was changed.

For RX13, retain RX12's bounded pure-function direction: verify the standard function set and argument rules against the IBM/ANSI references, then consider DATATYPE, LEFT/RIGHT, LASTPOS, SUBWORD, WORDLENGTH/WORDINDEX, DELSTR, and DELWORD with independent Regina comparisons when an approved interpreter is available. Do not start that work as part of RX12R1.

The RX12R1 changes and evidence are committed locally on `REXX_SUPPORT`; no push was made.
