# IA64-5AR1 Cooperative Switch Stall Attribution

**Outcome B — `IA64_COOPERATIVE_STALL_ATTRIBUTED` (2026-11-13).** Static target-code inspection proves that the original probe passed its two `Context*` arguments in the wrong registers: the A/B callers wrote local registers `r36/r37`, while the switch reads callee inputs `r32/r33`. The caller frames reserved no output registers. The callers are corrected and the new ELF passes all static gates. The one authorized interactive Ski run stopped during SSC console initialization, before reaching the switch, so the original timeout's exact dynamic failure point and the complete switching contract remain unqualified.

No guideXOS kernel, scheduler, timer, external interrupt, paging, user-mode, or device path was run or activated.

## Repository and preserved artifacts

- Branch: `IA64_SUPPORT`; starting HEAD: `ac506cbc35de28e1122ec9bdace789bed4df28d4`; `origin/IA64_SUPPORT`: `2640e00b40353f2094f87b502d76d01661399f12`; the branch was one commit ahead at start.
- The tracked tree was clean at start. Existing untracked Python caches were preserved and remain untracked. No reset, clean, merge, rebase, pull, or push was performed.
- The original failed probe ELF SHA-256 remains `9189f5c26b819016dd0bd5f663f201d30cdcfb58fc7ad9046381806894bb4b45`; its manifest SHA-256 remains `000a98cebc36a28dd4d5afa81e6faf2a29b0028f4a40a4793af89bf90f7d8f0c`.
- The accepted IA64-4G kernel remains `ab13fabb6a92ebe039afa2afd9729f6f6ceb2529b63be2b47b0c87cf264d960c`.
- Pinned Ski identity: version 1.5.1, source commit `dfc2902ea1423d9b32543a5daf8026213f2b37a1`, executable SHA-256 `ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45`.
- Fresh standalone probe: `/home/guidex/src/ia64-5ar1-coop-probe-20261113-r1/ia64-cooperative-context-probe.elf`, SHA-256 `76c03f06a33bf219b2aefee2a7cf17fbc3ac31837e4aed592484a562cbfd8cfa`. Its fresh manifest and disassembly are beside the ELF.

## Original timeout reconstruction

The preserved result at `/home/guidex/src/ia64-5a-coop-probe-20261112-r8/capture/run-result.json` records this launch:

```text
/home/guidex/opt/ski-dfc2902/bin/bskinc -stats -icnt /home/guidex/src/ia64-5a-coop-probe-20261112-r8/capture/instruction-counts.txt -forcesystem /tmp/guidexos-ia64-ski-elf-hi8ggm9u/selected.elf
```

The runner's selected ELF hash matches the preserved original probe hash above. `stdout.log` and `stderr.log` are both zero bytes (SHA-256 `e3b0c442...b855`). The requested `instruction-counts.txt` is absent, and the result has no instruction count, fault count, transition log, or CPU-state record. The result classifies `COOPERATIVE_CONTEXT_PROBE_FAIL`, records `timed_out=true`, elapsed time `60.002` seconds, exit code `-15`, and `cleanup_complete=true`. The `-stats` summary and instruction-count file were not flushed on forced termination.

The Linux runner starts Ski in a new process session and signals its process group on timeout, escalating to SIGKILL if needed. The preserved result does not record the original PID/PGID or group-member inventory; `cleanup_complete` establishes the runner's cleanup result, but cannot reconstruct whether Ski was executing guest code, waiting inside the simulator, or blocked on host I/O at the deadline. The exact original stop PC and instruction count remain unknown.

The zero-byte console files do not localize the original run. The console implementation emits bytes through SSC calls without stdio buffering, while the probe's first progress text is printed only after the A/B sequence returns. A pre-return stall therefore naturally produces no transition text.

## Static control-flow and ABI finding

The target disassembly confirms the entry setup: `_start=0x108000`, IVT base `0x100000`, GP `0x310000`, initial memory SP `0x122a10`, RSC set to lazy mode before `loadrs`, then BSPSTORE `0x133000` and RSC `3`. Static ranges are non-overlapping: Context B `0x112200`, Context A `0x112600`; A stack `[0x112a10,0x122a10)`, B stack `[0x122c10,0x132c10)`, A RBS `[0x133000,0x143000)`, and B RBS `[0x143400,0x153400)`. Text is `[0x108000,0x10e1d0)`.

The switch interface is a normal IA-64 procedure call. `context_switch.s` reads `in0` and `in1`, assembled as `r32` and `r33`. Before this change, both `ia64_coop_a_test` and `ia64_coop_b_test` used `alloc r16=ar.pfs,6,4,0,0` and placed the pointers in local registers `r36/r37`. With zero output registers, those values did not populate the caller's output area, so the switch received stale/uninitialized input pointers. This is a concrete source and ABI defect that would prevent a valid first context handoff if execution reached the call.

The narrow correction changes both frames to `alloc r16=ar.pfs,6,4,2,0` and writes every context argument pair through `out0/out1`. The new disassembly shows those operands mapped to caller registers `r42/r43`; the switch still reads its corresponding `r32/r33` inputs. The focused contract test checks both frames and all four A/B call pairs.

The four switch call instructions are at `0x10b21c`, `0x10b8fc`, `0x10c81c`, and `0x10cf16`. Context B's fresh bootstrap starts at `0x10ae90`; its bootstrap/resume distinction is checked through the context's first-entry flag and PFS contract. A's first post-call continuation starts at `0x10b220`. The switch's saved-state/RSE path includes `flushrs` at `0x10dc86`, RSC freeze at `0x10dc96`, `invala` at `0x10ddf0`, incoming BSPSTORE at `0x10de16`, final incoming RSC at `0x10e180`, and `br.ret` at `0x10e19c`. Static checks verify stack/RBS bounds, continuations, register ownership, RNAT validity, and the RSE transition order. The sequence follows Intel's synchronous backing-store switching requirements in Volume 2 §6.11.3.

## Ski debugger capabilities and the single controlled execution

The pinned source and installed manual confirm interactive command-file input (`-i`), finite `step <count>`, exact-address breakpoints (`bs`), register dumps (`rd`), memory dumps (`dd`), expression evaluation (`eval`), simulator-state save (`save`), and `quit`. `$insts$` is available as an expression. Interactive `ski` does not enable the batch-only `-stats`/`-icnt` flags; those were not supplied. The separate `bskinc` executable is batch mode and does not provide the interactive inspection path.

Exactly **one** new cooperative-probe guest execution was authorized and launched. It used the fresh ELF, pinned Ski, `-forcesystem`, a PTY, and a command file with twenty finite `step 10000` commands (200,000 instructions maximum). The supervising runner recorded PID/PGID 589, a 35-second wall bound, 25/27-second soft/hard CPU bounds, a 2-GiB address-space limit, and completed process-group cleanup. The preserved capture is `/home/guidex/src/ia64-5ar1-coop-probe-20261113-r1/debugger-run/`.

The run reached ELF entry (`0x108000`) and executed 34 guest instructions with zero Ski faults. At the first SSC console initialization (`r15=0x14`, `SSC_CONSOLE_INIT`), Ski printed `Unable to create console window`. Pinned Ski source shows the interactive path's `SSC_CONSOLE_INIT` calls `initConsole()` to create an xterm console; that host operation failed. The captured register dump is at the bundle address `0x1080c0`, containing the `break.i 0x80000` call at `0x1080cc`, before `ia64_coop_probe_run` at `0x108126`. Snapshot values include `SP=0x122a10`, `GP=0x310000`, `RSC=3`, `BSP=BSPSTORE=0x133000`, `RNAT=0`, and `r15=0x14`. No switch call, context save, RBS handoff, or Context B instruction was reached.

After Ski reported that console failure, the next finite step returned `Nothing to run`; the command file stopped before the context-memory dump and state save. The runner remained in its interactive command loop until its wall limit, then terminated and reaped the process group. The result records 35.03 seconds elapsed, `timed_out=true`, exit code 1, and `cleanup_complete=true`. This is a debugger/console startup limitation, not runtime evidence for or against the corrected switch. The run was preserved and **not retried**.

| Frontier | Observation |
|---|---|
| A — ELF entry | Proven at `0x108000` in the initial register dump |
| B — Context A initialized, switch about to begin | Not reached; startup stopped at SSC console initialization before `ia64_coop_probe_run` |
| C — Context A saved | Not reached |
| D — RBS ownership transferred toward B | Not reached |
| E — first Context B instruction | Not reached |
| F — first B-to-A return | Not reached |

## Validation and execution counts

| Check | Result |
|---|---|
| Fresh probe build and cooperative static gate | PASS; new ELF hash recorded above |
| Cooperative contract tests | PASS, 12 tests, including the new A/B output-register contract |
| Complete IA-64 contract suite | PASS, 96 tests; 5 Linux-only skips on Windows |
| Malformed ELF/IVT tests | PASS, 3 negative cases |
| FP code-generation harness | PASS; report in `/home/guidex/src/ia64-5ar1-fp-codegen-20261113-r1/report.md` |
| Independent Ski register diagnostics | PASS; legacy expected corruption and fixed output both reproduced; IVA/BSPSTORE/RSC/PSR checks and FPSR readback passed, RNAT remains unverified |
| Direct Mbed TLS profile verification | PASS; Mbed TLS 4.1.0, TF-PSA-Crypto 1.1.0, 3 patches |
| Targeted shell syntax checks and `git diff --check` | PASS |

This phase launched three Ski guest ELFs: one cooperative probe (34 instructions before console initialization exited) and two independent register diagnostic ELFs. No kernel ELF was run. Including the three guest ELF executions preserved from the preceding IA64-5A phase, the two phases together account for six guest ELF launches: two cooperative probes and four register diagnostics.

## Qualification and IA64-5AR2 recommendation

The implementation-level cause is established and corrected, but the cooperative contract is still unqualified. The original batch timeout cannot be assigned an exact PC because its capture lacks instruction counts and registers. The fresh interactive attempt cannot validate the correction because its console initialization failed before the first switch. Do not integrate this mechanism into guideXOS scheduling.

The next minimal phase should first provide a supported interactive Ski console environment that allows `SSC_CONSOLE_INIT` to complete, without modifying the pinned simulator. Then, under a new one-run authorization, inspect the corrected probe at the first A-to-B boundary and validate the complete A/B/A/B/A sequence, stack and RBS ownership, NaT/RNAT, PFS, preserved registers/FP state, and continuation addresses. If that debugger environment cannot be provided, define a bounded, batch-compatible checkpoint method before asking for another guest execution.

## Architectural reference

- Intel, [Itanium Architecture Software Developer's Manual, Volume 2](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/itanium-architecture-software-developer-rev-2-3-vol-2-manual.pdf), §6.11.3, synchronous backing-store switching and RSE state rules.
- Intel, [IA-64 Software Conventions and Runtime Architecture Guide](https://portal.cs.umbc.edu/help/architecture/24525601.pdf), §10.2, non-preemptive thread-switching conventions.
