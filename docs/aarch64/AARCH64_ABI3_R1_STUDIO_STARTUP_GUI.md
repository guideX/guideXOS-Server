# AArch64 ABI3 R1 Developer Studio Startup and GUI Qualification

- **Qualification status:** `STUDIO_ARM64_GUI_PARTIAL` (latest completed diagnostic boot; R1c attempt queued)
- **Scope:** Startup, first window, basic input, and clean shutdown. Guest Build/Run is a later milestone.
- **Repository:** `D:\dev\guideXOSServer_AARCH64`
- **Branch / expected base:** `AARCH64_SUPPORT` / `b02ce98e37e7a7566050a6e1fdf1a2c5831d0f30`
- **Pinned Developer Studio:** `058c83fe1ae270a103eadc7caca961328e8abe2a`
- **Pinned v0.5 SDK:** `f63a7710a30b49f785aac134abf06a3a6c5b5aca`
- **Package ABI identity:** `guidexos-c-abi-v1-studio-v05-464`

## Current result

The first R1 guest attempt reached Studio's genuine `gx_main` entry after UEFI, the AArch64 kernel, DTB validation, GICv2/timer setup, scheduler and desktop startup, package discovery, NativeElf loading, and v0.5 adapter selection. The typed VFS status repair is confirmed in the guest: the optional Phase 29I sentinel lookup now reports `not_found` / `GX_ERROR_NOT_FOUND`. Studio initialization then reached window creation, where the first boot stopped with `request_window_failed` because the pinned application asks for a 960×700 window while the guest compositor was capped at 640×640. This is an observed Stage E failure; no first frame, input, or cleanup is claimed for that boot.

A narrow status bridge now preserves `VFS_ERR_NOT_FOUND` and `VFS_ERR_NOT_MOUNT` across the AArch64 host service and private v0.5 adapter without changing either public ABI. The adapter suite covers both translations. The Stage E repair queries the compositor work area and permits an adapter-associated window up to 1024×768 only when the centered rectangle fits. A fresh production candidate contains that change. Its exclusive QAC request reached the 1,800-second bound behind another acceptance reservation and was canceled before launch. The candidate remains preserved; no qualifying R1 guest boot is claimed.

## Source, overlay, and package

The pinned Studio checkout was clean and was copied into the build output. Only the copied build driver and generated compatibility include were changed. The overlay restores shared model sources omitted from selected test lists, imports two namespaced Build diagnostic helpers, and maps the same-signature `window_destroy` spelling to `native_window_destroy`. It does not alter the Studio checkout or v0.5 ABI.

The package is a sectionless AArch64 `ET_EXEC` NativeElf with entry `gx_main`, loaded at `0x50000000`. The Studio ELF is 1,321,236 bytes, has three `PT_LOAD` segments and no dynamic relocations, and uses a 64 KiB initial stack. These are build and static ELF checks; guest memory safety still requires lifecycle evidence.

| Artifact | SHA-256 |
| --- | --- |
| Studio ELF | `a08fc9c4599475165874a983b7d11366dcac2f35f5dc377ad6e875a960f2e483` |
| `Apps/DeveloperStudio/app.json` | `62af7dce96baa5605092bb561e6c4bfe5428367bff8bbb229d4f01d41691ad4c` |
| R1c `candidate-manifest.json` | `05a6266757ab127624614317529bae78cfc8a7009ed907b22d93bc3fb397c13b` |
| ARM64 kernel ELF (R1c) | `4f61e7a1dfe95cd4d9fb34f9cd84f301d434f4aaa6b9d829d4121f86d7a5e883` |
| AArch64 UEFI loader | `1605c55a4d7c0c7b0589bd181e28d2e32f7ef9da31a6aeb23695c237ff7bfe42` |
| R1c RAM disk | `19a1edf66e05e2ed410730259032867fed89d5539cf7e91538adde24b6fccaa9` |

R1a's diagnostic run used `out/aarch64-abi3-r1a-studio-build-20261105` and completed as a partial Stage E failure. The current exclusive request uses `out/aarch64-abi3-r1c-studio-build-20261105`, built after the work-area/window-size repair. Its package, ELF, EFI, and kernel hashes are recorded above.

The R1c candidate manifest records server base `b02ce98e37e7a7566050a6e1fdf1a2c5831d0f30`, the pinned Studio and SDK revisions above, `production-ui`, and all package hashes. The generated compatibility include SHA-256 is `fe1aa62429adae70848ee1566c23e4d25056e46537442e0df1e57ebeaddc274b`; the copied Studio build driver SHA-256 is `f7b0bb8d77887cf68deb61b5cf15480287d9bc0f5a651a7f0a177c995ee3645c`. No source in the original Studio checkout was modified.

## Narrow startup repair

The guest trace reached the package and ABI boundary, then reported `DEVELOPER_STUDIO_PHASE29I_SENTINEL_LOOKUP_ERROR` for `/Apps/DeveloperStudio/.phase28q-diagnostic`. The file is optional; VFS correctly returned `VFS_ERR_NOT_FOUND`, but `phase11_file_stat` translated every non-success status to `GX_ERROR_FAILED`, so Studio did not receive the expected not-found result.

The ARM64 Phase 12 NativeElf path now forwards not-found and mount-unavailable through private bridge values `-1001` and `-1002`; the v0.5 adapter translates those to its own `GX_ERROR_NOT_FOUND` and `GX_ERROR_NOT_MOUNTED`. These bridge values never enter either public callback table. Other VFS failures retain the generic failure mapping. Phase 11 builds, which do not include the v0.5 adapter, retain their prior generic behavior.

The guest reached `request_window_ex` after completing Studio construction; that is the mandatory window operation observed in the pinned startup path. It already forwards through the typed v0.5 adapter to the existing ARM64 window manager. The ABI2 `native_window_create`, `native_window_set_text`, and `native_window_run` slots remain unsupported because the startup trace did not call them. Document activation, debugger, and cancellation callbacks likewise were not observed before first-window creation. Their later application workflows are outside this startup milestone.

## Hosted and build regression gates

| Gate | Result | Evidence |
| --- | --- | --- |
| ABI2/v0.5 adapter | PASS, 106/106 | `scripts/test-aarch64-studio-v05-adapter.ps1`; includes precise not-found and mount-status translation. |
| ABI1 and ABI3 layouts | PASS | 400-byte AARCH64 and 464-byte v0.5 layouts and offsets remain separately verified. |
| Unsafe cross-SDK linkage | PASS negative control | The 400-byte AARCH64 SDK rejects the v0.5 document-activation consumer for the expected missing field. |
| Standalone Phase 12 SDK alignment | Semantic checks PASS | Includes both SDK include-resolution checks, both layout probes, 9/9 activation checks, and expected rejection. In PowerShell `$LASTEXITCODE` remains `1` after the intentionally rejected final native compile; this is the historical exit-code anomaly, not a failed positive compile or ABI check. The integrated Phase 12 build completes successfully. |
| Phase 10 architecture/resolver controls | PASS | ARM64 and AMD64 payload selection plus missing, unknown, wrong-machine, traversal, malformed, oversized, duplicate, and legacy negative controls. |
| Phase 11 compiler/package build | PASS | ARM64/AMD64 compile paths and staged package; rerun passed after scoping the private bridge to Phase 12. |
| Phase 12 production build | PASS | `scripts/build-aarch64-phase12.ps1 -ProductionStudio`; authentic pinned sources, hosted model/startup/debugger suites, NativeElf link, and production package staging. |
| Phase 2 captured firmware DTB | PASS, 52 assertions | Preserved source fixture SHA-256 `5aec32b925cb7259d651aca355106e58d77f7f1c553d80f0420c5b3e3036f311`; parser result 1, diagnostic 0, stage 4, token 9, offset 7096. The fresh default synthetic suite also passes 51 assertions. |
| Phase 3 scheduler controls | PASS | ARM64 Phase 3 build and `AARCH64_PHASE3_HOST_CONTRACT_PASS`. |
| Phase 4 allocator/VFS and SCHEDR1 controls | PASS | Phase 4 build, resource-table controls, and simulated scheduler/IRQ host workload. |
| GICv3 hosted regression | PASS, 62 assertions | `scripts/test-aarch64-gicv3-host.ps1`; hosted register model only, no guest launched. |

These results are host/build evidence. They do not prove guest Studio startup, GUI input, safe application teardown, Build/Run, or physical-hardware support.

## QAC request and guest protocol

The R1 guest runner uses QAC 2.0.0, the `guideXOSServer-AARCH64-ABI3-Studio` acceptance project, a 1,800-second bounded admission wait, a 300-second guest limit, a fresh copy of the UEFI variables template, and QAC child/owner/profile checks. It does not start QEMU outside QAC.

| Reservation | Project | State at this report | Result |
| --- | --- | --- | --- |
| `8e3ca2521621413eaafeba161c9f1197` | `guideXOSServer-AARCH64-ABI3-Studio` | Completed and released | R1a reached `gx_main`; typed not-found worked; failed Stage E at window creation. Session: `out/aarch64-abi3-r1a-studio-build-20261105/abi3-guest-20261105-191314-099`. |
| `be8b4c6fb57d49c2a901ebb8dee27b89` | `guideXOSServer-AARCH64-ABI3-Studio` | Canceled at 1,800-second timeout | No QEMU guest launched. Session: `out/aarch64-abi3-r1c-studio-build-20261105/abi3-guest-20261105-193825-717`; QAC log records `Cancelled by client`. |
| `2755eece88ce4fd2a1f7cb85463f831e` | `guideXOSServer` | Running; QAC-owned x86_64 QEMU child PID 5924 | Unrelated active acceptance reservation; left untouched. |

An earlier QAC runner ownership-probe diagnostic used reservation `f380836540bf467a8e50ca3744e0d03a`, child PID `4364`, and QMP port `56443`. The probe failed its owner check, so the exact QAC-owned child was manually sent QMP `quit`; QAC then returned to zero sessions, reservations, and QEMU processes. No Studio qualification markers are claimed from that harness diagnostic.

QAC reported online with zero foreign QEMU processes both before and after the R1c request; after cancellation it retained only the unrelated managed x86_64 acceptance guest, with zero queued reservations and zero foreign QEMU processes. That reservation was left untouched. The R1c session record shows a fresh UEFI variables copy matching the template hash. A guest serial log and screenshot were not produced because no QEMU guest launched. Each fresh attempt uses separate UEFI variables and its own QAC reservation.

For an authorized guest attempt, record the following separately: UEFI/kernel/DTB/GICv2/timer/scheduler/desktop; package discovery and manifest identity; NativeElf load and adapter selection; `gx_main`; Studio initialization; window creation and first draw; guest-observed mouse, focus, and keyboard events; redraw after exposure; close event; adapter/image/stack/window cleanup; and desktop responsiveness after Studio exits. QMP acknowledgements alone are not application evidence.

## Guest GUI and lifecycle evidence

R1a did not create a window, so it produced no GUI screenshot, focus/input, redraw, close, or cleanup evidence. R1c was not admitted and did not launch. Do not count either attempt as a qualifying boot unless it independently demonstrates the full acceptance sequence.

| Required behavior | Current evidence |
| --- | --- |
| Visible window and initial client-area drawing | R1a failed before window creation; R1c pending QAC admission. |
| Clipping and redraw after exposure | Not observed. |
| Pointer, button, focus, keyboard text, basic editor interaction | Not observed. |
| Window close, app exit, resource release, desktop responsiveness | Not observed. |
| Three fresh qualifying boots | 0 of 3. |
| Studio guest Build/Run and disposable workspace VFS workflow | Not part of this startup milestone; not yet qualified. |
| Physical hardware | Not tested. |

## Remaining acceptance

The accepted GICv2 Phase 4 profile is used to minimize unrelated variables; GICv3 remains a separate qualification. Document activation, debugger and cancellation services, and the v0.5 NativeWindow create/set/run callbacks were previously identified as optional for the pinned Studio startup path; their guest behavior is not claimed here.

Complete an authorized Studio open/interaction/close cycle with guest-side event, redraw, and cleanup evidence, then perform three fresh independent qualifying boots after the successful diagnostic run. Keep VFS workspace boundary/read/write checks and actual Studio Build plus ordinary Run as separate remaining acceptance work. Do not count a diagnostic boot unless it independently meets every GUI and cleanup requirement.
