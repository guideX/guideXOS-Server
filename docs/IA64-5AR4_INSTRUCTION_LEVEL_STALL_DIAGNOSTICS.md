# IA64-5AR4 Instruction-Level Cooperative Stall Diagnostics

**Date:** 2026-11-14 · **Branch:** `IA64_SUPPORT` · **Starting HEAD:** `d95a8df6af8d774cab8bcc68fc446e3ed45c5df3`

**Outcome B — `IA64_COOPERATIVE_STALL_LOCALIZED`.** One bounded guest run advanced through startup, the SSC `break.i`, and BSS clearing, then repeatedly executed `ia64_coop_probe_setup`. Its nested call overwrites the incoming `b0`, and the setup routine's final `br.ret b0` returns to the nested-call continuation inside setup. The probe never reached its first context switch. The cooperative switch remains unqualified; no guideXOS kernel was run.

The checkout began on `IA64_SUPPORT` at `d95a8df6af8d774cab8bcc68fc446e3ed45c5df3`, matching `origin/IA64_SUPPORT`. Pre-existing untracked Python caches and prior captures/build artifacts were preserved. No reset, clean, pull, merge, rebase, or push was used.

## Artifact and simulator identity

The existing IA64-5AR3 cooperative artifact was used without rebuilding or modifying it. The accepted IA64-4G kernel was checked for identity only; it was not loaded or executed.

| Artifact | Path | SHA-256 |
|---|---|---|
| Cooperative probe ELF | `/home/guidex/src/ia64-5ar1-coop-probe-20261113-r1/ia64-cooperative-context-probe.elf` | `76c03f06a33bf219b2aefee2a7cf17fbc3ac31837e4aed592484a562cbfd8cfa` |
| Matching manifest | `/home/guidex/src/ia64-5ar1-coop-probe-20261113-r1/ia64-cooperative-context-probe.manifest.json` | `0aa278cf89b497b501119c261418caeb3e3869216ec2e6a8dad2c26627cd7a42` |
| Accepted IA64-4G kernel, identity check only | `/home/guidex/src/ia64-4g-stacked-nat-rse-kernel-20261111-r3/bin/kernel.elf` | `ab13fabb6a92ebe039afa2afd9729f6f6ceb2529b63be2b47b0c87cf264d960c` |
| Ski 1.5.1 executable (`bskinc`) | `/home/guidex/opt/ski-dfc2902/bin/bskinc` | `ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45` |

The pinned Ski source is commit `dfc2902ea1423d9b32543a5daf8026213f2b37a1`. The diagnostic also verified `/bin/bski` and `/bin/ski` resolve to the same pinned executable; `argv[0]` selects the frontend. The qualified command-file syntax was checked against that source/manual and with a no-ELF parser smoke before guest authorization.

The ELF preflight accepted entry `0x108000`, IVT `[0x100000,0x108000)`, text `[0x108000,0x10e1d0)`, and load ranges `[0x100000,0x10e1d0)`, `[0x10f000,0x10f678)`, and `[0x110000,0x153428)`. The existing cooperative static gate passed for this exact ELF and manifest.

## Source and disassembly frontier map

Objdump labels instruction addresses at slot addresses; Ski reports an encoded IP whose low two bits select the slot. Thus a slot-2 instruction at bundle base `0x1080c0` is reported as IIP `0x1080c0`, slot 2, IP `0x1080c8`.

| Source or symbol | ELF/disassembly boundary | Meaning |
|---|---:|---|
| `_start` in `tests/ia64_cooperative_context_switch/entry.s` | `0x108000` | ELF entry; first instruction masks interrupts/interrupt collection with `rsm 0x6000`. |
| Startup RSE initialization | `0x108036`, `0x108040`, `0x108060`, `0x108066` | `ar.rsc=0`, `loadrs`, `ar.bspstore=0x133000`, then `ar.rsc=3`. |
| First SSC console-init trap | `0x1080cc`; debugger stop IP `0x1080c8` | `r15=20`, then `break.i 0x80000`; the debugger breakpoint is at the slot-2 IP immediately before the trap. |
| BSS clear loop | `0x108100` | Clears `[0x112000,0x153428)`; the trace later observes this loop complete. |
| Call into `ia64_coop_probe_run` | `0x108126`; callee `0x10adc0` | Entry assembly transfers to the assembly probe sequence after BSS clearing. |
| First probe output call | `0x108fac`; target `puts` at `0x108280` | In `ia64_coop_probe_main` (`probe_main.cpp:236`), called only after `ia64_coop_probe_run` returns. No probe output precedes the A/B/A/B/A sequence. |
| `ia64_coop_probe_setup` | starts `0x10a2d0`, ends at helper boundary `0x10ab50` | Its first nested call is at `0x10a2dc`; the call's continuation is `0x10a2e0`. The final `br.ret b0` is at `0x10ab4c`. |
| Caller of setup | `0x10adfc`; expected return continuation `0x10ae00` | `ia64_coop_probe_run` calls setup. Its expected caller continuation is not restored by setup. |
| Context B first-entry bootstrap | `0x10ae90` | Prepared as B's first-entry continuation, but never reached dynamically. |
| First context-switch call | `0x10b21c`; switch entry `0x10cf90` | First of the four statically checked call sites; not reached dynamically. |
| Context A first continuation | `0x10b220` | First return point after the initial A-to-B transfer; not reached. |
| First RSE save | `0x10dc86` (`flushrs`, slot 1) | First spill/flush point in the switch; not reached. |
| Incoming BSPSTORE handoff | `0x10de16` (`mov.m ar.bspstore`, slot 1) | Loads the incoming context's backing-store pointer; not reached. |
| Switch return | `0x10e19c` (`br.ret b0`, slot 2) | Shared switch return instruction; not reached. |
| Context B resume / A final continuation | `0x10c820` / `0x10b900` | Later continuations in the planned sequence; neither was reached. |

The map was checked against the linked ELF's symbols and disassembly and the assembly sources in `entry.s` and `probe_sequence.s`. The first observable probe output is downstream of the failing setup call, so empty stdout alone would not have located this stall.

## Bounded debugger contract

The new host-side diagnostic is `scripts/diagnose-ia64-coop-switch.py`. It reuses the existing `scripts/run-ia64-ski.py` PTY/process-group infrastructure and pins the exact probe, manifest, kernel identity, Ski binary, and supported stepping plan. Its command file uses only the pinned Ski commands `bs`, finite `step`, `eval`, `rd`, `dd`, guarded `if`, and final `quit`; it contains no `run` or `continue`.

The fixed instruction plan is:

| Stage | Command | Maximum instructions |
|---|---|---:|
| Reach the SSC breakpoint | `step 64` | 64 |
| Execute the SSC trap instruction | `step 1` | 1 |
| Inspect after the SSC frontier | 469 × `step 512` | 240,128 |
| Total configured ceiling |  | 240,193 |

The first step stopped at the breakpoint after 33 guest instructions, rather than consuming all 64. The SSC instruction advanced the count to 34, then the 469 planned quanta reached 240,162 cumulative instructions. Every checkpoint evaluated `ip+0` and `$insts$`, saved guest registers (`psr`, `rsc`, `bsp`, `bspst`, `rnat`, `unat`, `pfs`, `sp`, `gp`, `b0`, `r32`, `r33`, `r42`, and `r43`), dumped both 1,024-byte context records and the 352-byte probe event record, and guarded against guest exit. Breakpoints covered SSC, switch entry, `flushrs`, incoming BSPSTORE, B bootstrap, both return/resume paths, and the switch return.

The command file SHA-256 was `58c4643860eeb81f2f8d63616e12a78bf6d0ecb2e9711fd61b9f0703763aaafa`. The one-run authorization record is `/home/guidex/src/ia64-5ar4-coop-instruction-diagnostic-20261114-r1/authorization-consumed.json`; it records one launch, a one-CPU simulator, 2-GiB address-space ceiling, 20/22-second CPU soft/hard limits, 30-second wall limit, and `kernel_execution=false`. The pinned `bskinc` ran in system mode with `-noconsole -forcesystem -i`; stdin was an attested PTY slave at `/dev/pts/2`, and process-group cleanup completed.

The effective Ski command was `bskinc -stats -icnt <capture>/simulator/instruction-counts.txt -noconsole -forcesystem -i <capture>/debugger-commands.txt <verified ELF snapshot>`. The runner selected the snapshot only after verifying the pinned original ELF identity.

Before guest authorization, a no-ELF parser smoke exercised register evaluation, breakpoint, memory dump, guarded quit, and clean command-file exit. The preflight also checked the exact ELF/manifest/kernel/Ski hashes, loader ranges, static probe contract, and one-CPU command configuration. The single guest execution used the existing verified ELF; no rebuild or artifact replacement occurred.

## Captured execution

Exactly **one** cooperative guest execution occurred. No guideXOS kernel execution occurred. The preserved capture is `/home/guidex/src/ia64-5ar4-coop-instruction-diagnostic-20261114-r1/`.

Raw capture hashes: stdout `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` (0 bytes); stderr `178c03403bf17d4b9430c76400232c92c3b2ddda6173ec3800289267f8871db7`; Ski `-icnt` report `bd52588ca0b0392682bb3c5f2fdb6b4cd06657d1df53c17629bc559cf09b4e09`. The derived `analysis.json` was updated offline after implementing the matching classifier; this did not change raw streams or checkpoint captures.

The simulator exited 0 in 0.519 seconds after the final debugger `quit`; it did not time out, cleanup completed, and Ski reported zero faults. The runner labels the intentional debugger quit `SIMULATOR_EXIT_UNCLASSIFIED` because the guest did not emit the probe's normal completion record. Its compatibility `ski_instruction_count=512` field is the final `step 512` quantum, not a cumulative count; cumulative counts below come from Ski's `$insts$` evaluations. Captured simulator stdout is empty. The missing strict probe-output record is therefore expected for this stalled run and is not used as a root-cause signal.

| Observation | Captured evidence |
|---|---|
| Progress samples / register samples / context+event snapshots | 472 / 472 / 471; no parsing, freshness, or context/event snapshot errors. |
| Initial sample | 0 instructions, IIP `0x108000`, slot 0. |
| SSC breakpoint | 33 instructions, IIP `0x1080c0`, slot 2, IP `0x1080c8`; this is the slot-2 `break.i` stop. |
| After one SSC instruction step | 34 instructions, IIP/IP `0x1080d0`, slot 0; guest advanced past `break.i`. |
| BSS clear | IIP `0x108100` recurred at 261 checkpoints, then the trace left startup and entered setup. |
| First paired A/B contexts | Sample 264 at 134,178 instructions; both expected context identities are present, probe event count remains zero. |
| Post-setup checkpoints | 208 samples (indices 264–471), every IIP within setup `[0x10a2d0,0x10ab50)`, observed IIP range `0x10a2f0`–`0x10ab40`. |
| Stable branch register | `b0=0x10a2e0` at all 208 post-setup checkpoints. |
| Final observation | 240,162 instructions, IIP `0x10a360`, slot 2, IP `0x10a368`; `b0=0x10a2e0`. |
| Switch/RSE breakpoints | Zero hits for switch entry, `flushrs`, BSPSTORE handoff, B bootstrap, A/B continuations, and switch return. |
| Probe record | `event_count=0`, `errors=0` in captured snapshots; no A/B/A/B/A event. |

At the first sample with both initialized context records, Context A at `0x112600` has ID `0xA`, BSP/BSPSTORE `0x133000`, and zero switch/first-entry/resume counts. Context B at `0x112200` has ID `0xB`, first-entry flag 1, RP `0x10ae90`, RSC 3, BSP/BSPSTORE `0x143400`, and zero switch/first-entry/resume counts. These are the probe's setup records, not evidence of a context transfer.

The final guest register evaluation is:

| Register | Value | Register | Value |
|---|---:|---|---:|
| PSR | `0x2000` | RSC | `0x3` |
| BSP | `0x133030` | BSPSTORE | `0x133000` |
| RNAT | `0x0` | UNAT | `0x0` |
| PFS | `0x0` | SP | `0x122a10` |
| GP | `0x310000` | B0 | `0x10a2e0` |
| r32 / r33 | `0x60` / `0x108130` | r42 / r43 | `0x0` / `0x0` |

The underlying current register reads are Ski `eval` guest-register values. The bounded command file and parser smoke established the pinned command syntax; no application register outside the requested guest state was read.

## Root cause and scope

`ia64_coop_probe_setup` is a non-leaf routine. At `0x10a2dc` it calls `ia64_coop_set_nat8`; the call writes the setup routine's return continuation, `0x10a2e0`, into `b0`. The setup routine does not save and restore its caller's incoming `b0`. At `0x10ab4c`, `br.ret b0` therefore returns to `0x10a2e0`, inside the same setup routine, rather than to `ia64_coop_probe_run` at `0x10ae00`. The setup body rewrites the A/B setup records on each pass, so the event record stays empty and the first switch is never called. The trace's 208 setup-range observations and stable `b0` match this exact control-flow defect.

This is a concrete **probe bootstrap return-address defect**, not a demonstrated fault in `ia64_coop_context_switch` or its RSE algorithm. The guest reached its intended ELF entry, completed BSS clearing, hit and advanced past the SSC trap, populated the expected A/B setup records, then remained in the setup routine. That evidence rules against an entry/loader failure, a BSS-clear loop, and a console-init block as explanations for this run. The first switch call, outgoing arguments, saved PFS, `flushrs`, BSPSTORE handoff, Context B entry, resumed continuations, and all runtime preservation requirements remain unobserved.

The pre-existing static cooperative gate still passes for the exact artifact. It checks the four corrected call sites (`0x10b21c`, `0x10b8fc`, `0x10c81c`, `0x10cf16`) passing outgoing context pointers in `r42/r43` for the switch's `r32/r33` inputs, as well as context layout/alignment, separate stack and RSE ranges, B bootstrap and continuations, RSC/RNAT ordering, PFS/GP and ABI assumptions, FP images/caller-scratch policy, and absence of scheduler or interrupt enablement. These remain static checks. No runtime result from IA64-5AR4 qualifies any of them.

The derived `analysis.json` was reclassified offline using all saved progress, register, context, and event snapshots after the source/disassembly cross-check. Raw stdout/stderr and per-checkpoint files were not changed. The parser's strict completion-record validation still reports the expected missing probe sequence; the Outcome B classification comes from the instruction/state evidence instead.

## Verification

| Check | Result |
|---|---|
| Exact-artifact static preflight and system ELF gates | PASS; hashes, entry, load ranges, and cooperative static contract matched. |
| No-ELF Ski command parser smoke | PASS; pinned `bskinc`, PTY-backed, no guest ELF loaded. |
| Focused diagnostic negative/boundary tests | PASS, 14 tests. |
| Full IA-64 host contract and malformed-ELF suites | PASS, 117 tests in Linux/WSL after the localization-classifier changes. |
| FP code-generation harness | PASS; `/home/guidex/src/ia64-5ar4-fp-codegen-20261114-r3/report.md`. |
| Direct Mbed TLS profile verification | PASS; Mbed TLS 4.1.0, TF-PSA-Crypto 1.1.0, three patches. |
| Additional register-diagnostic guest run | Not needed; this run captured guest register state at every checkpoint, and preserved historical register-diagnostic artifacts were left unchanged. |
| Targeted shell syntax and Python AST checks | PASS for the cooperative build, FP harness, Linux runner, build wrapper, and diagnostic module. |
| `git diff --check` | PASS before local commit. |

The supplemental Windows-host discovery also ran 117 tests, with eight expected skips. Its only failure was the existing Linux-path assertion in `test_command_uses_headless_batch_console_and_explicit_system_mode` (`/opt/...` expected, `\opt\...` produced by Windows `Path`); the authoritative Linux/WSL discovery passed all 117 tests.

## Recommended next phase

Make a narrowly scoped probe-only change to preserve and restore setup's incoming `b0` across the `ia64_coop_set_nat8` call. Build a fresh isolated ELF and matching manifest; never overwrite the IA64-5AR3 artifact. Run the existing static checks and the diagnostic contract tests on that candidate, then request a separate one-run cooperative guest authorization. Keep guideXOS kernel execution out of scope. The context-switch implementation still requires a complete, independently validated A/B/A/B/A run before it can be qualified.
