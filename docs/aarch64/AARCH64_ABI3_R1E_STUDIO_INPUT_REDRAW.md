# AArch64 ABI3 R1e Developer Studio Input and Redraw

**Current outcome:** `STUDIO_ARM64_GUI_REPEATABILITY_VERIFIED` — three fresh QAC-authorized boots passed the accepted Studio input, close, redraw, and post-close responsiveness criteria.

**Repository:** `D:\dev\guideXOSServer_AARCH64`

**Branch / starting HEAD:** `AARCH64_SUPPORT` / `088bbe72cc40ac8af4808089761db4ed5dae6eec`

**R1d parent candidate:** `out\aarch64-abi3-r1d-studio-build-20261107`

**R1e candidate:** `E:\CodexArtifacts\guideXOSServer_AARCH64\aarch64-abi3-r1e-studio-redraw-r12-build-20261107`

**Pinned Developer Studio:** `058c83fe1ae270a103eadc7caca961328e8abe2a`

**Pinned v0.5 SDK:** `f63a7710a30b49f785aac134abf06a3a6c5b5aca`
**Package identity:** `guidexos-c-abi-v1-studio-v05-464`

## R1d baseline and source identity

R1d established authentic `gx_main` startup, the v0.5 adapter, an 800×536 client inside an 800×560 frame, and clean Studio close in one of four QAC diagnostic sessions. It did not establish text entry, editable project state, or immediate post-close redraw; none of its sessions counts toward R1e's three fresh boots.

R1e began from the exact requested HEAD, `088bbe72cc40ac8af4808089761db4ed5dae6eec`. The preserved R1d manifest hash is `f536edf970b20a978976320474df4e422521ef31b10ca1dd8962c5ad19a965b4`; it records source HEAD `4503ee6215cb6114352c855f484b2fff8084fa93`. R1e does not treat that historical manifest as the source identity for the new kernel. Its `kernel.elf` and `BOOTAA64.EFI` were rebuilt from R1e's starting HEAD and the modified working tree. The Studio package files and RAM disk were verified against the preserved R1d candidate before reuse.

## Project prerequisite and supported workflow

The pinned Studio's File → New File command deliberately requires an open project. When none is active, it reports `file_create=FAIL reason=no_active_project`. That is expected validation and not a VFS failure. The implementation does not invent a project to satisfy the test.

The authentic GUI provides File → New Project. The guest workflow was exercised through this real Studio path in two diagnostic sessions. Studio created the `app` and `src` directories under `/Apps`, then failed to create `guidexos.project` with `FAT_FILE_WRITE_INVALID_NAME`; the `rollback_failed` marker was preserved. An earlier longer folder name also failed the FAT name validation. The file name `guidexos.project` has an extension longer than the FAT writer's supported 8.3 create form. This is a current guest filesystem capability limit, not a reason to bypass Studio's project validation or claim project creation succeeded.

One earlier, nonqualifying diagnostic used the authentic pre-provisioned `/Phase12IDEProof` sample in the verified Studio RAM disk. Studio opened that project, then opened and edited `src/main.c`; this did not count as GUI-created project evidence. Closing the modified editor triggered a debugger backup write rejected by the FAT 8.3 create path. The three qualifying boots use the narrower acceptance path allowed when a project-dependent editor is unavailable: the actual Studio **Open Project** modal's editable path field. Each displayed the exact text `guideXOS ARM64`; no path was submitted, and no project creation or project open is claimed for those boots.

## Keyboard path and correction

The lost input path was confirmed:

```text
QEMU virtio keyboard
  → Linux evdev key code
  → guideXOS common input queue
  → focused kernel window owner
  → ABI3 v0.5 GX_EVENT_KEY queue
  → Studio event polling and editor focus
  → TextBufferInsert
```

Before R1e, the common desktop loop consumed key-down as a printable character. `Phase9GuiOwner::onKeyChar` forwarded that character only to a focused native TextBox. Studio's editor is rendered and owned by Studio, so it is not a kernel TextBox; its `GX_EVENT_KEY` consumer never received printable input. Key-up and modifier state also were not preserved through the focused-window path. In addition, the virtual key values and Enter code emitted from Linux did not match Studio's existing Windows-style key mapper.

R1e now carries translated text, stable virtual-key code, key-down/up state, and shift/caps/control/alt bits in each queued input event. Focused native windows receive both key actions. The v0.5 owner forwards text only to an actual focused kernel TextBox; otherwise it queues authentic `GX_EVENT_KEY` events for Studio. Linux letters map to uppercase virtual-key codes, while shift/caps remain separate modifier state. The adapter's guest log records key code, action, and modifiers only when Studio polls that event. `scripts/test-aarch64-virtio-keyboard.ps1` covers the Linux-to-virtual-key map and down/up/modifier queue preservation.

The diagnostic test string is `guideXOS ARM64`. QMP acknowledgements alone do not count. Each qualifying boot has guest-observed key-down/up events, one exact occurrence of the test string in the Studio event stream, and a visually reviewed capture showing the string in Studio's real editable Open Project path field. The reducer's raw text includes a leading `O` from the Ctrl+Shift+O shortcut; the requested string itself occurs once. The editor was exercised separately in the nonqualifying fixture diagnostic.

## Close invalidation and repaint

R1d destroyed and cleaned up Studio correctly, but its close path did not guarantee that compositor damage was consumed after the application stopped pumping events. The desktop redraw request is now consumed synchronously by the lifecycle redraw path, which redraws and presents the desktop without another input event. `Phase9GuiOwner` calls it after removing owned windows during cleanup and logs `exposed desktop redraw=PASS`.

R10/R11 diagnostics then found a separate response failure: the Phase12 kernel tail loop ran with `desktop::is_initialized()==true`, but `cooperative_yield()` saw its shared reentrancy guard stuck at `1` after Studio cleanup and skipped every pump. R12 moves that guard to desktop state and releases it when the clean Studio close arms the post-close probe. The guest logged `initialized=1 reentrant=0`, consumed the queued pointer event, dispatched the Start-button click, then dispatched Super and toggled the Start menu. Three fresh boots repeated both actions.

The QAC runner captures `desktop-after-close.png` as soon as it observes the first graphics pass marker, without injecting any post-close input first, then a 500 ms no-input resample. It waits for the second Phase12 marker (after scheduler/VFS durability) before testing desktop input, so input cannot start while the proof workload is still active. The three final captures show the exposed desktop before input, after the Start-button action, and after the Super-key action. The runner holds the guest for a bounded 60-second post-exit observation and records its QAC cleanup.

## QAC runner evidence integrity

The runner preserves QAC stdout and stderr directly to per-run files while the QAC-managed guest is active. It checkpoints a granted reservation identity immediately, records the verified QEMU child PID, and wraps the active lifecycle in `try/finally`. The finalizer always preserves the latest guest log, QAC process state, child liveness when inspectable, and raw QAC status/list when available. If QMP shutdown fails while QAC still owns the child, the record explicitly says that exit/release evidence is unavailable and leaves that managed run under QAC control; it does not kill the QAC process or an unrelated guest.

`scripts/send-aarch64-qmp-studio-input.ps1` accepts only an in-progress session record and verifies that QAC still reports that exact verified reservation and QEMU child before sending input or taking a screendump. It does not start QEMU. A baseline foreign-process anomaly fails closed. PNG signature validation uses `BitConverter` so evidence capture also works under Windows PowerShell 5.1.

## Candidate identity

The full machine-readable provenance is `candidate-manifest.json` in the R1e candidate directory.

| Artifact | SHA-256 |
| --- | --- |
| Candidate manifest | `0c9aca092751d3e35549046dab082ad089c29eff04f85bbce49ded48cdd8374a` |
| ARM64 kernel ELF | `ec074f6a5a7031982c7f15b8dbec572f1fdc8b6e89ce10453a1c3b9d7595d1c2` |
| AArch64 UEFI loader | `1605c55a4d7c0c7b0589bd181e28d2e32f7ef9da31a6aeb23695c237ff7bfe42` |
| Reused Studio RAM disk | `19a1edf66e05e2ed410730259032867fed89d5539cf7e91538adde24b6fccaa9` |
| Separate generic Phase 10 RAM disk | `2f2c181600358cbf800972be4d654a67bcf14142b33c5954eceb9b67da0e7f41` |
| Studio ELF | `a08fc9c4599475165874a983b7d11366dcac2f35f5dc377ad6e875a960f2e483` |
| Studio package manifest | `62af7dce96baa5605092bb561e6c4bfe5428367bff8bbb229d4f01d41691ad4c` |
| Parent R1d candidate manifest | `f536edf970b20a978976320474df4e422521ef31b10ca1dd8962c5ad19a965b4` |

The R1e Phase 12 production build linked the kernel and EFI. The generic Phase 10 RAM disk was retained separately at `phase10-generic-ramdisk.img`; only the new candidate's `esp\ramdisk.img` was replaced with the verified R1d Studio package image. Historical R1d artifacts remain unchanged.

## Hosted and production gates

| Gate | R1e result | Evidence |
| --- | --- | --- |
| v0.5 adapter and bounds | PASS, 125/125 | `out\aarch64-studio-v05-adapter-host-r1e-20261107` |
| ABI1 layout and unsafe-linkage rejection | PASS; expected negative compile rejected v0.5 activation consumer | `out\phase12-sdk-alignment-r1e-20261107` |
| Phase 10 package resolver and machine selection | PASS | Integrated Phase 10 controls in the R1e production build |
| Phase 11 compiler and package checks | PASS | `E:\CodexArtifacts\guideXOSServer_AARCH64\aarch64-phase11-r1e-host-20261107` |
| Phase 12 production ARM64 kernel and EFI link | PASS | Final R12 build `out\aarch64-phase12-r1e-r12-20261107`; copied immutable candidate above |
| Captured firmware DTB parser | PASS, 52 assertions | `out\aarch64-phase2-host-r1e-20261107`; includes preserved `firmware-source.dtb` |
| Phase 3 scheduler contract controls | PASS | `out\aarch64-phase3-host-r1e-20261107` |
| Phase 4 allocator, VFS, GIC layout, scheduler repeatability | PASS | `out\aarch64-phase4-host-r1e-20261107` |
| GICv3 hosted timer/scheduler suite | PASS, 62 assertions | `out\aarch64-gicv3-host-r1e-20261107`; no QEMU started |
| Focused virtio keyboard queue/map | PASS, 25 assertions | Final rerun: `out\aarch64-virtio-keyboard-host-r1e-final-20261107` |
| Focused desktop redraw and post-close probe state | PASS, 27 assertions | Final rerun: `out\aarch64-desktop-redraw-host-r1e-final-20261107` |
| Phase 2 captured-DTB parser controls | PASS, 52 assertions | `out\aarch64-phase2-r1e-r7-20261107` |
| Phase 3/4 hosted build controls | PASS | `out\aarch64-phase3-r1e-r7-20261107`; `out\aarch64-phase4-r1e-r7-20261107` |
| QAC runner and QMP helper syntax | PASS | PowerShell parser validation |

## Native guest runs

The first new Phase12-built ramdisk (`b2252465…`) failed the guest scheduler/VFS durability check and was rejected. The final candidate pairs the R12 diagnostic kernel with the immutable R6 Studio RAM disk (`19a1edf…`) and keeps the generic Phase 10 image separately. R9/R10/R11/R12 exploratory runs are preserved in their build directories but do not count: R9 exposed the VFS image mismatch; R10 did not yet clear the stale pump guard; R11 proved the tail loop was skipped as reentrant; the first R12 attempt sent Escape before the final two text keys had reached Studio. The R12 guard reset fixed the earliest missing post-close transition.

| R1e run | Reservation / session | Result | Qualifying boot? |
| --- | --- | --- | --- |
| Fresh boot 1 | `bf1cf1dc922a4afa9800ce2da496655f` / `abi3-guest-20261107-102529-267` | Text observed once; key-down/up balanced; clean close; no-input redraw; Start-button and Super actions pass; zero unexpected IRQs/exceptions; QAC child exit 0 and reservation released. | Yes |
| Fresh boot 2 | `cf1aadffe1844e84adc223211b130ceb` / `abi3-guest-20261107-102814-912` | Same lifecycle assertions passed on the identical candidate; zero unexpected IRQs/exceptions; QAC child exit 0 and reservation released. | Yes |
| Fresh boot 3 | `21d94e2d284c41c38937bd1642494a45` / `abi3-guest-20261107-103107-601` | Same lifecycle assertions passed on the identical candidate; zero unexpected IRQs/exceptions; QAC child exit 0 and reservation released. | Yes |

All three runs used candidate manifest `0c9aca092751d3e35549046dab082ad089c29eff04f85bbce49ded48cdd8374a`, kernel `ec074f6a5a7031982c7f15b8dbec572f1fdc8b6e89ce10453a1c3b9d7595d1c2`, EFI `1605c55a4d7c0c7b0589bd181e28d2e32f7ef9da31a6aeb23695c237ff7bfe42`, and Studio RAM disk `19a1edf66e05e2ed410730259032867fed89d5539cf7e91538adde24b6fccaa9`. Each final session records two Phase12 pass markers, 61 seconds of post-exit observation, confirmed managed-child exit, QAC reservation release, and zero QEMU processes after cleanup. Run-specific session JSON, guest/QAC logs, editable-control evidence, and PNG captures are under `E:\CodexArtifacts\guideXOSServer_AARCH64\aarch64-abi3-r1e-studio-redraw-r12-build-20261107\<session>\`.

## Scope and remaining acceptance

**Final outcome:** `STUDIO_ARM64_GUI_REPEATABILITY_VERIFIED` (3/3 fresh boots). The accepted editable context was Studio's genuine Open Project path field; the tests did not create or open a project. Editor-specific text insertion was separately observed in one nonqualifying fixture diagnostic. Studio Build/Run, Debug, physical hardware, and general project VFS security remain outside this milestone. Both public ABI layouts, GICv2/GICv3 implementations, scheduler policy, DTB parser behavior, VFS ownership rules, and window isolation remain as established by their existing controls.
