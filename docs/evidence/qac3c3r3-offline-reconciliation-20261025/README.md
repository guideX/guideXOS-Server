# QAC3C3R3 offline reconciliation

Date: 2026-10-25
Disposition: **Outcome B — QAC3C3 remains open**
Source run: `qac3c3r2-s22r10-boot2-20261025-1301-544488a076de4301ad6a82f99ca62bec`

This record is append-only. It references, but does not alter or copy, the retained R2 evidence at:

`C:\Users\guideX\AppData\Local\guideXOS\QAC\artifacts\qac3c3r2-s22r10-boot2-20261025-1301-544488a076de4301ad6a82f99ca62bec`

## Run identity and ownership

- Run ID: `qac3c3r2-s22r10-boot2-20261025-1301-544488a076de4301ad6a82f99ca62bec`
- Owner: `LEON2_RDT\guideX`; token `544488a076de4301ad6a82f99ca62bec`
- Workspace path: the source path above; output root `%LOCALAPPDATA%\guideXOS\QAC\artifacts`
- Reservation: `5555811b6f1b438e84f7d2f2648ea24c`
- QAC project/mode: `guideXOSServer` / `acceptance`
- QAC owner process: PID `30912`
- Direct QEMU child: PID `13804`, `C:\Program Files\qemu\qemu-system-x86_64.exe`
- QMP endpoint: `127.0.0.1:60212`
- Child start: `2026-10-25T20:02:30.413235Z`

The manifest, `.qac3c2-owner.json`, and `.complete` marker name this same run and owner token. The owner marker’s creation time is `2026-10-25T20:02:21.9088508Z`. Their SHA-256 values are listed below.

## Lifecycle reconciliation

| Evidence source | Recorded result | Evidence class |
| --- | --- | --- |
| Original `output/execution.json` | `FailedWithRetainedEvidence`; `recoveryRequired=true`; `qacCliReleaseReported=false`; `reservationReleaseVerified=true`; QEMU exit 0; CLI exit 0; guest `FAIL`; failure says inspect reported Released but CLI did not report verified release | Runner-observed state, preserved unchanged |
| `output/qac-run.log` | Reservation requested and granted; direct child PID 13804 launched at 20:02:30.413235Z and exited with code 0 at 20:08:41.4713205Z; no final `QAC reservation released` CLI line | Runner-observed output |
| `output/qmp-transcript.jsonl` | `quit` sent at 20:08:41.4032962Z; `SHUTDOWN` event reason `host-qmp-quit`; successful reply at 20:08:41.4054053Z | QMP transcript |
| `output/qac-inspect-5555811b6f1b438e84f7d2f2648ea24c.json` | Captured at 20:08:42.0361615Z; exact reservation ID, project, acceptance mode, `Released`, PID 13804, exact child start and executable, reason `Managed child exited.`, owner liveness `Verified` | Coordinator-confirmed exact reservation and child |
| Coordinator event log | Reservation requested and child registered for this exact ID/PID; no dedicated release journal event retained | Coordinator journal evidence; release event itself absent |
| Final census | Handoff stated zero sessions, queue requests, foreign QEMU processes, and listener on port 60212 | Handoff evidence only; no final census artifact was retained or recaptured |

The independent inspect snapshot proves release for the exact reservation and child. The run can therefore be classified **QEMU execution completed and QAC release verified** for infrastructure. The original runner record remains historically `RecoveryRequired`; neither it nor the CLI trace was rewritten. No missing CLI line or journal event is fabricated. The zero-count final census is not promoted to independently archived evidence.

### Reconstructed order

1. Preflight observed QAC Online v2 and an empty session/queue census.
2. QAC requested and granted reservation `5555811b6f1b438e84f7d2f2648ea24c` at approximately `20:02:30.374Z`; the direct child started at `20:02:30.413235Z`.
3. The runner verified the listener and QEMU identity on port 60212. Guest progress appeared at 20:04:06Z.
4. Guest evaluation failed at `2026-10-25T13:08:26.534-07:00` (`20:08:26.534Z`).
5. QMP `quit` was acknowledged at 20:08:41.4054053Z; QEMU exited with code 0 at 20:08:41.4713205Z.
6. The CLI exited with code 0 but emitted no verified-release line. The exact QAC inspect snapshot captured at 20:08:42.0361615Z reported the reservation Released for PID 13804.
7. The local runner finalized as `RecoveryRequired` because its CLI release-line requirement had not been met. This is the runner’s historical status, not evidence that the coordinator still held the reservation.

## Retained evidence file hashes

SHA-256 values were computed from the original retained files. Paths below are relative to the source run directory.

| File | SHA-256 |
| --- | --- |
| `.qac3c2-owner.json` | `C52D3CBC89A64EB500F40A06F94637CCBC421A75148AC5D493A899A74EA230AD` |
| `.complete` | `C355EC65AB57B51CE04B066FD91EC08272861200866013BFE61E9DADB531C6D8` |
| `manifest.json` | `7EB35446F851376B55F848A289D222549499694B9517C8404C3785C3CFE9A389` |
| `output/execution.json` | `1DB831747DD11F244CF458084012A46EFCFB35829DAD4FBFABFAB319086CF693` |
| `output/host-execution.jsonl` | `A48BBF3C01EC4C1F406E20A69A1D523F1FDAD995F35C16BBC3EFFA406F9260D9` |
| `output/qac-inspect-5555811b6f1b438e84f7d2f2648ea24c.json` | `1ACBBE56988A3C6ACCEAF8EEE2525F342556531594BBDDC8605088B628142687` |
| `output/qac-run.log` | `9D5F917DF57FFFD197AD8B05A50DEF32BE931BFE30182174107F164A23824667` |
| `output/qmp-transcript.jsonl` | `67C67F98458705520DFCE35EAFF30253C61028D89CD921E0D7C0015A118208D9` |
| `output/serial.log` | `5BD934673C0AF8254B45DFB68BD4C6A5D21FAB1986FC3C91FD67DA59EFBDE77D` |
| `output/qemu-debug.log` | `6E394BA39FEE409484C93AB0C0CCEBE6D5A63764A65297A0C240F3DBEF0E0425` |

## Output drain and regression evidence

The runner drains stdout and stderr concurrently while QAC runs, waits for the managed process to exit, and then performs a bounded post-exit read through EOF before processing remaining output and deciding release status. The validated drain bound is 1–30 seconds, default 5 seconds. A CLI release line must identify the exact reservation and child PID. The final coordinator inspection must match exact ID, project, mode, `Released` state, and child PID. Missing or mismatched proof retains the existing `RecoveryRequired` fallback. Guest result and integration lifecycle remain separate.

This implementation correction was made after R2 and does not alter the recorded R2 lifecycle. Completed regression results: focused QAC3C3 **85/85**, QAC3C2 isolation **36/36**, server launcher contract **80/80**, and five PowerShell files parsed. A later focused rerun after removal of an experimental pipe-capacity fixture emitted no output and was stopped after more than two minutes; the 85/85 result is from the completed run before that fixture was added. The fixture was removed, and no result is claimed for its stress test. The historical 98-check QAC core suite was not rerun because QAC core source is unchanged.

## Private ESP review

- Canonical ESP: `D:\dev\guideXOSServer\out\release-profile\95cf3e2d-778b-4b31-8948-4acdb7cc6aaa\ESP`; 16 files, 71,435,180 bytes; identity `5ffa5777faf76e10633bae441e61e81989d45ffb660aff42feff49674060efe7`.
- Run-private ESP: `<source run>\artifacts\esp`; 17 files, 71,445,421 bytes; identity `de162001d15517a04b8b70f3cfdac92c8a12cfa16b91c8a6e08490e5e07e26d8`.
- Added: `NvVars`, 10,241 bytes, SHA-256 `ba953f322c467d793e2a35feff699fb4f1c4de04e70427d745cdd4635e1f97c5`; expected private UEFI variable-state output.
- Removed: none.
- Content modified: none; hashes for all 16 canonical files match the private copies.
- Timestamp-only private changes: `build-identity.txt`, `kernel-symbol-audit.txt`, `kernel.elf`, and `Apps/PacMan/resources/audio/startmusicold.wav`. Their hashes, byte counts, and Archive attributes are unchanged. The cause of these four timestamp updates is unknown. No shared canonical write is shown; the updates are within the private ESP.

Canonical ISO identity remains 90,245,120 bytes / `e98bf087efc071ddf82db4dce9d79335aacbf38310de43b032744165792d712b`. Canonical OVMF identity remains 3,653,632 bytes / `2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d34733393ab2792`. QEMU mounted only the private ESP writable (`fat:rw`); ISO and OVMF were read-only. No source artifact was modified.

## Screenshot validation

The four screenshots are in the private run’s `output/screenshots/` directory. Each is P6 PPM, 1280×800, max value 255, 3,072,016 bytes total, with all 3,072,000 RGB bytes present. Each current-run path appears in the execution screenshot events and QMP `screendump` records. Original PPMs were not modified. Derived PNG inspection copies were kept outside the source run.

| Screenshot | SHA-256 | What it shows |
| --- | --- | --- |
| `appmodel-selected-live.ppm` | `AD08EEF19AE09C2E1577C99C0CDA1051DAE13416C06F42864270C7304B1418FB` | Guest desktop with Start menu open; Calculator is highlighted, not AppModel or PacMan |
| `desktop-pacman-state.ppm` | `E5554B43779D420512A421A85C31CBA54F9973FCF1FC21BEC018FC0D74454BFA` | Guest desktop state |
| `start-menu-after-ctrl-esc.ppm` | `AD08EEF19AE09C2E1577C99C0CDA1051DAE13416C06F42864270C7304B1418FB` | Byte-identical to `appmodel-selected-live.ppm`; Start menu is open |
| `start-menu-live.ppm` | `B3A7739FEA3E5BEECB677516453A77D41158B10D2DCB2F9823A6783059257AEE` | Guest desktop with Start menu open |

The images are useful guest-progress evidence: the desktop rendered and the Start menu opened. The filename `appmodel-selected-live.ppm` does not prove an AppModel selection because the pixels match a Start-menu capture with Calculator highlighted.

## Guest and dashboard results

The serial log initially reports `App Model package discovered package=PacMan id=com.guidexos.pacman displayName=Nexgen PacMan` and `discovery result packages=0x00000001`. Later checks report `NO_APPS_DIRECTORY`. No `[PACMAN-SMOKE] PASS`, successful launch marker, or gameplay marker exists. Guest qualification is **FAIL**: PacMan discovery did not remain available; gameplay acceptance is not established. QEMU exit code 0 is infrastructure evidence only.

Dashboard minimize/restore was observed during the active acceptance session. The WPF post-release completion/empty view was not observed. Bounded read-only inspection of the available WPF UI timed out; its prescribed reset and retry also timed out, and no UI input was sent. Exact QAC release is independently verified by the inspect snapshot, but the mandatory WPF completion-view gate remains unverified. This is the reason for Outcome B. QAC `.phase` remains QAC3 open with `Next: QAC3C3`; QAC3C4 must not begin.
