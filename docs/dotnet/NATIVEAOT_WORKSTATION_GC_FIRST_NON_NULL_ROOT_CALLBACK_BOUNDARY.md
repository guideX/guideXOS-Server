# NativeAOT Workstation GC: managed ThreadStatic proof and first non-null root callback boundary

Date: 2026-08-09  
Outcome: **E — normal NativeAOT ThreadStatic initialization faulted before the managed proof assignment completed**  
Requested next marker: `C011EC06`  
Reached marker: **none**

## Result

This pass added the smallest managed proof needed to exercise the runtime's
normal NativeAOT thread-static semantics:

```csharp
[ThreadStatic]
private static byte[]? s_gcProofThreadRoot;
```

The workload assigns an existing live `byte[4096]` sentinel to that field and
later reads the field back through managed code. Native hooks record only the
assignment/readback evidence; they do not create a slot, object, root, or
candidate value.

The proof did not reach the assignment hook. Every fresh QEMU boot entered
`ManagedMain`, then faulted in the real NativeAOT
`GetInlinedThreadStaticBaseSlow` path while trying to initialize the
thread-static storage. Therefore this run proves neither a managed
thread-static root nor a non-null GC candidate. It is recorded as Outcome E,
not as an Outcome A/D candidate result.

## Checkpoint and identity

The actual starting committed HEAD was
`5ccd6c1d13359a5942e21bca6befe2218a5eeb89` on branch
`v1.1_DOTNET_SUPPORT`. The starting tree was already dirty with the prior
NativeAOT GC proof work; the complete starting `git status --short` snapshot
is preserved as `startingDirtyState` in the manifest. No commit, amend,
reset, discard, squash, or history rewrite was performed.

The previous checkpoint remains intact:

* `C011EC05` — one real candidate-slot machine-word load, before callback and
  semantic processing;
* report: `NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CANDIDATE_LOAD.md`;
* historical candidate evidence and manifests remain under
  `out/dotnet/gc-first-root-candidate-load/`.

The locked identity was unchanged: NativeAOT 9.0.0 AMD64, Workstation GC,
GC/EE interfaces `5.3 / 2`, locked source commit
`9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`, and active PAL archive
SHA-256 `C617D95647A20862947B52A1301DF96FE9104E5A13F11BB0C016B8370DDE115F`.

## Managed proof and normal runtime path

The proof-mode managed source assigns `s_gcProofThreadRoot` to sentinel ordinal
zero after the first allocation and performs one managed readback at iteration
40. The native diagnostics require exactly one assignment and one exact
readback before accepting a later root candidate. Those counters remained
zero because the first field access faulted before the assignment hook.

The generated NativeAOT map nevertheless confirms that the field was emitted
into the normal NativeAOT thread-static machinery, rather than being replaced
with a native test slot:

* `HostLogProof_HostLogProof_Program__ManagedMain` at `0x0000000010066C00`;
* `S_P_CoreLib_Internal_Runtime_ThreadStatics__GetInlinedThreadStaticBaseSlow`
  at `0x000000001008E270`;
* `__THREADSTATICINDEX@HostLogProof_HostLogProof_Program@@` at
  `0x00000000100C4CA8`;
* `tls_InlinedThreadStatics` at `0x000000001010A480`;
* `__ThreadStaticRegion` at `0x000000001011C0F0`.

The locked source trace explains the eventual GC boundary but it was not
reached in this pass:

* `nativeaot/Runtime/thread.cpp:1251-1261` returns the real
  `&m_pThreadLocalStatics` storage address and exposes the inline static list;
* `nativeaot/Runtime/gcenv.ee.cpp:94-133` enumerates inline thread-static
  roots, then the real `GetThreadStaticStorage()` slot, then thread roots;
* `nativeaot/Runtime/GcEnum.cpp:68-96` is the callback boundary where the
  bounded candidate-load instrumentation would execute.

The generated candidate source is retained at
`out/dotnet/gc-first-non-null-root-callback-boundary/build/runtime-pack/GcEnum.first-root-candidate-load.cpp`.
It contains the one explicit pointer-width load, but no candidate request was
observed because the managed field access failed first.

The inherited fault is now characterized and corrected through the real
NativeAOT module-startup contract. See
[NATIVEAOT_THREAD_STATIC_RUNTIME_SUPPORT.md](NATIVEAOT_THREAD_STATIC_RUNTIME_SUPPORT.md).
This historical Outcome E remains unchanged: the managed assignment did not
complete and `C011EC06` was not reached.

## Fresh QEMU evidence

The final evidence root is
`out/dotnet/gc-first-non-null-root-callback-boundary/run-20260809-132238954/`.
The harness rebuilt the specialized proof kernel, enabled QEMU
`-d int,guest_errors`, and ran three disposable single-vCPU QEMU boots.
QEMU was `11.0.0 (v11.0.0-12122-ga4bb4b10c9)`.

| Run | Result | Serial SHA-256 | Final fault RIP | Final CR2 |
|---|---|---|---|---|
| `first-run` | Outcome E | `DA19A9375F30699EB6E64BA53339E6CB31CA014333C80145A5FBB8F955626D06` | `0x000000001008E2BE` | `0x00000000FFFB5FF9` |
| `repeat-1` | Outcome E | `DA19A9375F30699EB6E64BA53339E6CB31CA014333C80145A5FBB8F955626D06` | `0x000000001008E2BE` | `0x00000000FFFB5FF9` |
| `repeat-2` | Outcome E | `DA19A9375F30699EB6E64BA53339E6CB31CA014333C80145A5FBB8F955626D06` | `0x000000001008E2BE` | `0x00000000FFFB5FF9` |

Each serial log contains:

```text
[nativeaot-gc-single-thread-suspend-ee] entering ManagedMain once
[PageFault] Not-present violation on read (kernel) at 0xFFFB5FF9
```

The corresponding QEMU instruction traces are `first-run/qemu-debug.log`,
`repeat-1/qemu-debug.log`, and `repeat-2/qemu-debug.log`. Their SHA-256 values
are recorded per run in `manifest.json`.

The final exception record identifies the same instruction in all three runs:

```text
IP=0008:000000001008e2be ... CR2=00000000fffb5ff9
RIP=000000001008e2be
```

The generated artifact disassembly identifies `0x1008E2BE` as the load inside
`S_P_CoreLib_Internal_Runtime_ThreadStatics__GetInlinedThreadStaticBaseSlow`
that dereferences the NativeAOT thread-static base metadata cell. No GC
provider hook, candidate load hook, callback, promotion, marking, restart, or
managed resume ran.

## Proof counters and boundary status

The machine-readable record is
`out/dotnet/gc-first-non-null-root-callback-boundary/run-20260809-132238954/manifest.json`.
The decisive fields are:

| Field | Recorded value |
|---|---|
| ManagedMain entry | `1` |
| Managed proof assignment count | `0` |
| Managed proof readback count | `0` |
| Candidate scan started | `false` |
| Candidate visits / null / non-null | `0 / 0 / 0` |
| Candidate machine-word loads | `0` |
| Callback / promotion / marking | `0 / 0 / 0` |
| `C011EC06` | not reached |
| Fabricated slot or object | `false / false` |

Because the field did not initialize, there is no candidate ABI or metadata
classification to report from this pass. In particular, the page-fault
address is not a GC candidate value and is not treated as one.

## Ordinary-kernel isolation and restoration

The specialized kernel SHA-256 was
`AA6F5CE7D0C2741811CA16000209E296B0C639A9A16C4DDCA816C8588719AEBC`.
The harness restored the ordinary kernel and ESP image after the run. Both
were restored to SHA-256
`D68791B66BF268425B6E646F3EA94CE7B3777B97D9CCED6682C10A9E1389066C`, as
recorded in `restored-normal-kernel.sha256` and `manifest.json`.

## Next smallest boundary

The next investigation is the legitimate NativeAOT thread-static startup
failure: establish why the normal generated metadata cell is still read as
`0x00000000FFFB5FE9` in this bare-metal runtime and make the smallest
runtime-correct initialization fix. That work must preserve normal
`[ThreadStatic]` semantics and rerun this exact proof. Until it reaches the
managed assignment/readback hooks and then a real provider load, no non-null
root, callback ABI, promotion, marking, restart, or resume claim is allowed.
