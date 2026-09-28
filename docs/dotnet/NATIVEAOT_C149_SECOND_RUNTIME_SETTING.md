# NativeAOT C149: second shared runtime setting

## Outcome and selected field

C149 promotes the existing `ShowKeyboardTips` field into shared runtime state. The field already belongs to the v2 settings schema and already has a Settings Center checkbox. Managed Notes is the existing production consumer: it renders a fixed keyboard-navigation instruction when the shared value is `true`, and clears that exact row when the value is `false`. This gives a deterministic consumer metric: `true` maps to the instruction text being rendered; `false` maps to an empty row.

The selection avoids inventing a new setting or format version, and it uses an existing managed UI surface without changing the native/managed ABI.

## Persisted v2 field audit

The v2 record remains a 12-byte header, 10-byte payload, and 4-byte CRC (26 bytes total). Header fields retain the `GXSC` magic, version `2`, payload length `10`, and zero flags. The payload order is:

| Payload byte | Field | C149 classification |
| ---: | --- | --- |
| 0 | `Density` | Settings-Center/application-only |
| 1 | `ShowStatus` | Settings-Center/application-only |
| 2 | `ShowAdvanced` | Settings-Center/application-only |
| 3 | `InputEnabled` | Settings-Center/application-only |
| 4 | `NaturalScroll` | Shared runtime-backed |
| 5 | `ScrollSpeed` | Settings-Center/application-only |
| 6 | `ShowKeyboardTips` | Shared runtime-backed (C149) |
| 7 | `StatusDetail` | Settings-Center/application-only |
| 8 | `ReportFormat` | Settings-Center/application-only |
| 9 | `ScrollLinesPerNotch` | Shared runtime-backed (C148) |

There are ten persisted semantic fields, three runtime-backed fields, and seven fields that remain local to the Settings Center/application's own presentation, control gating, or report output. None is treated as transient runtime state by the shared runtime snapshot. C149 does not give those seven fields new subsystem semantics.

`ShowKeyboardTips` is payload byte 6, absolute record byte 18. `true` is `1`; `false` is `0`. The v2 format, byte meanings, record size, checksum, and canonical default (`true`) are unchanged.

## Runtime lifecycle and ownership

`GuideXosRuntimeSettingsSnapshot` now includes `ShowKeyboardTips` alongside `NaturalScroll` and `ScrollLinesPerNotch`. `GuideXosRuntimeSettingsState.TryCommit` validates a complete settings candidate and assigns one immutable snapshot. The shared managed runtime settings owner is authoritative; Settings Center is only the editor and persistence client.

At startup the existing sequence loads and validates `GXSETT.BIN`, applies the canonical default snapshot on missing/invalid input, and publishes the selected snapshot before the first managed application dispatch. Managed Notes reads `GuideXosRuntimeSettings.Current.ShowKeyboardTips` during its normal render, before Settings Center is launched. The feature uses no second loader, mutable public setting, registry, settings daemon, new ownership/focus/capture model, or ABI expansion.

The existing Settings Center `Show keyboard tips` CheckBox remains the control. Its working value can change without changing runtime or disk. Reset changes only the working snapshot. Successful Apply first saves and parses exact read-back, then publishes the full runtime snapshot. A newly launched Managed Notes instance reads and paints that snapshot. A reported persistence failure leaves the runtime, applied snapshot, and persisted snapshot unchanged while the working edit remains dirty and the existing persistence-error dialog/retry path remains in control. Cancel retains its working edit while open; Discard abandons it on close. The shared runtime snapshot remains committed when Settings Center closes.

`NaturalScroll` remains solely responsible for wheel direction and `ScrollLinesPerNotch` for wheel magnitude. C149 does not change either behavior.

### Apply repaint boundary (Outcome D)

Apply publishes the new value immediately after verified persistence. The NativeAOT native adapter owns one window at a time: opening Settings Center calls `requestClose()` on the existing Notes window before creating the Settings Center window. The physical QEMU serial showed Notes window `0x3E8` close, followed by Settings Center window `0x3E9` creation. Closing Settings Center closes `0x3E9` and does not recreate Notes, so there is no running Notes surface to refresh or dispatch into after close.

Even while both app objects are resident, native draw callbacks require `context == g_activeManagedContext`; the Settings Center context is active during Apply. `GuideXosHost` is a static dispatch facade rebound for each app callback, and the v1 ABI has no cross-application invalidation, repaint, or application-relaunch service. C149 does not retain a native context pointer, bypass the owner check, or expand ABI v1. The persisted/runtime value is proven on fresh Notes launch before Settings Center opens. Updating the same live Notes instance across the Settings Center lifetime would require native multi-surface ownership or a bounded cross-application repaint/relaunch service. C149 records that as Outcome D and does not claim same-instance immediate Apply or post-close retention.

## Compatibility and durability

- Authentic v1 settings remain readable and are not rewritten during boot. The v1 format does not contain `ScrollLinesPerNotch`; its canonical default remains `3`. `ShowKeyboardTips` was already present in v1 and retains its stored value.
- A captured C148-era v2 record with SHA-256 `006409696A838F8E27AEC444BB23B51A4603687961CD3A38E5ECA2DF9B46CBB9` loads with all prior values unchanged, including `ShowKeyboardTips=true` and `ScrollLinesPerNotch=7`; load performs no write.
- Invalid/corrupt input and unsupported version 3 are rejected by the existing parser and use the unified canonical defaults, including visible keyboard tips. No boot or Settings Center hydration write occurs.
- The v2 record stays 26 bytes and valid v2 boots without Apply leave the file hash unchanged.
- The VFS growth/write path verifies the expanded record and restores the old record on a reported write failure, but atomicity across sudden power loss is not guaranteed.

The authentic v1 fixture used by the proof has SHA-256 `952CA2183EE7DA2923EF76DBC121193C499750BDA7627D862D7F8B8BF5734FF5`.

## Evidence

The C149 NativeAOT proof adds three bounded test groups: field/schema audit, shared runtime lifecycle, and consumer render-policy integration. The expected group sizes are 14 cases each. The physical QEMU matrix covers first-launch consumer behavior before Settings Center, working-state isolation, verified Apply and runtime publication, the Notes-to-Settings-Center single-window replacement boundary, a non-default v2 startup, Reset then Apply, read-only v1, unchanged C148 v2, corrupt input, unsupported v3, and ordinary restored-artifact boots. It does not report an immediate live consumer repaint after Apply. The C146 focused Settings Center suite extends edit isolation, Reset, Cancel, Discard, failed save, retry, and Apply assertions to the new field. C146/C147/C148 regression gates remain in the composite proof.

The proof runner writes its machine-readable C149 evidence manifest and per-boot serial captures under `out/dotnet/c149-second-runtime-setting/`. Those generated proof files are ignored build artifacts and are not part of the source commit. C137's 46-case wheel run is historical evidence referenced by the C148 baseline; it is not represented as rerun by C149. C144/C145 menu and dialog workflows remain integrated in the physical Settings Center scenarios.

Host ABI remains version 1 with a 104-byte table. A fresh Notes launch does not require Settings Center to remain open; the same-window Apply requirement remains blocked by the native surface lifecycle boundary.
