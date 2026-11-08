# IA64-4A CPU-State and RSE Checkpoint Qualification

**Outcome B — `IA64_CPU_STATE_PARTIAL` (2026-11-07).** Static qualification passed and one authorized Ski run reached the intentional SSC exit with zero simulator faults. The run's raw PSR values are architecturally consistent with masked external interrupts and enabled interruption collection. The executed image printed the PSR `i` and `ic` fields with their bit positions reversed, so its checkpoint transcript did not pass strict record validation. The reporting code and validator are corrected in the final static candidate, but the one-run limit means that image was not executed.

## Scope and preserved baseline

- Repository: `D:\dev\guideXOSServer_IA64`; branch: `IA64_SUPPORT`; starting HEAD: `411e45ea2d324e77b3ba12a1a88993090c6ad583`.
- The accepted IA64-3R4 baseline image remains at `/home/guidex/src/ia64-3-ssc-3r3-register-diagnostic-20261107-d/bin/kernel.elf` with SHA-256 `a31a529df5a6f43de699a9ef1ce02e5d102375d0c3f00ee70d478f0bf32e12da`.
- No push, reset, clean, or QEMU execution was performed. Existing untracked Python caches and prior build/capture artifacts were preserved.
- Toolchain used: pinned GCC 13.5.0, Binutils 2.41, and Ski 1.5.1. Ski source commit is `dfc2902ea1423d9b32543a5daf8026213f2b37a1`; installed `ski` SHA-256 is `ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45`.

The first checkpoint image that was run is Candidate C, SHA-256 `397fbe07c41a41da70011c0be1ce7578b1d40d3c463f98a240fcbb7c9333ae8d`, built at `/home/guidex/src/ia64-4a-cpu-state-checkpoints-20261107-c/`. After the PSR field-label defect was identified, Candidate D was built with the corrected reporter and strict validator. Candidate D SHA-256 is `49f0ea7b0d68390916cf6ab7b7db3561825da34aeb45e8b8efd8e8ab01106af5`; it passed static qualification and remains unauthorized and unexecuted. Candidate C and D outputs are separate; neither overwrote the 3R4 baseline.

## Architectural access audit

The audit uses Intel's [Itanium Architecture Software Developer's Manual, Volume 2, Revision 2.3](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/itanium-architecture-software-developer-rev-2-3-vol-2-manual.pdf), especially the PSR field table, IVA description, and §6.5 RSE control, plus the [Itanium Software Conventions and Runtime Architecture Guide, document 245358-003](https://download.intel.com/design/Itanium/Downloads/245358.pdf), especially application-register and procedure-call conventions.

| State | Access and constraints | Checkpoint treatment |
|---|---|---|
| `CR.IVA` | A system control register, accessible at privilege level 0. IVA is a 32 KiB table base; its low 15 address bits read as zero. A write must be followed by `srlz.i` before dependent use. | Written in startup, serialized, and read only in C/D. Compared with the candidate's linked IVT base. |
| `PSR` | System state read at the privileged entry. The relevant fields are `PSR.ic` bit 13 (interruption collection) and `PSR.i` bit 14 (external interrupt enable). `PSR.i=0` masks external interrupts; `PSR.ic=1` allows interruption state collection. | A/B observe both clear. `arch::init()` sets only `PSR.ic`; C/D should observe `ic=1`, `i=0`. C/D/expected values were checked against raw PSR and field text. |
| `AR.RSC` | Application register for RSE mode. Mode `00` is enforced-lazy; `11` is eager. `loadrs`, `BSPSTORE`, and `RNAT` require mode `00`; no diagnostic changes mode inside C/D readers. | Captured at all stages: A is inherited, B is enforced-lazy, C/D are configured eager mode 3. |
| `AR.BSP` | Read-only pointer to the top of the in-memory register stack. It is readable in all RSE modes and does not change RSE state. BSP can advance through procedure frames and NaT collections, so it need not equal BSPSTORE. | Captured A–D. B/C/D are checked against the candidate RSE range, not for exact equality with BSPSTORE. |
| `AR.BSPSTORE` | Next RSE store pointer. Reads and writes are legal only with `RSC.mode=0`; other modes fault. Writing it resets the clean partition and makes `RNAT` undefined, while BSP reflects the preserved dirty partition and intervening NaT collections. | Read only at B, immediately after startup initializes it in mode 0. It is compared with the candidate's linked backing-store base. Not read in A/C/D. |
| `AR.RNAT` | NaT collection register, accessible only with `RSC.mode=0`. Its contents become undefined after `loadrs`, a BSPSTORE write, or certain RSE spills; only a subset of bits may otherwise be defined. | Omitted from the kernel checkpoints and reported `UNVERIFIED`. No value is inferred from the standalone diagnostic. |
| `AR.FPSR` | Floating-point status/control state. Reading it does not change FP ownership. Conventions constrain status fields at call boundaries; procedures must preserve control fields unless changing them is their purpose. | Read at all checkpoints. Startup installs the early-exec FPSR before C; C/D compare with `0x0009804C0270033F`. A/B retain inherited values. |
| `AR.PFS` | Previous function state; calls/`alloc` overwrite it, so a normal procedure must save and restore it as required by the ABI. | C/D readers are hand-written leaf assembly, use caller-saved `r2/r3/r8`, do not execute `alloc` or call, and do not write PFS. PFS is observed read-only. |
| `AR.UNAT` | Preserved NaT collection state for `st8.spill`; conventions require preservation when spill operations use it. | C/D readers only read it and contain no spill operations. The observed values are diagnostic; the reader does not modify it. |

The system-register privilege requirements, PSR bit positions, IVA alignment, RSE mode restrictions, BSP/BSPSTORE distinction, and RNAT invalidation are architectural requirements. The guest's values are self-reported reads formatted by the kernel; they are not independent Ski debugger dumps.

## Checkpoint implementation and image layout

Four fixed 96-byte records are aligned to 16 bytes and placed contiguously in initialized writable `.data.cpu_state_checkpoints`, outside the early BSS clear. Candidate D symbols are:

| Record | Address | Size |
|---|---:|---:|
| A | `0x4890C0` | 96 bytes |
| B | `0x489120` | 96 bytes |
| C | `0x489180` | 96 bytes |
| D | `0x4891E0` | 96 bytes |

The assembly fills A/B inline in `_start`; it adds no call in the RSE transition window. C/D readers do not allocate a stacked frame or modify the active PFS. Records store validity masks so mode-restricted or architecturally undefined fields remain `UNVERIFIED`. Reporting happens after normal startup state is established and immediately before the existing stop path.

Both candidate ELFs have the same relevant linked geometry:

| Item | Linked value |
|---|---|
| Entry / `_start` | `0x108000` |
| `__gp` | `0x673000` |
| IVT | `[0x100000, 0x108000)`, 32 KiB aligned |
| Stack | `[0x4C60E00, 0x4C70E00)`, 64 KiB |
| RSE backing store | `[0x4C70E00, 0x4C80E00)`, 64 KiB |
| Checkpoint data | `[0x4890C0, 0x489240)`, 384 bytes |

The RSE and checkpoint areas are within writable, non-executable load memory. The stack and RSE store are adjacent and disjoint. BSP equality with BSPSTORE is not an acceptance condition.

## Build and static qualification

Candidate D was built with the pinned toolchain in an isolated directory:

```bash
IA64_FP_PROFILE=early-exec \
IA64_BUILD_DIR=/home/guidex/src/ia64-4a-cpu-state-checkpoints-20261107-d \
IA64_TOOLCHAIN_PREFIX=/home/guidex/src/ia64-2-toolchain-shim/ia64-linux \
IA64_RUNTIME_LIBGCC=/home/guidex/src/gcc-13.5.0-build/ia64-linux-gnu/libgcc/libgcc.a \
IA64_JOBS=4 bash scripts/build-ia64.sh
```

The final manifest binds Candidate D's SHA-256 and records `static_gates=PASS`, `cpu_state_static_gates=PASS`, `guest_execution_authorized=false`, `runtime_qualified=false`, `scheduler_qualified=false`, and `interrupt_fp_preservation=false`.

- Independent ELF/object/profile validation: PASS; 163 target objects, 95 core C++ objects, 3 non-overlapping load segments, 132 function descriptors, 64 KiB stack and RSE ranges, no unresolved symbols or remaining relocations.
- Existing early-exec disassembly gates: PASS, 12/12.
- CPU-state source, ELF storage, symbol, mode, stack/RSE, and target-disassembly gates: PASS, 14/14.
- Target disassembly confirms checkpoint A/B placement, BSPSTORE access only in lazy mode, no RNAT access in the kernel, and C/D leaves without a frame allocation or PFS/UNAT writes.
- No scheduler, thread switching, external interrupt enable, device initialization, exception-resumption, or desktop path is reachable from the bounded entry.

Candidate C had also passed these static gates before its one authorized execution. Candidate D is the final source-corrected static artifact and was not executed.

## One authorized Ski execution

Preflight verified the Candidate C manifest and SHA-256, candidate-bound loader sidecar, Ski executable hash and symlinks, direct system-mode ELF ranges, one-CPU source contract, and static gates. Ski's pinned source tree was clean at commit `dfc2902ea1423d9b32543a5daf8026213f2b37a1`; `SKI_NPROC` is 1, `nproc` initializes to 1, `NEW_MP` is not enabled, and the installed binary contains no conditional `-cpus` option. The loader record remained unauthorized; kernel execution was authorized only through the runner flag.

The exact one-time invocation was:

```bash
prlimit --as=2147483648 --cpu=120 -- \
  python3 -B scripts/run-ia64-ski.py kernel \
  --artifact /home/guidex/src/ia64-4a-cpu-state-checkpoints-20261107-c/bin/kernel.elf \
  --manifest /home/guidex/src/ia64-4a-cpu-state-checkpoints-20261107-c/ia64-artifact-manifest.json \
  --profile early-exec \
  --expected-sha256 397fbe07c41a41da70011c0be1ce7578b1d40d3c463f98a240fcbb7c9333ae8d \
  --loader-record /home/guidex/src/ia64-4a-cpu-state-checkpoints-20261107-c/preflight/ia64-4a-ski-loader-record.json \
  --ski-prefix /home/guidex/opt/ski-dfc2902 \
  --capture-dir /home/guidex/src/ia64-4a-cpu-state-checkpoints-20261107-c/kernel-capture \
  --timeout 60 \
  --mode system \
  --objdump /home/guidex/src/ia64-2-toolchain-shim/ia64-linux-objdump \
  --authorize-kernel-guest-execution
```

There was one kernel launch. The simulator returned exit code 0, reported `kernel exited with status 0`, counted **56,648,994 instructions** and **0 faults**, and completed without the 60-second timeout. Runner elapsed time was 1.317 seconds; Ski's own summary reported 0.64 seconds. The bounded guest printed the architecture-init messages and reached the established `SSC_EXIT=66` stop path. The runner classified the result `CPU_STATE_CHECKPOINT_FAIL` because the record text used the wrong PSR field labels; it returned failure for qualification even though Ski exited normally. The raw capture is preserved at `/home/guidex/src/ia64-4a-cpu-state-checkpoints-20261107-c/kernel-capture/`.

### Captured values and PSR reporting defect

The run's raw checkpoint observations were:

| Stage | PSR raw | RSC | BSP | BSPSTORE | FPSR | IVA | PFS / UNAT |
|---|---:|---:|---:|---:|---:|---:|---|
| A | `0x0` | `0x0` | `0x0` | UNVERIFIED | `0x0` | UNVERIFIED | UNVERIFIED / UNVERIFIED |
| B | `0x0` | `0x0` | `0x4C70E00` | `0x4C70E00` | `0x0` | UNVERIFIED | UNVERIFIED / UNVERIFIED |
| C | `0x2000` | `0x3` | `0x4C70E18` | UNVERIFIED | `0x9804C0270033F` | `0x100000` | `0x184` / `0x0` |
| D | `0x2000` | `0x3` | `0x4C70E40` | UNVERIFIED | `0x9804C0270033F` | `0x100000` | `0x285` / `0x0` |

Candidate C's reporter decoded bit 13 as `PSR.i` and bit 14 as `PSR.ic`, the reverse of Intel's field table. Thus its text says `PSR.i=1 PSR.ic=0` at C/D. The captured raw value `0x2000` has bit 13 set and bit 14 clear: architecturally, `PSR.ic=1` and `PSR.i=0`. This agrees with the code path, which enables collection in `arch::init()` and leaves external interrupts masked. The raw PSR value is consistent, but the exact executed candidate's mislabeled record fails strict validation. The reporter and validator bit mapping were corrected in source and Candidate D passed static gates; the transcript defect remains a qualification limit for the executed Candidate C.

The corrected strict parser was applied to the preserved Candidate C stdout without launching Ski again. It accepted A/B, rejected the C/D `PSR.i` text as inconsistent with raw `PSR=0x2000`, and reported C/D missing after rejecting those malformed records. This post-run analysis preserves the original `run-result.json` and does not convert the Candidate C transcript into a pass.

RSC mode, BSP, FPSR, and IVA are otherwise consistent with the candidate's setup. BSP advances from `0x4C70E18` at C to `0x4C70E40` at D while remaining inside the linked RSE range. BSPSTORE is verified at B only; RNAT is unverified throughout. No independent Ski register dump or IP trace was captured. No unexpected vector appeared in stdout/stderr, but vector delivery was not independently sampled.

## Regression and independent diagnostics

Final-source regression results:

| Suite / check | Result |
|---|---|
| IA-64 foundation contracts | PASS, 17/17 |
| Early-exec contracts | PASS, 9/9, including authorization and stale-manifest rejection |
| Ski loader contracts | PASS, 8/8, including rejection-before-launch and process-group timeout cleanup |
| CPU-state checkpoint tests | PASS, 6/6, including malformed output, wrong identity/ranges, prohibited reads, and invalid mode transitions |
| ELF malformed-input tests | PASS, 3/3 |
| Mbed TLS profile verification | PASS: Mbed TLS 4.1.0, TF-PSA-Crypto 1.1.0, 3 patches |
| IA-64 FP code-generation regression | PASS using pinned GCC 13.5.0; report at `/home/guidex/src/ia64-4a-cpu-state-checkpoints-20261107-c/fp-codegen/report.md` |
| Standalone Ski register/SSC diagnostics | PASS; corrected formatter, FPSR readback, IVA/BSPSTORE/RSC/PSR probe, SSC GETCHAR. RNAT reported UNVERIFIED. These were separate diagnostic ELFs, not the kernel. |
| Shell syntax and Git whitespace checks | PASS |

The five Python suites total 43 passing tests. The negative cases that intentionally print rejected manifest values are expected test fixtures, not suite failures.

## Qualification limits and next phase

Outcome B is retained because the only executed candidate did not produce PSR field text accepted by the strict checkpoint validator. Candidate D corrects that defect and is statically qualified, but it has no guest-runtime evidence. The user-authorized run limit was honored; no second kernel execution was attempted.

The following remain unqualified: independent live-register sampling, interruption-vector observation, asynchronous interrupt delivery, exception recovery/resumption, FP state preservation across interrupts, RSE context switching, scheduler/thread activation, multiprocessor operation, UEFI handoff, devices, and the full kernel runtime. RNAT remains `UNVERIFIED` by design. A future phase would need its own authorization before executing Candidate D.
