# Navigator JavaScript JS112 Guest Integration

| Field | Result |
|---|---|
| Date | 2026-11-07 (America/Los_Angeles) |
| Outcome | C — `JS112_INTEGRATION_BLOCKED` |
| Highest fully passed integration gate | None; G1 was attempted and failed. |

**Qualification source commit:** `9fd1b7362143dac1f33db0dfd0578f6bb533bc47`

## 1. Disposition

JS112 did not integrate or execute the accepted `navigator_javascript` runtime in a guideXOS guest. The first guest-target translation unit failed before producing an object: the kernel's actual AMD64 flags include `-nostdinc++`, and `navigator_javascript/ast.cpp` reaches the C++ standard header `<cstddef>` through `ast.h` and `lexer.h`. The same target profile also uses C++14 and disables exceptions, while the accepted hosted build uses C++17 and the runtime uses C++ standard-library containers and exception handling for allocation failures.

The smallest route for the requested Navigator behavior is static integration with the existing compiled-in kernel `NavigatorApp`. That route would retain the existing guest app model and avoid adding a server ELF loader. It cannot pass G1 under the current kernel toolchain/runtime contract. No STL replacement, exception runtime, or loader was introduced as an incidental Navigator change.

JS111's main conclusion remains valid: the accepted runtime is absent from the freestanding guest, and no source-matched guest candidate exists. One refinement from the JS112 source audit is that this checkout also contains a separate bare-metal `NativeElf` application launcher. It is not the unimplemented `guideXOSServer` init-process loader, does not contain the JavaScript runtime, and was not booted or qualified for JS112.

No guest object or kernel was linked, no candidate was packaged, no QAC reservation was requested, and no QEMU process was launched. This is an integration/build prerequisite block, not a QAC deferral and not an observed guest safety failure.

## 2. Starting source and workstation state

| Item | Verified state |
|---|---|
| Host | `LEON2_RDT` |
| Active Navigator JavaScript checkout | `D:\dev\guideXOSServer_NAVIGATOR_JS_SUPPORT` |
| Repository root | `D:\dev\guideXOSServer_NAVIGATOR_JS_SUPPORT` |
| Branch | `NAVIGATOR_JAVASCRIPT_SUPPORT` |
| Starting HEAD | `9fd1b7362143dac1f33db0dfd0578f6bb533bc47` |
| Starting worktree | Clean; no tracked or untracked changes |
| Cached upstream divergence at start | 0 ahead / 0 behind |
| `.phase` | Absent |
| Toolchain/artifact volume | MinGW tools on `C:`; existing JS111 evidence on `E:\CodexArtifacts` |

The accepted starting commit matches the JS111 documentation commit. No fetch or branch operation was performed.

Related guest workspaces were found without switching or modifying them. `D:\dev\guideXOSServer` is on `main` at `f5646c5fdf269438d28fb075071e56db47d6c11e`, seven commits ahead of its cached upstream, and has the pre-existing untracked file `assets/Backgrounds/troncity.png`. It has `ESP\EFI\BOOT\BOOTX64.EFI`, `ESP\kernel.elf`, and `ESP\ramdisk.img`, but no `navigator_javascript\runtime.cpp`; it is not a source-matched JS112 candidate. `D:\dev\guideXOSServer_AARCH64` also exists and was left untouched. No candidate from either checkout was substituted for the active accepted source tree.

## 3. JS111 blocker reconciliation

The JS111 report at `docs/NAVIGATOR_JAVASCRIPT_JS111_QUALIFICATION.md` was checked against the current source:

* `build.bat` compiles `navigator.cpp` and the eight accepted JavaScript translation units: `ast.cpp`, `value.cpp`, `host.cpp`, `environment.cpp`, `runtime.cpp`, `lexer.cpp`, `parser.cpp`, and `navigator_script_host.cpp` (`build.bat:41,97-104,144`). It uses `-std=c++17`.
* The guest kernel source target is separate. `kernel/Makefile:119-121` gathers only `kernel/core/*.cpp` and `kernel/arch/$(ARCH)/*.cpp`; it does not include `navigator_javascript` or hosted `navigator.cpp`.
* The guest Navigator is the capability-limited `NavigatorApp` in `kernel/core/include/kernel/kernel_apps.h:23,555-563` and `kernel/core/kernel_apps.cpp`. Its own page text says it renders local guideWeb documents “without requiring CSS, JavaScript, or a network connection” (`kernel/core/kernel_apps.cpp:14388`). Apart from that capability text, those kernel Navigator files have no accepted JavaScript runtime or JSON function references.
* `docs/ARCHITECTURE.md:155,175` still marks loading `guideXOSServer` from the ramdisk and the kernel ELF loader as TODO. `kernel/core/process.cpp` contains only a small process table stub; its `schedule()` halts rather than switching into a user process.
* A distinct `kernel/core/native_elf_baremetal.cpp` path exists for `NativeElf` applications discovered under `/Apps`. `run_package()` loads an image, allocates a 128 KiB stack, and calls the app entry through `gxos_native_call_on_stack` (`:1146-1179`; `launch()` at `:1217`). This is not the `guideXOSServer` init loader. The source demonstrates an entry-call implementation, but JS112 did not boot a package or treat that source alone as guest execution evidence.

Thus JS111 correctly identified the source/build split and missing server-init loading path. The added finding is a separate in-kernel NativeElf app route that may be evaluated for a standalone runtime smoke after the accepted runtime can compile. It does not resolve G1 or provide the Navigator DOM host path.

## 4. Windows-hosted implementation path

The Windows-hosted path is source-matched to the accepted runtime:

1. `build.bat` compiles the eight JavaScript translation units with the rest of the hosted Navigator using MinGW C++17.
2. `Navigator::s_scriptRuntime` and `Navigator::s_scriptHostAdapter` are static members defined in `navigator.cpp:87-88`. The hosted process startup constructs them through the Windows C++ runtime.
3. `RuntimeContext::RuntimeContext` calls `reset()` (`navigator_javascript/runtime.cpp:5385`). Reset clears realm state and initializes built-ins.
4. The built-in registration path creates the native JSON parse and stringify functions (`runtime.cpp:6040-6043`); the `JSON` built-in resolves those function IDs at `runtime.cpp:1358-1361`. The implementations are `parseJson` at `:2785` and `stringifyJson` at `:3462`.
5. On navigation, `Navigator::resetJavaScriptRealmForNavigation` detaches the prior document, installs the host adapter, resets the runtime, and records reset failures (`navigator.cpp:176-204`). After the replacement document is installed, `loadUrl` attaches the adapter and invokes `executeJavaScriptDocumentScripts` (`navigator.cpp:20723-20726,20852-20853`). The script method installs the `document` global and calls `executeInSameRealm` (`navigator.cpp:208-224`).
6. Runtime errors are translated with `runtimeErrorCodeName`, retained in `s_lastJavaScriptError`, and written through `Logger::write` (`navigator.cpp:176-184`).
7. `NavigatorScriptHostAdapter` bridges runtime host objects to `gxos::web::WebDocument`; its dynamic element creation and block projection paths are in `navigator_javascript/navigator_script_host.cpp` (for example `createDynamicElement` at `:4250` and `rebuildDynamicBlockProjections` at `:4407`). The hosted Navigator then resolves and renders the updated document.

The hosted `navigator.cpp` path also uses platform file, stream, networking, logging, and Windows APIs. Those services are not part of the accepted interpreter core and are not present in the kernel Navigator build.

## 5. Freestanding guest implementation and loading paths

The guest has three distinct models that must not be conflated:

| Model | Actual source path | JS112 relevance |
|---|---|---|
| Built-in bare-metal Navigator | `kernel/core/kernel_apps.cpp`, `NavigatorApp` in `kernel/core/include/kernel/kernel_apps.h` | Smallest path for Navigator page/DOM integration, but currently has no accepted runtime or script execution. |
| Native external app | `kernel/core/native_elf_baremetal.cpp`, manifest-backed `NativeElf` packages in `/Apps` | An existing app loader/entry path could host an isolated smoke if a compatible runtime app can be built. Its ABI is `guidexos-c-abi-v1`; it does not make an app a script-capable Navigator page. |
| `guideXOSServer` as init process | `kernel/core/process.cpp` and the documented ramdisk path | Not implemented; the kernel ELF loader and user-mode transition remain TODO. |

The UEFI bootloader source (`guideXOSBootLoader/main.cpp`, `elf.cpp`) is a separate boot path for loading the kernel. Its presence is not proof that a current boot package exists or that it loads the server as a user process. No dynamic JS module interface was found.

The active checkout's `ESP` contains a 64 MiB `ramdisk.img`, `startup.nsh`, a `PacMan` app package, and `build-identity.txt`. It lacks `ESP\EFI\BOOT\BOOTX64.EFI`, `ESP\kernel.elf`, and `ESP\EFI\BOOT\BOOTAA64.EFI`. Its identity file points to `D:\dev\guideXOSServerV0.2` and an August 2026 build, so the recorded old hashes are not provenance for the current accepted source. The current kernel build output paths `kernel\build\amd64\bin\kernel.pe` and `kernel.elf` are absent. The related `D:\dev\guideXOSServer` package is complete but has a different source checkout and no accepted JavaScript runtime.

`build.ps1` contains packaging steps that write into `ESP` and a `-Clean` path that removes it. It was inspected but not run. `build.bat` can replace the protected hosted executable and was also not run.

## 6. Runtime source closure and freestanding dependencies

The accepted production closure is:

`navigator_javascript/{ast.cpp,value.cpp,host.cpp,environment.cpp,runtime.cpp,lexer.cpp,parser.cpp,navigator_script_host.cpp}` plus their headers; DOM integration additionally depends on `guide_web_document.h` and `guide_web_html_parser.h`.

| Dependency class | Use in accepted source | Guest evidence/status |
|---|---|---|
| C++ standard headers/containers | `<cstddef>`, `<cstdint>`, `<array>`, `<string>`, `<vector>`, `<memory>`, `<algorithm>`, `<charconv>`, `<cmath>`, `<cctype>`, `<cstring>`, `<iterator>`, `<limits>`, `<new>`, and `<utility>`; AST, environments, callbacks, JSON traversal, and host records use standard containers. | Kernel uses `-nostdinc++`; the probe failed at the first `<cstddef>`. No compatible STL is supplied to this target. Hosted `navigator.cpp` separately uses facilities such as `<unordered_map>` and `<fstream>`, outside the runtime closure. |
| Allocation/deallocation | `std::string`, vectors, maps, unique pointers, and callbacks allocate. The implementation catches `std::bad_alloc` in AST, environment, runtime, and host paths. | `kernel/core/cxx_runtime.cpp` has a custom `operator new/delete` backed by a fixed free-list arena, but it is not a standard-library implementation. Production heap is 32 MiB; a diagnostic Navigator HTTP build selects 128 MiB. The allocator uses 8-byte alignment and does not establish compatibility with all C++17/STL allocation requirements. |
| Global and static initialization | Hosted Navigator owns static `RuntimeContext` and `NavigatorScriptHostAdapter` instances. The runtime constructor resets and initializes built-ins. | Kernel `__cxa_guard_*` helpers are simple single-threaded guards; `__cxa_atexit` is a no-op. The inspected AMD64 linker script has no explicit `.init_array` handling, and constructor startup was not established. Global construction or teardown is not qualified for an integrated runtime. |
| Exceptions | `try`/`catch` and `std::bad_alloc` handle bounded allocation failures across runtime operations. | Kernel compiles with `-fno-exceptions`; its custom C++ support does not provide the exception machinery used by these sources. |
| RTTI | No `dynamic_cast` or `typeid` dependency was found in the accepted closure. | Kernel uses `-fno-rtti`; no RTTI requirement was observed for this source set. |
| Thread-local storage/synchronization | No `thread_local`, standard mutex, or condition-variable dependency was found in the accepted closure. | No guest TLS or synchronization path was exercised; the kernel guard helper explicitly assumes single-threaded initialization. |
| Files, streams, OS handles | The interpreter core is passed script source and has no direct file or stream API. Hosted `navigator.cpp` handles file/network/Win32 services outside the freestanding source list. | No corresponding guest service is required for the standalone runtime core; hosted services cannot be carried across as guest evidence. |
| Numeric/locale/time helpers | JSON number parsing is project code; host adapter uses `std::from_chars`; JSON serialization uses `std::isfinite`. No locale-sensitive `strtod` is used by the JSON number parser (`runtime.cpp:80,269`). | Floating-point target support was not compiled. The AMD64 kernel flags disable SSE/SSE2, so the required floating-point lowering/runtime remains unverified. No locale or clock dependency was found in the accepted closure. |
| Unicode | The accepted source uses project-owned string/escape handling; no external Unicode library is in the listed closure. | Guest parity and non-ASCII behavior were not run. No Unicode expansion was attempted. |
| Compiler built-ins/runtime | Memory intrinsics and C++ ABI helpers are supported by `kernel/core/cxx_runtime.cpp`; the kernel link adds the compiler's `libgcc` archive. | This minimal support does not provide the missing standard headers, containers, exception handling, or verified floating-point runtime. |
| Linker sections/symbols | Hosted link uses the Windows toolchain/CRT. | Kernel's AMD64 profile links PE first and converts it to ELF64; it links with a kernel linker script and compiler runtime only. It discards exception frame sections and has no verified runtime initialization path for this interpreter. |
| Navigator document/renderer | `navigator_script_host.cpp` uses the hosted `WebDocument`, HTML element refs, and dynamic `DocBlock` projections. | Guest `NavigatorApp` uses a different kernel-side document/render adapter. No source-parity bridge was added or tested. |

No STL replacement, new general C++ runtime, or new loader was introduced.

## 7. Guest target and G1 compile probe

The active kernel profile is AMD64. On this workstation, `kernel/arch/amd64/Makefile.arch` selects `g++`/`ld` on Windows, with `-m64 -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2`, non-PIC/non-PIE flags, and a PE image base at `0x100000`. `kernel/Makefile:96-97` adds `-std=c++14 -ffreestanding -fno-exceptions -fno-rtti -nostdlib -nostdinc++ -fno-builtin`; source lists are `core/*.cpp` and `arch/amd64/*.cpp` (`:119-127`). The AMD64 make path links PE/COFF and converts with `objcopy -I pei-x86-64 -O elf64-x86-64` (`:154-170`).

Available compiler: MinGW-W64 GCC 15.2.0, target triple `x86_64-w64-mingw32`. No `x86_64-elf-g++`, `clang++`, or `aarch64-linux-gnu-g++` command was found on `PATH`. The guest objects therefore use the installed MinGW Windows target/COFF ABI before conversion; a separate SysV `x86_64-elf` toolchain was not available. The existing NativeElf trampoline documents the Microsoft x64 register ABI and aligns its app stack to 16 bytes with 32 bytes of shadow space (`kernel/arch/amd64/native_elf_call.asm`). That does not establish JS runtime call-frame or stack safety. C++ constructor startup, unwind support, and ABI compatibility for the JS runtime were not inferred from the host build.

The accepted closure also uses `std::from_chars` (`navigator_script_host.cpp:245`, `runtime.cpp` number conversion), which is a C++17 library facility; the kernel profile selects C++14. The missing header was the first observed failure, so later language/library diagnostics were not collected.

The smallest probe compiled `navigator_javascript/ast.cpp` using the AMD64 kernel's C++14 freestanding flags and a separate output path. It exited 1 before object emission:

```text
In file included from ...\navigator_javascript\lexer.h:3,
                 from ...\navigator_javascript\ast.h:3,
                 from ...\navigator_javascript\ast.cpp:1:
...\navigator_javascript\source.h:3:10: fatal error: cstddef: No such file or directory
    3 | #include <cstddef>
      |          ^~~~~~~~~~
compilation terminated.
```

This is the exact effect of the configured `-nostdinc++` profile. No speculative include-path or exception fixes were applied. No accepted JS object was produced, so no link command was attempted. The captured command and output are at:

`E:\CodexArtifacts\guideXOSServer_NAVIGATOR_JS_SUPPORT\JS112\run-20261107-31ecea4f026b\compiler-probes\amd64-kernel-profile-ast.log`

The compile log SHA-256 is `E4D5A92A1D044DC774019E0C3E20FEAD955E3931FF5C7537C147595EF44482C7`; toolchain identity is recorded in the adjacent `toolchain.txt`.

## 8. Integration decision and gate results

**Selected architecture: Option A, static integration with the existing kernel `NavigatorApp`, after a separately approved freestanding C++ runtime/toolchain contract exists.** It is the smallest path to the desired page integration because the guest Navigator already runs as a built-in `KernelApp`; it does not depend on the unimplemented `guideXOSServer` user-mode loader. It still requires explicit runtime construction, a supported allocator and error model, and a narrow adapter from the accepted host bindings to the kernel document/renderer.

The NativeElf app model is a possible separate runtime-smoke route, not the selected Navigator page route. Its ABI and loader are distinct from the accepted C++ runtime, and no source-matched JS app or boot was produced. A dynamic module route was not found. Option D (implementing a general server ELF loader) is not needed for the selected static route and remains outside JS112 scope.

| Gate | Result | Evidence |
|---|---|---|
| G1 — Compiles | **Fail** | One actual target TU attempted; `<cstddef>` unavailable under guest flags; exit 1. |
| G2 — Links | Not reached | No JS object files. |
| G3 — Initializes | Not reached | No guest runtime instance, built-in registration, or error path. |
| G4 — Loads | Not reached | No JS-bearing guest package. Existing app loader source was not treated as a JS load result. |
| G5 — Executes | Not reached | No guest smoke result `24`. |
| G6 — JSON-qualified | Not reached | No guest parse/reviver/stringify or negative tests. |
| G7 — Navigator integration | Not reached | No guest script execution, DOM mutation, projection, selector, layout, or rendered JSON content. |
| G8 — Safety-qualified | Not reached | No guest JS memory, stack high-water, lifecycle, teardown, or failure evidence. |

The actual guest `typeof` subset, minimal script result, JSON fixture behavior, own-property deletion, and accepted JS109–JS110 negative semantics were not tested on guideXOS.

## 9. Memory and stack evidence

The JS111/JS110 accepted hosted limits include JSON traversal depth 32, strings up to 64 KiB, 256 KiB aggregate runtime string payload, 4,096 string values, 1,024 objects, 256 properties per object, 4,096 aggregate properties, 1,024 array elements per array, and 4,096 total array elements. The JS110 maximum reviver counted-payload estimate is approximately 1.66 MiB before allocator/container overhead, callback-created values, and unrelated runtime state.

The current production kernel heap is a fixed 32 MiB arena in `kernel/core/cxx_runtime.cpp:217-243`; the 128 MiB value is selected only for `GXOS_NAVIGATOR_HTTP_SMOKE_ACTIVE`. The bare-metal NativeElf path allocates an app stack of 128 KiB and caps its loaded image at 32 MiB (`kernel/core/native_elf_baremetal.cpp:28`); that is not a JS stack measurement. The kernel Navigator smoke source refers to a 16 KiB boot stack (`kernel/core/include/kernel/kernel_apps.h:803`, `kernel/core/kernel_apps.cpp:9626`). These are configured capacities, not free-memory or high-water observations for the JS runtime.

Existing hosted compiler frame evidence from JS111 is: `parseJson` 1,504 B; recursive parser helper 384 B; `stringifyJson` 848 B; serializer emitter 400 B; value preparation 416 B; `invokeFunction` 768 B; and `invokeNative` 736 B. Reviver traversal is iterative; stringify remains recursive. These host measurements are not guest stack evidence. The target probe failed before stack-usage output could be generated. No stack safety, heap sufficiency, navigation cleanup, or allocation-failure claim is made for guest JS.

## 10. Package, QAC, and guest execution

There is no source-matched candidate. The active checkout's `ESP` is missing the current EFI bootloader and kernel image; the old `build-identity.txt` points to V0.2 artifacts. The sibling `D:\dev\guideXOSServer` has a complete boot directory but no accepted runtime source and has a pre-existing untracked asset, so it was not altered or used.

QAC policy documentation was inspected in `D:\dev\guideXOS_QAC\README.md` and `D:\dev\guideXOS_QAC\docs\QAC_SHARED_QEMU_COORDINATION_PHASE4R2.md`. The CLI source project exists at `D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\GuideXOS.QAC.Cli.csproj`; the README documents `status`, `list`, and `queue` via `dotnet run`. No `qac` executable was found on `PATH`. The policy requires admission before a QEMU launch and forbids launching while queued or denied. Because there is no JS candidate, no live admission request or reservation was appropriate; this is **not** `JS112_DEFERRED_QAC_BUSY`. No QEMU launch, guest boot, serial capture, or result record exists for JS112.

## 11. Regression baseline and preservation

No JavaScript or host-integration source changed, so the JS109/JS110 suites and hosted aggregate were not rerun. The accepted JS111 baseline remains the last test evidence:

| Check | JS111 recorded result |
|---|---:|
| JS109 focused native suite | 86/86 |
| JS110 focused native suite | 28/28 |
| JavaScript matrix | 111/111 lanes; 17,656/17,656 assertions across 70 lanes |
| Hosted JS109 and JS110 fixtures | HTTP 200; no reported inline runtime error |
| Hosted Navigator aggregate | 767 passed / 7 established CSS failures / 774 total |

The seven CSS failures and the aggregate were not rerun in JS112; there is no new comparison evidence or JavaScript regression result.

The established CSS baseline consists of phase 3C baselines/clipping/hit targets/focus; phase 3G parser/containing-block diagnostics; phase 6A nested scrolling/fixed-descendant invariance; phase 6B typed sticky diagnostics; phase 6B local/nested sticky scrollports; phase 6B fixed-descendant viewport invariance; and phase 6C sticky/fixed/clipping/flex/extent neutrality.

Preservation was checked against the saved JS111 generated-fixture manifest during JS112:

* `guideXOSServer.exe`: 8,398,274 bytes; SHA-256 `A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971` (matches the protected value).
* `navigator-smoke/generated`: 42 files / 458,372 bytes; changed 0, missing 0, extra 0; every file's current size and SHA-256 matched the saved manifest.
* `.phase` remained absent.
* No source build was run; the protected executable and generated fixtures were not rewritten.

The full per-file verification is saved at:

`E:\CodexArtifacts\guideXOSServer_NAVIGATOR_JS_SUPPORT\JS112\run-20261107-31ecea4f026b\dependency-audit\preservation-verification.json`

Its SHA-256 is `D68DF2AFF5F07CCDC12A929F885C352A143C3CEE7DBEAB7C50979317FB8D3791`. Hashes for the compiler log, toolchain identity, and both preflight records are indexed in `dependency-audit\artifact-hashes.json`.

## 12. Remaining prerequisite and JS113 recommendation

The next decision belongs at the freestanding C++ runtime/toolchain boundary. Before another Navigator implementation pass, establish a supported way to compile the accepted C++17 closure with its required standard headers, containers, allocation behavior, and exception semantics—or make a separate architectural decision for an explicitly bounded adapter while preserving shared interpreter and JSON semantics. Confirm target ABI, global initialization, and teardown; do not silently change the accepted runtime's failure behavior to fit `-fno-exceptions`.

After that prerequisite is accepted, JS113 can resume at G1 with an isolated compile probe, then G2–G3 static integration in `NavigatorApp`. Only after those gates pass should the team build and hash a source-matched package, obtain QAC admission, run the deterministic runtime smoke, qualify JSON, and exercise the separate Navigator DOM/rendering and safety gates. No new JSON features or general ELF loader are recommended as part of that prerequisite work.
