# NativeAOT reverse-P/Invoke entry diagnosis

## 1. Executive summary

The clean artifact does not enter a different managed symbol. Its ELF entry is the actual NativeAOT-generated `ManagedMain` body at `0x10001900`. The body immediately calls `RhpReversePInvoke` at `0x1004B140`, which transfers to `RhpReversePInvokeAttachOrTrapThread2` at `0x1004B1A0` and then to `ThreadStore::AttachCurrentThread` at `0x1004EBD0`.

The first invalid operation is:

```text
0x1004ECAF: FF 15 53 34 00 00    call qword ptr [0x10052108]
             __imp_FlsGetValue
IAT contents: 44 DC 08 00 00 00 00 00
attempted target: 0x000000000008DC44
```

The process raises `0xC0000005` with `RIP=0x8DC44`. This is an invalid call target, not a null read or null write. The host callback is never entered.

The surviving historical ELF and the current clean ELF have the same entry, generated body, NativeAOT reverse-P/Invoke chain, TLS directory, imports, and seven-segment fixed-base envelope. The historical PE, map, response files, NativeAOT server executable, and complete successful-build disassembly did not survive. A later clean artifact (`C413...`) replayed successfully twice with an earlier Server executable, but the successful Server executable was not preserved; after a later Server rebuild the same frozen ELF failed at the same transition boundary. This prevents a reproducible success claim.

Decision: **Outcome C - Custom runtime pack required.** The stock Windows NativeAOT reverse-P/Invoke machinery reaches FLS, thread-store, runtime-initialization, and transition-frame state that the current TLS envelope does not provide. This pass does not patch the thunk or fabricate that state.

## 2. Historical successful execution

The historical staged ELF is preserved read-only at:

`out/dotnet/reverse-pinvoke-comparison/successful/staged/apps/ManagedHostLogProof/bin/amd64/HostLogProof.elf`

SHA-256:

`CDE78AD872795394D212E3AE0FCC9863B643C5D0B9414DAC4302D3A1A3AE0BF5`

Surviving logs report two launches in one Server process, one host log call per launch, return code `0`, entry `0x10001900`, preferred-base mapping, and no native fallback message. Those logs are historical evidence only. They do not identify the reverse-P/Invoke state at entry and do not prove that the stock transition was runtime-correct.

The successful ELF itself is reconstructable. The complete successful toolchain set and the successful Server executable are not.

## 3. Clean-build regression

A clean NativeAOT build and clean stage completed successfully statically. The frozen failing artifact is preserved read-only at:

`out/dotnet/reverse-pinvoke-comparison/failing/clean-current/artifacts/HostLogProof.elf`

SHA-256:

`26D973C02B969B1F0CD84F710315433B7423627D1332D5F228EB295B00F73287`

The source and staged clean ELF hashes are equal. The live positive smoke exits with Windows exception `0xC0000005` before the first host log call. The first launch therefore fails; second-launch and cross-process success requirements are not met for the clean current path.

## 4. Exact fault location

The latest clean failing capture is archived under `failing/clean-current-server-rebuilt-20260713-062415/` and reports:

```text
code=0xc0000005
faultAddress=0x8dc44
rip=0x8dc44
rsp=0x3bbd9fdd58
rspMod16=0x8
rbp=0x3bbd9fde10
rax=0x0
rbx=0x17ce71f5080
rcx=0xffffffff
rdx=0x7
r8=0x17ce71f5080
r9=0x17ce7627870
fsSelector=0x53
gsSelector=0x2b
entry=0x10001900
mappedBase=0x10000000
tlsIndexAddress=0x100982ac
tlsBlock=0x17ce75200b0
tlsBlockSize=0x110
tlsSlot=0x7
info0=0x8
info1=0x8dc44
ctx.size=0x18
ctx.apiVersion=0x0
ctx.host=0x3bbd9feb00
ctx.userData=0x0
```

The last successfully entered symbols are `RhpReversePInvoke`, `RhpReversePInvokeAttachOrTrapThread2`, and `ThreadStore::AttachCurrentThread`. The invalid instruction is the indirect FLS call at `0x1004ECAF`; the faulting instruction pointer is the unresolved target `0x8DC44`.

The preceding `GS:[0x58]` TLS-vector load succeeds. The fault is therefore not the first TLS-vector read. It is an invalid execute/call target caused by the unresolved Windows import address.

## 5. Successful/failing artifact comparison

| Property | Historical successful run | Archived clean pass | Clean failing run | Significance |
| --- | --- | --- | --- | --- |
| ELF SHA-256 | `CDE78...AE0BF5` | `C41342...544AF45` | `26D973...0F73287` | Historical and generated artifacts are distinct files. |
| ELF entry | `0x10001900` | `0x10001900` | `0x10001900` | No entry-address drift. |
| Entry category | Not recorded | `ManagedMain` body requiring reverse-P/Invoke | `ManagedMain` body requiring reverse-P/Invoke | No evidence of a direct-body bypass. |
| PT_LOAD count | 7 | 7 | 7 | Fixed-base envelope is present in all surviving ELF artifacts. |
| Image size | `755200` | `755200` | `755200` | Same mapped image geometry. |
| Preferred mapping | `0x10000000` | `0x10000000` | `0x10000000` | Mapping is not the fault boundary. |
| `_tls_index` | `0x100982AC` from clean map/metadata | `0x100982AC` | `0x100982AC` | Same PE/ELF TLS index location. |
| TLS template | 0x110 bytes | 0x110 bytes | 0x110 bytes | Size equality does not imply NativeAOT thread-state validity. |
| ManagedMain call | Not available in surviving historical disassembly | `call 0x1004B140` | `call 0x1004B140` | Both generated bodies enter reverse-P/Invoke. |
| FLS IAT | Not available in historical PE | `0x10052108 -> 0x8DC44` | `0x10052108 -> 0x8DC44` | The clean artifact carries an unresolved low-RVA thunk. |
| Converter | Exact historical invocation not recorded | local fixed-base converter | local fixed-base converter | Current tool identity is deterministic; historical path is not recoverable. |
| Runtime pack | Not recorded | ILCompiler/runtime pack 9.0.0 | ILCompiler/runtime pack 9.0.0 | Same stock Windows pack family in current comparisons. |

The archived clean pass and clean fail ELFs differ only in linker debug-directory/CodeView timestamp and GUID bytes; executable bytes, imports, maps, program headers, TLS metadata, and disassembly are identical. This is not a valid functional repair or a valid symbol-selection explanation.

## 6. Entry-symbol analysis

The clean map resolves:

```text
ManagedMain                              0x10001900
RhpReversePInvoke                        0x1004B140
RhpReversePInvokeAttachOrTrapThread2    0x1004B1A0
RhpReversePInvokeReturn                  0x1004B290
ThreadStore::AttachCurrentThread         0x1004EBD0
__imp_FlsGetValue                        0x10052108
g_flsIndex                               0x1008E2D0
_tls_index                               0x100982AC
_tls_start                               0x10081480
_tls_end                                 0x10081590
```

The entry bytes are:

```text
0x10001900  push rbp; push r15; ...; sub rsp,0x78
0x1000193C  lea rcx,[rbp+0x18]
0x10001940  call 0x1004B140
```

Therefore `0x10001900` is the actual generated `ManagedMain` method body, not an unmanaged export thunk selected in place of the method. It is also not a direct-body proof in the required sense, because the body immediately invokes the NativeAOT reverse-P/Invoke transition.

## 7. Reverse-P/Invoke call chain

```text
ELF e_entry 0x10001900
  -> ManagedMain body
  -> call 0x1004B140 RhpReversePInvoke
  -> jump 0x1004B1A0 RhpReversePInvokeAttachOrTrapThread2
  -> call 0x1004EBD0 ThreadStore::AttachCurrentThread
  -> call [0x10052108] __imp_FlsGetValue
  -> target 0x8DC44
  -> 0xC0000005 before host callback
```

`RhpReversePInvokeReturn` is present at `0x1004B290`, but it is not reached in the failing run.

## 8. Required runtime/thread state

The disassembled helper requires more than a TLS vector slot and a zeroed block:

- a valid PE/Windows FLS import address for `FlsGetValue` and `FlsSetValue`;
- an initialized NativeAOT `g_flsIndex` rather than `0xFFFFFFFF`;
- a runtime initialization callback/thread-store contract;
- a valid per-thread NativeAOT structure at the TLS-derived offset `+0x30`;
- the `ThreadStore::AttachCurrentThread` path and its transition-frame expectations;
- module/runtime initialization before reverse-P/Invoke entry;
- the GC/runtime state required by the workstation runtime pack, even for this stack-only body;
- a valid return transition through `RhpReversePInvokeReturn`.

The first observed unresolved call is enough to reject a narrow “bind one thunk” fix. Once FLS is made callable, the helper still receives the uninitialized FLS index and continues into thread-store/runtime initialization.

## 9. Current TLS bootstrap assessment

The Server currently:

1. reads `_tls_index` and the TLS template size from staging metadata;
2. calls Windows `TlsAlloc`;
3. allocates and zeroes a 0x110-byte host vector block;
4. calls `TlsSetValue` for the allocated slot;
5. writes slot `7` into the mapped image at `0x100982AC`;
6. clears and frees the slot after the entry returns.

This is a generic ELF-host TLS envelope. It is not the Windows TLS directory loader, not FLS initialization, not NativeAOT module startup, and not thread-store attachment. It is per launch and is reset on cleanup, but its reset semantics do not establish the missing runtime semantics.

The NativeAOT code uses Windows TLS-vector access through `GS:[0x58]` and separately uses FLS through the unresolved `FlsGetValue`/`FlsSetValue` imports. “TLS initialized” is therefore not a sufficient description of the state expected by this runtime helper.

## 10. Converter provenance

The UEFI repository is currently clean and detached at `e2bbb0ef6d3eb78eb316235ec4b5748ee95718b0`:

```text
D:/dev/guideXOSUEFI/Tools/pe_to_elf_v2.py
SHA-256 4B7EB3DB32297638EF47583051EEA6CD00E7046ACC31AB0CAF55995820B5D313
```

The Server proof copy is:

```text
tools/dotnet/pe_to_elf_v2_fixed_base.py
SHA-256 EAAEFBC8862D6E1A4AC1A679073AF5311A3AF9CE96F45D258617BD4FE0977434
```

The local copy contains the intended proof-only change: a page-aligned, fileless read-only PT_LOAD reservation at the PE image base, producing seven PT_LOADs and preserving preferred-base mapping. The canonical converter produces six PT_LOADs for the same PE.

The current build script uses the local fixed-base copy and records its path and hash in `toolchain.txt`. The historical cce6 build script named the external converter by default, but the surviving successful ELF has seven PT_LOADs. The exact historical converter invocation is not recorded; the artifact proves that a fixed-base variant generated it, but not whether that variant was the local copy or a temporarily modified external copy. Neither converter was overwritten.

## 11. Repair strategy selected

Strategy C is selected: **custom guideXOS NativeAOT runtime/platform pack required**.

The current stock Windows runtime pack fundamentally reaches Windows FLS, runtime initialization, thread-store attachment, transition frames, and GC/runtime globals. Implementing those semantics is beyond a bounded TLS adapter and is explicitly outside this pass. The Server does not patch the IAT, return success from `FlsGetValue`, fabricate a thread object, or bypass the managed body.

## 12. Code changes

- Added environment-gated, fail-through vectored exception diagnostics in `native_elf_executor.cpp`. The handler records the exception, fault target, instruction/register state, FS/GS selectors, entry/mapping/TLS envelope, context, and does not suppress the access violation.
- Added map/disassembly/PE-import assertions for the exact `ManagedMain -> RhpReversePInvoke -> AttachCurrentThread -> FlsGetValue` path.
- Added entry category, symbol addresses, converter/toolchain/runtime-pack identity, TLS envelope identity, and source/staged hash equality to the stage envelope.
- Extended the live smoke to assert those identities and to support `-SkipBuild` for frozen artifact replay and `-EnableFaultDiagnostics` for failure capture.
- Fixed the static artifact smoke to pass the PE dump into the reverse-P/Invoke assertion.

No managed code, allocation, GC implementation, exception handling, thread support, GUI, filesystem, networking, loader broadening, or default inventory was added.

## 13. Static validation

Passing checks:

- clean NativeAOT artifact build;
- fixed-base conversion;
- source/staged ELF hash equality;
- ELF64 AMD64 ET_EXEC envelope with seven PT_LOADs;
- no dynamic section, relocations, or ELF sections;
- map entry and exact reverse-P/Invoke symbols;
- PE import/TLS metadata checks;
- static managed ABI checks;
- generic Native ELF regression smoke;
- default inventory remains isolated.

The static artifact smoke reports entry `0x10001900`, seven load segments, and a matching source/staged hash. Static acceptance does not imply runtime-correct managed entry.

## 14. Live validation

The diagnostic live run reaches the clean ELF entry and fails with `0xC0000005` at the unresolved `FlsGetValue` target before `[NativeAppHost]` logging. The smoke script fails nonzero; no access violation is caught as success.

The normal live path also fails for the frozen clean artifact. The diagnostic switch only adds capture; it is not required for the failure.

## 15. Repeat-launch validation

The historical logs report two successful launches in one process. A newly generated clean artifact (`C41342...`) also replayed twice with an earlier Server executable, and that replay set is preserved under `successful/clean-pass-20260713-060/`. The successful Server executable was not preserved, and after a later clean Server rebuild the frozen pass artifact failed at the same transition boundary.

With the current rebuilt Server and frozen clean failing artifact, the first launch fails. Consequently the required second same-process launch and second independent-process launch are not successful and are not claimed.

## 16. What the corrected milestone proves

The corrected static classification proves:

- NativeAOT generated C# machine code exists at the selected entry;
- the guideXOS C ABI and host context layout are represented in the generated body;
- the exact runtime transition boundary and unresolved platform dependency are identified;
- the failure is observable and not suppressed;
- source/staged packaging and converter identity are deterministic for the current build.

It does not establish a runtime-correct managed entry.

## 17. What remains unproven

The following remain unproven:

- a valid NativeAOT reverse-P/Invoke transition in a clean Server process;
- FLS import binding and `g_flsIndex` initialization;
- NativeAOT thread-store attachment and transition-frame correctness;
- GC safety, allocation safety, exceptions, or general managed execution;
- a reproducible same-process or cross-process live success;
- the exact historical successful Server binary and complete historical toolchain provenance.

The direct managed-body and runtime-correct classifications must remain separate. This work does not authorize allocation through either an unresolved transition or a direct-body workaround.

## 18. Exact next experiment

Build the smallest guideXOS NativeAOT runtime/platform pack that replaces the stock Windows startup contract for this target: module initialization, FLS/TLS runtime slot setup, current-thread/thread-store attachment, transition-frame initialization, required import policy, and fail-fast behavior. Re-run the same non-allocating host-log proof with no changes to `ManagedMain`.

Do not bind only `FlsGetValue`, do not fabricate `g_flsIndex` or a thread object, and do not begin allocation until the expected reverse-P/Invoke transition returns correctly twice in one process and in a second independent Server process.

**Final decision: Outcome C - Custom runtime pack required.**
