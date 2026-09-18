# C120 Reusable Managed Control Host Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a bounded `GuideXosControlHost` that owns focus traversal and routing for the four reusable managed controls, integrate it into Managed Notes and picker scopes, and validate it with focused tests plus three fresh QEMU boots.

**Architecture:** A fixed eight-entry host stores stable IDs, explicit control kinds, control references, and focusability in registration order. Explicit adapter dispatch handles focus, pointer-down, key, and character events without reflection or moving control behavior into the host. Managed Notes owns a main host; the picker owns a second host linked through one bounded modal/restoration slot.

**Tech Stack:** C# 9 NativeAOT managed sample, existing `GuideXosButton`, `GuideXosTextInput`, `GuideXosTextArea`, `GuideXosListBox`, PowerShell build/evidence scripts, C++ kernel proof harness, QEMU serial evidence.

**Spec:** `docs/superpowers/specs/2026-09-18-c120-managed-control-host-design.md`

## Global Constraints

- Preserve Host ABI version `1` and Host ABI table size `104`.
- Make no capability, NativeAOT runtime, GC, executable-mapping, or VFS interface changes.
- Preserve the existing launch/input payload; represent Tab as managed key value `9` and route traversal from `KeyDown` only.
- Use a fixed host capacity of `8` entries and one bounded modal restoration slot.
- Use explicit control-kind dispatch; do not use reflection, type scanning, unbounded collections, layout, bubbling, or command binding.
- Keep button activation, text editing, list navigation, picker validation, and file commands in their existing controls/application.
- Use TDD: each production behavior is preceded by a failing focused test or proof assertion.
- Preserve the live checkout and do not push or perform destructive Git operations.

---

### Task 1: Add C120 build mode and failing host contract tests

**Files:**
- Modify: `samples/managed/HostLogProof/HostLogProof.csproj`
- Modify: `scripts/dotnet/build-managed-hostlog-proof.ps1`
- Create: `samples/managed/HostLogProof/GuideXos/GuideXosControlHostTests.cs`
- Test: `samples/managed/HostLogProof/GuideXos/GuideXosControlHostTests.cs`

**Interfaces:**
- Produces the compile-time `HOSTLOGPROOF_C120_MANAGED_CONTROL_HOST` define and the failing test contract for `GuideXosControlHost`.
- The tests call the following API that Task 2 must implement:

```csharp
public enum GuideXosManagedControlKind
{
    None = 0, Button = 1, TextInput = 2, TextArea = 3, ListBox = 4,
}

public enum GuideXosControlHostResult
{
    Ignored = 0, Registered = 1, Focused = 2, Traversed = 3,
    Changed = 4, Moved = 5, Activated = 6, Submitted = 7,
    Cancelled = 8, Rejected = 9, Disabled = 10,
}

public sealed class GuideXosControlHost
{
    public const int MaximumSupportedControlCount = 8;
    public int RegistrationCount { get; }
    public int ActiveIndex { get; }
    public int ActiveControlId { get; }
    public GuideXosManagedControlKind ActiveControlKind { get; }
    public bool IsModalActive { get; }
    public GuideXosControlHost ActiveScopeHost { get; }
    public GuideXosControlHostResult TryRegisterButton(int id, GuideXosButton control, bool focusable = true);
    public GuideXosControlHostResult TryRegisterTextInput(int id, GuideXosTextInput control, bool focusable = true);
    public GuideXosControlHostResult TryRegisterTextArea(int id, GuideXosTextArea control, bool focusable = true);
    public GuideXosControlHostResult TryRegisterListBox(int id, GuideXosListBox control, bool focusable = true);
    public GuideXosControlHostResult TrySetFocusable(int id, bool focusable);
    public GuideXosControlHostResult TryFocus(int id);
    public GuideXosControlHostResult FocusAndRoutePointer(int id, int x, int y);
    public GuideXosControlHostResult HandleInput(GuideXosInputEvent input);
    public GuideXosControlHostResult HandleKey(GuideXosTextInputKey key, bool shift = false);
    public GuideXosControlHostResult HandleCharacter(char character);
    public bool EnterModal(GuideXosControlHost modalHost);
    public bool ExitModal();
    public void Reset();
}
```

- [x] **Step 1: Add the C120 composite mode without adding production host code.**

Extend the C119 property group in `HostLogProof.csproj` with `HOSTLOGPROOF_C120_MANAGED_CONTROL_HOST`, add a `C120Composite` property group carrying all C112–C119 defines plus C120, and include `C120Composite` in the production-mode condition. Add `C120Composite` to the `ValidateSet` and mode-to-arguments logic in `build-managed-hostlog-proof.ps1`.

- [x] **Step 2: Write the failing focused test entry point.**

Create `GuideXosControlHostTests.Run(GuideXosHost host)` with a fixed set of actual controls and a final log marker. The first version must reference `GuideXosControlHost` and assert the contract below; it must not use mocks or reflection:

```csharp
public static bool Run(GuideXosHost host)
{
    bool result = EmptyHost() && RegistrationAndOrder() && CapacityAndRejection() &&
        FocusTransferInvariant() && ForwardTraversal() && ReverseTraversal() &&
        DisabledNormalization() && NoFocusTraversal() && OneControlTraversal() &&
        KeyboardOwnership() && PointerSynchronization() && ModalRestoration() &&
        IndependentHosts();
    host.TryLog(result
        ? "C120-TESTS cases=50 result=PASS"u8
        : "C120-TESTS cases=50 result=FAIL"u8);
    return result;
}
```

Implement the cases as small private methods using real controls: empty/single/multiple registration, order, capacity 8, overflow, duplicate IDs, reset, no-focus, explicit focus, invalid focus, blur/focus transfer, one-focus invariants across all four kinds, forward/reverse traversal and wrapping, disabled skipping and normalization, all-disabled and one-control scopes, no-focus traversal, Tab suppression, ordinary routing, Enter/Space/Up/Down ownership, pointer transfer, modal isolation/restoration/fallback, independent hosts, and rejected-operation consistency.

- [x] **Step 3: Run the C120 managed build and verify the expected red failure.**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\dotnet\build-managed-hostlog-proof.ps1 -RepoRoot . -ManagedProjectMode C120Composite
```

Expected: compilation fails because `GuideXosControlHost`, its enums, and its API are not yet defined. Record the missing-type failure before writing production host code.

### Task 2: Implement the bounded host and make the focused suite green

**Files:**
- Create: `samples/managed/HostLogProof/GuideXos/GuideXosControlHost.cs`
- Modify: `samples/managed/HostLogProof/GuideXos/GuideXosTextInput.cs`
- Test: `samples/managed/HostLogProof/GuideXos/GuideXosControlHostTests.cs`

**Interfaces:**
- Consumes the four existing control APIs and `GuideXosInputEvent`.
- Produces the exact host API declared in Task 1.

- [x] **Step 1: Implement fixed registration storage.**

Create a private `ControlEntry` struct with `int Id`, `GuideXosManagedControlKind Kind`, `object Control`, and `bool Focusable`. Store exactly `new ControlEntry[8]`, a registration count, active index initialized to `-1`, a modal host reference, and one saved restoration ID/index. Registration methods must reject `id <= 0`, null controls, duplicate IDs, invalid `focusable` state, and capacity overflow before modifying the array. `Reset()` must blur all entries, clear entries/count, set active index to `-1`, and clear modal state.

- [x] **Step 2: Run focused tests and verify registration failures are now the remaining red cases.**

Run the same C120 build. Expected: the compiler succeeds and `C120-TESTS` fails only for behavior not yet implemented; capture the marker and failure location.

- [x] **Step 3: Implement authoritative focus transfer and eligibility normalization.**

Implement `TryFocus`, `TrySetFocusable`, `Active*` properties, and private `NormalizeActiveFocus`. `TryFocus` must reject missing/ineligible IDs without changing state; otherwise blur every registered control, set the active index, and focus the target. Eligibility is `Entry.Focusable` plus `GuideXosButton.Enabled` for buttons. If the active entry becomes ineligible, normalize by searching forward with wrap, or clear to `-1` when no entry is eligible. Repeated one-control traversal must leave the active control focused without a blur/focus cycle.

- [x] **Step 4: Implement explicit adapter dispatch for pointer and keyboard input.**

Use a `switch` on `GuideXosManagedControlKind` with casts to the known control type. `FocusAndRoutePointer` must focus the requested entry first, then call its pointer-down method. Map control-specific outcomes into `GuideXosControlHostResult` while retaining activation/submission/cancellation distinctions. `HandleKey` must intercept `GuideXosTextInputKey.Tab` before adapters, traverse forward or reverse based on `shift`, and never forward it. `HandleCharacter('\t')` must return `Ignored` without routing or traversing. All other key/character events go only to the active entry. `HandleInput` maps `KeyDown` and `KeyChar` to those canonical methods.

- [x] **Step 5: Add Tab to the managed key representation and run the focused suite.**

Add `Tab = 9u` to `GuideXosTextInputKey`. Run the C120 build and inspect the serial test marker. Expected: `C120-TESTS cases=50 result=PASS`, with no failure marker. If a case fails, add or correct a focused assertion before changing production behavior.

- [x] **Step 6: Refactor only test helpers after green.**

Once the suite is green, consolidate repeated host fixture setup into bounded test helpers and keep the public host API unchanged. Re-run the C120 build and confirm the same 50-case marker.

### Task 3: Integrate picker-owned focus host and modal scope

**Files:**
- Modify: `samples/managed/HostLogProof/GuideXos/GuideXosFilePicker.cs`
- Modify: `samples/managed/HostLogProof/GuideXos/GuideXosControlHost.cs`
- Modify: `samples/managed/HostLogProof/GuideXos/GuideXosControlHostTests.cs`
- Test: `samples/managed/HostLogProof/GuideXos/GuideXosControlHostTests.cs`

**Interfaces:**
- Consumes `GuideXosControlHost` from Task 2.
- Produces `GuideXosFilePicker.FocusHost` and picker input dispatch that leaves the host authoritative.

- [x] **Step 1: Add a failing picker-host isolation assertion.**

Extend the focused host test to enter a modal host after focusing a main button, assert the main button is blurred and `IsModalActive`/`ActiveScopeHost` identify the picker host, route Tab/Enter/Space/Up through the modal host, and assert the main host's active control and control state remain unchanged. Exit the modal with the saved target enabled and assert it is restored; disable the saved target before exit and assert the next eligible main entry is selected.

- [x] **Step 2: Implement the bounded modal link.**

Implement `EnterModal` to reject null/self/active modal links, save the current main ID (or `-1`), blur and clear the main active entry, set the modal host, and leave the modal host's existing active control intact. Implement `ExitModal` to clear modal focus, remove the link, and restore the saved ID if eligible, otherwise select the next eligible registration after the saved index, or no-focus. Keep the state one-level and reject a second nested modal without changing the first link.

- [x] **Step 3: Add picker host storage and registration.**

Add a fixed picker host with capacity `2` and expose it as `FocusHost`. Reset it with picker state. On Open `Begin`, reset/register the candidate list as ID `1` and focus it. On Save with C116 text input, register filename input as ID `1`, candidate list as ID `2`, and focus filename input. On Save without text input, register/focus the candidate list. Remove direct picker `Focus()` calls that would bypass the host.

- [x] **Step 4: Route picker input through the picker host.**

For Open list coordinates, call `FocusAndRoutePointer(1, ...)`; for Save filename coordinates call the filename ID; for Save candidate-list coordinates call the list ID. For keyboard input call `FocusHost.HandleInput(input)`. Preserve existing Escape cancellation, list Enter completion, filename Enter submission, and rendering/file validation by mapping the host result back into the existing picker result state. `KeyDown(Tab)` remains host-owned and `KeyChar('\\t')` is ignored.

- [x] **Step 5: Run the focused suite and C120 build.**

Run the managed C120 build again. Expected: the host suite remains `cases=50 result=PASS`; the picker compiles and no direct picker focus path remains in the picker dispatch methods.

### Task 4: Replace Managed Notes ad-hoc focus with the main host

**Files:**
- Modify: `samples/managed/HostLogProof/Applications/ManagedNotes.cs`
- Modify: `samples/managed/HostLogProof/GuideXos/GuideXosControlHostTests.cs`
- Test: `samples/managed/HostLogProof/Applications/ManagedNotes.cs`

**Interfaces:**
- Consumes main/picker host APIs and existing Notes command IDs `20` Open, `21` Save As, `22` Save.
- Produces the C120 production focus order: Open, Save, Save As, Document.

- [x] **Step 1: Add a failing integration proof branch.**

Under `HOSTLOGPROOF_C120_MANAGED_CONTROL_HOST`, add C120-only launch/input state and assertions that expect a main host with registration count `4`, no initial focus, active IDs `1..4` under traversal, and modal restoration. Wire the proof marker before replacing current focus code so the first C120 build fails on the missing host-owned transitions.

- [x] **Step 2: Create and initialize the main host.**

Add a `GuideXosControlHost(4)` field and IDs for the three existing buttons plus `_textArea`. During C120 Notes launch, reset/register in explicit order and leave active index `-1`. Keep C119 fields and behavior available under the earlier define, but C120 must use the host path.

- [x] **Step 3: Route main pointer input through the host.**

Use the existing bounded button rectangles and text-area rectangle to identify a target. Call `FocusAndRoutePointer` on the target ID, map button activation to the existing command IDs, and render. Do not call `BlurButtons`, `_textArea.Focus`, or a button's direct focus method from the C120 path. A pointer-down on an enabled button still activates once; a disabled Save remains ineligible and does not activate.

- [x] **Step 4: Route main keyboard input through the host.**

Call `mainHost.HandleInput(input)` for non-modal input. When the result is `Activated`, use `ActiveControlId` to invoke the existing Open/Save As/Save command. When the active ID is Document, preserve text-area editing, including Space insertion and Enter newline. Host Tab handling must run before any control and must never reach text input, text area, list box, or button.

- [x] **Step 5: Enter and exit modal scopes through the host.**

When an existing command starts Open or Save As, call `mainHost.EnterModal(_picker.FocusHost)` after the picker has initialized its focus host. While `_picker.IsActive`, dispatch only through the picker host/picker state machine. In `ApplyPickerResult`, when the picker becomes inactive, call `mainHost.ExitModal()` before rendering; restore Open or Save As when eligible, and use the host's next-eligible fallback if the initiating control was disabled.

- [x] **Step 6: Add C120 serial markers and run the managed build.**

Emit bounded markers for app model, registration/order, active transitions, forward/reverse wrap, disabled skip, pointer synchronization, one-focus invariant, keyboard routing, modal entry/isolation/restoration, Notes integration, and rendering focus state. Run the C120 managed build and require all focused markers to be `PASS`.

### Task 5: Add the C120 kernel proof and dedicated three-boot runner

**Files:**
- Modify: `kernel/core/main.cpp`
- Modify: `samples/managed/HostLogProof/HostLogProof.csproj`
- Modify: `scripts/dotnet/build-managed-hostlog-proof.ps1`
- Create: `scripts/dotnet/run-c120-managed-control-host.ps1`
- Test: `scripts/dotnet/run-c120-managed-control-host.ps1`

**Interfaces:**
- Consumes C120 managed markers and existing C118/C119 compositor proof helpers.
- Produces `out/dotnet/c011ec120-managed-control-host/c120.manifest.json` and three independent QEMU serial logs.

- [x] **Step 1: Add a failing native proof expectation.**

Add a `GXOS_NATIVEAOT_C120_MANAGED_CONTROL_HOST` block in `kernel/core/main.cpp` after the C119 block. It must launch the managed workspace, run deterministic C120 Notes scenarios for main traversal/isolation, Open picker, and Save As restoration, and require C116–C119 markers, lifecycle markers, missing-image and Busy markers, and a final `[C120-RESULT] outcome=PASS` marker. Before the managed integration exists, run the kernel proof only far enough to observe the expected missing C120 marker.

- [x] **Step 2: Implement the C120 compositor sequence.**

Reuse the existing launch/click/key/serial helpers and add checks for:

```text
main registration=4; no-focus -> Tab Open -> Tab Save -> Tab SaveAs -> Tab Document -> Tab Open
Shift+Tab Open -> Document -> SaveAs -> Save -> Open
disabled Save skipped both directions
pointer Document -> Space changes document and does not activate a button
Tab and Enter activation occur on the intended registered control only
Open picker owns Tab/Up/Down/Enter/Space while main controls remain unchanged
Open completion restores Open
Save As filename input owns typing and Enter; completion/cancel restores Save As
final Notes save/reopen bytes are exact
```

Log each category with `[C120-*]` markers and make the final outcome depend on every category, prior C116–C119 regressions, application regressions, and lifecycle evidence.

- [x] **Step 3: Add the canonical PowerShell runner.**

Create `run-c120-managed-control-host.ps1` with the same parameter style as C119, default evidence root `out\dotnet\c011ec120-managed-control-host`, `FreshBootCount` default/required value `3`, optional `SkipManagedBuild`, `SkipKernelBuild`, `SkipQemu`, and repository preflight. Build `C120Composite`, stage the C118 fixtures, compile kernel flags through `GXOS_NATIVEAOT_C120_MANAGED_CONTROL_HOST`, run three isolated ESP/QEMU boots, reject any `C120-*FAIL`, fault, or boot-failure marker, and write `repository-state.txt`, `inputs.json`, per-boot serial/stdout/stderr files, categorized evidence, and `c120.manifest.json`.

- [x] **Step 4: Run the managed build, kernel build, and runner in build-only mode.**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\dotnet\run-c120-managed-control-host.ps1 -RepoRoot . -SkipQemu
```

Expected: managed NativeAOT composite and kernel build succeed; the manifest is `BUILD_ONLY` and contains ABI version `1`, table size `104`, no capability changes, and the C120 source hashes.

### Task 6: Documentation, full verification, and focused commit

**Files:**
- Create: `docs/dotnet/NATIVEAOT_C120_MANAGED_CONTROL_HOST.md`
- Modify: `docs/superpowers/plans/2026-09-18-c120-managed-control-host.md`
- Test: `scripts/dotnet/run-c120-managed-control-host.ps1`

**Interfaces:**
- Consumes final host API, proof manifest, and serial evidence.
- Produces the committed C120 documentation and a clean verified implementation commit.

- [x] **Step 1: Document the final implementation.**

Document the problem, host/control boundary, fixed registration capacity/storage, IDs/kinds, active/no-focus state, adapter dispatch, initial-focus policy, Tab/Shift+Tab transport and double-delivery guard, wrapping, disabled skipping, pointer synchronization, routing, modal scope and restoration policy, Notes order, C116–C119 interaction, ABI/input/runtime/GC/VFS impact, allocation bounds, evidence paths, validation limits, and the out-of-scope list from the approved spec. Do not claim behavior not present in the serial evidence.

- [x] **Step 2: Run the required full C120 proof with three fresh QEMU boots.**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\dotnet\run-c120-managed-control-host.ps1 -RepoRoot .
```

Require exit code `0`, `c120.manifest.json` outcome `PASS`, `freshBootCount=3`, `qemuExecuted=true`, and boot outcomes `PASS,PASS,PASS`. Inspect all three serial logs for exactly one C120 final result, no failure/fault markers, and the required C116–C119/lifecycle evidence.

- [x] **Step 3: Run direct validation checks.**

Run:

```powershell
git diff --check
git status --short --branch
git diff --stat HEAD~1 HEAD
git show --stat --oneline HEAD
git rev-parse HEAD
git rev-parse --abbrev-ref --symbolic-full-name '@{upstream}'
git rev-list --left-right --count HEAD...origin/v1.1_DOTNET_SUPPORT
```

Inspect changed files and confirm no Host ABI table growth, capability additions, NativeAOT runtime changes, GC changes, VFS changes, or unrelated cleanup.

- [x] **Step 4: Commit the implementation locally.**

Stage the host, tests, picker/Notes integration, build config, kernel proof, runner, documentation, and this plan. Commit with:

```powershell
git add -- samples/managed/HostLogProof kernel/core/main.cpp scripts/dotnet/build-managed-hostlog-proof.ps1 scripts/dotnet/run-c120-managed-control-host.ps1 docs/dotnet/NATIVEAOT_C120_MANAGED_CONTROL_HOST.md docs/superpowers/plans/2026-09-18-c120-managed-control-host.md
git commit -m "Add reusable managed control focus host"
```

- [x] **Step 5: Re-run post-commit repository verification.**

Confirm the final worktree is clean, the ending commit subject is exactly `Add reusable managed control focus host`, upstream remains unchanged, and the branch is not pushed. Report any live-state discrepancy rather than correcting it.
