# AArch64 ABI3 R1 Developer Studio Startup and GUI Qualification

- **Current outcome:** `STUDIO_ARM64_GUI_PARTIAL` (R1d; 0 of 3 qualifying boots)
- **Latest native result:** Studio window shown and closed cleanly in one QAC-authorized guest; key text input and repeatability remain unproven.
- **Repository / branch:** `D:\dev\guideXOSServer_AARCH64` / `AARCH64_SUPPORT`
- **Starting HEAD:** `4503ee6215cb6114352c855f484b2fff8084fa93` (equal to `origin/AARCH64_SUPPORT` at preflight)
- **Pinned Studio:** `058c83fe1ae270a103eadc7caca961328e8abe2a`
- **Pinned v0.5 SDK:** `f63a7710a30b49f785aac134abf06a3a6c5b5aca`
- **Package identity:** `guidexos-c-abi-v1-studio-v05-464`

## Qualification result

R1c was admitted for a genuine guest boot and reached Studio's `gx_main`, but its 960×700 client request was rejected by the actual 800×560 desktop work area. That confirmed the earlier Stage E limit was still present in the candidate.

R1d adds bounded client-size negotiation against the real framebuffer and work area. Four separate QAC-authorized R1d diagnostic guests reached the authentic pinned Studio entry, selected the exact v0.5 adapter, created a window, and emitted the first-render marker. A directly inspected framebuffer from the fourth run shows Studio's title bar, menus, Explorer and Outline panes, and welcome text. Mouse and focus events reached `poll_event`. One visible File → New File action also reached Studio, which returned `file_create=FAIL reason=no_active_project` because no project was open. Keyboard text delivery was not observed in any run.

The fourth run then demonstrated an application close event, `application_close=PASS`, `clean_close=PASS`, zero live mappings/windows/widgets at GUI cleanup, runtime cleanup, and zero unexpected IRQs or exceptions. The desktop did not immediately repaint cleanly after close; a later Start-menu interaction followed by Escape restored the wallpaper. This one successful close/cleanup path and one interactive screenshot do not satisfy the keyboard, redraw, and three-fresh-boot acceptance criteria. Outcome remains **`STUDIO_ARM64_GUI_PARTIAL`**, with **0/3 qualifying boots**.

### Earlier R1 work retained as history

The earlier R1 stages repaired ARM64 firmware-DTB handling and mapped the optional Studio sentinel's `VFS_ERR_NOT_FOUND` and `VFS_ERR_NOT_MOUNT` through private bridge values `-1001` and `-1002` to the exact v0.5 error results. The bridge is private to the Phase 12 adapter path; other VFS errors keep their generic mapping, and neither public callback table changed. The first R1a guest then reached authentic Studio initialization but could not create the requested 960×700 window at the earlier 640×640 limit. R1c added work-area awareness and a 1024×768 adapter maximum, but its first newly admitted guest showed that rejecting oversized requests still failed at the real 800×560 work area. R1d's bounded grant behavior described here supersedes that R1c result.

The Studio source remains the clean pinned `058c83fe1ae270a103eadc7caca961328e8abe2a` checkout; generated compatibility and build overlays were confined to copied build inputs. The package is a sectionless AArch64 `ET_EXEC` NativeElf with entry `gx_main`, loaded at `0x50000000`, with three `PT_LOAD` segments, no dynamic relocations, and a 64 KiB initial stack. The 400-byte AARCH64 ABI-v1 table and the 464-byte v0.5 table remain separate typed contracts. The documented v0.5 NativeWindow create/set/run callbacks and some activation/debug/cancellation services remain unavailable or untested and are outside the startup behavior proven here.

## Candidate identity and bounded sizing repair

The R1d candidate was built from the requested starting HEAD. It recompiled the ARM64 production kernel and ABI adapter, and reused the source-matched R1c Studio ELF, package manifest, UEFI loader, and RAM disk after hash verification. It did not alter the Studio source or either public ABI table.

The preserved R1c candidate remains at `out/aarch64-abi3-r1c-studio-build-20261105`; its manifest SHA-256 is `05a6266757ab127624614317529bae78cfc8a7009ed907b22d93bc3fb397c13b`, and its kernel ELF SHA-256 is `4f61e7a1dfe95cd4d9fb34f9cd84f301d434f4aaa6b9d829d4121f86d7a5e883`. Its Studio ELF (`a08fc9c4599475165874a983b7d11366dcac2f35f5dc377ad6e875a960f2e483`), package manifest (`62af7dce96baa5605092bb561e6c4bfe5428367bff8bbb229d4f01d41691ad4c`), RAM disk (`19a1edf66e05e2ed410730259032867fed89d5539cf7e91538adde24b6fccaa9`), and UEFI loader (`1605c55a4d7c0c7b0589bd181e28d2e32f7ef9da31a6aeb23695c237ff7bfe42`) matched the R1d reuse inputs. The R1c directory was preserved unchanged.

| R1d artifact | SHA-256 |
| --- | --- |
| Candidate manifest | `f536edf970b20a978976320474df4e422521ef31b10ca1dd8962c5ad19a965b4` |
| ARM64 kernel ELF | `639444df91749b60a5d26390d69fbe9b2b7db2bf2365f7e44b48736bf533eb9c` |
| Studio ELF | `a08fc9c4599475165874a983b7d11366dcac2f35f5dc377ad6e875a960f2e483` |
| Studio package manifest (`app.json`) | `62af7dce96baa5605092bb561e6c4bfe5428367bff8bbb229d4f01d41691ad4c` |
| RAM disk | `19a1edf66e05e2ed410730259032867fed89d5539cf7e91538adde24b6fccaa9` |
| AArch64 UEFI loader | `1605c55a4d7c0c7b0589bd181e28d2e32f7ef9da31a6aeb23695c237ff7bfe42` |
| QEMU 11.0.0 | `8d2183e745155a303ffb9fd861c5da31c5e6c19fcbfd082081cc3e4bcdb0e748` |
| EDK2 AArch64 code firmware | `47765fe344818cbc464b1c14ae658fb4b854f5c2ceffa982411731eb4865594d` |
| Fresh UEFI variables template | `b3b855c5a80310168051164986855692d1bdb06e67619856177965cd87c6774f` |

The window-bound helper is `kernel/core/include/kernel/aarch64_gui_window_bounds.h`; the ARM64 GUI owner applies it only to the Studio v0.5 adapter. It validates positive requested dimensions, compositor work-area and framebuffer bounds, title-bar space, configured maximum dimensions, and arithmetic before allocation. Requested, granted client, outer frame, position, display, and work-area values are logged separately. Other ABI callers retain their existing sizing path. No compositor capacity was fabricated, and no second window manager was introduced.

Each R1d guest reported the following sizing result:

```text
requested-client=960x700 granted-client=800x536 frame=800x560
position=0,0 display=800x600 work-area=800x560@0,0 titlebar=24 result=created
```

The client surface is 800×536. Its outer frame, including the 24-pixel title bar, fits the full 800×560 work area. The actual framebuffer remains 800×600; the bottom 40 pixels are outside the desktop work area.

## QAC-authorized native evidence

All four R1d guest runs used the QAC exclusive reservation flow, fresh UEFI variable copies, the QEMU-owned guest child, and the AArch64 UEFI candidate. The runner used `-device ramfb` so EDK2 selected a real GOP mode; the earlier `ramfb,xres=1280,yres=800` option was invalid for this QEMU build.

| Reservation | Guest evidence | End state |
| --- | --- | --- |
| `284aeb81e8f741c29425871de635afd8` — `abi3-guest-20261107-051957-653` | Reached `gx_main`, v0.5 window creation and first-render markers. Guest observed mouse and focus events. A later click caused the Studio interface to become visible. No key event or close was observed. | 300-second runner limit; QAC recorded its child exit code 0 and released this reservation. Not a qualifying boot. |
| `6d01c972214c43d58a691b16db4529f5` — `abi3-guest-20261107-052654-887` | Reached the same startup/window stages and mouse/focus delivery. File → New File produced `file_create=FAIL reason=no_active_project`. No key event was observed. | Runner's QMP shutdown/finalization failed at its runtime limit before it wrote `qac.log` or a final session record; `session.json` remains `IN_PROGRESS`. A later QAC status/list showed no R1d session or QEMU child, but this run's own release record is absent. |
| `7aa2f3ea1c2e4482b4d696cecc368402` — `abi3-guest-20261107-053900-560` | Reached the same startup/window stages and mouse/focus delivery. No keyboard event or successful project interaction was observed. | Runner's QMP shutdown/finalization failed at its runtime limit before it wrote `qac.log` or a final session record; `session.json` remains `IN_PROGRESS`. A later QAC status/list showed no R1d session or QEMU child, but this run's own release record is absent. |
| `9caa37eea7b9468592429f9b0f3c4dbd` — `abi3-guest-20261107-055837-452` | Reached the same startup/window stages. Focused screenshot reviewed; mouse/focus reached `poll_event`. QMP Ctrl+Shift+O and typing were attempted, but no Studio key event or entered text was observed. Close click at the title-bar X reached `poll_event`; close and cleanup markers passed. | QAC recorded child PID `10168` exit code 0 and reservation release. This is the one observed clean Studio close and runtime cleanup. The runner's old session record says `ABI3_STUDIO_GUEST_RUNTIME_TIMEOUT`; this is a classification bug because the Phase 12 pass marker was seen and the QAC child exited cleanly. The corrected runner allows the full post-close observation window after that marker. |

The guest trace in each run includes ARM64 kernel/firmware startup, DTB acceptance, GICv2 and scheduler setup, desktop initialization, package discovery, exact ABI identity selection, NativeElf load, `gx_main`, window creation, and first-render markers. In the fourth run the closing trace additionally contains:

```text
ABI3 guest observed Studio close event through v0.5 poll_event
application_close=PASS
clean_close=PASS
NativeElf GUI cleanup: PASS mappings=0 windows=0 widgets=0
ABI3 Developer Studio runtime cleanup: PASS
AARCH64_PHASE12_PASS
unexpected-irq=0 exceptions=0 last-unexpected-irq=0
```

The durable guest counter line reports `unexpected-irq=0`, `exceptions=0`, and `last-unexpected-irq=0`. The fourth run's QAC log records the same reservation grant, QEMU child exit and reservation release. After close, an immediate capture retained part of the old Studio surface; a later Start-menu interaction and Escape produced a wallpaper-only desktop. Desktop repaint after close therefore remains a reliability question.

### Screenshot evidence

The automatically captured `studio-initial.png` is a wallpaper frame, despite an old `session.json` boolean that called it a Studio screenshot. A serial render marker plus screendump did not establish visibility. The runner now records only that a framebuffer capture exists and labels visual review separately. The screenshot below is the fourth run's later, directly reviewed frame after focus interaction; it visibly shows the running Studio UI:

[Focused Studio frame](../../out/aarch64-abi3-r1d-studio-build-20261107/abi3-guest-20261107-055837-452/studio-after-key-combo-focused.png) (SHA-256 `7a7590b9e168115b075e7d1ddb720f96973af920714430ce176e78a9ae797c6b`).

Other preserved captures in that run are `studio-after-key-shortcut.png`, `studio-desktop-after-close.png`, `desktop-responsive-start-menu.png`, and `desktop-responsive-restored.png` in the same directory. The restored wallpaper proves a later desktop redraw after input; it does not prove the immediate post-close repaint is reliable.

The supported `ramfb` device was already in use for these four runs. Post-close accounting and record-accuracy fixes were made afterward; they do not change the staged kernel, Studio ELF, firmware, or RAM disk. The runner now gives a successful Studio exit the full post-close observation interval, avoids claiming a visible Studio frame from a serial marker alone, and preserves an explicit unverified-shutdown record if bounded QMP shutdown fails. Only PowerShell syntax and hosted adapter checks have been rerun for those post-run changes; no fifth guest is claimed.

### Earlier R1c result and harness diagnostic

The real R1c guest `abi3-guest-20261107-050718-991` (reservation `02e9ce6c9521427fae07ec86268c44be`) was QAC-authorized and did reach the authentic `gx_main` and v0.5 adapter, but it rejected the 960×700 client request against the 800×560 work area. It did not create or render a window and is not a qualifying boot. Its serial log, QAC log, and session record remain in `out/aarch64-abi3-r1c-studio-build-20261105/abi3-guest-20261107-050718-991/`. R1c's earlier 1,800-second queued request without a launch remains historical and is not counted.

One preliminary harness check directly invoked QEMU with unsupported `ramfb,xres,yres` properties, outside QAC. QEMU rejected the option and exited before the guest CPU ran; its diagnostic is preserved in `abi3-guest-20261107-050049-389/runner-diagnostic.json` and was excluded from qualification. QAC was checked clear afterward. This was a harness safety deviation and is disclosed here; subsequent guest attempts used only QAC-authorized launches.

## Hosted and production gates

Hosted checks are separate from the guest evidence above and cannot qualify a boot.

| Gate | Result | Evidence status |
| --- | --- | --- |
| v0.5 adapter and window bounds | PASS, 125/125 | Reran after the bounds repair with `scripts/test-aarch64-studio-v05-adapter.ps1`; output `out/aarch64-studio-v05-adapter-host-r1d-20261107-final`; includes framebuffer/work-area dimensions, title-bar bounds, invalid requests, and overflow cases. |
| ABI1 unsafe-linkage negative control | PASS | Source-matched prior gate; the distinct 400-byte AARCH64 table rejects the v0.5 activation consumer. |
| Phase 10 package selection controls | PASS | Rebuilt in the R1d production build; ARM64/AMD64 selection and malformed, unsupported, traversal, duplicate and legacy negative controls. |
| Phase 11 compiler/package build | PASS | Source-matched R1c build result carried forward; the Studio ELF and package are hash-identical in R1d. |
| Phase 12 production ARM64 integration | PASS | Rebuilt kernel and ABI adapter with `scripts/build-aarch64-phase10.ps1 -Phase12Proof -StudioGuestQualification -StudioV05SdkInclude ...`; EFI/kernel link and staging passed. |
| Captured-DTB parser regression | PASS, 52 assertions | Preserved captured-firmware fixture; unchanged parser source. |
| Phase 3/4 scheduler, allocator and VFS controls | PASS | Source-matched earlier results; no scheduler, VFS, GIC, or DTB implementation was changed here. |
| GICv3 hosted regression | PASS, 62 assertions | Source-matched hosted register-model suite; no guest launched for this gate. |
| PowerShell guest-runner syntax | PASS | Parser validation after runner lifecycle changes. |

The standalone Phase 12 alignment script still has its documented `$LASTEXITCODE` anomaly after an intentionally rejected negative-control compile; positive alignment, SDK include, layout, and activation checks passed. It did not block the integrated production build. The ABI2 adapter documentation was reviewed: the 400-byte AARCH64 and 464-byte v0.5 contracts remain distinct and unchanged. No ABI2 documentation or public ABI was edited.

## Fresh-boot repeatability and remaining acceptance

There are **0 of 3 qualifying fresh boots**. Diagnostic boots that miss keyboard text, a complete interactive redraw, clean post-close desktop response, or complete run-specific QAC evidence do not count. Acceptance still needs three fresh QAC-authorized guests demonstrating visible Studio layout, mouse and keyboard text delivered through the v0.5 event loop, a visible state change/redraw, clean application close and resource cleanup, and a responsive desktop afterward.

Studio guest workspace VFS boundaries/read-write behavior and actual Build plus ordinary Run remain later milestones and were not exercised. Physical hardware is untested. Preserve the exact v0.5 464-byte adapter and the independent 400-byte AARCH64 contract.
