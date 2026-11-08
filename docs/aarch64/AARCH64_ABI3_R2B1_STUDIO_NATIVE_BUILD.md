# AArch64 ABI3 R2B1 Developer Studio Native Build

- **Outcome:** `STUDIO_ARM64_NATIVE_BUILD_VERIFIED` (Outcome A).
- **Qualification date:** 2026-11-07 local time; the QAC run records are dated 2026-11-08 UTC.
- **Repository:** `AARCH64_SUPPORT`, source candidate `b75074e675864febee0bbf15d5d773e7ea4a9895`.
- **Scope:** authentic ARM64 Build from Developer Studio inside guideXOS. Executing the resulting program is deferred to R2B2.

Four fresh QAC-managed boots completed the positive Build, malformed-source rejection, stale-output removal, changed-source recovery, unavailable VFS path rejection, clean Studio close, GUI cleanup, and post-close desktop response gates. All four used the same frozen candidate manifest and guest artifact hashes. No QEMU was started outside QAC. No push was performed.

## Provenance and preserved R2A behavior

The request listed expected HEAD `9852ea115cd57399862158866cb2761c35a33a2b`; preflight found that commit as an ancestor and the checkout already at `b75074e6`, 12 local commits later and 14 commits ahead of `origin/AARCH64_SUPPORT`. The R2B1 implementation began at `bcdb119a`; subsequent commits repaired project metadata compatibility, FAT storage names, the QAC workflow, and post-close desktop input. The candidate manifest records a clean guest-source checkout. The only uncommitted changes during QAC qualification were host-side runner refinements; they did not alter candidate binaries. Those runner changes and this record are committed together below. The remote was not updated. No physical hardware was used.

R2A's accepted project workflow remains the source of the project and VFS contract: `/Apps/R2ATST`, project metadata open, real source editor, exact save and readback of `int gx_main(void* ctx) { return 42; }`, document/project close and reopen, and normal lifecycle cleanup. Its evidence remains in [AARCH64_ABI3_R2A_STUDIO_PROJECT_VFS_EDITOR.md](AARCH64_ABI3_R2A_STUDIO_PROJECT_VFS_EDITOR.md); R1e evidence remains in [AARCH64_ABI3_R1E_STUDIO_INPUT_REDRAW.md](AARCH64_ABI3_R1E_STUDIO_INPUT_REDRAW.md). Existing accepted and incomplete R1e/R2A sessions were retained.

The pinned Developer Studio source revision is `058c83fe1ae270a103eadc7caca961328e8abe2a`; its SDK revision is `f63a7710a30b49f785aac134abf06a3a6c5b5aca`. The candidate copied the pinned source into its build output without modifying the source checkout. The build overlay restores shared model files omitted by selected test source lists, adds same-signature compile compatibility aliases for `window_destroy` and two namespaced Build diagnostic helpers, and uses the copied build driver. The Studio smoke define is disabled. Neither public ABI layout was changed.

All R2B1 outputs and firmware-variable files use unique run directories. The earlier R2A record documents a historical default-directory invocation that overwrote two Phase 10 files; their original contents could not be reconstructed. That incident is preserved in the R2A report. No R2B1 run reused or rewrote an existing run directory.

## Frozen candidate identity

Candidate directory: `out/aarch64-r2b1-20261107-qualified-commit-b75074e6`.

| Item | Identity |
| --- | --- |
| Candidate profile | `r2b1-native-build-ui` |
| Candidate manifest SHA-256 | `c163292cffbe99396a48521806fdd0e51a943a484d2e616d20b8e7b5816e5c97` |
| Server source HEAD | `b75074e675864febee0bbf15d5d773e7ea4a9895` |
| ARM64 kernel ELF SHA-256 | `a51ea98ed048a0ea0fa5c1d6b66a47bb55d170757897d448aeae39f3b6c15592` |
| AArch64 UEFI loader SHA-256 | `1605c55a4d7c0c7b0589bd181e28d2e32f7ef9da31a6aeb23695c237ff7bfe42` |
| Studio ELF SHA-256 | `a08fc9c4599475165874a983b7d11366dcac2f35f5dc377ad6e875a960f2e483` |
| Studio package manifest SHA-256 | `62af7dce96baa5605092bb561e6c4bfe5428367bff8bbb229d4f01d41691ad4c` |
| RAM disk SHA-256 | `a2d1a9b3d8d8c346adba42e2d90e62a63a82250f1970f138c8eaa1fa3c6d84b1` |
| Studio package ABI identity | `guidexos-c-abi-v1-studio-v05-464` |
| QAC-managed guest | `virt,gic-version=2,acpi=off`, Cortex-A53, AArch64 UEFI |

Compiler source components compiled into this candidate are identified by these source hashes:

| Source | SHA-256 |
| --- | --- |
| `kernel/core/compiler/compiler_build_service.cpp` | `80d7d24eb395c28e8c2351fe6e83d25abfd9955493db95494c910eae2f041518` |
| `kernel/core/compiler/compiler_driver.cpp` | `57bdf8b6a89208adb56705c2198292751207a02ca84a3ef353eb2e1911d55f01` |
| `kernel/core/compiler/compiler_lexer.cpp` | `bb3a92671735e421dc5859b328f78f71affcd8c26aa98b0ac9f61e3221300212` |
| `kernel/core/compiler/compiler_parser.cpp` | `ff29e1954637b556c1d6369056e8a9ea5807b39efc5e4cb53041951bb047554a` |
| `kernel/core/compiler/compiler_target.cpp` | `731f9ad637e83e7c990f4a3fea778edafd0d81f7f8f787bdeafcd9eb8b6a9ae6` |
| `kernel/core/compiler/compiler_diagnostics.cpp` | `b4a9337cac966f564fce8a86adaba27cb358292fc98f4661f69b5cbd696e709c` |
| `kernel/core/compiler/elf_writer.cpp` | `13dd11ea97b8cc03be0c1db6ea628cb4f742262f1dc7fcf20d9845f7a9cabfc0` |
| `kernel/arch/arm64/compiler_backend.cpp` | `9058687b97a8f4d8563cf2ae8e01bf397b14cad9b9fdc192e4b777d9614ef0df` |

## Regression attribution before accepting Build

The older R2A report describes these guest observations. The table separates the historical failed condition from the current source-matched evidence; the historical logs and candidates were not edited.

| Issue and baseline | First failing condition and attribution | Current evidence and Build impact |
| --- | --- | --- |
| Phase 10 multi-architecture lifecycle, `out/aarch64-phase10-r2a-20261107`, three-boot run 3 | Acceptance expected 26 teardown lifecycle records, one for each launch (one interactive plus 25 automatic relaunches), and matched 24 complete cleanup records. This is a count of sequential launch/teardown cycles, not 26 simultaneously live resources. The same guest log has `multiarch lifecycle durability: PASS launches=26`, stable global page accounting, and no report of retained mappings/windows/widgets. The serial transcript contains split/interleaved cleanup records; the 24 value is the count of intact matched records, not 24 released application resources. No exact live resource remained: none was reported. This was a log/acceptance-count integrity issue, not a demonstrated application lifetime leak. | Current host Phase 10 app-loader controls pass. All four R2B1 boots record `NativeElf GUI cleanup: PASS mappings=0 windows=0 widgets=0`; the runner accepts a bounded serial interleave only when all three counts are zero and the runtime cleanup marker is present. No cleanup check was removed or weakened. |
| Phase 11 fixed-address contract, `out/aarch64-phase11-r2a-20261107`, boot 1 | The resident compiler emitted the ARM64 nested payload with PT_LOAD base `0x51000000` and entry `0x51001000`; the nested NativeElf caller supplied the Studio base `0x50000000`, causing `NativeElf load rejected: fixed-address-contract`. The producer's ELF passed its own validator. The defect was the consumer passing the wrong expected base. | The compiler and nested loader now share `ARM64_NATIVE_ELF_BOOTSTRAP_BASE` (`0x51000000`), preserving the exact fixed-address check. Current output has entry `0x51001000`; all four Studio Build transitions pass `arm64_entry_and_strict_base`. Phase 11 host build and compiler host regressions pass. No R2B1 Run is claimed. |
| Phase 12 historical integration, `out/aarch64-phase12-r2a-20261107`, and R2A session `abi3-guest-20261107-145531-622` | The original Phase 12 log does **not** contain the Phase 11 `fixed-address-contract` rejection; it ends with `Phase 12 Developer Studio runtime: FAIL` / `AARCH64_PHASE12_ERROR` after the Build/Run proof markers. The later R2A session reached Studio close, then logged `fs-failures=1` and `scheduler/VFS integration: FAIL`. That single failure was not localized in the preserved log, so a narrower root cause is unproven. R2B1 serializes the Phase 12 workspace VFS stat/read/list/write/mkdir/remove callbacks with `Phase9VfsScope`; this is a safety measure for VFS access, not proof that it caused the historical single failure. It is not evidence of a compiler or ELF-base defect. | Phase 12 host integration and SDK alignment pass. Every R2B1 guest reaches its Phase 12 pass and scheduler/VFS durability marker; the latest records show reads and enumerations progressing, 10,000 preemptions, and zero unexpected IRQs/exceptions. The R2B1 missing-package-root control also proves VFS publication errors fail closed. |
| Phase 3 scheduler timeout, `out/aarch64-phase3-r2a-20261107` | The first guest reached cooperative scheduling and made progress but missed its configured completion bound. Later bounded SCHEDR1 guest runs completed after roughly 151–155 seconds with progress counters advancing; this does not support a deadlock diagnosis. | The Phase 3 host contract passes, and current R2B1 guests show scheduler progress and finish their bounded lifecycle. The Phase 3 timeout is tracked as a runtime-bound issue, separate from Build correctness. |
| BOOTDTB2 guest timeout, preserved candidate `out/aarch64-bootdtb2-repaired-20261102`, run `bootdtb2-guest-20261107-145040-236` | The guest logged `DTB: OK`, GICv2/timer setup, and scheduler entry, then reached its 120-second bound before Phase 4 pass. QAC observed child exit 0 and released reservation `1e0b8b1024e446aa88c4973e4f073982`. The preserved evidence does not establish a parser defect or a completed BOOTDTB2 guest qualification. | Captured-DTB host parsing passes 52 assertions on the preserved 1,052,672-byte firmware DTB. BOOTDTB2 is a separate guest acceptance item and does not participate in Studio Build. |

The R2B1 source-matched guest run provides four repetitions of the relevant Phase 12 desktop, scheduler, VFS, Build, and cleanup path. Its per-run `scheduler/VFS integration: PASS` and durability counters are direct guest evidence. Historical partials are retained and are not reclassified as passes.

## Real Studio Build path and bounds

The guest execution path is:

```text
Developer Studio project and active source document
  -> Studio Save writes /Apps/R2B1B01/src/main.cpp through guest VFS
  -> Studio Ctrl+Shift+B Build action
  -> pinned v0.5 bare-metal Build callbacks
  -> ABI3 adapter forwards to the AARCH64 Build callbacks
  -> BareMetalBuildService reads the exact source path through VFS
  -> resident lexer/parser, ARM64 backend, and ELF writer run in the kernel
  -> output is written through guest VFS
  -> VFS reopen/readback, ELF validation, SHA-256, and Build snapshot return
  -> Studio displays completion; normal project/window cleanup follows
```

This is not a host compiler invocation: `BareMetalBuildService` is linked into the ARM64 kernel, consumes the saved guest VFS source, and emits the guest artifact. The QAC workflow sends the actual `Ctrl+Shift+B` input to Studio and requires the guest Build start/result markers. There is no injected or precompiled project output.

Studio's v0.5 public call table is 464 bytes. It remains a distinct type from the 400-byte AARCH64 ABI-v1 table; the adapter translates between them field by field. The Build callback slots are compile-time asserted: v0.5 `bare_metal_build_project_start(ctx, request, outHandle)`, `bare_metal_build_project_poll(ctx, handle, outSnapshot)`, and `bare_metal_build_project_release(ctx, handle)` map to the equivalent AARCH64 callback signatures. Their table offsets are 240 in v0.5 and 288 in ABI-v1. The adapter validates guest request/snapshot pointers and versions, copies bounded strings, translates result codes, and returns an error when a required service is absent. Snapshot strings, diagnostics, and artifact identity are copied field by field into the guest snapshot; the service does not retain pointers to Studio-owned request buffers. The pinned Build controller polls a returned handle and releases it on completion. The kernel service currently completes the bounded compile within `start`; poll returns its terminal snapshot. The v0.5 cancel slot at offset 456 is not wired, so interruption/cancellation during that synchronous compile is not qualified.

The compiler service accepts a confined absolute project root, a safe project-relative output, canonical project-kind/ABI values, and a current/ARM64/AMD64/multi target profile. It resolves the source from the project's `sourceRoot` and `sourceEntry`; it does not select an arbitrary host file. Project/manifest text buffers are bounded to 16 KiB, paths to 256 bytes, compiler source to 64 KiB, and emitted ELF output to 16 KiB. Compiler work buffers are static. A build starts only one job at a time. On parse/emission failure the service removes stale project-local target output, preserves the last published package, records a source diagnostic, and returns a failed snapshot. A VFS publication error is also returned as failure; it does not synthesize a success result.

The ARM64 grammar is deliberately small: one `int gx_main(void* ctx)` or compatible `gx_app_context*` function, optional bounded `log(ctx, "...")` calls, and one signed 32-bit integer return literal. It does not implement general C/C++, expressions, variables, control flow, multiple translation units, symbol resolution, or general relocation/linking. Diagnostics retain line, column, byte offset, and token. The ARM64 backend follows AAPCS64, returns in `w0`, preserves required registers, keeps a 16-byte stack alignment, and validates PC-relative literal range.

FAT create remains 8.3-only. R2A's adapter preserves Studio's logical filenames by reversible deterministic mapping at the VFS boundary; canonical physical files take precedence, ambiguous aliases and path escapes are rejected, and FAT long-filename creation was not enabled. The project/build service exposes the Studio logical `.elf` name but stores long names under a deterministic uppercase eight-hex-digit `.ELF` alias. In the first qualifying build, `/Apps/R2B1B01/build/bin/arm64/r2b1b01-guidexos.elf` mapped to `/Apps/R2B1B01/build/bin/arm64/1457FBF6.ELF`.

## Artifact identity and validation

The exact saved source is 37 bytes:

```c
int gx_main(void* ctx) { return 42; }
```

The guest log records the project VFS write and the compiler's matching source read. The output is 4,108 bytes, ELF64 little-endian `ET_EXEC`, `EM_AARCH64` (183), with entry `0x51001000` and PT_LOAD image base `0x51000000`. The bootstrap writer emits one deterministic RX `PT_LOAD`; the image has no dynamic loader, dynamic relocations, or W+X segment. The strict validator checks machine/type, load bounds/alignment, exact expected image base and entry, executable permissions, and emitted code bytes. Studio Build then reopens the VFS file, validates it again, and reports the cryptographic digest:

| Build | Source | Artifact SHA-256 |
| --- | --- | --- |
| Initial positive Build | `return 42;` | `76c5a81849c6f9372432a5e164563d10226a5c8ae211f4a16a8e70b5f09e48b2` |
| Recovery Build | `return 43;` | `e636e87fca022d1e2d4b4068067e2a7d19f59a626d24c28872c4fe23ebecc8d6` |

The initial artifact digest is identical across all four fresh boots. The recovery digest differs, establishing that the changed saved source was recompiled. The first boot log records the output size, compiler readback hash, ELF entry, VFS storage alias, logical project artifact name, and SHA-256. Build validates the image but does not execute it. The `0x51000000` load slot matches the existing nested-application contract beside Studio at `0x50000000`; actual loading/execution is part of R2B2.

## Positive, negative, and recovery Build controls

Each qualifying boot performed all of these guest-side actions through Studio:

1. Created and opened a new project in Studio, then saved `main.cpp` to its guest VFS path.
2. Pressed the real Build shortcut and waited for successful Build completion, one source file compiled/linked, valid ARM64 artifact identity, VFS readback, and SHA-256.
3. Replaced the source with malformed input missing the semicolon after the return literal. The resident parser reported `line 1, column 36, offset 35: expected ';' after return value (token='}')`; Studio showed Build failure. The service removed the stale project-local ELF, did not report artifact validation or Build success, and did not replace a published package.
4. Restored valid source with `return 43;`, performed another authentic Build, and verified the new digest.
5. Opened the staged `/Phase12VfsProof` project whose manifest targets absent package root `/Apps/R2B1Missing/Child`. The compiler emitted its temporary target output, but package publication returned `VFS package output path was unavailable`; Studio reported failure and no package was published.
6. Closed Studio, checked zero GUI mappings/windows/widgets, captured the redrawn desktop, and verified a Start-button pointer action and Super-key keyboard action afterward.

I visually reviewed the latest run's `r2b1-build-started.png`, `desktop-after-close-pointer-response.png`, and `desktop-after-close-keyboard-response.png`. The pointer capture shows the Start menu opening after Studio closes; the following keyboard capture shows it toggled closed. The R2B1 run directories also retain the source-save, malformed-source, recovery, VFS-failure, and initial no-input desktop captures.

The unavailable VFS publication case distinguishes a compiled temporary target artifact from a published package and requires no false successful Build marker. Candidate fixture metadata states that no target artifacts were prebuilt.

## QAC-managed fresh boots

Every run had a fresh QAC acceptance reservation, unique firmware-variable file, same candidate-manifest SHA-256, and same kernel/EFI/Studio/RAM-disk hashes. Each workflow JSON marks all 16 R2B1 transitions true. QAC logs record the managed child exit code as 0 and reservation release; the durable session record reports `ABI3_STUDIO_GUEST_BOOT_AND_CLEANUP_PASS`.

| Fresh boot | QAC session / reservation | Project / source | Positive artifact SHA-256 | QAC child / release |
| --- | --- | --- | --- | --- |
| 1 | `abi3-guest-20261107-214531-361` / `60d3352f25314ed288d5170529340fcf` | `R2B1B01` / `/Apps/R2B1B01/src/main.cpp` | `76c5a81849c6f9372432a5e164563d10226a5c8ae211f4a16a8e70b5f09e48b2` | Exit 0; released |
| 2 | `abi3-guest-20261107-215051-714` / `2b3ec10ddb7d49d8b9b7635ccbbe5b67` | `R2B1B01` / `/Apps/R2B1B01/src/main.cpp` | `76c5a81849c6f9372432a5e164563d10226a5c8ae211f4a16a8e70b5f09e48b2` | Exit 0; released |
| 3 | `abi3-guest-20261107-215519-081` / `517d57a17b0a426687659c8fd3a9d049` | `R2B1B01` / `/Apps/R2B1B01/src/main.cpp` | `76c5a81849c6f9372432a5e164563d10226a5c8ae211f4a16a8e70b5f09e48b2` | Exit 0; released |
| 4 | `abi3-guest-20261107-215759-728` / `a4aa16056ca14c1bb379505196a2a0a6` | `R2B1B02` / `/Apps/R2B1B02/src/main.cpp` | `76c5a81849c6f9372432a5e164563d10226a5c8ae211f4a16a8e70b5f09e48b2` | Exit 0; released |

All four have `qac_exclusive_grant=true`, `qac_reservation_released=true`, `qac_child_exit_verified=true`, `phase12_cleanup_pass=true`, GUI counts zero, pointer and keyboard response true, and `no_unexpected_irqs_or_exceptions=true`. QAC status/list was checked after the final run: zero managed sessions, zero reservations, zero foreign QEMU processes.

Three earlier b750 candidate attempts remain preserved as partials and do not count: `abi3-guest-20261107-214045-823` had serial writes split across the GUI cleanup marker; `abi3-guest-20261107-214805-160` split the compiler source/output diagnostic; `abi3-guest-20261107-215317-061` split the reopened-hash diagnostic. The guest had progressed, but the then-current harness could not bind its complete acceptance evidence. The parser refinements below preserve the raw logs and tolerate only bounded interleaving with independent source, output, and cleanup checks. Older-candidate explorations also remain in their original directories.

## Hosted and freestanding regressions

| Gate | Result | Evidence directory |
| --- | --- | --- |
| ABI3 adapter, VFS path mapping, bounds, errors, aliases, collisions, short writes | PASS, 175/175 | `out/aarch64-r2b1-20261107-qualified-commit-b75074e6-adapter-tests` |
| ABI-v1 / Studio v0.5 layout and activation compatibility | PASS; 400-byte and 464-byte layouts, 9/9 activation cases, incompatible linkage rejected | `out/phase12-sdk-alignment-r2b1-b75074e6` |
| ARM64 compiler and compiler bootstrap host regressions | PASS; real compiler/backend and ELF contract controls | `out/compiler-host-regressions-r2b1-b75074e6` |
| Compiler Build-service host regression | PASS; output digest matches guest positive artifact | `out/compiler-build-service-host-20261107-220152-238-cb0c8716` |
| Phase 10 application-loader controls | PASS | R2B1 production build and host controls |
| Phase 11 fresh freestanding build/package staging | PASS; compiled ARM64 Studio client and AMD64 control payload, no guest launch | `out/aarch64-phase11-host-r2b1-b75074e6` |
| Captured firmware-DTB parser | PASS, 52 assertions; parser reached COMPLETE / FDT_END | `out/aarch64-phase2-host-r2b1-b75074e6` |
| Phase 3 scheduler host contract | PASS | `out/aarch64-phase3-host-r2b1-b75074e6` |
| Phase 4 allocator/VFS and SCHEDR1 host controls | PASS | `out/aarch64-phase4-host-r2b1-b75074e6` |
| GICv3 hosted timer/scheduler suite | PASS, 62 assertions; no QEMU | `out/aarch64-gicv3-host-r2b1-b75074e6` |
| Phase 2–4 and Phase 12 production ARM64 kernel/EFI builds | PASS | corresponding `out/*-r2b1-b75074e6` directories |

Shared-fixture host regressions ran serially. Candidate guest builds and QAC output are isolated from accepted R1e/R2A evidence. The runner changes make R2B1 validation bind the exact Studio VFS source read, compiler byte count, target ELF entry/base, VFS output write, artifact readback, and digest even if the two guest serial writers interleave a diagnostic line. GUI cleanup parsing accepts a bounded split only when `mappings=0 windows=0 widgets=0` and the runtime cleanup PASS marker both exist. The process-exit check reads the durable QAC child exit record rather than treating a nullable wrapper `Process.ExitCode` as guest evidence.

## R2B2 Run qualification still required

R2B1 proves Build and validated output only. R2B2 must use Studio's authentic Run action and prove, over fresh exclusive QAC-managed boots, that:

- the newly built artifact is published or resolved through the production App Model path and the ARM64 payload is selected by its real identity;
- the fixed-address loader accepts the exact artifact generated by Studio, maps it with safe permissions, provides a bounded aligned stack, and invokes its entry point;
- the guest program executes and returns `42` (or the selected test value) with its output observable through the normal captured-output path;
- unavailable, malformed, wrong-architecture, or stale artifacts fail without a false Run-success marker;
- normal completion, launch failure, and requested close release mappings, stack, handles, GUI/runtime resources, and App Model/session state;
- the desktop remains responsive after close and unexpected IRQ/exception counts stay zero; and
- at least three fresh QAC-exclusive boots repeat the same frozen candidate, preserve logs/screenshots, verify child exit and reservation release, and leave no QEMU process.

Do not treat the R2B1 ELF validation or host Build-service tests as an execution result. The Build source subset remains intentionally limited until a separate compiler-language expansion is qualified.
