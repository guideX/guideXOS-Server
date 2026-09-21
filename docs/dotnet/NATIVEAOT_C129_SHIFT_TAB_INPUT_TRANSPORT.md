# NativeAOT C129: Direct Shift/Tab Input Transport

## Scope and outcome

C129 restores a bounded NativeAOT proof of a real Shift+Tab gesture through the existing guideXOS keyboard path. The final proof uses QEMU's QMP `input-send-event` key events, the PS/2 IRQ path, the native compositor/input bridge, the existing Host ABI v1 input record, and `GuideXosControlHost`. It does not call managed focus navigation from the native fixture.

Primary outcome: **Outcome A — transport defect repaired and native Shift+Tab proof restored.**

The earlier C127 reverse-focus check was not an authoritative transport proof: it used the internal `invokeManagedKeyDownForProof` helper to re-enter managed code directly, while the forward check used compositor calls. A bounded C127 reproduction with `-SkipKernelBuild` reused the ordinary kernel and correctly timed out because that kernel had no C127 proof marker. Re-running with the matching diagnostic kernel reached the old direct sequence, but the full mixed C127 wrapper later stalled before its native focus-order result. The C127 managed 60-case fixture itself passed; C129 replaces that non-physical reverse proof with a bounded physical transport proof.

C129 closes that proof gap and repairs the demonstrated production modifier-state weakness. The old keyboard driver kept one aggregate Shift bit. C129 tracks left and right Shift independently and derives the aggregate state, so releasing one key cannot clear the other key's active modifier. The managed transport itself already represented Tab correctly as a key-down-only control event; no synthetic Tab `KeyChar` was added.

Primary classification is therefore **both**: the original reproducible completion failure was in the validation harness/QEMU injection synchronization, while production also had a real two-Shift bookkeeping weakness that could clear an active modifier prematurely. The repaired harness sends explicit physical key transitions, and the repaired driver preserves the aggregate modifier until both physical Shift keys are released.

## Reproduction and diagnosis

The narrow existing reproduction was:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\dotnet\run-c127-managed-panel.ps1 `
  -RepoRoot D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT `
  -SkipManagedBuild -FreshBootCount 3 -TimeoutSeconds 120
```

With `-SkipKernelBuild`, this reused the canonical ordinary kernel rather than a kernel compiled with the C127 proof flag. It produced no C127 result marker and timed out. Repeating with the matching C127 kernel showed the old direct managed re-entry rather than QEMU key injection; the later complete C127 wrapper stalled after repeated `C121-TAB` direct traversal markers and before its C127 focus-order result. There was therefore no trustworthy evidence that a physical Shift+Tab had crossed the keyboard, native queue, ABI, and NativeAOT dispatch boundaries.

The old input path is now documented by the C129 trace. Set-2 Shift make/break events are consumed by the native keyboard driver. Tab (`0x0d`) is translated to `\t`, queued as a control key, and dispatched as `KeyDown`; printable characters use the separate `KeyChar` path. `GuideXosControlHost.HandleInput` maps Tab to `Traverse(shift)`, so it does not wait for a character phase. The C129 runner waits for each managed acknowledgement before injecting the next event, which distinguishes a lost transport event from stale serial output or runner ordering.

## Repair and event semantics

The production repair is limited to `kernel/core/ps2keyboard.cpp`:

- left and right Shift have independent pressed state;
- `is_shift_down()` and ASCII translation use their OR aggregate;
- every Shift make/break is consumed by the modifier handler and emits an ordered diagnostic trace in the C129 image;
- Tab remains key-down-only and does not generate a printable `KeyChar`;
- the native bridge snapshots the current aggregate modifier into the existing KeyDown payload;
- the native bridge logs the Tab KeyDown and following ordinary `KeyChar` result without changing the ABI.

The required sequence is:

1. QEMU QMP `input-send-event` produces separate left-Shift press, Tab press/release, and left-Shift release events. This avoids the observed HMP combined-key ordering on the failing third boot, where Shift was released before Tab.
2. The native trace shows Shift aggregate `1`, Tab KeyDown with Shift `1`, and eventual Shift aggregate `0`.
3. Managed Notes observes one reverse traversal from the initial no-focus state to `Document`, with `modifier=shift` and `keychar=none`.
4. The runner injects plain Tab and then `a` only after the previous serial acknowledgement.
5. Managed Notes observes one forward traversal to `Open`, then an ordinary character with no activation.
6. A bounded `C129-RESULT outcome=PASS transport=production` marker terminates the proof.

The release path is not allowed to latch state: a dropped Tab does not alter Shift state, a late Shift release clears only its physical side, and a timed-out proof is terminated rather than waiting indefinitely. The four-control C129 context is an existing `GuideXosControlHost`; it is not a Panel change or a new owned control tree.

## Managed proof coverage

`GuideXosShiftTabTransportTests` contains 31 focused cases covering forward/reverse traversal, no Tab character, no phantom activation, disabled and hidden skipping, modal isolation and restoration, stale split-Space consumption, and repeated forward/back traversal. The native proof adds the production Host ABI observations for Shift+Tab, plain Tab, and a printable character. C128's 46-case lifecycle fixture and C127's 60-case Panel fixture remain separate regressions.

The C129 proof context uses four registered controls to keep the transport boundary narrow. The C128 Notes lifecycle context remains the seven-registration fixture: `Open`, `Save`, `Save As`, `Show Path`, `Full Path`, `File Name`, and `Document`. No Panel nesting, ownership, or registration behavior was changed.

## Native proof and validation evidence

The runner is `scripts\dotnet\run-c129-shift-tab-input-transport.ps1`. It starts one fresh QEMU process per boot with a TCP QMP monitor, sends explicit Shift/Tab/character transitions in order, waits for the exact serial acknowledgement after each command, and stops at a bounded deadline on failure. The authoritative evidence is under:

`out\dotnet\c011ec129-shift-tab-input-transport`

The final three fresh NativeAOT boots all passed. The manifest is
`out/dotnet/c011ec129-shift-tab-input-transport/c129.manifest.json`.
The composite ELF SHA-256 is
`1BC87CFC33409F48CAF55F11A93DE70D75B7CB1B343A7A67F02B2B2430624090`;
the proof-kernel SHA-256 is
`8D103A8DB87B2CB3FFB79B80F04567EF983ECA4755DB68E647BF50AA1FA8DDCA`;
the proof ramdisk SHA-256 is
`024F9FFC690C1185D49388F7F53157995FBE19AB8F935875F96D2EA98F41EC7F`.
Serial SHA-256 values for boots 1-3 are respectively
`70E1A5D8DA8C65FCB118A3FFF0B222817C55FBAD280CE068D52B7C6A8064114E`,
`F7FD7066CFCC672BA72AE8D3DE5F8B18D0A9CFE3B3B56C67B3233AF7C64466A5`,
and `B4E5C40572AC65BCB580D123D73534CC58EB27B38306789364BD8A397EEBEAE6`.
The manifest SHA-256 is
`9EF932F30C935F731BABDB1FCD11D4C4AAF1668AEC5248BDBD9E59ACD1FAE392`.

The ordered boot-3 trace included:

```text
[C102-MANAGED-OUTPUT] C129-MANAGED proof-started initial-focus=none registration=4 tab=key-down-only result=PASS
[C129-KEYBOARD] shift=down side=left aggregate=1 result=PASS
[C129-NATIVE] tab-keydown shift=1 transport=production result=PASS
[C129-MANAGED reverse=Document count=1 modifier=shift keychar=none result=PASS
[C129-NATIVE-INPUT] kind=key-down key=00000009 shift=00000001 result=PASS
[C129-KEYBOARD] shift=up side=left aggregate=0 result=PASS
[C129-NATIVE] tab-keydown shift=0 transport=production result=PASS
[C129-MANAGED plain-tab=Document->Open count=1 modifier=none result=PASS
[C129-MANAGED ordinary-char=a focused=Open activation=none result=PASS
[C129-MANAGED C129-RESULT outcome=PASS transport=production
[C129-NATIVE-INPUT] kind=key-char value=00000061 shift=00000000 result=PASS
```

After the managed result, the same bounded native proof audits both physical
Shift sides without injecting another navigation gesture:

```text
[C129-KEYBOARD] shift=down side=right aggregate=1 result=PASS
[C129-KEYBOARD] shift=down side=left aggregate=1 result=PASS
[C129-KEYBOARD] shift=up side=right aggregate=1 result=PASS
[C129-KEYBOARD] shift=up side=left aggregate=0 result=PASS
```

This demonstrates that releasing right Shift while left Shift remains held
does not clear the aggregate modifier, and the final release clears it.

The runner exit status was zero for all three boots and no proof timeout was
reported. The C129 managed fixture passed 31 focused cases on each proof
boot. The earlier HMP attempt is retained in the evidence history as a
failed harness experiment: boots 1-2 completed, while boot 3 stalled because
the combined HMP command released Shift before Tab. It is not counted as a
C129 proof boot.

## Ordinary-kernel restoration

After the proof image, the canonical ordinary kernel artifact was restored
from the pre-existing ordinary `ESP\kernel.elf` into
`kernel\build\amd64\bin\kernel.elf`; both copies were then verified equal.
The ordinary validator performed three fresh QEMU boots and checked that the
copies remained unchanged. The ordinary manifest is
`out/dotnet/c129-ordinary-boot-final/run-20260920-180758159-09594b4d/ordinary-boot.manifest.json`.
It reports PASS for all three boots, with serial SHA-256 values
`F256D411D6BB9C82E052931019729EED043C5AF965A663CFDF7DB3F7E6D5D96A`,
`75F6AA60796BDC0AF9B576A9E66351CBFC8F603B5AC31EB635C3DCB9A079E15C`,
and `0E8AC2328C3C3E93B53E591DE6986ED9389A8126507A952FF145F0BC56B792AC`.
The ordinary kernel SHA-256 is
`A3325E6C5E62C4ECD9C463EE38DB0763DDA2A67F54D2A1AE1F86673B37457CDF`
in both locations; the manifest SHA-256 is
`70B1C7B93D39E2B8D5327762D246A5ADDB73CE1E2751AB40882A03930074B2CD`.
These boots validate the normal kernel only; they do not claim bare-metal
validation and do not run the C129 managed fixture.

## Preserved interfaces and limitations

- ABI remains v1 with table size 104; no capability or ABI expansion was made.
- NativeAOT runtime, GC, VFS, composite-image, App Model, and lifecycle architecture remain unchanged.
- Panel remains single-level and non-owning.
- Notes' C128 lifecycle fixture still has seven registrations; the C129 transport context intentionally registers four existing controls.
- Evidence is from fresh QEMU boots and must not be read as bare-metal evidence.
- The proof covers the existing PS/2 set-2 path and the two physical Shift sides. Other keyboard layouts, USB HID-specific scan translations, and arbitrary concurrent key streams are outside this bounded phase.
