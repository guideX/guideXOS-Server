# Managed HostLog access-violation diagnosis

Status: 2026-07-23. The historical stock-artifact fault is precisely
classified as an unresolved `FlsGetValue` import cell. The current guideXOS
runtime-pack artifact passes HostLog reproducibly. No GC startup was entered.

## 1. Reproduction matrix

| Artifact/run | Result |
| --- | --- |
| Historical clean stock runtime-pack artifact | `0xC0000005` before host callback |
| Current clean guideXOS pack, server process 1 | PASS; runtime IDs 1 and 2 |
| Current clean guideXOS pack, fresh server process 2 | PASS; runtime IDs 1 and 2 |
| Current exact GC/PAL startup | Not run; `RhInitialize` prohibited |

The two current clean invocations each delivered exactly one callback per
launch, the exact message, return code 0, and normal cleanup.

## 2. Artifact hashes

Fresh current HostLog run:

```text
HostLogProof.exe  3CCBE9E965058264966BFB673557D7D18E00FF24BED225970CD688CF6336BA3E
HostLogProof.elf  01AD727CE3C5302E0A2FC12F20C1DEACBCB0A088FB925DB5547DCFA7DEE83371
staged ELF        01AD727CE3C5302E0A2FC12F20C1DEACBCB0A088FB925DB5547DCFA7DEE83371
runtime-pack obj  5B53DCAA897A4AC7162706762CF6E272002758405E59E92A4D55BFA9190B0FF8
runtime manifest   8AFF894C312D13AE6977EF80CD419B1E86240232A57E68ED4C0F7D526697791B
PE-to-ELF tool    5F21B87D343106120EB5CAD1F98DF524404171E084C40F4FC3AFED6BE6F84B96
```

The immutable proof envelope is
`out/dotnet/track-b-hostlog-fresh-20260723/stage-managed-hostlog-proof/proof/proof-envelope.json`.

## 3. Fault code and RIP

The stock fault was `0xC0000005`. The first invalid target was RIP/target
`0x8DC44`. The faulting image instruction was:

```text
0x1004ECAF: FF 15 53 34 00 00
call qword ptr [0x10052108]    ; __imp_FlsGetValue
```

The IAT/data cell contained `0x8DC44`, which was not an executable mapped
address in the proof image or Server image. The fault was therefore not a
valid callback target.

## 4. Faulting instruction

The instruction is an indirect import call, not the managed host-table log
call. The target contents and disassembly classify the access violation before
the host callback body. No access violation was suppressed and no generated
machine-code call site was patched.

## 5. Indirect target

Classification: unresolved stock PAL FLS import (`__imp_FlsGetValue`). It is
not the host log callback, reverse-P/Invoke helper, resolver result, or a
ThreadStore callback. The current corrected envelope reports
`flsGetValueImportThunkAddress: 0x0` because the guideXOS runtime-pack binding
does not use the unresolved Windows thunk.

## 6. Host ABI state

The current successful proof entered the actual `NativeHostCallTable::log`
path. The host callback validates the context pointer, table pointer, context
size, table identity, API version/size contract, and message pointer before
counting the call. It recorded exactly:

```text
Hello from managed guideXOS code
```

The entry is `ManagedMain` at `0x10001900`; the current envelope records the
reverse-P/Invoke address `0x1004A650` and return address `0x1004A700`.
The successful callback proves that the context/table binding, Microsoft-x64
argument register setup, shadow space, stack alignment, and executable host
target are valid for the current corrected artifact. The historical stock
fault occurred earlier and did not reach this callback.

## 7. FLS/ThreadStore state

The historical artifact had TLS block state and an FLS slot, but the stock
FLS import cell was unresolved. The current artifact uses the guideXOS
TLS-template-backed runtime cell and local FLS namespace. It returned from the
managed body and cleaned up for both launches. No GC ThreadStore record,
collector thread, finalizer thread, or GC heap was created.

## 8. Stack and transition frame

The current envelope validates the fixed-base image and trampoline path; the
runtime returned 0 after the managed body. The bounded HostLog proof does not
publish a live collector transition-frame snapshot. That state was therefore
not invented or treated as a PASS for `RhInitialize`; it was not the faulting
boundary. Existing inactive ThreadStore/stack probes remain the evidence for
generic attach/detach state.

## 9. Import/resolver state

The stock fault target was the unresolved `__imp_FlsGetValue` cell at
`0x10052108`. The current guideXOS runtime pack removes that live dependency
through its platform object and FLS implementation. The remaining stock
NativeAOT PAL object family is separately inventoried in
[NATIVEAOT_PAL_RUNTIME_REPLACEMENT](NATIVEAOT_PAL_RUNTIME_REPLACEMENT.md);
this HostLog correction does not claim that GC startup PAL imports are gone.

## 10. PE-to-ELF state

The current artifact uses preferred base `0x10000000`, maps at that base, and
reports seven segments with image size 767488. Source and staged ELF hashes
match. The converter hash is recorded above. The current proof therefore
passes the fixed-base, writable-data, BSS, section-permission, entry-point,
and TLS bootstrap checks already covered by the execution envelope. No
converter broadening was required.

## 11. Root cause

Root cause: the clean stock runtime-pack artifact retained a Windows PAL
FLS import whose IAT/data cell was not initialized by the PE-to-ELF execution
environment. The first managed reverse-entry path called through that cell,
so the process faulted before host logging. This is a PAL/runtime binding
failure, not a host-table ABI failure.

## 12. Correction

The exact correction was the existing guideXOS runtime-pack platform binding:
guideXOS-owned FLS get/set operations and reverse-P/Invoke transition helpers
are supplied in the adapted object, while the unresolved Windows FLS thunk is
not entered. No fake Windows export, broad import whitelist, fallback native
logger, or access-violation suppression was added.

## 13. Repeat-launch evidence

The current clean run `track-b-hostlog-current-20260723` passed twice in one
Server process. A second clean run `track-b-hostlog-fresh-20260723` passed in a
fresh Server process and again delivered two successful runtime IDs. Each
launch logged exactly once, returned 0, and left zero owned windows.

## 14. Remaining limitations

- This diagnoses and corrects the managed HostLog baseline only.
- `RhInitialize` was not called; collections and GC-backed allocations are 0.
- The four-object NativeAOT PAL/runtime family still needs exact MSVC-source
  replacement and ABI-safe QEMU validation.
- The exact Workstation GC QEMU probe remains blocked by the COFF/Win64 versus
  ELF/SysV boundary.
- The current repeated-allocation live rerun encountered an unrelated clean
  staging failure (`PrivateSdkAssemblies` required by the NativeAOT target);
  the prior clean repeated-allocation evidence remains PASS and no runtime
  change was made to that path.

Final HostLog result: **PASS** for the corrected guideXOS artifact; the
historical stock fault is precisely diagnosed as the unresolved FLS import.

