# NativeAOT Workstation GC startup readiness

Status: 2026-07-25. PAL bridge readiness and the first Workstation GC
initialization-only dry run pass. Full orderly same-process GC shutdown is not
available in the locked NativeAOT source; the QEMU runs therefore use
disposable processes and never signal the finalizer event.

Evidence: `out/dotnet/pal-win64-qemu-bridge/` and
`out/dotnet/gc-initialization-dry-run/`. See also
[NATIVEAOT_PAL_WIN64_QEMU_BRIDGE.md](NATIVEAOT_PAL_WIN64_QEMU_BRIDGE.md) and
[NATIVEAOT_WORKSTATION_GC_FEASIBILITY.md](NATIVEAOT_WORKSTATION_GC_FEASIBILITY.md).

| Required readiness item | Current status |
| --- | --- |
| Active PAL archive replacement | PASS |
| Exact symbol parity | PASS, 6/6, 38/38, 74/74, 3/3 |
| Replacement Windows imports | PASS, 0 |
| Win64 PAL hook-table versioning | PASS, ABI v1, 232 bytes |
| SysV hook implementation | PASS |
| SysV-to-Win64 callback bridge | PASS |
| Worker lifecycle bridge | PASS |
| FLS detach-callback bridge | PASS |
| Stack-bound bridge | PASS |
| ThreadStore bridge | PASS |
| Exact hosted PAL probe | PASS |
| Server PE-to-ELF PAL probe | PASS |
| System-QEMU exact PAL probe | PASS |
| HostLog | PASS |
| Managed allocation proofs | PASS, 234 / 14 and controlled OOM |
| Workstation GC initialization-only probe | PASS, `RhInitialize` returned 0 |
| Workstation GC orderly shutdown | UNSUPPORTED by locked source contract |
| Collections | 0 |
| GC-backed allocations | 0 |
| Heap expansion | 0 |

The startup QEMU matrix reports PASS for first, repeat, and fresh disposable
processes. The startup platform extension is ABI v1, 216 bytes, capability
mask `0x7`; the PAL table is ABI v1, 232 bytes, capability mask `0x1FF`.

`RhInitialize` was called only in the startup-only probe, once per disposable
QEMU process. No managed entry point, collector allocation, collection, or
managed finalizer callback was entered. The NativeAOT helper creation request
is kept parked and cleanup is process-lifetime only.

Decision: **Outcome A - Win64 PAL hook-table and system-QEMU bridge complete.**

The next experiment requires a separately defined and validated GC shutdown
boundary before any managed execution or collection experiment is authorized.
