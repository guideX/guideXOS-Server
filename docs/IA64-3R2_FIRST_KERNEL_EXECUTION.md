# guideXOS Server IA-64 — IA64-3R2 First Kernel Execution

| Field | Value |
|---|---|
| Date | 2026-11-07 |
| Repository | `D:\dev\guideXOSServer_IA64` |
| Branch | `IA64_SUPPORT` |
| Starting HEAD | `feb8cd857e9dc59748bb37fab6d445773ede0ba0` |
**Outcome:** **B — `IA64_KERNEL_EXEC_PARTIAL`**

Exactly one authorized system-mode run loaded the immutable guideXOS IA-64 `early-exec` kernel in the qualified Ski simulator. Native kernel instructions ran, BSS clearing and early C initialization completed, authentic guest console messages appeared, and Ski reported a zero-status exit with no faults. The guest's IVA and BSP readbacks were both `0x77`; those values conflict with the configured IVT and RSE address ranges. The run therefore demonstrates native execution and the bounded exit path, while runtime IVT/RSE state remains unresolved.

This report makes no UEFI, desktop, scheduler, interrupt-resumption, device, or general IA-64 runtime claim.

## Repository and evidence preservation

- The checkout started on `IA64_SUPPORT` at the requested HEAD, tracking `origin/IA64_SUPPORT` with one local commit ahead and no commits behind.
- Existing untracked `scripts/__pycache__` and `tests/__pycache__` artifacts were preserved. Test runs set `PYTHONDONTWRITEBYTECODE=1`.
- No reset, clean, pull, merge, rebase, or push was performed. The kernel candidate was not rebuilt or modified.
- No QEMU launch or QAC reservation was made.
- All phase evidence is in `/home/guidex/src/ia64-3r2-first-kernel-execution-20261107-YTYOYG/`, including preflight validators, runner source and loader record snapshots, the exact simulator invocation and result, complete simulator stdout/stderr, instruction counts, cleanup evidence, and test logs.

## Environment and identities

Windows remained the host. Execution used the installed Ubuntu 24.04 WSL2 distribution on x86-64.

| Component | Verified identity |
|---|---|
| Kernel candidate | `/home/guidex/src/ia64-3-ssc-corrected-20261105-b/bin/kernel.elf` |
| Candidate SHA-256 | `b9c6053e2acfd96935c1cf83bff76f4e7516b00d004d163d74e1f2303f61d820` |
| Profile | `early-exec`, matching `ia64-artifact-manifest.json` |
| Ski installation | `/home/guidex/opt/ski-dfc2902`; executable mode `0755` |
| Runner executable | `/home/guidex/opt/ski-dfc2902/bin/bskinc`, symlink to `ski` |
| Ski SHA-256 | `ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45` |
| Ski source | clean checkout at `dfc2902ea1423d9b32543a5daf8026213f2b37a1`, described as `v1.5.1-32-gdfc2902` (version 1.5.1) |
| CPU count | Source-enforced one CPU: `SKI_NPROC=1`, `nproc=1`; the qualified build has no `NEW_MP`/`-cpus` configuration |

The runner's manifest, exact hash, profile, Ski hash, pinned source/version record, direct-loader record, simulator symlinks, ELF bounds, early-exec source/disassembly gates, and explicit authorization flag all passed. The loader record remained `kernel_execution_authorized: false`; this run used the separate `--authorize-kernel-guest-execution` flag.

## Memory and handoff preflight

The pinned Ski direct system-mode ELF loader uses `p_vaddr` with ET_EXEC bias zero. Ski's loader source allocates a zeroed `p_memsz` segment buffer, reads `p_filesz`, and writes the full memory extent into dynamically allocated simulator pages. The runner independently checked the ELF class, endianness, machine/type, program-header and file bounds, arithmetic, `p_filesz <= p_memsz`, `p_vaddr == p_paddr`, alignment, non-overlap, permissions, entry mapping, and the simulator's 63-bit physical-address limit. The direct loader has no guest-resident loader ranges or fixed guest-physical reservations.

| PT_LOAD | Physical interval, end exclusive | File size | Memory size | Flags |
|---|---:|---:|---:|---|
| 0 | `0x100000–0x3cde30` | `0x2cde30` | `0x2cde30` | RX |
| 1 | `0x3ce000–0x471788` | `0xa3788` | `0xa3788` | R |
| 2 | `0x472000–0x4c82798` | `0x16130` | `0x4810798` | RW |

All ranges are disjoint and below Ski's default 63-bit physical-address limit. The `0x4810798` RW memory extent includes the zero-filled BSS. Entry `0x108000` is executable; GP `0x672000`, the 64 KiB stack `0x4c5fe00..0x4c6fe00`, and the 64 KiB RSE backing store `0x4c6fe00..0x4c7fe00` lie in the mapped RW extent. The IVT is at `0x100000`, aligned to `0x8000` and covered by RX memory. No loader or simulator reservation overlaps these guest ranges.

WSL reported about 30 GiB available host memory. The runner process and child inherited `RLIMIT_AS=2 GiB` and `RLIMIT_CPU=120 seconds`; the runner also enforced a 60-second wall-clock timeout and process-group cleanup.

## Invocation

The one authorized runner invocation was:

```bash
prlimit --as=2147483648 --cpu=120 -- \
  python3 scripts/run-ia64-ski.py kernel \
  --artifact /home/guidex/src/ia64-3-ssc-corrected-20261105-b/bin/kernel.elf \
  --manifest /home/guidex/src/ia64-3-ssc-corrected-20261105-b/ia64-artifact-manifest.json \
  --profile early-exec \
  --expected-sha256 b9c6053e2acfd96935c1cf83bff76f4e7516b00d004d163d74e1f2303f61d820 \
  --loader-record /mnt/d/dev/guideXOSServer_IA64/docs/ia64-3r1-ski-loader-record.json \
  --ski-prefix /home/guidex/opt/ski-dfc2902 \
  --capture-dir /home/guidex/src/ia64-3r2-first-kernel-execution-20261107-YTYOYG/capture \
  --timeout 60 \
  --mode system \
  --objdump /home/guidex/src/ia64-2-toolchain-shim/ia64-linux-objdump \
  --authorize-kernel-guest-execution
```

The runner invoked `/home/guidex/opt/ski-dfc2902/bin/bskinc -stats -icnt <capture>/instruction-counts.txt -forcesystem <verified-private-snapshot>`. The private snapshot was hash-checked and removed by runner cleanup. The exact snapshot path is recorded in `capture/run-result.json`.

## Startup evidence

The target disassembly begins at `0x108000` with `rsm 0x6000` (mask `PSR.i` and `PSR.ic`), followed by serialization and setup of GP, SP, RSE, FP state, and `CR.IVA`. It initializes the Ski console, clears BSS, then calls `kernel_main`. The independent startup gates passed, including the RSE ordering, FP state setup, IVT geometry, SSC ABI, external-interrupt policy, and stop-only early-entry call graph.

Ski's `-icnt` output aggregates instruction counts by mnemonic, not by address. The system-mode loader sets the instruction pointer from ELF `e_entry=0x108000`; the first instruction at that address in the verified target disassembly is `rsm 0x6000`. Runtime entry is demonstrated by the guest-originated C-level console output and startup instruction counts, though this runner does not record a per-IP trace.

The captured guest-originated console messages were:

```text
guideXOS IA-64 kernel init
  IVA = 0x77
  BSP = 0x77
  Interrupt collection enabled
IA-64 arch init complete
IA64-2 early-exec: SKI single-entry profile
Interrupts masked; scheduler and threads unavailable
```

`capture/stdout.log` contains these guest SSC console messages. The simulator-generated exit and statistics messages below came from `capture/stderr.log`; `runner-cli.stdout` separately contains host-side validator output and the runner classification.

The early profile enables exception collection (`PSR.ic`) after installing the IVT while leaving external interrupts masked (`PSR.i=0`). The guest's `Interrupt collection enabled` message describes `PSR.ic`; it does not indicate external interrupt delivery. The static gates confirm no interrupt-enable instruction in the path.

Ski's simulator-generated output, kept separate from guest output, was:

```text
kernel exited with status 0
56606488 insts (0 faults), 0.70 sec, 81422762 i/s, 18869287 cycles, 3.00 ipc
```

The runner recorded `GUEST_EXIT_ZERO`, exit code `0`, `timed_out=false`, and elapsed wall time `1.018` seconds. The early path calls the qualified `SSC_EXIT=66` service after printing its bounded status, and Ski reported its zero-status exit. No fault or unexpected vector appeared. The aggregate instruction report included `loadrs` once, `rsm` twice, `ssm` once, and `st8` 9,433,844 times as the startup BSS clear loop ran.

## Runtime register discrepancy and outcome

The guest's `IVA` diagnostic reads `CR.IVA`; the target disassembly shows the corresponding read instruction. The configured IVT address is `0x100000`, but the guest read back `0x77`. The guest's `BSP` diagnostic reads `AR.BSP` (not `BSPSTORE`); after RSE setup it reported `0x77`, outside the configured RSE backing-store interval `0x4c6fe00..0x4c7fe00`. These are guest-side register readbacks and are preserved exactly as observed.

The batch runner exposes aggregate instruction statistics and guest console text, but no initial/final register dump or per-instruction IP trace. GP, SP, BSPSTORE, FPSR, and PSR were not independently sampled during the run. The source/disassembly show their initialization instructions, while the unexpected IVA/BSP values prevent a claim that runtime IVT/RSE state is correct. The available evidence does not identify whether the mismatch comes from Ski register semantics, the target code/ABI, or the startup state; no fault was reported.

**Acceptance: `IA64_KERNEL_EXEC_PARTIAL`.** The guest entered native code, reached the kernel and architecture initialization paths, printed the expected early-exec status, executed 56,606,488 instructions with zero faults, and reached the deliberate exit. Runtime IVT/RSE state remains unproven because the guest reported inconsistent readbacks. The earliest demonstrated discrepancy is the IVA readback in architecture initialization, followed by the BSP readback; the underlying failure class remains unknown.

The simulator process was absent after runner return, and the verified snapshot no longer existed. This phase did not start scheduling, timers, external interrupts, additional CPUs, desktop code, devices, user processes, or UEFI.

## Regression results

| Check | Result |
|---|---:|
| `tests/ia64_foundation_contract_test.py` | PASS, 17/17 |
| `tests/ia64_early_exec_contract_test.py` | PASS, 9/9 |
| `tests/ia64_ski_loader_contract_test.py` | PASS, 8/8; includes seven pre-launch rejection cases and timeout/process-group cleanup |
| `tests/ia64_elf_validator_negative_test.py` | PASS, 3/3 malformed IVT cases rejected |
| `scripts/validate-ia64-early-exec.py` | PASS, 12 static gates |
| Manifest/hash/profile/loader/simulator identity preflight | PASS |
| `scripts/verify-mbedtls-profile.ps1` | PASS; Mbed TLS 4.1.0, TF-PSA-Crypto 1.1.0, three patches |
| Bash syntax | PASS for `scripts/build-ia64.sh`, `scripts/run-ia64_in_linux.sh`, and `tests/ia64_ski_smoke/build.sh` |
| `git diff --cached --check` | PASS |

No test case failed. The first ELF-negative harness invocation omitted its required `--elf` and `--profile` arguments, so it ran no cases; the corrected invocation passed 3/3. The first Mbed TLS invocation could not resolve `pwsh` from the non-login WSL path; `/home/guidex/bin/pwsh` then ran the verifier successfully. The verifier emitted a non-fatal WSL user-session startup warning and returned `MBEDTLS_PROFILE_VERIFY=PASS`.

## Next phase

Keep external interrupts masked and scheduling disabled. First investigate the `CR.IVA` and `AR.BSP` readbacks using a separate bounded diagnostic plan that can expose per-IP and register state. Do not rerun this kernel as part of IA64-3R2. A later, separately authorized phase can decide whether to instrument Ski or correct the target's state setup and then perform one controlled retry. UEFI, desktop, scheduler, timer, device, and multi-CPU qualification remain separate work.
