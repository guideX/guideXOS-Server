# REXX RX38 — Release Readiness

## Decision

**A — Core ready for a bounded guideXOS Server v0.3 integration profile.** The accepted interpreter core is suitable for local script execution, numeric and string work, control flow, routines, variable access, the VM-owned data stack, and the existing `KERNEL` command environment. RX38 makes no production changes and does not perform the integration.

The recommendation depends on keeping host-dependent services outside the v0.3 promise: no live `PULL` input provider, no production stream provider, no trusted wall-clock interpretation for current `DATE`/`TIME` operations, no `LINESIZE`, no `USERID`, and no interactive or clause-level TRACE. Those calls either fail closed or remain unsupported. They do not prevent ordinary scripts that stay within the accepted profile from executing.

The kernel shell's `SAY` writer has a visible layout limitation: it stores at most 119 payload bytes per row, flushes each callback independently, and the VM sends the final LF in a separate callback. The shell currently stores that LF as an empty row. Script text is emitted, but row wrapping and compact output are not yet an authoritative logical-column contract. The v0.3 integration should document this behavior and avoid promising `LINESIZE` or IBM terminal formatting. Treat a clean shared SAY/wrap policy as post-release integration debt.

This is a readiness recommendation only. No main-branch integration, package, or published release is claimed.

## Bounded v0.3 profile

- Run REXX programs through the existing guideXOS shell/CLI bridge and the production `KERNEL` command environment.
- Use the qualified language core, supported built-ins, and the active VM-owned data stack. `QUEUE`, `PUSH`, `PULL`, `PARSE PULL`, and `QUEUED()` preserve stack priority and accounting. When the stack is empty, production has no line reader and fails closed.
- Do not use REXX as a filesystem, device, network, or general stream API. Production installs no stream provider; `STREAM()` reports `UNAVAILABLE`, and `CHARIN()` fails closed.
- Do not use current wall-date/time operations as a trusted clock. `TIME('E')` and `TIME('R')` provide elapsed/reset timing; current wall forms report unavailable in production.
- Limit TRACE expectations to the qualified command-return actions `N`, `E`, `F`, and `O`. There is no interactive debugger or clause/expression trace stream.
- Treat `SAY` as visible shell output with current 119-byte row segmentation and the extra empty row. Do not promise column-aware wrapping, redirected-record behavior, resize updates, or `LINESIZE()`.
- Keep the production profile free of fixture-only providers. The RX31 input and RX34 stream fixtures are qualification devices, not Server services.

## Core interpreter readiness

| Area | Readiness and evidence |
|---|---|
| Expressions and execution | Hosted arithmetic, assignment, comparisons, IF/DO, bounded bytecode execution, resource limits, and shell integration passed in the RX37 hosted runner. RX37R1 audited production source blobs unchanged from RX37. |
| Numeric context | Decimal arithmetic, DIGITS/FORM context, rounding, conversion, inspection, and error cases have focused suites. The current RX37 run rebuilt and ran the inherited hosted targets. |
| Strings and binary values | Length-bearing byte strings, editing, conversion, binary bit operations, literals, and deterministic oracle/property cases passed. No ASCII/EBCDIC conversion is claimed; target-byte semantics and the 16 MiB runtime bounds apply. |
| Routines and INTERPRET | Internal calls, functions, PROCEDURE/EXPOSE, recursion, nested INTERPRET, and syntax/allocation recovery have accepted hosted suites. |
| PARSE | Argument and template parsing, PULL case behavior, binary bytes, and allocation/resource limits are covered. |
| SIGNAL and conditions | Condition traps, invalid arguments, nested recovery, and continued execution after errors passed the RX8–RX10 and later inherited suites. |
| Variables and VALUE | The portable active REXX variable pool is implemented and passed RX32 hosted and native guest checks. z/VM CMS selectors remain unsupported. |
| RANDOM | Per-VM seeded/reseeded behavior, invalid ranges, and a fixed-seed distribution check passed RX25; native guest evidence passed. |
| ADDRESS and TRACE | `KERNEL`/default environment state and host command callbacks are available. TRACE `N/E/F/O` command-return behavior passed RX24; other TRACE actions remain partial. |
| Data stack and PULL | RX30 model: 10,000 operations and 181,417 assertions; executable queue language: 1,200 model operations and 193 assertions. RX31 provider contract: 256 mixed operations and 169 assertions. Production empty-input behavior is fail-closed and has later native guest evidence. |
| Host callbacks and ownership | Input, stream, clock, command, output, and capability callbacks are per-VM where specified. Borrowed input is copied before parsing; provider contexts remain host-owned; callback replacement is quiescent; VM teardown does not free host context. Allocation-failure, multi-VM, recovery, and replacement paths are in hosted qualification. |
| Teardown and error recovery | No accepted evidence identifies memory corruption or a teardown defect. Allocation failpoints and post-error execution are covered by the hosted suites; RX35 also demonstrated native shell recovery after input and stream errors. |

Unsupported IBM/platform services are not interpreter-core failures. RX38 found no demonstrated P0 safety or essential-execution defect in the accepted bounded profile.

## Inventory and qualification boundary

The latest 75-entry inventory is `REXX_RX37_BUILTIN_INVENTORY.csv`, SHA-256 `cec3b7bef1a29a19e5263a7589f6770acf019c4cb72a2b204ab5100fa39b340e`. Its current classifications match the RX37R1 confirmation: **61 Implemented, 7 Not implemented, 5 Partial, 1 Not assessed, and 1 Not implemented by design**. RX38 changes no classification.

The full per-entry production/fixture boundary and incomplete-entry review are in [REXX_RX38_CAPABILITY_MATRIX.md](REXX_RX38_CAPABILITY_MATRIX.md). Open qualification obligations and their release priority are in [REXX_RX38_QUALIFICATION_DEBT.md](REXX_RX38_QUALIFICATION_DEBT.md).

## Regression and native evidence

Newly executed in RX38:

- `pwsh -NoLogo -NoProfile -ExecutionPolicy Bypass -File tests/run_rexx_tests.ps1 -Phase RX37` completed the current hosted regression phase. It rebuilt the core and the focused RX30, RX31, RX32, RX33, RX34, RX35, and RX37 targets; the hosted fixtures and artifact guards reported pass. CLI results were 28/28; RX9 conditions 24/24; RX10 INTERPRET 19/19; RX12–RX18 focused fixtures and RX22/RX26–RX35 fixtures passed. Regina comparisons remained `NOT_RUN` because Regina was unavailable.
- The just-built RX37 output-capability executable was run directly: **4,203 assertions, 0 failures, exit 0** (107 direct assertions and 4,096 property cases).
- The current run's artifact guard reported all 31 protected root `.obj`/`.pdb` artifacts unchanged at its hosted-build, hosted-fixture, and final gates. RX38 independently rehashed all 31 against its starting path/size/SHA-256 manifest; all match.

ASan and UBSan were not rerun because RX37R1 changed documentation only and audited the production source blobs against RX37. The accepted RX37 source-revision evidence remains **36/36 targets under ASan and 36/36 under UBSan**. The accepted freestanding AMD64 build remains current to production source: kernel ELF SHA-256 `11aa057a68734c8ec39defac19b9a1310ab7f1d7c946f6f64ef8d22f92bfe6a8`. It was reviewed, not rebuilt in RX38. Detailed native evidence and known unverified checks are recorded in `evidence/rx38/native-evidence-audit.md`.

RX31's formal native acceptance remains open for an injected input reader. RX32, RX33, and RX35 later guests directly demonstrated production empty-input fail-closed behavior and recovery, but did not exercise an injected native provider. RX37R1's native output-capability query proof also remains open; ordinary production SAY does not call that optional query. Neither obligation blocks the bounded profile above.

## QAC and preservation

QAC status and list were checked read-only at the beginning and end of RX38. At the final snapshot the coordinator was online with zero managed reservations, sessions, queued requests, or blocked requests, and it reported two unmanaged/foreign QEMU processes (PIDs 20392 and 21932). RX38 did not launch a guest or control either process. The process snapshot and both QAC outputs are under `docs/rexx/evidence/rx38/`.

All 141,969 starting untracked paths remain present. The exact starting set matches the RX37R1 recorded path set; RX38's current baseline is serialized as an ordinal-sorted UTF-8/LF manifest and gzip-compressed. The final reconciliation records every additional path and is refreshed after staging the RX38 report set, so its ending manifest matches the post-commit untracked set.

## Next step

Use RX39 for a bounded v0.3 integration and acceptance profile. Keep it to wiring the already-qualified interpreter into the Server build, verifying the production `KERNEL` bridge and documented output/input/clock boundaries, and recording guest markers. Do not add a filesystem, Console input, clock policy, debugger, stream-I/O, or graphics subsystem. Revisit the small production SAY row/newline behavior and native output-capability query only if the v0.3 product contract requires those semantics.
