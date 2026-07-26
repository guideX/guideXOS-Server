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
