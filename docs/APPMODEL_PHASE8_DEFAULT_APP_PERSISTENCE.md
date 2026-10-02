# App Model Phase 8 — Machine-Global Default-App Persistence

Status: **Outcome A — durable machine-global default-app overrides are implemented and validated.**

## Ownership audit and decision

guideXOS does not yet have a human account, profile, or general-purpose machine-configuration service. Phase 8 therefore defines defaults as **machine-global guideXOS policy**. The hosted Windows username and profile are storage environment details only; they are not guideXOS identity.

The existing owners were reviewed before adding the store:

- `desktop.json` is owned by `DesktopConfig` and `DesktopService`. It mixes wallpaper, display and desktop behavior, accessibility, window state, pinned apps, recent programs, and shortcuts. `DesktopConfig::Save` serializes its known fields back over the whole file and does not preserve arbitrary unknown JSON fields. It is not a safe narrow owner for this policy, and its current worktree edit was preserved.
- `display-options.cfg` is owned by `DisplayOptionsStore` and the display configuration path. It stores display, wallpaper, theme, taskbar, clock, and related desktop options; it is not an App Model policy service.
- `BackgroundStore` owns wallpaper records and image files under `/user-data/backgrounds/`; wallpaper identity and content are not default-handler policy.
- `WindowBoundsStore` owns bounded normal-window geometry in `window-bounds.cfg`; it has no application-association role.
- App manifests under `Apps/**/app.json` declare application identity and capabilities. They are registration input, not mutable default policy.
- Pinned and recent program records in `desktop.json` are shell history and shortcuts. Their App IDs remain untouched and are not interpreted as defaults.
- Shell/startup state, accessibility settings, clock settings, Settings models, and network configuration each have narrower owners or contracts. None is a general machine-global App Model configuration owner.

**AppRegistry is the single authoritative owner of default-handler policy.** Its one dedicated bounded store is `appmodel-default-handlers.cfg` in the hosted guideXOS process working directory. AppRegistry owns load, validation, mutation, query, and effective resolution. `DesktopService` exposes the same registry APIs to hosted callers. File Explorer and future Settings code do not maintain copies or parse the file.

This boundary keeps application capability in the registry and user-choice policy beside the resolver that validates it. The persistence file is machine-global to this guideXOS hosted instance; it is not placed in a Windows user profile. Bare-metal App Model code does not read or write it.

## Persisted format and bounds

The version 1 text format stores only a normalized extension and its canonical App Model ID:

```text
GXOS-APP-DEFAULTS 1
.txt=gxos.builtin.notepad
END 1 <eight-digit FNV-1a checksum>
```

The checksum covers the exact record body and the footer records its line count. It detects truncated or accidentally modified content; it is not a cryptographic authenticity mechanism. Records are serialized in extension order. Display labels, manifest locations, host executable paths, registration slots, pointers, and temporary owner/generation values are not stored.

| Bound | Limit |
| --- | ---: |
| Override records | 128 |
| Normalized extension | 32 bytes, including the dot |
| Canonical App Model ID | 128 bytes |
| Entire configuration | 32 KiB |

The extension parser is the Phase 6/7 AppRegistry normalizer: a leading dot, ASCII letters/digits/underscore/hyphen, ASCII case-folded to lowercase. Path suffix resolution still uses the existing final-basename rules. Equivalent `.txt`, `.TXT`, and `.Txt` inputs address one key.

Adding a 129th record fails without eviction. A record or serialized file over its bound is rejected. Duplicate keys in a syntactically intact file are all ignored; a malformed individual line is ignored and counted while other valid records survive. Missing/invalid headers, bad checksums, truncated footers, or excess file/record capacity invalidate the whole store and produce no configured handlers. Reload does not rewrite corrupt input. Set and clear fail if the authoritative store cannot be reread. No unknown line has policy meaning in version 1; unknown lines are counted as invalid records and are omitted by a later successful version 1 write. An unsupported version header remains invalid and is not silently rewritten.

## API and resolution contract

`AppRegistry` exposes:

- `SetDefaultHandler(extension, canonicalAppId)`
- `ClearDefaultHandler(extension)`
- `GetDefaultHandlerInfo(extension)`
- `GetKnownDocumentExtensions()`
- `GetDefaultHandlerStoreDiagnostics()` and `ReloadDefaultHandlerConfiguration(...)`

The value returned by `GetDefaultHandlerInfo` keeps three identities separate: `builtInDefaultAppId`, `configuredOverrideAppId`, and `effectiveDefaultAppId`. `configuredStatus` reports whether the stored choice is currently available, missing, incapable, unsupported, non-durable, capacity-limited, or temporarily unavailable. `effectiveDefaultAvailable` tells the future Settings UI whether ordinary Open can dispatch the effective selection now. Known extensions are sorted and bounded by the retained 256 association records, 128 persisted keys, and the four built-in text extensions.

Set validates the normalized extension, exact registered canonical ID, current declared capability, document-activation support, durable registration identity, current backend availability, and presence in the bounded association index. Unknown IDs, incapable apps, unavailable handlers, and Development Run/test registrations are rejected with distinct statuses. No display-name lookup is used. Persistent overrides do not carry registry owner/generation because canonical App ID is the durable identity; temporary registrations are rejected.

Ordinary Open resolves the current extension, then uses a configured override only when that exact ID is still registered, declares the extension, supports document activation, is a durable registration, remains in the current bounded index, and is available on the current backend. It then performs the existing owned-path activation validation again before dispatch. Otherwise the override remains stored but is ignored for that resolution. Resolution falls back to the valid built-in default, then the existing deterministic available-handler fallback; if there is no usable handler, Open fails closed. No stale ID is mapped by display label or registration slot.

This preserves preferences across temporary backend unavailability without discarding them. A missing registration, lost capability, unsupported document activation, malformed record, or reused registry position cannot redirect ordinary Open. A different app with the same display label has no effect. Open With still enumerates current capabilities independently; choosing another row activates that exact canonical ID once and never writes the default.

Clearing an extension removes its override. The built-in Notepad declarations remain the defaults for `.txt`, `.log`, `.ini`, and `.cfg`; clearing returns to those declarations without storing a redundant Notepad record.

## Write and reload behavior

Mutation builds a complete bounded replacement in a sibling `.tmp` file. The writer flushes the file before replacement. On Windows it uses `MoveFileEx` with replace-existing and write-through flags; on POSIX it uses `fsync`, atomic `rename`, and a containing-directory flush attempt. It rereads and parses the temporary contents before replacement, atomically replaces the target, then rereads and verifies the final file and effective ID. The in-memory owner changes only after verification. A failed temporary write or replace leaves the old file and in-memory records intact.

The AppRegistry constructor loads the durable file; callers can explicitly reload. A valid but malformed individual line produces a partial-load diagnostic and the valid records remain queryable. An invalid whole file remains on disk, is ignored for resolution, and is not rewritten merely because its handlers cannot be used. A later Settings UI must surface mutation failures instead of claiming an unverified change succeeded.

The production default file is not modified by the test suite. All persistence tests use unique temporary paths. Unrelated `desktop.json` bytes are checked before and after 100 store-recreation cycles.

## Validation

The focused store/resolution harness reports **44/44 checks passed**, including format and corruption handling, 128-record capacity, failed write and failed atomic replacement preservation, validation statuses, duplicate display labels, stale IDs, lost capability, temporary unavailability, temporary-registration rejection, clearing, and production Notepad defaults. It performs **100 owner recreation cycles**; ordinary Open resolved and delivered canonical synthetic handler B in 100/100, explicit one-time Open With delivered Notepad A in 100/100, and the configured effective default remained B after each choice.

The B handler is an isolated test-only durable registration with an activation sink compiled under `GXOS_APPMODEL_TESTING`. It does not enter production registration or add a second production app. Production Notepad and File Explorer behavior was separately exercised by the Phase 6 and Phase 7 runtime smokes.

Additional regression results:

- Phase 7 capable-handler/Open With model: **16/16**, including 100 menu lifecycle cycles; production File Explorer runtime: **20/20** explicit activations and **4/4** assertions.
- Phase 6 document activation: File Explorer **7/7**, Notepad **29/29**, negative cases **3/3**.
- Phase 5B: **PASS**, `appModelV1Status=ready`, `unresolved=0`, `highRisk=0`.
- Startup App Model: **PASS**; Recent Programs: **PASS**.
- Settings Center **89/89**; Apps inventory Device **21/21** and Storage **24/24**; S6 **42/42**; S7 accessibility **21/21**, developer **23/23**, navigation/search **14/14**; Users model **11/11**, navigation **8/8**, interaction/layout **13/13**.
- Hosted `build.bat`: passed. The full build emitted existing warnings in unrelated allocator, UI/configuration, rendering, and application sources; the Phase 8 store and registry code emitted no compiler warnings.
- QEMU was skipped: the store, registry changes, and normal Open integration are hosted-only. No kernel or bare-metal configuration path changed. Physical hardware was not required.

Runtime tests restored `desktop.json`, `desktop.state`, `display-options.cfg`, and `window-bounds.cfg` byte-for-byte. `appmodel-default-handlers.cfg` remained absent in the production working directory. The developer's pre-existing `desktop.json` edit and startup-regression directories were not staged or committed.

## Future Settings integration and limits

Settings → Apps → Default apps can use the bounded AppRegistry/`DesktopService` query and mutation APIs. It can enumerate extensions, read the built-in/configured/effective distinction and status, request capable handlers through the existing enumeration API, call set/clear, and display returned result codes. It must not parse this file or add another preference owner.

The production hosted document dispatcher currently has one real handler: Notepad. This phase does not add a Settings UI, fake production handler, user/profile model, package manager, MIME database, content sniffing, arbitrary executable selection, or bare-metal persistence. If guideXOS later adds real account/profile identity, default ownership semantics will need a deliberate migration; Phase 8 defines only the current machine-global contract.
