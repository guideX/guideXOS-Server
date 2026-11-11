# Server v0.3 — S22R21 Legacy Display UI Remediation

**Assessment date:** 2026-11-10 (local); evidence timestamps are UTC 2026-11-11

**Implementation commit:** `b71147271d046a547885c47f333561dd66425785`

**Phase outcome:** `DISPLAY_CORRECTED_CANDIDATE_QUALIFIED` — candidate for owner review only

## Finding

The desktop exposes Legacy Display Options through the Control Panel and the legacy desktop display route. Its Displays tab previously showed topology, monitor selection, resolution, backend status, and Apply/OK controls even though the v0.3 Public Preview has no supported display-configuration backend. A failed query could leave controls that implied the requested change could still be applied.

Modern Settings is a separate, working navigation surface. Its Display page reports framebuffer characteristics as a read-only snapshot. S22R21 did not change Settings navigation, add a display backend, or claim that modern Settings can change display modes.

## Change

The policy in `kernel/core/include/kernel/display_options_state.h` now distinguishes `UnsupportedPublicPreview`, `BackendUnavailable`, and `Available`. In the public-preview build, Legacy Display Options does not query the display backend and resolves to the explicit unsupported state even if a backend response were to appear successful. When another build has no registered backend or output, it also resolves to a visible unavailable state.

On the legacy Displays tab, the unsupported state renders:

> Display configuration is not supported in the v0.3 public technical preview.
>
> This page is read-only until a supported display backend is qualified.

Topology, monitor, resolution, Apply, and OK controls are not drawn or actionable in this state. Close remains available. Apply/confirm submit paths and keyboard activation also check availability, so an operation cannot emit a false success response if the backend is absent. The normal window close/reopen lifecycle remains intact. Backgrounds, Desktop Icons, and Gradients remain available.

The changes are localized to `kernel/core/include/kernel/display_options_state.h`, `kernel/core/include/kernel/kernel_apps.h`, and `kernel/core/kernel_apps.cpp`. Regression coverage is in `tests/display_options_preview_policy_test.cpp` and `scripts/test-display-options-preview-ui.ps1`. No new framework, display driver, GPU initialization, mode-selection code, Native ELF privilege, or writable package-storage dependency was introduced.

The VirtIO-GPU display probe remains disabled. `EXTRA_CFLAGS` was empty, `GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE` was unset, and the release profile used only `-DGXOS_PUBLIC_PREVIEW_SECURITY_PROFILE`. The generic tracked build identity strings in `build.ps1` are not GPU-probe switches.

## Validation recorded

The hosted and freestanding validations completed before candidate construction:

- Display Options policy/UI checks: 15/15, plus source and lifecycle checks.
- Settings layout: 1,037/1,037; presentation core: 3,659/3,659; Center model: 89/89.
- Settings inventory: 21 devices and 24 storage cases.
- Default apps: 39 model checks, 9 interaction checks, and 100 lifecycle cycles.
- Native Settings runtime: 1,426/1,426; 100 lifecycle cycles, 250 provider checks, and 1,000 transitions. The build emitted one unused `text_ends_with` static-function warning.
- AMD64 canonical kernel build and symbol audit passed.
- Public-preview Native ELF policy: 27 cases passed; the native entry path was reached only by the approved test cases (`nativeEntryCalls=4`).
- Release integrity: 37 assertions; public-preview QAC launch profile: 7 assertions; dev launcher regression: 80/80 under PowerShell 7.
- Existing S22R18 security policy, resource identity, read-only package-source, ABI/platform compatibility, fail-closed checks, and release integrity checks remained enabled.

The affected 16 KiB static stack guards passed with at least 4,096 bytes of required headroom:

| Path | Static estimate | Budget | Headroom |
|---|---:|---:|---:|
| Settings Display | 11,512 bytes | 16,384 | 4,872 bytes |
| Legacy Display Options | 11,896 bytes | 16,384 | 4,488 bytes |
| COM2 | 7,768 bytes | 16,384 | 8,616 bytes |

These are compiler `.su` static estimates, not guest runtime stack telemetry.

Focused guest evidence is recorded in [S22R21 Public Preview Guest Qualification](RELEASE_S22R21_PUBLIC_PREVIEW_GUEST_QUALIFICATION.md). QAC run `s22r21-final-positive-r3-3258b2d2593d475fb8fb9ba69e7a67c2` visibly opened Modern Settings Display and the Legacy Display tab on the new ISO. The screenshot `legacy-display-tab.ppm` shows the unsupported message and the single Close action. Close/reopen and re-navigation completed. That run's PacMan cleanup marker was not observed because the app was closed with the window X; the run is not counted for PacMan acceptance. A separate exact-candidate run `s22r21-final-positive-r4-a9e119800f454a81b85d6eb36a1b5ac7` passed PacMan gameplay input and Escape cleanup.

Evidence root: `E:\CodexArtifacts\S22R21\run-20261110-3d8d7075754749d7beb64590d4e2f162`. The UI screenshots are under `qac-positive-r3\s22r21-final-positive-r3-3258b2d2593d475fb8fb9ba69e7a67c2\output\screenshots\`.

## Disposition

The Legacy Display behavior is corrected for the Model B Public Technical Preview and is qualified on the exact S22R21 candidate. Display configuration remains unsupported until a real backend is separately implemented and qualified. This engineering outcome does not approve the deployment contract, signing, publication, or any release-readiness marker. See the S22R21 addenda in [S22R19 readiness](RELEASE_S22R19_PUBLIC_PREVIEW_READINESS.md) and the [S22R19 deployment contract](RELEASE_S22R19_PUBLIC_PREVIEW_DEPLOYMENT_CONTRACT.md).
