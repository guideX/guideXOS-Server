# C188R8 Notes input routing, focus, and document-edit qualification

**Date:** 2026-10-25
**Branch:** `v1.1_DOTNET_SUPPORT`
**Starting HEAD:** `7790959ddb8bebc73ce17e9f11fe7278aa8a82da`
**Outcome:** **B — input boundary narrowed**

## Result

The C188R8 diagnostic guest launched real selector-4 Managed Notes, created and rendered its Notes surface, returned status zero from native launch, initialized the input manager, and entered the normal input loop. The guest then decoded the bounded click and `a` key input and sent both through the focused Notes window’s native input callbacks.

The first failed boundary is `GuideXosApplicationRegistry.TryFind(4)` in managed dispatch. `Dispatch` logs `managed-entry`, then takes its unknown-selector return (`GxAbi.ErrorInvalidApplicationId`, `-4`) before it reaches the managed lifetime lookup or `ManagedNotes.HandleInput`. The Notes control never receives the click, so it cannot focus the text area or insert the character. No document edit was observed.

This establishes a real dispatch failure, not a keyboard-device failure or an invalid click target. It does not establish why the same static registry accepted selector 4 during Notes launch but rejected selector 4 on the later input call. No product correction was justified without that explanation. The next diagnostic should compare the registry table and context identity at launch and input reentry, including table length, selector values, low user-data selector, and generation, without logging unmanaged pointers.

## Preflight and preserved state

The repository, branch, and complete starting commit matched the task header. The required C188R7 authorization record and C188 native identity record were read. The existing R7R1 raw serial, QMP, framebuffer, QAC, and artifact records remain under `out/dotnet/c188r7-notes-proof-authorization/`.

The fresh R8 preflight matched the prior baseline: 13 protected roots, 8,497 files, and 136 existing worktree status paths. The modified `ESP/ramdisk.img` began with SHA-256 `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`. Its hash remains unchanged. A failed non-isolated `dotnet run` retry created three generated MSBuild files in the protected `ManagedUiRegression/obj` tree. They were moved byte-for-byte to `out/dotnet/c188r8-notes-input-routing/preflight/derived-msbuild-output/`; the original names, sizes, and hashes are recorded in `relocated-test-build-artifacts.json`. No historical file was removed or overwritten.

There was no standalone repository `.phase` file or stale-prompt guard. The diagnostic and build scripts require output beneath the C188 evidence root and refuse to overwrite an existing run or build root. No branch changes, worktrees, stash, reset, clean, merge, rebase, cherry-pick, or history rewrite were used.

## Startup evidence and R8 input sequence

The R7R1 record and raw serial show this ordered sequence:

1. The native selector-4 Notes route was invoked and the managed Notes app instance was created (`C150-APP-INSTANCE id=4 generation=1`).
2. The application identity was `com.guidexos.apps.managed.notes`; its managed window was `000003E8` and Notes was reported as focused.
3. C133/C134/C137 proof suites were skipped for this ordinary Notes launch. C147/C148 Notes context checks passed.
4. The real Notes surface was registered and initially rendered.
5. `ManagedNotes.Launch` returned, native application dispatch returned, and the raw native status was `status=00000000`.
6. The input manager initialized and the kernel entered `[KERNEL] Entering main loop (waiting for input)...`.
7. The R8 QMP probe sent pointer movement, primary-button down/up, and `a` key down/up. QMP accepted each request.
8. The compositor trace hit client area of window `000003E8`; the managed input trace entered with selector 4 and generation 1.
9. Managed dispatch rejected selector 4 with `-4`. No `ManagedNotes.HandleInput` or text-area handler marker followed.
10. C136 logged primary pointer down/up as `FAIL`; C138 logged pointer movement as `IGNORED`; C129 logged the `a` key-char callback as `FAIL`. No text-area edit marker appeared.

`C162-MANAGED-FOCUS` identifies the managed application focus record. The compositor has a separate focus id. On a primary client click, `KernelCompositor::handleMouseDown` calls `setFocus(hitWin->id)` before calling the owner’s pointer method; the R8 compositor trace identifies the Notes client hit. There is no separate post-click serial marker that prints the compositor’s focused id.

## Actual input call paths

### Pointer

`input_manager::poll` in `kernel/core/input_manager.cpp` samples mouse state. `desktop::cooperative_yield` calls it and then calls `desktop::handle_mouse` when the mouse state is dirty. `handle_mouse` gives overlays priority, checks `KernelCompositor::isPointOverWindow`, computes button edges, and sends the event to `KernelCompositor::handleMouseMove`, `handleMouseDown`, and `handleMouseUp`.

`KernelCompositor::hitTest` in `kernel/core/kernel_compositor.cpp` checks visible windows front to back and distinguishes titlebar, resize corner, and client. For a client hit, the compositor translates screen coordinates to owner-local coordinates using `x - win->x` and `y - win->y - TITLEBAR_HEIGHT`. A primary down sets window focus before forwarding the event.

`NativeAotManagedSurface::onMouseDown`, `onMouseUp`, and the C138-gated `onMouseMove` in `kernel/core/nativeaot_application.cpp` encode the local coordinates and event kind, then call `invokeManagedInput`. The native context carries selector 4 and generation 1; `managedApplicationUserData` packs selector in the low 32 bits and generation in the high 32 bits. No ABI field or callback offset changed.

The managed entrypoint calls `GuideXosApplicationRegistry.Dispatch`. It creates the host and launch context, then calls `GuideXosApplicationRegistry.TryFind(selector)`. The latter fails before `GuideXosManagedApplicationLifetime.TryGet` and before `GuideXosApplication.HandleInput`.

If dispatch reaches Notes, `ManagedNotes.HandleInput` in `samples/managed/HostLogProof/Applications/ManagedNotes.cs` handles the ordinary pointer path by calling `_textArea.HandlePointerDown` with the C175 editor origin. `GuideXosTextArea.HandlePointerDown` checks visibility, enabled state, and editor bounds, maps coordinates to a caret, and focuses the control. C120’s separate control-host pointer path is enabled only for its explicit proof context.

### Keyboard

QMP sends qcode `a` down and up. `ps2keyboard::irq_handler` in `kernel/core/ps2keyboard.cpp` decodes set-2 scan codes, ignores key-up for normal key processing, and queues the printable character on key-down. `desktop::cooperative_yield` drains the key queue through `desktop::handle_key`; printable non-control characters go to `KernelCompositor::handleKeyChar` for the focused window.

`NativeAotManagedSurface::onKeyChar` turns that character into a `kLaunchFlagInputKeyChar` event and calls `invokeManagedInput`. The serial trace shows value `0x61` (`a`), proving the keyboard path reached the native focused-window callback and managed dispatcher. The key-up does not independently create another character event.

If dispatch reaches Notes, `ManagedNotes.HandleInput` routes `KeyChar` to `_textArea.HandleCharacter` and `KeyDown` to `_textArea.HandleKey`. The text area rejects input when it is unfocused, rejects unsupported characters transactionally, and inserts a supported character once at the caret. The C188R8 trace never reaches these handlers.

## Boundary evidence

| Source function | Required state | Expected destination | R8 evidence | Rejection or result | Next diagnostic |
|---|---|---|---|---|---|
| QMP `input-send-event` | Guest QMP connected and running | Virtual PS/2 mouse and keyboard | Every request returned `{}`; QMP state was `running=true` | No QMP error | Existing raw QMP log is retained; use device trace only if dispatch reentry is fixed |
| `input_manager::poll`; PS/2 decoders | Mouse state change or decoded key make code | Desktop input pump | Pointer reached compositor; `a` reached `onKeyChar` as `0x61` | No event-level IRQ marker exists; downstream callbacks prove delivery past the decoder boundary | Add a bounded decode marker only if later evidence needs to separate IRQ from decode |
| `desktop::handle_mouse`; `KernelCompositor::hitTest` | Pointer inside a visible, non-overlay window | Client hit of Notes `000003E8` | `C172` reported screen `(379,359)`, client hit, window `000003E8`, size `600x360` | Client selected, not titlebar or resize corner | None for target selection |
| `KernelCompositor::handleMouseDown`; `setFocus` | Primary button and client hit | Focus Notes compositor window, then owner pointer callback | Source path sets focus before forwarding; `C172` records the primary client down | No separate post-click focus-id marker | If needed, add a single post-setFocus id marker |
| `NativeAotManagedSurface::onMouseDown/Up/Move` | Valid selector, local coordinates, pointer event flags | `invokeManagedInput` | `C188R8-300 stage=managed-entry selector=4 generation=1` and C136/C138 native callbacks | Native callback receives a nonzero managed return for down/up/move | Already instrumented through `selector-rejected` |
| `GuideXosApplicationRegistry.Dispatch` → `TryFind` | Host creation succeeds; selector 4 | Descriptor for Managed Notes, then lifetime generation lookup | `C188R8-300 stage=selector-rejected ... result=-4`; `C112-UNKNOWN-SELECTOR` | First established rejection: `GxAbi.ErrorInvalidApplicationId` before lifetime lookup | Compare actual registry table and selector/generation context at launch versus input |
| `ManagedNotes.HandleInput` → `_textArea.HandlePointerDown` | Valid descriptor and live generation; pointer in C175 editor bounds | Notes text area caret and control focus | Not entered; no `C117-TEXT-AREA focus=PASS source=pointer` | Selector lookup returned first | Do not bypass registry; diagnose its reentry state |
| `NativeAotManagedSurface::onKeyChar` → managed dispatch | Printable make code, focused window, no control chord | Notes character handler | `C188R8-300 kind=3 ... x=97`, then C129 native result `FAIL` | Same selector lookup rejection `-4` | Same registry comparison as pointer path |
| `GuideXosTextArea.HandleCharacter` → Notes render | Text area focused and character supported | Single document insertion, `RenderMain`, changed framebuffer | No managed Notes marker, document mutation, or changed text observed | Handler never entered | Re-run only after the first failed boundary is corrected |

The `generation=1` in `managed-entry` is the generation supplied in the input launch context. Because registry lookup failed first, this run did not prove that the managed lifetime accepted or rejected that generation.

## Input-probe audit and diagnostic semantics

The runner computes the target from the guest framebuffer dimensions for a centered 600×360 Notes window. It uses QMP relative moves in steps no larger than 40 pixels, presses and releases the primary button, waits 250 ms, sends `a` down, waits 80 ms, releases it, and waits 400 ms. It waits 100 ms between pointer down and up. QMP logs preserve the exact events and order.

The compositor reported the click at screen `(379,359)` on Notes window `000003E8` at `(340,220)`. The resulting local point was `(39,115)` after the 24-pixel titlebar translation. The C175 editor origin is `(20,84)`, character width 8, and line height 18; `(39,115)` is row 1, column 2 inside the editor. The text area is visible and enabled in the ordinary C147 Notes context. The click therefore targets the editable region, not a titlebar, empty panel, or disabled control.

One runner expectation was context-inaccurate: the old `notesPointerAccepted` check looked for `C120-POINTER target=document`, which is emitted by the explicit C120 proof context, not by ordinary Notes. The ordinary C175 Notes path emits `C117-TEXT-AREA focus=PASS source=pointer`. The runner now checks that normal Notes marker. Its three-case regression confirms normal C117 focus/edit evidence passes, C120-only evidence does not qualify ordinary input, and keyboard edit evidence does not imply pointer focus. The original run manifest is retained unchanged; its pointer flag was based on the old proof-only marker. The raw `C172`, native callback return, `C112`, and R8 managed trace independently establish the actual pointer rejection.

C129, C136, and C138 guest labels are status reports from the diagnostic NativeAOT callbacks: the native surface maps `invokeManagedInput` status zero to PASS (or nonzero to FAIL/IGNORED). They are not proof that a physical keyboard interrupt failed. The key was decoded and reached the native callback; the managed app descriptor lookup failed. The hosted C129/C136/C138 suites are separate component qualifications and passed.

The ordinary Notes document-change expectation is the C117 text-area edit marker. It does apply to this C160/C188 ordinary Notes configuration. It is absent because dispatch never reached Notes. C120’s proof-only pointer marker was the runner mismatch described above.

## Checkpoint validator correction

The native launch checkpoint previously required `status=0x...`, while the raw R7R1 serial uses `status=00000000`. It now accepts exactly eight hexadecimal zero digits, either prefixed or unprefixed (and either `x` case), for the required native launch return. A valid nonzero status, malformed width or digits, missing marker, duplicate marker, out-of-order marker, or truncated required sequence still fails. Actual serial order remains authoritative and the existing branch-optional checkpoint handling is unchanged. C188R8 uses the R7 startup checkpoint sequence.

The focused Python validator suite passed 20 tests. It covers prefixed and unprefixed zero, nonzero, missing and malformed status, duplicate and out-of-order markers, truncated capture, and R8/R7 sequence equivalence. The R8 raw serial passed the corrected readiness validator with the unprefixed status.

## Diagnostic instrumentation and bounded change

The managed-only R8 trace is gated by `HOSTLOGPROOF_C188R8_NOTES_INPUT_ROUTING`. It records `managed-entry`, `selector-rejected`, `application-found`, `lifetime-found`, `lifetime-rejected`, `notes-handler-enter`, and `notes-handler-return`. It skips pointer-move and wheel events and is capped at 16 trace records. Each record uses stack-allocated storage, reports selector/generation/kind/flags/coordinates/status, and exposes no unmanaged pointer.

No production input, compositor, control-host, ABI, or Notes implementation was changed. The only Notes-path correction is the runner’s proof-context-specific acceptance marker. The managed registry failure remains uncorrected pending evidence.

## Hosted qualification

Freshly executed in this phase:

- Managed UI baseline: 38 suites, 930 cases, all pass. This preserves the accepted baseline.
- C188 hosted identity/lifecycle: 25 cases pass; 1,000 identity operations, 500 accepted resizes, 250 invalid geometry requests, 250 stale-generation attempts, 100 launch/close lifecycles, and zero resize hot-path allocations.
- Prior host regressions: C182 15, C185 28, C186 15, C187 22; all pass.
- Authorization and document suites: C147/C148 context checks 23; C133 24; C134 30; C152 document-state 8; C137 46; all pass.
- Focused R8 wrapper: 136 cases pass: C129 31, C136 36, C138 62, and 7 Notes editor checks.
- New runner-marker contract: 3 cases pass.
- Checkpoint validator: 20 tests pass.

The 7 Notes editor checks cover unfocused input rejection, an outside click, pointer focus at the C175 guest coordinates, caret mapping, no insertion through the keydown character path, exactly one insertion through KeyChar, and transactional rejection of an unsupported control character. Existing suites retain LF handling, C152 save/document-state behavior, and the 256-byte Notes capacity/save limit. Those hosted controls do not substitute for a guest document edit. The guest did not mutate or save a Notes document.

The new focused run result is recorded in the R8 qualification output. The original 38-suite count is not added again to the focused 136-case count; those are distinct executions with overlapping component coverage.

## NativeAOT, kernel, and identity qualification

The isolated R8 diagnostic NativeAOT build passed in `builds/diagnostic-002`. Independent ManagedUiContracts and CounterApp builds passed; the final diagnostic ELF is ELF64 x86-64 with seven load segments and zero relocations. The PE-to-ELF conversion and native artifact verifier passed. The diagnostic profile retains production selectors 4–8 and the development CounterApp selector 9. ABI v4 / 128 bytes, callback offsets 104, 112, and 120, five managed slots, C174/C180 resize handling, and C181 generation-safe ownership are unchanged.

The diagnostic kernel built under the isolated R8 output root with freestanding flags (`-std=c++14`, no STL, exceptions, or RTTI). No kernel source changed; its ELF hash matches the prior R7 kernel. A separate ordinary proof-disabled production C160 composite build passed in `builds/production-proof-disabled-004`: registry dispatch is present, selectors 4–8 are the production set, no development selector-9 route is present, and R7/R8 proof markers are absent from the ELF. Since kernel source was not changed, no proof-disabled production kernel rebuild was needed. These static checks do not establish production Notes guest readiness.

Two earlier production build attempts remain preserved. `production-proof-disabled-002` failed during project-reference isolation; `production-proof-disabled-003` built but failed its profile verification because it included the C185 test composition. They are not counted as production qualification. The final `004` build omits both C185 test composition and C188 external identity and uses isolated project artifacts.

## One targeted QAC guest

The fresh pre-run QAC census showed no QEMU process, active reservation, managed session, or queued run. The run used QAC owner PID 32812, reservation `939edf9ea52e41ccbe4bd616fd83d298`, and QEMU PID 21308. The QEMU command line, candidate/media hashes, QMP state, raw serial, checkpoints, screenshots, and reservation release are stored under `out/dotnet/c188r8-notes-input-routing/runs/diag-001/`.

QAC exited successfully with code 0 and verified release. The post-run QAC status/list showed zero QEMU processes, reservations, managed sessions, or queued runs. This phase launched one bounded Notes diagnostic guest. It did not launch any of the six C188 acceptance scenarios or a production guest.

The guest was responsive through the probe and QMP reported `running=true` before the runner’s owned `quit`. The before/after screenshots show the same Notes document. The framebuffer diff is 210 pixels out of 1,024,000 and corresponds to cursor/overlay changes; there is no observed editor text change. No second input was sent because the first input failed before Notes.

## Artifact hashes

| Artifact | SHA-256 |
|---|---|
| R8 diagnostic NativeAOT ELF | `A7A3D75B69A791CE4FF7631AA1277021BD3F43F4CCCCA3458728D9F46D2C2BC6` |
| Diagnostic PE | `E3468B7E321768ABC6AEB0F1BE83127B9BACE7BEB126A1AFA17CEB378DF0B6E9` |
| R8 diagnostic kernel ELF | `9C12F856569E4FE40E1D70CEB56252D3D8E0138DCD4016F83E6F8627DAC261FA` |
| Proof-disabled production NativeAOT ELF | `C8DCF4A3D0CBCFDE2F99FE130472452AE1A5F632CBEC7D136A3423031A38B8E0` |
| R8 staged ramdisk | `DA75EDE5E764B10AFF67BAD50791065A7B4DBB190D28F3280CFCA118AA953D91` |
| UEFI loader | `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B` |
| Raw serial log | `FEF8835D68C24F767D08CC71CC310AE4A280FDE174ECB2D2680FC293256E57F5` |
| Raw QMP log | `B5804D36B031E696B81ABDDB24650251B2AF116916A380B0FC4619AC2BFAA306` |
| Before-input framebuffer | `447C7734B0E7A4A40768B23616CCC4169DC20AF613AC94143165CB5511F4B87F` |
| Final framebuffer | `9FAE6C354DEBF0BF51D68530E75F4E39B12CE71377C5F5810CCEB551421E180B` |

The modified source RAM disk hash remains `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`. Generated ESP, RAM disk, and guest media remain outside the commit.

## Protected-tree comparison and retained history

The fresh after-comparison matched all 13 protected roots and all 8,497 relative files by membership, byte size, and SHA-256. There were no protected-file differences. All 136 pre-existing status paths retained their prior flags, sizes, and hashes; the original ramdisk hash is unchanged. The three generated MSBuild support files created by the unisolated retry are preserved byte-for-byte outside the protected root. A post-commit comparison records the final worktree status count.

The C186R1 historical limitation remains: 212 C185-generated files have no reconstructible earlier byte identity. R8 makes no retroactive claim about those bytes.

## Remaining qualification and C188R9 direction

C188R8 establishes successful Notes startup and identifies selector-registry rejection as the first incomplete input boundary. It does not establish document editing and is not full C188 acceptance. The six C188 scenarios remain unaccepted. Proof-disabled production Notes readiness/input is not guest-qualified.

Recommended C188R9 scope: instrument the registry dispatch entry and compare launch versus input selector, registry length/entries, application generation, and dispatch path in a bounded trace. Establish why the same selector succeeds on launch and fails on input before changing membership or bypassing generation ownership. Then add a regression for the demonstrated cause and run one targeted diagnostic confirmation. Once diagnostic Notes editing is proven, perform one proof-disabled production Notes readiness/input qualification before resuming the six C188 acceptance scenarios. Do not begin C189 as part of this direction.
