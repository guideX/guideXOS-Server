# C188R11R2 Managed Keyboard Dispatch Qualification

## Result

**Outcome D — `EXTERNAL_EXECUTION_BLOCKER`.** Hosted qualification and both diagnostic builds passed, but the one authorized QAC guest boot was not attempted. QAC reported one foreign QEMU process and blocked acceptance reservations. No guest input, managed callback, registry snapshot, screenshot, or serial trace was obtained. The foreign process was left untouched.

The source path supports an ordinary `a` key reaching Managed Notes when its compositor window is focused. Runtime delivery and the registry state at dispatch remain unqualified.

## Baseline and preservation

- Branch: `v1.1_DOTNET_SUPPORT` throughout.
- Starting HEAD: `62f096befacb8f7ab0be1cff6d556a80c8a3fb40`.
- Ending diagnostic HEAD before the closeout commit: `62f096befacb8f7ab0be1cff6d556a80c8a3fb40`.
- Reviewed `NATIVEAOT_C188R11_MANAGED_STATE_PROVENANCE.md` and `NATIVEAOT_C188R11R1_FIRST_INPUT_BOUNDARY.md`. The R11 state snapshot and R1 corrected `managed-return` readiness predicate were retained.
- The pre-existing `ESP/ramdisk.img` remained at SHA-256 `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`. A separate candidate ESP was copied under `out/` and staged there; its final ramdisk hash is `6F673BA41E7245CB1BA7B7857327A4EE74212F09559A920D2B267710F3E00A9D`.
- Preserved the pre-existing untracked paths: `samples/host/ManagedHostServicesC185/{bin,obj}/`, `samples/host/ManagedStarterC182/obj/`, `samples/host/ManagedUiRegression/obj/`, `samples/managed/C185CounterApp/{bin,obj}/`, `samples/managed/HostLogProof/c182-host-smoke/`, and `samples/managed/ManagedUiContracts/{bin,obj}/`.
- No reset, clean, stash, branch switch, history rewrite, or QEMU process termination was performed.

## Input path audit

The planned transaction was QMP qcode `a` down followed by the matching key-up, using the existing `scripts/dotnet/c188-qmp-control.py key a` helper. The helper sends the QMP `input-send-event` key-down, then key-up, and waits for the existing keyboard path to consume them.

The PS/2 Set-2 table maps scancode `0x1C` to ASCII `a` with the normal Caps Lock-off, Shift-up startup state. The shifted table maps it to `A`; Caps Lock also toggles the letter case. Shift make/break updates modifier state and returns before ordinary key decoding or enqueue. Therefore Shift key-down is intentionally consumed as modifier state and is not expected to produce a managed callback by itself. Key releases are also consumed; printable key makes are placed in the existing single pending-key slot. The diagnostic counts slot overwrites without changing that behavior.

The audited route is:

1. QMP qcode `a` produces the guest keyboard make/break transaction.
2. The PS/2 IRQ handler decodes Set-2 `0x1C`; the make maps to `a` and enters the pending slot. Shift follows its modifier-only path.
3. The kernel main loop dequeues the key and calls `desktop::handle_key`.
4. `desktop::handle_key` routes ordinary printable input as `KeyChar` to the focused compositor window. Ctrl printable chords use `KeyDown` instead.
5. The focused managed window's `onKeyChar` calls the existing `invokeManagedInput` bridge.
6. Managed `GuideXosApplicationRegistry.Dispatch` emits the existing R11 `input-dispatch-entry` snapshot before `TryFind`, then performs lookup and calls `HandleInput` for a found resident descriptor.
7. Managed Notes' `HandleInput` is the application callback. Its text area inserts a character only when the text area itself is focused.

Notes being resident does not establish that it is the keyboard recipient. The compositor's active window controls keyboard routing; the text area's pointer-established focus controls text insertion after delivery. Normal managed-window creation registers and focuses its compositor window. The diagnostic wrapper was prepared to require the serial `main-loop-focus` snapshot for `Managed Notes` and to capture the visible window before sending input. It would not click the text area or force focus.

These route conclusions come from the source and hosted contracts. Because QAC denied admission, none of the guest runtime stages was observed.

## Instrumentation

Added the `C188R11R2_KEY` markers behind the existing R11 managed-state provenance build define. The existing R11 managed registry snapshot remains the earliest managed application-dispatch diagnostic and retains its explicit managed sequence, registry identity and epoch, descriptor count, resident state, and reset/teardown provenance fields.

The R2 markers cover PS/2 scan receipt and classification, key enqueue/release/unsupported disposition, pending-slot overwrite count, kernel dequeue, focused window and owner, native managed entry/return and explicit native rejection paths, managed dispatch entry/return, and Notes callback entry. A fixed 16-record IRQ trace buffer defers serial output to the main loop. Event IDs are bounded to 32; kernel and native records carry those IDs. Native `call`/`corr` values reuse the existing R11 invocation correlation. Managed snapshots use their existing managed sequence. These separate counters are not presented as a global execution order.

New diagnostic records are capped: IRQ storage and output at 16 records, native keyboard boundary records at 16, managed keyboard records at 16, and kernel dequeue/focus records at 24. R2 itself adds no serial output or allocation to the IRQ path; the pre-existing C129 Shift marker remains unchanged. No managed ABI or input ABI changed. Production builds omit the R2 marker.

Files changed for instrumentation and qualification:

- `kernel/core/include/kernel/ps2keyboard.h`
- `kernel/core/ps2keyboard.cpp`
- `kernel/core/desktop.cpp`
- `kernel/core/main.cpp`
- `kernel/core/nativeaot_application.cpp`
- `samples/managed/HostLogProof/GuideXos/GuideXosApplication.cs`
- `samples/managed/HostLogProof/Applications/ManagedNotes.cs`
- `scripts/dotnet/build-c188-candidate-kernel.ps1`
- `scripts/dotnet/verify-c188-nativeaot-artifacts.ps1`
- `scripts/dotnet/c188r11r2-runner-predicates.ps1`
- `scripts/dotnet/run-c188r11r2-keyboard-dispatch.ps1`
- `scripts/dotnet/test-c188r11r2-keyboard-contracts.ps1`
- `scripts/dotnet/test-c188r11r2-runner-predicates.ps1`

## Hosted qualification and builds

Final hosted results:

- R9 registry re-entry: 14 cases passed.
- R10 managed-state re-entry: 14 cases passed.
- R11 managed-state provenance: 13 cases passed.
- R11/R1 readiness predicates, including the required `managed-return` marker: 26 cases passed.
- R11 bounded-output checks: 4 cases passed.
- R2 keyboard source contracts: 14 cases passed, including Shift consumption, Set-2 `a` mapping, printable routing, focus rules, and Notes editor focus.
- R2 evidence predicates: 8 cases passed.
- `git diff --check`: passed; Git emitted only the repository's LF-to-CRLF working-copy warnings.

The first hosted attempt caught a namespace error in the new Notes marker reference. That reference was corrected, the source contract was strengthened to check it, and the complete hosted suite passed from the fresh `keyboard-dispatch-002` output root.

Diagnostic build results:

- NativeAOT diagnostic: PASS, ELF SHA-256 `6B41883A2BDF6F8DCA3448ABB5459C6456D7842B2BBC7973828B2FE7DE78750C`.
- Diagnostic kernel: PASS, ELF SHA-256 `0032BD297D86314D86B182C8CFDC7A15E3C12FE07BDEE5F192A2BFFD78403B93`.
- Candidate ramdisk staging: PASS; Managed Notes app SHA-256 matches the diagnostic NativeAOT ELF, 185 unrelated files preserved, zero other files changed.

Proof-disabled production build results:

- NativeAOT: PASS, ELF SHA-256 `2B98CC9FD09EF1EFB1CF7ACC50D7CC216EAC5442D5B5B3729FB611C04A9A3398`.
- Kernel: PASS, ELF SHA-256 `2BFCB9D3BEDEA1C4C161B5978EC1A71B33BCF11FAC846815B6D9EE49986465BF`.
- Byte scans found `C188R11R2_KEY` in both diagnostic artifacts and absent from both production artifacts.

The hosted logs and build/staging manifests are retained under `out/dotnet/c188r11-managed-state-provenance/`. Key manifest hashes:

- Hosted R9–R11 manifest: `DC16A2FBB83A67E85EFC5B85026794DEC8EA9DDBA0DB42ED32B3C9227C74B50C`.
- Diagnostic NativeAOT manifest: `F26DE3540CDBDF850C4C97951842154EEB261A4A35556B256F5EBCC92F486A1B`.
- Diagnostic kernel qualification: `2859FBB6932D1238621A3A3163B89DA8D4957C80A9ED2EB537151F0707DC93A0`.
- Production NativeAOT qualification: `BFC1896267D8869E4524453EA92648727C9C5FBA29BB8B8E64F2C335E53825F2`.
- Production kernel qualification: `834DC4323895CB009D6F226A159410280AB8BCF35AE5E3A8A20C3C776C6785B2`.
- Candidate ramdisk staging manifest: `75DD947E393CA6FA6FA9103F313B73FE28AF1A9BD372E7507FAE5C51B0495581`.
- R2 keyboard contract log: `A81B9A2FF0B07440625B7C6C2344F178F2467AE88E3ADE3814EA8B1A147E482E`.
- R2 runner predicate log: `1522B4ACC5D4A9F88567BD926FF4C277649510AD868EAF8FA0512302B4CEAF91`.

## QAC admission and runtime evidence

The preflight evidence is retained under `out/dotnet/c188r11-managed-state-provenance/preflight/keyboard-dispatch-001/`.

- QAC coordinator: online.
- QAC process census: 1 QEMU process, 1 foreign; local process census found PID `10820`.
- Reservations: 0; managed sessions: 0.
- QAC list explicitly warned that the foreign QEMU process blocks acceptance reservations.
- No reservation was acquired and no guest was launched. No QMP input was sent, so no QAC-owned shutdown or release was attempted.
- Preflight manifest SHA-256: `74B40AE60979A498A5B9C7D0DC669F2733CC592FCBC8644E9E81EB05A12EBA57`.
- QAC status SHA-256: `C05141D3C85ABF40E243011A5650BE0905469457E1C70D8B5321625ECDC9C0E7`.
- QAC list SHA-256: `DD7815DA4884962A3752322EF37FFE2B9D60C2060120A07B011E805B9D5F6216`.

There is no guest serial log, screenshot, QMP transaction log, runtime input boundary, managed callback record, or runtime registry state for this phase. In particular, managed dispatch was not observed; registry population at a keyboard dispatch boundary is unknown. No registry corruption or application defect is inferred from this blocked run.

## Interpretation and next action

Shift-only input is expected to stop at kernel modifier-state handling and produce no managed callback. The selected ordinary input is `a` because the existing Set-2 mapping turns scancode `0x1C` into ASCII `a`, and the existing compositor path routes ordinary printable characters as `KeyChar` to the focused window. Hosted contracts and diagnostic builds support that route, but QAC admission prevented end-to-end qualification.

Root-cause confidence is high for the intentional Shift behavior and the source-level `a` decode/routing contract. Runtime delivery confidence is unestablished because no guest ran. There is no evidence to characterize a registry loss, empty registry, native/managed transition failure, or Notes editor behavior.

The last confirmed boundary is source-level PS/2 decoding and enqueue behavior. There is no confirmed guest runtime boundary. The smallest next action is for the owner of foreign QEMU PID `10820` to release or stop it; then re-run the QAC census and, when it reports zero foreign processes and zero reservations, perform the single already planned `a` down/up transaction. Do not use another input or boot until that exclusive admission check passes.
