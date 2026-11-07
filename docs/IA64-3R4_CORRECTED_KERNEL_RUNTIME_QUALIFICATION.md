# guideXOS Server IA64-3R4 Corrected Kernel Runtime Qualification

**Date:** 2026-11-07
**Outcome:** **A — `IA64_KERNEL_EARLY_EXEC_ACCEPTED`**
**Scope:** One bounded Ski system-mode execution of the immutable IA64-3R3 `early-exec` ELF.

## Result

The corrected IA-64 kernel entered its linked `_start`, completed the bounded architecture initialization path, printed trustworthy IVA and BSP values, and stopped through the intended Ski SSC exit. Ski reported 56,609,230 instructions, zero faults, and a zero-status guest exit. The IVA readback matched this image's linked IVT at `0x100000`. The BSP readback, `0x4C6FE18`, lies inside this image's RSE backing-store range.

The register text is guest output, not an independent Ski register dump. The Ski batch interface used by the qualified runner reports aggregate instruction counts and simulator output, but does not record a CPU register snapshot or per-instruction IP trace. The corrected formatter was independently regression-tested in IA64-3R3. BSPSTORE, PSR, FPSR, RSC, and RNAT were not printed by this kernel diagnostic path, so their live values remain unobserved.

This accepts only the bounded early-execution milestone. It does not qualify a full kernel boot, UEFI, desktop, devices, scheduler, timer preemption, external interrupt delivery, interrupt handlers, or multi-CPU execution.

## Repository and evidence preservation

| Field | Value |
|---|---|
| Repository | `D:\dev\guideXOSServer_IA64` |
| Branch | `IA64_SUPPORT` |
| Starting HEAD | `96691c5b519c2377cc73dbd6bd2098f63990d831` |
| Qualification-run ending HEAD | `96691c5b519c2377cc73dbd6bd2098f63990d831` (documentation was committed after the run) |
| Upstream relation during qualification | `origin/IA64_SUPPORT`, one commit ahead and zero behind |
| Starting worktree | Existing untracked `scripts/__pycache__` and `tests/__pycache__` preserved |

No reset, clean, merge, rebase, pull, or push was performed. The candidate ELF was not rebuilt, modified, or replaced. Earlier ELF files, Ski traces, reports, and caches were preserved. No QEMU execution or QAC reservation occurred. Only this report and the status summary in `docs/IA64_SUPPORT.md` are in-scope repository documentation changes.

The run and its preflight evidence are outside the repository at:

`/home/guidex/src/ia64-3r4-corrected-kernel-runtime-20261107-codex/`

The complete runner capture is in its `capture/` subdirectory. It contains `stdout.log`, `stderr.log`, `instruction-counts.txt`, and `run-result.json`. The separate `preflight/` directory contains the phase-specific loader record. The independent IA64-3R3 diagnostic regression outputs are under `diagnostic-regression/`.

## Candidate identity and ELF-derived state

IA64-3R3's report identifies the candidate; its manifest binds the `early-exec` profile and this exact hash:

| Item | Verified value |
|---|---|
| ELF | `/home/guidex/src/ia64-3-ssc-3r3-register-diagnostic-20261107-d/bin/kernel.elf` |
| SHA-256 | `a31a529df5a6f43de699a9ef1ce02e5d102375d0c3f00ee70d478f0bf32e12da` |
| Manifest | `/home/guidex/src/ia64-3-ssc-3r3-register-diagnostic-20261107-d/ia64-artifact-manifest.json` |
| Profile | `early-exec` |
| Manifest execution authorization | `guest_execution_authorized: false` |
| ELF type and machine | ELF64 little-endian, `ET_EXEC`, `EM_IA_64=50` |
| Entry | `_start = e_entry = 0x108000` |
| Initial GP | `__gp = 0x672000` |
| Linked IVT | `ia64_ivt = 0x100000`, size `0x8000`, alignment `0x8000` |
| Memory stack | `0x4C5FE00..0x4C6FE00`, 64 KiB |
| RSE backing store | `0x4C6FE00..0x4C7FE00`, 64 KiB |

The current image's PT_LOAD layout, as independently parsed from this ELF and accepted by the runner, is:

| Segment | Address range (end exclusive) | File size | Memory size | Permissions |
|---|---:|---:|---:|---|
| 0 | `0x100000–0x3CDE70` | `0x2CDE70` | `0x2CDE70` | RX |
| 1 | `0x3CE000–0x4718B0` | `0xA38B0` | `0xA38B0` | R |
| 2 | `0x472000–0x4C82798` | `0x16130` | `0x4810798` | RW |

All three ranges are disjoint, the entry is in the executable segment, and no segment is writable and executable. The stack and RSE backing store are distinct and covered by the RW segment. The IVT is inside the executable segment and has the required 32 KiB size and alignment.

### Expected CPU state and diagnostics

These expectations come from the candidate's own symbols and startup instructions, not the separate IA64-3R3 probe:

| State | Candidate-derived expectation | Runtime evidence |
|---|---|---|
| `CR.IVA` | `0x100000`, the linked `ia64_ivt`; 32 KiB aligned | Guest printed `0x100000` |
| Entry | `_start = 0x108000` | Runner directly loaded the ELF entry; guest reached C initialization |
| GP | `__gp = 0x672000` before C code | Established by target startup disassembly; not separately read back |
| Initial RSE setup | `RSC=0` for `loadrs` and `BSPSTORE`; set `BSPSTORE=0x4C6FE00`; then `RSC=3` (eager mode) | Verified in target startup disassembly; live RSC/BSPSTORE not printed |
| `AR.BSP` | May advance as procedure calls use the RSE; expected within `[0x4C6FE00, 0x4C7FE00)` for this bounded path | Guest printed `0x4C6FE18`, inside the candidate range |
| PSR interrupt policy | Entry clears `PSR.i` and `PSR.ic`; `arch::ia64::init()` later sets `PSR.ic` only. External interrupts remain masked (`PSR.i=0`). | Static path gate and guest status message pass; no live PSR readback |
| FP state | Startup clears `PSR.dfl`/`PSR.dfh` and writes `AR.FPSR=0x0009804C0270033F` before C | Verified in target startup disassembly; FPSR not read back |

`arch::ia64::init()` emits the live `read_iva()` and `read_bsp()` values. The ELF also contains `read_bspstore()` and `read_psr()` accessors, but this diagnostic path does not call them for output. No FPSR, RSC, or RNAT diagnostic is emitted. `AR.BSP` is not `BSPSTORE`; equality is not expected after calls. The observed BSP is consistent with the configured backing-store range and the calls performed before its read.

The target disassembly at `guidexos_ski_ssc` confirms the corrected bridge in this exact candidate: it allocates an input frame, maps C ABI `r32/r33` to Ski SSC `r15/r32`, executes `break.i 0x80000`, restores `ar.pfs`, and returns through `r8`. The independent IA64-3R3 regression passed byte-exact formatter checks and known-value IVA/BSPSTORE/RSC/PSR probes. These independent probes qualify the reporting mechanism; they are not register observations from this kernel run.

## Simulator identity and loader binding

Execution used the established Ubuntu 24.04.5 LTS WSL2 x86-64 environment.

| Item | Verified value |
|---|---|
| Ski prefix | `/home/guidex/opt/ski-dfc2902` |
| Installed executable | `/home/guidex/opt/ski-dfc2902/bin/ski`, mode `0755` |
| `bskinc` | `/home/guidex/opt/ski-dfc2902/bin/bskinc`, symlink resolving to the installed `ski` |
| Installed executable SHA-256 | `ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45` |
| Qualified source | `dfc2902ea1423d9b32543a5daf8026213f2b37a1`, `v1.5.1-32-gdfc2902` (Ski 1.5.1) |
| Source tree | Clean |
| CPU configuration | `SKI_NPROC=1`, `nproc=1`; `NEW_MP` is not enabled and `-cpus` is absent |
| Mode | Direct Ski system-mode ELF loading with `-forcesystem` |

The tracked IA64-3R1 loader record remains unchanged and binds the earlier IA64-3R2 kernel hash. The runner correctly requires a loader record bound to the selected image, so this phase preserved that record and created a separate sidecar at `preflight/ia64-3r4-ski-loader-record.json`. The sidecar retains the qualified Ski source/hash, one-CPU, smoke, and direct-loader assertions; candidate-specific entry, GP, PT_LOAD, IVT, stack, and RSE metadata were derived from the exact IA64-3R3 ELF. It keeps `kernel_execution_authorized: false`; authorization was supplied separately to the runner. Sidecar SHA-256: `a7f3cc5a2d0c814b1a0f14a3d96cd28f7a9e3ab5dd7664c7765545c138e8f3d1`.

The preflight invoked the runner's own manifest, loader-record, Ski identity, and direct-ELF checks without starting a simulator process. The runner then repeated the manifest/hash, ELF, early-execution, and 12 static safety-gate validations before loading its hash-checked private snapshot with `-forcesystem`. It did not use the incompatible stock Linux bootloader.

## Safety gates

- The candidate starts with `rsm 0x6000`, masking `PSR.i` and `PSR.ic`; architecture initialization sets exception collection (`PSR.ic`) without enabling external interrupts (`PSR.i`).
- The compiled early-entry path calls architecture initialization, prints its bounded status, and calls `ski_console::stop()`. The path has no scheduler, thread creation, timer/preemption, device, storage, network, or desktop initialization calls.
- The path does not enter an unsupported exception-return flow. The static validator confirms the bounded IVT handler layout and the isolated early-entry call graph.
- The `early-exec` FP profile fixes ABI-preserved FP registers and permits scratch FP registers only in this single-context path. No asynchronous interrupt or thread-switch FP use is enabled.
- The manifest remains unauthorized. The runner's explicit `--authorize-kernel-guest-execution` flag was passed once for this candidate hash. Manifest, hash, loader, and static gates were not bypassed or weakened.

## Single authorized execution record

The exact runner invocation, from `/mnt/d/dev/guideXOSServer_IA64` in WSL, was:

```bash
prlimit --as=2147483648 --cpu=120 -- \
  python3 scripts/run-ia64-ski.py kernel \
  --artifact /home/guidex/src/ia64-3-ssc-3r3-register-diagnostic-20261107-d/bin/kernel.elf \
  --manifest /home/guidex/src/ia64-3-ssc-3r3-register-diagnostic-20261107-d/ia64-artifact-manifest.json \
  --profile early-exec \
  --expected-sha256 a31a529df5a6f43de699a9ef1ce02e5d102375d0c3f00ee70d478f0bf32e12da \
  --loader-record /home/guidex/src/ia64-3r4-corrected-kernel-runtime-20261107-codex/preflight/ia64-3r4-ski-loader-record.json \
  --ski-prefix /home/guidex/opt/ski-dfc2902 \
  --capture-dir /home/guidex/src/ia64-3r4-corrected-kernel-runtime-20261107-codex/capture \
  --timeout 60 \
  --mode system \
  --objdump /home/guidex/src/ia64-2-toolchain-shim/ia64-linux-objdump \
  --authorize-kernel-guest-execution
```

The CPU limit was 120 seconds; `--as=2147483648` set the 2 GiB address-space limit. The runner enforced its 60-second wall timeout and deterministic process-group cleanup. The actual simulator command recorded in `run-result.json` was:

```text
/home/guidex/opt/ski-dfc2902/bin/bskinc -stats -icnt /home/guidex/src/ia64-3r4-corrected-kernel-runtime-20261107-codex/capture/instruction-counts.txt -forcesystem /tmp/guidexos-ia64-ski-elf-or9q_84k/selected.elf
```

`selected.elf` was the runner's private, SHA-256-verified copy of the candidate. The runner removed it during cleanup.

### Entry and startup checkpoints

The runner does not save a per-IP trace. The checkpoints below combine the exact candidate's target disassembly with guest-originated progress messages; they are not Ski debugger register snapshots.

| Checkpoint | Evidence |
|---|---|
| ELF handoff | Direct loader sets the instruction pointer to `e_entry=0x108000`; `_start` begins with `rsm 0x6000` |
| Early setup | Disassembly loads GP `0x672000`, SP `0x4C6FE00`, configures the RSE backing store, installs FPSR, then writes `CR.IVA=0x100000` and executes `srlz.i` |
| C initialization | Guest printed `guideXOS IA-64 kernel init` after BSS clear and entry to `kernel_main` |
| Architecture diagnostics | Guest printed IVA `0x100000`, BSP `0x4C6FE18`, then `Interrupt collection enabled` and `IA-64 arch init complete` |
| Bounded exit path | Guest printed the single-entry/masked-interrupt status; the compiled path calls `stop()` using `SSC_EXIT=66`; Ski reported its successful exit |

### Complete captured guest stdout

```text
guideXOS IA-64 kernel init
  IVA = 0x100000
  BSP = 0x4C6FE18
  Interrupt collection enabled
IA-64 arch init complete
IA64-2 early-exec: SKI single-entry profile
Interrupts masked; scheduler and threads unavailable
```

The `\r\n` markers above denote the captured CRLF line endings. `capture/stdout.log` is 222 bytes with SHA-256 `8939ee701c1799bee1b6018a538660d4e603b3b3431c059b14c41d26a3799395`.

### Complete simulator stderr

```text
kernel exited with status 0
56609230 insts (0 faults), 0.71 sec, 80185544 i/s, 18870227 cycles, 3.00 ipc
```

This is simulator-generated output, not guest text. `capture/stderr.log` is 105 bytes with SHA-256 `39df4bde176707c9b0479eb069e10a667e76c7ffd64092a799a9e5ad41b52b6b`.

### Instruction, fault, and termination evidence

- Ski reported **56,609,230 instructions**, **0 faults**, 18,870,227 cycles, and 3.00 IPC. The 40-row `instruction-counts.txt` first numeric column sums to 56,609,230.
- Runner result: `classification=GUEST_EXIT_ZERO`, simulator `exit_code=0`, `timed_out=false`, elapsed wall time `0.916` seconds.
- The early path calls `stop()`, which issues Ski `SSC_EXIT=66`; Ski's own successful process status is zero. The captured exit message and the exact compiled early path jointly establish the intentional exit.
- After runner return, no Ski/bskinc process remained, and the recorded private snapshot no longer existed. The capture files remained in the phase evidence directory.
- `instruction-counts.txt` SHA-256: `c53f49bb362fca4bab4dd0af114d138b3dabc3b5024a5d6df70ad48239c5691f`.

## Runtime acceptance

| Check | Result | Evidence and limit |
|---|---|---|
| Authentic guideXOS execution | PASS | Exact authorized candidate entered C initialization and printed guest-originated messages |
| Intended early path | PASS | Architecture initialization completed, then the stop-only profile printed its bounded status |
| IVA matches linked IVT | PASS | Expected and guest-read `CR.IVA` are both `0x100000` |
| BSP is consistent with image RSE | PASS | Guest `AR.BSP=0x4C6FE18` is inside `[0x4C6FE00,0x4C7FE00)`; no `BSPSTORE` readback was emitted |
| Register formatting | PASS | Actual values are plausible and the separate IA64-3R3 byte-exact formatter regression passes |
| Interrupt/scheduling policy | PASS, path-based | Static gates and guest status show masked external interrupts and no scheduler path; live PSR was not dumped |
| Unexpected faults/vectors | PASS | Ski reports zero faults; no exception/fault output appeared |
| Expected diagnostics | PASS | IVA, BSP, interrupt-collection, architecture-complete, and early-exec messages captured |
| Deliberate `SSC_EXIT=66` | PASS | Compiled path calls `stop()`; Ski reported `kernel exited with status 0` |
| Simulator termination and cleanup | PASS | Runner returned `GUEST_EXIT_ZERO`, no timeout, process group cleaned, private snapshot removed |

## Regression verification

All commands below ran against the unchanged candidate and source. Test suites ran in Ubuntu 24.04 WSL with `PYTHONDONTWRITEBYTECODE=1` so the existing untracked Python caches were preserved.

| Check | Command / scope | Result |
|---|---|---|
| Foundation contracts | `python3 tests/ia64_foundation_contract_test.py` | PASS, 17/17 |
| Early-execution contracts | `python3 tests/ia64_early_exec_contract_test.py` | PASS, 9/9; stale hash/profile and authorization guards included |
| Ski loader contracts | `python3 tests/ia64_ski_loader_contract_test.py` | PASS, 8/8; rejection-before-launch, verified snapshot, timeout, and process-group cleanup included |
| ELF malformed-input tests | `python3 tests/ia64_elf_validator_negative_test.py --elf /home/guidex/src/ia64-3-ssc-3r3-register-diagnostic-20261107-d/bin/kernel.elf --profile early-exec` | PASS, 3/3: bad IVT size, handler boundary, and source offset rejected |
| Independent ELF/object validator | `python3 /mnt/d/dev/guideXOSServer_IA64/scripts/validate-ia64-elf.py --elf /home/guidex/src/ia64-3-ssc-3r3-register-diagnostic-20261107-d/bin/kernel.elf --object-dir /home/guidex/src/ia64-3-ssc-3r3-register-diagnostic-20261107-d/obj --repo-root /mnt/d/dev/guideXOSServer_IA64 --profile early-exec --manifest /home/guidex/src/ia64-3-ssc-3r3-register-diagnostic-20261107-d/ia64-artifact-manifest.json` | PASS; 161 IA-64 objects (95 core C++), 3 non-overlapping segments, 132 function descriptors, 64 KiB stack/RSE ranges |
| Early-execution static gates | `python3 /mnt/d/dev/guideXOSServer_IA64/scripts/validate-ia64-early-exec.py --repo-root /mnt/d/dev/guideXOSServer_IA64 --elf /home/guidex/src/ia64-3-ssc-3r3-register-diagnostic-20261107-d/bin/kernel.elf --objdump /home/guidex/src/ia64-2-toolchain-shim/ia64-linux-objdump` | PASS, 12/12 |
| IA64-3R3 register diagnostic regression | `bash tests/ia64_ski_diagnostic/run.sh /home/guidex/src/ia64-3r4-corrected-kernel-runtime-20261107-codex/diagnostic-regression` | PASS; expected legacy formatter failure reproduced, corrected bridge output and known IVA/BSPSTORE/RSC/PSR probes passed. These were independent diagnostic ELFs, not the kernel. |
| Artifact/authorization rejection | Early-exec and Ski-loader contract suites | PASS; invalid hash/profile and missing authorization rejected before guest launch |
| Mbed TLS dependency verification | `/home/guidex/bin/pwsh -NoProfile -File scripts/verify-mbedtls-profile.ps1 -RepoRoot /mnt/d/dev/guideXOSServer_IA64` | PASS; Mbed TLS 4.1.0, TF-PSA-Crypto 1.1.0, three patches |
| Bash syntax | `bash -n scripts/build-ia64.sh scripts/run-ia64_in_linux.sh tests/ia64_ski_smoke/build.sh tests/ia64_ski_diagnostic/run.sh` | PASS |
| Git whitespace checks | `git diff --check`; `git diff --cached --check` | PASS in the Windows checkout. The equivalent WSL command stalled on the mounted worktree and was interrupted; the host checks completed successfully. |

No regression test failed. The kernel runner repeated the manifest, ELF, loader, and all 12 early-execution gates immediately before the single authorized execution.

## Remaining limits and next phase

The Ski batch run did not independently capture live `BSPSTORE`, `RSC`, `PSR`, `FPSR`, or RNAT, and did not produce an IP trace. The guest's IVA/BSP text is a readback performed by the kernel using the independently qualified formatter, not a simulator-side register dump. The current milestone therefore establishes consistent IVA/BSP diagnostics, a bounded early path, and clean intentional termination; it does not establish general IA-64 runtime readiness.

Keep external interrupts masked and the scheduler disabled. A narrowly scoped follow-on can add or use a single safe Ski debugger checkpoint to independently sample PSR and RSE state, observing the RSC mode requirement for BSPSTORE/RNAT. Treat interrupt delivery, timer setup, scheduling, UEFI, devices, desktop, and multi-CPU work as separately qualified phases.
