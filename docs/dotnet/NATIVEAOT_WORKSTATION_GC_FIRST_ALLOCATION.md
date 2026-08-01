# NativeAOT Workstation GC first real allocation

Status: 2026-07-26. This is the authorized disposable-QEMU experiment for one
managed `byte[24]` through the adapted real Workstation GC. It follows the
startup-only pass and preserves the process-lifetime shutdown boundary.

## Decision

| Gate | Result |
| --- | --- |
| Adapted Workstation GC identity | **Identity B**: normalized fresh builds match; raw archive bytes are not a stable identity, and the one historical normalized semantic difference is the reviewed QEMU virtual-memory range expansion |
| Hosted exact-symbol/import probe | **PASS** |
| Startup-only Workstation GC in fresh QEMU | **PASS**, `RhInitialize` returned 0 |
| First managed entry | **PASS**, entered once in each of `first`, `repeat`, and `fresh` |
| First real `byte[24]` allocation | **FAIL/HANG**, stock `RhpNewArray` path did not return before timeout |
| Object layout/zero/pattern/ownership proof | **NOT ESTABLISHED**, no post-allocation diagnostics returned |
| Same-process shutdown/reinitialization | **UNSUPPORTED**, Model C process lifetime |

Overall result: **Outcome C for real managed allocation**. Startup-only
readiness remains Outcome B, and the historical bounded image-backed
no-collection proofs remain valid as separate experiments. Neither result is a
claim that the real collector allocation succeeded.

## Identity resolution

The locked runtime-pack identity is Workstation GC, NativeAOT 9.0.0, source
commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`, GC interface 5.3, EE 2, with
server/background/concurrent modes disabled.

The comparison at
`out/dotnet/gc-first-real-allocation/identity/comparison/normalized-comparison.json`
records:

- two clean fresh rebuilds with zero normalized member, order, and symbol
  differences;
- raw archive differences caused by COFF timestamps, embedded debug paths,
  checksum records, and archive index metadata;
- one historical normalized semantic difference,
  `guidexos_virtual_memory_region.obj`, matching the reviewed QEMU range
  expansion from 64 MiB/1,024 pages to 128 MiB/32,768 pages for the startup
  test.

The final hosted probe log is
`out/dotnet/gc-first-real-allocation/identity/exact-symbol-probe.log` and
reports `Exact-symbol hosted probe: PASS`.

## Real-allocation image

The managed body contains one allocation:

```csharp
byte[] value = new byte[24];
```

It checks all 24 bytes for zero initialization, writes a deterministic pattern,
verifies the pattern, keeps the array live, and returns. The native runtime
wrapper does not seed the EE allocation context or replace it with the old
image-backed bounded heap. It records the context before and after the stock
`RhpNewArray` call and is intended to validate the real GC heap owner.

The expected historical layout, re-used as an explicit check, is:

| Field | Expected value |
| --- | --- |
| Object alignment | 8 bytes |
| EEType | object + `0x00` |
| Array length | object + `0x08`, value 24 |
| Array data | object + `0x10` |
| Base size | `0x18` |
| Total aligned object size | `0x28` / 40 bytes |

No field in this table is reported as revalidated by the failed run: the stock
allocation path did not return to the wrapper’s diagnostics finalizer.

## QEMU result

The first-allocation kernel was built with the final embedded ELF and executed
in three fresh disposable QEMU processes. Each serial log contains:

```text
RhInitialize return=00000000 state=02000012
entering ManagedMain once
```

No run produced `managedStatus`, `allocationSucceeded`, or `ALL_PASS` before
the bounded 20-second timeout. The three logs are:

- `out/dotnet/gc-first-real-allocation/managed-authorized/qemu-final/first/serial.log`
- `out/dotnet/gc-first-real-allocation/managed-authorized/qemu-final/repeat/serial.log`
- `out/dotnet/gc-first-real-allocation/managed-authorized/qemu-final/fresh/serial.log`

The matrix and runner manifest are in the same `qemu-final` directory. The
startup state shows the finalizer worker parked before managed entry. No managed
finalizer callback, second `RhInitialize`, collection request, or shutdown call
was made. Because the allocation call did not return, the run does not claim
zero collections or a successful no-finalizer allocation; those checks remain
unproven for the real path.

## Artifact hashes

| Artifact | SHA-256 |
| --- | --- |
| Real-GC runtime archive | `A36FCA208A5910ACFF11B521C231C99FFE0872BF0A3A9DB09A6DCF9ED48CAF3D` |
| NativeAOT managed PE | `47ACD0179F25F3A4C0D39314E3F73D0629325D4528B92198A710845DDCA7D743` |
| Converted managed ELF | `047572BB975DC89FA8B53C7E8253770BED852EF7B44A42B3F60DA77554E94D00` |
| Embedded artifact object | `6B9D48B60F93659460AF2E5BECE1E51E722560951FBD7CDF3DAC75EC5275A25C` |
| First-allocation kernel | `5EA55AA12A176605C88932482922BD58F5A6C7431901E7E3FFEA78A3FE7551DA` |

The first-allocation kernel is preserved under
`out/dotnet/gc-first-real-allocation/managed-authorized/kernel-build-first-final`;
the normal ignored `kernel/build/amd64` output was restored to its prior
startup artifact.

## Boundary and next action

This result does not authorize a normal managed runtime, a second allocation,
`GC.Collect`, finalizer signaling, same-process reinitialization, or live
shutdown. Further work is diagnosis of the collector/PAL boundary in a new
disposable process, retaining the same identity and shutdown gates.

## Follow-up: exact hash-specific diagnosis and correction

The original Outcome C evidence above is intentionally retained. The immutable
PE/ELF baseline was preserved under
`out/dotnet/gc-first-allocation-hang/baseline/` and confirmed in a fresh QEMU
process. The live GDB capture for that exact hash resolves the terminal
boundary as the `RhpReversePInvoke` current-thread TLS guard: `gs_base=0`, RIP
`0x10001CA7`, and a terminal `guideXosFailFast+0x7` self-loop with interrupts
enabled. It was therefore a terminal fail-fast spin, not a Workstation heap
lock or Event wait. The earlier coarse serial description remains part of the
original record; the exact RIP capture is the stronger boundary evidence.

The correction installed the current-thread NativeAOT TLS vector/runtime cell,
mapped the zero-raw-size `hydrated` PE section in the PE-to-ELF converter, and
enabled the matching source-backed `RehydrateData` call before managed entry.
The diagnostic validator was also corrected to use the real `byte[24]` object
size of `0x30` (48) and valid segment bounds. No collector lock, collection,
helper signal, or old no-collection heap was bypassed.

The final corrected artifact is recorded in
[NATIVEAOT_WORKSTATION_GC_FIRST_ALLOCATION_HANG.md](NATIVEAOT_WORKSTATION_GC_FIRST_ALLOCATION_HANG.md).
It passed exactly one real collector-backed `byte[24]` allocation in the first
corrected process and in two additional fresh processes, with non-null object,
length 24, 24 zero bytes, pattern and heap ownership all passing. Collection
and finalization counters remained zero. The follow-up decision is **Outcome A**;
the original Outcome C result is not deleted or rewritten.

The later verification report supersedes that follow-up's completion status:
the core allocation remains valid, but the authoritative verification decision
is **Outcome B** until the explicitly listed regression, harness, and evidence
tracking closure items are complete. No repeated Workstation-GC allocation is
authorized by that verification.

## Final closure validation — 2026-08-01

The historical hang and follow-up sections above are retained as the record of
the staged investigation. The authoritative core implementation is commit
`95580df6872e85527d27526b78ae3cdcee25dd53`. The original hang was a terminal
fail-fast loop; GS/TLS was the first blocker, followed by PE-to-ELF zero-fill
mapping and NativeAOT metadata hydration. The corrected path then completed one
real Workstation-GC-backed `byte[24]` allocation.

The dedicated native-thread, true-VM, local-storage/FLS, PAL system-QEMU,
stack-bounds, FLS-before-initialization, process-teardown-policy, managed
baseline, hosted generic, and focused PE-to-ELF closure checks completed. The
allocation-specific runner now enforces the matching Makefile selectors,
`EXTRA_CFLAGS` definitions, fresh evidence, allocation markers, and ordinary
kernel restoration. Generated evidence is ignored and untracked while remaining
available locally; human-authored documentation remains under `docs/`.

The final immutable run used one fresh disposable QEMU process and exactly one
collector-backed `byte[24]`. The returned object was `0x100A00028`; its size was
source-derived as `ALIGN_UP(baseSize + componentSize * length,
pointerAlignment) = 0x30` (48 bytes), with 24 initial zero bytes, pattern PASS,
and GC ownership PASS. Managed entry, `RhpNewArray`, and real GC allocation
counters were each 1. Collection and finalization counters were each 0.
Process teardown passed; runtime-level GC shutdown remains NOT SUPPORTED.

The final decision is **Closure Outcome A — First collector-backed allocation
milestone fully closed**. This authorizes bounded primitive-array allocations
through Workstation GC until the first subsequent allocation-context refill,
without allowing collection. Repeated allocations were not started during this
closure pass.

## Follow-up: first subsequent allocation-context refill

The historical one-allocation and hang/correction records above are preserved.
The bounded repeated-allocation authorization was exercised in the separate
[NATIVEAOT_WORKSTATION_GC_FIRST_REFILL.md](NATIVEAOT_WORKSTATION_GC_FIRST_REFILL.md)
report. It used three fresh QEMU processes and 15 fresh `byte[256]` arrays per
process, proving the initial collector context, 13 fast allocations, and the
first later refill. The refill-2 object passed zeroing, pattern, layout,
primitive-array, overlap, monotonicity, context-geometry, and GC-ownership
validation; no collection or finalizer ran. The stop boundary was enforced
before any allocation after that object. Runtime-level GC shutdown remains
unsupported and the ordinary kernel is restored after each run.
