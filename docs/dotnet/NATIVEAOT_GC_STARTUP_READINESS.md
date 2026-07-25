# NativeAOT Workstation GC Startup Readiness

## Current gate — 2026-07-24

Authoritative implementation status: [NativeAOT PAL/runtime replacement](NATIVEAOT_PAL_RUNTIME_REPLACEMENT.md).
ABI and system-QEMU evidence: [NativeAOT PAL Win64/QEMU bridge](NATIVEAOT_PAL_WIN64_QEMU_BRIDGE.md).
Machine evidence: `out/dotnet/pal-win64-qemu-bridge/artifact/` and its
`qemu/smoke-*/qemu-validation-matrix.json`.

The PAL bridge readiness gate passes. The collector itself was not started:
`RhInitialize` was not called, the real Workstation GC heap was not
constructed, the real finalizer/helper thread was not started, no collection
was entered, and no allocation used the collector.

| Required readiness item | Current status |
| --- | --- |
| Active PAL archive replacement | PASS, four objects together |
| Exact symbol parity | PASS, 6/6, 38/38, 74/74, 3/3; missing/unexpected/duplicate 0 |
| Replacement Windows imports | PASS, 0 for all four replacements |
| Win64 PAL hook-table versioning | PASS, ABI v1, 232 bytes, magic/version/size validation |
| SysV hook implementation | PASS |
| SysV-to-Win64 callback bridge | PASS, standalone ABI probe and QEMU worker callback |
| Worker lifecycle bridge | PASS, opaque generation-checked handle, join, cleanup |
| FLS detach-callback bridge | PASS, exact value and one callback |
| Stack-bound bridge | PASS, initial and worker exact bounds/current pointer |
| ThreadStore bridge | PASS, attach/lookup/detach and baseline restoration |
| Exact hosted PAL probe | PASS, two launches |
| Server PE-to-ELF PAL probe | PASS, repeat and fresh-process launches |
| System-QEMU exact PAL probe | PASS, first/repeat/fresh-process launches |
| HostLog | PASS, exact message, one callback, return 0 |
| Managed allocation proofs | PASS, 234 at 64 KiB; 14 at 4 KiB; controlled OOM |
| Collections | 0 |
| GC-backed allocations | 0 |
| Heap expansion | 0 |
| Generic scheduler/events/native threads | PASS |
| Generic VM/true-QEMU VM | PASS |
| Generic mutex/local-storage/FLS/stack bounds/ThreadStore | PASS |
| Windows PAL thunk entered by exact QEMU probe | NO |

The active archive hash is
`5593D0FC4B99986797123C8494DF117570DB795DF8FCE63D732BB53594C794BF`.
The pre-bridge baseline hash is preserved in
`out/dotnet/pal-win64-qemu-bridge/baseline/`.

## Stop rule

This gate authorizes only the next separately gated experiment. It does not
authorize ordinary boots to enable the QEMU test mode, and it does not claim
live GC startup, collection-safe suspension, root enumeration, write-barrier
publication, or finalizer behavior.

## Decision and exact next experiment

Decision: **Outcome A — Win64 PAL hook-table and system-QEMU bridge complete.**

The exact next experiment is the first Workstation GC
initialization-and-shutdown dry run, with its own explicit opt-in artifact,
serial markers, timeout, cleanup, and no-collection fallback checks. Do not
run that experiment as part of the PAL bridge validation.
