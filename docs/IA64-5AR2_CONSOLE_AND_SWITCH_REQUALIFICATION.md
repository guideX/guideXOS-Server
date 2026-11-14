# IA64-5AR2 Ski Console and Cooperative Switch Requalification

**Date:** 2026-11-14 · **Branch:** `IA64_SUPPORT` · **Starting HEAD:** `39c3f48d3912258a16db9ffe3aecb3b149975b18`
**Outcome C — `IA64_COOPERATIVE_REQUALIFICATION_BLOCKED`**

The interactive Ski failure was traced to its xterm-based console path, and the cooperative runner was changed to select batch `bskinc -noconsole -forcesystem`. The one authorized cooperative run did not establish a usable guest console or produce guest evidence: it used `/dev/null` for stdin, while Ski's no-console SSC path expects a tty on fd 0. It exhausted its 55-second CPU soft limit and produced no instruction count, fault count, address, or context transition. No second cooperative run was made. The corrected calling convention is statically verified but remains unqualified at runtime.

No guideXOS kernel was built or run. Scheduling, interrupts, paging, user mode, and device initialization remain outside this probe.

## Repository and preserved artifacts

- Repository: `D:\dev\guideXOSServer_IA64`; branch `IA64_SUPPORT`.
- Starting HEAD was `39c3f48d3912258a16db9ffe3aecb3b149975b18`; upstream `origin/IA64_SUPPORT` was `2640e00b40353f2094f87b502d76d01661399f12`. The tracked tree was clean before these scoped changes. Existing untracked Python caches and historical artifacts were preserved.
- The original failed probe remains SHA-256 `9189f5c26b819016dd0bd5f663f201d30cdcfb58fc7ad9046381806894bb4b45`; its manifest remains `000a98cebc36a28dd4d5afa81e6faf2a29b0028f4a40a4793af89bf90f7d8f0c`.
- Corrected IA64-5AR1 probe: `/home/guidex/src/ia64-5ar1-coop-probe-20261113-r1/ia64-cooperative-context-probe.elf`, SHA-256 `76c03f06a33bf219b2aefee2a7cf17fbc3ac31837e4aed592484a562cbfd8cfa`. Manifest: `/home/guidex/src/ia64-5ar1-coop-probe-20261113-r1/ia64-cooperative-context-probe.manifest.json`, SHA-256 `0aa278cf89b497b501119c261418caeb3e3869216ec2e6a8dad2c26627cd7a42`.
- Accepted IA64-4G kernel remains `/home/guidex/src/ia64-4g-stacked-nat-rse-kernel-20261111-r3/bin/kernel.elf`, SHA-256 `ab13fabb6a92ebe039afa2afd9729f6f6ceb2529b63be2b47b0c87cf264d960c`. It was not rebuilt or run.
- Pinned Ski is version 1.5.1, source commit `dfc2902ea1423d9b32543a5daf8026213f2b37a1`, executable `/home/guidex/opt/ski-dfc2902/bin/ski`, SHA-256 `ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45`. `/bin/bskinc` and `/bin/bski` resolve to this executable; argv0 selects the batch or interactive frontend.

The complete new capture is preserved at `/home/guidex/src/ia64-5ar2-coop-probe-20261114-r1/capture/`. The two independent register-diagnostic runs are preserved at `/home/guidex/src/ia64-5ar2-register-diagnostic-20261114-r1/`.

## Ski console diagnosis

The IA64-5AR1 failure used the interactive `ski` frontend with `-i`, launched by the bounded debugger runner. Its command file used only finite `step`, `eval`, `rd`, `dd`, `save`, and `quit` operations and did not change console configuration. The runner used `pty.fork()`, shared the PTY for standard streams, set `TERM=xterm`, inherited `DISPLAY`, and enforced wall, CPU, and address-space limits.

The pinned Linux Ski source shows that interactive `SSC_CONSOLE_INIT` enters `initConsole()`: it forks and executes `xterm`, then reads the xterm startup byte. The preserved IA64-5AR1 output contains `Unable to create console window`. In the current WSL environment, `DISPLAY=:0`, `WAYLAND_DISPLAY=wayland-0`, `TERM=xterm-256color`, and xterm terminfo entries exist, but `xterm` is absent from `PATH`. This accounts for the failed interactive console path; no X11 or xterm dependency was installed.

The pinned executable's `bskinc -help` lists `-noconsole`; its installed man page says this batch option uses the current tty. The Linux `bskinc` argv0 also enables batch mode and `noConsole`; the explicit `-forcesystem` option overrides its user-mode default. Option parsing was checked without a guest ELF, and the existing [IA64-3R1 direct system-ELF loader record](IA64-3R1_SKI_LOADER_QUALIFICATION.md) already verifies the `bskinc -forcesystem` loader path with a privileged smoke and intentional SSC exit.

There was a gap in the one-run preflight. Pinned `syscall-linux.c` shows that `SSC_CONSOLE_INIT` skips xterm when `noConsole` is set, but still calls `tcgetattr(0)` and `tcsetattr(0, ...)` to put fd 0 into raw mode; batch output goes to fd 1. A PTY on stdin is therefore required. The cooperative run below instead passed `stdin=DEVNULL`. Its empty output does not tell us whether the SSC call failed or whether execution stopped elsewhere. This is a runner configuration error and an incomplete console setup, not evidence for a switch or RSE defect.

After the single run, `scripts/run-ia64-ski.py` was corrected to optionally attach a PTY slave to stdin while retaining stdout/stderr file capture and keeping the PTY master open through execution. The cooperative runner now selects this mode. Fake-simulator tests verify the PTY and cleanup behavior, but the corrected Ski guest path has not been exercised. This change must not be represented as runtime-qualified.

## Calling-convention and static probe verification

The corrected source and target disassembly were rechecked at all four switch call sites: `0x10b21c`, `0x10b8fc`, `0x10c81c`, and `0x10cf16`. Each caller allocates two outgoing registers (`alloc r16=ar.pfs,12,10,0`, equivalent to source `alloc r16=ar.pfs,6,4,2,0`), writes the context pointers through `out0/out1` (`r42/r43`), and does not overwrite them before `br.call`. The switch consumes them as `in0/in1` (`r32/r33`). The focused source contract test checks the two procedures and four pointer pairs. This establishes the static ABI mapping, not successful dynamic argument delivery.

The final static-only cooperative probe gate passed. It checked ELF64 little-endian `EM_IA_64`, entry and segment geometry, context/stack/RBS boundaries, saved-continuation and bootstrap/resume rules, RSC/RNAT constraints, switch save/restore ordering, source, and target disassembly. The key linked ranges are:

| Region | Address range |
|---|---|
| IVT | `[0x100000, 0x108000)` |
| Text | `[0x108000, 0x10e1d0)` |
| Context B / Context A | `0x112200` / `0x112600` |
| A memory stack | `[0x112a10, 0x122a10)` |
| B memory stack | `[0x122c10, 0x132c10)` |
| A RSE backing store | `[0x133000, 0x143000)` |
| B RSE backing store | `[0x143400, 0x153400)` |

The static gate confirms the independent non-overlapping storage and legal static switch sequence. Runtime preservation of BSP/BSPSTORE, RSC, RNAT/NaT, ABI/FP state, branch registers, and saved continuations is still unproven.

## Single authorized cooperative execution

Exactly one cooperative guest execution was launched. The wrapper invocation selected the verified corrected probe and set a 60-second wall bound:

```text
python3 scripts/run-ia64-ski.py cooperative-context-probe \
  --artifact /home/guidex/src/ia64-5ar1-coop-probe-20261113-r1/ia64-cooperative-context-probe.elf \
  --manifest /home/guidex/src/ia64-5ar1-coop-probe-20261113-r1/ia64-cooperative-context-probe.manifest.json \
  --expected-sha256 76c03f06a33bf219b2aefee2a7cf17fbc3ac31837e4aed592484a562cbfd8cfa \
  --simulator-sha256 ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45 \
  --capture-dir /home/guidex/src/ia64-5ar2-coop-probe-20261114-r1/capture \
  --timeout 60
```

The recorded Ski argv was:

```text
/home/guidex/opt/ski-dfc2902/bin/bskinc -stats -icnt /home/guidex/src/ia64-5ar2-coop-probe-20261114-r1/capture/instruction-counts.txt -noconsole -forcesystem /tmp/guidexos-ia64-ski-elf-b09cfdqk/selected.elf
```

The run used one simulated CPU, a 2-GiB address-space bound, CPU soft/hard limits of 55/60 seconds, and a 60-second wall timeout. The actual runner record says stdin was `DEVNULL`; stdout and stderr were directed to the capture files. It records PID/PGID `362`, exit `-24` (`SIGXCPU`), elapsed `55.929` seconds, `timed_out=false`, and both cleanup flags true. The final qualification failed; the simulator's terminal condition was `SIMULATOR_ERROR`.

| Evidence | Recorded result |
|---|---|
| Cooperative guest executions in IA64-5AR2 | Exactly 1; no retry |
| Console initialization | `not_observed`; neither pass nor explicit console error was captured |
| Guest stdout / stderr | Both empty, 0 bytes; both SHA-256 `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| Instruction-count file / count | File absent / unavailable |
| Ski fault count | Unavailable; no zero-fault claim |
| Last guest address or CPU/RSE state | None recorded |
| Transition, context-state, validation, result-marker records | None |
| Process and process-group cleanup | Complete / complete |

The only proven frontier is that the bounded simulator process was launched with the selected ELF. Guest entry, console completion, initial context construction, and every switch boundary remain unproven. `SIGXCPU` establishes that the CPU soft bound ended the run; it does not identify what Ski or the guest was executing. No attribution is made to an RSE instruction, context transition, or switch algorithm.

## Regression and prerequisite results

- Full IA-64 host unittest discovery: **PASS, 101 tests, 0 skips**. The focused cooperative contract suite passed 15 tests; the Ski runner/process-group and PTY coverage passed 11 tests.
- Final cooperative static-only ELF and target-disassembly gate: **PASS**.
- Independent Ski register diagnostics: **PASS**, two separately authorized standalone diagnostic ELFs, not cooperative probes or a kernel. The legacy formatter reproduced its expected `0x77` corruption; the fixed diagnostic returned the expected values and `SSC_GETCHAR=0x51`, with register and FPSR readback checks passing. RNAT remains explicitly unverified. Logs, ELFs, instruction-count files, disassemblies, and hashes remain in the diagnostic directory above.
- IA-64 FP code-generation harness: **PASS**; report `/tmp/guidexos-ia64-fp-repro.g38Gwe/report.md`.
- Direct Mbed TLS profile verifier: **PASS**; Mbed TLS 4.1.0, TF-PSA-Crypto 1.1.0, three patches.
- Targeted Bash syntax checks: **PASS** for the cooperative build, Ski diagnostic, FP harness, Linux runner, and IA-64 build wrapper. `scripts/test-build.sh` was not modified or run as a build; its syntax was checked only.
- `git diff --check`: **PASS**.

No guideXOS kernel build or execution occurred. The IA64-5AR1 and IA64-5A reports remain unchanged historical evidence.

## Qualification and IA64-5AR3 recommendation

This phase is **Outcome C — `IA64_COOPERATIVE_REQUALIFICATION_BLOCKED`**. The batch frontend selection removes the known xterm launch requirement, and static ABI checks pass, but this run's `/dev/null` stdin violated Ski's tty requirement for `SSC_CONSOLE_INIT`. No switch progress was observed. The subsequent PTY runner correction is not a reason to spend another guest execution under this authorization; that authorization has been consumed.

IA64-5AR3 should first review the PTY-backed runner and capture setup, use a new explicit one-run authorization, and run only the standalone corrected probe under `bskinc -noconsole -forcesystem` with PTY stdin, captured stdout/stderr, the existing resource bounds, and process-group cleanup. Qualification still requires the genuine A/B/A/B/A sequence and its register, stack, RSE, NaT/RNAT, ABI/FP, and continuation records. Do not integrate the switch into guideXOS scheduling before that evidence exists.

The local phase commit and final repository HEAD are reported in the delivery summary; no remote push was performed.
