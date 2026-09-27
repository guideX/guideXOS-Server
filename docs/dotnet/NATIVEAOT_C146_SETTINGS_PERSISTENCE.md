# NativeAOT C146 — Persistent Managed Settings Center

## Objective and state model

C146 keeps edits in the Settings Center's in-memory `working` snapshot. `applied` is the accepted configuration for the running session, and `persisted` is the last snapshot that completed a VFS write and a matching read-back. Apply snapshots `working`, validates and serializes it, writes and verifies the record, then updates `applied` and `persisted`. A failed save leaves the Settings Center open, preserves `working`, `applied`, and `persisted`, keeps the dirty state, and opens the existing modal MessageBox.

Reset confirmation changes only `working`. Apply is still required to persist defaults. Dirty-close Apply closes only after successful persistence; Discard closes without writing; Cancel keeps the Settings Center open. Apply performs no write when `working == applied == persisted`.

The only serialized state is the nine semantic C144/C145 settings fields. Focus, popup and modal state, pointer capture, ScrollBar drag ownership, viewport offset, and dirty state are not stored. A new launch starts at viewport offset zero and uses the normal focus policy. Programmatic control hydration runs under the existing `_syncing` callback guard.

## VFS audit and path

The existing managed host bridge remains ABI v1 with a 104-byte table. Its file operations are whole-file `fileReadAll`, `fileWriteAll`, and `fileStat`; managed settings use `GuideXosFile.TryGetInfo`, `ReadAllTextUtf8` as a bounded byte-buffer operation, and `WriteAllTextUtf8` as a bounded byte-buffer operation. The names reflect the existing API; these calls do not introduce a text encoding or serializer for the settings bytes. Existing application capabilities include FileRead, FileWrite, and FileStat.

The managed app path policy admits `/system/apps/` paths. The boot wallpaper pack is loaded into RAM and mounted at `/system`, so writing there would not survive reboot. QEMU's writable FAT ESP is mounted at `/`. C146 keeps the managed path `/system/apps/GXSETT.BIN` and adds exact-path routing in the native bridge to `/GXSETT.BIN` on that writable FAT mount. All other paths retain their existing mapping and checks. The narrow route does not expand the App Model capability set or host ABI.

The bridge has no managed file handles, flush operation, rename/replace, delete, or directory-creation callback. VFS `write_file` creates or truncates and synchronously writes sectors through the existing FAT implementation. C146 therefore uses a bounded direct overwrite and does not claim atomic replacement or guaranteed recovery from a torn write. A successful call is followed by stat, full read-back, parser validation, and exact snapshot comparison before the app reports success.

## File format

The file is exactly 25 bytes in format version 1. Integer fields are explicitly little-endian; no CLR or native struct layout is written.

| Offset | Size | Field | Version 1 rule |
|---:|---:|---|---|
| 0 | 4 | Magic | ASCII `GXSC` |
| 4 | 2 | Format version | `1` (`u16`) |
| 6 | 2 | Payload length | `9` (`u16`) |
| 8 | 4 | Flags | `0` (`u32`, reserved) |
| 12 | 1 | Density | `0..1` |
| 13 | 1 | ShowStatus | Boolean `0` or `1` |
| 14 | 1 | ShowAdvanced | Boolean `0` or `1` |
| 15 | 1 | InputEnabled | Boolean `0` or `1` |
| 16 | 1 | NaturalScroll | Boolean `0` or `1` |
| 17 | 1 | ScrollSpeed | `0..2` |
| 18 | 1 | ShowKeyboardTips | Boolean `0` or `1` |
| 19 | 1 | StatusDetail | `0..1` |
| 20 | 1 | ReportFormat | `0..1` |
| 21 | 4 | Checksum | CRC-32/IEEE over bytes `0..20`, stored little-endian |

The store has a hard 64-byte read bound. Version 1 accepts only the exact 25-byte record; it rejects bad magic, unknown versions, nonzero flags, wrong payload length, truncated or trailing data, invalid booleans/enums, oversized files, and checksum mismatches. Parsing fills a temporary snapshot and returns it only after every check succeeds. No migration framework is included.

The checksum is the standard reflected CRC-32/IEEE polynomial `0xEDB88320`, initialized to `0xFFFFFFFF` and complemented at the end. It detects accidental corruption and is not an authentication mechanism.

## Defaults, load recovery, and errors

One `ManagedSettingsSnapshot.Defaults` value supplies UI defaults, Reset, and recovery. A missing file loads defaults into `working`, `applied`, and `persisted`, leaves dirty false, and does not create a file. A malformed record or read error also loads defaults and presents a bounded warning MessageBox; it does not crash or partially update the live settings. If a modal is active, the warning is deferred until the modal closes.

Save failure does not advance `applied` or `persisted`. The existing one-modal-at-a-time `GuideXosDialog` path presents the error MessageBox and retains edits. The save-in-progress guards reject reentrant save attempts, and the store clears its guard in `finally` on all result paths.

## Proof media and validation

The C146 proof runner stages a separate writable FAT directory for each of three independent sequences. Each sequence boots with the same directory-backed ESP for Boot A (physical edits and Apply), Boot B (load, edit, dirty-close Discard), and Boot C (fresh load proving Discard left the prior record unchanged). Sequence 1 adds Boot D (physical Reset confirmation and Apply) and Boot E (fresh load of defaults). The protected repository `ESP/ramdisk.img` is not used as disposable test media.

For every sequence, the runner records the starting and post-write test-media directory hashes, settings-file size and SHA-256, serial hashes, written and loaded snapshots, and read-back markers. It verifies the `GXSC` framing, version, fixed size, values, and the managed app's CRC-validated read-back marker. The production interactions use QEMU physical pointer input through the existing path. A controlled failure injection runs in the Settings Center integration fixture and verifies modal display, retained dirty edits, unchanged prior applied/persisted state, dismissal, and successful retry.

The runner saves byte-for-byte copies of the starting ordinary kernel files before the proof build. After persistence runs it restores `kernel/build/amd64/bin/kernel.elf` and `ESP/kernel.elf`, verifies both hashes, verifies the protected `ESP/ramdisk.img` hash, and runs three fresh ordinary QEMU boots. The test-media directories and serial captures stay under the C146 evidence directory.

## Tests and regressions

The format suite covers defaults and non-default snapshots, round-trip, magic/version/header/payload/length/size/boolean/enum/trailing/checksum failures, maximum bound, and deterministic output. The store suite covers missing, save/load/overwrite, read-back, corrupt fallback, injected write/read failures, fake handle cleanup, 50 repeated saves, and reentrancy. The Settings Center suite covers defaults, edit/apply state relationships, Reset-only and Reset+Apply, dirty-close Discard/Cancel, failed Apply with MessageBox, dismissal and retry, no-op Apply, viewport/focus policy, registration, capture, and drag cleanup.

The C146 composite runs the C146 store and Settings Center suites on each managed proof boot. Its Settings Center cases exercise reset confirmation, dirty-close Apply/Discard/Cancel, failed Apply with modal recovery, retry, and no-op Apply. The C145 Dialog suite, C144's standalone 56-case suite, and lower historical phases are referenced as prior evidence rather than claimed as new reruns unless listed in the C146 manifest. ABI v1/table size 104, application ownership of controls, one modal scope, one capture owner, and the existing focus model are retained.

Host log records are limited to 127 bytes. Snapshot proof output therefore writes one bounded state line followed by a `C146-VALUES` line with all nine applied field values; the QEMU runner validates both records together.

See the generated `c146.manifest.json` and `ordinary.manifest.json` under `out/dotnet/c146-settings-persistence` for the measured build hashes, boot outcomes, serial hashes, test-media hashes, and snapshot values.

## Deferred work and limitation

C146 does not add a registry, database, daemon, profiles, encryption, cloud sync, JSON/XML serializer, generic configuration framework, migration system, transaction layer, journaling, atomic replacement, or backup UI. The direct overwrite and lack of an explicit flush/replace callback remain the bounded VFS durability limitation; successful persistence is evidenced across actual fresh boots, while torn-write recovery is not guaranteed.
