# Server v0.3 — S22R21 Public Preview Guest Qualification

**Assessment date:** 2026-11-10 (local); QAC timestamps are UTC 2026-11-11

**Phase outcome:** `DISPLAY_CORRECTED_CANDIDATE_QUALIFIED`

**Candidate SHA-256:** `86a3296adf0558fa509a044232b0086e57edd6c0bd4236677fba90d8a294d5fe`

## Admission and exact VM profile

Guest admission followed hosted, freestanding, release-integrity, and direct ISO/embedded-ESP verification. QAC coordinator version 2.0.0 granted exclusive acceptance reservations. The counted candidate runs used the same exact ISO hash above and the S22R18 restricted disposable profile:

```text
-machine pc,accel=whpx -smp 2 -m 2048M -vga std -display none
-drive if=pflash,format=raw,readonly=on,file=D:\dev\guideXOSServer\OVMF.fd
-drive if=none,id=releasecdrom,file=<candidate ISO>,media=cdrom,readonly=on,format=raw
-device ide-cd,drive=releasecdrom,bootindex=1 -boot order=d -net none
-serial file:<run serial log> -no-reboot -no-shutdown -monitor none
-qmp tcp:127.0.0.1:<run-specific port>,server=on,wait=off -S
```

The OVMF code image was read-only, 3,653,632 bytes, SHA-256 `2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d34733393ab2792`. Candidate CD-ROM was read-only; no writable disk, persistent UEFI variables, NIC, host share, guest COM2 endpoint, or HMP monitor was attached. QEMU was QEMU 11.0.0 (`v11.0.0-12122-ga4bb4b10c9`) with WHPX. QAC started paused and verified the owned child process and loopback QMP endpoint before guest interaction. The exact argv, child identity, source ISO hash, and reservation are retained in each `output\execution.json`.

## Counted guest evidence

### Display and desktop — run `s22r21-final-positive-r3-3258b2d2593d475fb8fb9ba69e7a67c2`

- Reservation `14b8f1e2947b4e14a2037ca1f1d33461`; QEMU PID 28764; QMP `127.0.0.1:55993`.
- The guest booted. Modern Settings opened and navigated to Display; its framebuffer snapshot remained read-only and Settings navigation stayed responsive. Screenshot: `qac-positive-r3\s22r21-final-positive-r3-3258b2d2593d475fb8fb9ba69e7a67c2\output\screenshots\modern-display.ppm`.
- The legacy Control Panel route opened Display Options. The Displays tab visibly showed the explicit v0.3 Public Preview unsupported message and only a Close action; no mode, monitor, resolution, Apply, or OK configuration controls were present. Screenshot: `...\screenshots\legacy-display-tab.ppm`.
- Close, reopen, and re-navigation to the Displays tab worked. The reopened view returned to its normal default tab before re-navigation. Screenshot sequence is in the same `screenshots\` directory (`legacy-closed.ppm`, `legacy-reopened.ppm`, `legacy-reopened-display.ppm`).
- This run's overall PacMan evaluator result was `FAIL` because the app was closed with the window X and the expected clean Escape return marker was not observed. QAC verified child exit 0 and reservation release. The run counts as Display UI evidence only, not as PacMan acceptance.

### PacMan and VFS/runtime — run `s22r21-final-positive-r4-a9e119800f454a81b85d6eb36a1b5ac7`

- Reservation `f20463d226c941f484d2a09d58bf6fe7`; QEMU PID 23528; QMP `127.0.0.1:57746`.
- The same candidate ISO hash was checked before launch. Desktop and Start menu operated; PacMan launched from Start, rendered gameplay, and responded to directional input. Screenshot: `qac-positive-r4\s22r21-final-positive-r4-a9e119800f454a81b85d6eb36a1b5ac7\output\screenshots\pacman-input.ppm`.
- Escape produced the approved native entry, return, cleanup, and smoke markers. The serial log reports approved `com.guidexos.pacman` entry, `GX_MAIN_RETURN`, result 0, `fault=none`, 924 frames, 5 input events, `RUNTIME_CLEANUP`, and `lifecycle PASS window/resource cleanup complete`; `[PACMAN-SMOKE] result=PASS proof=approved-entry-clean-return-presented-frame-key-events`. Screenshot `desktop-after-escape.ppm` shows the responsive desktop after cleanup.
- The approved runtime identity check and read-only `/Apps` source policy remained in the path. The QAC profile attached no writable media; no unexpected kernel exception was observed. This verifies the supported launch configuration, not resistance to an adversary who can mutate a live guest disk.

Both runs used the exact candidate manifest and serial/screenshot evidence under the S22R21 external run root. The r4 run exited QEMU with code 0 and verified the QAC reservation release.

## Negative security requalification

The enforcement code was not changed by the Display UI correction, but a new kernel/ISO identity was created. A narrowly scoped negative guest diagnostic therefore checked the changed binary's active resource policy without treating the modified test media as a release candidate.

- Diagnostic run ID: `s22r21-resource-tamper-negative-81f6f1590234488eb129b48eaf667b13`; reservation `8bcdd5e969dc434695a0b7bad8e31768`; QEMU PID 18448; QMP `127.0.0.1:60930`.
- A separate diagnostic ISO, `diagnostic-resource-tamper.iso`, is 90,245,120 bytes, SHA-256 `1d302ad38f0eef73cc143a787aa312ebb7171e53e724f34fd8ea4e80da199db1`. It was generated from a copy of the candidate ESP. Exactly one byte was changed in `Apps/PacMan/resources/audio/EatPill.wav`: original 7,156-byte file SHA-256 `510a52e5776573197aef14d77e393185b6c7148d1b59dac535d45c5ca0498686`; altered file SHA-256 `868ab1580302f4e88120bcb767bab796cc0ce582ec7a18db2cd80835795743e1`. Its ramdisk SHA-256 is `adecb0ccd3406aabe66fafed35c7261a5cfc5b0eac15d94e305707d248954368`, and ESP image SHA-256 is `82e635feeb13aad7a8d2d7e3783057cf2c49422f1bd256780b33058a4744c718`.
- When PacMan was requested, the guest displayed “Not available in bare-metal mode.” Serial recorded the changed resource digest, `decision=REJECT reason=package-resource-integrity-or-source-mismatch`, and `decision=REJECT reason=package-or-source-changed-before-load`. There was no `ALLOW`, `GX_MAIN_ENTER`, `GX_MAIN_RETURN`, or PacMan smoke pass marker. This is the expected pre-entry rejection; the diagnostic runner labels the expected negative classification separately from a candidate acceptance pass.
- The diagnostic is not the candidate and was kept under `negative-resource-tamper-r1\` and `qac-negative\`. Screenshot: `qac-negative\s22r21-resource-tamper-negative-81f6f1590234488eb129b48eaf667b13\output\screenshots\diagnostic-reject.ppm`.

This focused guest check complements the existing hosted suite: 27 Native ELF policy cases, 37 release-integrity assertions, and 7 QAC profile assertions. It does not claim a guest attack test for arbitrary third-party ELF injection, writable package media, or concurrent TOCTOU mutation.

## Attempt and cleanup ledger

Several setup attempts were not counted: one malformed manifest path reached an empty-media QEMU start and never booted the candidate; a QAC default-root preflight failed before admission; and an early r2 UI attempt used rejected `click 1` syntax. The r2 runner record remained stale/RecoveryRequired because it did not verify child exit, so it is not counted as acceptance evidence. Those attempts were not used to infer a candidate failure or success.

The valid r3 and r4 records show verified child exit, QAC release, and reservation release. The negative diagnostic also records child exit 0 and verified release. After the diagnostic, direct QAC queries reported coordinator Online (2.0.0), QEMU processes 0 (0 foreign), reservations 0, managed sessions 0, and queued reservations 0. No task QEMU process remained.

## Qualification result and limits

The exact S22R21 candidate passes the changed Legacy Display UI check, preserved Modern Settings navigation, PacMan launch/input/clean Escape exit, and one resource-tamper rejection before native entry. The correction did not weaken runtime identity, ABI, resource-integrity, or read-only source enforcement. Outcome: `DISPLAY_CORRECTED_CANDIDATE_QUALIFIED`.

This does not qualify physical hardware, other QEMU versions, other hypervisors, an interactive host launcher, persistence, writable storage, network access, arbitrary Native ELF software, or process isolation. PacMan still executes with kernel privilege; the kernel has no process address-space isolation. The release owner must decide whether those risks and the remaining `/Documents`, recent-history, COM2, reproducibility, signing, and publication issues are acceptable. No owner decision, signature, publication, or readiness marker is implied.
