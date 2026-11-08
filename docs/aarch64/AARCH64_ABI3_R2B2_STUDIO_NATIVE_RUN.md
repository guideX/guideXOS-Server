# AArch64 ABI3 R2B2 Developer Studio Native Run

- **Outcome:** `STUDIO_RUN_LOADER_BLOCKED` (Outcome C).
- **Qualification date:** 2026-11-08.
- **Repository:** `AARCH64_SUPPORT`, source candidate `963e497d17c4f26e1097ab86866a809d5f26d692`.
- **Scope:** audit the genuine Studio Run path after the accepted R2B1 guest Build workflow. The candidate was built and hosted gates were run. No R2B2 guest was started, and no child image was executed.

The loader currently enters the child inside the kernel's shared translation and privilege context. The Run callback also invokes that entry synchronously, without a time bound or effective interruption path. Because the child cannot be isolated safely from Studio and the kernel, execution was withheld before requesting QAC admission. The source audit therefore meets Outcome C; this is not a QAC scheduling outcome and not a partial guest Run.

## Provenance and preserved evidence

Preflight found branch `AARCH64_SUPPORT` at `963e497d17c4f26e1097ab86866a809d5f26d692`, with a clean worktree and `origin/AARCH64_SUPPORT` at the same commit (0 ahead, 0 behind). This differs from the brief's historical report of 15 commits ahead. No reset, rebase, merge, or push was performed.

The accepted R2B1 record remains [AARCH64_ABI3_R2B1_STUDIO_NATIVE_BUILD.md](AARCH64_ABI3_R2B1_STUDIO_NATIVE_BUILD.md). Its four fresh QAC-managed boots completed authentic Studio Build, malformed-source rejection, stale project-local output removal, changed-source recovery, VFS publication failure rejection, and Studio cleanup. Those sessions validated artifacts; they did not invoke Run. The accepted R2A project/VFS/editor evidence and R1e input/redraw evidence remain in their original reports and are not reclassified.

The R2B1 compiler witness remains 37 bytes:

```c
int gx_main(void* ctx) { return 42; }
```

It produced a 4,108-byte ELF64 little-endian `ET_EXEC`, `EM_AARCH64`, image base `0x51000000`, entry `0x51001000`, SHA-256 `76c5a81849c6f9372432a5e164563d10226a5c8ae211f4a16a8e70b5f09e48b2`. The recovery source returning 43 produced SHA-256 `e636e87fca022d1e2d4b4068067e2a7d19f59a626d24c28872c4fe23ebecc8d6`. Neither artifact has R2B2 execution evidence.

## R2B2 candidate identity

The source-matched candidate was built into `out/aarch64-r2b2-candidate-20261108-963e497d` with the Phase 12 builder in production Studio mode. The builder enforces output beneath the repository's `out` directory, so the candidate was kept there and did not reuse any accepted output folder. The complete builder manifest was preserved unchanged; its historical profile string is `r2b1-native-build-ui`. A separate `r2b2-candidate-manifest.json` embeds that complete manifest and records the R2B2 outcome and input hashes.

| Artifact | SHA-256 |
| --- | --- |
| Base candidate manifest | `18bb9d752a0bf5d413683c94a6366591a4cf5f944db19e76ad8b7b787002999b` |
| R2B2 sidecar manifest | `8224e7ad69583d704d937dd9ea3641dd56b5b01625737d14aefe2b1d983d0bbc` |
| ARM64 kernel ELF | `a51ea98ed048a0ea0fa5c1d6b66a47bb55d170757897d448aeae39f3b6c15592` |
| AArch64 EFI loader | `1605c55a4d7c0c7b0589bd181e28d2e32f7ef9da31a6aeb23695c237ff7bfe42` |
| Developer Studio ELF | `a08fc9c4599475165874a983b7d11366dcac2f35f5dc377ad6e875a960f2e483` |
| Studio package manifest | `62af7dce96baa5605092bb561e6c4bfe5428367bff8bbb229d4f01d41691ad4c` |
| RAM disk | `a2d1a9b3d8d8c346adba42e2d90e62a63a82250f1970f138c8eaa1fa3c6d84b1` |

These five guest artifacts are byte-identical to the accepted R2B1 candidate. Studio is pinned at `058c83fe1ae270a103eadc7caca961328e8abe2a`; its v0.5 SDK contract is `f63a7710a30b49f785aac134abf06a3a6c5b5aca`. The sidecar records SHA-256 fingerprints for the relevant compiler sources, both SDK header sets, the bare-metal loader, and the typed ABI adapter. No fresh firmware variables or guest output directory were created because there was no guest boot.

## Actual Run path and ABI

The source-level dispatch path is:

```text
Studio Run controller and BuildResult
  -> v0.5 464-byte bare-metal Run callback table
  -> typed adapter translates callbacks to the AARCH64 ABI-v1 400-byte table
  -> phase12_run_prepare resolves an App Model package by projectId
  -> phase12_run_start
  -> phase12_run_package(package, nested=true)
  -> phase9_load_and_run
  -> arm64_load_image
  -> gxos_native_call_on_stack(entry, app_context, stack_top)
  -> ARM64 entry via BLR
```

The v0.5 callback signatures are `prepare(ctx, const request*, outHandle*, outSnapshot*)`, `start(ctx, handle)`, `poll(ctx, handle, outSnapshot*)`, `request_close(ctx, handle)`, and `release(ctx, handle)`, each returning `gx_result`. The adapter accepts the v0.5 Run request version, validates its Studio-owned pointers, bounds and copies its strings into adapter-local buffers, translates it to the AARCH64 Run API v2 request, and translates the resulting snapshot back field by field. The request strings are borrowed for the duration of the prepare callback and are not retained by the service; the service stores its handle, owner, package pointer, and a copied requested digest. The output handle and snapshot are written to caller-owned Studio memory after the adapter checks their ranges. The start, poll, close, and release calls retain only the numeric handle across calls. The adapter does not wire a functional child cancellation operation.

The 464-byte Studio v0.5 table and 400-byte server ABI-v1 table remain separate types; the adapter copies fields and forwards typed callbacks. The adapter is selected for the Studio package using its explicit `guidexos-c-abi-v1-studio-v05-464` identity. That identity does not automatically become the compiled child's ABI. App Model package parsing requires `NativeElf`, runtime `native-elf`, and entry point `gx_main`; it accepts the explicit AARCH64 ABI-v1 identity `guidexos-c-abi-v1` or the supported Studio v0.5 identity, and rejects an unknown ABI before entry dispatch. The generated minimal child is a compiler-built `gx_main` with no host-call imports; it does not inherit Studio's 464-byte callback table. The nested loader passes a `gx_app_context*` in `x0`, switches to its allocated stack, calls the validated entry with `blr`, and treats returned `x0` as the child result.

The pinned Studio `RunRequestFromBuild` requires a successful BuildResult and checks project identity/root, target profile, artifact architecture/path, source-membership hash, artifact validity, and entry-point presence; it forwards artifact path, SHA-256, size, architecture, and ABI. This is a useful Studio-side guard. The bare-metal service still validates only the request envelope, project ID, and target profile, then uses `projectId` to refresh/discover and resolve an App Model package. It does not bind the resolved executable to the requested Build's artifact path, hash, byte size, architecture, or ABI identity. Instead, it copies the caller's requested SHA-256 into the Run snapshot without comparing that value to the resolved executable. Studio's checks therefore do not prove that the resolved package executable is the exact immediately preceding Build. R2B1 intentionally retains the last published package when a later Build fails; the genuine Studio controller rejects a failed BuildResult, but the separate App Model package still has no verified hash relationship to a subsequent successful project-local Build. The service's project-local stale-output removal does not establish freshness of the resolved package.

## Loader and lifecycle audit

The ARM64 loader keeps the existing strict fixed-address contract. `arm64_load_image` checks ELF64 little-endian identity, `ET_EXEC`, `EM_AARCH64`, program-header bounds, segment alignment and overlap, executable entry coverage, image-size bounds, and rejects writable-plus-executable segments. It requires the minimum load address to equal the selected preferred base; nested Run uses `0x51000000`. It allocates the image at that exact address, zeroes the whole image span before copying file bytes (so BSS is zero-initialized), applies segment permissions, and synchronizes the instruction cache. The entry must fall within an executable load segment.

The nested runtime allocates a 16-page stack (64 KiB), writes low/high canaries, marks it writable and non-executable, and passes a 16-byte-aligned top-of-stack to the assembly bridge. It releases the image and stack and restores image permissions after the entry returns. These are useful loader bounds and cleanup checks, but they do not form a child address space.

### Safety blocker: no child address-space or privilege isolation

`allocate_pages_at` and `phase2_mmu_set_range_permissions` operate on the active kernel memory regime. `native_elf_call.S` saves registers, changes SP, and performs `blr`; it does not install child page tables, switch TTBR, enter EL0, or trap child service calls across a privilege boundary. Studio at `0x50000000` and a child at `0x51000000` have different fixed bases, so this prevents exact image-base overlap, but both remain mapped under the same kernel translation regime and execute at the kernel's privilege level. A child can therefore address kernel and Studio mappings directly. Permission changes on the shared regime also cannot provide per-child isolation.

The code has page allocation/release and a separate stack, but it does not establish child ownership over an isolated address space. Bounded stack size and fixed placement do not satisfy the guest-memory-boundary or child-safety requirement. The user brief explicitly requires reporting this blocker before child execution; no child entry was invoked.

### Lifecycle blocker: synchronous and unbounded Run

`phase12_run_start` directly calls `phase12_run_package`, which descends through `phase9_load_and_run` to the child entry before returning from the callback. The assembly call stays on the suspended Studio/host call path until the child returns. There is no separately scheduled child task, execution budget, timeout, or working cancellation mechanism. `phase12_run_request_close` returns success after start but does not interrupt the active child. A child that loops or faults can keep Studio from processing events and can prevent normal teardown. The Run controller's polling after `start` cannot make this execution asynchronous.

### Completion is not reported truthfully for arbitrary return values

The nested loader's cleanup predicate accepts the child result only when it equals 42. `phase12_run_start` then writes snapshot exit code 42 for any successful boolean result and -1 for failure. Consequently, 42 is an assumed sentinel rather than a captured generic child exit value; a return of 43 is treated as launch failure and is not reported as a distinct completed result. A loader callback returning success would not independently prove the child's instructions ran, and no such callback result is being claimed here.

## Hosted and build evidence

The production candidate build and these requested hosted checks completed without launching QEMU:

| Gate | Result |
| --- | --- |
| Studio ABI-v1/v0.5 alignment | PASS: 400-byte and 464-byte layouts remain distinct; incompatible SDK activation consumer rejected; activation checks 9/9. |
| Studio v0.5 adapter | PASS: 175/175 assertions. |
| Pinned Studio host/model suite | PASS, including project/model/parser and Run-controller checks. This is hosted model evidence only. |
| ARM64 compiler and bootstrap regressions | PASS. |
| Compiler Build-service host regression | PASS; generated ELF digest matches the R2B1 return-42 artifact. |
| Phase 10 resolver/ELF host controls | PASS, including missing, unknown, wrong machine, traversal, malformed, oversized, duplicate, and legacy controls. These are common resolver/ELF tests, not an emulator of the bare-metal ARM64 Run path. |
| Phase 11 builder and freestanding package staging | PASS; no guest started. |
| Render/input lifecycle host suite | PASS: 1,380 checks and 100/100 lifecycle cases. |
| Captured firmware DTB parser | PASS: 52 assertions on the preserved 1,052,672-byte source DTB. |
| Phase 2 builder host suite | PASS: 51 assertions. |
| Phase 3 scheduler contract | PASS. |
| Phase 4 allocator/VFS and SCHEDR1 host controls | PASS. |
| GICv3 hosted suite | PASS: 62 assertions. |
| Phase 9 lifecycle controls | PASS when manually compiled with the SDK include path: handle forgery/staleness, cross-app, wrong-kind, quotas, and cleanup. |
| Packaged Phase 9 host builder | FAIL before running tests: omitted the SDK include directory and could not find `guidexos/app.h` through `aarch64_gui_render.h`. |
| Native filesystem contract script | FAIL at link time: unresolved `AppPayloadResolver` and `NativeArchitectureFromString` symbols from the manifest validator/registry source set. No test executable was produced or run. |

The Phase 9 packaged-build failure and native filesystem link failure are retained as failures; neither is counted as a pass. The ABI3 adapter and Phase 10 host controls validate callback translation and generic ELF constraints, not child isolation, actual bare-metal entry, scheduler behavior during a child run, or guest result delivery.

## QAC and guest result

At preflight the QAC coordinator was online with zero managed sessions, queued requests, blocked requests, or reservations; QEMU process count was zero. No QAC admission request was made because the safety audit had already established that child execution was not safe. The candidate was not booted, Studio did not execute the Run action in a guest, and no child was run.

| Requested guest evidence | R2B2 result |
| --- | --- |
| Fresh authorized R2B2 boots | 0 |
| Studio Run action in guest | Not attempted |
| R2B1 artifact loaded by the R2B2 bare-metal Run service | Not attempted |
| Child entry or executed ARM64 instructions | None observed |
| Return 42 or recovery return 43 at runtime | No runtime result |
| Studio responsiveness after child completion | Not observed |
| Child/Studio cleanup in an R2B2 guest | Not observed |
| Unexpected IRQs or exceptions | Not measured; there is no R2B2 guest record. |
| Physical ARM64 hardware | Not tested |

## Work required before a controlled Run guest

The next implementation must establish a child-owned address space or an equivalent isolation boundary that protects kernel and Studio mappings, with a defined privilege and host-call transition. The fixed-address image contract must remain valid inside that boundary. Run should execute in a separately scheduled child context with a bounded execution budget and cancellation/close semantics that Studio can service. Preparation must bind the resolved executable to the exact preceding BuildResult and fail closed on stale, missing, wrong-architecture, or incompatible-identity artifacts. Completion must preserve the actual child result and distinguish return 42 from return 43, followed by teardown that Studio can observe.

After those changes, add focused hosted tests for isolation setup, result delivery, artifact identity, cancellation, and cleanup; then run one authorized diagnostic guest before any repeatability campaign. Three fresh QAC-exclusive boots are still required for Outcome A, including at least one malformed-source and recovery sequence. Preserve R1e/R2A/R2B1 accepted history and their partial attempts. No debugger, cancellation, or physical-hardware qualification is claimed here.
