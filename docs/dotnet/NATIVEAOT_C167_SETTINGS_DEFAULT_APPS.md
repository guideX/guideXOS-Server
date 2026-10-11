# NativeAOT C167 — Settings Default Apps

## Status

C167 is accepted with Outcome A. The managed Settings page passed 20 focused host cases and 60 stress iterations; the managed-source scan reports zero direct association-slot references. The real three-boot Settings proof, clean production build, installation checks, and three fresh ordinary boots passed on branch `v1.1_DOTNET_SUPPORT` at source HEAD `e3550ef6f45201d30ce9fdf169195fc4d1d57ae9`.

C128 remains unverified because this work did not add an independent authoritative C128 run.

## Settings integration

`ManagedSettingsCenter` owns the `ManagedDefaultAppsPageC167` page inside the existing Settings application. The page is under **Settings → Apps → Default Apps** and manages only `.txt`.

The bounded page contains a file-type label, current-default label, a two-choice ComboBox, pending/error feedback, Apply, Reset to System Default, and an all-users note. Its controls belong to the existing Settings group, vertical stack, scroll view, and focus host. Re-entry queries the service again and resets pending state; leaving the page does not write an association.

Choices are **Managed Notes** and **No default application**. Native Notepad is not offered.

## Association service and state meanings

All operations use `GuideXosFileAssociations` through the managed host association callback and the accepted Host ABI v4. Native `kernel::appmodel` remains the single association authority, and its FAT dual-slot service owns persistence.

| Service state | Display | Apply or Reset behavior |
| --- | --- | --- |
| `NoOverride` | System default: Managed Notes | Reset restores this state |
| `ApplicationOverride` | Explicit default: Managed Notes | Apply Managed Notes calls SetOverride |
| `Disabled` | Disabled: no default app | Apply No default calls Disable |

Changing the ComboBox only changes pending page state. After a successful mutation, the page queries the service again and updates its committed display only when the returned state matches the requested operation. Mutation or verification failures retain the previous committed display and show an error.

Supported association data remains `.txt` → `com.guidexos.apps.managed.notes`; the C166 compiled default and eligibility checks are unchanged. C167 does not access `/GXAS0.BIN` or `/GXAS1.BIN` from managed Settings and does not put association data into `GXSETT.BIN`.

## Persistence and compatibility

Settings continues to use format v2, 26 bytes. Association persistence remains separate under the C166 version 2 dual-slot format. The managed host ABI remains v4, 128 bytes, with the association callback at offset 120; the association service response remains 328 bytes.

## Host validation

Run `scripts/dotnet/run-c167-default-apps-host-tests.ps1`. The accepted C167 manifest reports 20 focused UI/state cases, 60 stress iterations, and `directSlotReferences=0`. A fresh C166 host run against the same source reports 604 native cases, 34 managed ABI cases, 31 dual-slot cases, 136 partial-write cases, and 1,000 persistence audits.

The C167 fixture covers system, explicit, and disabled displays; pending selection; Apply and Reset service calls; authoritative re-query; persistence failure; unsupported inputs; refresh on page re-entry; close/reopen; keyboard and pointer behavior; bounded focus; popup lifecycle; Settings v2; ABI v4; and managed slot-access exclusion.

## Guest proof and regressions

The real UI proof runner is `scripts/dotnet/run-c166-file-associations.ps1 -UseExistingProofProducts -RunC167SettingsProofBoots`. It starts three fresh QEMU processes using one writable raw FAT16 ESP image. Settings interactions use pointer input and are checked with serial markers, screenshots, and slot-image hashes. PROOF1 is absent from this focused UI kernel.

The guest proof passed this sequence:

1. Boot 1 displayed the system default. Selecting No default left both association slots unchanged until Apply; Apply persisted Disabled, Settings re-entry reread Disabled, and Managed File Explorer reported no handler.
2. Boot 2 reread Disabled from the same image. Apply persisted explicit Managed Notes, and native File Explorer opened the C164 fixture through the association service.
3. Boot 3 reread the explicit override. Reset persisted NoOverride; Managed File Explorer then opened `/system/apps/C164/hello.txt` in Notes with exact text `hello from C164` and a clean baseline (`undo=0`, `redo=0`, caret/anchor/view all zero).

The final resolved state is NoOverride at generation 3, so the effective handler is the compiled Managed Notes default. All three boots used the identical disk path and recorded different before/after disk hashes. Settings stayed at format v2/26 bytes. The association authority is the kernel service via GuideXosHost and the C166 ABI v4/128 callback at offset 120, with a 328-byte response.

Fresh C167 evidence covers C166 service/ABI/persistence host validation, C164 activation, C163 Explorer, C162 identity-safe close, C145 dialogs, C146 Settings persistence, and Settings navigation/lifecycle. C160 snapshot and C156 modifier-routing results are retained from the accepted C166A2 proof manifest; their intrusive startup suites were excluded from the C167 clean ordinary boots. C128 remains unverified.

## Clean production

The clean candidate excluded PROOF1 and proof-control flags. Its composite hash is `3E1ECBB24F4A03F9F5E46898ABF322602270D5B421D112D4FAD6E5F5177222BF`; its kernel hash is `300E0E570956E240732157C00DE9F8C8D08E7C16C865E721AC61B087A36F3876`; and its ramdisk hash is `02C65B0DD3BCA6B7615137D803EF4FCF9E616C5A948E080227535D48F71D2617`. All three artifacts passed the PROOF1 string scan.

The kernel and ramdisk were installed only after the candidate and all three ordinary boots passed. Canonical and ESP kernel hashes match the candidate exactly; the installed ESP ramdisk matches its candidate hash. The ordinary boots verified the system default, Notes activation through Managed and native File Explorer, identity-safe Task Manager close, Calculator `7*8=56`, Settings Apply and Reset, and Settings close/reopen. Final production state has no association override records and resolves `.txt` to Managed Notes.

Evidence is stored under `out/dotnet/c167-default-apps/`, including `c167-acceptance-manifest.json`, `c167-settings-ui-proof-manifest.json`, `host-validation/c167-host-manifest.json`, `host-validation/c166r9-host-manifest.json`, and `production-acceptance/c166a2-manifest.json`. The C167 ordinary lane reused the already accepted C166A2 PROOF1 guest manifest with 26 successful managed mutations; it preserved that accepted evidence unchanged.
