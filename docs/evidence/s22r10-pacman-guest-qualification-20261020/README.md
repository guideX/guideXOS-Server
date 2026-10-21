# S22R10 guest qualification evidence

Qualification report: [../../RELEASE_S22R10_PACMAN_GUEST_QUALIFICATION.md](../../RELEASE_S22R10_PACMAN_GUEST_QUALIFICATION.md)

Evidence captured on 2026-10-20 Pacific time against the existing candidate ISO. The ISO SHA-256 is `e98bf087efc071ddf82db4dce9d79335aacbf38310de43b032744165792d712b`.

## Root files

- `repository-preflight.json` — initial branch, HEAD, tracked cleanliness, and pre-existing untracked count.
- `candidate-integrity.json` — candidate identity, hash, profile, and Native ELF composition.
- `final-integrity.json` — final ISO digest, unchanged source ESP checks, clean tracked state, and no remaining QEMU process.
- `qac-preflight.json` and `qac-status.txt` — historical initial QAC snapshots; consult the timeline for all admission and release events.
- `qac-ownership-timeline.md` — QAC reservation IDs, QEMU process IDs, exit results, and final empty status.
- `qmp-client.py` and `qmp-quit.py` — small evidence-collection helpers used to issue QMP commands and end only the reservation-owned guest.
- `ppm-to-png.py` — converts captured PPM screendumps to PNG for review.
- `qac-launch.py` — starts the third guest through QAC’s acceptance CLI.

## Per-boot evidence

| Directory | Contents |
|---|---|
| `guest-boot1` | Exact ISO only; serial/QEMU/QAC/QMP logs, launch arguments, desktop, Start, Settings, and Navigator screenshots. No writable storage was attached. |
| `guest-boot2-storage-backed` | Exact ISO plus the first disposable FAT copy of the candidate ESP; serial/QEMU/QAC/QMP logs, launch arguments, the candidate ESP inventory, Start menu without PacMan, and the F12 attempt screenshot. |
| `guest-boot3-fresh-storage` | Exact ISO plus a fresh second ESP copy; serial/QEMU/QAC/QMP logs, launch arguments, inventory, Start menu without PacMan, Navigator open, and desktop after Navigator close. |

The `writable-esp-before.json` files record the source and pre-boot file inventory with SHA-256 digests. The copied guest ESP directories and raw `.ppm` screendumps are retained on disk but are not included in the documentation/evidence commit. Selected PNGs, reports, launch records, and logs are committed.

## Result at a glance

- Three cold boots reached the desktop.
- Settings and Navigator built-in app smoke passed for the observed paths.
- PacMan was discovered during startup on both storage-backed boots, then repeated `NO_APPS_DIRECTORY` responses caused it to disappear from Start.
- PacMan did not launch; gameplay acceptance remains blocked.
- All three QAC-managed QEMU processes exited 0 through QMP and their reservations were released.
