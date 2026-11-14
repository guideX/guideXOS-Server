# IA64-5AR3 PTY Console and Cooperative Context Qualification

**Date:** 2026-11-14 · **Branch:** `IA64_SUPPORT` · **Starting HEAD:** `7acaf878d9c583b884dbfbeaaef9f000ceb4104e`

**Outcome C — `IA64_COOPERATIVE_REQUALIFICATION_BLOCKED`**

The one PTY-backed system-mode console smoke passed. The one authorized cooperative probe was launched with the corrected IA64-5AR1 artifact, pinned Ski 1.5.1, a verified PTY on child fd 0, and bounded wall, CPU, and address-space limits. Ski was terminated by the 25-second CPU soft limit after 25.573 seconds. Its stdout and stderr were empty; it produced no instruction-count file, fault count, last IP, register snapshot, transition, or context record. No cooperative progress can be confirmed, so the A/B/A/B/A switch remains unqualified. The probe was not retried, no diagnostic guest ELF was launched, and no guideXOS kernel was executed.

This phase qualifies the standalone PTY console smoke only. It does not qualify the cooperative switch, kernel integration, interrupts, scheduling, paging, or any new exception mechanism.

## Preservation and provenance

- The requested repository, `IA64_SUPPORT` branch, and starting HEAD matched. The branch started three commits ahead of `origin/IA64_SUPPORT`; no pull, push, merge, rebase, reset, or clean was used. The qualification changes were committed locally after validation.
- Pre-existing untracked Python caches were preserved. Only the runner, its contract test, this report, and the IA64 support summary were staged.
- The accepted IA64-4G kernel candidate remains the historical reference at SHA-256 `ab13fabb6a92ebe039afa2afd9729f6f6ceb2529b63be2b47b0c87cf264d960c`. It was not executed or modified.
- Existing IA64-5A, IA64-5AR1, and IA64-5AR2 reports and their captures were left intact. The IA64-5AR2 one-run authorization was already consumed; this phase used only its separately authorized smoke and one cooperative execution. Register-diagnostic artifacts were checked offline; no new register-diagnostic guest was run.

## Pinned Ski and PTY console contract

The simulator was `/home/guidex/opt/ski-dfc2902/bin/bskinc`, Ski 1.5.1, source commit `dfc2902ea1423d9b32543a5daf8026213f2b37a1`, executable SHA-256 `ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45`. The batch frontend accepted `-noconsole -forcesystem`; the runner did not use the Linux-specific bootloader, patch Ski, or require xterm/X11. Ski's source confirms the Linux no-console SSC console path applies terminal attributes to fd 0 with `tcgetattr`/`tcsetattr` and writes SSC console output to stdout. A controlling terminal is not required for those fd-based terminal operations.

The runner now creates a PTY master/slave pair, passes the slave as the simulator's stdin, and holds both ends open through simulator exit. It checks `/proc/<pid>/fd/0` while the child is alive and records the slave device identity, character-device status, `/dev/null` comparison, and controlling-terminal number from `/proc/<pid>/stat`. Stdout and stderr are separate regular capture files; SSC output is written to stdout, so the runner does not depend on draining output from the PTY master. Cleanup closes both PTY descriptors and reaps the simulator process group. The PTY path records terminal state before and after guest initialization.

Both the smoke and cooperative child had fd 0 at `/dev/pts/2`, a character device matching the PTY slave's device number, not `/dev/null`. `/proc` showed `tty_nr=0` (no controlling terminal). This is consistent with Ski's inspected no-console source path. Before SSC initialization the terminal was canonical, echoing, signal-enabled, `VMIN=1`, `VTIME=0`; after it was noncanonical, no-echo, signal-disabled, `VMIN=0`, `VTIME=0`. The master remained open until child exit. Output stayed observable in regular stdout/stderr files.

The runner retains finite wall limits, process-group cleanup, one Ski processor, a 2-GiB address-space limit, and CPU limits. The smoke used 15/20 seconds soft/hard CPU and 30 seconds wall. The cooperative run used 25/30 seconds soft/hard CPU and 35 seconds wall to bound an opaque stall more tightly than the prior 55–60-second attempts.

## One PTY console smoke

The existing independently validated system-mode smoke ELF was reused without rebuilding:

- ELF: `/home/guidex/src/ia64-3r1-ski-smoke-qualified-20261106/bin/ski-system-smoke.elf`
- ELF SHA-256: `60e2b0d3bec5879298b3a3136940dbe8508fdaa36eff906a9f9577d92731e6ab`
- Guest behavior: entry at the qualified ELF entry point, `SSC_CONSOLE_INIT`, one `SSC_PUTCHAR` marker, then intentional `SSC_EXIT(66)`; no context switching.
- Frontend arguments: `-noconsole -forcesystem`.
- Capture: `/home/guidex/src/ia64-5ar3-qualification-20261114-r1/smoke-capture/`.

Exactly one smoke guest execution occurred. The guest emitted exactly one `SKI_SYSTEM_MODE_SMOKE_PASS` marker (`534b495f53595354454d5f4d4f44455f534d4f4b455f50415353`) to stdout after `SSC_CONSOLE_INIT` returned. Ski reported intentional guest exit status 0 on stderr, 358 instructions, and 0 faults. The simulator exited 0 in 0.170 seconds without timing out; process and PTY cleanup completed. The marker and subsequent intentional exit establish console initialization and output for this smoke path.

The first host postprocessor pass incorrectly searched stdout alone for Ski's `SSC_EXIT` status line and returned a qualification failure even though the preserved guest run had exited successfully. Inspection showed the line was on stderr. The original result is preserved as `run-result.pre-offline-requalification.json`; the host parser now checks both streams, and the existing capture was requalified offline as `SMOKE_PASS`. The guest was not rerun. Raw stdout SHA-256 is `d600c15e2f74f9f83df167792741fdb67856c57dfc9e4255e179da8421200a98`; raw stderr SHA-256 is `4aa43479d9c89d942ebc998abe5815bf770d247486f821a9f0fa1927d95078f5`.

## Corrected probe static gate

The original IA64-5AR1 corrected probe was reused without rebuilding or modification:

- ELF: `/home/guidex/src/ia64-5ar1-coop-probe-20261113-r1/ia64-cooperative-context-probe.elf`
- ELF SHA-256: `76c03f06a33bf219b2aefee2a7cf17fbc3ac31837e4aed592484a562cbfd8cfa`
- Matching manifest: `/home/guidex/src/ia64-5ar1-coop-probe-20261113-r1/ia64-cooperative-context-probe.manifest.json`
- Manifest SHA-256: `0aa278cf89b497b501119c261418caeb3e3869216ec2e6a8dad2c26627cd7a42`

The complete cooperative static gate passed before the guest launch. It verified four outgoing context-switch call sites at `0x10b21c`, `0x10b8fc`, `0x10c81c`, and `0x10cf16`: callers allocate two output registers and place context pointers in `out0/out1` (`r42/r43`); the switch receives them in `in0/in1` (`r32/r33`). It also checked the 1,024-byte, 512-byte-aligned context records, separate stacks and RSE backing stores, B first-entry bootstrap and saved continuations, RSC/RNAT ordering, PFS/GP and ABI assumptions, preserved FP images and caller-scratch policy, and absence of interrupt/scheduler enablement.

The static address ranges were:

| Region | Range |
|---|---|
| IVT | `[0x100000, 0x108000)` |
| Probe text | `[0x108000, 0x10e1d0)` |
| Context B / Context A | `0x112200` / `0x112600` |
| A stack / B stack | `[0x112a10, 0x122a10)` / `[0x122c10, 0x132c10)` |
| A RSE backing store / B RSE backing store | `[0x133000, 0x143000)` / `[0x143400, 0x153400)` |

These checks establish the intended linked layout and assembly contract. They are not runtime evidence that either context switched or preserved architectural state.

## One cooperative execution

The runner's authorized invocation was:

```text
python3 scripts/run-ia64-ski.py cooperative-context-probe \
  --artifact /home/guidex/src/ia64-5ar1-coop-probe-20261113-r1/ia64-cooperative-context-probe.elf \
  --manifest /home/guidex/src/ia64-5ar1-coop-probe-20261113-r1/ia64-cooperative-context-probe.manifest.json \
  --expected-sha256 76c03f06a33bf219b2aefee2a7cf17fbc3ac31837e4aed592484a562cbfd8cfa \
  --simulator-sha256 ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45 \
  --ski-prefix /home/guidex/opt/ski-dfc2902 \
  --objdump /home/guidex/src/ia64-2-toolchain-shim/ia64-linux-objdump \
  --capture-dir /home/guidex/src/ia64-5ar3-qualification-20261114-r1/probe-capture \
  --timeout 35
```

Ski was invoked as `bskinc -stats -icnt <capture>/instruction-counts.txt -noconsole -forcesystem <verified snapshot>`. The capture is `/home/guidex/src/ia64-5ar3-qualification-20261114-r1/probe-capture/`. The run record verifies the probe hash, manifest hash, pinned simulator identity, PTY child fd 0, and one-CPU system mode.

| Evidence | Result |
|---|---|
| Authorized cooperative guest executions | 1; no retry |
| Elapsed / bounds | 25.573 seconds; 35-second wall, 25/30-second CPU soft/hard, 2-GiB address-space limit |
| Termination | Signal `SIGXCPU` (`exit_code=-24`) at the CPU soft limit; not a wall timeout |
| PTY / cleanup | Child fd 0 matched `/dev/pts/2`, not `/dev/null`; process-group and descriptor cleanup completed |
| stdout / stderr | Both empty (0 bytes; SHA-256 `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`) |
| Ski instructions / faults / final IP | Unavailable / unavailable / unavailable; Ski did not produce the requested final `-icnt` data before termination |
| Transition, context, and architectural-state records | None; no A/B/A/B/A event, stack/RBS runtime observation, sentinel, NaT/RNAT, FP, ABI, or continuation observation |
| Automatic retry / kernel execution | None / none |

The earliest confirmed runtime frontier is the launched pinned Ski process with verified PTY stdin. The probe capture confirms no guest marker and no final simulator statistics; it does not establish the guest entry address or prove whether the probe returned from its own console initialization. The independent smoke proves the same batch no-console PTY mechanism can initialize SSC, emit output, and exit for the smoke ELF. It does not prove that this probe reached the first switch.

The probe calls `SSC_CONSOLE_INIT`, executes `ia64_coop_probe_run`, and only afterward enters its C++ reporter. That reporter emits its START line, A/B transition labels, context records, and preservation results only after the whole switch sequence returns. Therefore this capture format cannot identify a mid-sequence stall from output alone. Ski was killed before writing `-icnt` statistics, and the pinned batch invocation yielded no last IP, register snapshot, or bounded trace. The pinned manual documents `-strace` as a user-mode facility, so it does not provide system-mode trace attribution for this run. The runtime frontier within the guest and the cause of CPU consumption remain unknown. Do not infer RSE or NaT preservation from the static gate or empty capture.

## Regression results

| Check | Result |
|---|---|
| Full IA-64 contract and malformed-ELF suite | PASS, 103 tests in Linux/WSL; includes loader, cooperative, static, negative manifest, and malformed-ELF contracts |
| Corrected probe static gate | PASS with the exact artifact and manifest above |
| PTY runner contract tests | PASS, including child fd-0 PTY attestation and `SSC_EXIT` parsing from stderr |
| Historical register-diagnostic captures | Offline revalidation PASS for preserved legacy/fixed captures; no diagnostic guest launched in this phase. Fixed ordinary-register and FPSR checks pass; RNAT remains unverified by those diagnostics. |
| FP code-generation harness | PASS; `/home/guidex/src/ia64-5ar3-qualification-20261114-r1/fp-codegen/report.md` |
| Direct Mbed TLS profile verification | PASS; Mbed TLS 4.1.0, TF-PSA-Crypto 1.1.0, 3 patches |
| Targeted shell/Python syntax checks | PASS (`bash -n` for five targeted scripts; Python AST syntax plus module imports through the suite) |
| `git diff --check` | PASS before local commit |

A supplemental Windows-host run of the 103-test suite had one Linux-path assertion mismatch (`/opt/...` expected, `\opt\...` produced by Windows `Path`). The authoritative Linux/WSL run passed all 103 tests. The Windows result did not affect the artifact or guest-run authorization.

## Qualification and next milestone

The smoke qualifies the PTY-backed console route for the small system-mode smoke ELF. The cooperative probe is **not qualified**. No dynamic evidence establishes either context entry, a first transfer, a return, or correct stack/RSE/NaT/FP/ABI preservation. Outcome C is required because no authentic cooperative progress was observed and the guest execution frontier is unknown.

Keep the cooperative primitive isolated from guideXOS scheduling. The next milestone is a separately authorized diagnostic design that can preserve an instruction address/register checkpoint or bounded trace during the switch sequence without changing this already-executed probe's evidence. Any changed probe needs a new artifact hash and preflight, followed by a new explicit one-run authorization. Kernel integration remains pending; no kernel run is recommended from this result.

\n