# Navigator JavaScript JS114 Runtime Strategy

**Date:** 2026-11-08

**Outcome C — `JS114_RUNTIME_STRATEGY_BLOCKED`**

**Highest verified gate:** G2 — guest-target compilation

**Repository:** `D:\dev\guideXOSServer_NAVIGATOR_JS_SUPPORT`

**Branch:** `NAVIGATOR_JAVASCRIPT_SUPPORT`
**Starting HEAD:** `747418a1817fd3bfd3879665f70879d4ab73d6d0`

## 1. Decision

No currently available guest execution architecture can safely host the accepted JavaScript runtime. The probe establishes a useful narrower result: a C++17, exception-free component with caller-owned bounded storage can compile under the actual AMD64 compiler profile and can be statically linked with the production kernel linker script without imports. It does not establish a supported guest-loadable component, a C++ standard-library runtime, or execution in a guest.

The preferred future direction is **Option C — a shared, portable, exception-free interpreter core**. It keeps the accepted interpreter as the sole JavaScript implementation and confines platform changes to the interpreter's storage and error boundary. This is a direction for JS115, not a completed or yet qualified integration. The scope is materially larger than a small allocator adapter: the accepted source uses `std::string`, `std::vector`, `std::unique_ptr`, `std::from_chars`/`std::to_chars`, floating-point math, 61 `try` sites, 65 `catch` sites, and six explicit `std::bad_alloc` throws across the engine and host adapter.

The smallest prerequisite for changing this no-go is a bounded JS115 portability tranche that defines one source-shared text/container/result interface and proves it on the lexer, AST, and parser path. It must compile those accepted translation units with the guest flags and preserve the existing hosted lexer/parser behavior before the approach expands to the runtime and DOM adapter. JS114 did not begin that refactor.

## 2. JS113 reconciliation and preflight

The requested starting commit was present. The worktree was clean at start, the branch was `NAVIGATOR_JAVASCRIPT_SUPPORT`, and `E:\CodexArtifacts` was available. JS112 and JS113 reports and their artifacts were available. No checkout switch or production kernel build output was used.

JS113 established that C++17 syntax works, while the freestanding MinGW libstdc++ rejects the required string/vector headers. Its exception-runtime link reported 33 unique unresolved symbols and ten `IMAGE_REL_AMD64_ADDR32NB` overflows. JS114 reproduced the overflows and found why they appeared: the JS113 diagnostic command omitted the AMD64 link flags from `kernel/arch/amd64/Makefile.arch`. Without them, GNU ld selected PE image base `0x140000000` while the linker script placed the image sections near `0x100000`. With the actual `-no-pie --image-base=0x100000` flags, all ten overflows disappear. The exception link still exits 1 with the same 33 unresolved symbols, so this corrects the relocation finding but does not qualify exception support.

The linker script's `/DISCARD/` matches exact `.pdata` and `.xdata` section names. It does not match suffixed compiler sections such as `.pdata.unlikely`, `.xdata.unlikely`, `.pdata$...`, or `.xdata$...`; those appear as orphan output sections in the diagnostic map. Their presence in a failed link map is not evidence of supported unwinding or stable section placement.

| Item | JS114 evidence |
|---|---|
| AMD64 compiler | `C:\mingw64\bin\g++.exe`, MinGW-W64 GCC 15.2.0 |
| Target | `x86_64-w64-mingw32`, PE/COFF x86-64 objects, Microsoft x64 ABI |
| Linker / conversion | GNU ld and GNU objcopy 2.46.0.20260210; production path links PE then converts to ELF64 |
| C++ profile | Kernel C++14, freestanding, no exceptions/RTTI, no standard C++ include search, no standard libraries; probe changes the language flag to C++17 |
| AMD64 profile | `-m64 -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2`, non-PIC/non-PIE, no stack probe/check/protector |
| Other compiler paths | No `clang++` or `x86_64-elf-g++` was available; the installed MinGW-prefixed driver is the same target family |
| Kernel link settings | `-T kernel/arch/amd64/linker.ld -no-pie --image-base=0x100000`; `libgcc` is the kernel link library |
| Other settings | `.phase` absent; no QEMU launch, QAC request, package, or guest execution |

Full compiler commands and versions, target objects, the PE map, converted ELF inspection, the JS113 exception inputs, both exception-link maps, and hashes are under `E:\CodexArtifacts\guideXOSServer_NAVIGATOR_JS_SUPPORT\JS114\run-20261108`.

## 3. Architecture comparison

| Criterion | A — Kernel-static C++17 runtime | B — NativeElf/application runtime | C — Portable exception-free core |
|---|---|---|---|
| Source parity | Direct reuse is attractive, but the required runtime cannot currently build/link. | Could reuse the source if the app supplied a compatible C++ runtime and DOM adapter; neither is present. | Can keep one interpreter source, but requires a broad storage and error-flow refactor with hosted parity gates. |
| Runtime correctness | Not established: string/vector/charconv/math and throwing allocation are unavailable in the freestanding profile. | Not established: NativeElf's C ABI entry is real, but no C++ runtime or exception runtime is supplied. | Could use explicit bounds and errors without stubs; only the probe contract is established so far. |
| Memory safety | Current kernel heap is 8-byte aligned, `std::max_align_t` is 16 bytes, and allocation failure returns null. | The loader allocates image and 128 KiB app stack from kernel `new`; this is not a separate process heap/address space. | A shared bounded-storage policy can define alignment, limits, ownership, and failure. The engine integration remains unimplemented. |
| Exceptions | SEH metadata/unwinder and required platform dependencies are not linkable; kernel-wide exception enablement is out of scope. | Loader hardware-fault containment is not C++ stack unwinding. | Explicit error propagation removes the exception runtime requirement if all relevant throw/catch paths are migrated. |
| Kernel isolation | Poor until a real runtime and unwind boundary exist; broad exception settings are not acceptable. | Better entry/fault boundary, but still runs in kernel address space and uses kernel allocation. | Strongest: changes remain in the interpreter/adapter and preserve the kernel's no-exception profile. |
| Build feasibility | The small no-STL probe compiles and links; the accepted runtime closure does not. | Loader accepts a constrained static ELF app, but the available MinGW link/conversion output is not a `gx_main` app. | C++17 syntax works; a bounded container/error layer still needs implementation and actual-source compile evidence. |
| DOM integration | Built-in Navigator would be the direct attachment point, but its document model differs from the accepted hosted `WebDocument` adapter. | `gx_host_calls` has window, drawing, file, and event calls, but no Navigator document/DOM API. | The existing host adapter still needs a source-matched guest document boundary; this is separate work after the core compiles. |
| Testing | Existing hosted semantic suites can test the same engine, but no guest runtime gate is available yet. | Guest testing would still need a package and approved guest qualification; JS114 did neither. | Existing lexer/parser/runtime and JS109/JS110 matrix can guard parity in staged slices. |
| Maintenance | One engine source, but requires maintaining a target-specific STL/ABI/unwinder. | One engine source, plus a second app/host integration surface and runtime profile. | One engine source with shared abstractions; custom bounded containers increase maintenance but avoid a second JS implementation. |
| Development cost | High: C++17 library, exception allocation, SEH unwind, alignment, init/teardown, and link-profile work. | High: C++ runtime plus app ABI, loader profile, and document/render host API. | High and source-facing: storage and error propagation touch multiple translation units and need extensive regression evidence. |

**Selection:** no option is qualified for guest integration now. Option C is the preferred future route because it avoids broad kernel runtime changes and does not duplicate JavaScript semantics. Option A is rejected for the current platform profile. Option B is rejected as a Navigator runtime because the existing loader does not provide the needed runtime or document host.

### NativeElf facts relevant to Option B

`kernel/core/native_elf_baremetal.cpp` validates a static AMD64 `ET_EXEC` image and calls the manifest entry `gx_main` through the `guidexos-c-abi-v1` C ABI. It has a 128 KiB app-owned stack, but image and stack buffers use the kernel allocator; the app does not run in a separate address space. The loader's current rebasing code supports a constrained static image and does not provide a C++ ABI runtime. Its host-call table exposes window, draw, file, input, and event services, not a `WebDocument`/DOM interface.

JS114's C++ probe image is deliberately not a NativeElf package. The PE-to-ELF inspection has ELF64/AMD64 `ET_EXEC` and two `PT_LOAD` segments, but entry address zero. The loader explicitly rejects `header.entry == 0`; the probe also has the two-argument `guidex_cpp_probe_run` signature instead of the one-argument `gx_main(gx_app_context*)` contract. No manifest or guest package was created.

The hosted `NavigatorScriptHostAdapter` targets the accepted `guideWeb::WebDocument`. The bare-metal `NavigatorApp` has a different document/render path and presently documents that it does not require JavaScript. A route between these models remains an explicit future dependency.

## 4. C++17 C ABI and allocation probe

The source probe is `tests/js114_cpp_probe.cpp` with its C-compatible contract in `tests/js114_cpp_probe.h`. A C translation unit, `tests/js114_cpp_probe_c_abi.c`, calls the C++ entry to verify name/linkage compatibility. The source uses an actual C++17 `if constexpr`, a 16-byte-aligned object, nontrivial constructor/destructor, placement construction from the target-matched `<new>` header, and no standard container or global allocation. It compiles with `-fno-exceptions -fno-rtti`; errors return explicit status values and no exception can cross the C boundary.

The contract is caller-owned and bounded:

- The caller supplies a writable arena and context for the call lifetime.
- One temporary object allocation is made from a monotonic arena mark; the mark is restored after destruction. The caller owns/reset the arena; there is no arbitrary individual free operation.
- The request must be nonzero and at least the object size. Zero-size requests are rejected.
- Alignment must be a power of two from 16 through 4096 bytes. The probe object itself requires 16 bytes; no `alignas`/over-aligned type was found in the accepted interpreter source.
- The allocator checks `arena_used <= arena_capacity`, pointer-plus-capacity overflow, align-up overflow, and `bytes <= capacity - offset` before committing the new mark. Oversized requests fail without arithmetic wrap or writes.
- Result status distinguishes invalid context, invalid request, bad alignment, exhaustion, unsupported flags, and success. The record contains request values, observed alignment, construction/destruction flags, restored arena mark, and the deterministic value `0x4A533131`.
- The probe does not replace global `operator new`, claim standard allocator semantics, or fabricate runtime functions.

The hosted checker `tests/js114_cpp_probe_host.cpp` exercised success from an unaligned arena base, 16-byte and 64-byte alignment, arena exhaustion, invalid alignment, undersized and zero-size requests, `SIZE_MAX` request handling, pointer-range overflow, unsupported flags, constructor/destructor execution, arena-mark restore, and guard-byte preservation. These are hosted checks of the same target-source logic, not guest execution evidence.

The target C++ object has no undefined symbols. The C object has one expected reference to `guidex_cpp_probe_run`; the static link resolves it. The exception-free probe was statically linked with the actual kernel linker script, `-no-pie`, image base `0x100000`, `--no-undefined`, and the kernel `libgcc` archive. The PE/COFF image has no imports. GNU objcopy converted it to ELF64 for format inspection. The isolated image is not a supported guest component, as described above.

## 5. Standard-library feasibility

The probe script manually re-adds only the matching MinGW C++ header directories while retaining `-nostdinc++`, so it can use standard placement construction without claiming that the freestanding STL is available. The bounded header probe compiled `new`, `array`, `algorithm`, `limits`, `memory`, and `utility`; its separate object references `memmove`, which exists in the kernel runtime, but this object was not linked as a guest component.

Target freestanding probes reproduced the blockers:

| Facility | Result under `-std=c++17 -ffreestanding -fno-exceptions` |
|---|---|
| `std::string` | Rejected by libstdc++ `bits/requires_hosted.h`; follow-on allocation/throw declarations are unavailable. |
| `std::vector` | Rejected by the same hosted-only guard and allocator throw declarations. |
| `std::from_chars` / `std::to_chars` | `<charconv>` is rejected by `bits/requires_hosted.h`. |
| `std::cmath` operations | `<cmath>` is rejected in freestanding mode; its included templates also reference unavailable throw helpers. |
| `std::unique_ptr` header | Header/type layout compiled in the small header probe; no engine-level runtime/lifetime closure is established. |

The accepted engine needs strings/vectors throughout lexer, parser, environment, runtime, JSON parse/stringify/reviver, and DOM host storage. C++17 numeric conversion and floating-point operations are also in the accepted closure. The existing kernel's `memmove` does not supply the missing throwing allocator or container runtime. The accepted engine was not recompiled as a bounded-source probe because no architecture yet supplies those dependencies.

The current kernel C++ runtime in `kernel/core/cxx_runtime.cpp` rounds heap allocations to 8 bytes, while the verified target `std::max_align_t` is 16 bytes. It provides no C++17 aligned `new/delete` overloads. Its ordinary `operator new` returns the allocator result directly, including null on exhaustion. This differs from the accepted interpreter's `std::bad_alloc`-based allocation handling and is not standard allocator compatibility.

## 6. Exceptions, initialization, and linker relocation

### Exception closure

The production kernel compile profile disables exceptions and RTTI. The C++17 probe uses that profile and does not invoke unwinding. The MinGW diagnostic exception object emits SEH `.pdata`/`.xdata` records and references `__gxx_personality_seh0`. A complete exception link remains blocked even with the correct kernel image-base flags. Its 33 unique unresolved symbols are:

```text
__imp___acrt_iob_func
__imp_RaiseException
__imp_RtlCaptureContext
__imp_RtlLookupFunctionEntry
__imp_RtlUnwindEx
__imp_RtlVirtualUnwind
__mingw_sprintf
abort
atexit
calloc
fputc
fputs
free
fwrite
getenv
malloc
memcmp
memcpy
memset
pthread_getspecific
pthread_key_create
pthread_mutex_destroy
pthread_mutex_init
pthread_mutex_lock
pthread_mutex_unlock
pthread_once
pthread_setspecific
realloc
strchr
strcmp
strlen
strncmp
strtoul
```

These include Windows SEH imports, POSIX thread/TLS functions, and hosted C heap/string/stdio/environment functions. No stubs were supplied. The actual-flags exception link still exits 1; only its relocation overflows are resolved.

The existing linker script has no constructor-table startup path. JS113's static-initialization object emitted `.ctors`, `.pdata`, `.xdata`, `__cxa_guard_acquire`, `__cxa_guard_release`, and `atexit`. The kernel's simplified guard helpers and no-op `__cxa_atexit` do not establish standard initialization/destruction semantics. The JS114 probe has no dynamic globals or static constructors; its automatic object is explicitly constructed and destroyed within the entry function.

### Relocation comparison

Microsoft's PE/COFF reference defines `IMAGE_REL_AMD64_ADDR32NB` (`0x0003`) as a 32-bit RVA, i.e. a target address without the image base ([PE format relocation table](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)). The diagnostic map resolves the following fields. Values are linker-map VAs; the no-production-flags link has no final output because relocation and unresolved-symbol errors stop the link.

| Relocation field | Source VA | Target VA | RVA with actual image base `0x100000` | Result with default base `0x140000000` |
|---|---:|---:|---:|---|
| `exception.o .xdata.unlikely+0x8` → `__gxx_personality_seh0` | `0x112008` | `0x10EC60` | `0x0EC60` | Negative/out of unsigned 32-bit range |
| `exception.o .pdata.unlikely+0x0` → `caught_exception_probe` | `0x113000` | `0x10CC20` | `0x0CC20` | Negative/out of unsigned 32-bit range |
| `exception.o .pdata.unlikely+0x4` → function end (`+0x53`) | `0x113004` | `0x10CC73` | `0x0CC73` | Negative/out of unsigned 32-bit range |
| `exception.o .pdata.unlikely+0x8` → `.xdata.unlikely` | `0x113008` | `0x112000` | `0x12000` | Negative/out of unsigned 32-bit range |
| `class_type_info.o .pdata` `+0/+4/+8` → upcast start/end and xdata | `0x115168/0x11516C/0x115170` | `0x10E370/0x10E3D0/0x114154` | `0x0E370/0x0E3D0/0x14154` | All out of unsigned 32-bit range |
| `class_type_info.o .pdata` `+0/+4/+8` → find-public-source start/end and xdata | `0x115180/0x115184/0x115188` | `0x10E450/0x10E460/0x114164` | `0x0E450/0x0E460/0x14164` | All out of unsigned 32-bit range |

The default-image-base comparison reproduces ten overflows. With the actual AMD64 `-no-pie --image-base=0x100000` flags, the same objects and archives produce zero relocation-overflow diagnostics. Both variants still fail with the same 33 unique unresolved symbols. Thus the recorded overflow was caused by the diagnostic command's image-base mismatch, not by switching exception handling or by an image larger than 4 GiB. The production Makefile already contains the needed image-base flag; no kernel linker layout was changed.

## 7. Target build, link, and size evidence

The reproducible command is:

```powershell
.\scripts\probe-navigator-js114-cpp.ps1 -ArtifactDir E:\CodexArtifacts\guideXOSServer_NAVIGATOR_JS_SUPPORT\JS114\run-20261108
```

The script uses GCC 15.2.0 and the production AMD64 architecture flags, switches the C++ language flag from C++14 to C++17, and adds `-fstack-usage`/dependency output. It compiles the C ABI caller with the same target profile. Results:

| Gate/evidence | Result |
|---|---|
| C++17 probe compile | Pass; PE/COFF AMD64 object, 1,161 bytes before the final rerun with identical layout. |
| C11 ABI caller compile | Pass; its sole undefined entry symbol is resolved by the C++ object. |
| Exception-free static link | Pass as an isolated PE/COFF link using the kernel script and actual AMD64 link flags; no imports or unresolved symbols. |
| PE → ELF64 conversion | Pass for format inspection; ELF64 AMD64 `ET_EXEC`, two `PT_LOAD` headers, but entry zero and therefore not a supported guest component. |
| `std::string`, `vector`, `charconv`, `cmath` probes | Expected freestanding compile rejection. |
| Actual exception-runtime link | Fail with 33 unresolved symbols; zero relocation overflows when actual kernel flags are used. |
| Accepted interpreter translation units | Not attempted in JS114; the required standard facilities remain unavailable. |

Static stack output for the linked probe is 24 bytes in `guidex_cpp_probe_run` and 8 bytes in the C ABI caller; the arena helper and constructor/destructor inline into the 24-byte frame at `-O2`. The separate standard-header probe's maximum reported frame is 96 bytes. These are compile-time frames only. They do not estimate JavaScript parse, execute, JSON, reviver, or DOM host stack use.

The target C++ object is 1,161 bytes; the freestanding header probe object is 2,585 bytes and has one unresolved `memmove`; the linked PE image is 3,379 bytes and converted ELF is 10,232 bytes. No interpreter object sizes, guest image size, JS heap high-water, or guest stack high-water were established. The existing production kernel heap remains 32 MiB; no heap/stack increase was made.

## 8. Source parity and regressions

Only probe sources, their build script, and this report changed. The accepted JavaScript source remains authoritative and unchanged. The JS109/JS110 suites, 111-lane matrix, and hosted aggregate were not rerun because no accepted interpreter or host adapter code changed. The hosted ABI probe checks only the new isolated allocation/lifetime contract and is not JavaScript semantic regression evidence.

The accepted baseline remains prior evidence: JS109 86/86; JS110 28/28; JavaScript matrix 111/111 lanes and 17,656/17,656 assertions; hosted aggregate 767/774 with seven established CSS failures. Those numbers are not JS114 results.

## 9. Gates and next milestone

| Gate | Result | Qualification |
|---|---|---|
| G0 — architecture decision | Pass | No-go for current guest integration; Option C selected as future direction with a concrete JS115 prerequisite. |
| G1 — allocation/ABI contract | Pass for the isolated probe | Contract is source-backed, compiled for the target, and hosted-checked. Guest allocation behavior remains unverified. |
| G2 — guest-target compilation | Pass | Real AMD64 compiler produced the C++17 object and C ABI caller. |
| G3 — complete link into supported guest component | Not passed | Standalone static image link passes, but the converted ELF entry is zero, is not `gx_main`, and is not a NativeElf/kernel component. The exception runtime still has 33 unresolved symbols. |
| G4 — accepted interpreter source compatibility | Not reached | No compatible engine library/runtime is available; no bypass was attempted. |

**Recommended JS115:** implement a source-shared bounded text/container/result seam for the lexer–AST–parser path only. Compile those actual accepted units using the current guest profile with exceptions disabled; run the existing hosted lexer/parser suites against the same source; demonstrate explicit capacity/OOM propagation and no new runtime imports. Use those measurements to decide whether Option C can scale to `environment.cpp`, `runtime.cpp`, JSON/reviver, and the document host adapter. Do not begin guest packaging or QEMU/QAC qualification until that core and its document boundary have independent compile/link evidence.

## 10. Preservation and completion

The protected `guideXOSServer.exe` remains 8,398,274 bytes with SHA-256 `A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971`. All 42 generated fixtures match their saved hashes (458,372 bytes total); `.phase` remains absent. The machine-readable verification is `metadata/final-preservation-verification.json` in the JS114 artifact directory.

No QEMU, QAC, packaging, or guest execution occurred. The final local commit, ending HEAD, branch tracking state, and clean-worktree result are reported in the JS114 completion record after commit.
