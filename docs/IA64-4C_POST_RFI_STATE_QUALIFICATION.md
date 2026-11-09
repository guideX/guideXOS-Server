# IA64-4C Direct Post-RFI State Qualification

**Status (2026-11-08): Outcome A — `IA64_POST_RFI_STATE_VERIFIED`.** Candidate 4C ran once in the qualified Ski 1.5.1 system-mode environment. Both controlled Break returns produced a direct PSR capture before cleanup. Each capture was `0x2000` (`PSR.i=0`, `PSR.ic=1`); checkpoints A–D, exception records, expected fault attribution, and intentional SSC exit all passed strict postflight validation.

This qualifies only the two bounded Break Instruction sites in the Ski early-exec profile. It does not qualify generalized exception handling, interrupts, scheduling, user mode, UEFI, or hypervisor execution.

## Baseline and preserved evidence

- Repository: `D:\dev\guideXOSServer_IA64`; branch `IA64_SUPPORT`.
- Starting HEAD: `a953b6c15427dad56c2f6878f613969040f1cce5`.
- Starting upstream: `origin/IA64_SUPPORT`; the starting branch and upstream were even.
- Candidate E from IA64-4BR1 remains unchanged at SHA-256 `3f6fa1c6c21bb44555621a6540664e82bf6f68aa9d472d29dbd16d6b3c3f4eae`.
- Prior qualification reports, simulator logs, artifacts, and untracked Python caches were preserved. No reset, clean, pull, merge, rebase, or push was run.
- No QEMU or QAC process was launched.

## Candidate and environment

The fresh isolated `early-exec` artifact is `/home/guidex/src/ia64-4c-post-rfi-20261108-r2/bin/kernel.elf`, SHA-256 `13a81925da8661ecea0f5751bebb11675337e8c7b2a55eb3987ea51da611f3f8`. It is ELF64 little-endian `EM_IA_64` / `ET_EXEC`, entry `_start=0x108000`, GP `0x676000`, with three non-overlapping PT_LOAD segments and no writable-executable segment. The linked IVT is 32 KiB at `0x100000`, has 68 architectural slots, and places Break at `0x2C00`. The 64 KiB memory stack is `0x4c63e00..0x4c73e00`; the separate 64 KiB RSE backing store is `0x4c73e00..0x4c83e00`.

The toolchain was GCC 13.5.0 and GNU Binutils 2.41 from the established WSL2 setup. The target-compatible constant-GP `libgcc.a` was supplied explicitly. Ski was version 1.5.1, pinned source commit `dfc2902ea1423d9b32543a5daf8026213f2b37a1`, executable SHA-256 `ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45`, direct system mode `-forcesystem`, one CPU.

The candidate manifest records all static gates as passing and retains `guest_execution_authorized: false`. The one run required the runner's explicit `--authorize-kernel-guest-execution` option. The matching loader record and saved invocation are in `/home/guidex/src/ia64-4c-post-rfi-20261108-r2/preflight/`.

## Return path and capture ordering

The exception handler validates vector `0x2C00`, the incoming IPSR/ISR, IIP, instruction slot, immediate, and bounded RSE state. It records each exception in its own 176-byte schema-2 record, updates saved IPSR to skip the faulting slot, serializes the update, restores predicates, and executes `rfi` at `0x3d05bc`.

For slot 1, Break `0x6A17` is at `0x3d0946` in the bundle beginning at `0x3d0940`. The return IPSR has `RI=2`, so `rfi` resumes at the bundle's B-slot instruction `br.many` at `0x3d094c`. That branch goes directly to `0x3d0950`, whose first instruction is `mov r15=psr`. The branch does not change PSR. This B-slot branch is the only instruction executed after `rfi` and before the actual PSR read.

For slot 2, Break `0x6A18` is at `0x3d0d1c` in the bundle beginning at `0x3d0d10`. The return IPSR advances IIP by one bundle and clears RI, so `rfi` resumes at continuation `0x3d0d20`. Its first instruction is `mov r15=psr`.

Neither continuation changes PSR before capture or before cleanup. After the read, the paths update the test's predicate-count result, save the sampled PSR and continuation identity, check the expected sequence, write the slot-specific validity marker last, and complete the existing register/continuation checks. The cleanup label is `0x3d10e0`; its first state-changing instruction is `rsm psr.ic` at `0x3d10e6`, followed later by `ssm psr.ic` after restoring bank 0. There is no `ssm`, `rsm`, or other intentional PSR update between either return and cleanup.

The post-RFI records carry the Break immediate, continuation address, return slot, order, unique validity marker, and actual PSR. Startup requires both records and the global order value to be zero. The slot-1 path only accepts the initial sequence and a clear slot-1 validity marker; slot 2 only accepts the slot-1 sequence and a clear slot-2 marker. The marker is written last. Guest validation, the runner, and the standalone probe reject missing, malformed, duplicate, stale, reversed, out-of-order, or wrong-state records. A valid checkpoint D cannot substitute for a failed direct capture.

## Runtime result

The independent Break probe was built and run before the kernel. Its static target-disassembly gate passed. Ski reported `BREAK_EXCEPTION_PROBE_PASS`, exit 0, two faults, and the same two direct captures at `PSR=0x2000`.

The one kernel run was executed with a 60-second wall limit, inherited 2 GiB address-space and 120-second CPU limits, one CPU, system-mode direct ELF loading, captured stdout/stderr/instruction statistics, and process-group cleanup. The runner returned `BREAK_EXCEPTION_KERNEL_PASS`, exit 0, in 1.317 seconds. Ski recorded 56,685,505 instructions and exactly two faults.

The exact one-run command was:

```bash
prlimit --as=2147483648 --cpu=120 -- python3 scripts/run-ia64-ski.py kernel --artifact /home/guidex/src/ia64-4c-post-rfi-20261108-r2/bin/kernel.elf --manifest /home/guidex/src/ia64-4c-post-rfi-20261108-r2/ia64-artifact-manifest.json --profile early-exec --expected-sha256 13a81925da8661ecea0f5751bebb11675337e8c7b2a55eb3987ea51da611f3f8 --loader-record /home/guidex/src/ia64-4c-post-rfi-20261108-r2/preflight/candidate-loader-record.json --ski-prefix /home/guidex/opt/ski-dfc2902 --timeout 60 --mode system --objdump /home/guidex/src/ia64-2-toolchain-shim/ia64-linux-objdump --capture-dir /home/guidex/src/ia64-4c-kernel-run-20261108 --authorize-kernel-guest-execution
```

| Break record | Immediate | Vector | Fault slot | Return slot | Order | Captured PSR | Result |
|---|---:|---:|---:|---:|---:|---:|---|
| slot 1 | `0x6A17` | `0x2C00` | 1 | 2 | 1 | `0x2000` | pass |
| slot 2 | `0x6A18` | `0x2C00` | 2 | 0 | 2 | `0x2000` | pass |

The PSR bit check uses bit 14 for `i` and bit 13 for `ic`; `0x2000` therefore means `i=0`, `ic=1`. Both exception records, continuation counts, return markers, and saved-state invariants passed. Fault attribution was exactly two expected Break faults, with zero unexpected exceptions or fail-stop vectors. Checkpoints A–D passed in order. Checkpoints C and D each reported PSR `0x2000`, RSC `3`, IVA `0x100000`, and the expected RSE bounds. The guest exited intentionally through SSC exit 66; Ski process status was 0, with no timeout and complete process cleanup.

The standalone register diagnostic also passed its legacy-versus-corrected SSC checks and FPSR write/readback. RNAT remains explicitly unverified; it was not required by this qualification.

## Verification record

All commands below exited 0 unless a row describes an expected negative fixture. Python suites were run in WSL2 with `python3 -B`; the Ski loader tests therefore exercised their Linux-only process-group and prelaunch checks.

| Command or check | Result |
|---|---|
| `IA64_FP_PROFILE=early-exec IA64_TOOLCHAIN_PREFIX=/home/guidex/src/ia64-2-toolchain-shim/ia64-linux IA64_RUNTIME_LIBGCC=/home/guidex/src/gcc-13.5.0-build/ia64-linux-gnu/libgcc/libgcc.a IA64_BUILD_DIR=/home/guidex/src/ia64-4c-post-rfi-20261108-r2 IA64_JOBS=8 scripts/build-ia64.sh` | PASS; 165 target objects, candidate SHA recorded above |
| `python3 scripts/validate-ia64-elf.py --elf /home/guidex/src/ia64-4c-post-rfi-20261108-r2/bin/kernel.elf --repo-root . --profile early-exec --manifest /home/guidex/src/ia64-4c-post-rfi-20261108-r2/ia64-artifact-manifest.json` | PASS; ELF, layout, GP, IVT, stack/RSE, and manifest identity |
| `python3 scripts/validate-ia64-early-exec.py --repo-root . --elf /home/guidex/src/ia64-4c-post-rfi-20261108-r2/bin/kernel.elf --objdump /home/guidex/src/ia64-2-toolchain-shim/ia64-linux-objdump` | PASS |
| `python3 scripts/validate-ia64-break-exception.py --repo-root . --elf /home/guidex/src/ia64-4c-post-rfi-20261108-r2/bin/kernel.elf --objdump /home/guidex/src/ia64-2-toolchain-shim/ia64-linux-objdump` | PASS; linked continuation and capture order |
| `python3 scripts/validate-ia64-cpu-state.py --repo-root . --elf /home/guidex/src/ia64-4c-post-rfi-20261108-r2/bin/kernel.elf --objdump /home/guidex/src/ia64-2-toolchain-shim/ia64-linux-objdump` | PASS; 14 static gates |
| `python3 tests/ia64_foundation_contract_test.py` | PASS, 17 tests |
| `python3 tests/ia64_early_exec_contract_test.py` | PASS, 9 tests |
| `python3 tests/ia64_ski_loader_contract_test.py` | PASS, 8 tests; Linux process-group/prelaunch cases included |
| `python3 tests/ia64_cpu_state_checkpoint_test.py` | PASS, 12 tests |
| `python3 tests/ia64_break_exception_contract_test.py` | PASS, 9 tests; missing/duplicate/stale records, bad `ic`/`i`, reversed identity/order, and checkpoint-D-substitution negatives included |
| `python3 tests/ia64_elf_validator_negative_test.py --elf /home/guidex/src/ia64-4c-post-rfi-20261108-r2/bin/kernel.elf --profile early-exec` | PASS, 3 malformed-artifact cases: bad size, handler boundary, source offset |
| `IA64_TOOLCHAIN_PREFIX=/home/guidex/src/ia64-2-toolchain-shim/ia64-linux IA64_RUNTIME_LIBGCC=/home/guidex/src/gcc-13.5.0-build/ia64-linux-gnu/libgcc/libgcc.a IA64_BREAK_PROBE_OUT_DIR=/home/guidex/src/ia64-4c-post-rfi-probe-20261108 tests/ia64_break_exception/build.sh` | PASS; independent probe ELF built |
| `python3 scripts/validate-ia64-break-exception.py --repo-root . --elf /home/guidex/src/ia64-4c-post-rfi-probe-20261108/break-probe.elf --objdump /home/guidex/src/ia64-2-toolchain-shim/ia64-linux-objdump --probe` | PASS; independent probe disassembly gates |
| `python3 scripts/run-ia64-ski.py break-probe --artifact /home/guidex/src/ia64-4c-post-rfi-probe-20261108/break-probe.elf --manifest /home/guidex/src/ia64-4c-post-rfi-probe-20261108/break-probe.manifest.json --expected-sha256 a98616f0e2ed6af5132134ceb4e118461ba14b2df7da43f0fb1d216fbbc1ac82 --simulator-sha256 ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45 --ski-prefix /home/guidex/opt/ski-dfc2902 --capture-dir /home/guidex/src/ia64-4c-post-rfi-probe-run-20261108 --timeout 60 --mode system --objdump /home/guidex/src/ia64-2-toolchain-shim/ia64-linux-objdump` | PASS; two expected Break faults, exit 0, direct PSR records revalidated |
| `tests/ia64_ski_diagnostic/run.sh /home/guidex/src/ia64-4c-register-diagnostic-20261108` | PASS; fixed SSC ABI output and FPSR readback; legacy comparison failed as expected |
| `python3 scripts/validate-ia64-cpu-state.py --repo-root . --elf /home/guidex/src/ia64-4c-post-rfi-20261108-r2/bin/kernel.elf --objdump /home/guidex/src/ia64-2-toolchain-shim/ia64-linux-objdump --runtime-run-result /home/guidex/src/ia64-4c-kernel-run-20261108/run-result.json --runtime-manifest /home/guidex/src/ia64-4c-post-rfi-20261108-r2/ia64-artifact-manifest.json --runtime-loader-record /home/guidex/src/ia64-4c-post-rfi-20261108-r2/preflight/candidate-loader-record.json --runtime-ski-prefix /home/guidex/opt/ski-dfc2902 --runtime-invocation /home/guidex/src/ia64-4c-post-rfi-20261108-r2/preflight/kernel-invocation.txt --expected-sha256 13a81925da8661ecea0f5751bebb11675337e8c7b2a55eb3987ea51da611f3f8` | PASS; strict postflight validation |
| `pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/verify-mbedtls-profile.ps1 -RepoRoot /mnt/d/dev/guideXOSServer_IA64` | PASS; Mbed TLS 4.1.0, TF-PSA-Crypto 1.1.0, 3 profile patches |
| `IA64_GCC=/home/guidex/opt/ia64-gcc-13.5.0-cxx/bin/ia64-linux-gcc IA64_OBJDUMP=/home/guidex/src/ia64-2-toolchain-shim/ia64-linux-objdump IA64_NM=/home/guidex/src/ia64-2-toolchain-shim/ia64-linux-nm scripts/test-ia64-fp-codegen.sh /home/guidex/src/ia64-4c-fp-codegen-20261108` | PASS; 21 compiler/source cases, 13 expected compiles and 8 expected reproduced compile failures |
| `bash -n scripts/build-ia64.sh && bash -n scripts/run-ia64_in_linux.sh && bash -n scripts/test-ia64-fp-codegen.sh && bash -n tests/ia64_break_exception/build.sh && bash -n tests/ia64_ski_diagnostic/run.sh` | PASS; 5 shell scripts |

The first isolated build attempt stopped at preflight because the target compiler did not report an installed `libgcc.a`; it exited before compilation. Supplying the already qualified target archive fixed that setup issue. A first linked candidate was retained but not run after source review found a stack-frame save-offset error. The corrected `-r2` candidate above was freshly built and passed every gate. No unrelated shell-script failures were repaired.

## Evidence paths and next milestone

Kernel, probe, register-diagnostic, and FP codegen artifacts are retained under `/home/guidex/src/ia64-4c-*`. Kernel run captures are in `/home/guidex/src/ia64-4c-kernel-run-20261108`; the full machine-readable result is `run-result.json`.

Recommended next milestone: define and statically review a vector-specific exception-frame contract for one separately selected synchronous exception, with explicit saved-state and return preconditions. Keep all other vectors fail-stop, and keep external interrupts, scheduling, nesting, and FP-context switching disabled until they receive separate qualification.
