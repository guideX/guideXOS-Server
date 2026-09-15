# NativeAOT C116 managed text input

Status: implemented on the C116 continuation branch. C116 adds a reusable bounded managed text-input control and wires it into the existing C115 `GuideXosFilePicker` Save As flow. The C115 picker contract remains intact: enumeration, extension filtering, path validation, stat, overwrite confirmation, cancellation, and selection stay in the picker; `ManagedNotes` continues to own document content reads and writes.

## User-visible result

Managed Notes now renders a `Filename: [ ... ]` field during Save As. A pointer click focuses the field and shows a caret. Printable keyboard characters are inserted at the caret, Left and Right move it, Backspace and Delete edit around it, Enter submits the filename, and Escape cancels the active Save As operation. Save As still appends `.TXT` when the user omits an extension, rejects unrelated extensions, checks the selected path through the picker, and requires explicit overwrite confirmation.

The control is deliberately bounded. `GuideXosTextInput.MaximumSupportedLength` is 127 UTF-16 code units, and the Save As integration derives a stricter maximum from the existing 96-byte path limit and the UTF-8 length of `/system/apps`. Only printable ASCII `0x20` through `0x7e` is accepted. Invalid constructor limits normalize to the default rather than throwing. Input rejected by the bound or character policy leaves the value unchanged and increments the diagnostic rejection counter.

The public surface is small and allocation-conscious:

* `GuideXosTextInput(int maximumLength = 64, string placeholder = "")`
* `Focus()`, `Blur()`, `ResetTransientState()`, and `SetValue(string)`
* `HandlePointerDown(int x, int y)`, `HandleCharacter(char)`, and `HandleKey(GuideXosTextInputKey)`
* `Value`, `Length`, `CaretIndex`, `IsFocused`, `IsSubmitted`, `IsCancelled`, `HasChanged`, and `RejectedInputCount`
* `Render(GuideXosSurface, int x, int y, ReadOnlySpan<byte> label)`

The key constants are `Backspace=8`, `Enter=10`, `Escape=27`, `Left=0x102`, `Right=0x103`, and `Delete=0x106`. Boundary operations are safe: editing while unfocused is ignored, moving beyond either end is ignored, backspacing at index zero is ignored, deleting at the end is ignored, and unknown special keys are rejected without changing the buffer.

## Native-to-managed bridge

The input path is:

`PS/2 decoder -> desktop key/pointer routing -> KernelCompositor -> NativeAotManagedSurface -> existing launch-flags transport -> GuideXosLaunchContext -> GuideXosApplication.HandleInput -> application/picker -> GuideXosTextInput`.

C116 reuses the established synchronous launch-flags transport for pointer-down, key-down, and key-character events. It does not add a host callback-table entry or change the native ABI. The ABI remains NativeHostCallTable v1, 104 bytes, with the C115 offsets unchanged (`open=72`, `close=80`, `directoryList=88`, `fileStat=96`). Existing action routing and the C115 managed file-picker behavior continue to use the same table.

Escape is forwarded to the focused compositor application so a focused managed filename field can cancel its picker. The native surface validates selector, event kind, coordinate bounds, and payload before invoking managed code. The managed launch context decodes the transport into a typed `GuideXosInputEvent`.

## Save As ownership and lifecycle

`GuideXosFilePicker` creates the text input when Save As begins, initializes it with the picker’s proposed basename, and clears it when the picker resets. Pointer and keyboard events are accepted only while Save As is active. Enter returns to the existing picker validation path; it does not bypass extension, path, stat, or overwrite policy. Escape returns a canceled picker result and cannot write a file. `ManagedNotes` applies the picker result and performs the content `FileRead`/`FileWrite` operation only after a path has been selected.

The composite managed host remains resident. C116 exercises Workspace, Notes, native Notepad, Counter, Status, and a Notes return cycle. It also sends a character to Counter while no text picker is active to prove that the new input transport does not make unrelated applications editable or break their action paths.

## Verification

Run the full workflow from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dotnet/run-c116-managed-text-input.ps1
```

The runner requires at least three fresh QEMU boots. It builds `C116Composite`, stages the managed ELF and C114 fixtures, builds the kernel with `GXOS_NATIVEAOT_C116_MANAGED_TEXT_INPUT`, boots QEMU with serial capture, and asserts the `[C116-RESULT] outcome=PASS` marker plus the required input, picker, VFS, regression, ABI, and lifecycle markers.

Evidence is written below `out/dotnet/c011ec116-managed-text-input/`. The important files are:

* `c116.manifest.json` — repository, ABI, API, input, lifecycle, source-hash, build-input, and per-boot metadata.
* `managed-text-input-output.txt` — managed C116 output, including initialization, focus, edits, typed saves, cancellation, and negative probes.
* `input-event-evidence.txt` — native pointer/key events paired with managed text-input output.
* `vfs-results.txt` — open, typed save, reopen, overwrite-decline, overwrite-confirm, cancel-no-write, and final-save checks.
* `mixed-sequence.txt` — the composite application sequence and final result.
* `negative-tests.txt` and `input-contract.txt` — bounded-input, path, capability, ABI, and capacity coverage.
* `lifecycle-counters.txt`, `regressions.txt`, and `repository-state.txt` — runtime/lifecycle and repository evidence.
* `boot-01/` through `boot-03/` — fresh ESP, serial, stdout, stderr, and hashes for each QEMU boot.

The proof is serial-first and deterministic; it does not claim a screenshot or GUI capture. The rendered field and caret are verified through the compositor’s widget text and the managed/native serial markers.

## Negative and compatibility coverage

The C116 probe covers unfocused character input, maximum-length overflow, unsupported control characters, empty backspace, caret insertion, Escape cancellation, empty filename rejection, and path overflow. The mixed flow additionally verifies no write on Escape, preserves the original file after overwrite decline, requires explicit confirmation before overwrite, rejects capability downgrades, retains the C112 ABI-mismatch proof, and retains C114 directory/capacity probes.

No GC algorithm, allocator, host-table layout, or existing C115 evidence is changed by C116. New managed allocations are bounded to the control buffer and the existing picker’s bounded candidate/path/result objects.
