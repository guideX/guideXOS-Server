# Server v0.3 — S22R23 AppData Guest Qualification

**Status:** pending candidate build and two QAC cold boots. No guest persistence claim is made yet.

## Qualification contract

The implementation under qualification is the local S22R23 implementation commit on `main`, built with `ServerV03PublicPreviewPacManOnly`. Candidate and kernel identities, source inputs, tool versions, embedded ESP checks, and exact hashes will be recorded here after the isolated candidate build.

Use a dedicated writable copy of the accepted S22R22 data image. Preserve the accepted original image and accepted S22R22 ISO. The QAC runner is the S22R22R1 ownership-checked launcher with the guest event/shutdown handling regression suite. Verify QAC health, zero active reservations/sessions, and no unrelated QEMU before launch. Each boot uses a new run ID and a separate artifact workspace, while both reservations reuse the exact same data image.

## Boot 1 — save and same-boot reopen

Record QAC reservation/session identifiers and verify the serial mount identity reports the expected `GXOS-DATA-V1` device, `/Documents`, and `/AppData`. In Modern Settings, inspect the initial wallpaper, choose a different existing wallpaper/gradient, Apply, and verify both the changed desktop and Settings success state. Close and reopen Settings and confirm the selected value. Run the PacMan smoke flow and capture its guest result marker. Verify the preexisting Notepad document remains readable and unchanged. Shut down from the guest UI; record normal guest shutdown evidence, QAC child exit, and reservation release.

## Boot 2 — cold-boot restore

Reuse the stopped image and record its exact pre-launch SHA-256. Start a second independent QAC reservation with the same ISO, firmware, and data image. Verify the mount identity again. Confirm from serial diagnostics and the desktop that the first boot's wallpaper/gradient is restored before interacting with Settings. Open Settings and confirm the actual selected value. Recheck the existing Notepad document, verify `/Apps` is trusted/read-only, and run PacMan. If appropriate, apply and verify a second preference so the store is exercised again. Shut down normally and confirm QAC session completion and ownership release.

## Offline stopped-image inspection

After QAC has released the second session, inspect the dedicated image read-only. Record `/APPDATA/DESKAPP.CFG` bytes, format/version, wallpaper ID, icon values, and its file hash. Verify expected Notepad document paths, lengths, and hashes against the accepted baseline. Recompute the writable image's hash and size. Record QAC final status/reservation/session counts and confirm the accepted original image hash did not change.

## Results

| Evidence | Result |
|---|---|
| Candidate ISO / embedded ESP / kernel identity | Pending |
| PacMan-only package and Native ELF integrity policy | Pending candidate inspection |
| QAC health and preflight ownership state | Pending |
| Boot 1 setting Apply and close/reopen | Pending |
| Boot 1 PacMan and normal shutdown | Pending |
| Boot 2 cold-boot restored desktop behavior | Pending |
| Boot 2 `/Apps` trust and PacMan | Pending |
| Existing Notepad document integrity | Pending |
| Offline AppData and document inspection | Pending |
| QAC final coordinator and ownership state | Pending |
| Accepted S22R22 source artifacts unchanged | Pending |

No QMP acknowledgement alone counts as proof of application behavior. If guest execution fails, record the first failing operation and retain runner, serial, QAC, and screenshot evidence; do not repeat an identical attempt without a specific diagnosed change.

## Outcome

Pending. `APPDATA_PERSISTENCE_GUEST_ACCEPTED` requires the persisted preference to restore and affect the application on the second independent cold boot while existing documents and the read-only Native ELF boundary remain intact. If hosted and freestanding checks pass but guest qualification cannot complete, report `APPDATA_PERSISTENCE_PARTIAL` and identify the exact unresolved workflow. Use `APPDATA_IMPLEMENTATION_BLOCKER` only for a demonstrated application-lifecycle, filesystem, storage-integrity, or execution-security blocker.
