# Managed Host-Log Artifact Proof

Conclusion: Outcome B - Packaging adaptation required.

## 1. Objective

Build the smallest practical AMD64 managed payload for the first guideXOS Server managed application, then prove whether it can be represented as a guideXOS Server-compatible ELF64 artifact without touching Server runtime integration.

The managed body is intentionally tiny:

- one managed entry method
- one logging boundary
- no GUI, filesystem, networking, threads, tasks, timers, reflection, or dynamic loading

## 2. Scope Boundaries

This pass stayed outside the Server runtime path.

Untouched subsystems:

- kernel
- ELF loader
- process manager
- App Model resolver
- compositor
- VFS
- boot image
- default build

Allowed proof-only additions:

- `[scripts/dotnet/build-managed-hostlog-proof.ps1](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/scripts/dotnet/build-managed-hostlog-proof.ps1)`
- `[samples/managed/HostLogProof/HostLogProof.csproj](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/samples/managed/HostLogProof/HostLogProof.csproj)`
- `[samples/managed/HostLogProof/Program.cs](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/samples/managed/HostLogProof/Program.cs)`
- `[samples/managed/HostLogProof/NativeAbi.cs](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/samples/managed/HostLogProof/NativeAbi.cs)`
- `[samples/managed/HostLogProof/runtime_support.c](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/samples/managed/HostLogProof/runtime_support.c)`
- `[.gitignore](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/.gitignore)`

No Server execution or loader integration was attempted.

## 3. Toolchain Used

The proof used the current Windows host and the existing guideXOS NativeAOT/PE-to-ELF path.

Evidence files:

- `[out/dotnet/managed-hostlog/artifacts/toolchain.txt](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/out/dotnet/managed-hostlog/artifacts/toolchain.txt)`
- `[out/dotnet/managed-hostlog/artifacts/dotnet-info.txt](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/out/dotnet/managed-hostlog/artifacts/dotnet-info.txt)`
- `[out/dotnet/managed-hostlog/artifacts/python-info.txt](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/out/dotnet/managed-hostlog/artifacts/python-info.txt)`

Build host and tool versions:

- .NET SDK `10.0.301`
- .NET host/runtime `10.0.9`
- ILCompiler package `Microsoft.DotNet.ILCompiler 9.0.0`
- Python `3.12.13`
- Visual Studio `18 Community` MSVC `14.51.36231`
- `readelf.exe` and `objdump.exe` from `C:\mingw64\bin`
- PE-to-ELF converter from `[D:\dev\guideXOSUEFI\tools\pe_to_elf_v2.py](D:/dev/guideXOSUEFI/tools/pe_to_elf_v2.py)`

Required environment and paths:

- `GUIDEXOS_LEGACY_ROOT` defaults to `D:\dev\guideXOSUEFI`
- bundled Python at `C:\Users\guideX\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe`
- `vcvars64.bat` from Visual Studio
- `dotnet` on PATH

Relocatability:

- Partially parameterized through script inputs
- Not fully relocatable yet because the pipeline still depends on a Windows VC toolchain, a Windows-targeted NativeAOT runtime pack, and the legacy guideXOSUEFI PE-to-ELF converter

## 4. Exact Build Pipeline

The proof build was driven by:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dotnet/build-managed-hostlog-proof.ps1 -Clean
```

The script performs these steps:

1. Validates the repo root, legacy root, `dotnet`, Python, `readelf`, `objdump`, and `vcvars64.bat`.
2. Writes the evidence files in `out/dotnet/managed-hostlog/artifacts/`.
3. Emits a one-shot `build-native-hostlog.bat` shim into the artifact directory.
4. Compiles `[samples/managed/HostLogProof/runtime_support.c](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/samples/managed/HostLogProof/runtime_support.c)` with `cl.exe /TC /c /GS- /Zl`.
5. Runs `dotnet publish` for `[samples/managed/HostLogProof/HostLogProof.csproj](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/samples/managed/HostLogProof/HostLogProof.csproj)` with:
   - `PublishAot=true`
   - `InvariantGlobalization=true`
   - `IlcGenerateStackTraceData=false`
   - `IlcUseEnvironmentalTools=true`
   - `HostLogProofRuntimeSupportObj=<artifact runtime_support.obj>`
   - fixed bin/obj roots under `out/dotnet/managed-hostlog`
6. Copies the published PE image and linker map into the artifact directory.
7. Converts the PE image to ELF64 with `pe_to_elf_v2.py` using the map file and `ManagedMain` as the symbol name.
8. Captures PE and ELF inspection output with `objdump` and `readelf`.

The build completed successfully from a clean output root.

## 5. Managed Source Structure

The sample lives under `[samples/managed/HostLogProof/](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/samples/managed/HostLogProof/)`.

Roles:

- `HostLogProof.csproj`: NativeAOT x64/win-x64 build, fixed base address, output redirected to `out/dotnet/managed-hostlog`
- `Program.cs`: empty compile-time `Main` plus the exported `ManagedMain` unmanaged entry
- `NativeAbi.cs`: small ABI mirror for the host-call table and app context
- `runtime_support.c`: minimal C runtime shim for the NativeAOT link step

The managed body is deliberately narrow:

- `ManagedMain` checks `ctx`, `ctx->host`, and `ctx->host->log`
- it encodes `"Hello from managed guideXOS code"` as UTF-8
- it uses `stackalloc` and `fixed` so no managed string crosses the boundary
- it calls the existing host log ABI through the context table
- it returns the host call result or `GxAbi.ErrorInvalidArgument`

## 6. Runtime Startup Pieces Reused

What was actually needed:

- Native entry stub: yes
- Runtime initialization: yes
- Module table initialization: yes
- Static constructor initialization: not required by this sample
- Managed entry selection: yes
- Runtime exports: not required for this proof
- Shutdown or return path: yes

Evidence:

- `link.rsp` contains `/ENTRY:ManagedMain` and `/INCLUDE:ManagedMain`
- the NativeAOT object contains `HostLogProof_HostLogProof_Program__Main`, `HostLogProof_HostLogProof_Program__ManagedMain`, `HostLogProof__Module___MainMethodWrapper`, and `HostLogProof__Module___StartupCodeMain`
- the map file records `entry point at 0001:00000900`
- the final function body returns via a normal `ret`

## 7. Interop / Logging ABI

The proof reuses the closest existing Server ABI shape rather than inventing a new startup contract.

Server-side ABI reference:

- `[sdk/include/guidexos/abi.h](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/sdk/include/guidexos/abi.h)`
- `[sdk/include/guidexos/app.h](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/sdk/include/guidexos/app.h)`

Relevant existing host-call shape:

- `gx_host_calls.log(gx_app_context* ctx, const char* message)`

Managed-side adapter:

- `NativeHostCallTable.log` is modeled as `delegate* unmanaged<NativeGxAppContext*, byte*, int>`
- `NativeGxAppContext` contains `size`, `apiVersion`, `host`, and `userData`
- the UTF-8 message is stack-allocated and NUL-terminated inside the managed adapter
- no `System.String` crosses the boundary

Ownership and lifetime:

- the message buffer is stack memory owned by the managed call frame
- the host may read it during the call but must not retain the pointer
- the app context and host table are only dereferenced after null checks

## 8. Generated Artifacts

All generated files live under `[out/dotnet/managed-hostlog/artifacts/](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/out/dotnet/managed-hostlog/artifacts/)`.

Key outputs:

- `[HostLogProof.exe](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/out/dotnet/managed-hostlog/artifacts/HostLogProof.exe)`
- `[HostLogProof.map](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/out/dotnet/managed-hostlog/artifacts/HostLogProof.map)`
- `[HostLogProof.elf](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/out/dotnet/managed-hostlog/artifacts/HostLogProof.elf)`
- `[HostLogProof.pe.objdump.txt](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/out/dotnet/managed-hostlog/artifacts/HostLogProof.pe.objdump.txt)`
- `[HostLogProof.elf.objdump.txt](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/out/dotnet/managed-hostlog/artifacts/HostLogProof.elf.objdump.txt)`
- `[HostLogProof.elf.readelf.txt](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/out/dotnet/managed-hostlog/artifacts/HostLogProof.elf.readelf.txt)`
- `[HostLogProof.elf.disasm.txt](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/out/dotnet/managed-hostlog/artifacts/HostLogProof.elf.disasm.txt)`
- `[runtime_support.obj](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/out/dotnet/managed-hostlog/artifacts/runtime_support.obj)`
- `[build-native-hostlog.bat](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/out/dotnet/managed-hostlog/artifacts/build-native-hostlog.bat)`

The map file is now preserved in the ignored artifact directory rather than leaking into the sample source directory.

## 9. PE-to-ELF Findings

The converter at `[D:\dev\guideXOSUEFI\tools\pe_to_elf_v2.py](D:/dev/guideXOSUEFI/tools/pe_to_elf_v2.py)` is a flat PE-section copier, not a full linker.

What it does:

- parses a PE32+ image
- copies raw loadable sections into ELF `PT_LOAD` segments
- emits an `ET_EXEC` `elf64-x86-64` image
- can use the map file to resolve a symbol and search for the real function prologue
- sets the ELF entry point directly

What it does not do:

- no ELF section headers
- no ELF symbol table reconstruction
- no `PT_DYNAMIC`
- no relocation processing
- no TLS handling
- no import-table translation
- no unwind-info reconstruction

Observed consequence:

- the converted artifact is structurally simple and static
- it is not yet a fully packaged user-process ELF
- the code body is traceable through the map file, but the ELF itself does not preserve rich linker metadata

## 10. ELF Inspection Results

Inspection source:

- `[HostLogProof.elf.readelf.txt](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/out/dotnet/managed-hostlog/artifacts/HostLogProof.elf.readelf.txt)`
- `[HostLogProof.elf.objdump.txt](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/out/dotnet/managed-hostlog/artifacts/HostLogProof.elf.objdump.txt)`

Observed ELF properties:

- class: `ELF64`
- endianness: little-endian
- machine: `Advanced Micro Devices X86-64`
- type: `EXEC`
- entry point: `0x10001900`
- program headers: 6 `PT_LOAD` segments
- section headers: none
- dynamic section: none
- relocations: none
- dynamic symbol info: not available
- interpreter: none
- TLS: none in the final ELF

Program header shape:

- one `R E` segment
- one `R` segment
- one `RW` segment with `MemSiz > FileSiz`, so BSS needs are preserved
- three additional `R` segments
- alignment: `0x1000`

Disassembly note:

- `[HostLogProof.elf.disasm.txt](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/out/dotnet/managed-hostlog/artifacts/HostLogProof.elf.disasm.txt)` is effectively header-only because the converter emits a sectionless ELF
- the managed code body was therefore verified from the NativeAOT object and map file instead of from ELF section disassembly

## 11. Server-Loader Compatibility Table

| Requirement | Managed artifact | Server loader expectation | Status |
| --- | --- | --- | --- |
| ELF class | `ELF64` | `ELF64` | Compatible |
| Architecture | `x86-64` / AMD64 | amd64 | Compatible |
| ELF type | `ET_EXEC` | static `ET_EXEC` | Compatible |
| Program headers | 6 `PT_LOAD` segments, no `PT_INTERP` | loadable segments only | Compatible |
| Relocations | none in the final ELF | relocations rejected today | Compatible |
| TLS | no `PT_TLS` in the final ELF; NativeAOT object still contains TLS-related sections | no TLS support is evident in the loader path | Requires adaptation |
| Entry point | `0x10001900` (`ManagedMain`) | entry point inside mapped image | Compatible |
| Segment permissions | `R E`, `R`, `RW`, then `R` segments | sane loadable permissions | Compatible |
| Address assumptions | fixed base `0x10000000`; exact preferred-base mapping used | exact preferred-base mapping is expected | Compatible |
| Dynamic linking | no ELF dynamic section; PE side still imports Windows DLLs | no dynamic linker support | Requires adaptation |
| Unresolved host import | host logging is through `ctx->host->log`, not an ELF unresolved symbol | future packaging may want an explicit unresolved guideXOS import | Requires adaptation |
| Exit behavior | `ManagedMain` returns `int` and has a normal tail return | exit code should be capturable | Compatible |

Overall:

- the ELF shape is close to the current loader contract
- the packaging story is not complete enough for a loader experiment yet

## 12. External Dependencies

Build-time dependencies observed:

- Windows host
- Visual Studio C++ toolchain
- .NET SDK 10.0.301
- ILCompiler package 9.0.0
- Python 3.12.13
- MinGW binutils
- legacy guideXOSUEFI tree

Runtime/link dependencies observed in the NativeAOT PE:

- `ADVAPI32.dll`
- `bcrypt.dll`
- `KERNEL32.dll`
- `ole32.dll`
- `api-ms-win-crt-heap-l1-1-0.dll`

Link-time libraries in `link.rsp` also include:

- `advapi32.lib`
- `bcrypt.lib`
- `crypt32.lib`
- `iphlpapi.lib`
- `kernel32.lib`
- `mswsock.lib`
- `ncrypt.lib`
- `normaliz.lib`
- `ntdll.lib`
- `ole32.lib`
- `oleaut32.lib`
- `secur32.lib`
- `user32.lib`
- `version.lib`
- `ws2_32.lib`

No `libc`, `libpthread`, `libdl`, `libm`, or Linux system-call dependencies were observed in the inspected Windows NativeAOT output.

## 13. Runtime Assumptions

- The host passes a valid `gx_app_context*` with a populated `gx_host_calls*`
- `ctx->host->log` is valid for the duration of the call
- the UTF-8 message buffer is only borrowed during the call
- the current proof assumes a fixed preferred base at `0x10000000`
- no GUI, filesystem, networking, threading, timers, reflection, configuration, or hardware access is used
- no user-authored static constructors are required

## 14. Known Limitations

- The NativeAOT output is still Windows-oriented and brings Windows DLL imports along with it
- The PE-to-ELF converter does not preserve imports, relocations, TLS, or symbol tables as ELF semantics
- The saved ELF disassembly artifact is not useful for code review because the ELF has no sections
- The proof does not execute inside Server
- The normal Server build was not rerun because the workspace still lacks `third_party/stb/stb_image.h` and required `third_party/mbedtls` sources

## 15. Ready for a Server Loader Experiment?

Not yet.

Why:

- the structural ELF contract is close
- the packaging layer still needs work
- the current proof still carries Windows NativeAOT dependencies and a flat PE-to-ELF conversion path that drops important loader metadata

## 16. Exact Next-Step Recommendation

The next pass should focus on packaging adaptation, not Server runtime integration.

Recommended next experiment:

1. Keep the managed sample as-is.
2. Rework the PE-to-ELF packaging so the remaining runtime dependencies are either removed or represented in a Server-consumable form.
3. Preserve enough metadata for the loader experiment to know exactly what it is mapping and why.
4. Then attempt a loader-only smoke test with one host log call.

## 17. Files the Next Pass Would Likely Need to Touch

Likely follow-on touch points:

- `[scripts/dotnet/build-managed-hostlog-proof.ps1](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/scripts/dotnet/build-managed-hostlog-proof.ps1)`
- `[samples/managed/HostLogProof/HostLogProof.csproj](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/samples/managed/HostLogProof/HostLogProof.csproj)`
- `[samples/managed/HostLogProof/Program.cs](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/samples/managed/HostLogProof/Program.cs)`
- `[samples/managed/HostLogProof/NativeAbi.cs](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/samples/managed/HostLogProof/NativeAbi.cs)`
- `[samples/managed/HostLogProof/runtime_support.c](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/samples/managed/HostLogProof/runtime_support.c)`
- the PE-to-ELF converter, either at `[D:\dev\guideXOSUEFI\tools\pe_to_elf_v2.py](D:/dev/guideXOSUEFI/tools/pe_to_elf_v2.py)` or a repo-local replacement under `tools/dotnet/`

Files that should remain untouched in the next pass:

- kernel
- compositor
- VFS
- ELF loader
- App Model
- boot image
- default build graph

## 18. Server Subsystems That Should Remain Untouched

Keep these out of the next iteration unless the proof explicitly changes scope:

- kernel
- compositor
- VFS
- ELF loader
- App Model resolver
- process manager
- boot image
- default build

## 19. Bottom Line

The managed native image was produced, the ELF64 conversion succeeded, and the proof is reproducible from the provided script. The remaining gap is packaging: the current converter and runtime dependency surface are not yet ready for a Server loader experiment.

Conclusion: Outcome B - Packaging adaptation required.
