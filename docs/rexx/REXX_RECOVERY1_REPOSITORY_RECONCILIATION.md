# REXX RECOVERY1 — Repository Reconciliation

**Assessment date:** 2026-11-27
**Repository:** `guideX/guideXOS-Server`
**Checkout:** `E:\dev\guideXOSServer_REXX`
**Recovery outcome:** **C — Partial recovery**

## Executive assessment

The intact `REXX_SUPPORT` checkout restores the RX38 interpreter and its Git history from GitHub. The separate RX39/RX40 Server integration checkout, its source commits, and its qualification reports were local-only and are not present in the inspected GitHub refs, current local repositories, bundles, or available object databases. Archived Codex task records preserve commit identifiers and summaries, but not the Git objects needed to restore that branch. This recovery did not recreate or import integration source.

REXX-only work can resume from RX38 on `REXX_SUPPORT`. A Server integration must be reconstructed and reviewed against the then-current `origin/main` after the mainline S22R29 work in the separate `E:\dev\guideXOSServer` checkout has settled. That checkout contains uncommitted S22R29 work and was left untouched.

## Initial checkout and preservation

At the start of recovery, the working directory was `E:\dev\guideXOSServer_REXX`, on branch `REXX_SUPPORT`, at `911abeea1dcecbbc6af01e7f4c54227723265f6d`. The index and working tree were clean; there were zero untracked paths, and the branch was even with its configured `origin/REXX_SUPPORT` tracking ref. The remote is the `guideX/guideXOS-Server` repository. Before fetching, local recovery refs under `refs/recovery1/pre-fetch/*` were created to pin every then-present local branch, remote-tracking ref, and tag.

The initial clone contained one local branch, 25 remote-tracking branches plus `origin/HEAD -> origin/main`, the `v0.2` tag, and clone-only reflogs. The starting commit resolved as a valid commit object. The E: volume had about 5.23 TB free and C: about 112.84 GB free; D: was not present as a mounted drive. The historical paths `D:\dev\guideXOSServer_REXX`, `D:\dev\guideXOSServer_RX39_Integration`, and `D:\dev\guideXOSServer` were absent.

A non-pruning fetch from GitHub completed successfully. GitHub identified the public repository as `guideX/guideXOS-Server`, with `main` as its default branch. The fetched `origin/REXX_SUPPORT` remains at `911abeea1dcecbbc6af01e7f4c54227723265f6d`; fetched `origin/main` is `02422a86d44319e6e6473342af7e8b1e29c55ff9` (`S22R28R3R3: qualify corrected RAMDISK mapping`). The REXX commit is not an ancestor of `main`; their merge base is `c6354904ca2287af2c38de0bc0f7aba2063158e2`. The current main tree has no REXX source paths, so the two histories remain separate.

## Recovered REXX source and evidence

The current branch contains the RX38 interpreter, scorecard, documentation, and qualification record. The RX38 scorecard records 75 built-ins: 61 implemented, 7 not implemented, 5 partial, 1 not assessed, and 1 not implemented by design. It documents core parsing and execution; numeric, string, and binary operations; `VALUE`, `RANDOM`, `EXTERNALS`, `ADDRESS KERNEL`; the VM-owned external data stack; queue operations; optional input and stream providers; bounded stream state; output capability metadata; and resource limits. Production input/stream services and several host-dependent built-ins remain explicitly limited.

RX38 records that production source is unchanged from RX37. Comparing `4906677df9818f6c241d4c233bde1a65e68f5741..911abeea1dcecbbc6af01e7f4c54227723265f6d` showed documentation/evidence changes only, with no C, header, assembler, or REXX source changes. Existing documentation records the RX37 hosted suite as passing, including 4,203 output capability assertions; it also records older RX30 evidence of 181,417 assertions, an RX37 ASan/UBSan pass (36/36), and a historical RX37 freestanding AMD64 source-current pass. These are inherited records, not runs performed in this recovery. RX31 injected input and RX37 native output capability remain open; Regina is recorded as not run.

## Missing RX39/RX40 integration history

The requested integration branch `codex/rx39-server-integration` is absent from the GitHub branch listing. Exact-commit lookups on GitHub did not find its reported commits, and the branch/ref was not found in the inspected local clones or verified all-refs bundles. Full strict `git fsck` checks on the primary REXX checkout, the main checkout, `SQL_RECOVERY_R3BM`, and `TASKBAR_INTEGRATION3` found no unreachable objects or repository errors. Searches of the available Codex worktree directories found no surviving Git checkout. The old D: paths are unavailable.

Archived Codex task records document these integration commits, but their objects are unavailable in this recovery:

| Recorded purpose | Recorded commit | Recovery status |
| --- | --- | --- |
| RX39R3 source work | `e15e220314f884e28d79a6ad0c9f406ba908628c` | Transcript only; object unavailable |
| RX39R3 final preservation HEAD | `d9d473675e2f74128bc99e2945475da45404b021` | Transcript only; object unavailable |
| RX39R6 exact `ADDRESS KERNEL` allowlist update | `9f75b934013c4ab0ad7478d457535672dd496593` | Transcript only; object unavailable |
| RX39R7 qualification report | `3c4e18ed34d53b98cde1be39fdfbc465da97c392` | Transcript only; object unavailable |
| RX40R2 integration source checkpoint | `96deb004a947fd96a993e0806b1fea75d1d886b8` | Transcript only; object unavailable |
| RX40R2 report-only commit | `914bf993184983ca118cb018f3d573a206606d77` | Transcript only; object unavailable |
| RX40R2R1 reconciliation HEAD | `8cbc4d4af9249f3126da2c047bfb3df7679b6864` | Transcript only; object unavailable |

The archived records state that RX39R6 changed only `kernel/shell/rexx_shell_cmd.cpp`, adding an exact, lowercase `ADDRESS KERNEL` allowlist check, after the RX39R3 interrupt ABI/IST repair. They also state that RX40R2 committed nine report/evidence files without importing source, merging, building, launching a guest, running QAC, or pushing. RX40R2R1 likewise did not launch a guest because QAC was reserved. These records help identify likely review topics but are not source recovery. No integration source was reconstructed in this task.

The RX39R7 transcript records three successful QAC guest runs for ISO SHA-256 `a0dd72417f2abcdeb5af9c89485577deba684d815303b60857cababcf21b8291`. The corresponding ISO/binary was not recovered here. This historical acceptance applies only to the exact artifact recorded in that transcript and must not be transferred to a reconstructed or new candidate.

## Mainline prerequisite and recommended continuation

The current `origin/main` commit `02422a86d44319e6e6473342af7e8b1e29c55ff9` includes `docs/RELEASE_S22R28R3R3_CORRECTED_RAMDISK_GUEST_ACCEPTANCE.md`. That report records successful QAC-managed CPL3-off/on guest acceptance for its RAMDISK alias and LBA0 fingerprint, FAT32 root and application paths, desktop/user input, three CPL3 task cycles, and clean desktop shutdown. It also says those CPL3 cycles did not fault or preempt, and leaves fault recovery, timer-driven preemption, forced termination, and promotion/publication outside acceptance. It recommends S22R29 recoverable CPL3 fault handling as the next mainline milestone. This newer R3R3 acceptance supersedes the older RX40R2R1 root-mount comparison as a prerequisite; it does not establish acceptance for any RX39/RX40 integration candidate.

The separate `E:\dev\guideXOSServer` checkout has uncommitted S22R29 changes, including kernel interrupt and protected-task files plus an untracked AMD64 exception-contract header. It was inspected only and left unchanged. After that work settles, the smallest useful continuation is a source-grounded RX39/RX40 reconstruction/reconciliation against the then-current mainline, starting from this recovered RX38 source and reviewing the shared AMD64 interrupt ABI/IST, the `ADDRESS KERNEL` allowlist, build-profile contracts, and current CPL3/memory behavior. Any new candidate needs fresh hosted/freestanding checks and candidate-specific qualification. Do not reuse the RX39R7 ISO result.

## Work performed in this recovery

- Preserved the pre-fetch refs under `refs/recovery1/pre-fetch/*` and fetched GitHub without pruning.
- Inspected repository refs, available clones, all-refs bundles, object databases, archived task records, and current mainline acceptance documentation.
- Did not run tests, builds, QAC, or a native guest. Existing RX37/RX39 records above are historical evidence only.
- Did not edit or clean any other working tree and did not recreate the historical 145,400 untracked paths or 31 protected root object/debug files reported for the old D: workspace.
- The only recovery artifact recreated here is this reconciliation report, based on the intact RX38 checkout, live GitHub ref state, available evidence, and archived task-record summaries. It does not restore missing integration commits or source files.

**Conclusion:** Outcome **C — Partial recovery**. The REXX interpreter branch and its history are intact and available for REXX-only development. The separate Server integration source/history and its report commits were not recoverable as Git objects from the inspected locations. Integration work must resume as a new, reviewed effort after current mainline work settles.

