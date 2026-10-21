# REXX RX21 — Settings Integration Audit

**Decision: no production RTC policy integration in this phase. Outcome B — Partial.** The current checkout has file persistence and several feature-specific configuration paths, but no supported persistent system-settings interface that connects the Settings owner to the kernel clock service with RTC-specific validation, version handling, boot loading, and safe update semantics.

## Preflight and audit scope

- Required branch: `REXX_SUPPORT`.
- Accepted starting HEAD: `0d8fd1d52c941df962cfa94d0d55b48f560bc895` (RX20).
- Starting index and tracked worktree: clean. The branch was one commit ahead of `origin/REXX_SUPPORT`.
- No `AGENTS.md`, RX21 marker, stale RX21 prompt file, or root `.phase` file was present. The unrelated `desktop.json.phase2.bak` is not a phase marker for this work.
- The 31 root `.obj`/`.pdb` files and 37,042 pre-existing untracked paths were inventoried before changes. Their baseline is recorded in `evidence/rx21/preflight.json`; protected object/PDB hashes are repeated in `evidence/rx21/protected-artifacts-start.json`.
- The RX19, RX19R1, and RX20 reports and the RX20 clock contract were reviewed. No other repository was modified.

## Facilities found in this checkout

| Facility and version | What it actually provides | Why it does not qualify as the RTC policy service |
| --- | --- | --- |
| Settings Center S6 Date & Time route/model | `settings://date-time` and subroutes exist in `settings_center_model.h`. `settings_s6_model.h` reads hosted `std::chrono::system_clock` and formats it using display preferences. `settings_center.cpp` opens Display Options from the advanced action. The S6 report records 42/42 model checks and hosted-runtime exercise, but explicitly says the Settings window was not visually inspected. | It is a hosted clock/display view, not a kernel RTC policy editor or kernel clock snapshot. Its runtime UI acceptance is not claimed here. |
| `DisplayOptionsStore`, schema `version=1` | `display-options.cfg`, maximum 4096 bytes, holds wallpaper/display fields plus display-only `timeZoneId` and `use24HourTime`. Bare-metal code calls `kernel::vfs::read_file` / `write_file`; hosted code uses the hosted filesystem helper. | The zone is Pacific/Mountain/Central/Eastern/UTC for formatting hosted epoch time. The data model defaults to Pacific, the loader recognizes but does not enforce the version value, invalid zone values normalize to Pacific, and writes rewrite the complete file directly. This is not RTC interpretation, fail-closed policy validation, or an atomic policy transaction. |
| Legacy `DesktopConfig`, `desktop.json` | Hosted configuration fallback for desktop preferences, including `desktop.clock.timeZoneId` and `desktop.clock.use24HourTime`; parser limit is 1 MiB. | The bare-metal kernel clock has no reader for this hosted config path. The values remain display preferences and do not describe RTC storage semantics. |
| Kernel VFS and persistent root mount | `kernel::vfs::write_file` writes to a mounted writable filesystem. `kernel_main` initializes VFS and calls `mount_persistent_storage()` at `kernel/core/main.cpp`; this is a storage primitive, not a settings service. | The API provides no schema, RTC validation, transaction, generation/invalidation notification, or policy load ordering. Direct VFS availability alone does not establish a supported configuration contract. |
| Kernel display persistence, format version 2 | `kernel/core/display_configuration_service.cpp` stores `/display.cfg`, validates a bounded payload and checksum, and applies display topology through its own transaction. | It is explicitly display hardware configuration. Its service and persistence format are not a general system-settings store and cannot be repurposed as an RTC policy API. |
| Network settings contract version 1 and settings inventory contract version 1 | Network configuration has its own typed transaction; settings inventory provides read-only application/device/storage snapshots through the COM2 system-service bridge. | These are feature-specific interfaces. The inventory bridge is read-only and neither contract has clock policy fields or a date/time mutation operation. |
| RX20 `kernel::time::ClockReading` contract | Source, read status, raw/local/UTC fields, offset, provenance basis, resolution, accuracy, limitations, and PIT monotonic data are separate. Production CMOS observations use unknown provenance. | It has no settings provider, persistent-state indicator, policy source ID, load lifecycle, or invalidation API. It also has no configured-local-with-offset provenance case. |

## Persistence, validation, and lifecycle findings

The VFS can persist files when a writable persistent filesystem is mounted, and existing feature owners use it. That does not establish end-to-end RTC policy persistence across guest reboot. The Display Options writer uses the feature-specific version-1 file and direct `write_file` replacement; its loader tolerates unknown version text and normalizes invalid time-zone values to the Pacific display default. Missing/corrupt policy data therefore cannot be safely interpreted as an RTC policy through that store.

The kernel display configuration service has stricter version/checksum validation, but it owns only display topology. No current service provides all of the required pieces together: an RTC-specific durable key, validated candidate updates, failure-safe persistence, a kernel-readable effective snapshot, a safe pre-query load lifecycle, persistence/error status, and invalidation when policy changes.

The missing dependency is narrow: establish a versioned, failure-safe system-settings contract with an RTC interpretation record and a kernel consumer interface. It must specify when the validated record becomes available relative to `kernel::time` capability reads, how failed updates preserve the previous effective state, how cache generations are invalidated, and how missing/corrupt/unsupported records fall back to `Unknown`. A Settings editor can connect only after that contract exists in the current app model.

## Integration decision

No new service, file format, kernel policy cache, Settings application, UEFI runtime call, CMOS write, or cross-version App Model dependency was introduced. RX20 remains the production behavior: raw CMOS only, no UTC/local wall capability, and independent PIT monotonic time. QEMU `-rtc` configuration remains test-harness setup only.

The existing Date & Time source/model is documented, but its visual/native availability is not treated as proven from source presence alone. The current S6 report says that page was not visually inspected. No v0.5 App Model/service was imported into this checkout.

## Qualification scope and next phase

RX21's hosted guest checks exercise RX20's current default-Unknown and fail-closed behavior; they do not test policy writes, persistence/reload, invalid persisted records, or policy-change capability invalidation because no production policy path exists. RX19/RX19R1/RX20 accepted runtime evidence remains historical and is not represented as new RX21 policy acceptance.

Keep `DATE` and `TIME` Partial. Do not create RX21R1/RX21R2 clock-only follow-up phases automatically. Recommend RX22 continue with independent classic REXX built-ins and carry this policy contract as a gated future integration item.
