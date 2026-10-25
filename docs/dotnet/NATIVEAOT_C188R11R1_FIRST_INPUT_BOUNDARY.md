# NativeAOT C188R11R1 First Input Boundary

- **Classification:** Outcome C — `DIAGNOSTIC_INCONCLUSIVE`
- **Repository:** `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`
- **Branch:** `v1.1_DOTNET_SUPPORT`
- **Starting HEAD:** `a22b79b3e1c2cbc6c6d5e64a6952546f9ef85b39`
- **Ending validated HEAD before the phase commit:** `a22b79b3e1c2cbc6c6d5e64a6952546f9ef85b39`
- **Product behavior correction:** none

## Scope

This phase fixes the C188R11 readiness check, adds bounded managed and native sequence markers, qualifies the checkpoints that can be observed safely, and uses at most one QAC-managed diagnostic guest boot. The readiness check accepts the emitted `managed-return` marker and requires a successful Notes launch, its surface, the main loop, populated resident state, initialization evidence, and a matching native launch boundary. Missing prerequisites have explicit failure reasons.

Managed events use a 128-entry saturating sequence counter whose lifetime is the diagnostic NativeAOT image's static-state lifetime. Kernel records use a separate `nseq` counter. The counters are independent; the `call` and `corr` fields correlate each managed invocation with its native boundary without claiming one global order. State records include registry, pool, and pool-storage identities and epochs, descriptor count, resident mask, Notes owner generation, replacement, clear, reset, and teardown counts.

The diagnostic build accepts at most 255 managed log bytes; the kernel scans at most 256 bytes, including the terminator. Other builds retain their 127-byte / 128-byte limits. State records are capped at 48, native init records at 16, and existing native host and invocation records remain capped at 24 each. The hosted output-bound checks passed; the guest produced 14 C188R11 managed records, with a largest body of 157 bytes.

No managed ABI structure or public layout changed. The diagnostic-only C# instrumentation is compile-time gated. Proof-disabled NativeAOT and kernel builds passed, and the production kernel retained its existing hash.

## Hosted qualification

The combined hosted qualification passed at `out/dotnet/c188r11-managed-state-provenance/hosted/state-provenance-015/manifest.json`:

| Qualification | Cases | Result |
|---|---:|---|
| C188R9 registry reentry | 14 | PASS |
| C188R10 state reentry | 14 | PASS |
| C188R11 state provenance and bounded sequence | 13 | PASS |
| C188R11 runner predicates | 26 | PASS |
| C188R11 output bounds | 4 | PASS |

Diagnostic NativeAOT qualification passed: ELF64 x86-64, seven load segments, zero relocations, and eight required retained symbols. The diagnostic kernel qualification passed with the freestanding flags recorded in `build/diagnostic-kernel-002/kernel-qualification.json`. Proof-disabled production NativeAOT and kernel builds passed.

| Artifact | SHA-256 |
|---|---|
| Diagnostic NativeAOT ELF | `A9E8FF4DA9567E10A8D9C026B1B0C177EDFE2FF78DB3543FAACE57EC22606726` |
| Diagnostic kernel ELF | `F6C0978CDC826C9A417F80B3AFC04F06FC70AFF367534F8AEC83467F25B011F8` |
| Production NativeAOT ELF | `FCFDFE88604253706E8AFCFCFA411ED4725EDC33A5E7E5D276EE8A7961D5055E` |
| Production kernel ELF | `22BC4AFE228263E4187B2AA57EAF21D1CEA46D3992A139F81EC185B1C22154AF` |

The modified tracked `ESP/ramdisk.img` was copied into an isolated candidate before staging. Its original SHA-256 remained `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`. The staged ramdisk SHA-256 is `43F2BC12AF9F89326B3C26A54A7E84C2EED36C48613F9C91C3ECDF372B59066D`; the staged kernel and bootloader matched their validated inputs.

## QAC run

One guest was launched from `out/dotnet/c188r11-managed-state-provenance/runs/diagnostic-002/guest-001/`. The pre-run census showed an online coordinator and zero QEMU processes, reservations, managed sessions, and queued requests. QAC granted reservation `90d782a3859945e8888065d09963515e`; the reservation owner was verified and QEMU PID `32948` was owned by runner PID `29260`.

The corrected runner reached readiness, captured a 1280×800 screenshot showing Managed Notes, and recorded checkpoint B as unavailable because the existing interface has no safe read-only managed snapshot request after main-loop readiness. Checkpoint A (`managed-return`) was therefore the latest nonmutating managed observation before input.

One QMP `input-send-event` was sent: key-down for `shift`. The QMP reply was successful, and serial recorded `[C129-KEYBOARD] shift=down side=left aggregate=1 result=PASS`. The event did not produce a managed input callback. The runner was then told to quit through QMP, which is a shutdown command, and the guest exited with code 0. No second input event or guest boot was issued.

QAC emitted `reservation ... is degraded. Exclusive conditions compromised: QEMU process census is incomplete.` The warning is recorded in `qac.stderr.log` and `acceptance-manifest.json`. Admission and ownership checks still showed the sole QEMU as managed with owner verification. QAC logged the matching reservation release; the post-run census showed zero QEMU processes, reservations, sessions, and queued requests. The local QEMU process list was empty after shutdown.

The runner's acceptance manifest is `result=FAIL` because the requested first managed input boundary was not observed. The reservation was released successfully. The degraded census warning is reported separately from both results.

## Checkpoints and observed state

| Checkpoint | Evidence | State |
|---|---|---|
| A — managed launch return | Serial line 418, managed sequence 13, `st=managed-return` | Five descriptors; registry identity/epoch `1/1`; pool identity/epoch `1/1`; storage identity `1`; Notes owner generation `1`; resident mask `1`; generation high-water `1`; replacement, clear, reset, and teardown counts all `0`; result `0`. |
| B — pre-input snapshot | `active.json` and runner output | Unavailable. No existing safe read-only snapshot request interface. Checkpoint A is the latest managed observation. |
| C — first input-dispatch entry | QMP log and serial | Kernel accepted the Shift key-down. No native `route=input` boundary, managed `input-dispatch-entry`, or managed input-entry marker followed it. |
| D — input registry lookup | Serial | No input-side `TryFind(4)` snapshot. The only `st=lookup` row is launch sequence 10; it returned `0` with five descriptors before Notes became resident. |

The launch timeline is fully populated:

| Managed sequence | Stage | Relevant state |
|---:|---|---|
| 7 | `C188R11-INIT` | Registry and pool epochs `1`; registry, pool, and storage identities `1`. |
| 8 | `launch-dispatch-entry` | Five descriptors; mask `0`; no resident Notes instance yet. |
| 10 | `lookup` | `TryFind(4)` result `0`; five descriptors; mask `0`. |
| 12 | `instance-start` | Notes owner generation `1`; resident mask `1`; generation high-water `1`. |
| 13 | `managed-return` | The same identities and epochs; five descriptors; resident mask `1`; generation `1`; no clear, replacement, reset, or teardown; result `0`. |

Native launch records show module `1`, entry offset `0x101290`, seven segments, span `0x24D000`, and a successful return. The managed invocation and native launch share correlation `1`. No native input-boundary pair exists in this run.

The raw serial SHA-256 is `5438292378D23A0398CDF7B079B1865ADE1F4C95D4AF33A6387179E379D418EA`; the screenshot SHA-256 is `394C9EF89728834207655E909A821CC590A1526BB3CA511DC14FE6E1DA34200D`. The QMP control log records exactly one input event followed by the shutdown command. The screenshot review copy is `desktop-ready.bmp`; `desktop-ready.ppm` is the original QEMU capture.

## Conclusion and next bounded action

The corrected readiness predicate reached screenshot and input qualification. The guest shows the launch-side state and the kernel's handling of the single key event. It does not show the first managed input callback or an input-side lookup. The earliest observed gap is between kernel keyboard handling and managed dispatch. The registry-loss mechanism remains unknown; no root cause or runtime fix is claimed. Classification is **Outcome C — `DIAGNOSTIC_INCONCLUSIVE`**.

The next bounded diagnostic should use a single QMP pointer-down at a verified point inside the visible Notes text area, with markers that distinguish native input entry, managed input entry, and input-side lookup. Run it only under a separately authorized guest budget. Investigate the recurring QAC census warning as a separate admission diagnostic.

## Changed phase files

- `kernel/core/nativeaot_application.cpp`
- `samples/managed/HostLogProof/GuideXos/GuideXosApplication.cs`
- `samples/managed/HostLogProof/GuideXos/GuideXosApplicationRegistryC188R11Tests.cs`
- `samples/managed/HostLogProof/GuideXos/GuideXosHost.cs`
- `samples/managed/HostLogProof/GuideXos/GuideXosManagedApplicationC150Tests.cs`
- `samples/managed/HostLogProof/Program.cs`
- `scripts/dotnet/c188r11-runner-predicates.ps1`
- `scripts/dotnet/fixtures/c188r11/state-provenance.serial.txt`
- `scripts/dotnet/run-c188-qac-guest.ps1`
- `scripts/dotnet/run-c188r11-managed-provenance-tests.ps1`
- `scripts/dotnet/test-c188r11-output-bounds.ps1`
- `scripts/dotnet/test-c188r11-runner-predicates.ps1`
- `scripts/dotnet/verify-c188-nativeaot-artifacts.ps1`
- `docs/dotnet/NATIVEAOT_C188R11R1_FIRST_INPUT_BOUNDARY.md`

The pre-existing `ESP/ramdisk.img` modification and pre-existing generated/untracked paths were preserved. Phase changes and this report are committed locally; no remote push was attempted.
