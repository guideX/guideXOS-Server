+# SIR2R1 input reliability closeout

## Result

**Outcome A — SIR2R1_HOSTED_CLOSEOUT.** The FIFO repair and overflow/release invariants passed hosted validation and were committed locally on `INPUT_RELIABILITY`. The AMD64 kernel and UEFI bootloader build passed. The final staged image identity is recorded at `out/input-reliability-qemu-qualified-sir2r1/ESP/input-reliability-build-identity.txt`.

This closes hosted work only. No QEMU guest or AIDA_LPT physical run was performed, so neither guest nor physical input reliability is accepted here.

## Repair review

SIR1 reproduced a lost desktop character when `a down`, `a up`, and `b down` arrived before foreground code consumed input. The native queue retained all three transitions, but the desktop path exposed only a latest-key slot. The old path also silently discarded events when its 64-entry ring reached 63 pending entries.

The legacy desktop path now consumes a bounded FIFO. Key identity, event-time modifiers, shortcut metadata, and a shared sequence number are recorded for each decoded transition. It delivers each make in order, filters breaks from the legacy API, and drains breaks following a delivered make while stopping at the next make. Repeated makes remain distinct, preserving typematic behavior.

The native application path keeps its own FIFO and continues delivering both makes and breaks. It does not depend on whether the desktop consumer has drained its queue. PS/2 producer/consumer index publication uses acquire/release atomics on GCC/Clang and the existing compiler-barrier fallback elsewhere.

Both queues have 64 slots and 63 usable entries. When full, the policy drops the incoming event and records it. One recovery case applies: an incoming break may replace only the final queued make for the same key. This preserves all older FIFO entries while evicting one matching make; the eviction is counted as a dropped make and the missing sequence remains visible. A nonmatching break is dropped without displacing an unrelated make. A release can still be lost when there is no matching final make, so saturation remains visible and cannot be claimed harmless.

The counters are lifetime totals since `init()`; `clear()` counts discarded pending entries in `droppedEvents`. For each queue, the event ledger is:

```text
receivedEvents = dequeuedEvents + droppedEvents + currentOccupancy
overflowDrops = overflowKeyDownDrops + overflowKeyUpDrops
```

The first relation includes events discarded by `clear()`. The second covers full-queue losses, including a make evicted to admit its matching break. Pairing and outstanding-make counters track transitions actually dequeued by that consumer. Sequence gaps become visible as later events are dequeued.

`input.stats` prints both queue snapshots and the IRQ12-routed keyboard-byte counter. It is a diagnostic aid; counters do not prevent finite queues from overflowing under sustained backlog.

## Hosted validation

| Check | Result |
| --- | ---: |
| PS/2 FIFO, independent native delivery, modifiers, IRQ12 routing, saturation, recovery, and wraparound | **110/110** |
| Existing PS/2 modifier and repeated-Tab regression | **40/40** |
| Settings native runtime | **1426/1426** |
| App audio mixer | **PASS** |
| App audio stream | **PASS** |
| Compositor pointer/buttons | **PASS** |
| Virtio GPU/input-routing source smoke | **PASS** |
| AMD64 kernel build, including forced recompilation of the changed keyboard, mouse, and shell objects | **PASS** |
| UEFI x64 bootloader build | **PASS** |

The added overflow assertions verify event conservation before and after matching-break replacement and for a dropped unrelated break. The focused tests also cover SIR1 FIFO order, event-time Shift/Alt state, key-up/key-down ordering, typematic repeated makes, independent native consumption, and both ring wraparounds.

The PS/2 keyboard and mouse translation units compiled without warnings; the new `input.stats` functions also compiled without warnings. A forced full kernel compile emitted 230 warnings in existing code across other components; the final incremental build emitted 46 warnings in the rebuilt dependents. Ten full-build warnings were in `shell.cpp` outside the added diagnostics, including existing narrowing/unused warnings and an array-bounds warning in the NIC diagnostic path. Other examples include unused functions and variables, type-limit/sign comparisons, and an existing desktop string-operation warning. These sites are unchanged and outside the SIR2 input paths; the kernel linked successfully. The bootloader build emitted no warnings. The Settings runtime build emitted one unrelated warning: `kernel/core/app_launch_target_resolver.cpp:48`, unused static helper `text_ends_with`.

Hosted evidence was written under the ignored directory `out/validation/input-reliability-closeout-20261028/`.

## AMD64 image identity

The original `out/input-reliability-qemu-qualified/ESP` directory was preserved and checked. Its kernel hash (`11664641e2201f7080c36aad3eb8b913197f4697766fac2595e5def4d64285d7`) is from the earlier staged build and does not match the final rebuilt kernel after the IRQ12 counter declaration change. Its bootloader still matches the rebuilt output, and its Pac-Man ELF matches the baseline ESP package. It remains **staged, not guest-qualified**. Its historical identity file records the pre-closeout base commit and a modified working tree.

The final closeout staging copy is `out/input-reliability-qemu-qualified-sir2r1/ESP`. It is made from the tracked baseline ESP with the freshly built kernel and bootloader plus the small `Audio Beep` sample package for the audio/input guest procedure. Pac-Man is copied unchanged. The SIR2-specific identity file records branch, source commit, tracked-tree state, count of other untracked evidence files, architecture, keyboard interface, kernel and bootloader hashes, and Audio Beep package hashes. This SIR2-specific file is authoritative; the copied baseline `build-identity.txt` remains historical metadata.

Verified build files:

- Kernel: ELF64 AMD64, 2,800,152 bytes, SHA-256 `2d910f0ee79d01809edb85f9fff68efdd3323c766114ce6ab3830f2f5a928723`.
- UEFI bootloader: x64, 56,832 bytes, SHA-256 `6bbd71782587bdc20c7f21d162a9d701b846f0e4ac8917ddc4e6eda8f5fa17f0`.
- Audio Beep manifest SHA-256: `13437e78d7403061062eb46b0be3673683bb6c1b0b1d9c132763877515257f36`.
- Audio Beep ELF SHA-256: `3b5b77ca9dd85cb7e58a2caad072448724b206c80a158b549423898d7013440e`.

QEMU wrote nothing to the preserved source staging directory. A future guest run must use a fresh private copy of the final ESP because UEFI may create variable-state files in the writable FAT directory.

## Reproducible QEMU guest procedure

No QAC acceptance reservation was requested during this closeout and no QEMU process was started. The QAC status snapshot was Online with zero managed sessions and zero queued requests; no admission grant was obtained. Run the guest procedure only after QAC grants the appropriate `guideXOSServer` acceptance reservation and the .NET Support qualification owner has released priority.

From the repository root, create a run-private writable ESP and log directory. Keep the source staging directory unchanged:

```powershell
$sourceEsp = (Resolve-Path 'out\input-reliability-qemu-qualified-sir2r1\ESP').Path
$runRoot = Join-Path 'out\input-reliability-qemu-runs' ('sir2r1-' + [guid]::NewGuid().ToString('N'))
$runEsp = Join-Path $runRoot 'ESP'
$serialLog = Join-Path $runRoot 'serial.log'
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
Copy-Item -LiteralPath $sourceEsp -Destination $runEsp -Recurse
Get-FileHash (Join-Path $runEsp 'kernel.elf'), (Join-Path $runEsp 'EFI\BOOT\BOOTX64.EFI') -Algorithm SHA256
Get-FileHash 'OVMF.fd' -Algorithm SHA256
```

Launch QEMU as the direct child of the QAC CLI. Resolve `$qac` to the installed QAC CLI and `$qemu` to `qemu-system-x86_64.exe`; do not start QEMU directly or use the x86 prebuilt-kernel launcher for this AMD64 ESP:

```powershell
$qac = $env:GUIDEXOS_QAC_CLI
$qemu = 'C:\Program Files\qemu\qemu-system-x86_64.exe'
& $qac run --project guideXOSServer --mode acceptance -- $qemu `
    -machine pc,usb=off `
    -drive 'if=pflash,format=raw,readonly=on,file=OVMF.fd' `
    -drive "file=fat:rw:$runEsp,format=raw,if=ide,index=0" `
    -m 1024M -vga std -display gtk `
    -serial "file:$serialLog" -rtc base=utc,clock=host -no-reboot
```

Record the QAC reservation and child PID, run ID, firmware hash, copied kernel and bootloader hashes, serial log, start/end time, and the `input.stats` output before and after each interval. The shell counters are cumulative, so compare deltas. Confirm the copied kernel hash again after QEMU exits.

Use this repeatable sequence:

1. Immediately after boot, run a five-minute baseline. In the CLI, rapidly type the fixed text `SIR2-INPUT-0123456789-abcdefghijklmnopqrstuvwxyz` several times and compare the visible result. Then perform short press/release bursts across Up, Right, Down, and Left. Record the counter snapshot before and after.
2. Exercise modifiers: type with left Shift, then right Shift; overlap both Shifts, release one, type again, then release the other and type lowercase. In a native app, use only safe Ctrl/Alt combinations that do not close the app or invoke destructive commands. Record visible output and counters.
3. Launch Pac-Man and verify each direction changes movement and that movement stops after release. Repeat quick direction changes while the game is continuously rendering.
4. Launch `Audio Beep` repeatedly from the native app menu. Each launch submits two overlapping 0.30-second PCM beeps. Send a short CLI typing burst during the audible interval, then return to the CLI and record counter deltas. Repeat enough times to cover multiple active playback intervals. Run the same burst with audio idle as the comparison.
5. Attempt guest saturation while Pac-Man is rendering: hold one direction at the fastest available repeat for five seconds, release it, then allow the app to drain input. A saturation attempt counts only if an overflow counter increases. Afterward, verify occupancy returns to zero, outstanding makes return to zero, sequence gaps and dropped-event deltas are accounted for, and Pac-Man no longer moves. If overflow remains zero, record that saturation was not reached; the hosted saturation/recovery result remains the evidence for that invariant.
6. Keep the same guest boot running for a 60-minute extended interval, then repeat the identical five-minute CLI burst, modifier sequence, and Pac-Man direction sequence. Compare its counter deltas and visible text with the immediate post-boot run.

For every interval capture legacy and native received, enqueued, dequeued, dropped, overflow, make/break overflow, occupancy, high-water mark, wraps, paired, unmatched-break, outstanding-make, and sequence-gap values, plus IRQ12-routed bytes. Audio idle/active and rendering idle/active must be recorded separately so workload comparisons remain interpretable.

## AIDA_LPT physical procedure

The physical run is also pending. Do not write a USB stick, disk, or other physical media for this closeout. Boot only through an already available non-writing test path; if no such path exists, leave physical qualification pending until one is provided.

Before boot, verify that the selected image identity says AMD64 and matches the committed source, kernel, and bootloader hashes above. Record the image root and hash the actual kernel and UEFI bootloader loaded for the test. Record the laptop model and firmware version.

Identify the keyboard path on the running laptop instead of assuming it from the model. Save `dmesg`, `lspci`, and `lsusb` output and note the active keyboard controller/driver and delivery path: PS/2 Set 2 through IRQ1, keyboard bytes recovered on IRQ12, USB HID polling, or another backend. `input.stats` reports the SIR2 PS/2 queues; if the actual keyboard uses another backend, those counters do not qualify its input path.

Run the fixed rapid-typing and modifier sequences once immediately after boot, first with audio idle and then while Audio Beep playback is audible. Repeat rapid typing and press/release bursts while Pac-Man is continuously rendering. Capture `input.stats` before and after each interval; note whether text matches exactly, whether releases stop movement, and all counter deltas. After 60 minutes of continued runtime, repeat the same sequence with audio idle and active. Record whether the audio device actually produced playback and whether the keyboard path changed.

Report the source commit, loaded kernel and bootloader hashes, actual keyboard controller/driver, laptop firmware, boot time, each test interval, visible typing comparison, and all counter snapshots. Do not mark AIDA_LPT successful until the actual device run and its evidence exist.

## Qualification boundary

Hosted validation establishes the decoder, queue, overflow, release recovery, modifiers, IRQ12 routing branch, and adjacent regression suites. It does not measure guest scheduling latency under rendering/audio load, QEMU PS/2 delivery, or AIDA_LPT hardware behavior. Guest and physical qualification remain separate gates.
