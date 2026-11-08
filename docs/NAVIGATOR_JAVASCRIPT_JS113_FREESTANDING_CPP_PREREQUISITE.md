# Navigator JavaScript JS113 Freestanding C++ Runtime Prerequisite

**Date:** 2026-11-08
**Outcome C — JS113_CPP_PREREQUISITE_BLOCKED**
**Highest fully verified gate:** G1B, C++17 language syntax probe passed independently. G1A did not pass, so no cumulative integration gate is complete.

## 1. Disposition

JS113 confirms the exact header failure from JS112 and finds a broader incompatibility in the installed target library and runtime.

The repository's AMD64 build uses MinGW-W64 GCC 15.2.0 for target x86_64-w64-mingw32. The compiler understands C++17. Its target C++ headers and static libraries exist, but libstdc++ rejects the required string and vector headers when the compiler is in freestanding mode. The kernel build also excludes all C++ standard headers with -nostdinc++.

A narrow exception probe compiled to PE/COFF, but its link against the installed MinGW exception runtime and the kernel linker script failed. The diagnostics show missing Windows SEH imports, POSIX pthread and C runtime functions, plus AMD64 COFF relative-address relocation overflows. The kernel linker script discards the SEH unwind sections required by this compiler's exception model.

The existing kernel heap and C++ ABI helpers do not satisfy standard allocation and lifetime contracts for this runtime. JS113 therefore establishes no supported C++17 standard-library, exception, allocation, or teardown configuration for the accepted Navigator JavaScript core.

No JavaScript implementation, kernel policy, build definition, or boot package was changed. All probes used isolated output under:

E:\CodexArtifacts\guideXOSServer_NAVIGATOR_JS_SUPPORT\JS113\run-20261108-c3eb1bbd

No guest binary was run, no QAC request was made, and QEMU was not launched.

## 2. Starting repository state

| Item | Verified state |
|---|---|
| Machine | LEON2_RDT |
| Checkout | D:\dev\guideXOSServer_NAVIGATOR_JS_SUPPORT |
| Branch | NAVIGATOR_JAVASCRIPT_SUPPORT |
| Starting HEAD | 6162d016ed76fd9b188e987999e568bd421eff5b |
| Cached upstream divergence at preflight | 0 ahead / 0 behind |
| Starting worktree | Clean |
| AGENTS.md | None in repository or its parent directories |
| .phase | Absent |

The starting HEAD is the JS112 documentation commit requested by the prompt. The JS111 and JS112 reports were read. Their accepted baseline remains the last hosted/native regression evidence; JS113 did not rerun those suites.

## 3. Guest compiler and ABI

The actual Windows AMD64 kernel rules select g++ in kernel/arch/amd64/Makefile.arch. The build links PE/COFF first, then converts the result to ELF64 with objcopy. No x86_64-elf-g++ or clang++ was found. The x86_64-w64-mingw32-g++ executable resolves to the same MinGW target family.

| Property | Observed configuration |
|---|---|
| Compiler | C:\mingw64\bin\g++.exe, MinGW-W64 GCC 15.2.0 |
| Target triple | x86_64-w64-mingw32 |
| Linker | GNU ld 2.46.0.20260210; supported emulations i386pep and i386pe |
| Object format | PE/COFF x86-64 |
| Kernel link conversion | PE image converted to ELF64 by GNU objcopy 2.46.0.20260210 |
| Language and runtime defaults | C++14, -ffreestanding, -fno-exceptions, -fno-rtti, -nostdlib, -nostdinc++, -fno-builtin |
| AMD64 flags | -m64, -mcmodel=large, -mno-red-zone, -mno-mmx, -mno-sse, -mno-sse2, non-PIC/non-PIE, no stack probe/check/protector |
| Calling convention | Microsoft x64 ABI; 16-byte stack alignment and 32-byte shadow space in the existing NativeElf trampoline |
| Linker script | kernel/arch/amd64/linker.ld |
| Kernel link libraries | libgcc only, selected by the existing Makefile |

The available C++ headers and static archives belong to this MinGW target. The installation contains libstdc++.a, libsupc++.a, libgcc.a, libgcc_eh.a, and libmingwex.a. GCC is configured for MinGW x86-64 with POSIX threads and SEH exceptions. The presence of those files does not establish a kernel-compatible runtime.

The kernel rule has no C++ constructor-table startup path. A target initialization probe emitted .ctors, .pdata, and .xdata sections and references to __cxa_guard_acquire, __cxa_guard_release, and atexit. The AMD64 linker script does not collect or invoke a constructor table, and discards .pdata and .xdata along with .eh_frame. The kernel implementation of __cxa_atexit is a no-op.

Full toolchain paths, compiler include search paths, archive paths, versions, and commands are in the run directory's toolchain folder.

## 4. Header and C++17 probes

The JS112 failure was reproduced with the kernel profile. The exact diagnostic was:

    navigator_javascript/source.h:3:10: fatal error: cstddef: No such file or directory
        3 | #include <cstddef>
          |          ^~~~~~~~~
    compilation terminated.

This occurs because -nostdinc++ excludes the compiler's C++ header directories.

With those directories enabled, the freestanding C++17 probe for cstddef compiled. The required representative probe containing cstddef, string, and vector failed inside the installed libstdc++:

    bits/requires_hosted.h:34:4: error: #error "This header is not available in freestanding mode."

The installed vector and string headers are present on disk but are rejected under the actual -ffreestanding profile. The template allocator then also reports unavailable std::__throw_bad_alloc and std::__throw_bad_array_new_length declarations. Adding ordinary hosted header paths would not resolve this; building in hosted mode would select a different runtime contract.

A separate probe using if constexpr and structured bindings compiled with -std=c++17 and the kernel architecture flags. This establishes compiler language support only. It does not establish C++17 library support. The accepted runtime's C++17 numeric conversion uses std::from_chars and std::to_chars; those headers and facilities were not available in the freestanding profile.

The saved command, exit code, object, and diagnostics for each probe are under header-probes and logs.

## 5. Accepted-source dependency inventory

The source graph is recorded in dependency-audit/source-dependency-inventory.md. The relevant closure is:

- value.cpp -> value.h -> cstdint.
- host.cpp -> host.h -> source.h and value.h -> cstddef and cstdint.
- lexer.cpp -> lexer.h -> source.h and vector; implementation also uses algorithm and limits.
- ast.cpp -> ast.h -> lexer.h and vector.
- parser.cpp -> parser.h -> ast.h; implementation also uses string, new, limits, and utility.
- environment.cpp -> environment.h -> source.h and value.h, string and vector.
- runtime.cpp -> runtime.h -> AST, environment, host, parser, and value interfaces; implementation also uses array, string, vector, algorithms, charconv, cmath, new, and utility. It contains function invocation, script execution, JSON.parse, JSON.stringify, and reviver traversal.
- navigator_script_host.cpp -> document, host, and runtime interfaces; it also uses guideWeb HTML parsing, string/vector, charconv, algorithms, arrays, ctype and memory helpers. This is the document host adapter.

The core uses std::string, std::vector, std::array, std::unique_ptr, standard algorithms, limits, iterator helpers, and numeric conversion. No std::optional use was found. Many AST and runtime relationships use integer IDs backed by vectors; vector growth can invalidate native references, so all retained pointer lifetimes still require a full audit.

The engine has 61 try sites, 65 catch sites, and six explicit throws of std::bad_alloc in runtime.cpp. Other exception paths come from allocation and standard-library operations. The source catches bad_alloc and uses catch-all handlers at public execution/error boundaries. No dynamic_cast or typeid use was found, although typed exception handling still depends on type information. The accepted source contains no thread_local, std::thread, std::mutex, or condition_variable use. No Windows API calls or Windows headers were found in the accepted JavaScript source closure.

Header-only or compiler-oriented facilities include cstddef, cstdint, array, algorithm, utility, limits, and iterator helpers. String/vector/unique_ptr storage, container growth, exception support, and C++ ABI functions require a linked runtime. Numeric conversion and floating point use from_chars/to_chars and pow, fmod, floor, ceil, round, isfinite, isnan, isinf, and signbit.

## 6. Source compilation and link evidence

All eight production JavaScript translation units were attempted with C++17, freestanding mode, the real kernel architecture flags, exceptions enabled for diagnostic purposes, and output directed to the JS113 artifact folder.

| Translation unit | Result |
|---|---|
| value.cpp | Compiled to PE/COFF object |
| host.cpp | Compiled to PE/COFF object |
| ast.cpp | Failed at hosted-only vector headers |
| environment.cpp | Failed at hosted-only string headers |
| runtime.cpp | Failed at hosted-only vector headers |
| lexer.cpp | Failed at hosted-only vector headers |
| parser.cpp | Failed at hosted-only vector headers |
| navigator_script_host.cpp | Failed at hosted-only string headers |

Thus the source-matched parser, AST, environment, runtime, JSON, and reviver path was not compiled. Two small representation/interface translation units compiled, which is partial source evidence only. Per-file commands, exit codes, dependency outputs, stack-usage files, and object hashes are in source-build, logs, and dependency-audit/object-hashes.json.

A minimal exception probe compiled with -fexceptions and emitted PE/COFF sections .pdata.unlikely and .xdata.unlikely plus references to __cxa_allocate_exception, __cxa_throw, __cxa_begin_catch, __cxa_end_catch, and __gxx_personality_seh0. The same exception syntax failed under the kernel's -fno-exceptions profile.

A diagnostic link then used the actual MinGW libsupc++, libgcc_eh, and libgcc archives with kernel/arch/amd64/linker.ld and --no-undefined. It failed with exit code 1. The linker reported 10 IMAGE_REL_AMD64_ADDR32NB relocation overflows and 33 unique unresolved symbols. Representative missing dependencies were:

- Windows SEH imports: __imp_RaiseException, __imp_RtlCaptureContext, __imp_RtlLookupFunctionEntry, __imp_RtlUnwindEx, and __imp_RtlVirtualUnwind.
- POSIX synchronization: pthread_mutex_init/lock/unlock/destroy, pthread_once, and pthread TLS functions.
- C runtime: malloc, calloc, realloc, free, abort, getenv, stdio functions, and string functions.

The full log and unresolved-symbol summary are in link-probes. No symbol stubs were supplied. This was an exception-runtime diagnostic link, not a complete JavaScript-core link. A complete core link was not reached because the accepted core objects were unavailable.

## 7. Allocation, initialization, and lifetime

The current kernel heap in kernel/core/cxx_runtime.cpp is a 32 MiB fixed arena in production builds; an opt-in diagnostic macro changes it to 128 MiB. The allocator rounds blocks to 8-byte alignment. A target compile-time probe verified alignof(std::max_align_t) is 16, so the current heap does not meet the ordinary C++ new alignment contract.

The kernel defines ordinary, array, and sized delete operations. It has no C++17 aligned new/delete overloads. Its global operator new and operator new[] are declared with throw() and return the allocator result directly. The allocator returns null on exhaustion instead of producing std::bad_alloc. The JavaScript source relies on bad_alloc catches around container and string growth, so its failure handling cannot be assumed to work with this allocation contract.

The kernel supplies basic memory functions and guard helpers. It does not provide the C heap, POSIX thread/TLS, Windows SEH imports, or full C++ exception runtime required by the selected MinGW archives. The exception allocator also uses malloc/free, distinct from the kernel arena.

RuntimeContext construction calls reset(), and reset clears realm state and initializes built-ins. This explicit lifecycle is useful, but it does not solve standard-library initialization or teardown. runtime.cpp also has a function-local static empty string. The kernel's simplified guard helpers and no-op destructor registration do not establish standard C++ object lifetime semantics for the engine.

## 8. Strategy decision and gates

| Strategy | JS113 finding |
|---|---|
| A — Isolated C++17 component | Not supportable with the installed target runtime. The available library rejects required headers under freestanding mode; its exception libraries depend on MinGW/Windows and POSIX services and fail against the kernel linker profile. |
| B — Narrow compatibility adaptation | Rejected for this phase. Replacing strings, vectors, conversion, and exception-based allocation failure with a smaller portability layer would be a broad semantic and regression-sensitive engine change. |
| C — Existing guideXOS freestanding C++ library | None found. kernel/core/cxx_runtime.cpp supplies basic kernel ABI helpers and a small allocator, not an STL or unwinder. |

No supported integration strategy is available in the current checkout and toolchain. The chosen disposition is Outcome C, blocked on a specific target C++ library/runtime prerequisite.

| Gate | Result |
|---|---|
| G1A — Required standard headers resolve in a target-compatible freestanding configuration | Fail |
| G1B — C++17 language syntax | Pass independently |
| G1C — Accepted core source closure compiles | Partial: 2 of 8 production translation units |
| G2A — Dependency-complete core link | Not reached; the separate exception-runtime link probe failed |
| G2B — ABI, allocation, exception, and lifetime contract | Not passed |
| G3 — Ready to attach to NavigatorApp | Not passed |

No C ABI entry interface was added because a safe runtime does not exist to implement it. The future Navigator attachment point is the built-in NavigatorApp at kernel/core/include/kernel/kernel_apps.h:563 and its implementation in kernel/core/kernel_apps.cpp. A later integration should own an explicit RuntimeContext instance, call its reset/execute lifecycle after the guest document is constructed, and use a bounded C ABI that converts all C++ failures into explicit result/error data before control returns to kernel code. The accepted NavigatorScriptHostAdapter targets guideWeb document types; a separate narrow adapter to the guest Navigator document model would still need design work. This is a recommendation, not a passed G3 result.

## 9. Stack, memory, package, and regression status

The target compiler reported static stack frames of at most 8 bytes for the compiled value.cpp and host.cpp functions. The isolated exception probe frame was 48 bytes. These are not interpreter or JSON frames. No target stack data exists for AST parsing, function invocation, JSON parse/stringify, or reviver traversal because those translation units did not compile. AST/runtime object sizes, linked image size, allocation peaks, and guest stack high-water remain unknown.

The configured production kernel heap is 32 MiB. This capacity is not evidence of available headroom or of runtime memory safety. JS112's approximately 1.66 MiB maximal-reviver payload estimate and its hosted frame measurements remain hosted/source estimates; JS113 did not convert them into target evidence.

The active checkout still has ESP/ramdisk.img but no ESP/EFI/BOOT/BOOTX64.EFI, ESP/kernel.elf, or current kernel/build/amd64/bin outputs. No source-matched EFI/kernel package was created. Guest boot, QAC admission, and QEMU remain unattempted as required.

No JavaScript runtime source changed, so JS109, JS110, the 111-lane matrix, hosted fixtures, and the hosted aggregate were not rerun. The prior accepted baseline remains JS109 86/86, JS110 28/28, matrix 111/111 lanes with 17,656/17,656 assertions, and hosted aggregate 767 passed / 7 established CSS failures / 774 total. These are prior results, not JS113 results.

Preservation verification after the probes found:

- guideXOSServer.exe: 8,398,274 bytes, SHA-256 A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971; matches the protected baseline.
- navigator-smoke/generated: 42 files / 458,372 bytes; changed 0, missing 0, extra 0; every SHA-256 matches the JS112 saved manifest.
- .phase absent.

## 10. Recommended JS114 scope

First provide an explicitly supported AMD64 freestanding C++17 environment with the required string/vector/array and numeric-conversion headers, a compatible C++ ABI library, exception allocation and unwinding that preserve the guest's unwind metadata, a kernel-compatible allocation bridge with 16-byte alignment and defined exhaustion behavior, and explicit construction/teardown rules. Resolve the MinGW SEH/COFF relocation and Windows/POSIX runtime dependencies with target-specific evidence.

If such a runtime cannot be supported, make a deliberate architecture decision to refactor the accepted interpreter's portability boundary and error propagation. Preserve JavaScript semantics and hosted behavior, add focused regression evidence, then re-run the JS109/JS110 suites and hosted aggregate.

After the C++ prerequisite is established, proceed to a static NavigatorApp adapter and a bounded C ABI. Only after that should a later phase prepare a source-matched package and request proper QAC admission for guest qualification.

## 11. Evidence index

Run directory: E:\CodexArtifacts\guideXOSServer_NAVIGATOR_JS_SUPPORT\JS113\run-20261108-c3eb1bbd

- Toolchain inventory and paths: toolchain/
- Header and C++17 probes: header-probes/ and logs/
- Accepted translation-unit results: source-build/source-compilation-results.json and logs/source-*.log
- Source dependency graph: dependency-audit/source-dependency-inventory.md
- Exception/runtime link diagnostics: link-probes/ and logs/exception-link.log
- Stack and initialization probes: memory-stack-evidence/
- Gate results: dependency-audit/gate-results.json
- Final preservation verification: dependency-audit/final-preservation-verification.json
- Probe hashes and artifact manifest: dependency-audit/
